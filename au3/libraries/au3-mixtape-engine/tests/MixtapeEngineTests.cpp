/*
 * GlideMix -- playback engine tests (offline, no audio device)
 */
#include <chrono>
#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <wx/init.h>

#include "au3-mixtape-engine/MixtapeArrangement.h"
#include "au3-mixtape-engine/MixtapeClipVoice.h"
#include "au3-mixtape-engine/MixtapeDeck.h"
#include "au3-mixtape-engine/MixtapeDemo.h"
#include "au3-mixtape-engine/MixtapeLoudness.h"
#include "au3-mixtape-engine/MixtapeRenderer.h"
#include "au3-mixtape-engine/MixtapeTransport.h"
#include <thread>
#include <cstdlib>
#include "au3-stretching-sequence/tests/MockSampleBlockFactory.h"
#include "au3-wave-track/WaveClip.h"
#include "au3-mixer/Envelope.h"
#include "au3-mixer/PlaybackSpeedCurve.h"

using namespace mixtape;

#ifdef _WIN32
// POSIX setenv/unsetenv for the tests that switch engine options on Windows.
static int setenv(const char* name, const char* value, int)
{
    return _putenv_s(name, value);
}

static int unsetenv(const char* name)
{
    return _putenv_s(name, "");
}
#endif

