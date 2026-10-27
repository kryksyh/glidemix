/*
* Audacity: A Digital Audio Editor
*/

#include <QCoreApplication>
#include <QTimer>

#include "project/iaudacityproject.h"

#include "projectsceneactionscontroller.h"

using namespace muse;
using namespace au::projectscene;
using namespace muse::async;
using namespace muse::actions;

static const ActionCode VERTICAL_RULERS_CODE("toggle-vertical-rulers");
static const ActionCode RMS_IN_WAVEFORM_CODE("toggle-rms-in-waveform");
static const ActionCode CLIPPING_IN_WAVEFORM_CODE("toggle-clipping-in-waveform");
static const ActionCode MINUTES_SECONDS_RULER("minutes-seconds-ruler");
static const ActionCode BEATS_MEASURES_RULER("beats-measures-ruler");
static const ActionCode CLIP_PITCH_AND_SPEED_CODE("clip-pitch-speed");
static const ActionCode TOGGLE_UPDATE_DISPLAY_WHILE_PLAYING_CODE("toggle-update-display-while-playing");
static const ActionCode TOGGLE_PINNED_PLAY_HEAD_CODE("toggle-pinned-play-head");
static const ActionCode TOGGLE_PLAYBACK_ON_RULER_CLICK_ENABLED_CODE("toggle-playback-on-ruler-click-enabled");
static const ActionQuery TOGGLE_TRACK_HALF_WAVE("action://projectscene/track-view-half-wave");
static const ActionQuery TOGGLE_TRACK_SINGLE_WAVE("action://projectscene/track-view-single-wave");
static const ActionCode LABEL_OPEN_EDITOR_CODE("open-label-editor");
static const ActionCode CLIP_GAIN_CODE("clip-gain");
static const ActionCode BEAT_GRID_CODE("toggle-beat-grid");
static const ActionCode COLOUR_WAVEFORM_CODE("toggle-colour-waveform");
// GlideMix: View > Track view, every song at once (IProjectSceneUiState::allTracksView).
static const ActionCode ALL_TRACKS_VIEW_CODES[] = {
    "track-view-all-waveform", "track-view-all-single-wave", "track-view-all-half-wave",
    "track-view-all-spectrogram", "track-view-all-multi",
};

static const muse::Uri EDIT_PITCH_AND_SPEED_URI("audacity://projectscene/editpitchandspeed");

