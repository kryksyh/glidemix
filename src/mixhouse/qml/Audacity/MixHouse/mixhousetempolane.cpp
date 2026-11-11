/*
 * GlideMix
 */
#include "mixhousetempolane.h"

#include <QLinearGradient>

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
    m_handoverPoll.setInterval(500);
    connect(&m_handoverPoll, &QTimer::timeout, this, [this]() { refreshHandovers(); });
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
    // The gradient meters' colours: calm green, building yellow, peak red
    // (meter_gradient_green/yellow, meter_clipped).
    static const QColor stops[] = { QColor(0x50, 0xDF, 0x46), QColor(0xFF, 0xE1, 0x00), QColor(0xEF, 0x47, 0x6F) };
    const double pos = std::clamp((energy - 2.0) / 7.0, 0.0, 1.0) * 2.0;   // 2 and below green, 9 and up red
    const int i = std::min(1, static_cast<int>(pos));
    const double f = pos - i;
    const QColor& a = stops[i];
    const QColor& b = stops[i + 1];
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * f, a.greenF() + (b.greenF() - a.greenF()) * f,
                            a.blueF() + (b.blueF() - a.blueF()) * f);
}

void MixHouseTempoLane::paintEnergy(QPainter* p)
{
    // Like a row of level meters: one bar per column, as high as the energy
    // (1 at the bottom, 10 at the top), lit with the meters' gradient --
    // green low down, yellow in the middle, red at the top -- so a peak
    // shows red tops and a breakdown short green bars.
    const double top = kInset;
    const double bottom = height() - 2.0;
    const auto yOf = [&](double e) { return bottom - (e - 1.0) / 9.0 * (bottom - top); };
    QLinearGradient meter(0.0, bottom, 0.0, top);
    meter.setColorAt(0.0, QColor(0x50, 0xDF, 0x46));
    meter.setColorAt(0.55, QColor(0xFF, 0xE1, 0x00));
    meter.setColorAt(1.0, QColor(0xEF, 0x47, 0x6F));
    p->setOpacity(m_showTempo ? 0.85 : 1.0);
    p->setPen(Qt::NoPen);
    p->setBrush(meter);
    const double gap = kEnergyColumnPx >= 4.0 ? 1.0 : 0.0;
    for (size_t i = 0; i < m_energy.size(); ++i) {
        const double e = m_energy[i];
        if (e <= 0.0) {
            continue;
        }
        const double x = i * kEnergyColumnPx;
        const double y = yOf(e);
        p->drawRect(QRectF(x, y, kEnergyColumnPx - gap, bottom - y));
    }
    p->setOpacity(1.0);
    if (!m_showTempo) {
        return;
    }
    // Which colour is which: a small legend at the bottom right.
    QFont font = p->font();
    font.setPixelSize(10);
    p->setFont(font);
    QColor legend = m_labelColor;
    legend.setAlphaF(0.7);
    p->setPen(legend);
    p->drawText(QRectF(0, 0, width() - 8.0, height() - 3.0), Qt::AlignRight | Qt::AlignBottom,
                QStringLiteral("ENERGY"));
}

void MixHouseTempoLane::refreshHandovers()
{
    if (!tempoMap() || !isVisible() || !m_showTempo) {
        return;
    }
    std::vector<IMixHouseTempoMap::Handover> handovers = tempoMap()->handovers();
    const bool same = handovers.size() == m_handovers.size()
                      && std::equal(handovers.begin(), handovers.end(), m_handovers.begin(),
                                    [](const IMixHouseTempoMap::Handover& a, const IMixHouseTempoMap::Handover& b) {
        return a.start == b.start && a.end == b.end;
    });
    if (!same) {
        m_handovers = std::move(handovers);
        update();
    }
}

