/*
 * GlideMix -- playback engine
 */
#include "MixtapeClipVoice.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <thread>

#ifdef MIXTAPE_ENGINE_HAS_RUBBERBAND
#include <rubberband/RubberBandStretcher.h>
#else
// No Rubber Band on this platform yet: a stand-in that is never constructed,
// so clips always take the unstretched path.
namespace RubberBand {
class RubberBandStretcher
{
public:
    size_t getStartDelay() const { return 0; }
    size_t getPreferredStartPad() const { return 0; }
    size_t getSamplesRequired() const { return 0; }
    int available() const { return 0; }
    size_t retrieve(float* const*, size_t) const { return 0; }
    void process(const float* const*, size_t, bool) {}
    void reset() {}
    void setTimeRatio(double) {}
    void setPitchScale(double) {}
};
}
#endif

#include "au3-mixer/Envelope.h"
#include "au3-mixer/PlaybackSpeedCurve.h"

namespace mixtape {
namespace {
std::atomic<bool> s_keylock{ true };
}

void SetKeylock(bool enabled)
{
    s_keylock = enabled;
}

bool KeylockEnabled()
{
    return s_keylock.load();
}

double ClipVoice::PitchFor(double ratio) const
{
    const double cents = std::pow(2.0, mClip.centShift / 1200.0);
    if (s_keylock.load() || ratio <= 0.0) {
        return cents * mClip.sampleRate / mOutputRate;
    }
    // Off: the pitch moves with the speed. At ratio outRate/clipRate (normal
    // speed) this is the plain rate conversion above.
    return cents / ratio;
}

namespace {
constexpr size_t kMaxChunk = 16384;

using RubberBand::RubberBandStretcher;

//! 4 threads or fewer: a 2-core laptop or NUC.
bool SmallMachine()
{
    static const bool small = std::thread::hardware_concurrency() > 0
                              && std::thread::hardware_concurrency() <= 4;
    return small;
}

//! Below this stretch the pitch change of plain resampling is inaudible
//! (0.2% = 3.5 cents).
constexpr double kResampleStretch = 0.002;

//! 4-point Hermite interpolation between y1 and y2.
inline float Hermite(float y0, float y1, float y2, float y3, float t)
{
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * t + c2) * t + c1) * t + y1;
}
}

ClipVoice::ClipVoice(double outputRate, size_t outputChannels)
    : mOutputRate{outputRate}
    , mOutputChannels{outputChannels}
{
}

ClipVoice::~ClipVoice() = default;

void ClipVoice::Assign(const ClipSnapshot& clip, bool alwaysStretch)
{
    mAlwaysStretch = alwaysStretch;
    mAppliedRatio = 0.0;
    const bool sameShape = mStretcher && clip.channels.size() == mClip.channels.size()
                           && clip.sampleRate == mClip.sampleRate;
    mClip = clip;
    mStarted = false;
    const size_t channels = std::max<size_t>(1, mClip.channels.size());
    mInput.assign(channels, std::vector<float>(kMaxChunk));
    mCursors.assign(channels, SourceCursor {});
    mOutput.assign(channels, std::vector<float>(kMaxChunk));
    mInputPtrs.resize(channels);
    mOutputPtrs.resize(channels);
    for (size_t c = 0; c < channels; ++c) {
        mInputPtrs[c] = mInput[c].data();
        mOutputPtrs[c] = mOutput[c].data();
    }
    mEnvelope.resize(kMaxChunk);
    mLowBand.clear();
    mBassWanted = mClip.lowBand || mClip.midBand || mClip.highBand;
    mBassBlend = 1.0;
    if (mBassWanted) {
        mLowBand.resize(channels);
        for (LowBand& band : mLowBand) {
            band.Configure(mOutputRate);   // it runs on the output, after any stretching
        }
        mLowGain.resize(kMaxChunk);
        mMidGain.resize(kMaxChunk);
        mHighGain.resize(kMaxChunk);
        mLowBlend.resize(kMaxChunk);
        mBassDry.resize(kMaxChunk);
    }
    mResample = Resamples();
    if (Bypass() || mResample) {
        mStretcher.reset();
        return;
    }
    if (!sameShape) {
        // Mixxx's keylock options (EngineBufferScaleRubberBand::onSignalChanged):
        // R3 "finer" engine, channels together so the result stays mono-compatible.
#ifdef MIXTAPE_ENGINE_HAS_RUBBERBAND
        RubberBandStretcher::Options options = RubberBandStretcher::OptionProcessRealTime;
        // GlideMix: the R3 "finer" engine costs ~3x the R2 "faster" one. On
        // small machines (4 threads or fewer -- a 2-core laptop) two stretched
        // tracks in a transition crackled, so they use "faster", as Mixxx
        // offers for weak computers. Mono-compatible channels either way.
        if (SmallMachine() || std::getenv("MIXTAPE_RUBBERBAND_FASTER")) {
            options |= RubberBandStretcher::OptionChannelsTogether;
        } else {
            options |= RubberBandStretcher::OptionEngineFiner | RubberBandStretcher::OptionChannelsTogether;
        }
        mStretcher = std::make_unique<RubberBandStretcher>(static_cast<size_t>(mClip.sampleRate), channels, options);
#endif
        // Preallocate for a wide range of ratios, as Mixxx does.
        mStretcher->setTimeRatio(2.0);
        mStretcher->setTimeRatio(1.0);
    }
    // Rubber Band's time ratio is output duration over input duration.
    mAppliedRatio = mClip.stretchRatio * mOutputRate / mClip.sampleRate;
    mStretcher->setTimeRatio(mAppliedRatio);
    mAppliedPitch = PitchFor(mAppliedRatio);
    mStretcher->setPitchScale(mAppliedPitch);
}

