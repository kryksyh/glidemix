/*
 * GlideMix -- playback engine
 *
 * Plays a Renderer to an audio device. A render thread keeps a small
 * read-ahead (MixMeister's "output delay") in a lock-free ring; the device
 * callback only copies out of it. The playhead is the frame the device is
 * playing now, so it keeps moving smoothly while edits are rendered ahead.
 */
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "MixtapeRenderer.h"

class IMeterSender;

namespace mixtape {
class EffectsHost;

//! Frames per render block (effects hosts size their buffers to it).
inline constexpr size_t kRenderBlockFrames = 256;

//! True while any Transport is playing: edits then need no playback restart.
bool AnyTransportActive();

class Transport
{
public:
    struct Options {
        double sampleRate = 44100.0;
        size_t channels = 2;
        double readAheadSeconds = 0.08;
        //! Output device name as Audacity stores it; empty = system default.
        std::string deviceName;
        //! Audacity's audio host ("Windows WASAPI", "Core Audio"...): the
        //! device is looked for there first. Empty = any.
        std::string hostName;
        //! Output buffer to ask the device for, at least (Audacity's latency setting).
        double latencySeconds = 0.0;
        double endTime = 0.0;   // stop by itself after this timeline second (0 = never)
        //! Level meters (master and per track); fed from the render thread with the time each block will be heard.
        std::shared_ptr<IMeterSender> meter;
        //! Output channel the stereo pair starts on: 0 = outputs 1-2, 2 = outputs
        //! 3-4 (headphones on a DJ interface). Falls back to 0 when the device
        //! has fewer outputs.
        size_t channelOffset = 0;
        //! Mixed over every block after the master effects (the metronome).
        Renderer::Overlay overlay;
        //! True when this output is the headphone cue (the preview while the
        //! audience is being performed to), so it takes the cue level.
        bool isCue = false;
        //! Play at the tempo lane's speed (PlaybackSpeedCurve::Live()).
        bool followTempoCurve = true;
    };

    Transport();
    ~Transport();

    //! Main thread. Opens the device and starts playing from `startTime`.
    //! `effects` (optional) must outlive playback: stop the transport before destroying it.
    bool Start(const Options& options, std::shared_ptr<const Arrangement> arrangement, double startTime,
               EffectsHost* effects = nullptr);
    void Stop();
    void SetPaused(bool paused);
    bool IsPaused() const { return mPaused.load(); }
    bool IsActive() const { return mActive.load(); }
    //! True once playback ran past the end time.
    bool ReachedEnd() const { return mReachedEnd.load(); }
    //! Any thread: move the end (0 = never stop), e.g. when tracks are added past it while playing.
    void SetEndTime(double time) { mEndTime = time; }

    //! Any thread: jump the playhead.
    void Seek(double time);
    //! Any thread: loop playback between start and end.
    void SetLoop(bool active, double start, double end);

    //! Any thread: the arrangement to play from the next rendered block on.
    void SetArrangement(std::shared_ptr<const Arrangement> arrangement);

    //! Timeline second the device is playing now.
    double Position() const;

    const std::string& LastError() const { return mError; }
    //! Device callbacks that found the read-ahead short (heard as a dropout).
    uint64_t Underruns() const { return mUnderruns.load(); }
    //! Slowest block the render thread took since the last call, in ms, and
    //! the frames buffered ahead of the device now.
    double TakeSlowestRenderMs() { return mSlowestRenderUs.exchange(0) / 1000.0; }
    double BufferedMs() const;

    //! Device callback body; public only for the PortAudio trampoline.
    int OnCallback(float* output, unsigned long frames);

    //! Output channel the pair actually plays on (after the device fallback).
    size_t ChannelOffset() const { return mChannelOffset; }
    //! Outputs of a device by Audacity's name (empty = default), 0 if unknown.
    static int DeviceOutputs(const std::string& deviceName);
    //! Output devices with at least two channels (PortAudio names).
    static std::vector<std::string> OutputDevices();

private:
    void RenderLoop();
    int OnCallbackPair(float* output, unsigned long frames);
    void ResetBuffer(double time);

    Options mOptions;
    size_t mChannelOffset = 0;
    size_t mDeviceChannels = 2;
    std::unique_ptr<Renderer> mRenderer;
    void* mStream = nullptr;
    std::string mError;

    // Single-producer (render thread) / single-consumer (callback) ring of
    // interleaved frames. Positions count frames since the last reset.
    std::vector<float> mRing;
    size_t mRingFrames = 0;
    std::atomic<uint64_t> mWritten{ 0 };
    std::atomic<uint64_t> mRead{ 0 };
    //! Timeline span of each rendered block, so the playhead can say where the
    //! frame the device plays now sits on the timeline (a tempo curve makes
    //! that non-linear). Written by the render thread, read by any.
    struct BlockTime {
        std::atomic<uint64_t> start{ 0 };   // ring counter of the block's first frame
        std::atomic<double> t0{ 0.0 };
        std::atomic<double> t1{ 0.0 };
    };
    static constexpr size_t kBlockTimes = 1024;
    BlockTime mBlockTimes[kBlockTimes];
    std::atomic<uint64_t> mBlockCount{ 0 };
    std::atomic<double> mResetTime{ 0.0 };   // position while nothing is rendered yet
    std::atomic<uint64_t> mResetIndex{ 0 };

    std::atomic<double> mSeekRequest{ -1.0 };
    std::atomic<bool> mRunning{ false };
    std::atomic<bool> mActive{ false };
    std::atomic<bool> mPaused{ false };
    std::atomic<bool> mReachedEnd{ false };
    std::atomic<double> mEndTime{ 0.0 };
    std::atomic<uint64_t> mUnderruns{ 0 };
    std::atomic<uint64_t> mSlowestRenderUs{ 0 };
    std::atomic<bool> mStarted{ false };
    std::atomic<double> mOutputLatency{ 0.0 };
    std::chrono::steady_clock::time_point mBlockDacTime;   // render thread
    std::thread mThread;
};
}
