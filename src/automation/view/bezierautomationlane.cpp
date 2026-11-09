/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <QLinearGradient>
#include "bezierautomationlane.h"

#include <algorithm>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QHoverEvent>
#include <QKeyEvent>
#include <QCursor>
#include <QPixmap>

using namespace au::automation;

namespace {
// Every lane alive, so the transition pill's lane can ask whether a press
// lands on a clip's point (anyLaneNodeAtScene).
QSet<BezierAutomationLane*>& allLanes()
{
    static QSet<BezierAutomationLane*> lanes;
    return lanes;
}
}

BezierAutomationLane::BezierAutomationLane(QQuickItem* parent) : QQuickPaintedItem(parent)
{
    setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton);   // right: a point's menu
    setAcceptHoverEvents(true);
    // The clip underneath also supports dragging.  Once an automation edit
    // starts, keep the grab so crossing Qt's drag threshold cannot hand the
    // gesture to the clip and make a node appear immovable.
    setKeepMouseGrab(true);
    setKeepTouchGrab(true);
    setAntialiasing(true);
    connect(this, &BezierAutomationLane::lineColorChanged, this, [this] { update(); });
    allLanes().insert(this);
}

BezierAutomationLane::~BezierAutomationLane()
{
    allLanes().remove(this);
}

bool BezierAutomationLane::anyLaneNodeAtScene(double sceneX, double sceneY) const
{
    const QPointF scenePoint(sceneX, sceneY);
    for (BezierAutomationLane* lane : allLanes()) {
        if (lane == this || !lane->isVisible() || !lane->isEnabled() || lane->window() != window()) {
            continue;
        }
        const QPointF p = lane->mapFromScene(scenePoint);
        if (!lane->contains(p)) {
            continue;
        }
        if (lane->hitNode(p) >= 0) {
            return true;
        }
    }
    return false;
}

QVariantList BezierAutomationLane::segments() const
{
    QVariantList result;
    for (const auto& s : m_curve.segments) {
        result.push_back(QVariantMap{{"startTime", s.startPoint.timeOffset}, {"startValue", s.startPoint.value},
            {"endTime", s.endPoint.timeOffset}, {"endValue", s.endPoint.value}, {"curvature", s.curvature},
            {"eased", s.eased}});
    }
    return result;
}

void BezierAutomationLane::setSegments(const QVariantList& data)
{
    // While dragging, the owner may push the curve straight back (live gain
    // edits reload the clip model on every move) -- keep the gesture's own
    // curve; the release commits and the next update applies normally.
    if (m_dragging) { return; }
    // GlideMix: never keep showing the previous curve when new data comes in
    // -- that left stray points on screen until a restart. Values a rounding
    // error outside 0..1 are clamped and joins re-attached; anything still
    // unusable clears the lane.
    clipautomation::Curve curve;
    for (const auto& entry : data) {
        const auto s = entry.toMap();
        if (!s.contains("startTime") || !s.contains("endTime") || !s.contains("startValue")
            || !s.contains("endValue") || !s.contains("curvature")) { curve = {}; break; }
        clipautomation::Segment segment{{s["startTime"].toDouble(), std::clamp(s["startValue"].toDouble(), 0.0, 1.0)},
            {s["endTime"].toDouble(), std::clamp(s["endValue"].toDouble(), 0.0, 1.0)},
            std::clamp(s["curvature"].toFloat(), -1.0f, 1.0f), s.value("eased", false).toBool()};
        if (!curve.segments.empty()) {
            segment.startPoint = curve.segments.back().endPoint;
        }
        curve.segments.push_back(segment);
    }
    if (!curve.Valid()) { curve = {}; }
    m_hoverNode = -1;
    // GlideMix: the same points coming back (the edit just committed): a
    // selected point stays selected, so a new point stays visible.
    if (curve.segments.size() != m_curve.segments.size()) {
        m_selectedNode = -1;
        m_picked.clear();
        m_lastClickedNode = -1;
    }
    m_hoverPoint = false;
    m_curve = std::move(curve);
    emit segmentsChanged();
    update();
}