void ProjectSceneActionsController::init()
{
    dispatcher()->reg(this, MINUTES_SECONDS_RULER, this, &ProjectSceneActionsController::toggleMinutesSecondsRuler);
    dispatcher()->reg(this, BEATS_MEASURES_RULER, this, &ProjectSceneActionsController::toggleBeatsMeasuresRuler);

    dispatcher()->reg(this, VERTICAL_RULERS_CODE, this, &ProjectSceneActionsController::toggleVerticalRulers);
    dispatcher()->reg(this, RMS_IN_WAVEFORM_CODE, this, &ProjectSceneActionsController::toggleRMSInWaveform);
    dispatcher()->reg(this, CLIPPING_IN_WAVEFORM_CODE, this, &ProjectSceneActionsController::toggleClippingInWaveform);
    dispatcher()->reg(this, TOGGLE_UPDATE_DISPLAY_WHILE_PLAYING_CODE, this,
                      &ProjectSceneActionsController::toggleUpdateDisplayWhilePlaying);
    dispatcher()->reg(this, TOGGLE_PINNED_PLAY_HEAD_CODE, this, &ProjectSceneActionsController::togglePinnedPlayHead);
    dispatcher()->reg(this, CLIP_PITCH_AND_SPEED_CODE, this, &ProjectSceneActionsController::openClipPitchAndSpeedEdit);
    dispatcher()->reg(this, TOGGLE_PLAYBACK_ON_RULER_CLICK_ENABLED_CODE, this,
                      &ProjectSceneActionsController::togglePlaybackOnRulerClickEnabled);
    dispatcher()->reg(this, TOGGLE_TRACK_HALF_WAVE, this, &ProjectSceneActionsController::toggleTrackHalfWave);
    dispatcher()->reg(this, TOGGLE_TRACK_SINGLE_WAVE, this, &ProjectSceneActionsController::toggleTrackSingleWave);
    dispatcher()->reg(this, LABEL_OPEN_EDITOR_CODE, this, &ProjectSceneActionsController::openLabelEditor);
    dispatcher()->reg(this, CLIP_GAIN_CODE, this, &ProjectSceneActionsController::toggleAutomation);
    dispatcher()->reg(this, BEAT_GRID_CODE, this, &ProjectSceneActionsController::toggleBeatGrid);
    dispatcher()->reg(this, COLOUR_WAVEFORM_CODE, this, &ProjectSceneActionsController::toggleColourWaveform);
    for (int view = 0; view < 5; ++view) {
        dispatcher()->reg(this, ALL_TRACKS_VIEW_CODES[view], [this, view]() { setAllTracksView(view); });
    }
    projectSceneUiState()->allTracksViewChanged().onNotify(this, [this]() {
        for (const ActionCode& code : ALL_TRACKS_VIEW_CODES) {
            notifyActionCheckedChanged(code);
        }
    });
    // A song added later is drawn the way the rest are.
    globalContext()->currentProjectChanged().onNotify(this, [this]() {
        const auto prj = globalContext()->currentTrackeditProject();
        if (!prj) {
            return;
        }
        prj->trackAdded().onReceive(this, [this](const trackedit::Track& track) {
            const int view = projectSceneUiState()->allTracksView();
            if (view < 0) {
                return;
            }
            const trackedit::TrackId id = track.id;
            // After the import has finished setting the track up.
            QTimer::singleShot(0, qApp, [this, id, view]() { applyTrackView(id, view); });
        }, muse::async::Asyncable::Mode::SetReplace);
    });

    projectSceneUiState()->timelineRulerModeChanged().onNotify(this, [this]() {
        notifyActionCheckedChanged(MINUTES_SECONDS_RULER);
        notifyActionCheckedChanged(BEATS_MEASURES_RULER);
    });

    // Both are saved with the workspace, so switching workspace changes them.
    projectSceneUiState()->gridLinesVisibleChanged().onNotify(this, [this]() {
        notifyActionCheckedChanged(BEAT_GRID_CODE);
    });
    projectSceneUiState()->clipWaveformRgbChanged().onNotify(this, [this]() {
        notifyActionCheckedChanged(COLOUR_WAVEFORM_CODE);
    });
}

void ProjectSceneActionsController::notifyActionCheckedChanged(const ActionCode& actionCode)
{
    m_actionCheckedChanged.send(actionCode);
}

void ProjectSceneActionsController::toggleMinutesSecondsRuler()
{
    projectSceneUiState()->setTimelineRulerMode(TimelineRulerMode::MINUTES_AND_SECONDS);
}

void ProjectSceneActionsController::toggleBeatsMeasuresRuler()
{
    projectSceneUiState()->setTimelineRulerMode(TimelineRulerMode::BEATS_AND_MEASURES);
}

// GlideMix: the bar and beat lines behind the tracks, and rekordbox's
// colour waveform on the clips. Both live in the workspace.
void ProjectSceneActionsController::toggleBeatGrid()
{
    projectSceneUiState()->setGridLinesVisible(!projectSceneUiState()->gridLinesVisible());
    notifyActionCheckedChanged(BEAT_GRID_CODE);
}

void ProjectSceneActionsController::toggleColourWaveform()
{
    projectSceneUiState()->setClipWaveformRgb(!projectSceneUiState()->clipWaveformRgb());
    notifyActionCheckedChanged(COLOUR_WAVEFORM_CODE);
}

void ProjectSceneActionsController::toggleVerticalRulers()
{
    bool verticalRulersVisible = configuration()->isVerticalRulersVisible();
    configuration()->setVerticalRulersVisible(!verticalRulersVisible);
    notifyActionCheckedChanged(VERTICAL_RULERS_CODE);
}

