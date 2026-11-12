/*
* Audacity: A Digital Audio Editor
*/
#pragma once

#include <QObject>
#include <QVariantList>

#include "global/async/asyncable.h"

#include "modularity/ioc.h"
#include "context/iglobalcontext.h"
#include "playback/iplaybackcontroller.h"
#include "playback/iplaybackconfiguration.h"
#include "record/irecordcontroller.h"
#include "trackedit/itrackeditinteraction.h"

#include "playback/view/common/playbackmetermodel.h"
#include "types/val.h"

namespace au::projectscene {
class TrackViewStateModel : public QObject, public muse::Contextable, public muse::async::Asyncable
{
    Q_OBJECT

    Q_PROPERTY(QVariant trackId READ trackId WRITE setTrackId NOTIFY trackIdChanged FINAL)
    Q_PROPERTY(int trackHeight READ trackHeight NOTIFY trackHeightChanged FINAL)
    Q_PROPERTY(bool isTrackCollapsed READ isTrackCollapsed NOTIFY isTrackCollapsedChanged FINAL)
    Q_PROPERTY(double channelHeightRatio READ channelHeightRatio NOTIFY channelHeightRatioChanged FINAL)
    Q_PROPERTY(bool isSingleWave READ isSingleWave NOTIFY isSingleWaveChanged FINAL)
    //! GlideMix: parameters shown in lanes of their own under this track:
    //! [{ instanceId, pluginId, parameterId, label, color, defaultValue, stepped }].
    Q_PROPERTY(QVariantList automationLanes READ automationLanes NOTIFY automationLanesChanged FINAL)
    Q_PROPERTY(int automationLanesHeight READ automationLanesHeight NOTIFY automationLanesChanged FINAL)
    Q_PROPERTY(QVariant displayBounds READ displayBounds NOTIFY displayBoundsChanged FINAL)
    Q_PROPERTY(bool isLinear READ isLinear NOTIFY isLinearChanged FINAL)

    Q_PROPERTY(bool isPlaying READ isPlaying NOTIFY isPlayingChanged FINAL)
    Q_PROPERTY(bool isRecording READ isRecording NOTIFY isRecordingChanged FINAL)

    Q_PROPERTY(playback::PlaybackMeterModel * meterModel READ meterModel NOTIFY meterModelChanged FINAL)
    //! GlideMix Samples (B2): this track is a song's sample line / the song's
    //! sample line is open right under it.
    Q_PROPERTY(bool isSampleLine READ isSampleLine NOTIFY sampleLineChanged FINAL)
    Q_PROPERTY(bool hasSampleLine READ hasSampleLine NOTIFY sampleLineChanged FINAL)
    //! The song's sample line is open and not folded (on a sample line: itself).
    Q_PROPERTY(bool sampleLineShown READ sampleLineShown NOTIFY sampleLineChanged FINAL)

    muse::GlobalInject<playback::IPlaybackConfiguration> playbackConfiguration;

    muse::ContextInject<context::IGlobalContext> globalContext{ this };
    muse::ContextInject<playback::IPlaybackController> playbackController{ this };
    muse::ContextInject<record::IRecordController> recordController{ this };
    muse::ContextInject<trackedit::ITrackeditInteraction> trackeditInteraction{ this };

public:
    TrackViewStateModel(QObject* parent = nullptr);

    Q_INVOKABLE void init();

    QVariant trackId() const;
    void setTrackId(const QVariant& newTrackId);

    int trackHeight() const;
    Q_INVOKABLE void changeTrackHeight(int deltaY);

    bool isTrackCollapsed() const;
    double channelHeightRatio() const;
    bool isSingleWave() const { return m_isSingleWave.val; }
    QVariantList automationLanes() const;
    int automationLanesHeight() const;
    Q_INVOKABLE void removeAutomationLane(const QString& instanceId, const QString& parameterId);
    Q_INVOKABLE void changeChannelHeightRatio(double ratio);

    QVariant displayBounds() const;

    bool isLinear() const;

    playback::PlaybackMeterModel* meterModel() const;

    bool isPlaying() const;
    bool isRecording() const;

    bool isSampleLine() const;
    bool hasSampleLine() const;
    bool sampleLineShown() const;
    //! The SAMPLES button: opens the song's sample line right under it (a
    //! track named "Sample line"), then shows / hides (folds) it. On a sample
    //! line: its own hide button.
    Q_INVOKABLE void toggleSampleLine();

signals:
    void trackIdChanged();
    void trackHeightChanged();
    void isTrackCollapsedChanged();
    void channelHeightRatioChanged();
    void isSingleWaveChanged();
    void automationLanesChanged();
    void meterModelChanged();
    void isPlayingChanged();
    void isRecordingChanged();
    void displayBoundsChanged();
    void isLinearChanged();
    void sampleLineChanged();

private:
    IProjectViewStatePtr viewState() const;

    trackedit::TrackId m_trackId = -1;
    muse::ValCh<int> m_trackHeight;
    muse::ValCh<bool> m_isTrackCollapsed;
    muse::ValCh<double> m_channelHeightRatio;
    muse::ValCh<bool> m_isSingleWave;
    muse::ValCh<std::pair<float, float> > m_displayBounds;
    muse::ValCh<int> m_rulerType;

    playback::PlaybackMeterModel* m_meterModel = nullptr;
};
}