void BezierAutomationLane::setViewStart(double v)
{
    if (!std::isfinite(v) || v == m_start) { return; }
    m_start = v; emit viewChanged(); update();
}
void BezierAutomationLane::setViewEnd(double v)
{
    if (!std::isfinite(v) || v == m_end) { return; }
    m_end = v; emit viewChanged(); update();
}
namespace { constexpr double kTopInset = 10.0; }

QPointF BezierAutomationLane::map(clipautomation::Point p) const
{
    return {(p.timeOffset - m_start) * width() / (m_end - m_start),
        kTopInset + (1.0 - p.value) * drawableHeight()};
}
QPointF BezierAutomationLane::nodePosition(int node) const
{
    const int last = static_cast<int>(m_curve.segments.size());
    const auto point = node == last ? m_curve.segments.back().endPoint : m_curve.segments[node].startPoint;
    QPointF position = map(point);
    // GlideMix: the first and last points sit on the clip's edges, half
    // outside the lane. Their dot is drawn just inside the edge, on the line,
    // so it is whole; hitNode() also grabs them from the very edge.
    constexpr double edgeDot = 6.0;
    if (width() > 4 * edgeDot) {
        if (node == 0 && position.x() < edgeDot) {
            position.setX(edgeDot);
        } else if (node == last && position.x() > width() - edgeDot) {
            position.setX(width() - edgeDot);
        }
    }
    return position;
}

double BezierAutomationLane::timeAt(double x) const
{
    return m_start + x * (m_end - m_start) / std::max(1.0, width());
}

double BezierAutomationLane::drawableHeight() const
{
    // A ten-pixel inset makes the end values readable and leaves room for
    // the node radius. On a very short clip, keep a drawable range.
    // GlideMix: the bottom inset keeps 0 above the phrase ribbon, where it can be grabbed.
    const double bottom = height() > 4 * (kTopInset + m_bottomInset) ? m_bottomInset : kTopInset;
    return std::max(1.0, height() - kTopInset - bottom);
}

double BezierAutomationLane::valueAt(double y) const
{
    return std::clamp(1.0 - (y - kTopInset) / drawableHeight(), 0.0, 1.0);
}

