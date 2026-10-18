/*
* Audacity: A Digital Audio Editor
*/
#pragma once

#include <QQuickPaintedItem>

#include "modularity/ioc.h"
#include "context/iglobalcontext.h"
#include "trackedit/iprojecthistory.h"
#include "global/async/asyncable.h"

#include "iwavepainter.h"
#include "iprojectsceneuistate.h"
#include "playback/itrackplaybackcontrol.h"
#include "mixhouse/imixhousebeatgrids.h"
#include "mixhouse/mixhousehdwaveform.h"
#include "../timeline/timelinecontext.h"
#include "types/projectscenetypes.h"

class WaveClipItem;
namespace au::projectscene {
class WaveView : public QQuickPaintedItem, public muse::async::Asyncable, public muse::Contextable
{
    Q_OBJECT
    Q_PROPERTY(TimelineContext * context READ timelineContext WRITE setTimelineContext NOTIFY timelineContextChanged FINAL)
    Q_PROPERTY(ClipKey clipKey READ clipKey WRITE setClipKey NOTIFY clipKeyChanged FINAL)
    Q_PROPERTY(QColor clipColor READ clipColor WRITE setClipColor NOTIFY clipColorChanged FINAL)
    Q_PROPERTY(QColor clipSelectedColor READ clipSelectedColor WRITE setClipSelectedColor NOTIFY clipSelectedColorChanged FINAL)
    // GlideMix: the track's colour intensity (0..1) and the canvas it fades into.
    Q_PROPERTY(double colorIntensity READ colorIntensity WRITE setColorIntensity NOTIFY colorIntensityChanged FINAL)
    Q_PROPERTY(QColor canvasColor READ canvasColor WRITE setCanvasColor NOTIFY canvasColorChanged FINAL)
    Q_PROPERTY(bool clipSelected READ clipSelected WRITE setClipSelected NOTIFY clipSelectedChanged FINAL)
    Q_PROPERTY(QColor backgroundColor READ backgroundColor NOTIFY backgroundColorChanged FINAL)
    Q_PROPERTY(double channelHeightRatio READ channelHeightRatio WRITE setChannelHeightRatio NOTIFY channelHeightRatioChanged FINAL)

    Q_PROPERTY(double startTime READ startTime NOTIFY clipTimeChanged)
    Q_PROPERTY(double endTime READ endTime NOTIFY clipTimeChanged)
    Q_PROPERTY(double itemStartTime READ itemStartTime NOTIFY clipTimeChanged)
    Q_PROPERTY(double itemEndTime READ itemEndTime NOTIFY clipTimeChanged)
    Q_PROPERTY(ClipTime clipTime READ clipTime WRITE setClipTime NOTIFY clipTimeChanged FINAL)

    Q_PROPERTY(bool isNearSample READ isNearSample WRITE setIsNearSample NOTIFY isNearSampleChanged FINAL)
    Q_PROPERTY(bool isStemPlot READ isStemPlot WRITE setIsStemPlot NOTIFY isStemPlotChanged FINAL)
    Q_PROPERTY(int currentChannel READ currentChannel WRITE setCurrentChannel FINAL)
    Q_PROPERTY(bool isIsolationMode READ isIsolationMode WRITE setIsIsolationMode NOTIFY isIsolationModeChanged FINAL)
    Q_PROPERTY(bool multiSampleEdit READ multiSampleEdit WRITE setMultiSampleEdit NOTIFY multiSampleEditChanged FINAL)
    Q_PROPERTY(bool isBrush READ isBrush WRITE setIsBrush NOTIFY isBrushChanged FINAL)
    Q_PROPERTY(bool isLinear READ isLinear WRITE setIsLinear FINAL)
    Q_PROPERTY(double dbRange READ dbRange WRITE setDbRange FINAL)
    Q_PROPERTY(QVariant displayBounds READ displayBounds WRITE setDisplayBounds FINAL)

    muse::GlobalInject<IProjectSceneConfiguration> configuration;

    muse::ContextInject<au::context::IGlobalContext> globalContext{ this };
    muse::ContextInject<au::projectscene::IWavePainter> wavePainter{ this };
    muse::ContextInject<au::trackedit::IProjectHistory> projectHistory{ this };
    muse::ContextInject<au::mixhouse::IMixHouseBeatGrids> beatGrids{ this };   // GlideMix: bar marks
    muse::GlobalInject<au::mixhouse::IMixHouseHdWaveforms> hdWaveforms;        // GlideMix: HD colour waveform
    muse::ContextInject<IProjectSceneUiState> projectSceneUiState { this };    // GlideMix: the workspace's lane look
    muse::ContextInject<au::playback::ITrackPlaybackControl> trackPlaybackControl { this };   // GlideMix: track volume

public:
    WaveView(QQuickItem* parent = nullptr);
    ~WaveView() override;

