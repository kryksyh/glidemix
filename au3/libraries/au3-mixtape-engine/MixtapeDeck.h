/*
 * GlideMix -- playback engine
 *
 * One deck: plays a single decoded track on its own output stream, for the
 * Beatgrid editor. What a DJ deck does to a track that the timeline doesn't:
 * loops, slip mode (Mixxx: playback carries on silently under a loop or a
 * scratch and comes back where the track would have been), and a metronome
 * on the track's own beat grid to hear whether the grid sits on the kicks.
 *
 * Threads: every method is for the main thread; the audio callback reads
 * atomics only.
 */
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace mixtape {
struct DeckAudio {
    std::vector<float> samples;   // interleaved
    unsigned channels = 2;
    double sampleRate = 44100.0;

    int64_t Frames() const { return channels ? static_cast<int64_t>(samples.size() / channels) : 0; }
    double Duration() const { return sampleRate > 0 ? static_cast<double>(Frames()) / sampleRate : 0.0; }
};

//! A metronome click pair at the deck's sample rate (the app's metronome sound).
struct DeckClicks {
    std::vector<float> beat;
    std::vector<float> accent;
    float gain = 1.0f;
};

class Deck
{
public:
    Deck();
    ~Deck();

    //! Takes the track without opening a device (tests drive OnCallback).
    bool Load(std::shared_ptr<const DeckAudio> audio);
    //! Opens the output device (Audacity's playback device name; empty or
    //! unknown = the system default) for `audio`, paused at 0.
    bool Open(std::shared_ptr<const DeckAudio> audio, const std::string& deviceName);
    void Close();
    bool IsOpen() const { return mStream != nullptr; }
    const std::string& LastError() const { return mError; }

    void SetPlaying(bool playing);
    bool IsPlaying() const { return mPlaying.load(); }
    //! Jumps the playhead (seconds into the track).
    void Seek(double seconds);
    //! The second of the track being heard now (output latency taken off while playing).
    double Position() const;
    bool AtEnd() const { return mAtEnd.load(); }

    //! Loops [start, end) while active. Starting a loop begins a slip action,
    //! ending it ends one.
    void SetLoop(bool active, double start, double end);
    bool LoopActive() const { return mLoopActive.load(); }

    //! Slip mode on/off. Turning it off while an action is running lets the
    //! action end where it is.
    void SetSlip(bool enabled) { mSlipEnabled = enabled; }
    //! A slip action (a loop, a scratch drag): while one runs the track keeps
    //! time underneath; when the last one ends with slip on, playback jumps
    //! to where the track would be.
    void BeginSlipAction();
    void EndSlipAction();

    //! Dragging the waveform: silent, the playhead only moves by Seek; the
    //! track's time (for slip) keeps running while the deck is playing.
    void SetScrubbing(bool scrubbing) { mScrubbing = scrubbing; }

    //! The click sound to use (null = a plain synthesised tick). Any thread.
    void SetClicks(std::shared_ptr<const DeckClicks> clicks) { std::atomic_store(&mClicks, std::move(clicks)); }

    //! Clicks on the beats of a constant grid; the downbeat of each bar clicks higher.
    void SetMetronome(bool enabled, double bpm, double firstBeat, double downbeat, int beatsPerBar = 4);

    //! Audio callback.
    int OnCallback(float* output, unsigned long frames);

private:
    std::shared_ptr<const DeckAudio> mAudio;
    void* mStream = nullptr;
    std::string mError;
    double mOutputLatency = 0.0;

    std::atomic<bool> mPlaying{ false };
    std::atomic<bool> mAtEnd{ false };
    std::atomic<int64_t> mFrame{ 0 };        // written by the callback while the stream runs
    std::atomic<int64_t> mSeekFrame{ -1 };   // main thread asks, callback applies
    std::atomic<bool> mLoopActive{ false };
    std::atomic<int64_t> mLoopStart{ 0 };
    std::atomic<int64_t> mLoopEnd{ 0 };

    std::atomic<bool> mSlipEnabled{ false };
    std::atomic<int> mSlipActions{ 0 };
    std::atomic<int64_t> mShadowFrame{ 0 };      // where the track would be
    std::atomic<bool> mShadowReset{ false };     // callback: start the shadow at the playhead
    std::atomic<bool> mSlipReturn{ false };      // callback: jump to the shadow

    std::atomic<bool> mScrubbing{ false };
    // When the last callback ran and how many frames it played, so Position()
    // can move smoothly between callbacks instead of in buffer-sized steps.
    std::atomic<int64_t> mCallbackNanos{ 0 };
    std::atomic<int64_t> mCallbackFrames{ 0 };
    std::atomic<bool> mMetronome{ false };
    std::shared_ptr<const DeckClicks> mClicks;   // atomic_load/store
    std::atomic<double> mBeatPeriod{ 0.5 };
    std::atomic<double> mFirstBeat{ 0.0 };
    std::atomic<double> mDownbeat{ 0.0 };
    std::atomic<int> mBeatsPerBar{ 4 };
};
}