void BezierAutomationLane::paint(QPainter* painter)
{
    if (m_end <= m_start || width() <= 0 || height() <= 0) { return; }
    painter->setClipRect(boundingRect());
    painter->setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    for (const auto& s : m_curve.segments) {
        if (s.endPoint.timeOffset < m_start || s.startPoint.timeOffset > m_end) { continue; }
        path.moveTo(map(s.startPoint));
        if (m_stepped) {
            const QPointF from = map(s.startPoint), to = map(s.endPoint);
            path.lineTo(to.x(), from.y());   // hold
            path.lineTo(to);                 // jump at the next point
        } else if (s.eased) {
            const auto [c1, c2] = s.CubicControlPoints();
            path.cubicTo(map(c1), map(c2), map(s.endPoint));
        } else {
            path.quadTo(map(s.ControlPoint()), map(s.endPoint));
        }
    }

    // Stock Audacity's envelope look, copied from the PolylinePlot this lane
    // replaced (muse polylineplot.cpp paint/paintPoint + ClipItem.qml's point
    // styles on master): a plain 2px line, no dark halo, solid 5px points;
    // the hovered point is a 1px white centre, 2px black ring and 3px outline
    // in the line colour; the hover ghost is a plain 4px dot.     // 2026-09-13: the dark halo/outlines made the line "more yellow", not crisp.
    if (m_glow && !m_curve.segments.empty()) {
        // The pill's glow: one outline along the line, closed down to the base
        // value, filled strong at the line and almost clear at the base.
        QPainterPath fill;
        bool started = false;
        double far = 0.0;
        const double baseY = map({ m_start, m_glowBase }).y();
        for (const auto& s : m_curve.segments) {
            if (s.endPoint.timeOffset < m_start || s.startPoint.timeOffset > m_end) { continue; }
            const QPointF from = map(s.startPoint), to = map(s.endPoint);
            if (!started) {
                fill.moveTo(from.x(), baseY);
                fill.lineTo(from);
                started = true;
            } else {
                fill.lineTo(from);
            }
            if (m_stepped) {
                fill.lineTo(to.x(), from.y());
                fill.lineTo(to);
            } else if (s.eased) {
                const auto [c1, c2] = s.CubicControlPoints();
                fill.cubicTo(map(c1), map(c2), to);
            } else {
                fill.quadTo(map(s.ControlPoint()), to);
            }
            far = std::max({ far, std::abs(from.y() - baseY), std::abs(to.y() - baseY) });
        }
        if (started) {
            fill.lineTo(fill.currentPosition().x(), baseY);
            fill.closeSubpath();
            const double farY = fill.boundingRect().top() < baseY - 0.5 ? fill.boundingRect().top() : fill.boundingRect().bottom();
            QLinearGradient g(0, farY, 0, baseY);
            QColor strong = m_color, faint = m_color;
            strong.setAlphaF(m_dashed ? 0.35 : 0.5);
            faint.setAlphaF(0.04);
            g.setColorAt(0, strong);
            g.setColorAt(1, faint);
            painter->setPen(Qt::NoPen);
            painter->setBrush(g);
            painter->drawPath(fill);
        }
        Q_UNUSED(far)
    }

    painter->setPen(QPen(m_color, 2, m_dashed ? Qt::DashLine : Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(path);
    if (m_dashed) {
        return;   // no points until the envelope exists
    }
    auto paintNode = [&](QPointF centre, bool hovered) {
        painter->setPen(Qt::NoPen);
        if (hovered) {
            painter->setBrush(m_color);
            painter->drawEllipse(centre, 7.5, 7.5);          // 1 + 2 + 3
            painter->setBrush(QColor(0, 0, 0));
            painter->drawEllipse(centre, 3.0, 3.0);          // 1 + 2
            painter->setBrush(QColor(255, 255, 255));
            painter->drawEllipse(centre, 1.0, 1.0);
        } else {
            painter->setBrush(m_color);
            painter->drawEllipse(centre, 6.0, 6.0);   // GlideMix: easier to see and hit
        }
    };
    const int nodeCount = m_curve.segments.empty() ? 0 : static_cast<int>(m_curve.segments.size()) + 1;
    for (int i = 0; i < nodeCount; ++i) {
        const bool active = i == m_hoverNode || (m_dragging && i == m_node) || i == m_selectedNode || m_picked.contains(i);
        if (active || nodeShown(i)) {
            paintNode(nodePosition(i), active);
        }
    }
    if (m_hoverPoint && !m_dragging) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(m_color);
        painter->drawEllipse(m_hover, 4.0, 4.0);
    }
}

int BezierAutomationLane::hitSegment(QPointF p) const
{
    if (m_end <= m_start || height() <= 0) { return -1; }
    const double time = timeAt(p.x());
    for (size_t i = 0; i < m_curve.segments.size(); ++i) {
        const auto& s = m_curve.segments[i];
        if (time < s.startPoint.timeOffset || time > s.endPoint.timeOffset) { continue; }
        if (std::abs(map({time, s.Evaluate(time)}).y() - p.y()) <= 10) { return static_cast<int>(i); }
    }
    return -1;
}

double BezierAutomationLane::nodeValue(int node) const
{
    const int last = static_cast<int>(m_curve.segments.size());
    return node == last ? m_curve.segments.back().endPoint.value : m_curve.segments[node].startPoint.value;
}

bool BezierAutomationLane::nodeShown(int node) const
{
    // GlideMix (Chris): a point in the middle of a flat stretch, or a clip
    // edge where the line is already flat, adds a dot that tells nothing --
    // transitions leave two or three of them side by side. Only the corners
    // are drawn and grabbed; a click on the flat line adds a point as usual.
    // While editing (a drag) every point still behaves as before.
    const int last = static_cast<int>(m_curve.segments.size());
    auto same = [](double a, double b) { return std::abs(a - b) < 1e-4; };
    const double v = nodeValue(node);
    const bool flatBefore = node > 0 && same(nodeValue(node - 1), v);
    const bool flatAfter = node < last && same(nodeValue(node + 1), v);
    if (node == 0) { return !flatAfter; }
    if (node == last) { return !flatBefore; }
    return !(flatBefore && flatAfter);
}

int BezierAutomationLane::hitNode(QPointF p) const
{
    if (m_curve.segments.empty()) { return -1; }
    // The painted radius stays small and crisp, while the interaction target
    // is deliberately more forgiving on Retina displays.
    // GlideMix: the nearest point wins, so two close points never swap, and
    // the clip's first and last points also answer from the very edge.
    constexpr double hitRadius = 14.0;
    const int last = static_cast<int>(m_curve.segments.size());
    int best = -1;
    double bestDistance = hitRadius;
    for (int i = 0; i <= last; ++i) {
        if (!nodeShown(i) && !(m_dragging && i == m_node) && i != m_selectedNode) {
            continue;
        }
        const QPointF dot = nodePosition(i);
        double distance = QLineF(dot, p).length();
        if (i == 0 || i == last) {
            const double edge = i == 0 ? 0.0 : width();
            const double lo = std::min(edge, dot.x()), hi = std::max(edge, dot.x());
            if (p.x() >= lo - 4.0 && p.x() <= hi + 4.0 && std::abs(p.y() - dot.y()) <= hitRadius) {
                distance = std::min(distance, std::abs(p.y() - dot.y()));
            }
        }
        if (distance <= bestDistance) { best = i; bestDistance = distance; }
    }
    return best;
}

void BezierAutomationLane::hoverMoveEvent(QHoverEvent* e)
{
    const int node = hitNode(e->position());
    const int segment = hitSegment(e->position());
    m_hoverNode = node;
    m_hoverPoint = node < 0 && segment >= 0 && !e->modifiers().testFlag(Qt::AltModifier);
    if (m_hoverPoint) {
        const double time = timeAt(e->position().x());
        m_hover = map({time, m_curve.segments[segment].Evaluate(time)});
    }
    update();
    if (node < 0 && segment >= 0 && e->modifiers().testFlag(Qt::AltModifier)) {
        QPixmap pixmap(32, 32);
        pixmap.fill(Qt::transparent);
        QPainter p(&pixmap);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(m_color, 2));
        QPainterPath arc;
        arc.moveTo(3, 24); arc.quadTo(16, 0, 29, 24);
        p.drawPath(arc); p.drawLine(16, 8, 16, 22);
        p.end();
        setCursor(QCursor(pixmap, 16, 16));
    } else if (node >= 0) { setCursor(Qt::SizeAllCursor); }
    else { setCursor(Qt::ArrowCursor); }
}

