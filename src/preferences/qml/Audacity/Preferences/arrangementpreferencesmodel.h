/*
 * GlideMix
 */
#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>

#include "modularity/ioc.h"

#include "projectscene/iprojectsceneuistate.h"
#include "mixhouse/imixhouselibraryconfiguration.h"
#include "framework/actions/iactionsdispatcher.h"

namespace au::preferences {
/// Backs WaveformsPreferencesPage.qml (formerly Arrangement Mode): how
/// clips are drawn on the timeline, what the lanes are painted with, and
/// whether the bar and beat lines are drawn behind them.
///
/// These three are the same settings View > Show beat grid and View > Colour
/// waveform flip, and they are saved WITH THE WORKSPACE, not globally: each
/// workspace carries its own lane look, so switching workspace changes it.
class ArrangementPreferencesModel : public QObject, public muse::Contextable
{
    Q_OBJECT
    QML_ELEMENT

    muse::ContextInject<projectscene::IProjectSceneUiState> uiState { this };
    muse::ContextInject<muse::actions::IActionsDispatcher> dispatcher { this };
    muse::GlobalInject<au::mixhouse::IMixHouseLibraryConfiguration> libraryConfiguration;

    /// 0 = Audacity's own waveform in the clip's colour, 1 = rekordbox's
    /// colour waveform on black.
    Q_PROPERTY(int waveformStyle READ waveformStyle WRITE setWaveformStyle NOTIFY changed)
    /// 0 = follows the waveform style, 1 = always black, 2 = always the theme's.
    Q_PROPERTY(int laneBackground READ laneBackground WRITE setLaneBackground NOTIFY changed)
    Q_PROPERTY(bool beatGridVisible READ beatGridVisible WRITE setBeatGridVisible NOTIFY changed)
    /// GlideMix: the colour waveform as left and right lanes.
    Q_PROPERTY(bool stereoWaveform READ stereoWaveform WRITE setStereoWaveform NOTIFY changed)
    /// How every song's track is drawn, as View > Track view: 0 Waveform,
    /// 1 Single wave, 2 Half-wave, 3 Spectrogram, 4 Multi-view, -1 none chosen.
    Q_PROPERTY(int trackView READ trackView WRITE setTrackView NOTIFY changed)
    /// GlideMix: the coloured Intro / Chorus ribbon (the clip menu's "Show phrase detection").
    Q_PROPERTY(bool phraseRibbon READ phraseRibbon WRITE setPhraseRibbon NOTIFY changed)
    /// GlideMix: the mouse wheel zooms the timeline; Cmd/Ctrl + wheel scrolls the tracks.
    Q_PROPERTY(bool wheelZooms READ wheelZooms WRITE setWheelZooms NOTIFY changed)

public:
    explicit ArrangementPreferencesModel(QObject* parent = nullptr);

    int waveformStyle() const;
    void setWaveformStyle(int style);
    int laneBackground() const;
    void setLaneBackground(int mode);
    bool beatGridVisible() const;
    void setBeatGridVisible(bool visible);
    bool stereoWaveform() const;
    void setStereoWaveform(bool stereo);
    int trackView() const;
    void setTrackView(int view);
    bool phraseRibbon() const;
    void setPhraseRibbon(bool visible);
    bool wheelZooms() const;
    void setWheelZooms(bool zooms);

signals:
    void changed();
};
}
