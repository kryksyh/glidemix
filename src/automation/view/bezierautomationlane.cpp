/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "bezierautomationlane.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QHoverEvent>
#include <QKeyEvent>
#include <QCursor>
#include <QPixmap>

using namespace au::automation;

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
    m_selectedNode = -1;
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
QPointF BezierAutomationLane::map(clipautomation::Point p) const
{
    return {(p.timeOffset - m_start) * width() / (m_end - m_start),
        10.0 + (1.0 - p.value) * drawableHeight()};
}
QPointF BezierAutomationLane::nodePosition(int node) const
{
    const int last = static_cast<int>(m_curve.segments.size());
    const auto point = node == last ? m_curve.segments.back().endPoint : m_curve.segments[node].startPoint;
    QPointF position = map(point);
    // GlideMix: the first and last points sit on the clip's edges,
    // under the trim handles (12 px into a selected clip) and half outside
    // the lane, so they could hardly be grabbed. Where the edge is on screen
    // their dot is drawn and hit a little inside the clip; they still set the
    // value at the very edge.
    constexpr double edgeInset = 16.0;
    if (width() > 4 * edgeInset) {
        if (node == 0 && std::abs(position.x()) < 1.0) {
            position.setX(edgeInset);
        } else if (node == last && std::abs(position.x() - width()) < 1.0) {
            position.setX(width() - edgeInset);
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
    return std::max(1.0, height() - 20.0);
}

double BezierAutomationLane::valueAt(double y) const
{
    return std::clamp(1.0 - (y - 10.0) / drawableHeight(), 0.0, 1.0);
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
            painter->drawEllipse(centre, 6.0, 6.0);          // 1 + 2 + 3
            painter->setBrush(QColor(0, 0, 0));
            painter->drawEllipse(centre, 3.0, 3.0);          // 1 + 2
            painter->setBrush(QColor(255, 255, 255));
            painter->drawEllipse(centre, 1.0, 1.0);
        } else {
            painter->setBrush(m_color);
            painter->drawEllipse(centre, 5.0, 5.0);
        }
    };
    const int nodeCount = m_curve.segments.empty() ? 0 : static_cast<int>(m_curve.segments.size()) + 1;
    for (int i = 0; i < nodeCount; ++i) {
        paintNode(nodePosition(i), i == m_hoverNode || (m_dragging && i == m_node) || i == m_selectedNode);
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
        if (std::abs(map({time, s.Evaluate(time)}).y() - p.y()) <= 8) { return static_cast<int>(i); }
    }
    return -1;
}

int BezierAutomationLane::hitNode(QPointF p) const
{
    if (m_curve.segments.empty()) { return -1; }
    // The painted radius stays small and crisp, while the interaction target
    // is deliberately more forgiving on Retina displays.
    constexpr double hitRadius = 11.0;
    for (size_t i = 0; i <= m_curve.segments.size(); ++i) {
        if (QLineF(nodePosition(static_cast<int>(i)), p).length() <= hitRadius) { return static_cast<int>(i); }
    }
    return -1;
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
        if (node < 0 || node >= static_cast<int>(m_curve.segments.size())) {
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
    if (m_inserting) { e->accept(); return; }
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
    } else if (m_node >= 0) {
        const auto n = static_cast<int>(m_curve.segments.size());
        const double lo = m_node ? m_curve.segments[m_node - 1].startPoint.timeOffset : m_curve.segments.front().startPoint.timeOffset;
        const double hi = m_node < n ? m_curve.segments[m_node].endPoint.timeOffset : m_curve.segments.back().endPoint.timeOffset;
        const double t = m_node == 0 ? lo : (m_node == n ? hi : std::clamp(timeAt(e->position().x()), lo, hi));
        const clipautomation::Point point{t, valueAt(e->position().y())};
        if (m_node) { m_curve.segments[m_node - 1].endPoint = point; }
        if (m_node < n) { m_curve.segments[m_node].startPoint = point; }
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

void BezierAutomationLane::mouseReleaseEvent(QMouseEvent* e)
{
    if (!m_dragging) { e->ignore(); return; }
    m_dragging = false;
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
    m_dragging = false; m_inserting = false; m_curve = m_before;
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
