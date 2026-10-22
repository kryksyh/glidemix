/*
 * GlideMix
 */
#include "mixhousetempolane.h"

#include <algorithm>
#include <cmath>

#include <QCursor>
#include <QFontMetricsF>
#include <QHoverEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

using namespace au::mixhouse;

namespace {
using Points = IMixHouseTempoMap::Points;

constexpr double kInset = 6.0;            // parked bpmToY: 6 px above and below
constexpr double kDragSpanPx = 600.0;     // parked kTempoDragSpanPx
constexpr double kBendSpanPx = 90.0;      // parked TempoCurve drag
constexpr double kHitRadius = 9.0;        // parked point grab radius
constexpr double kLineHitDistance = 8.0;  // how near the line a click adds a point
constexpr double kMinPointGapSec = 0.01;

QString bpmLabel(double bpm)
{
    const double rounded = std::round(bpm * 10.0) / 10.0;
    return std::abs(rounded - std::round(rounded)) < 0.05
           ? QString::number(std::round(rounded), 'f', 0)
           : QString::number(rounded, 'f', 1);
}
}

MixHouseTempoLane::MixHouseTempoLane(QQuickItem* parent)
    : QQuickPaintedItem(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
    setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton);   // right: a point's menu
    setAcceptHoverEvents(true);
    setKeepMouseGrab(true);
    setAntialiasing(true);
    connect(this, &MixHouseTempoLane::colorsChanged, this, [this]() { update(); });
    connect(this, &MixHouseTempoLane::heightChanged, this, [this]() { update(); });
    m_energyPoll.setInterval(500);
    connect(&m_energyPoll, &QTimer::timeout, this, [this]() { refreshEnergy(); });
    connect(this, &MixHouseTempoLane::widthChanged, this, [this]() { refreshEnergy(); });
    connect(this, &QQuickItem::visibleChanged, this, [this]() { setShowEnergy(m_showEnergy); });
}

void MixHouseTempoLane::setShowTempo(bool show)
{
    if (m_showTempo == show) {
        return;
    }
    m_showTempo = show;
    emit layersChanged();
    update();
}

void MixHouseTempoLane::setShowEnergy(bool show)
{
    const bool changed = m_showEnergy != show;
    m_showEnergy = show;
    if (show && isVisible()) {
        if (!m_energyPoll.isActive()) {
            m_energyPoll.start();
        }
        refreshEnergy();
    } else {
        m_energyPoll.stop();
    }
    if (changed) {
        emit layersChanged();
        update();
    }
}

void MixHouseTempoLane::refreshEnergy()
{
    if (!m_showEnergy || !isVisible() || width() < 2.0 || !beatGrids() || m_zoom <= 0.0) {
        return;
    }
    const size_t count = static_cast<size_t>(std::ceil(width() / kEnergyColumnPx));
    std::vector<double> curve = beatGrids()->energyCurve(m_frameStartTime, kEnergyColumnPx / m_zoom, count);
    if (curve != m_energy) {
        m_energy = std::move(curve);
        update();
    }
}

QColor MixHouseTempoLane::energyColor(double energy)
{
    // Calm blue, steady green, building yellow-orange, peak red.
    static const QColor stops[] = { QColor(0x3B, 0x6E, 0xF0), QColor(0x22, 0xB8, 0xCF), QColor(0x2F, 0xC4, 0x5A),
                                    QColor(0xF2, 0xC2, 0x2E), QColor(0xF5, 0x7C, 0x22), QColor(0xE8, 0x30, 0x3A) };
    const double pos = std::clamp((energy - 1.0) / 9.0, 0.0, 1.0) * 5.0;
    const int i = std::min(4, static_cast<int>(pos));
    const double f = pos - i;
    const QColor& a = stops[i];
    const QColor& b = stops[i + 1];
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * f, a.greenF() + (b.greenF() - a.greenF()) * f,
                            a.blueF() + (b.blueF() - a.blueF()) * f);
}