void BezierAutomationLane::hoverLeaveEvent(QHoverEvent* e)
{
    m_hoverPoint = false;
    m_hoverNode = -1;
    unsetCursor();
    update();
    QQuickPaintedItem::hoverLeaveEvent(e);
}

void BezierAutomationLane::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::RightButton) {
        // Right-click on a point: its menu. Anywhere else the clip's menu.
        // The first point has one too (its line's shape), without Delete.
        const int node = hitNode(e->position());
        if (node < 0) {
            e->ignore();
            return;
        }
        m_selectedNode = node;
        update();
        emit pointMenuRequested(node, e->position().x(), e->position().y());
        e->accept();
        return;
    }
    m_node = hitNode(e->position());
    m_segment = hitSegment(e->position());
    m_selectedNode = -1;
    // GlideMix: Shift-click points to pick several; dragging one of them moves
    // them all together. A plain click elsewhere lets go of them.
    if (m_node >= 0 && e->modifiers().testFlag(Qt::ShiftModifier)) {
        if (m_picked.isEmpty() && m_lastClickedNode >= 0 && m_lastClickedNode != m_node
            && m_lastClickedNode < nodeCount()) {
            m_picked.insert(m_lastClickedNode);
        }
        if (m_picked.contains(m_node)) {
            m_picked.remove(m_node);
        } else {
            m_picked.insert(m_node);
        }
        m_lastClickedNode = m_node;
        update();
        forceActiveFocus();
        e->accept();
        return;
    }
    m_groupDrag = m_node >= 0 && m_picked.size() >= 2 && m_picked.contains(m_node);
    if (!m_groupDrag && !m_picked.isEmpty()) {
        m_picked.clear();
        update();
    }
    m_lastClickedNode = m_node;
    if (m_node >= 0) {
        // A shared node belongs to the segment on its right, except for the
        // final endpoint. Both adjacent endpoints are still updated below.
        m_segment = std::min(m_node, static_cast<int>(m_curve.segments.size()) - 1);
    }
    if (m_segment < 0) { e->ignore(); return; }
    m_before = m_curve;
    m_press = e->position();
    const double t = timeAt(m_press.x());
    m_shelf = m_node < 0 && m_selectionEnd > m_selectionStart
        && t >= m_selectionStart && t <= m_selectionEnd;
    if (m_shelf) { m_node = -1; }
    // Logic-style interaction: the ordinary arrow clicks a segment to add a
    // point; holding Option changes to the curve tool. Existing points remain
    // directly draggable without a modifier.
    m_bending = m_node < 0 && !m_shelf && e->modifiers().testFlag(Qt::AltModifier);
    m_inserting = m_node < 0 && !m_shelf && !m_bending;
    m_dragging = true;
    forceActiveFocus();
    e->accept();
}