bool ClipVoice::Bypass() const
{
#ifndef MIXTAPE_ENGINE_HAS_RUBBERBAND
    return true;
#endif
    return !mAlwaysStretch && mClip.stretchRatio == 1.0 && mClip.centShift == 0 && mClip.sampleRate == mOutputRate;
}

bool ClipVoice::Resamples() const
{
    // GlideMix: on small machines a clip at (nearly) its own tempo is only
    // resampled -- to the output rate, and by the tiny stretch -- instead of
    // going through Rubber Band. A 44.1 kHz track on a 48 kHz output ran the
    // stretcher just to change the rate: five such tracks took half of a
    // 2-core i3 and the transitions crackled.
    return !Bypass() && !mAlwaysStretch && mClip.centShift == 0 && (SmallMachine() || std::getenv("MIXTAPE_RESAMPLE"))
           && std::abs(mClip.stretchRatio - 1.0) < kResampleStretch;
}

void ClipVoice::RenderResampled(float* const* out, size_t frames, double time, double endTime, const float* gains)
{
    const double step = (endTime - time) / frames;
    const size_t clipChannels = mInput.size();
    const double perFrame = (mClip.SourceAt(endTime) - mClip.SourceAt(time)) / frames;
    const size_t chunk = std::max<size_t>(1, static_cast<size_t>((kMaxChunk - 8) / std::max(1.0, perFrame + 0.01)));
    size_t produced = 0;
    while (produced < frames) {
        const size_t n = std::min(frames - produced, std::min(chunk, kMaxChunk));
        const double t0 = time + produced * step;
        const double s0 = mClip.SourceAt(t0);
        const double inc = (mClip.SourceAt(t0 + n * step) - s0) / n;
        const int64_t first = static_cast<int64_t>(std::floor(s0)) - 1;
        const size_t need = std::min(kMaxChunk, static_cast<size_t>(std::ceil(s0 + n * inc) - first) + 3);
        for (size_t c = 0; c < clipChannels; ++c) {
            float* in = mInput[c].data();
            if (c < mClip.channels.size()) {
                ReadSource(mClip.channels[c], first, in, need, &mCursors[c]);
            } else {
                std::fill_n(in, need, 0.0f);
            }
            float* dst = mOutput[c].data();
            for (size_t i = 0; i < n; ++i) {
                const double x = s0 + i * inc - static_cast<double>(first);
                const size_t k = std::min(static_cast<size_t>(x), need - 3);
                dst[i] = Hermite(in[k - 1], in[k], in[k + 1], in[k + 2], static_cast<float>(x - k));
            }
        }
        ApplyBass(mOutput, 0, n, t0, step);
        ApplyEnvelope(n, t0, step);
        for (size_t o = 0; o < mOutputChannels; ++o) {
            const float* src = mOutput[std::min(o, clipChannels - 1)].data();
            const float g = gains[o];
            float* dst = out[o] + produced;
            for (size_t i = 0; i < n; ++i) {
                dst[i] += src[i] * static_cast<float>(mEnvelope[i]) * g;
            }
        }
        produced += n;
    }
    mStarted = true;
    mNextTime = endTime;
}