void MixHouseTempoLane::paintEnergy(QPainter* p)
{
    // Energy 1 at the bottom, 10 at the top of the field, behind the line.
    const double top = kInset;
    const double bottom = height() - 2.0;
    const auto yOf = [&](double e) { return bottom - (e - 1.0) / 9.0 * (bottom - top); };
    // With the tempo line on top it stays a backdrop; alone it is the picture.
    const double alpha = m_showTempo ? 0.45 : 0.8;
    QPainterPath edge;
    bool drawing = false;
    for (size_t i = 0; i < m_energy.size(); ++i) {
        const double e = m_energy[i];
        const double x = i * kEnergyColumnPx;
        if (e <= 0.0) {
            drawing = false;
            continue;
        }
        QColor fill = energyColor(e);
        fill.setAlphaF(alpha);
        const double y = yOf(e);
        p->fillRect(QRectF(x, y, kEnergyColumnPx, bottom - y), fill);
        if (!drawing) {
            edge.moveTo(x, y);
            drawing = true;
        } else {
            edge.lineTo(x + kEnergyColumnPx / 2.0, y);
        }
    }
    QColor edgeColor = m_labelColor;
    edgeColor.setAlphaF(m_showTempo ? 0.35 : 0.8);
    p->setPen(QPen(edgeColor, 1.2));
    p->setBrush(Qt::NoBrush);
    p->drawPath(edge);
    if (!m_showTempo) {
        return;
    }
    // Which colour is which: a small legend at the bottom right.
    QFont font = p->font();
    font.setPixelSize(10);
    p->setFont(font);
    QColor legend = m_labelColor;
    legend.setAlphaF(0.6);
    p->setPen(legend);
    p->drawText(QRectF(0, 0, width() - 8.0, height() - 3.0), Qt::AlignRight | Qt::AlignBottom,
                QStringLiteral("ENERGY"));
}

void MixHouseTempoLane::componentComplete()
{
    QQuickPaintedItem::componentComplete();
    if (tempoMap()) {
        tempoMap()->tempoMapChanged().onNotify(this, [this]() { update(); });
    }
    if (beatGrids()) {
        beatGrids()->beatGridsChanged().onNotify(this, [this]() { refreshEnergy(); });
    }
    setShowEnergy(m_showEnergy);
}

void MixHouseTempoLane::setFrameStartTime(double time)
{
    if (!std::isfinite(time) || time == m_frameStartTime) {
        return;
    }
    m_frameStartTime = time;
    emit viewChanged();
    refreshEnergy();
    update();
}

void MixHouseTempoLane::setZoom(double zoom)
{
    if (!std::isfinite(zoom) || zoom <= 0.0 || zoom == m_zoom) {
        return;
    }
    m_zoom = zoom;
    emit viewChanged();
    refreshEnergy();
    update();
}

double MixHouseTempoLane::timeToX(double time) const
{
    return (time - m_frameStartTime) * m_zoom;   // TimelineContext::timeToPosition
}

double MixHouseTempoLane::xToTime(double x) const
{
    return m_frameStartTime + x / m_zoom;
}

double MixHouseTempoLane::bpmToY(double bpm) const
{
    const double t0 = kInset;
    const double t1 = height() - kInset;
    const double f = (std::clamp(bpm, IMixHouseTempoMap::kMinBpm, IMixHouseTempoMap::kMaxBpm) - IMixHouseTempoMap::kMinBpm)
                     / (IMixHouseTempoMap::kMaxBpm - IMixHouseTempoMap::kMinBpm);
    return t1 - f * (t1 - t0);
}

double MixHouseTempoLane::yToBpm(double y) const
{
    const double t0 = kInset;
    const double t1 = height() - kInset;
    const double f = std::clamp((t1 - y) / std::max(1.0, t1 - t0), 0.0, 1.0);
    return IMixHouseTempoMap::kMinBpm + f * (IMixHouseTempoMap::kMaxBpm - IMixHouseTempoMap::kMinBpm);
}

int MixHouseTempoLane::hitPoint(const QPointF& pos, const Points& points) const
{
    for (int i = 0; i < static_cast<int>(points.size()); ++i) {
        if (std::hypot(pos.x() - timeToX(points[i].time), pos.y() - bpmToY(points[i].bpm)) < kHitRadius) {
            return i;
        }
    }
    return -1;
}