void BezierAutomationLane::mouseMoveEvent(QMouseEvent* e)
{
    if (!m_dragging) { e->ignore(); return; }
    m_curve = m_before; // gesture is relative to mouse-down, never cumulative
    if (m_inserting) {
        // GlideMix: press on the line and drag = add a point there and drag it
        // (one move, like the transition lines). A plain click still adds one.
        if (QLineF(m_press, e->position()).length() < 4.0) { e->accept(); return; }
        const auto index = hitSegment(m_press);
        const double t = timeAt(m_press.x());
        if (index < 0) { e->accept(); return; }
        const auto segment = m_before.segments[index];
        if (!(t > segment.startPoint.timeOffset && t < segment.endPoint.timeOffset)) { e->accept(); return; }
        m_before.segments[index] = segment.Slice(segment.startPoint.timeOffset, t);
        m_before.segments.insert(m_before.segments.begin() + index + 1, segment.Slice(t, segment.endPoint.timeOffset));
        m_curve = m_before;
        m_inserting = false;
        m_node = index + 1;
        m_segment = index + 1;
        m_selectedNode = m_node;
    }
    const double delta = (m_press.y() - e->position().y()) / drawableHeight();
    auto& s = m_curve.segments[m_segment];
    if (m_bending) {
        // Curvature isn't a point value -- nothing for a value readout to show.
        s.curvature = static_cast<float>(std::clamp(s.curvature + delta * 4, -1.0, 1.0));
        s.eased = false;   // bending replaces an S-curve, as on the tempo line
        clearActivePoint();
    } else if (m_shelf) {
        const double from = std::max(m_selectionStart, m_curve.segments.front().startPoint.timeOffset);
        const double to = std::min(m_selectionEnd, m_curve.segments.back().endPoint.timeOffset);
        if (from < to) {
            const double value = std::clamp(m_before.Evaluate(timeAt(m_press.x())) + delta, 0.0, 1.0);
            m_curve.BoxStep(from, to, value);
            setActivePoint(e->position(), value);
        }
    } else if (m_node >= 0 && m_groupDrag) {
        movePicked(timeAt(e->position().x()) - timeAt(m_press.x()), delta);
        setActivePoint(nodePosition(m_node), nodeValue(m_node));
    } else if (m_node >= 0) {
        const auto n = static_cast<int>(m_curve.segments.size());
        const double lo = m_node ? m_curve.segments[m_node - 1].startPoint.timeOffset : m_curve.segments.front().startPoint.timeOffset;
        const double hi = m_node < n ? m_curve.segments[m_node].endPoint.timeOffset : m_curve.segments.back().endPoint.timeOffset;
        const double t = m_node == 0 ? lo : (m_node == n ? hi : std::clamp(timeAt(e->position().x()), lo, hi));
        const clipautomation::Point point{t, valueAt(e->position().y())};
        if (m_node) { m_curve.segments[m_node - 1].endPoint = point; }
        if (m_node < n) { m_curve.segments[m_node].startPoint = point; }
        // GlideMix: a flat line from this point to the clip's edge stays flat,
        // so a point added by hand doesn't spring back to 100% at the end.
        auto same = [](double a, double b) { return std::abs(a - b) < 1e-4; };
        if (m_node == n - 1 && m_node > 0 && same(m_before.segments[m_node].startPoint.value, m_before.segments[m_node].endPoint.value)) {
            m_curve.segments[m_node].endPoint.value = point.value;
        }
        setActivePoint(nodePosition(m_node), point.value);
    } else {
        // Move the segment's endpoints together, keeping adjacent edges joined.
        const double shift = std::clamp(delta, -std::min(s.startPoint.value, s.endPoint.value),
            1.0 - std::max(s.startPoint.value, s.endPoint.value));
        s.startPoint.value += shift; s.endPoint.value += shift;
        if (m_segment) { m_curve.segments[m_segment - 1].endPoint.value = s.startPoint.value; }
        if (m_segment + 1 < static_cast<int>(m_curve.segments.size())) {
            m_curve.segments[m_segment + 1].startPoint.value = s.endPoint.value;
        }
        setActivePoint(e->position(), s.startPoint.value);
    }
    emit segmentsChanged(); update();
    emit editChanged(segments());
    e->accept();
}

