#pragma once
#include <algorithm>
#include <vector>
#include <map>

#include <functional>
#include <string>

#include <memory>

#include "global/async/notification.h"
#include "global/types/retval.h"

#include "trackedit/trackedittypes.h"
#include "types/projectscenetypes.h"
#include "types/secs.h"

namespace au::projectscene {
class IProjectViewState
{
public:
    //! Each plugin parameter's own lane colour, the same every time.
    static std::string pluginParameterColor(const std::string& instanceId, const std::string& parameterId)
    {
        static const char* const palette[] = {
            "#FFD43B", "#4DD0FF", "#FF6B9D", "#7CFF6B", "#FF9F43",
            "#B388FF", "#00E5C3", "#FF5252", "#E6FF4D", "#82B1FF",
        };
        return palette[std::hash<std::string>{}(instanceId + "/" + parameterId) % 10];
    }
    struct PluginAutomationFocus {
        trackedit::TrackId trackId{-1};
        std::string instanceId, pluginId, parameterId, label, color;
        double defaultValue{};
        bool visible{};
        bool stepped{};   // a switch or stepped choice: drawn and played as steps
    };
    // Transient, project-local editing focus. No curve points live on a track.
    const PluginAutomationFocus& pluginAutomationFocus() const { return m_pluginFocus; }
    void setPluginAutomationFocus(PluginAutomationFocus focus)
    { m_pluginFocus = std::move(focus); m_pluginFocusChanged.notify(); }
    muse::async::Notification pluginAutomationFocusChanged() const { return m_pluginFocusChanged; }
    //! GlideMix: Ableton's "Show Automation in New Lane": parameters shown in
    //! lanes of their own under a track, each editable directly.
    static constexpr int kAutomationLaneHeight = 56;
    using AutomationLane = PluginAutomationFocus;
    const std::vector<AutomationLane>& automationLanes(trackedit::TrackId trackId) const
    {
        static const std::vector<AutomationLane> none;
        const auto it = m_automationLanes.find(trackId);
        return it == m_automationLanes.end() ? none : it->second;
    }
    //! False when the parameter already has a lane on that track.
    bool addAutomationLane(trackedit::TrackId trackId, AutomationLane lane)
    {
        auto& lanes = m_automationLanes[trackId];
        for (const auto& l : lanes) {
            if (l.instanceId == lane.instanceId && l.parameterId == lane.parameterId) {
                return false;
            }
        }
        lane.trackId = trackId;
        lane.visible = true;
        lanes.push_back(std::move(lane));
        saveAutomationLanes(trackId);
        m_automationLanesChanged.notify();
        return true;
    }
    bool removeAutomationLane(trackedit::TrackId trackId, const std::string& instanceId, const std::string& parameterId)
    {
        auto it = m_automationLanes.find(trackId);
        if (it == m_automationLanes.end()) {
            return false;
        }
        auto& lanes = it->second;
        const auto before = lanes.size();
        lanes.erase(std::remove_if(lanes.begin(), lanes.end(), [&](const AutomationLane& l) {
            return l.instanceId == instanceId && l.parameterId == parameterId;
        }), lanes.end());
        if (lanes.size() == before) {
            return false;
        }
        saveAutomationLanes(trackId);
        m_automationLanesChanged.notify();
        return true;
    }
    muse::async::Notification automationLanesChanged() const { return m_automationLanesChanged; }

    //! Ableton's Automation Arm: while playing, moving a plugin control writes its line.
    bool automationArmed() const { return m_automationArmed; }
    void setAutomationArmed(bool armed) { m_automationArmed = armed; m_pluginFocusChanged.notify(); }
    virtual ~IProjectViewState() = default;

    //State of elements
    virtual muse::ValCh<int> totalTrackHeight() const = 0;
    virtual muse::ValCh<int> trackHeight(const trackedit::TrackId& trackId) const = 0;
    virtual muse::ValCh<bool> isTrackCollapsed(const trackedit::TrackId& trackId) const = 0;
    virtual muse::ValCh<double> channelHeightRatio(const trackedit::TrackId& trackId) const = 0;

    virtual int trackVerticalPosition(const trackedit::TrackId& trackId) const = 0;
    virtual void changeTrackHeight(const trackedit::TrackId& trackId, int deltaY) = 0;
    virtual void setTrackHeight(const trackedit::TrackId& trackId, int height) = 0;
    //! GlideMix: one height for several tracks, laid out once (smooth track zoom).
    virtual void setTracksHeight(const trackedit::TrackIdList& trackIds, int height) = 0;
    //! GlideMix: changes to many lanes count as one: the same lanes stay in
    //! view, and one undo state.
    virtual void beginTracksChange() = 0;
    virtual void endTracksChange() = 0;
    virtual void setChannelHeightRatio(const trackedit::TrackId& trackId, double ratio) = 0;
    virtual trackedit::TrackId trackAtPosition(double y) const = 0;
    virtual trackedit::TrackIdList tracksInRange(double y1, double y2) const = 0;

    virtual bool isSnapEnabled() const = 0;
    virtual void setIsSnapEnabled(bool enabled) = 0;

    virtual SnapType snapType() const = 0;
    virtual void setSnapType(SnapType type) = 0;

    virtual bool isSnapTripletsEnabled() const = 0;
    virtual void setIsSnapTripletsEnabled(bool enabled) = 0;