void MixHouseTempoLane::paint(QPainter* p)
{
    const QRectF r = boundingRect();
    p->fillRect(r, m_backgroundColor);
    if (!tempoMap() || width() <= 0.0 || height() <= 0.0) {
        return;
    }
    p->setRenderHint(QPainter::Antialiasing, true);
    p->setClipRect(r);
    if (m_showEnergy) {
        paintEnergy(p);
    }
    if (!m_showTempo) {
        return;
    }
    const Points points = tempoMap()->points();

    // The line: from the left edge at the first BPM, through every point
    // (curved segments subdivided, as the parked drawTempoCurve did), then the
    // last BPM held to the right edge.
    QPainterPath path;
    path.moveTo(std::min(0.0, timeToX(points.front().time)), bpmToY(points.front().bpm));
    for (size_t i = 0; i < points.size(); ++i) {
        const QPointF a(timeToX(points[i].time), bpmToY(points[i].bpm));
        path.lineTo(a);
        if (i + 1 >= points.size()
            || (points[i].shape == IMixHouseTempoMap::Shape::Curve && std::abs(points[i].curve) < 1e-6)) {
            continue;
        }
        const QPointF b(timeToX(points[i + 1].time), bpmToY(points[i + 1].bpm));
        if (b.x() < 0.0 || a.x() > width()) {
            continue;
        }
        const int steps = std::clamp(static_cast<int>(std::abs(b.x() - a.x()) / 4.0), 2, 256);
        for (int k = 1; k < steps; ++k) {
            const double t = double(k) / steps;
            const double e = IMixHouseTempoMap::segmentShape(t, points[i]);
            path.lineTo(a.x() + (b.x() - a.x()) * t, a.y() + (b.y() - a.y()) * e);
        }
    }
    path.lineTo(std::max(width(), timeToX(points.back().time)), bpmToY(points.back().bpm));
    p->setPen(QPen(m_lineColor, 2.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p->setBrush(Qt::NoBrush);
    p->drawPath(path);

    // Points with their BPM; the readout flips below a point near the top.
    QFont font = p->font();
    font.setPixelSize(11);
    font.setBold(true);
    p->setFont(font);
    const QFontMetricsF fm(font);
    for (int i = 0; i < static_cast<int>(points.size()); ++i) {
        const QPointF pt(timeToX(points[i].time), bpmToY(points[i].bpm));
        if (pt.x() < -60.0 || pt.x() > width() + 10.0) {
            continue;
        }
        const bool active = (m_drag == Drag::Point && i == m_dragIndex) || i == m_selectedIndex;
        p->setPen(QPen(Qt::white, 1.5));
        p->setBrush(m_lineColor);
        p->drawEllipse(pt, active ? 6.0 : 5.0, active ? 6.0 : 5.0);
        p->setPen(m_labelColor);
        const bool below = pt.y() - 4.0 - fm.height() < 0.0;
        p->drawText(QPointF(pt.x() + 8.0, below ? pt.y() + fm.height() : pt.y() - 4.0), bpmLabel(points[i].bpm));
    }
}

void MixHouseTempoLane::mousePressEvent(QMouseEvent* e)
{
    if (!tempoMap() || !m_showTempo) {
        e->ignore();
        return;
    }
    const QPointF pos = e->position();
    Points points = tempoMap()->points();
    if (e->button() == Qt::RightButton) {
        const int index = hitPoint(pos, points);
        if (index < 0) {
            e->ignore();
            return;
        }
        m_selectedIndex = index;
        update();
        emit pointMenuRequested(index, pos.x(), pos.y());
        e->accept();
        return;
    }
    m_before = points;
    m_dragStartY = pos.y();

    // Option bends the segment under the cursor -- checked before the point
    // grab, as in the parked app.
    if (e->modifiers().testFlag(Qt::AltModifier)) {
        const double time = xToTime(pos.x());
        for (int i = 0; i + 1 < static_cast<int>(points.size()); ++i) {
            if (time >= points[i].time && time <= points[i + 1].time) {
                m_drag = Drag::Curve;
                m_dragIndex = i;
                m_dragStartValue = points[i].shape == IMixHouseTempoMap::Shape::Curve ? points[i].curve : 0.0;
                e->accept();
                return;
            }
        }
        e->accept();
        return;
    }

    int index = hitPoint(pos, points);
    if (index < 0) {
        // GlideMix: a new point only from a click on the line, and placed on
        // it -- so the tempo does not change until it is dragged. A click
        // anywhere else in the lane used to put a point there and pull the
        // whole line to it: easy to do by accident, mid-set.
        const double time = std::max(0.0, xToTime(pos.x()));
        const double lineBpm = tempoMap()->bpmAt(time);
        if (std::abs(pos.y() - bpmToY(lineBpm)) > kLineHitDistance) {
            e->ignore();
            return;
        }
        const IMixHouseTempoMap::Point point { time, lineBpm, 0.0 };
        auto it = std::upper_bound(points.begin(), points.end(), point.time,
                                   [](double t, const IMixHouseTempoMap::Point& p) { return t < p.time; });
        index = static_cast<int>(it - points.begin());
        points.insert(it, point);
        tempoMap()->setPoints(points, false);
    }
    m_drag = Drag::Point;
    m_dragIndex = index;
    m_dragStartValue = points[index].bpm;
    update();
    e->accept();
}

void MixHouseTempoLane::mouseMoveEvent(QMouseEvent* e)
{
    if (m_drag == Drag::None || !tempoMap()) {
        e->ignore();
        return;
    }
    Points points = tempoMap()->points();
    if (m_dragIndex < 0 || m_dragIndex >= static_cast<int>(points.size())) {
        e->ignore();
        return;
    }
    const QPointF pos = e->position();

    if (m_drag == Drag::Curve) {
        if (m_dragIndex + 1 >= static_cast<int>(points.size())) {
            return;
        }
        // Dragging up bows the line upward, whichever way the ramp runs.
        const double dy = (m_dragStartY - pos.y()) / kBendSpanPx;
        const bool rising = points[m_dragIndex + 1].bpm >= points[m_dragIndex].bpm;
        points[m_dragIndex].curve = std::clamp(m_dragStartValue + (rising ? dy : -dy), -1.0, 1.0);
        points[m_dragIndex].shape = IMixHouseTempoMap::Shape::Curve;   // bending replaces an S-curve
    } else {
        const double lo = m_dragIndex > 0 ? points[m_dragIndex - 1].time + kMinPointGapSec : 0.0;
        const double hi = m_dragIndex + 1 < static_cast<int>(points.size())
                          ? points[m_dragIndex + 1].time - kMinPointGapSec : 1e9;
        points[m_dragIndex].time = std::clamp(xToTime(pos.x()), lo, std::max(lo, hi));
        // Fader feel: relative to the grab, independent of the lane height.
        double sensitivity = (IMixHouseTempoMap::kMaxBpm - IMixHouseTempoMap::kMinBpm) / kDragSpanPx;
        if (e->modifiers().testFlag(Qt::ShiftModifier)) {
            sensitivity *= 0.25;
        }
        points[m_dragIndex].bpm = std::clamp(m_dragStartValue - (pos.y() - m_dragStartY) * sensitivity,
                                             IMixHouseTempoMap::kMinBpm, IMixHouseTempoMap::kMaxBpm);
    }
    tempoMap()->setPoints(points, false);
    e->accept();
}

void MixHouseTempoLane::mouseReleaseEvent(QMouseEvent* e)
{
    if (m_drag == Drag::None || !tempoMap()) {
        e->ignore();
        return;
    }
    m_drag = Drag::None;
    m_dragIndex = -1;
    tempoMap()->setPoints(tempoMap()->points(), true);
    e->accept();
}

void MixHouseTempoLane::mouseUngrabEvent()
{
    if (m_drag == Drag::None || !tempoMap()) {
        return;
    }
    m_drag = Drag::None;
    m_dragIndex = -1;
    tempoMap()->setPoints(m_before, false);
}

void MixHouseTempoLane::mouseDoubleClickEvent(QMouseEvent* e)
{
    e->accept();
    if (e->button() != Qt::LeftButton) {
        return;
    }
    if (!tempoMap() || !m_showTempo) {
        return;
    }
    // The first click of a double-click already added (or grabbed) a point;
    // restore the line from before it and remove the point under the cursor.
    Points points = m_before.empty() ? tempoMap()->points() : m_before;
    const int index = hitPoint(e->position(), points);
    m_drag = Drag::None;
    m_dragIndex = -1;
    if (index >= 0 && points.size() > 1) {
        points.erase(points.begin() + index);
    }
    tempoMap()->setPoints(points, true);
}

void MixHouseTempoLane::hoverMoveEvent(QHoverEvent* e)
{
    if (!tempoMap() || !m_showTempo) {
        return;
    }
    setCursor(hitPoint(e->position(), tempoMap()->points()) >= 0 ? Qt::SizeAllCursor : Qt::ArrowCursor);
}

bool MixHouseTempoLane::deletePoint(int index)
{
    if (!tempoMap() || m_drag != Drag::None) {
        return false;
    }
    Points points = tempoMap()->points();
    if (index < 0 || index >= static_cast<int>(points.size()) || points.size() <= 1) {
        return false;
    }
    points.erase(points.begin() + index);
    m_selectedIndex = -1;
    tempoMap()->setPoints(points, true);
    update();
    return true;
}

bool MixHouseTempoLane::isSCurve(int index) const
{
    if (!tempoMap()) {
        return false;
    }
    const Points points = tempoMap()->points();
    return index >= 0 && index < static_cast<int>(points.size())
           && points[index].shape == IMixHouseTempoMap::Shape::SCurve;
}

bool MixHouseTempoLane::hasNextPoint(int index) const
{
    return tempoMap() && index >= 0 && index + 1 < static_cast<int>(tempoMap()->points().size());
}

void MixHouseTempoLane::setGlideShape(int index, bool sCurve)
{
    if (!tempoMap() || m_drag != Drag::None) {
        return;
    }
    Points points = tempoMap()->points();
    if (index < 0 || index + 1 >= static_cast<int>(points.size())) {
        return;
    }
    points[index].shape = sCurve ? IMixHouseTempoMap::Shape::SCurve : IMixHouseTempoMap::Shape::Curve;
    points[index].curve = 0.0;
    tempoMap()->setPoints(points, true);
    update();
}

void MixHouseTempoLane::clearPointHighlight()
{
    if (m_selectedIndex >= 0) {
        m_selectedIndex = -1;
        update();
    }
}
