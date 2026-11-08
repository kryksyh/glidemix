/*
 * GlideMix
 */
#pragma once

#include <QColor>
#include <QQmlParserStatus>
#include <QQuickPaintedItem>
#include <QTimer>
#include <QtQmlIntegration>

#include "async/asyncable.h"
#include "modularity/ioc.h"

#include "mixhouse/imixhouselibraryconfiguration.h"
#include "mixhouse/imixhousetempomap.h"
#include "mixhouse/imixhousebeatgrids.h"

namespace au::mixhouse {
/// The TEMPO lane under the ruler: the master tempo line, drawn and edited the
/// way the parked MixHouse DJ app's tempo lane was (ArrangementWindow's
/// drawTempoCurve and its TempoPoint / TempoCurve drags).
///
/// Click adds a point and drags it; a point drags like a fader (the whole
/// 50..300 BPM range over 600 px of mouse travel, Shift for quarter speed);
/// Option-drag bends a segment; double-click, or right-click > Delete point,
/// removes a point.
///
/// GlideMix: the same field also shows the mix's energy (1-10) behind the
/// tempo line -- each playing song's energy rating, lower in its quiet parts
/// and fades (IMixHouseBeatGrids::energyCurve); blue calm .. red peak.
///
/// GlideMix: dim read-only markers show the line's tempo where the mix hands
/// over from one track to the next, with or without a point there (MixMeister's
/// row of tempos under the tracks, on our line).
class MixHouseTempoLane : public QQuickPaintedItem, public muse::Contextable, public muse::async::Asyncable
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(double frameStartTime READ frameStartTime WRITE setFrameStartTime NOTIFY viewChanged)
    Q_PROPERTY(double zoom READ zoom WRITE setZoom NOTIFY viewChanged)
    Q_PROPERTY(QColor lineColor MEMBER m_lineColor NOTIFY colorsChanged)
    //! The points and the handover markers: the theme's accent colour.
    Q_PROPERTY(QColor pointColor MEMBER m_pointColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor labelColor MEMBER m_labelColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor backgroundColor MEMBER m_backgroundColor NOTIFY colorsChanged)
    //! TEMPO: the tempo line and its points (editable); ENERGY: the energy fill.
    Q_PROPERTY(bool showTempo READ showTempo WRITE setShowTempo NOTIFY layersChanged)
    Q_PROPERTY(bool showEnergy READ showEnergy WRITE setShowEnergy NOTIFY layersChanged)

    muse::ContextInject<IMixHouseTempoMap> tempoMap { this };
    muse::ContextInject<IMixHouseBeatGrids> beatGrids { this };

public:
    explicit MixHouseTempoLane(QQuickItem* parent = nullptr);

    double frameStartTime() const { return m_frameStartTime; }
    void setFrameStartTime(double time);
    double zoom() const { return m_zoom; }
    void setZoom(double zoom);
    bool showTempo() const { return m_showTempo; }
    void setShowTempo(bool show);
    bool showEnergy() const { return m_showEnergy; }
    void setShowEnergy(bool show);

    //! The colour of an energy level (1 blue .. 10 red).
    static QColor energyColor(double energy);

    void paint(QPainter* painter) override;

    //! Removes point `index` (the last remaining point stays).
    Q_INVOKABLE bool deletePoint(int index);
    //! GlideMix: a point's tempo, and typing one in (Set tempo…). -1 for no point.
    Q_INVOKABLE double pointBpm(int index) const;
    Q_INVOKABLE bool setPointBpm(int index, double bpm);
    Q_INVOKABLE void clearPointHighlight();
    //! The glide from point `index` to the next: S-curve, or straight.
    Q_INVOKABLE bool isSCurve(int index) const;
    Q_INVOKABLE bool hasNextPoint(int index) const;
    Q_INVOKABLE void setGlideShape(int index, bool sCurve);

signals:
    //! Right-click on a point: the owner shows "Delete point" at x, y.
    void pointMenuRequested(int index, double x, double y);
    void viewChanged();
    void colorsChanged();
    void layersChanged();

protected:
    void componentComplete() override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseUngrabEvent() override;
    void hoverMoveEvent(QHoverEvent* event) override;


private:
    enum class Drag { None, Point, Curve };

    double timeToX(double time) const;
    double xToTime(double x) const;
    double bpmToY(double bpm) const;
    double yToBpm(double y) const;
    int hitPoint(const QPointF& pos, const IMixHouseTempoMap::Points& points) const;
    //! Recomputes the energy curve; repaints only when it changed (clip
    //! moves and fades have no notification of their own here).
    void refreshEnergy();
    void paintEnergy(QPainter* p);
    //! Re-reads the handovers; repaints only when they moved (clip moves have
    //! no notification of their own here).
    void refreshHandovers();
    void paintHandovers(QPainter* p, const IMixHouseTempoMap::Points& points);

    static constexpr double kEnergyColumnPx = 5.0;   // 4 px bars, 1 px gaps
    bool m_showTempo = true;
    bool m_showEnergy = false;
    std::vector<double> m_energy;
    QTimer m_energyPoll;
    std::vector<IMixHouseTempoMap::Handover> m_handovers;
    QTimer m_handoverPoll;

    double m_frameStartTime = 0.0;
    double m_zoom = 1.0;
    QColor m_lineColor { 0xb7, 0xff, 0x01 };   // the volume line's yellow (audio_envelope_line)
    QColor m_pointColor { 0xb7, 0xff, 0x01 };
    QColor m_labelColor { 0xc8, 0xc8, 0xc8 };  // parked kLabelText
    QColor m_backgroundColor { 0x0c, 0x12, 0x18 };

    Drag m_drag = Drag::None;
    IMixHouseTempoMap::Points m_before;   // the line at mouse-down (Esc/ungrab restores it)
    int m_dragIndex = -1;
    double m_dragStartY = 0.0;
    double m_dragStartValue = 0.0;        // BPM for a point, curvature for a bend
    int m_selectedIndex = -1;             // the point whose menu is open
};
}
