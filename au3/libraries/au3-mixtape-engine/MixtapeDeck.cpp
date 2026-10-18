/*
 * GlideMix -- playback engine
 */
#include "MixtapeDeck.h"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstring>

#include "portaudio.h"

namespace mixtape {
namespace {
PaDeviceIndex FindDeckOutputDevice(const std::string& name)
{
    if (!name.empty()) {
        const int count = Pa_GetDeviceCount();
        for (int i = 0; i < count; ++i) {
            const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
            if (info && info->maxOutputChannels > 0 && name == info->name) {
                return i;
            }
        }
    }
    return Pa_GetDefaultOutputDevice();
}

int DeckCallback(const void*, void* output, unsigned long frames, const PaStreamCallbackTimeInfo*,
                 PaStreamCallbackFlags, void* user)
{
    return static_cast<Deck*>(user)->OnCallback(static_cast<float*>(output), frames);
}

constexpr double kPi = 3.14159265358979323846;

int64_t NowNanos()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
           .count();
}
constexpr double kClickSeconds = 0.03;
}

Deck::Deck() = default;

Deck::~Deck()
{
    Close();
}

bool Deck::Load(std::shared_ptr<const DeckAudio> audio)
{
    Close();
    mError.clear();
    if (!audio || audio->Frames() == 0 || audio->channels == 0) {
        mError = "nothing to play";
        return false;
    }
    mAudio = std::move(audio);
    mPlaying = false;
    mAtEnd = false;
    mFrame = 0;
    mSeekFrame = -1;
    mLoopActive = false;
    mSlipActions = 0;
    mSlipReturn = false;
    return true;
}

bool Deck::Open(std::shared_ptr<const DeckAudio> audio, const std::string& deviceName)
{
    if (!Load(std::move(audio))) {
        return false;
    }

    if (Pa_Initialize() != paNoError) {
        mError = "PortAudio could not start";
        return false;
    }
    PaStreamParameters out{};
    out.device = FindDeckOutputDevice(deviceName);
    if (out.device == paNoDevice) {
        mError = "no audio output device";
        Pa_Terminate();
        return false;
    }
    const PaDeviceInfo* info = Pa_GetDeviceInfo(out.device);
    out.channelCount = 2;
    out.sampleFormat = paFloat32;
    out.suggestedLatency = info ? info->defaultLowOutputLatency : 0.01;
    PaStream* stream = nullptr;
    const PaError err = Pa_OpenStream(&stream, nullptr, &out, mAudio->sampleRate, paFramesPerBufferUnspecified,
                                      paClipOff, &DeckCallback, this);
    if (err != paNoError) {
        mError = Pa_GetErrorText(err);
        Pa_Terminate();
        return false;
    }
    if (const PaStreamInfo* streamInfo = Pa_GetStreamInfo(stream)) {
        mOutputLatency = streamInfo->outputLatency;
    }
    if (Pa_StartStream(stream) != paNoError) {
        mError = "audio stream did not start";
        Pa_CloseStream(stream);
        Pa_Terminate();
        return false;
    }
    mStream = stream;
    return true;
}

void Deck::Close()
{
    if (mStream) {
        Pa_AbortStream(static_cast<PaStream*>(mStream));
        Pa_CloseStream(static_cast<PaStream*>(mStream));
        mStream = nullptr;
        Pa_Terminate();
    }
    mPlaying = false;
    mAudio.reset();
}

void Deck::SetPlaying(bool playing)
{
    if (playing && mAtEnd.load()) {
        Seek(0.0);
    }
    mPlaying = playing;
}

void Deck::Seek(double seconds)
{
    if (!mAudio) {
        return;
    }
    const int64_t frame = std::clamp<int64_t>(static_cast<int64_t>(std::llround(seconds * mAudio->sampleRate)), 0,
                                              mAudio->Frames());
    mAtEnd = false;
    if (mStream) {
        mSeekFrame = frame;
    }
    mFrame = frame;
}

double Deck::Position() const
{
    if (!mAudio) {
        return 0.0;
    }
    const int64_t pending = mSeekFrame.load();
    if (pending >= 0 || !mPlaying.load() || mScrubbing.load() || !mStream) {
        return static_cast<double>(pending >= 0 ? pending : mFrame.load()) / mAudio->sampleRate;
    }
    // The last callback left the playhead at mFrame; the device has been
    // playing on since then. Never run ahead of what that callback wrote.
    const double rate = mAudio->sampleRate;
    const double since = (NowNanos() - mCallbackNanos.load()) / 1e9;
    const double ahead = std::clamp(since * rate, 0.0, static_cast<double>(mCallbackFrames.load()));
    const double heard = (static_cast<double>(mFrame.load()) - mCallbackFrames.load() + ahead) / rate - mOutputLatency;
    return std::max(0.0, heard);
}

