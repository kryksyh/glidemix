/**********************************************************************

  Audacity: A Digital Audio Editor

  ExportPluginHelpers.cpp

  Dominic Mazzoni

  Vitaly Sverchinsky split from ExportPlugin.h

**********************************************************************/

#include "ExportPluginHelpers.h"
#include "au3-track/Track.h"
#include "au3-mixer/Mix.h"
#include "au3-wave-track/WaveTrack.h"
#include "au3-effects/MixAndRender.h"
#include "ExportUtils.h"
#include "ExportPlugin.h"
#include "au3-stretching-sequence/StretchingSequence.h"

//Create a mixer by computing the time warp factor
namespace {
ExportPluginHelpers::MixRenderHook& MixRenderHookRef()
{
    static ExportPluginHelpers::MixRenderHook hook;
    return hook;
}
}

void ExportPluginHelpers::SetMixRenderHook(MixRenderHook hook)
{
    MixRenderHookRef() = std::move(hook);
}

std::unique_ptr<Mixer> ExportPluginHelpers::CreateMixer(
    const AudacityProject& project, bool selectionOnly, double startTime,
    double stopTime, unsigned numOutChannels, size_t outBufferSize,
    bool outInterleaved, double outRate, sampleFormat outFormat,
    MixerOptions::Downmix* mixerSpec)
{
    // GlideMix: what playback renders, unless a selection or a custom channel
    // map asks for the tracks themselves.
    if (MixRenderHookRef()) {
        if (auto rendered = MixRenderHookRef()(project, startTime, stopTime, outRate, selectionOnly || mixerSpec)) {
            const double endTime = rendered->GetEndTime();
            Mixer::Inputs renderedInputs;
            renderedInputs.emplace_back(std::move(rendered), Mixer::Stages {});
            return std::make_unique<Mixer>(
                std::move(renderedInputs), std::nullopt, true, Mixer::WarpOptions { 1.0, 1.0 }, startTime, endTime,
                numOutChannels, outBufferSize, outInterleaved, outRate, outFormat, true, nullptr,
                Mixer::ApplyVolume::Mixdown);
        }
    }
    Mixer::Inputs inputs;
    const auto& tracks = TrackList::Get(project);
    for (auto pTrack: ExportUtils::FindExportWaveTracks(tracks, selectionOnly)) {
        inputs.emplace_back(
            StretchingSequence::Create(*pTrack, pTrack->GetClipInterfaces()),
            GetEffectStages(*pTrack));
    }
    auto masterEffectStages = GetMasterEffectStages(project);
    //custom channel mapping isn't support with master effects on
    assert(masterEffectStages.empty() || (numOutChannels <= 2 && mixerSpec == nullptr));
    // MB: the stop time should not be warped, this was a bug.
    return std::make_unique<Mixer>(
        std::move(inputs), std::move(masterEffectStages),
        // Throw, to stop exporting, if read fails:
        true, Mixer::WarpOptions { tracks.GetOwner() }, startTime, stopTime,
        numOutChannels, outBufferSize, outInterleaved, outRate, outFormat, true,
        mixerSpec,
        mixerSpec ? Mixer::ApplyVolume::MapChannels : Mixer::ApplyVolume::Mixdown);
}

namespace {
double EvalExportProgress(Mixer& mixer, double t0, double t1)
{
    const auto duration = t1 - t0;
    if (duration > 0) {
        return std::clamp(mixer.MixGetCurrentTime() - t0, .0, duration) / duration;
    }
    return .0;
}
}

ExportResult ExportPluginHelpers::UpdateProgress(ExportProcessorDelegate& delegate, Mixer& mixer, double t0, double t1)
{
    delegate.OnProgress(EvalExportProgress(mixer, t0, t1));
    if (delegate.IsStopped()) {
        return ExportResult::Stopped;
    }
    if (delegate.IsCancelled()) {
        return ExportResult::Cancelled;
    }
    return ExportResult::Success;
}