    virtual void setSnap(const Snap& s) = 0;
    virtual Snap getSnap() const = 0;
    virtual muse::ValCh<Snap> snap() const = 0;

    virtual void setClipGainAutomationEnabled(bool enabled) = 0;
    virtual muse::ValCh<bool> clipGainAutomationEnabled() const = 0;

    virtual void setSplitToolEnabled(bool enabled) = 0;
    virtual muse::ValCh<bool> splitToolEnabled() const = 0;

    virtual muse::ValCh<std::pair<float, float> > verticalDisplayBounds(const trackedit::TrackId& trackId) const = 0;
    virtual void zoomInVertically(const trackedit::TrackId& trackId) = 0;
    virtual void zoomOutVertically(const trackedit::TrackId& trackId) = 0;
    virtual void resetVerticalZoom(const trackedit::TrackId& trackId) = 0;
    virtual bool isDefaultVerticalZoom(const trackedit::TrackId& trackId) const = 0;
    virtual bool isMaxVerticalZoom(const trackedit::TrackId& trackId) const = 0;
    virtual bool isMinVerticalZoom(const trackedit::TrackId& trackId) const = 0;

    virtual muse::ValCh<int> verticalRulerWidth() const = 0;

    virtual muse::ValCh<bool> isHalfWave(const trackedit::TrackId& trackId) const = 0;
    virtual void toggleHalfWave(const trackedit::TrackId& trackId) = 0;
    //! GlideMix: a stereo track drawn as one waveform in half the height.
    virtual muse::ValCh<bool> isSingleWave(const trackedit::TrackId& trackId) const = 0;
    virtual void toggleSingleWave(const trackedit::TrackId& trackId) = 0;

    virtual muse::ValCh<trackedit::TrackViewType> trackViewType(const trackedit::TrackId& trackId) const = 0;
    virtual void setTrackViewType(const trackedit::TrackId& trackId, trackedit::TrackViewType viewType) = 0;
    virtual void toggleGlobalSpectrogramView() = 0;
    virtual bool globalSpectrogramToggleIsOn() const = 0;
    virtual muse::async::Notification globalSpectrogramToggleIsOnChanged() const = 0;

    virtual muse::ValCh<int> trackRulerType(const trackedit::TrackId& trackId) const = 0;
    virtual void setTrackRulerType(const trackedit::TrackId& trackId, int rulerType) = 0;

    //State of user interaction
    virtual double mousePositionY() const = 0;
    virtual void setMousePositionY(double y) = 0;

    virtual muse::ValCh<int> tracksVerticalOffset() const = 0;
    virtual void changeTracksVerticalOffset(int deltaY) = 0;
    virtual muse::ValCh<bool> tracksVerticalScrollLocked() const = 0;
    virtual void setTracksVerticalScrollLocked(bool lock) = 0;

    virtual void setItemEditStartTimeOffset(double val) = 0;
    virtual double itemEditStartTimeOffset() const = 0;

    virtual void setItemEditEndTimeOffset(double val) = 0;
    virtual double itemEditEndTimeOffset() const = 0;

    virtual void setMoveInitiated(bool val) = 0;
    virtual bool moveInitiated() const = 0;

    virtual void setLastEditedClip(const trackedit::ClipKey& clipKey) = 0;
    virtual trackedit::ClipKey lastEditedClip() const = 0;

    //! Key of the item currently being moved/trimmed/stretched. While set, it is always
    //! omitted from itemsBoundaries so a drag never snaps the item to its own edges, even
    //! when the boundary set is rebuilt mid-drag (e.g. from trackChanged notifications).
    virtual void setEditedItem(const trackedit::TrackItemKey& key) = 0;

    virtual void setItemsBoundaries(const std::set<muse::secs_t>& boundaries) = 0;
    virtual std::set<muse::secs_t> itemsBoundaries() const = 0;
    virtual void updateItemsBoundaries(bool excludeCurrentSelection,
                                       const trackedit::TrackItemKey& itemKeyToOmit = trackedit::TrackItemKey {}) = 0;

    virtual void setZoomState(const ZoomState& state) = 0;
    virtual ZoomState zoomState() const = 0;
    //! Fires whenever the timeline saves a new zoom or scroll position
    virtual muse::async::Notification zoomStateChanged() const = 0;

    virtual muse::async::Notification rolledBack() const = 0;

    virtual muse::ValCh<bool> altPressed() const = 0;
    virtual muse::ValCh<bool> ctrlPressed() const = 0;

    virtual muse::ValCh<bool> keyboardMoveActive() const = 0;
    virtual void setKeyboardMoveActive(bool active) = 0;

    virtual muse::async::Notification modifiersReleased() const = 0;

    virtual int trackDefaultHeight() const = 0;
protected:
    //! Writes a track's lanes into the project (saved with it).
    virtual void saveAutomationLanes(trackedit::TrackId) {}
    void notifyAutomationLanesChanged() const { m_automationLanesChanged.notify(); }
    mutable std::map<trackedit::TrackId, std::vector<AutomationLane> > m_automationLanes;
private:
    PluginAutomationFocus m_pluginFocus;
    muse::async::Notification m_pluginFocusChanged;
    bool m_automationArmed = false;
    mutable muse::async::Notification m_automationLanesChanged;
};

using IProjectViewStatePtr = std::shared_ptr<IProjectViewState>;
}