void BezierAutomationLane::movePicked(double dt, double dv)
{
    const int n = static_cast<int>(m_before.segments.size());
    if (n == 0) { return; }
    auto timeOf = [&](int i) { return i == n ? m_before.segments.back().endPoint.timeOffset : m_before.segments[i].startPoint.timeOffset; };
    auto valueOf = [&](int i) { return i == n ? m_before.segments.back().endPoint.value : m_before.segments[i].startPoint.value; };
    // Keep every value inside 0..1 and every point between the points that
    // stay put (the clip's first and last points never move sideways).
    double lowV = 1.0, highV = 0.0;
    double dtMin = -1e18, dtMax = 1e18;
    constexpr double gap = 1e-4;
    for (int i : m_picked) {
        if (i < 0 || i > n) { continue; }
        lowV = std::min(lowV, valueOf(i));
        highV = std::max(highV, valueOf(i));
        if (i == 0 || i == n) { continue; }
        int left = i - 1;
        while (left > 0 && m_picked.contains(left)) { --left; }
        int right = i + 1;
        while (right < n && m_picked.contains(right)) { ++right; }
        dtMin = std::max(dtMin, timeOf(left) + gap * (i - left) - timeOf(i));
        dtMax = std::min(dtMax, timeOf(right) - gap * (right - i) - timeOf(i));
    }
    dv = std::clamp(dv, -lowV, 1.0 - highV);
    dt = dtMin <= dtMax ? std::clamp(dt, dtMin, dtMax) : 0.0;
    for (int i : m_picked) {
        if (i < 0 || i > n) { continue; }
        const double t = (i == 0 || i == n) ? timeOf(i) : timeOf(i) + dt;
        const clipautomation::Point point{t, valueOf(i) + dv};
        if (i > 0) { m_curve.segments[i - 1].endPoint = point; }
        if (i < n) { m_curve.segments[i].startPoint = point; }
    }
}

