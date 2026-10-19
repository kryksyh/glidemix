/**********************************************************************

  Audacity: A Digital Audio Editor

  ExportPluginHelpers.h

  Dominic Mazzoni

  Vitaly Sverchinsky split from ExportPlugin.h

**********************************************************************/

#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "ExportPlugin.h"
#include "ExportTypes.h"
#include "au3-math/SampleFormat.h"

class TrackList;
class WaveTrack;
class Mixer;
class WideSampleSequence;

namespace MixerOptions {
class Downmix;
}

///\brief Utility class that provides helper functions for ExportPlugin
class IMPORT_EXPORT_API ExportPluginHelpers final
{
public:
    static std::unique_ptr<Mixer> CreateMixer(
        const AudacityProject& project, bool selectionOnly, double startTime, double stopTime, unsigned numOutChannels,
        size_t outBufferSize, bool outInterleaved, double outRate, sampleFormat outFormat, MixerOptions::Downmix* mixerSpec);

    //! GlideMix: the whole mix rendered as playback renders it (limiter,
    //! tempo line, plugin automation), for timeline t0..t1 at `rate`; null
    //! when it cannot be (the export then mixes the tracks itself).
    //! `tracksAsked`: the export asked for selected tracks or a custom
    //! channel map; the hook may still render (the demo version does).
    using MixRenderHook = std::function<std::shared_ptr<const WideSampleSequence>(
                                            const AudacityProject&, double t0, double t1, double rate, bool tracksAsked)>;
    static void SetMixRenderHook(MixRenderHook hook);

    //! GlideMix: how loud the last export rendered through the hook was,
    //! measured as it was written (EBU R128).
    struct Loudness {
        double integratedLufs = -70.0;
        double truePeakDb = -100.0;
        double samplePeakDb = -100.0;
        double seconds = 0.0;
    };
    static void SetLastExportLoudness(std::optional<Loudness> loudness);

    //! GlideMix: whether this is the demo version (no licence). Demo
    //! exports are MP3 at 96 kbps, whatever format was chosen.
    static void SetDemoQuery(std::function<bool()> isDemo);

    //! GlideMix: the tracklist (.txt, with a .cue beside it) written for the
    //! last export; Take returns it and forgets it.
    static void SetLastTracklist(std::string path);
    static std::string TakeLastTracklist();
    static bool IsDemo();
    //! Returns the last measurement and forgets it.
    static std::optional<Loudness> TakeLastExportLoudness();

    ///\brief Sends progress update to delegate and retrieves state update from it.
    ///Typically used inside each export iteration.
    static ExportResult UpdateProgress(ExportProcessorDelegate& delegate, Mixer& mixer, double t0, double t1);

    template<typename T>
    static T GetParameterValue(const ExportProcessor::Parameters& parameters, int id, T defaultValue = T())
    {
        auto it = std::find_if(
            parameters.begin(),
            parameters.end(),
            [=](const auto& t) { return std::get<0>(t) == id; });
        if (it != parameters.end()) {
            if (auto value = std::get_if<T>(&std::get<1>(*it))) {
                return *value;
            }
        }
        return defaultValue;
    }
};
