/*
 * GlideMix
 */
#pragma once

#include <QColor>
#include <QQmlParserStatus>
#include <QQuickPaintedItem>
#include <QtQmlIntegration>

#include "async/asyncable.h"
#include "modularity/ioc.h"

#include "mixhouse/imixhouselibraryconfiguration.h"
#include "mixhouse/imixhousetempomap.h"

namespace au::mixhouse {
/// The TEMPO lane under the ruler: the master tempo line, drawn and edited the
/// way the parked MixHouse DJ app's tempo lane was (ArrangementWindow's
/// drawTempoCurve and its TempoPoint / TempoCurve drags).
///
/// Click adds a point and drags it; a point drags like a fader (the whole
/// 50..300 BPM range over 600 px of mouse travel, Shift for quarter speed);
/// Option-drag bends a segment; double-click, or right-click > Delete point,
/// removes a point.
class MixHouseTempoLane : public QQuickPaintedItem, public muse::Contextable, public muse::async::Asyncable
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(double frameStartTime READ frameStartTime WRITE setFrameStartTime NOTIFY viewChanged)
    Q_PROPERTY(double zoom READ zoom WRITE setZoom NOTIFY viewChanged)
    Q_PROPERTY(QColor lineColor MEMBER m_lineColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor labelColor MEMBER m_labelColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor backgroundColor MEMBER m_backgroundColor NOTIFY colorsChanged)

    muse::ContextInject<IMixHouseTempoMap> tempoMap { this };

public:
    explicit MixHouseTempoLane(QQuickItem* parent = nullptr);

    double frameStartTime() const { return m_frameStartTime; }
    void setFrameStartTime(double time);
    double zoom() const { return m_zoom; }
    void setZoom(double zoom);

    void paint(QPainter* painter) override;

    //! Removes point `index` (the last remaining point stays).
    Q_INVOKABLE bool deletePoint(int index);
    Q_INVOKABLE void clearPointHighlight();

signals:
    //! Right-click on a point: the owner shows "Delete point" at x, y.
    void pointMenuRequested(int index, double x, double y);
    void viewChanged();
    void colorsChanged();

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

    double m_frameStartTime = 0.0;
    double m_zoom = 1.0;
    QColor m_lineColor { 0x5b, 0xd0, 0x8a };   // parked activeTempoColor() for the master line
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