void MixHouseTempoLane::paintHandovers(QPainter* p, const Points& points)
{
    QFont font = p->font();
    font.setPixelSize(10);
    font.setBold(false);
    p->setFont(font);
    const QFontMetricsF fm(font);
    QColor tick = m_pointColor;
    tick.setAlphaF(0.8);
    QColor text = m_labelColor;
    text.setAlphaF(0.75);
    double labelEnd = -1e9;   // labels never run into each other: zoomed out, some give way

    const auto mark = [&](double time, bool withLabel) {
        const double x = timeToX(time);
        if (x < -60.0 || x > width() + 10.0) {
            return;
        }
        const double bpm = tempoMap()->bpmAt(time);
        const double y = bpmToY(bpm);
        p->setPen(QPen(tick, 1.0));
        p->drawLine(QPointF(x, y - 5.0), QPointF(x, y + 5.0));

        // A point here already says it.
        const bool onPoint = std::any_of(points.begin(), points.end(), [&](const IMixHouseTempoMap::Point& point) {
            return std::abs(timeToX(point.time) - x) < kHitRadius && bpmLabel(point.bpm) == bpmLabel(bpm);
        });
        if (!withLabel || onPoint || x + 3.0 < labelEnd) {
            return;
        }
        const QString label = bpmLabel(bpm);
        const bool above = y + 6.0 + fm.height() > height();
        p->setPen(text);
        p->drawText(QPointF(x + 3.0, above ? y - 7.0 : y + 6.0 + fm.ascent()), label);
        labelEnd = x + 3.0 + fm.horizontalAdvance(label) + 6.0;
    };
    for (const IMixHouseTempoMap::Handover& h : m_handovers) {
        mark(h.start, true);
        if (h.end > h.start) {
            // The same tempo at both ends: one number is enough.
            mark(h.end, bpmLabel(tempoMap()->bpmAt(h.end)) != bpmLabel(tempoMap()->bpmAt(h.start)));
        }
    }
}

void MixHouseTempoLane::componentComplete()
{
    QQuickPaintedItem::componentComplete();
    if (tempoMap()) {
        tempoMap()->tempoMapChanged().onNotify(this, [this]() {
            refreshHandovers();
            update();
        });
    }
    refreshHandovers();
    m_handoverPoll.start();
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
    // A dark edge keeps the yellow line readable over the energy colours.
    p->setBrush(Qt::NoBrush);
    if (m_showEnergy) {
        p->setPen(QPen(QColor(0, 0, 0, 150), 3.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p->drawPath(path);
    }
    // GlideMix (Chris, 2026-11-11): Logic's thin line and small dots, like the clip lines.
    p->setPen(QPen(m_lineColor, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p->drawPath(path);

    paintHandovers(p, points);

    // Points with their BPM; the readout flips below a point near the top.
    QFont font = p->font();
    font.setPixelSize(10);
    font.setWeight(QFont::DemiBold);
    p->setFont(font);
    const QFontMetricsF fm(font);
    for (int i = 0; i < static_cast<int>(points.size()); ++i) {
        const QPointF pt(timeToX(points[i].time), bpmToY(points[i].bpm));
        if (pt.x() < -60.0 || pt.x() > width() + 10.0) {
            continue;
        }
        const bool active = (m_drag == Drag::Point && i == m_dragIndex) || i == m_selectedIndex;
        p->setPen(active ? QPen(Qt::white, 1.0) : QPen(Qt::NoPen));
        p->setBrush(m_pointColor);
        p->drawEllipse(pt, active ? 5.0 : 3.5, active ? 5.0 : 3.5);
        const bool below = pt.y() - 6.0 - fm.height() < 0.0;
        const QPointF at(pt.x() + 6.0, below ? pt.y() + 6.0 + fm.ascent() : pt.y() - 6.0);
        p->setPen(QColor(0, 0, 0, 170));   // soft shadow, as on the clip lines
        p->drawText(at + QPointF(1.0, 1.0), bpmLabel(points[i].bpm));
        p->setPen(m_labelColor);
        p->drawText(at, bpmLabel(points[i].bpm));
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

double MixHouseTempoLane::pointBpm(int index) const
{
    if (!tempoMap()) {
        return -1.0;
    }
    const Points points = tempoMap()->points();
    return index >= 0 && index < static_cast<int>(points.size()) ? points[index].bpm : -1.0;
}

bool MixHouseTempoLane::setPointBpm(int index, double bpm)
{
    if (!tempoMap() || m_drag != Drag::None || !std::isfinite(bpm)) {
        return false;
    }
    Points points = tempoMap()->points();
    if (index < 0 || index >= static_cast<int>(points.size())) {
        return false;
    }
    points[index].bpm = std::clamp(bpm, IMixHouseTempoMap::kMinBpm, IMixHouseTempoMap::kMaxBpm);
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