namespace {
constexpr double kRate = 44100.0;
const double kPi = std::acos(-1.0);

void Require(bool condition, const std::string& message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::shared_ptr<WaveClip> SineClip(double seconds, double frequency, double startTime, size_t channels = 1)
{
    auto factory = std::make_shared<MockSampleBlockFactory>();
    auto clip = WaveClip::NewShared(channels, factory, floatSample, static_cast<int>(kRate));
    const size_t n = static_cast<size_t>(seconds * kRate);
    std::vector<float> samples(n);
    for (size_t i = 0; i < n; ++i) {
        samples[i] = static_cast<float>(0.5 * std::sin(2 * kPi * frequency * i / kRate));
    }
    std::vector<constSamplePtr> buffers(channels, reinterpret_cast<constSamplePtr>(samples.data()));
    clip->Append(buffers.data(), floatSample, n, 1, floatSample);
    clip->Flush();
    clip->SetSequenceStartTime(startTime);
    return clip;
}

std::shared_ptr<const Arrangement> OneClip(const WaveClip& clip, uint64_t generation = 1)
{
    auto arrangement = std::make_shared<Arrangement>();
    arrangement->generation = generation;
    TrackSnapshot track;
    track.trackId = 1;
    track.clips.push_back(SnapshotClip(clip));
    arrangement->tracks.push_back(std::move(track));
    return arrangement;
}

//! Renders `seconds` of stereo in blocks of `block` frames; returns channel 0,
//! lined up with the timeline (the master limiter's look-ahead taken off).
std::vector<float> RenderLeft(Renderer& renderer, double seconds, size_t block = 512,
                              const std::function<void(double)>& beforeBlock = {})
{
    const size_t lookahead = renderer.LimiterDelayFrames();
    const size_t total = static_cast<size_t>(seconds * kRate) + lookahead;
    std::vector<float> left(total), right(total);
    size_t done = 0;
    while (done < total) {
        const size_t n = std::min(block, total - done);
        if (beforeBlock) {
            beforeBlock(renderer.Position());
        }
        float* out[2] = { left.data() + done, right.data() + done };
        renderer.Render(out, n);
        done += n;
    }
    left.erase(left.begin(), left.begin() + static_cast<std::ptrdiff_t>(lookahead));
    return left;
}

//! Frequency from zero crossings over [t0, t1).
double Frequency(const std::vector<float>& signal, double t0, double t1)
{
    const size_t a = static_cast<size_t>(t0 * kRate);
    const size_t b = static_cast<size_t>(t1 * kRate);
    int crossings = 0;
    double first = -1, last = -1;
    for (size_t i = a + 1; i < b; ++i) {
        if (signal[i - 1] < 0 && signal[i] >= 0) {
            const double exact = (i - 1) + signal[i - 1] / (signal[i - 1] - signal[i]);
            if (first < 0) {
                first = exact;
            } else {
                ++crossings;
            }
            last = exact;
        }
    }
    return crossings > 0 ? crossings / ((last - first) / kRate) : 0.0;
}

//! First and last sample louder than `threshold`, in seconds.
std::pair<double, double> AudibleRange(const std::vector<float>& signal, float threshold = 0.05f)
{
    size_t first = signal.size(), last = 0;
    for (size_t i = 0; i < signal.size(); ++i) {
        if (std::abs(signal[i]) > threshold) {
            first = std::min(first, i);
            last = i;
        }
    }
    return { first / kRate, last / kRate };
}

void UnstretchedIsSampleExact()
{
    auto clip = SineClip(2.0, 440.0, 1.0);
    Renderer renderer(kRate, 2);
    renderer.SetArrangement(OneClip(*clip));
    const auto out = RenderLeft(renderer, 4.0, 333);
    const size_t start = static_cast<size_t>(kRate);
    Require(out[start - 1] == 0.0f, "audio before the clip start");
    for (size_t k = 0; k < 2 * kRate; k += 997) {
        const float expected = static_cast<float>(0.5 * std::sin(2 * kPi * 440.0 * k / kRate));
        Require(std::abs(out[start + k] - expected) < 1e-6f, "unstretched clip is not sample exact at " + std::to_string(k));
    }
    Require(out[static_cast<size_t>(3.0 * kRate) + 1] == 0.0f, "audio after the clip end");
}

void StretchKeepsPitch()
{
    auto clip = SineClip(4.0, 440.0, 1.0);
    clip->StretchBy(1.25);
    Renderer renderer(kRate, 2);
    renderer.SetArrangement(OneClip(*clip));
    const auto out = RenderLeft(renderer, 7.0);
    const double f = Frequency(out, 2.0, 5.0);
    std::cout << "  stretched x1.25: " << f << " Hz (source 440)\n";
    Require(std::abs(f - 440.0) < 1.0, "stretch changed the pitch: " + std::to_string(f));
    const auto [first, last] = AudibleRange(out);
    std::cout << "  audible " << first << " .. " << last << " s (expected 1.000 .. 6.000)\n";
    Require(std::abs(first - 1.0) < 0.02 && std::abs(last - 6.0) < 0.02, "stretched clip in the wrong place");
}

//! GlideMix: three songs, some stretched, overlapping and apart, as an export sees them.
std::shared_ptr<const Arrangement> ThreeSongs(WaveClip& a, WaveClip& b, WaveClip& c)
{
    auto arrangement = std::make_shared<Arrangement>();
    arrangement->generation = 1;
    int64_t id = 1;
    for (WaveClip* clip : { &a, &b, &c }) {
        TrackSnapshot track;
        track.trackId = id++;
        track.clips.push_back(SnapshotClip(*clip));
        arrangement->tracks.push_back(std::move(track));
    }
    return arrangement;
}

//! Both channels of `seconds` of the mix, in export's block size.
std::vector<float> RenderBoth(Renderer& renderer, double seconds)
{
    const size_t block = kRenderBlockFrames;
    const size_t total = (static_cast<size_t>(seconds * kRate) / block + 1) * block;
    std::vector<float> left(total), right(total);
    for (size_t done = 0; done < total; done += block) {
        float* out[2] = { left.data() + done, right.data() + done };
        renderer.Render(out, block);
    }
    left.insert(left.end(), right.begin(), right.end());
    return left;
}

void LossyCeilingHoldsAtMinusTwo()
{
    // Two in-phase 0.5 sines sum to full scale: well over the ceiling.
    auto clip = SineClip(2.0, 997.0, 0.0);
    auto arrangement = std::make_shared<Arrangement>();
    for (int t = 0; t < 2; ++t) {
        TrackSnapshot track;
        track.trackId = t + 1;
        track.clips.push_back(SnapshotClip(*clip));
        arrangement->tracks.push_back(track);
    }
    Renderer renderer(kRate, 2);
    renderer.SetLimitCeiling(Renderer::kLossyLimitCeiling);
    renderer.SetArrangement(arrangement);
    const auto out = RenderLeft(renderer, 2.0);
    float peak = 0.0f;
    for (const float sample : out) {
        peak = std::max(peak, std::abs(sample));
    }
    std::cout << "  peak " << 20.0 * std::log10(peak) << " dB\n";
    Require(peak <= Renderer::kLossyLimitCeiling + 1e-3f, "over the lossy ceiling");
}

void ExportMatchesOneCore()
{
    auto a = SineClip(6.0, 440.0, 0.0);
    a->StretchBy(1.25);
    auto b = SineClip(5.0, 660.0, 6.0);
    auto c = SineClip(5.0, 330.0, 10.0);
    c->StretchBy(0.8);
    const auto arrangement = ThreeSongs(*a, *b, *c);

    Renderer oneCore(kRate, 2);
    oneCore.SetArrangement(arrangement);
    const auto expected = RenderBoth(oneCore, 16.0);

    Renderer everyCore(kRate, 2);
    everyCore.SetOffline(true);
    everyCore.SetArrangement(arrangement);
    const auto actual = RenderBoth(everyCore, 16.0);

    Require(expected.size() == actual.size(), "different lengths");
    for (size_t i = 0; i < expected.size(); ++i) {
        Require(expected[i] == actual[i], "export differs from one core at sample " + std::to_string(i));
    }
}

void ExportResamplesNearItsTempo()
{
    auto clip = SineClip(4.0, 440.0, 1.0);
    clip->StretchBy(1.001);
    Renderer renderer(kRate, 2);
    renderer.SetOffline(true);
    renderer.SetArrangement(OneClip(*clip));
    const auto out = RenderLeft(renderer, 6.0, kRenderBlockFrames);
    const double f = Frequency(out, 2.0, 4.0);
    std::cout << "  stretched x1.001: " << f << " Hz (440 / 1.001 = 439.56)\n";
    Require(std::abs(f - 439.56) < 0.5, "resampled clip at the wrong pitch: " + std::to_string(f));
    const auto [first, last] = AudibleRange(out);
    std::cout << "  audible " << first << " .. " << last << " s (expected 1.000 .. 5.004)\n";
    Require(std::abs(first - 1.0) < 0.02 && std::abs(last - 5.004) < 0.02, "resampled clip in the wrong place");
}

void CentShiftChangesPitchOnly()
{
    auto clip = SineClip(3.0, 440.0, 0.0);
    clip->SetCentShift(1200);
    Renderer renderer(kRate, 2);
    renderer.SetArrangement(OneClip(*clip));
    const auto out = RenderLeft(renderer, 3.5);
    const double f = Frequency(out, 0.5, 2.5);
    std::cout << "  +1200 cents: " << f << " Hz (expected 880)\n";
    Require(std::abs(f - 880.0) < 2.0, "cent shift wrong: " + std::to_string(f));
    const auto [first, last] = AudibleRange(out);
    Require(last > 2.95 && last < 3.02, "cent shift changed the length");
}

void EditWhilePlayingTakesEffectNextBlock()
{
    auto clip = SineClip(4.0, 440.0, 1.0);
    Renderer renderer(kRate, 2);
    renderer.SetArrangement(OneClip(*clip, 1));
    bool moved = false;
    const auto out = RenderLeft(renderer, 5.0, 512, [&](double position) {
        if (!moved && position >= 1.5) {
            clip->SetSequenceStartTime(1.25);   // move 0.25 s later, like dragging a clip
            renderer.SetArrangement(OneClip(*clip, 2));
            moved = true;
        }
    });
    Require(moved, "edit never happened");
    // After the edit, timeline t plays source second (t - 1.25).
    const size_t at = static_cast<size_t>(2.0 * kRate);
    const double k = at - 1.25 * kRate;
    const float expected = static_cast<float>(0.5 * std::sin(2 * kPi * 440.0 * k / kRate));
    Require(std::abs(out[at] - expected) < 1e-6f, "edit not heard at the next block");
    Require(out[static_cast<size_t>(5.0 * kRate) - 1] != 0.0f, "moved clip should still play at 5 s");
}

//! A clip playing a low tone and a high one at once, each at amplitude 0.4.
std::shared_ptr<WaveClip> TwoToneClip(double seconds, double lowHz, double highHz, double startTime)
{
    auto factory = std::make_shared<MockSampleBlockFactory>();
    auto clip = WaveClip::NewShared(1, factory, floatSample, static_cast<int>(kRate));
    const size_t n = static_cast<size_t>(seconds * kRate);
    std::vector<float> samples(n);
    for (size_t i = 0; i < n; ++i) {
        samples[i] = static_cast<float>(0.4 * std::sin(2 * kPi * lowHz * i / kRate) + 0.4 * std::sin(2 * kPi * highHz * i / kRate));
    }
    std::vector<constSamplePtr> buffers(1, reinterpret_cast<constSamplePtr>(samples.data()));
    clip->Append(buffers.data(), floatSample, n, 1, floatSample);
    clip->Flush();
    clip->SetSequenceStartTime(startTime);
    return clip;
}

//! Amplitude of the `hz` component over [t0, t1) (projection on a sine and a cosine).
double ToneAmplitude(const std::vector<float>& signal, double hz, double t0, double t1)
{
    const size_t a = static_cast<size_t>(t0 * kRate);
    const size_t b = static_cast<size_t>(t1 * kRate);
    double c = 0, s = 0;
    for (size_t i = a; i < b; ++i) {
        const double phase = 2 * kPi * hz * i / kRate;
        c += signal[i] * std::cos(phase);
        s += signal[i] * std::sin(phase);
    }
    return 2.0 * std::sqrt(c * c + s * s) / static_cast<double>(b - a);
}

//! A line through (time, value) points, in absolute timeline seconds.
std::shared_ptr<const Envelope> Line(std::initializer_list<std::pair<double, double> > points)
{
    auto envelope = std::make_shared<Envelope>(false, 0.0, 1.0, 1.0);
    for (const auto& [time, value] : points) {
        envelope->Insert(time, value);
    }
    return envelope;
}

std::shared_ptr<const Arrangement> OneSnapshot(ClipSnapshot snapshot, uint64_t generation)
{
    auto arrangement = std::make_shared<Arrangement>();
    arrangement->generation = generation;
    TrackSnapshot track;
    track.trackId = 1;
    track.clips.push_back(std::move(snapshot));
    arrangement->tracks.push_back(std::move(track));
    return arrangement;
}

double LargestJump(const std::vector<float>& signal)
{
    double largest = 0;
    for (size_t i = 1; i < signal.size(); ++i) {
        largest = std::max(largest, static_cast<double>(std::abs(signal[i] - signal[i - 1])));
    }
    return largest;
}

void BassLineRemovesTheBassAndLeavesTheHighs()
{
    auto clip = TwoToneClip(4.0, 60.0, 2000.0, 0.0);
    ClipSnapshot snapshot = SnapshotClip(*clip);
    snapshot.lowBand = Line({ { 0.0, 1.0 }, { 1.0, 1.0 }, { 1.02, 0.0 }, { 4.0, 0.0 } });
    Renderer renderer(kRate, 2);
    renderer.SetArrangement(OneSnapshot(snapshot, 1));
    const auto out = RenderLeft(renderer, 3.5);
    const double lowBefore = ToneAmplitude(out, 60.0, 0.3, 0.9);
    const double highBefore = ToneAmplitude(out, 2000.0, 0.3, 0.9);
    const double lowAfter = ToneAmplitude(out, 60.0, 1.5, 3.4);
    const double highAfter = ToneAmplitude(out, 2000.0, 1.5, 3.4);
    std::cout << "  60 Hz: " << lowBefore << " -> " << lowAfter << "   2 kHz: " << highBefore << " -> " << highAfter << '\n';
    Require(std::abs(lowBefore - 0.4) < 0.01, "at gain 1 the bass must be untouched");
    Require(std::abs(highBefore - 0.4) < 0.01, "the highs must be untouched");
    Require(lowAfter < 0.004, "at gain 0 the bass must be gone (40 dB down or more)");
    Require(std::abs(highAfter - 0.4) < 0.01, "removing the bass must not touch the highs");
}

void MidAndHighLinesCutTheirBandsOnly()
{
    // 60 Hz is bass, 800 Hz mids, 6 kHz highs: the mid line cuts only the
    // 800 Hz tone, the high line only the 6 kHz one; before, all are untouched.
    auto clip = TwoToneClip(4.0, 800.0, 6000.0, 0.0);
    ClipSnapshot snapshot = SnapshotClip(*clip);
    snapshot.midBand = Line({ { 0.0, 1.0 }, { 1.0, 1.0 }, { 1.02, 0.0 }, { 4.0, 0.0 } });
    Renderer renderer(kRate, 2);
    renderer.SetArrangement(OneSnapshot(snapshot, 1));
    const auto out = RenderLeft(renderer, 3.5);
    const double midBefore = ToneAmplitude(out, 800.0, 0.3, 0.9);
    const double highBefore = ToneAmplitude(out, 6000.0, 0.3, 0.9);
    const double midAfter = ToneAmplitude(out, 800.0, 1.5, 3.4);
    const double highAfter = ToneAmplitude(out, 6000.0, 1.5, 3.4);
    std::cout << "  800 Hz: " << midBefore << " -> " << midAfter << "   6 kHz: " << highBefore << " -> " << highAfter << '\n';
    Require(std::abs(midBefore - 0.4) < 0.01, "at gain 1 the mids must be untouched");
    Require(std::abs(highBefore - 0.4) < 0.01, "the highs must be untouched");
    Require(midAfter < 0.02, "at gain 0 the mids must be cut");
    Require(std::abs(highAfter - 0.4) < 0.03, "cutting the mids must leave the highs");

    ClipSnapshot high = SnapshotClip(*clip);
    high.highBand = Line({ { 0.0, 0.0 }, { 4.0, 0.0 } });
    Renderer renderer2(kRate, 2);
    renderer2.SetArrangement(OneSnapshot(high, 1));
    const auto out2 = RenderLeft(renderer2, 2.0);
    const double mid2 = ToneAmplitude(out2, 800.0, 0.5, 1.8);
    const double high2 = ToneAmplitude(out2, 6000.0, 0.5, 1.8);
    std::cout << "  high cut: 800 Hz " << mid2 << ", 6 kHz " << high2 << '\n';
    Require(high2 < 0.02, "at gain 0 the highs must be cut");
    Require(std::abs(mid2 - 0.4) < 0.03, "cutting the highs must leave the mids");
}

void BassLineAppearingOnAPlayingClipBlendsInWithoutAClick()
{
    // The clip has been playing with no bass line; one appears mid-playback (a transition applied by
    // Mix Now). The crossover shifts the phase of the bass, so switching it on hard would click.
    auto clip = TwoToneClip(6.0, 60.0, 2000.0, 0.0);
    Renderer renderer(kRate, 2);
    renderer.SetArrangement(OneClip(*clip, 1));
    bool published = false;
    const auto out = RenderLeft(renderer, 5.0, 512, [&](double position) {
        if (!published && position >= 1.0) {
            ClipSnapshot snapshot = SnapshotClip(*clip);
            snapshot.lowBand = Line({ { 0.0, 1.0 }, { 2.0, 1.0 }, { 2.02, 0.0 }, { 6.0, 0.0 } });
            renderer.SetArrangement(OneSnapshot(snapshot, 2));
            published = true;
        }
    });
    Require(published, "the bass line was never published");
    const double lowBefore = ToneAmplitude(out, 60.0, 0.3, 0.9);
    const double lowBlended = ToneAmplitude(out, 60.0, 1.3, 1.9);
    const double lowAfter = ToneAmplitude(out, 60.0, 2.5, 4.9);
    const double highAfter = ToneAmplitude(out, 2000.0, 2.5, 4.9);
    const double jump = LargestJump(out);
    std::cout << "  60 Hz: " << lowBefore << " -> " << lowBlended << " -> " << lowAfter << "   2 kHz after: " << highAfter
              << "   largest sample step " << jump << " (the 2 kHz tone alone reaches " << 0.4 * 2 * kPi * 2000.0 / kRate << ")\n";
    Require(std::abs(lowBefore - 0.4) < 0.01, "before the line appears the clip must play as it did");
    Require(std::abs(lowBlended - 0.4) < 0.02, "with the line at gain 1 the bass level must be unchanged");
    Require(lowAfter < 0.004, "the bass line published on a playing clip must be heard");
    Require(std::abs(highAfter - 0.4) < 0.01, "the highs must not change");
    // No step beyond what the two tones make on their own (a click would show as a much larger one).
    Require(jump < 0.16, "switching the bass stage on must not click");
}

void VolumeLineEditedOnAPlayingClipIsHeard()
{
    // Editing the volume line of a clip that is already playing used to be ignored: only a move or a
    // change of the audio re-assigned the voice. A transition written during a performance needs it.
    auto clip = SineClip(6.0, 440.0, 0.0);
    Renderer renderer(kRate, 2);
    renderer.SetArrangement(OneClip(*clip, 1));
    bool published = false;
    const auto out = RenderLeft(renderer, 5.0, 512, [&](double position) {
        if (!published && position >= 1.0) {
            ClipSnapshot snapshot = SnapshotClip(*clip);
            snapshot.envelope = Line({ { 0.0, 1.0 }, { 2.0, 1.0 }, { 2.02, 0.25 }, { 6.0, 0.25 } });
            renderer.SetArrangement(OneSnapshot(snapshot, 2));
            published = true;
        }
    });
    Require(published, "the volume line was never published");
    const double before = ToneAmplitude(out, 440.0, 0.3, 0.9);
    const double after = ToneAmplitude(out, 440.0, 2.5, 4.9);
    std::cout << "  440 Hz: " << before << " -> " << after << " (expected 0.5 -> 0.125)\n";
    Require(std::abs(before - 0.5) < 0.01, "the clip must play at full volume before the edit");
    Require(std::abs(after - 0.125) < 0.005, "the edited volume line must be heard on a clip that was already playing");
}

void AddedTrackJoinsWithoutRestart()
{
    auto first = SineClip(4.0, 440.0, 0.0);
    auto second = SineClip(4.0, 660.0, 0.0);
    Renderer renderer(kRate, 2);
    renderer.SetArrangement(OneClip(*first, 1));
    bool added = false;
    const auto out = RenderLeft(renderer, 3.0, 512, [&](double position) {
        if (!added && position >= 1.0) {
            auto arrangement = std::make_shared<Arrangement>(*OneClip(*first, 2));
            TrackSnapshot track;
            track.trackId = 2;
            track.clips.push_back(SnapshotClip(*second));
            arrangement->tracks.push_back(track);
            renderer.SetArrangement(arrangement);
            added = true;
        }
    });
    const size_t at = static_cast<size_t>(2.0 * kRate);
    const float expected = static_cast<float>(0.5 * std::sin(2 * kPi * 440.0 * at / kRate)
                                              + 0.5 * std::sin(2 * kPi * 660.0 * at / kRate));
    Require(std::abs(out[at] - expected) < 1e-5f, "new track not mixed in");
}

void MuteSoloPan()
{
    auto clip = SineClip(1.0, 440.0, 0.0);
    auto arrangement = std::make_shared<Arrangement>(*OneClip(*clip));
    arrangement->tracks[0].pan = 1.0f;   // hard right
    Renderer renderer(kRate, 2);
    renderer.SetArrangement(arrangement);
    const auto left = RenderLeft(renderer, 1.0);
    Require(AudibleRange(left).first > 0.99, "pan right still plays left");
}

void StretchedVoicesCpu()
{
    auto arrangement = std::make_shared<Arrangement>();
    std::vector<std::shared_ptr<WaveClip> > clips;
    for (int t = 0; t < 8; ++t) {
        auto clip = SineClip(40.0, 220.0 * (t + 1), 0.0, 2);
        clip->StretchBy(155.0 / 127.0);
        clips.push_back(clip);
        TrackSnapshot track;
        track.trackId = t + 1;
        track.clips.push_back(SnapshotClip(*clip));
        arrangement->tracks.push_back(track);
    }
    Renderer renderer(kRate, 2);
    renderer.SetArrangement(arrangement);
    const auto started = std::chrono::steady_clock::now();
    RenderLeft(renderer, 30.0, 512);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    std::cout << "  8 stereo stretched clips, 30 s rendered in " << seconds << " s ("
              << (30.0 / seconds) << "x realtime)\n";
    Require(seconds < 15.0, "too slow for playback");
}

void DevicePlaysSilentlyWithoutDropouts()
{
    if (!std::getenv("MIXTAPE_TEST_DEVICE")) {
        std::cout << "  SKIP (set MIXTAPE_TEST_DEVICE=1 to open the audio device)\n";
        return;
    }
    // Stretched stereo clips at zero gain: exercises the real device path
    // without making a sound.
    auto arrangement = std::make_shared<Arrangement>();
    std::vector<std::shared_ptr<WaveClip> > clips;
    for (int t = 0; t < 4; ++t) {
        auto clip = SineClip(10.0, 330.0, 0.0, 2);
        clip->StretchBy(155.0 / 127.0);
        clips.push_back(clip);
        TrackSnapshot track;
        track.trackId = t + 1;
        track.volume = 0.0f;
        track.clips.push_back(SnapshotClip(*clip));
        arrangement->tracks.push_back(track);
    }
    Transport transport;
    Transport::Options options;
    options.sampleRate = kRate;
    Require(transport.Start(options, arrangement, 1.0), "transport did not start: " + transport.LastError());
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    const double moved = transport.Position() - 1.0;
    const auto steadyUnderruns = transport.Underruns();
    transport.Seek(5.0);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    const double afterSeek = transport.Position();
    const auto underruns = transport.Underruns();
    transport.Stop();
    std::cout << "  played " << moved << " s in 1.5 s, after seek at " << afterSeek << " s, underruns while playing "
              << steadyUnderruns << ", after the seek " << (underruns - steadyUnderruns) << "\n";
    Require(moved > 1.3 && moved < 1.6, "playhead did not follow the device");
    Require(afterSeek > 5.3 && afterSeek < 5.6, "seek not applied");
    Require(steadyUnderruns == 0, "dropouts while playing");
}

std::shared_ptr<const PlaybackSpeedCurve> Curve(std::vector<std::pair<double, double> > knots)
{
    auto curve = std::make_shared<PlaybackSpeedCurve>();
    curve->knots = std::move(knots);
    return curve;
}

void TempoCurveKeepsPitch()
{
    // 128 -> 155 BPM ramp over timeline 2..6 s, then held: the tone must stay 440 Hz.
    auto clip = SineClip(20.0, 440.0, 0.0);
    Renderer renderer(kRate, 2);
    renderer.SetArrangement(OneClip(*clip));
    const auto curve = Curve({ { 2.0, 1.0 }, { 6.0, 155.0 / 128.0 } });
    renderer.SetSpeedCurve(curve);
    const auto out = RenderLeft(renderer, 10.0);
    double worst = 0;
    for (double t = 0.5; t < 9.5; t += 0.5) {
        const double f = Frequency(out, t, t + 0.5);
        worst = std::max(worst, std::abs(f - 440.0));
    }
    std::cout << "  ramp 128->155: tone off by at most " << worst << " Hz over 9 s\n";
    Require(worst < 2.0, "pitch moved on the tempo ramp");
    // RenderLeft renders the limiter's look-ahead on top of the 10 s.
    const double expectedTimeline = curve->SolveIntegralOfInverse(0.0, 10.0 + renderer.LimiterDelayFrames() / kRate);
    std::cout << "  playhead after 10 s real: " << renderer.Position() << " (curve says " << expectedTimeline << ")\n";
    Require(std::abs(renderer.Position() - expectedTimeline) < 1e-6, "playhead does not follow the curve");
}

void LatencyCompensationHoldsOtherTracksBack()
{
    // Two identical clips; the second track "has effects" 441 frames late. The
    // first must come out 441 frames later so the two stay in step.
    auto clip = SineClip(2.0, 440.0, 0.0);
    auto arrangement = std::make_shared<Arrangement>();
    for (int t = 0; t < 2; ++t) {
        TrackSnapshot track;
        track.trackId = t + 1;
        track.clips.push_back(SnapshotClip(*clip));
        track.latencyFrames = t == 1 ? 441 : 0;
        arrangement->tracks.push_back(track);
    }
    Renderer delayed(kRate, 2);
    delayed.SetArrangement(arrangement);
    const auto out = RenderLeft(delayed, 1.0);
    auto plain = std::make_shared<Arrangement>(*arrangement);
    plain->tracks.pop_back();
    Renderer reference(kRate, 2);
    reference.SetArrangement(plain);
    const auto one = RenderLeft(reference, 1.0);
    // Output = first track delayed 441 frames + second track undelayed.
    double worst = 0;
    for (size_t i = 1000; i < 40000; ++i) {
        const float expected = one[i - 441] + one[i];
        worst = std::max(worst, double(std::abs(out[i] - expected)));
    }
    std::cout << "  output delay " << delayed.OutputDelayFrames() << " frames, worst mismatch " << worst << "\n";
    Require(delayed.OutputDelayFrames() == 441 + delayed.LimiterDelayFrames(), "output delay not reported");
    Require(worst < 1e-4, "the track without effects was not held back by the latency");
}

void KeylockOffPitchFollowsSpeed()
{
    // Same ramp with keylock off: after it the tone sits 155/128 higher, like a turntable.
    SetKeylock(false);
    auto clip = SineClip(20.0, 440.0, 0.0);
    Renderer renderer(kRate, 2);
    renderer.SetArrangement(OneClip(*clip));
    renderer.SetSpeedCurve(Curve({ { 2.0, 1.0 }, { 6.0, 155.0 / 128.0 } }));
    const auto out = RenderLeft(renderer, 10.0);
    SetKeylock(true);
    const double before = Frequency(out, 0.5, 1.5);
    const double after = Frequency(out, 8.0, 9.0);
    const double expected = 440.0 * 155.0 / 128.0;
    std::cout << "  keylock off: " << before << " Hz before the ramp, " << after << " Hz after (expected " << expected << ")\n";
    Require(std::abs(before - 440.0) < 2.0, "pitch wrong before the ramp");
    Require(std::abs(after - expected) < 3.0, "pitch did not follow the speed");
}

void TempoCurveKeepsBeatsInPlace()
{
    // A click every 0.5 s of source; under a ramp each click must be heard
    // when the curve says that timeline second is played.
    auto factory = std::make_shared<MockSampleBlockFactory>();
    auto clip = WaveClip::NewShared(1, factory, floatSample, static_cast<int>(kRate));
    const size_t n = static_cast<size_t>(20.0 * kRate);
    std::vector<float> samples(n, 0.0f);
    for (size_t beat = 0; beat * kRate / 2 < n; ++beat) {
        const size_t at = static_cast<size_t>(beat * kRate / 2);
        for (size_t k = 0; k < 64 && at + k < n; ++k) {
            samples[at + k] = static_cast<float>(0.9 * std::exp(-double(k) / 12.0) * ((k % 2) ? -1 : 1));
        }
    }
    constSamplePtr buffers[] = { reinterpret_cast<constSamplePtr>(samples.data()) };
    clip->Append(buffers, floatSample, n, 1, floatSample);
    clip->Flush();
    Renderer renderer(kRate, 2);
    renderer.SetArrangement(OneClip(*clip));
    const auto curve = Curve({ { 2.0, 1.0 }, { 6.0, 155.0 / 128.0 } });
    renderer.SetSpeedCurve(curve);
    const auto out = RenderLeft(renderer, 12.0);
    double worstMs = 0;
    int checked = 0;
    for (double beatTime = 1.0; beatTime < 12.0; beatTime += 0.5) {
        const double realTime = curve->IntegralOfInverse(0.0, beatTime);
        if (realTime > 11.8) {
            break;
        }
        // Loudest sample within +-40 ms of the predicted time.
        size_t best = 0;
        float peak = 0;
        const size_t from = static_cast<size_t>((realTime - 0.04) * kRate);
        const size_t to = static_cast<size_t>((realTime + 0.04) * kRate);
        for (size_t i = from; i < to && i < out.size(); ++i) {
            if (std::abs(out[i]) > peak) {
                peak = std::abs(out[i]);
                best = i;
            }
        }
        worstMs = std::max(worstMs, std::abs(best / kRate - realTime) * 1000.0);
        if (std::getenv("MIXTAPE_TEST_VERBOSE")) {
            std::cout << "    beat " << beatTime << " s: " << (best / kRate - realTime) * 1000.0 << " ms\n";
        }
        ++checked;
    }
    std::cout << "  " << checked << " clicks through the ramp: worst " << worstMs << " ms from where the curve puts them\n";
    Require(worstMs < 3.0, "beats drift from the tempo curve");
}

void LoopWrapsAtTheLoopEnd()
{
    auto clip = SineClip(10.0, 440.0, 0.0);
    Renderer renderer(kRate, 2);
    renderer.SetArrangement(OneClip(*clip));
    renderer.SetLoop(true, 1.0, 1.5);
    renderer.Seek(1.0);
    const size_t total = static_cast<size_t>(2.2 * kRate);
    std::vector<float> left(total), right(total);
    double maxPosition = 0;
    for (size_t done = 0; done < total; done += 256) {
        float* out[2] = { left.data() + done, right.data() + done };
        renderer.RenderLooped(out, std::min<size_t>(256, total - done));
        maxPosition = std::max(maxPosition, renderer.Position());
    }
    // 2.2 s of real time in a 0.5 s loop from 1.0: ends at 1.0 + 0.2 = 1.2.
    std::cout << "  playhead after 2.2 s: " << renderer.Position() << " (expected 1.2), never past " << maxPosition << "\n";
    Require(std::abs(renderer.Position() - 1.2) < 0.01, "loop position wrong");
    Require(maxPosition <= 1.5 + 1e-9, "played past the loop end");
    // Sample 0.6 s in is 0.1 s into the second pass: source second 1.1.
    const size_t at = static_cast<size_t>(0.6 * kRate) + 50 + renderer.LimiterDelayFrames();
    const float expected = static_cast<float>(0.5 * std::sin(2 * kPi * 440.0 * (1.1 * kRate + 50) / kRate));
    Require(std::abs(left[at] - expected) < 1e-3f, "second loop pass does not replay the loop start");
}

// Deck: a ramp track (sample value = its own second) makes positions readable from the output.
std::shared_ptr<DeckAudio> RampAudio(double seconds)
{
    auto audio = std::make_shared<DeckAudio>();
    audio->channels = 2;
    audio->sampleRate = kRate;
    const size_t n = static_cast<size_t>(seconds * kRate);
    audio->samples.resize(n * 2);
    for (size_t i = 0; i < n; ++i) {
        audio->samples[2 * i] = audio->samples[2 * i + 1] = static_cast<float>(i / kRate);
    }
    return audio;
}

double RunDeck(Deck& deck, double seconds, std::vector<float>* last = nullptr)
{
    std::vector<float> out(512 * 2);
    const size_t blocks = static_cast<size_t>(seconds * kRate / 512);
    for (size_t b = 0; b < blocks; ++b) {
        deck.OnCallback(out.data(), 512);
    }
    if (last) {
        *last = out;
    }
    return out[1022];   // the block's last left sample: the second it played
}

void DeckLoopsAndSlipsBack()
{
    Deck deck;
    Require(deck.Load(RampAudio(20.0)), "deck load");
    deck.SetSlip(true);
    deck.Seek(2.0);
    deck.SetPlaying(true);
    RunDeck(deck, 1.0);                     // now ~3.0
    deck.SetLoop(true, 3.0, 3.5);
    const double inLoop = RunDeck(deck, 2.0);
    std::cout << "  inside the loop: " << inLoop << " (3.0..3.5)\n";
    Require(inLoop >= 3.0 && inLoop < 3.5, "loop did not hold");
    deck.SetLoop(false, 0, 0);
    const double after = RunDeck(deck, 0.02);
    // Slip: the track kept time for the 2 s of looping, so it comes back near 5.0.
    std::cout << "  after the loop with slip: " << after << " (about 5.0)\n";
    Require(std::abs(after - 5.0) < 0.05, "slip did not return to where the track would be");

    deck.SetSlip(false);
    deck.SetLoop(true, 6.0, 6.5);
    deck.Seek(6.0);
    RunDeck(deck, 2.0);
    deck.SetLoop(false, 0, 0);
    const double noSlip = RunDeck(deck, 0.02);
    std::cout << "  after the loop without slip: " << noSlip << " (inside 6.0..6.6)\n";
    Require(noSlip >= 6.0 && noSlip < 6.6, "without slip the loop should just carry on");
}

void DeckMetronomeClicksOnBeats()
{
    auto audio = std::make_shared<DeckAudio>();
    audio->channels = 1;
    audio->sampleRate = kRate;
    audio->samples.assign(static_cast<size_t>(4 * kRate), 0.0f);
    Deck deck;
    Require(deck.Load(audio), "deck load");
    deck.SetMetronome(true, 120.0, 0.25, 0.25);   // beats at 0.25, 0.75, 1.25 ...
    deck.SetPlaying(true);
    std::vector<float> out(static_cast<size_t>(2 * kRate) * 2);
    deck.OnCallback(out.data(), static_cast<unsigned long>(2 * kRate));
    const auto energy = [&](double from, double to) {
        double e = 0;
        for (size_t i = static_cast<size_t>(from * kRate); i < static_cast<size_t>(to * kRate); ++i) {
            e += out[2 * i] * out[2 * i];
        }
        return e;
    };
    Require(energy(0.25, 0.28) > 1.0, "no click on the first beat");
    Require(energy(0.30, 0.74) < 1e-6, "sound between beats");
    Require(energy(0.75, 0.78) > 1.0, "no click on the second beat");
}
}


//! The small-machine resampler keeps the highs clean: a 15 kHz tone from a
//! 44.1 kHz clip on a 48 kHz output, with everything that is not the tone at
//! least 60 dB down (the old Hermite interpolation left only 12 dB).
void LoudnessMeterMeasuresSineAndPeaks()
{
    // EBU Tech 3341's test 1: a 1 kHz sine at -23 dBFS on both channels of a
    // stereo file reads -23 LUFS; here at -20 dBFS, so -20 LUFS.
    constexpr double rate = 48000.0;
    const size_t frames = static_cast<size_t>(10.0 * rate);
    const double amplitude = std::pow(10.0, -20.0 / 20.0);
    std::vector<float> left(frames), right(frames);
    for (size_t i = 0; i < frames; ++i) {
        left[i] = right[i] = static_cast<float>(amplitude * std::sin(2 * kPi * 1000.0 * i / rate));
    }
    LoudnessMeter meter(rate, 2);
    for (size_t at = 0; at < frames; at += 1024) {
        const size_t n = std::min<size_t>(1024, frames - at);
        const float* ch[] = { left.data() + at, right.data() + at };
        meter.Process(ch, n);
    }
    const LoudnessReport report = meter.Report();
    std::cout << "  " << report.integratedLufs << " LUFS, true peak " << report.truePeakDb << " dBTP, peak "
              << report.samplePeakDb << " dBFS\n";
    Require(std::abs(report.integratedLufs - -20.0) < 0.1, "a -20 dBFS stereo 1 kHz sine should read -20 LUFS");
    Require(std::abs(report.samplePeakDb - -20.0) < 0.1, "sample peak of a -20 dBFS sine");
    Require(std::abs(report.truePeakDb - -20.0) < 0.2, "true peak of a slow sine equals its peak");
    Require(std::abs(report.seconds - 10.0) < 1e-6, "length");

    // A sine at fs/4 sampled 45 degrees off its peaks: every sample is at
    // 0.707 of the real peak, which only the oversampled meter sees (+3 dB).
    std::vector<float> off(frames);
    for (size_t i = 0; i < frames; ++i) {
        off[i] = static_cast<float>(0.5 * std::sin(2 * kPi * (rate / 4) * i / rate + kPi / 4));
    }
    LoudnessMeter inter(rate, 1);
    const float* ch[] = { off.data() };
    inter.Process(ch, frames);
    const LoudnessReport between = inter.Report();
    std::cout << "  between samples: true peak " << between.truePeakDb << " dBTP, peak " << between.samplePeakDb << " dBFS\n";
    Require(std::abs(between.truePeakDb - 20 * std::log10(0.5)) < 0.3, "true peak finds the peak between samples");
    Require(between.truePeakDb - between.samplePeakDb > 2.5, "true peak above sample peak");
}

void LimiterHoldsTruePeakUnderCeiling()
{
    // Two in-phase 0.5 sines at 10 kHz sum to a full-scale tone whose peaks
    // fall mostly between the samples: the limiter must hold the true peak
    // (what an MP3 encoder sees) at -1 dBTP, not just the samples.
    auto clip = SineClip(4.0, 10000.0, 0.0);
    auto arrangement = std::make_shared<Arrangement>();
    for (int t = 0; t < 2; ++t) {
        TrackSnapshot track;
        track.trackId = t + 1;
        track.clips.push_back(SnapshotClip(*clip));
        arrangement->tracks.push_back(track);
    }
    Renderer renderer(kRate, 2);
    renderer.SetArrangement(arrangement);
    const auto out = RenderLeft(renderer, 3.5);
    // The whole output from its first frame: starting the meter mid-tone
    // would itself read as a jump from silence, a peak that isn't there.
    LoudnessMeter meter(kRate, 1);
    const float* ch[] = { out.data() };
    meter.Process(ch, out.size());
    const LoudnessReport report = meter.Report();
    std::cout << "  limited: true peak " << report.truePeakDb << " dBTP, sample peak " << report.samplePeakDb
              << " dBFS, look-ahead " << renderer.LimiterDelayFrames() << " frames\n";
    Require(report.truePeakDb <= -1.0, "true peak above -1 dBTP");
    Require(report.truePeakDb >= -1.6, "limited far more than needed");
}

void DemoVoiceTagsEveryOutput()
{
    // A loud tone through the renderer in demo mode: the voice is mixed in at
    // 30 s with the music ducked under it, nothing clips, and away from the
    // tag the output is untouched.
    auto clip = SineClip(40.0, 440.0, 0.0);
    auto arrangement = std::make_shared<Arrangement>();
    for (int t = 0; t < 2; ++t) {   // two 0.5 tones: the limiter keeps it at the ceiling
        TrackSnapshot track;
        track.trackId = t + 1;
        track.clips.push_back(SnapshotClip(*clip));
        arrangement->tracks.push_back(track);
    }
    Renderer plain(kRate, 2);
    plain.SetArrangement(arrangement);
    const auto clean = RenderLeft(plain, 34.0);
    SetDemoMode(true);
    Renderer demo(kRate, 2);
    demo.SetArrangement(arrangement);
    const auto tagged = RenderLeft(demo, 34.0);
    SetDemoMode(false);
    const auto window = [&](double t0, double t1) {
        double diff = 0.0, peak = 0.0;
        for (size_t i = static_cast<size_t>(t0 * kRate); i < static_cast<size_t>(t1 * kRate); ++i) {
            diff = std::max(diff, double(std::abs(tagged[i] - clean[i])));
            peak = std::max(peak, double(std::abs(tagged[i])));
        }
        return std::make_pair(diff, peak);
    };
    const auto before = window(5.0, 29.0);
    const auto during = window(30.2, 30.0 + DemoVoiceSeconds());
    std::cout << "  before the tag: max change " << before.first << "; during: change " << during.first
              << ", peak " << during.second << " (voice " << DemoVoiceSeconds() << " s)\n";
    Require(before.first < 1e-6, "demo changed the sound away from the tag");
    Require(during.first > 0.3, "no voice tag at 30 s");
    Require(during.second < 1.0, "the tag clips");
}

void ResamplerKeepsHighsClean()
{
    constexpr double outRate = 48000.0;
    constexpr double tone = 15000.0;
    auto clip = SineClip(3.0, tone, 1.0);
    setenv("MIXTAPE_RESAMPLE", "1", 1);
    Renderer renderer(outRate, 2);
    renderer.SetArrangement(OneClip(*clip));
    const size_t total = static_cast<size_t>(4.0 * outRate);
    std::vector<float> left(total), right(total);
    for (size_t done = 0; done < total;) {
        const size_t n = std::min<size_t>(512, total - done);
        float* out[2] = { left.data() + done, right.data() + done };
        renderer.Render(out, n);
        done += n;
    }
    unsetenv("MIXTAPE_RESAMPLE");
    // Least-squares fit of the 15 kHz tone over 2 s of steady output; the
    // rest is distortion.
    const size_t a = static_cast<size_t>(1.5 * outRate), b = static_cast<size_t>(3.5 * outRate);
    double ss = 0, sc = 0, cc = 0, ys = 0, yc = 0, yy = 0;
    for (size_t i = a; i < b; ++i) {
        const double w = 2 * M_PI * tone * i / outRate;
        const double s = std::sin(w), c = std::cos(w), y = left[i];
        ss += s * s; sc += s * c; cc += c * c; ys += y * s; yc += y * c; yy += y * y;
    }
    const double det = ss * cc - sc * sc;
    const double A = (ys * cc - yc * sc) / det, B = (yc * ss - ys * sc) / det;
    const double fitted = A * ys + B * yc;
    const double residualDb = 10 * std::log10(std::max(1e-20, (yy - fitted) / fitted));
    std::cout << "  15 kHz through the resampler: distortion " << residualDb << " dB\n";
    Require(residualDb < -60.0, "the resampler dulls or distorts the highs");
}

//! A 44.1 kHz clip on a 48 kHz output at stretch 1: pitch, place and level hold
//! both through Rubber Band and through the small-machine resampler
//! (MIXTAPE_RESAMPLE), and the resampler is the cheaper of the two.
void RateMismatchResamples()
{
    constexpr double outRate = 48000.0;
    auto clip = SineClip(3.0, 440.0, 1.0);
    const auto run = [&](bool resample, double* seconds) {
        if (resample) {
            setenv("MIXTAPE_RESAMPLE", "1", 1);
        } else {
            unsetenv("MIXTAPE_RESAMPLE");
        }
        Renderer renderer(outRate, 2);
        renderer.SetArrangement(OneClip(*clip));
        const size_t total = static_cast<size_t>(5.0 * outRate);
        std::vector<float> left(total), right(total);
        const auto t0 = std::chrono::steady_clock::now();
        for (size_t done = 0; done < total;) {
            const size_t n = std::min<size_t>(512, total - done);
            float* out[2] = { left.data() + done, right.data() + done };
            renderer.Render(out, n);
            done += n;
        }
        *seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        unsetenv("MIXTAPE_RESAMPLE");
        return left;
    };
    const auto check = [&](const std::vector<float>& out, const char* what) {
        int crossings = 0;
        double first = -1, last = -1;
        for (size_t i = static_cast<size_t>(1.5 * outRate); i < static_cast<size_t>(3.5 * outRate); ++i) {
            if (out[i - 1] < 0 && out[i] >= 0) {
                const double exact = (i - 1) + out[i - 1] / (out[i - 1] - out[i]);
                first = first < 0 ? exact : first;
                last = exact;
                ++crossings;
            }
        }
        const double hz = (crossings - 1) / ((last - first) / outRate);
        float peak = 0.0f;
        size_t onset = out.size();
        for (size_t i = 0; i < out.size(); ++i) {
            peak = std::max(peak, std::abs(out[i]));
            if (onset == out.size() && std::abs(out[i]) > 0.05f) {
                onset = i;
            }
        }
        std::cout << "  " << what << ": " << hz << " Hz, peak " << peak << ", starts at " << onset / outRate << " s\n";
        Require(std::abs(hz - 440.0) < 0.5, std::string(what) + ": pitch moved");
        Require(peak > 0.45f && peak < 0.56f, std::string(what) + ": level changed");
        Require(std::abs(onset / outRate - 1.0) < 0.01, std::string(what) + ": not in place");
    };
    double stretcherSec = 0.0, resampleSec = 0.0;
    check(run(false, &stretcherSec), "Rubber Band");
    check(run(true, &resampleSec), "resampler");
    std::cout << "  5 s rendered: Rubber Band " << stretcherSec * 1000 << " ms, resampler " << resampleSec * 1000 << " ms\n";
    Require(resampleSec < stretcherSec, "the resampler is not cheaper");
}

//! The common DJ case: tracks at (nearly) their own tempo, 44.1 kHz files on
//! a 48 kHz output. Reports how much of one core the render needs.
void OwnTempoVoicesCpu()
{
    auto arrangement = std::make_shared<Arrangement>();
    std::vector<std::shared_ptr<WaveClip> > clips;
    for (int t = 0; t < 4; ++t) {
        auto clip = SineClip(40.0, 220.0 * (t + 1), 0.0, 2);
        if (!std::getenv("TEST_NO_STRETCH")) {
            clip->StretchBy(1.0 + 0.0001 * t);
        }
        clips.push_back(clip);
        TrackSnapshot track;
        track.trackId = t + 1;
        track.clips.push_back(SnapshotClip(*clip));
        arrangement->tracks.push_back(track);
    }
    const double outRate = std::getenv("TEST_RATE") ? std::atof(std::getenv("TEST_RATE")) : 48000.0;
    Renderer renderer(outRate, 2);
    renderer.SetArrangement(arrangement);
    const auto started = std::chrono::steady_clock::now();
    const size_t total = static_cast<size_t>(30.0 * outRate);
    std::vector<float> left(256), right(256);
    for (size_t done = 0; done < total; done += 256) {
        float* out[2] = { left.data(), right.data() };
        renderer.Render(out, 256);
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    std::cout << "  4 stereo clips at their own tempo, 30 s at 48 kHz in " << seconds << " s ("
              << (30.0 / seconds) << "x realtime)\n";
    Require(seconds < 15.0, "too slow for playback");
}

int main(int argc, char** argv)
{
    wxInitializer wx;
    const std::pair<const char*, std::function<void()> > tests[] = {
        { "unstretched clip is sample exact", UnstretchedIsSampleExact },
        { "stretch keeps pitch and lands in place", StretchKeepsPitch },
        { "export on every core matches one core, sample for sample", ExportMatchesOneCore },
        { "lossy export's ceiling holds at -2 dB", LossyCeilingHoldsAtMinusTwo },
        { "export resamples a clip near its tempo, in place", ExportResamplesNearItsTempo },
        { "cent shift changes pitch only", CentShiftChangesPitchOnly },
        { "edit while playing is heard next block", EditWhilePlayingTakesEffectNextBlock },
        { "bass line removes the bass and leaves the highs", BassLineRemovesTheBassAndLeavesTheHighs },
        { "mid and high lines cut their bands only", MidAndHighLinesCutTheirBandsOnly },
        { "bass line added to a playing clip blends in, no click", BassLineAppearingOnAPlayingClipBlendsInWithoutAClick },
        { "volume line edited on a playing clip is heard", VolumeLineEditedOnAPlayingClipIsHeard },
        { "added track joins without restart", AddedTrackJoinsWithoutRestart },
        { "pan", MuteSoloPan },
        { "44.1 kHz clip on a 48 kHz output, stretcher and resampler", RateMismatchResamples },
        { "the small-machine resampler keeps the highs clean", ResamplerKeepsHighsClean },
        { "loudness meter: LUFS, true peak, sample peak", LoudnessMeterMeasuresSineAndPeaks },
        { "limiter holds the true peak at -1 dBTP", LimiterHoldsTruePeakUnderCeiling },
        { "demo: voice tag at 30 s, ducked, no clipping", DemoVoiceTagsEveryOutput },
        { "CPU: 8 stretched stereo clips", StretchedVoicesCpu },
        { "CPU: 4 clips at their own tempo, 48 kHz output", OwnTempoVoicesCpu },
        { "loop wraps at the loop end", LoopWrapsAtTheLoopEnd },
        { "tempo ramp keeps the pitch", TempoCurveKeepsPitch },
        { "tempo ramp keeps beats where the curve puts them", TempoCurveKeepsBeatsInPlace },
        { "keylock off: pitch follows the speed", KeylockOffPitchFollowsSpeed },
        { "latency compensation holds other tracks back", LatencyCompensationHoldsOtherTracksBack },
        { "deck loops, and slips back to where the track would be", DeckLoopsAndSlipsBack },
        { "deck metronome clicks on the grid", DeckMetronomeClicksOnBeats },
        { "device: 4 stretched clips, silent, no dropouts", DevicePlaysSilentlyWithoutDropouts },
    };
    int failures = 0;
    for (const auto& [name, run] : tests) {
        // An argument runs only the tests whose name contains it.
        if (argc > 1 && std::string(name).find(argv[1]) == std::string::npos) {
            continue;
        }
        try {
            run();
            std::cout << "PASS: " << name << '\n';
        } catch (const std::exception& e) {
            ++failures;
            std::cout << "FAIL: " << name << ": " << e.what() << '\n';
        }
    }
    return failures ? 1 : 0;
}
