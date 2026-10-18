/*
 * GlideMix -- playback engine
 *
 * One transport: renders the arrangement from a timeline position onward.
 * The arrangement may be replaced from any thread at any time; the render
 * thread switches to it at the start of its next block, and voices of clips
 * that did not change keep playing without a seam.
 */
#pragma once

#include <atomic>
#include <functional>
#include <cstdint>
#include <map>
#include <memory>
#include <vector>

#include "MixtapeArrangement.h"
#include "MixtapeWorkers.h"

namespace mixtape {
class ClipVoice;
class EffectsHost;
}

class PlaybackSpeedCurve;

namespace mixtape {

class Renderer
{
public:
    Renderer(double outputRate, size_t outputChannels);
    ~Renderer();

    //! Any thread.
    void SetArrangement(std::shared_ptr<const Arrangement> arrangement);
    std::shared_ptr<const Arrangement> CurrentArrangement() const;

    //! Before rendering starts: run track and master effects through this host.
    void SetEffects(EffectsHost* effects) { mEffects = effects; }

    //! Render thread: each track's post-fader block (planar, one pointer per output channel).
    using TrackSink = std::function<void (int64_t trackId, float* const* buffers, size_t frames)>;
    void SetTrackSink(TrackSink sink) { mTrackSink = std::move(sink); }

    //! The level of the two outputs a DJ has: what the audience hears (the
    //! Perform output, or the only output when nothing is being performed) and
    //! what the headphones hear (the preview while performing). Linear, 1 =
    //! unity. Any thread; the render reads them per block.
    static void SetAudienceGain(float gain);
    static void SetCueGain(float gain);
    static float AudienceGain();
    static float CueGain();
    //! Which of the two this renderer's output is.
    void SetIsCue(bool isCue) { mIsCue = isCue; }
    //! GlideMix: rendering to a file (export): no output level and no
    //! output tap (meters, broadcast); the limiter still applies.
    void SetOffline(bool offline) { mOffline = offline; }

    //! Render thread: every finished block of both outputs, for anything that
    //! listens to what is playing rather than making it -- the broadcaster.
    //! `isCue` says which output the block came from. Set from any thread; it
    //! is read once per block.
    using OutputTap = std::function<void (bool isCue, const float* const* out, size_t channels, size_t frames,
                                          double sampleRate)>;
    static void SetOutputTap(OutputTap tap);

    //! Render thread: mixed over the finished block (after master effects),
    //! e.g. the metronome. The block covers timeline [t0, t1).
    using Overlay = std::function<void (float* const* out, size_t frames, double t0, double t1)>;
    void SetOverlay(Overlay overlay) { mOverlay = std::move(overlay); }

    //! Follow the tempo lane's live speed curve (PlaybackSpeedCurve::Live()).
    void UseLiveSpeedCurve(bool use) { mUseLiveCurve = use; }
    //! A fixed speed curve instead (tests); null = normal speed.
    void SetSpeedCurve(std::shared_ptr<const PlaybackSpeedCurve> curve) { mFixedCurve = std::move(curve); }

    //! Any thread: loop between start and end (end > start), or stop looping.
    void SetLoop(bool active, double start, double end)
    {
        mLoopStart = start;
        mLoopEnd = end;
        mLoopActive = active && end > start;
    }

    //! Any thread: frames the output lags the timeline by, to line tracks up
    //! with the one whose effects delay it most.
    size_t OutputDelayFrames() const { return mOutputDelay.load(); }

    //! Render thread (or before rendering starts).
    void Seek(double time) { mTime = time; }
    //! Timeline second the next block starts at.
    double Position() const { return mTime; }

    //! Render thread: writes `frames` of the mix into `out` (overwrites),
    //! advances the position, and returns the timeline span rendered.
    std::pair<double, double> Render(float* const* out, size_t frames);
    //! Render thread: like Render, but wraps at the loop end within the block.
    //! Returns the block's start time and the time the next block starts at.
    std::pair<double, double> RenderLooped(float* const* out, size_t frames);

    double OutputRate() const { return mOutputRate; }
    size_t OutputChannels() const { return mOutputChannels; }

private:
    using VoiceKey = std::pair<int64_t, int64_t>;   // track id, clip id

    const double mOutputRate;
    const size_t mOutputChannels;

    //! GlideMix: the master's safety limiter, so overlapping loud tracks are
    //! turned down instead of clipping. No look-ahead: nothing is delayed, and
    //! audio below the ceiling passes untouched, sample for sample.
    struct Limiter {
        float gain = 1.0f;
        float release = 0.0f;
    };
    Limiter mLimiter;
    void Limit(float* const* out, size_t frames);

    EffectsHost* mEffects = nullptr;
    TrackSink mTrackSink;
    Overlay mOverlay;
    bool mIsCue = false;
    bool mOffline = false;
    std::shared_ptr<const Arrangement> mPending;   // guarded by atomic_load/store
    std::shared_ptr<const Arrangement> mCurrent;   // render thread only
    double mTime = 0.0;
    int64_t mDemoFrame = 0;     // output frames so far, for the demo gaps
    uint32_t mDemoNoise = 22222u;
    std::atomic<bool> mLoopActive{ false };
    std::atomic<double> mLoopStart{ 0.0 };
    std::atomic<double> mLoopEnd{ 0.0 };
    bool mUseLiveCurve = false;
    std::shared_ptr<const PlaybackSpeedCurve> mFixedCurve;

    std::map<VoiceKey, std::unique_ptr<ClipVoice> > mVoices;
    //! One block's work for a track: its voices and the clips they play.
    struct TrackJob {
        size_t track = 0;
        std::vector<std::pair<ClipVoice*, const ClipSnapshot*> > voices;
    };
    std::vector<TrackJob> mJobs;
    std::vector<std::vector<std::vector<float> > > mTrackBuffers;   // track, channel
    std::vector<float*> mTrackPtrs;
    WorkerPool mPool;
    //! Per track: delays it by (the most-delayed track's latency - its own).
    struct DelayLine {
        size_t frames = 0;
        size_t position = 0;
        std::vector<std::vector<float> > buffer;   // channel, frames
    };
    std::map<int64_t, DelayLine> mDelays;
    std::atomic<size_t> mOutputDelay{ 0 };
};
}