void ProjectSceneActionsController::toggleRMSInWaveform()
{
    bool rmsVisible = configuration()->isRMSInWaveformVisible();
    configuration()->setRMSInWaveformVisible(!rmsVisible);
    notifyActionCheckedChanged(RMS_IN_WAVEFORM_CODE);
}

void ProjectSceneActionsController::toggleClippingInWaveform()
{
    bool clippingVisible = configuration()->isClippingInWaveformVisible();
    configuration()->setClippingInWaveformVisible(!clippingVisible);
    notifyActionCheckedChanged(CLIPPING_IN_WAVEFORM_CODE);
}

void ProjectSceneActionsController::toggleUpdateDisplayWhilePlaying()
{
    bool enabled = configuration()->updateDisplayWhilePlayingEnabled();
    configuration()->setUpdateDisplayWhilePlayingEnabled(!enabled);
    notifyActionCheckedChanged(TOGGLE_UPDATE_DISPLAY_WHILE_PLAYING_CODE);
}

void ProjectSceneActionsController::togglePinnedPlayHead()
{
    bool enabled = configuration()->pinnedPlayHeadEnabled();
    configuration()->setPinnedPlayHeadEnabled(!enabled);
    notifyActionCheckedChanged(TOGGLE_PINNED_PLAY_HEAD_CODE);
}

void ProjectSceneActionsController::openClipPitchAndSpeedEdit(const ActionData& args)
{
    if (interactive()->isOpened(EDIT_PITCH_AND_SPEED_URI).val) {
        return;
    }

    IF_ASSERT_FAILED(args.count() == 1) {
        return;
    }

    trackedit::ClipKey clipKey = args.arg<trackedit::ClipKey>(0);
    if (!clipKey.isValid()) {
        return;
    }

    muse::UriQuery query(EDIT_PITCH_AND_SPEED_URI);
    query.addParam("trackId", muse::Val(std::to_string(clipKey.trackId)));
    query.addParam("clipId", muse::Val(std::to_string(clipKey.itemId)));
    query.addParam("focusItemName", muse::Val("pitch"));

    interactive()->open(query);
}

void ProjectSceneActionsController::openLabelEditor()
{
    interactive()->open("audacity://projectscene/openlabeleditor");
}

void ProjectSceneActionsController::togglePlaybackOnRulerClickEnabled()
{
    bool isEnabled = configuration()->playbackOnRulerClickEnabled();
    configuration()->setPlaybackOnRulerClickEnabled(!isEnabled);
    notifyActionCheckedChanged(TOGGLE_PLAYBACK_ON_RULER_CLICK_ENABLED_CODE);
}

void ProjectSceneActionsController::toggleAutomation()
{
    project::IAudacityProjectPtr prj = globalContext()->currentProject();
    const auto viewState = prj->viewState();

    if (viewState == nullptr) {
        return;
    }

    const bool automationState = viewState->clipGainAutomationEnabled().val;
    const bool enablingAutomation = !automationState;
    if (enablingAutomation && viewState->globalSpectrogramToggleIsOn()) {
        viewState->toggleGlobalSpectrogramView();
    }

    viewState->setClipGainAutomationEnabled(enablingAutomation);
}

void ProjectSceneActionsController::toggleTrackHalfWave(const muse::actions::ActionQuery& q)
{
    IF_ASSERT_FAILED(q.params().size() >= 1) {
        return;
    }
    const int trackId = q.param("trackId").toInt();

    project::IAudacityProjectPtr prj = globalContext()->currentProject();
    const auto viewState = prj->viewState();

    if (viewState == nullptr) {
        return;
    }
    viewState->toggleHalfWave(trackId);
    notifyActionCheckedChanged(TOGGLE_TRACK_HALF_WAVE.toString());
}

void ProjectSceneActionsController::toggleTrackSingleWave(const muse::actions::ActionQuery& q)
{
    IF_ASSERT_FAILED(q.params().size() >= 1) {
        return;
    }
    const project::IAudacityProjectPtr prj = globalContext()->currentProject();
    const auto viewState = prj ? prj->viewState() : nullptr;
    if (!viewState) {
        return;
    }
    viewState->toggleSingleWave(q.param("trackId").toInt());
    notifyActionCheckedChanged(TOGGLE_TRACK_SINGLE_WAVE.toString());
}

