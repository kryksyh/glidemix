/*
 * GlideMix -- playback engine
 *
 * Runs the tracks' realtime effects (DJ Effects, Audio Units, VST3, built-ins)
 * and their automation from the Mixtape renderer, through the same
 * RealtimeEffectManager the old AudioIO used.
 *
 * Threads: EnsureTrack / RefreshAutomation / the scope for adding effects run
 * on the main thread; BeginBlock / Process / EndBlock on the render thread.
 */
#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <vector>

class AudacityProject;
class ChannelGroup;
class WaveTrack;

namespace RealtimeEffects {
class InitializationScope;
class ProcessingScope;
}

namespace mixtape {
struct Arrangement;

class EffectsHost
{
public:
    EffectsHost(std::weak_ptr<AudacityProject> project, double sampleRate, unsigned channels, size_t blockSize);
    ~EffectsHost();

    //! Main thread, before a track is first rendered: give its effects processors.
    void EnsureTrack(const WaveTrack& track);
    //! Main thread: rebuild a track's automation after its clips or curves changed.
    void RefreshAutomation(const WaveTrack& track);

    //! Main thread: the scope to pass when effects are added, replaced or removed while playing.
    RealtimeEffects::InitializationScope* Scope();

    //! Main thread: hold while changing the effect lists of a playing project
    //! (adding, replacing, removing effects). The render thread never waits
    //! for it: a block that finds it held plays without effects.
    std::unique_lock<std::mutex> LockForEdit() { return std::unique_lock<std::mutex>(mMutex); }

    //! Main thread: each track's effect latency (active effects, in output
    //! frames), so the renderer can line every track up with the latest one.
    void MeasureLatencies(Arrangement& arrangement);

    //! Render thread.
    void BeginBlock();
    //! Processes `frames` of `group` in place; the block covers timeline [time, endTime).
    void Process(const ChannelGroup* group, float* const* buffers, size_t frames, double time, double endTime);
    void EndBlock();

private:
    void ConfigureAutomation(const WaveTrack& track);

    std::weak_ptr<AudacityProject> mProject;
    const double mRate;
    const unsigned mChannels;
    const size_t mBlockSize;

    std::unique_ptr<RealtimeEffects::InitializationScope> mInit;
    std::unique_ptr<RealtimeEffects::ProcessingScope> mProcessing;
    std::set<const ChannelGroup*> mGroups;   // main thread
    //! Tracks given processors stay alive until playback stops: the effect
    //! manager keeps raw pointers to them.
    std::vector<std::shared_ptr<const WaveTrack> > mKeepAlive;
    std::mutex mMutex;
    std::unique_lock<std::mutex> mBlockLock;   // render thread, BeginBlock..EndBlock

    std::vector<std::vector<float> > mScratch;   // channels + 1 scratch + dummy
    std::vector<float*> mScratchPtrs;
    std::vector<double> mTimes;
};

//! The effects host of the playing transport, if any (main thread). Effect
//! edits made while the Mixtape engine plays must go through its scope.
RealtimeEffects::InitializationScope* ActiveEffectsScope(const AudacityProject& project);
//! Main thread: locks the playing engine's effects against the render thread
//! for the lifetime of the returned lock (empty when the engine isn't playing).
std::unique_lock<std::mutex> LockEffectsForEdit(const AudacityProject& project);
}