    TimelineContext* timelineContext() const;
    void setTimelineContext(TimelineContext* newContext);
    ClipKey clipKey() const;
    void setClipKey(const ClipKey& newClipKey);
    QColor clipColor() const;
    double colorIntensity() const;
    void setColorIntensity(double intensity);
    QColor canvasColor() const;
    void setCanvasColor(const QColor& color);
    void setClipColor(const QColor& newClipColor);
    QColor clipSelectedColor() const;
    void setClipSelectedColor(const QColor& newClipSelectedColor);
    bool clipSelected() const;
    void setClipSelected(bool newClipSelected);
    QColor backgroundColor() const;
    double startTime() const { return m_clipTime.startTime; }
    double endTime()   const { return m_clipTime.endTime; }
    double itemStartTime() const { return m_clipTime.itemStartTime; }
    double itemEndTime()   const { return m_clipTime.itemEndTime; }
    ClipTime clipTime() const;
    void setClipTime(const ClipTime& newClipTime);
    double channelHeightRatio() const;
    void setChannelHeightRatio(double channelHeightRatio);
    bool isNearSample() const;
    void setIsNearSample(bool isNearSample);
    bool isStemPlot() const;
    void setIsStemPlot(bool isStemPlot);
    int currentChannel() const;
    void setCurrentChannel(int currentChannel);
    bool isIsolationMode() const;
    void setIsIsolationMode(bool isIsolationMode);
    bool multiSampleEdit() const;
    void setMultiSampleEdit(bool multiSampleEdit);
    bool isBrush() const;
    void setIsBrush(bool isBrush);
    bool isLinear() const;
    void setIsLinear(bool isLinear);
    double dbRange() const;
    void setDbRange(double dbRange);
    QVariant displayBounds() const;
    void setDisplayBounds(const QVariant& displayBounds);

    Q_INVOKABLE QColor transformColor(const QColor& originalColor) const;
    Q_INVOKABLE void setLastMousePos(const unsigned int x, const unsigned int y);
    //! GlideMix: MixMeister anchors, x in this view (-1 = none): [intro, outro].
    Q_INVOKABLE QVariantList anchorPositions() const;
    Q_INVOKABLE void dragAnchor(int which, double x, bool completed);
    Q_INVOKABLE void resetAnchors();
    Q_INVOKABLE void setLastClickPos(unsigned lastX, unsigned lastY, unsigned int x, const unsigned int y);
    Q_INVOKABLE void smoothLastClickPos(unsigned int x, const unsigned int y);
    Q_INVOKABLE void setIsolatedPoint(unsigned int x, unsigned int y);

    void paint(QPainter* painter) override;

signals:
    void anchorsChanged();
    void timelineContextChanged();
    void clipKeyChanged();
    void clipColorChanged();
    void colorIntensityChanged();
    void canvasColorChanged();
    void clipSelectedColorChanged();
    void clipTimeChanged();
    void clipSelectedChanged();
    void backgroundColorChanged();
    void channelHeightRatioChanged();
    void isNearSampleChanged();
    void isStemPlotChanged();
    void isIsolationModeChanged();
    void multiSampleEditChanged();
    void isBrushChanged();

private:
    //! GlideMix: rekordbox's colour waveform instead of Audacity's.
    //! False when the clip's track has no analysis to colour it with.
    QColor rgbBackground() const;
    double phraseRibbonHeight(const IWavePainter::Params& params) const;
    bool paintRgbWave(QPainter& painter, const IWavePainter::Params& params);
    bool paintHdLane(QPainter& painter, const IWavePainter::Params& params);
    //! The track's volume slider as an amplitude (1 = 0 dB).
    double trackGain() const;
    //! Zoomed in past the stored columns: paint the audio in greys and multiply
    //! rekordbox's colour over it, so the shape stays sample-sharp.
    void paintTintedStyle(IWavePainter::Params& params) const;
    void paintRgbTint(QPainter& painter, const IWavePainter::Params& params);
    void paintBarMarks(QPainter& painter, const IWavePainter::Params& params, double bottomInset = 0.0);
    void paintCues(QPainter& painter, const IWavePainter::Params& params);
    void paintAnchors(QPainter& painter, const IWavePainter::Params& params);
    //! Returns the ribbon height (0 without phrases).
    double paintPhrases(QPainter& painter, const IWavePainter::Params& params);
    bool m_beatGridsSubscribed = false;

    void updateView();
    void onWaveZoomChanged();
    void paintRecordingPlaceholder(QPainter& painter, const IWavePainter::Params& params);
    IWavePainter::Params getWavePainterParams() const;
    void applyColorfulStyle(IWavePainter::Params& params, const QColor& clipColor, const QColor& clipSelectedColor, bool selected) const;
    void applyClassicStyle(IWavePainter::Params& params, bool selected) const;
    void pushProjectHistorySampleEdit();

    context::IPlaybackStatePtr playbackState() const;

    TimelineContext* m_context = nullptr;
    ClipKey m_clipKey;
    QColor m_clipColor;
    double m_colorIntensity = 1.0;
    QColor m_canvasColor = QColor(0x1b, 0x1f, 0x23);
    QColor m_clipSelectedColor;
    double m_clipLeft = 0;
    double m_channelHeightRatio = 0.5;
    bool m_clipSelected = false;
    ClipTime m_clipTime;
    bool m_isNearSample = false;
    bool m_isStemPlot = false;
    bool m_isIsolationMode = false;
    bool m_multiSampleEdit = false;
    bool m_isBrush = false;
    bool m_isLinear = false;
    double m_dbRange = -60.0;
    std::pair<float, float> m_displayBounds = { -1.0f, 1.0f };

    std::optional<int> m_currentChannel;
    std::optional<QPoint> m_lastClickedPoint;
};
}