void ProjectSceneActionsController::setAllTracksView(int view)
{
    const auto project = globalContext()->currentProject();
    const auto viewState = project ? project->viewState() : nullptr;
    projectSceneUiState()->setAllTracksView(view);
    if (!viewState) {
        return;
    }
    // View > Toggle spectral view would turn the tracks back on its next press.
    if (view != 3 && viewState->globalSpectrogramToggleIsOn()) {
        viewState->toggleGlobalSpectrogramView();
    }
    for (const trackedit::TrackId trackId : project->trackeditProject()->trackIdList()) {
        applyTrackView(trackId, view);
    }
}

//! 0 Waveform, 1 Single wave, 2 Half-wave, 3 Spectrogram, 4 Multi-view.
void ProjectSceneActionsController::applyTrackView(trackedit::TrackId trackId, int view)
{
    const auto project = globalContext()->currentProject();
    const auto viewState = project ? project->viewState() : nullptr;
    const auto track = project ? project->trackeditProject()->track(trackId) : std::nullopt;
    if (!viewState || !track
        || (track->type != trackedit::TrackType::Mono && track->type != trackedit::TrackType::Stereo)) {
        return;
    }
    using trackedit::TrackViewType;
    viewState->setTrackViewType(trackId, view == 3 ? TrackViewType::Spectrogram
                                : view == 4 ? TrackViewType::WaveformAndSpectrogram : TrackViewType::Waveform);
    if (viewState->isHalfWave(trackId).val != (view == 2)) {
        viewState->toggleHalfWave(trackId);
    }
    if (track->type == trackedit::TrackType::Stereo && viewState->isSingleWave(trackId).val != (view == 1)) {
        viewState->toggleSingleWave(trackId);
    }
}

bool ProjectSceneActionsController::actionChecked(const ActionCode& actionCode) const
{
    for (int view = 0; view < 5; ++view) {
        if (actionCode == ALL_TRACKS_VIEW_CODES[view]) {
            return projectSceneUiState()->allTracksView() == view;
        }
    }
    QMap<std::string, bool> isChecked {
        { VERTICAL_RULERS_CODE, configuration()->isVerticalRulersVisible() },
        { RMS_IN_WAVEFORM_CODE, configuration()->isRMSInWaveformVisible() },
        { CLIPPING_IN_WAVEFORM_CODE, configuration()->isClippingInWaveformVisible() },
        { MINUTES_SECONDS_RULER, projectSceneUiState()->timelineRulerMode() == TimelineRulerMode::MINUTES_AND_SECONDS },
        { BEATS_MEASURES_RULER, projectSceneUiState()->timelineRulerMode() == TimelineRulerMode::BEATS_AND_MEASURES },
        { TOGGLE_PLAYBACK_ON_RULER_CLICK_ENABLED_CODE, configuration()->playbackOnRulerClickEnabled() },
        { TOGGLE_UPDATE_DISPLAY_WHILE_PLAYING_CODE, configuration()->updateDisplayWhilePlayingEnabled() },
        { TOGGLE_PINNED_PLAY_HEAD_CODE, configuration()->pinnedPlayHeadEnabled() },
        { BEAT_GRID_CODE, projectSceneUiState()->gridLinesVisible() },
        { COLOUR_WAVEFORM_CODE, projectSceneUiState()->clipWaveformRgb() }
    };

    return isChecked[actionCode];
}

Channel<ActionCode> ProjectSceneActionsController::actionCheckedChanged() const
{
    return m_actionCheckedChanged;
}

bool ProjectSceneActionsController::canReceiveAction(const ActionCode&) const
{
    return globalContext()->currentProject() != nullptr;
}

Channel<ActionCode> ProjectSceneActionsController::actionEnabledChanged() const
{
    return m_actionEnabledChanged;
}