void ClipVoice::Seek(double time)
{
    mNextTime = time;
    mStarted = true;
    mAppliedRatio = 0.0;
    mAppliedPitch = 0.0;
    for (LowBand& band : mLowBand) {
        band.Reset();
    }
    if (!mStretcher) {
        mInputPosition = std::llround(mClip.SourceAt(time));
        return;
    }
    // EngineBufferScaleRubberBand::reset(): run the preferred amount of
    // silence through first, then drop the start delay from the output, or
    // the first transient gets faded in.
    mStretcher->reset();
    for (auto& in : mInput) {
        std::fill(in.begin(), in.end(), 0.0f);
    }
    size_t pad = mStretcher->getPreferredStartPad();
    while (pad > 0) {
        const size_t n = std::min(pad, kMaxChunk);
        mStretcher->process(mInputPtrs.data(), n, false);
        pad -= n;
    }
    mDropOutput = mStretcher->getStartDelay();
    mInputPosition = static_cast<int64_t>(std::floor(mClip.SourceAt(time)));
}

void ClipVoice::ReadInput(size_t frames)
{
    for (size_t c = 0; c < mInput.size(); ++c) {
        if (c < mClip.channels.size()) {
            ReadSource(mClip.channels[c], mInputPosition, mInput[c].data(), frames, &mCursors[c]);
        } else {
            std::fill_n(mInput[c].data(), frames, 0.0f);
        }
    }
    mInputPosition += static_cast<int64_t>(frames);
}

void ClipVoice::ApplyEnvelope(size_t frames, double time, double step)
{
    if (mClip.envelope) {
        mClip.envelope->GetValues(mEnvelope.data(), static_cast<int>(frames), time, step);
    } else {
        std::fill_n(mEnvelope.data(), frames, 1.0);
    }
}

void ClipVoice::UpdateLines(const ClipSnapshot& clip)
{
    mClip.envelope = clip.envelope;
    mClip.lowBand = clip.lowBand;
    mClip.midBand = clip.midBand;
    mClip.highBand = clip.highBand;
    mBassWanted = mClip.lowBand || mClip.midBand || mClip.highBand;
    if (mBassWanted && mLowBand.empty()) {
        // A bass line has appeared on a clip that is already playing: start the crossover
        // from silence and blend it in (ApplyBass), so there is no click.
        const size_t channels = std::max<size_t>(1, mInput.size());
        mLowBand.resize(channels);
        for (LowBand& band : mLowBand) {
            band.Configure(mOutputRate);
        }
        mLowGain.resize(kMaxChunk);
        mMidGain.resize(kMaxChunk);
        mHighGain.resize(kMaxChunk);
        mLowBlend.resize(kMaxChunk);
        mBassDry.resize(kMaxChunk);
        mBassBlend = 0.0;
    }
}

void ClipVoice::ApplyBass(std::vector<std::vector<float> >& buffers, size_t offset, size_t frames, double time, double step)
{
    if (mLowBand.empty()) {
        return;
    }
    // Each band's line, or unity where it has none (or while blending out).
    const auto fill = [&](const std::shared_ptr<const Envelope>& line, std::vector<double>& gain) {
        if (line) {
            line->GetValues(gain.data(), static_cast<int>(frames), time, step);
            for (size_t i = 0; i < frames; ++i) {
                gain[i] = std::clamp(gain[i], 0.0, 1.0);
            }
        } else {
            std::fill_n(gain.data(), frames, 1.0);
        }
    };
    fill(mClip.lowBand, mLowGain);
    fill(mClip.midBand, mMidGain);
    fill(mClip.highBand, mHighGain);
    // 0 = the clip as it is, 1 = through the crossover. Fully in is the normal case.
    const bool settled = mBassWanted && mBassBlend >= 1.0;
    const double rate = 1.0 / (0.020 * mOutputRate);   // a 20 ms blend
    if (!settled) {
        double blend = mBassBlend;
        for (size_t i = 0; i < frames; ++i) {
            blend = std::clamp(blend + (mBassWanted ? rate : -rate), 0.0, 1.0);
            mLowBlend[i] = blend;
        }
        mBassBlend = blend;
    }
    for (size_t c = 0; c < mLowBand.size() && c < buffers.size(); ++c) {
        float* samples = buffers[c].data() + offset;
        if (settled) {
            mLowBand[c].Process3(samples, mLowGain.data(), mMidGain.data(), mHighGain.data(), frames);
            continue;
        }
        std::copy_n(samples, frames, mBassDry.data());
        mLowBand[c].Process3(samples, mLowGain.data(), mMidGain.data(), mHighGain.data(), frames);
        for (size_t i = 0; i < frames; ++i) {
            samples[i] = static_cast<float>(mBassDry[i] + mLowBlend[i] * (samples[i] - mBassDry[i]));
        }
    }
    if (!mBassWanted && mBassBlend <= 0.0) {
        mLowBand.clear();   // blended fully out: the clip is back to being bit for bit as it was
    }
}