void Deck::SetLoop(bool active, double start, double end)
{
    if (!mAudio) {
        return;
    }
    const double rate = mAudio->sampleRate;
    if (active && end > start) {
        mLoopStart = static_cast<int64_t>(std::llround(start * rate));
        mLoopEnd = static_cast<int64_t>(std::llround(end * rate));
        if (!mLoopActive.exchange(true)) {
            BeginSlipAction();
        }
    } else if (mLoopActive.exchange(false)) {
        EndSlipAction();
    }
}

void Deck::BeginSlipAction()
{
    if (mSlipActions.fetch_add(1) == 0) {
        mShadowFrame = mFrame.load();
        mShadowReset = true;
    }
}

void Deck::EndSlipAction()
{
    if (mSlipActions.load() <= 0) {
        return;
    }
    if (mSlipActions.fetch_sub(1) == 1 && mSlipEnabled.load()) {
        mSlipReturn = true;
    }
}

void Deck::SetMetronome(bool enabled, double bpm, double firstBeat, double downbeat, int beatsPerBar)
{
    if (bpm > 0.0) {
        mBeatPeriod = 60.0 / bpm;
        mFirstBeat = firstBeat;
        mDownbeat = downbeat;
        mBeatsPerBar = std::max(1, beatsPerBar);
    }
    mMetronome = enabled && bpm > 0.0;
}

int Deck::OnCallback(float* output, unsigned long frames)
{
    const std::shared_ptr<const DeckAudio> audio = std::atomic_load(&mAudio);
    if (!audio) {
        std::memset(output, 0, frames * 2 * sizeof(float));
        return paContinue;
    }
    const int64_t total = audio->Frames();
    const unsigned channels = audio->channels;
    const double rate = audio->sampleRate;

    int64_t frame = mFrame.load();
    int64_t shadow = mShadowFrame.load();
    if (mShadowReset.exchange(false)) {
        shadow = frame;   // a slip action starts: the shadow leaves from the playhead
    }
    if (const int64_t seek = mSeekFrame.exchange(-1); seek >= 0) {
        frame = seek;     // during a slip action (a scratch) the shadow keeps its time
    }
    if (mSlipReturn.exchange(false)) {
        frame = std::min(shadow, total);
    }

    const bool playing = mPlaying.load();
    const bool looping = mLoopActive.load();
    const int64_t loopStart = mLoopStart.load();
    const int64_t loopEnd = mLoopEnd.load();
    const bool slipping = mSlipActions.load() > 0;
    const bool scrubbing = mScrubbing.load();
    const bool metronome = mMetronome.load();
    const double period = mBeatPeriod.load();
    const double firstBeat = mFirstBeat.load();
    const double downbeat = mDownbeat.load();
    const int beatsPerBar = mBeatsPerBar.load();
    const std::shared_ptr<const DeckClicks> clicks = std::atomic_load(&mClicks);

    for (unsigned long i = 0; i < frames; ++i) {
        float left = 0.0f;
        float right = 0.0f;
        if (playing && scrubbing) {
            if (slipping) {
                ++shadow;
            }
        } else if (playing && frame < total) {
            const size_t at = static_cast<size_t>(frame) * channels;
            left = audio->samples[at];
            right = channels > 1 ? audio->samples[at + 1] : left;
            if (metronome && period > 0.0) {
                const double t = static_cast<double>(frame) / rate;
                const double sinceFirst = t - firstBeat;
                const double beat = std::floor(sinceFirst / period + 1e-9);
                const double dt = sinceFirst - beat * period;
                const long long index = std::llround((firstBeat + beat * period - downbeat) / period);
                const bool barStart = ((index % beatsPerBar) + beatsPerBar) % beatsPerBar == 0;
                if (sinceFirst >= 0.0 && clicks) {
                    const std::vector<float>& sound = barStart ? clicks->accent : clicks->beat;
                    const auto at = static_cast<size_t>(dt * rate);
                    if (at < sound.size()) {
                        const float click = sound[at] * clicks->gain;
                        left = std::clamp(left + click, -1.0f, 1.0f);
                        right = std::clamp(right + click, -1.0f, 1.0f);
                    }
                } else if (sinceFirst >= 0.0 && dt < kClickSeconds) {
                    const float click = static_cast<float>(0.8 * std::sin(2.0 * kPi * (barStart ? 1600.0 : 1000.0) * dt)
                                                           * std::exp(-dt * 90.0));
                    left += click;
                    right += click;
                }
            }
            ++frame;
            if (slipping) {
                ++shadow;
            }
            if (looping && loopEnd > loopStart && frame >= loopEnd) {
                frame = loopStart;
            }
        }
        output[2 * i] = left;
        output[2 * i + 1] = right;
    }
    if (playing && !scrubbing && frame >= total) {
        mPlaying = false;
        mAtEnd = true;
    }
    mShadowFrame = shadow;
    if (mSeekFrame.load() < 0) {
        mCallbackFrames = static_cast<int64_t>(frames);
        mFrame = frame;
        mCallbackNanos = NowNanos();
    }
    return paContinue;
}
}
