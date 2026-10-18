/*
 * Audacity: A Digital Audio Editor
 */
#pragma once

#include <map>
#include <optional>
#include <string>

#include "async/channel.h"

#include "automationtypes.h"
#include "trackedit/trackedittypes.h"

#include "modularity/imoduleinterface.h"

namespace au::automation {
class IClipGainInteraction : MODULE_EXPORT_INTERFACE
{
    INTERFACE_ID(IClipGainInteraction)

public:
    virtual ~IClipGainInteraction() = default;

    virtual std::optional<AutomationInfo> clipGainInfo(const trackedit::ClipKey& key) const = 0;
    virtual AutomationPoints clipGainPoints(const trackedit::ClipKey& key) const = 0;
    virtual AutomationSegments clipGainSegments(const trackedit::ClipKey& key) const = 0;
    /// completed=false: live update while dragging (envelope + waveform redraw, no undo step),
    /// exactly like setClipGainPoint's own flag; completed=true pushes one history state.
    virtual bool setClipGainSegments(const trackedit::ClipKey& key, const AutomationSegments& segments, bool completed) = 0;
    virtual bool setClipGainPoint(const trackedit::ClipKey& key, double tAbs, double value, bool completed) = 0;
    virtual bool removeClipGainPoint(const trackedit::ClipKey& key, int index, bool completed) = 0;
    virtual bool setClipGainPointAtIndex(const trackedit::ClipKey& key, int index, double tAbs, double value, bool completed) = 0;
    virtual bool beginClipGainPointDrag(const trackedit::ClipKey& clip, int pointIndex) = 0;
    virtual bool updateClipGainPointDrag(const trackedit::ClipKey& clip, double tAbs, double value) = 0;
    virtual bool endClipGainPointDrag(const trackedit::ClipKey& clip, bool commit) = 0;
    virtual muse::async::Channel<trackedit::ClipKey, bool> clipGainChanged() const = 0;

    /// The clip's bass line: the Mixtape engine's built-in low-band control (0..1, 1 leaves the bass
    /// alone, 0 removes it), used by transitions for the bass swap. Points are project seconds, like
    /// clipGainPoints. Empty means the clip has no bass line.
    virtual AutomationPoints clipBassPoints(const trackedit::ClipKey& key) const = 0;
    /// Replaces the whole bass line (empty removes it). Pushes no history: the caller does, so that
    /// a whole transition stays one undo step.
    virtual bool setClipBassPoints(const trackedit::ClipKey& key, const AutomationPoints& points) = 0;

    /// GlideMix: any band of the transition EQ, 0 = low (the bass line above), 1 = mid, 2 = high.
    virtual AutomationPoints clipBandPoints(const trackedit::ClipKey& key, int band) const = 0;
    virtual bool setClipBandPoints(const trackedit::ClipKey& key, int band, const AutomationPoints& points) = 0;

    /// GlideMix transitions: the automation line of one parameter of a track effect (a DJ effect's
    /// filter cutoff, echo send ...) on this clip. `effectId` names the effect; the clip's track gets
    /// one when it has none yet (with no undo step of its own). Points are project seconds, values
    /// 0..1 of the parameter's range, spanning the clip. Empty switches the line off. Pushes no
    /// history: the caller does, so that a whole transition stays one undo step.
    /// The line setClipEffectPoints writes (or one drawn by hand), project seconds; empty when the
    /// clip's track has no such effect or the clip no such line.
    virtual AutomationPoints clipEffectPoints(const trackedit::ClipKey& key, const std::string& effectId,
                                              const std::string& parameterId) const = 0;
    /// `addIfMissing` false: a track without the effect is left alone.
    virtual bool setClipEffectPoints(const trackedit::ClipKey& key, const std::string& effectId,
                                     const std::string& parameterId, const AutomationPoints& points,
                                     bool addIfMissing = true) = 0;

    /// GlideMix, Ableton's override: whether a plugin parameter's automation on this track plays
    /// (true), was overridden by moving its control (false), or doesn't exist (nullopt).
    virtual std::optional<bool> pluginAutomationActive(trackedit::TrackId trackId, const std::string& instanceId,
                                                       const std::string& parameterId) const = 0;
    /// Switches every clip's line of that parameter on this track off (override) or back on
    /// ("Re-enable automation"), and hands the effect its lines at once. Pushes no history.
    virtual bool setPluginAutomationActive(trackedit::TrackId trackId, const std::string& instanceId,
                                           const std::string& parameterId, bool active) = 0;
    /// Every automated parameter of that effect on this track: parameter id -> plays (true) or overridden.
    virtual std::map<std::string, bool> pluginAutomationStates(trackedit::TrackId trackId,
                                                               const std::string& instanceId) const = 0;
    /// Ableton's Control Bar Re-Enable Automation: anything overridden anywhere in the project.
    virtual bool hasOverriddenPluginAutomation() const = 0;
    /// Brings every overridden line in the project back. Pushes no history.
    virtual bool reenableAllPluginAutomation() = 0;
    /// Ableton's Delete Automation: removes that parameter's lines from every clip of the track.
    /// Pushes no history.
    virtual bool deletePluginAutomation(trackedit::TrackId trackId, const std::string& instanceId,
                                        const std::string& parameterId) = 0;
    /// Ableton's automation recording: writes `value` at project time `time` into the line of
    /// the clip under it, replacing the points after `previousTime` (the last write of this
    /// pass, or < 0 to start one). Makes the line if there is none. Returns the time written,
    /// or < 0 when no clip is there. The line is off while recording: switch it back on with
    /// setPluginAutomationActive when the pass ends. Pushes no history.
    virtual double recordPluginAutomation(trackedit::TrackId trackId, const std::string& instanceId,
                                          const std::string& pluginId, const std::string& parameterId,
                                          double time, double previousTime, double value, bool stepped) = 0;
};
}