void ClipVoice::RenderAdd(float* const* out, size_t frames, double time, double endTime, const float* gains,
                          const PlaybackSpeedCurve* curve)
{
    if (frames == 0) {
        return;
    }
    // Half an output sample of tolerance: anything else is a jump.
    if (!mStarted || std::abs(time - mNextTime) > 0.5 / mOutputRate) {
        Seek(time);
    }
    if (mResample) {
        RenderResampled(out, frames, time, endTime, gains);
        return;
    }
    const double step = (endTime - time) / frames;   // timeline seconds per output frame
    const size_t clipChannels = mInput.size();
    if (mStretcher) {
        // Output frames per input sample over this block: the clip's own
        // stretch and the tempo curve's speed in one ratio.
        // Input fed now is heard one stretcher latency later, so it gets the
        // speed the curve has then; otherwise every tempo change leaves the
        // beats late by that latency times the change.
        double from = time;
        double to = endTime;
        if (curve) {
            const double latency = mStretcher->getStartDelay() / mOutputRate;
            from = curve->SolveIntegralOfInverse(time, latency);
            to = curve->SolveIntegralOfInverse(endTime, latency);
        }
        const double inputFrames = mClip.SourceAt(to) - mClip.SourceAt(from);
        if (inputFrames > 1e-3) {
            const double ratio = frames / inputFrames;
            if (std::abs(ratio - mAppliedRatio) > 1e-9) {
                mStretcher->setTimeRatio(ratio);
                mAppliedRatio = ratio;
            }
            const double pitch = PitchFor(ratio);
            if (std::abs(pitch - mAppliedPitch) > 1e-9) {
                mStretcher->setPitchScale(pitch);
                mAppliedPitch = pitch;
            }
        }
    }
    size_t produced = 0;
    while (produced < frames) {
        const double chunkTime = time + produced * step;
        if (!mStretcher) {
            const size_t n = std::min(frames - produced, kMaxChunk);
            ReadInput(n);
            ApplyBass(mInput, 0, n, chunkTime, step);
            ApplyEnvelope(n, chunkTime, step);
            for (size_t o = 0; o < mOutputChannels; ++o) {
                const float* src = mInput[std::min(o, clipChannels - 1)].data();
                const float g = gains[o];
                float* dst = out[o] + produced;
                for (size_t i = 0; i < n; ++i) {
                    dst[i] += src[i] * static_cast<float>(mEnvelope[i]) * g;
                }
            }
            produced += n;
            continue;
        }
        const int available = mStretcher->available();
        if (available > 0) {
            size_t want = std::min<size_t>(available, frames - produced + mDropOutput);
            want = std::min(want, kMaxChunk);
            const size_t got = mStretcher->retrieve(mOutputPtrs.data(), want);
            const size_t skip = std::min(got, mDropOutput);
            mDropOutput -= skip;
            const size_t use = got - skip;
            if (use > 0) {
                ApplyBass(mOutput, skip, use, chunkTime, step);
                ApplyEnvelope(use, chunkTime, step);
                for (size_t o = 0; o < mOutputChannels; ++o) {
                    const float* src = mOutput[std::min(o, clipChannels - 1)].data() + skip;
                    const float g = gains[o];
                    float* dst = out[o] + produced;
                    for (size_t i = 0; i < use; ++i) {
                        dst[i] += src[i] * static_cast<float>(mEnvelope[i]) * g;
                    }
                }
                produced += use;
            }
            continue;
        }
        size_t required = mStretcher->getSamplesRequired();
        required = std::clamp<size_t>(required, 64, kMaxChunk);
        ReadInput(required);
        mStretcher->process(mInputPtrs.data(), required, false);
    }
    mNextTime = endTime;
}
}
