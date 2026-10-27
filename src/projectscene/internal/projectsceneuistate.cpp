/*
* Audacity: A Digital Audio Editor
*/
#include "projectsceneuistate.h"

#include <algorithm>

#include "global/settings.h"

using namespace au::projectscene;

static const QString TIMELINE_RULER_MODE_KEY("projectscene/timelineRulerMode");
static const QString GRID_LINES_VISIBLE_KEY("projectscene/gridLinesVisible");
static const QString CLIP_WAVEFORM_RGB_KEY("projectscene/clipWaveformRgb");
static const QString CLIP_WAVEFORM_STEREO_KEY("projectscene/clipWaveformStereo");
// GlideMix: the colour waveform and its stereo option are app settings, not
// part of a workspace: kept per workspace, switching workspace or pressing
// View > Color waveform in one of them quietly turned it off for good.
static const muse::Settings::Key WAVEFORM_RGB_SETTING("projectscene", "projectscene/colorWaveform");
static const muse::Settings::Key WAVEFORM_STEREO_SETTING("projectscene", "projectscene/colorWaveformStereo");
static const QString LANE_BACKGROUND_KEY("projectscene/laneBackground");
static const muse::Settings::Key ALL_TRACKS_VIEW_SETTING("projectscene", "projectscene/allTracksView");

void ProjectSceneUiState::init()
{
    // The plain waveform first, as Audacity's Modern workspace draws it; the
    // colour waveform is the option (View > Color waveform, Preferences > Waveforms).
    muse::settings()->setDefaultValue(WAVEFORM_RGB_SETTING, muse::Val(false));
    muse::settings()->setDefaultValue(WAVEFORM_STEREO_SETTING, muse::Val(false));
    muse::settings()->setDefaultValue(ALL_TRACKS_VIEW_SETTING, muse::Val(-1));
    muse::settings()->valueChanged(ALL_TRACKS_VIEW_SETTING).onReceive(this, [this](const muse::Val&) {
        m_allTracksViewChanged.notify();
    });
    muse::settings()->valueChanged(WAVEFORM_RGB_SETTING).onReceive(this, [this](const muse::Val&) {
        m_clipWaveformRgbChanged.notify();
        m_laneBackgroundChanged.notify();
    });
    muse::settings()->valueChanged(WAVEFORM_STEREO_SETTING).onReceive(this, [this](const muse::Val&) {
        m_clipWaveformRgbChanged.notify();
    });
    uiState()->uiItemStateChanged(TIMELINE_RULER_MODE_KEY).onNotify(this, [this]() {
        m_timelineRulerModeChanged.notify();
    });
    uiState()->uiItemStateChanged(GRID_LINES_VISIBLE_KEY).onNotify(this, [this]() {
        m_gridLinesVisibleChanged.notify();
    });
    uiState()->uiItemStateChanged(CLIP_WAVEFORM_RGB_KEY).onNotify(this, [this]() {
        m_clipWaveformRgbChanged.notify();
        // The canvas follows the waveform style unless it has been set.
        m_laneBackgroundChanged.notify();
    });
    // Stereo on or off repaints the clips the same way the colour waveform does.
    uiState()->uiItemStateChanged(CLIP_WAVEFORM_STEREO_KEY).onNotify(this, [this]() {
        m_clipWaveformRgbChanged.notify();
    });
    uiState()->uiItemStateChanged(LANE_BACKGROUND_KEY).onNotify(this, [this]() {
        m_laneBackgroundChanged.notify();
    });
}

TimelineRulerMode ProjectSceneUiState::timelineRulerMode() const
{
    return uiState()->uiItemState(TIMELINE_RULER_MODE_KEY) == "1"
           ? TimelineRulerMode::BEATS_AND_MEASURES
           : TimelineRulerMode::MINUTES_AND_SECONDS;
}

void ProjectSceneUiState::setTimelineRulerMode(TimelineRulerMode mode)
{
    const QString value = mode == TimelineRulerMode::BEATS_AND_MEASURES ? "1" : "0";
    if (uiState()->uiItemState(TIMELINE_RULER_MODE_KEY) == value) {
        return;
    }
    uiState()->setUiItemState(TIMELINE_RULER_MODE_KEY, value);
}

muse::async::Notification ProjectSceneUiState::timelineRulerModeChanged() const
{
    return m_timelineRulerModeChanged;
}

// A workspace that says nothing about them gets Audacity's own look: grid on,
// waveform in the clip's colour.
bool ProjectSceneUiState::gridLinesVisible() const
{
    return uiState()->uiItemState(GRID_LINES_VISIBLE_KEY) != "0";
}

void ProjectSceneUiState::setGridLinesVisible(bool visible)
{
    const QString value = visible ? "1" : "0";
    if (uiState()->uiItemState(GRID_LINES_VISIBLE_KEY) == value) {
        return;
    }
    uiState()->setUiItemState(GRID_LINES_VISIBLE_KEY, value);
}

muse::async::Notification ProjectSceneUiState::gridLinesVisibleChanged() const
{
    return m_gridLinesVisibleChanged;
}

bool ProjectSceneUiState::clipWaveformRgb() const
{
    return muse::settings()->value(WAVEFORM_RGB_SETTING).toBool();
}

void ProjectSceneUiState::setClipWaveformRgb(bool rgb)
{
    if (clipWaveformRgb() != rgb) {
        muse::settings()->setSharedValue(WAVEFORM_RGB_SETTING, muse::Val(rgb));
    }
}

bool ProjectSceneUiState::clipWaveformStereo() const
{
    return muse::settings()->value(WAVEFORM_STEREO_SETTING).toBool();
}

void ProjectSceneUiState::setClipWaveformStereo(bool stereo)
{
    if (clipWaveformStereo() != stereo) {
        muse::settings()->setSharedValue(WAVEFORM_STEREO_SETTING, muse::Val(stereo));
    }
}

int ProjectSceneUiState::allTracksView() const
{
    return std::clamp(muse::settings()->value(ALL_TRACKS_VIEW_SETTING).toInt(), -1, 4);
}

void ProjectSceneUiState::setAllTracksView(int view)
{
    view = std::clamp(view, -1, 4);
    if (allTracksView() != view) {
        muse::settings()->setSharedValue(ALL_TRACKS_VIEW_SETTING, muse::Val(view));
    }
}

muse::async::Notification ProjectSceneUiState::allTracksViewChanged() const
{
    return m_allTracksViewChanged;
}

muse::async::Notification ProjectSceneUiState::clipWaveformRgbChanged() const
{
    return m_clipWaveformRgbChanged;
}

int ProjectSceneUiState::laneBackground() const
{
    const QString value = uiState()->uiItemState(LANE_BACKGROUND_KEY);
    return value == "1" ? 1 : (value == "2" ? 2 : 0);
}

void ProjectSceneUiState::setLaneBackground(int mode)
{
    const QString value = QString::number(std::clamp(mode, 0, 2));
    if (uiState()->uiItemState(LANE_BACKGROUND_KEY) == value) {
        return;
    }
    uiState()->setUiItemState(LANE_BACKGROUND_KEY, value);
}

muse::async::Notification ProjectSceneUiState::laneBackgroundChanged() const
{
    return m_laneBackgroundChanged;
}

bool ProjectSceneUiState::blackCanvas() const
{
    switch (laneBackground()) {
    case 1: return true;
    case 2: return false;
    default: return clipWaveformRgb();
    }
}