void BezierAutomationLane::mouseReleaseEvent(QMouseEvent* e)
{
    if (!m_dragging) { e->ignore(); return; }
    m_dragging = false;
    m_groupDrag = false;
    if (m_inserting) {
        m_inserting = false;
        const auto index = hitSegment(m_press);
        if (index >= 0) {
            const auto segment = m_curve.segments[index];
            const double t = timeAt(m_press.x());
            if (t > segment.startPoint.timeOffset && t < segment.endPoint.timeOffset) {
                m_curve.segments[index] = segment.Slice(segment.startPoint.timeOffset, t);
                m_curve.segments.insert(m_curve.segments.begin() + index + 1,
                    segment.Slice(t, segment.endPoint.timeOffset));
                m_selectedNode = index + 1;   // GlideMix: the new point shows until you click elsewhere
                emit segmentsChanged(); update();
            }
        }
    }
    clearActivePoint();
    emit editCommitted(segments());
    e->accept();
}
void BezierAutomationLane::mouseDoubleClickEvent(QMouseEvent* e)
{
    // Point insertion is intentionally a single click. Accept the synthesized
    // double-click event so it cannot leak through and move the audio clip.
    e->accept();
}
void BezierAutomationLane::cancel()
{
    if (!m_dragging) { return; }
    m_dragging = false; m_inserting = false; m_groupDrag = false; m_curve = m_before;
    clearActivePoint();
    emit segmentsChanged(); emit editCancelled(); update();
}
void BezierAutomationLane::mouseUngrabEvent() { cancel(); }

void BezierAutomationLane::setActivePoint(QPointF position, double value)
{
    m_hasActivePoint = true;
    m_activePoint = position;
    m_activePointValue = value;
    emit activePointChanged();
}

void BezierAutomationLane::clearActivePoint()
{
    if (!m_hasActivePoint) { return; }
    m_hasActivePoint = false;
    emit activePointChanged();
}
void BezierAutomationLane::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Escape && m_dragging) { cancel(); e->accept(); }
    else if (e->key() == Qt::Key_Escape && !m_picked.isEmpty()) { m_picked.clear(); update(); e->accept(); }
    else { QQuickPaintedItem::keyPressEvent(e); }
}

void BezierAutomationLane::focusOutEvent(QFocusEvent* e)
{
    if (m_selectedNode >= 0) {
        m_selectedNode = -1;
        update();
    }
    QQuickPaintedItem::focusOutEvent(e);
}

void BezierAutomationLane::clearPointHighlight()
{
    if (m_selectedNode >= 0) {
        m_selectedNode = -1;
        update();
    }
}

bool BezierAutomationLane::isSCurve(int node) const
{
    return node >= 0 && node < static_cast<int>(m_curve.segments.size())
           && m_curve.segments[static_cast<size_t>(node)].eased;
}

bool BezierAutomationLane::setSCurve(int node, bool sCurve)
{
    if (m_dragging || node < 0 || node >= static_cast<int>(m_curve.segments.size())) {
        return false;
    }
    auto& segment = m_curve.segments[static_cast<size_t>(node)];
    segment.eased = sCurve;
    segment.curvature = 0;
    m_selectedNode = -1;
    emit segmentsChanged();
    update();
    emit editCommitted(segments());
    return true;
}

double BezierAutomationLane::pointValue(int node) const
{
    if (node < 0 || node > static_cast<int>(m_curve.segments.size()) || m_curve.segments.empty()) {
        return -1.0;
    }
    return nodeValue(node);
}

bool BezierAutomationLane::setPointValue(int node, double value)
{
    const int last = static_cast<int>(m_curve.segments.size());
    if (m_dragging || node < 0 || node > last || last == 0) {
        return false;
    }
    value = std::clamp(value, 0.0, 1.0);
    if (node < last) {
        m_curve.segments[static_cast<size_t>(node)].startPoint.value = value;
    }
    if (node > 0) {
        m_curve.segments[static_cast<size_t>(node - 1)].endPoint.value = value;
    }
    m_selectedNode = -1;
    clearActivePoint();
    emit segmentsChanged();
    update();
    emit editCommitted(segments());
    return true;
}

bool BezierAutomationLane::deletePoint(int node)
{
    if (m_dragging || node <= 0 || node >= static_cast<int>(m_curve.segments.size())) {
        return false;
    }
    auto& left = m_curve.segments[static_cast<size_t>(node - 1)];
    const auto& right = m_curve.segments[static_cast<size_t>(node)];
    left.endPoint = right.endPoint;
    m_curve.segments.erase(m_curve.segments.begin() + node);
    m_selectedNode = -1;
    m_hoverNode = -1;
    clearActivePoint();
    emit segmentsChanged();
    update();
    emit editCommitted(segments());
    return true;
}
