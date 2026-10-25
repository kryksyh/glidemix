/*
 * GlideMix -- playback engine
 */
#include "MixtapeEffects.h"

#include <algorithm>
#include <atomic>
#include <map>

#include <wx/log.h>

#include "au3-components/ParameterAutomationSnapshot.h"
#include "au3-project/Project.h"
#include "au3-realtime-effects/RealtimeEffectList.h"
#include "au3-realtime-effects/RealtimeEffectManager.h"
#include "au3-realtime-effects/RealtimeEffectState.h"
#include "MixtapeArrangement.h"
#include "au3-wave-track/TrackParameterAutomation.h"
#include "au3-wave-track/WaveTrack.h"

namespace mixtape {
namespace {
std::map<const AudacityProject*, EffectsHost*>& ActiveHosts()
{
    static std::map<const AudacityProject*, EffectsHost*> hosts;   // main thread only
    return hosts;
}
}

RealtimeEffects::InitializationScope* ActiveEffectsScope(const AudacityProject& project)
{
    const auto it = ActiveHosts().find(&project);
    return it != ActiveHosts().end() ? it->second->Scope() : nullptr;
}

std::unique_lock<std::mutex> LockEffectsForEdit(const AudacityProject& project)
{
    const auto it = ActiveHosts().find(&project);
    return it != ActiveHosts().end() ? it->second->LockForEdit() : std::unique_lock<std::mutex>();
}

EffectsHost::EffectsHost(std::weak_ptr<AudacityProject> project, double sampleRate, unsigned channels, size_t blockSize)
    : mProject{project}
    , mRate{sampleRate}
    , mChannels{channels}
    , mBlockSize{blockSize}
{
    // Initializes the project's (master) effects too.
    mInit = std::make_unique<RealtimeEffects::InitializationScope>(project, sampleRate, channels, blockSize);
    mScratch.assign(channels + 2, std::vector<float>(blockSize));
    mScratchPtrs.resize(channels + 2);
    for (size_t i = 0; i < mScratch.size(); ++i) {
        mScratchPtrs[i] = mScratch[i].data();
    }
    mTimes.resize(blockSize);
    if (const auto p = project.lock()) {
        ActiveHosts()[p.get()] = this;
        // Master effects get automation timing like track effects.
        auto& master = RealtimeEffectList::Get(*p);
        for (size_t e = 0; e < master.GetStatesCount(); ++e) {
            master.GetStateAt(e)->PrepareAutomationTiming(blockSize, sampleRate);
        }
    }
}

EffectsHost::~EffectsHost()
{
    for (auto it = ActiveHosts().begin(); it != ActiveHosts().end();) {
        it = it->second == this ? ActiveHosts().erase(it) : std::next(it);
    }
    mProcessing.reset();
    mInit.reset();   // finalizes every processor
    mKeepAlive.clear();
}

RealtimeEffects::InitializationScope* EffectsHost::Scope()
{
    return mInit.get();
}

void EffectsHost::EnsureTrack(const WaveTrack& track)
{
    if (mGroups.count(&track)) {
        return;
    }
    const auto lock = LockForEdit();
    mGroups.insert(&track);
    mKeepAlive.push_back(track.SharedPointer<const WaveTrack>());
    mInit->AddGroup(track, mChannels, static_cast<float>(mRate), mBlockSize);
    ConfigureAutomation(track);
    const auto count = RealtimeEffectList::Get(const_cast<WaveTrack&>(track)).GetStatesCount();
    if (count > 0) {
        wxLogMessage("Mixtape engine: track %s plays through %d effect(s)", track.GetName(), static_cast<int>(count));
    }
}

void EffectsHost::RefreshAutomation(const WaveTrack& track)
{
    if (mGroups.count(&track)) {
        const auto lock = LockForEdit();
        ConfigureAutomation(track);
    }
}

void EffectsHost::ConfigureAutomation(const WaveTrack& track)
{
    // As AudioIO's TransportState: a curve an effect can't take is logged
    // and left out, never fatal.
    auto& effects = RealtimeEffectList::Get(const_cast<WaveTrack&>(track));
    for (size_t e = 0; e < effects.GetStatesCount(); ++e) {
        const auto state = effects.GetStateAt(e);
        const auto instance = state ? state->GetInstance() : nullptr;
        if (!instance) {
            continue;
        }
        try {
            auto snapshot = std::make_shared<const ParameterAutomationSnapshot>(
                BuildTrackParameterAutomation(track, state->GetInstanceId(), state->GetID().ToStdString(wxConvUTF8)));
            if (!instance->ConfigureParameterAutomation(snapshot)) {
                wxLogWarning("Mixtape engine: automation skipped for %s", state->GetID());
                instance->ConfigureParameterAutomation(nullptr);
            }
        } catch (const std::exception& error) {
            wxLogWarning("Mixtape engine: automation skipped for %s: %s", state->GetID(), wxString::FromUTF8(error.what()));
            instance->ConfigureParameterAutomation(nullptr);
        }
        state->PrepareAutomationTiming(mBlockSize, mRate);
    }
}

void EffectsHost::MeasureLatencies(Arrangement& arrangement)
{
    // GlideMix: the master effects too, as Audacity's AudioIO discards their
    // latency from the master buffers.
    arrangement.masterLatencyFrames = 0;
    if (const auto project = mProject.lock()) {
        auto& master = RealtimeEffectList::Get(*project);
        if (master.IsActive()) {
            for (size_t e = 0; e < master.GetStatesCount(); ++e) {
                const auto state = master.GetStateAt(e);
                if (!state || !state->IsActive()) {
                    continue;
                }
                if (const auto instance = state->GetInstance()) {
                    arrangement.masterLatencyFrames
                        += static_cast<size_t>(instance->GetLatency(state->GetSettings(), mRate));
                }
            }
        }
    }
    for (TrackSnapshot& track : arrangement.tracks) {
        track.latencyFrames = 0;
        if (!track.track || !mGroups.count(track.track.get())) {
            continue;
        }
        auto& effects = RealtimeEffectList::Get(const_cast<WaveTrack&>(*track.track));
        if (!effects.IsActive()) {
            continue;
        }
        for (size_t e = 0; e < effects.GetStatesCount(); ++e) {
            const auto state = effects.GetStateAt(e);
            if (!state || !state->IsActive()) {
                continue;
            }
            if (const auto instance = state->GetInstance()) {
                track.latencyFrames += static_cast<size_t>(instance->GetLatency(state->GetSettings(), mRate));
            }
        }
    }
}

void EffectsHost::BeginBlock()
{
    mBlockLock = std::unique_lock<std::mutex>(mMutex, std::try_to_lock);
    if (!mBlockLock.owns_lock()) {
        return;   // the main thread is changing effects: this block plays dry
    }
    mProcessing = std::make_unique<RealtimeEffects::ProcessingScope>(*mInit, mProject);
}

void EffectsHost::Process(const ChannelGroup* group, float* const* buffers, size_t frames, double time, double endTime)
{
    if (!mProcessing || frames > mBlockSize || frames == 0) {
        return;
    }
    const double step = (endTime - time) / frames;
    for (size_t i = 0; i < frames; ++i) {
        mTimes[i] = time + i * step;
    }
    // Returns frames an effect's latency wants discarded; effects are heard
    // late by that much for now (latency compensation comes later).
    mProcessing->Process(group, buffers, mScratchPtrs.data(), mScratchPtrs[mChannels], mChannels, frames, mTimes.data());
}

void EffectsHost::EndBlock()
{
    mProcessing.reset();
    if (mBlockLock.owns_lock()) {
        mBlockLock.unlock();
    }
}
}
