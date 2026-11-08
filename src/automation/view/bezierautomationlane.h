/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <QQuickPaintedItem>
#include <QColor>
#include <QVariantList>
#include <QSet>
#include "au3-components/BezierAutomation.h"

namespace au::automation {
// UI-only item. The owning clip model applies editCommitted as ONE undo step.
// Painting reads the GUI snapshot synchronized by QQuickPaintedItem; it never
// reads a WaveClip or plugin on the scene-graph/audio threads.
class BezierAutomationLane : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(QVariantList segments READ segments WRITE setSegments NOTIFY segmentsChanged)
    Q_PROPERTY(double viewStart READ viewStart WRITE setViewStart NOTIFY viewChanged)
    Q_PROPERTY(double viewEnd READ viewEnd WRITE setViewEnd NOTIFY viewChanged)
    Q_PROPERTY(double selectionStart MEMBER m_selectionStart)
    Q_PROPERTY(double selectionEnd MEMBER m_selectionEnd)
    Q_PROPERTY(QColor lineColor MEMBER m_color NOTIFY lineColorChanged)
    //! Ableton: a control that isn't automated yet shows a dashed line at its
    //! current value, without points; the first click on it starts the envelope.
    Q_PROPERTY(bool dashed MEMBER m_dashed NOTIFY lineColorChanged)
    //! Switches: each value holds until the next point, then jumps (Ableton's on/off steps).
    Q_PROPERTY(bool stepped MEMBER m_stepped NOTIFY lineColorChanged)
    //! GlideMix: the lowest value sits this far above the bottom, clear of the phrase ribbon.
    Q_PROPERTY(double bottomInset MEMBER m_bottomInset NOTIFY lineColorChanged)
    // Live drag feedback -- e.g. a floating dB readout while a point is being
    // dragged, the same job PolylinePlot's activePoint* properties did before
    // this component replaced it. Normalized 0..1, same as the values in
    // `segments`; a consumer that needs a real unit (dB, Hz, ...) converts it
    // itself, since only the consumer's own model knows that mapping.
    Q_PROPERTY(bool hasActivePoint READ hasActivePoint NOTIFY activePointChanged)
    Q_PROPERTY(double activePointX READ activePointX NOTIFY activePointChanged)
    Q_PROPERTY(double activePointY READ activePointY NOTIFY activePointChanged)
    Q_PROPERTY(double activePointValue READ activePointValue NOTIFY activePointChanged)
public:
    explicit BezierAutomationLane(QQuickItem* parent = nullptr);
    QVariantList segments() const;
    void setSegments(const QVariantList& data);
    double viewStart() const { return m_start; }
    double viewEnd() const { return m_end; }
    void setViewStart(double value);
    void setViewEnd(double value);
    bool hasActivePoint() const { return m_hasActivePoint; }
    double activePointX() const { return m_activePoint.x(); }
    double activePointY() const { return m_activePoint.y(); }
    double activePointValue() const { return m_activePointValue; }
    void paint(QPainter* painter) override;
    /// Removes the point, joining its two segments (one undoable edit). The
    /// clip's first and last points can't be removed.
    Q_INVOKABLE bool deletePoint(int node);
    /// GlideMix: a point's value (0..1, as in `segments`), and typing one in
    /// (one undoable edit). -1 for no such point.
    Q_INVOKABLE double pointValue(int node) const;
    Q_INVOKABLE bool setPointValue(int node, double value);
    /// GlideMix: the line from point `node` to the next -- S-curve or straight.
    Q_INVOKABLE bool isSCurve(int node) const;
    Q_INVOKABLE bool setSCurve(int node, bool sCurve);
    /// The point stays highlighted while its menu is open.
    Q_INVOKABLE void clearPointHighlight();
    /// GlideMix: what a click at x, y would grab: a point (its index), else -1.
    Q_INVOKABLE int nodeAt(double x, double y) const { return hitNode(QPointF(x, y)); }
    /// True when x, y is on a point or on the line.
    Q_INVOKABLE bool grabsAt(double x, double y) const { return hitNode(QPointF(x, y)) >= 0 || hitSegment(QPointF(x, y)) >= 0; }
    Q_INVOKABLE int nodeCount() const { return m_curve.segments.empty() ? 0 : static_cast<int>(m_curve.segments.size()) + 1; }
signals:
    void segmentsChanged();
    void viewChanged();
    void lineColorChanged();
    void editCommitted(const QVariantList& segments);
    /// Every mouse move of a drag, before editCommitted -- lets the owner apply
    /// the curve live (stock Audacity's waveform follows a dragged gain point).
    void editChanged(const QVariantList& segments);
    void editCancelled();
    void activePointChanged();
    /// GlideMix: right-click on a point -- the owner shows its menu
    /// ("Delete point") at x, y and calls deletePoint(node).
    void pointMenuRequested(int node, double x, double y);
protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseUngrabEvent() override;
    void hoverMoveEvent(QHoverEvent* event) override;
    void hoverLeaveEvent(QHoverEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
private:

    // Keep extrema inside the clip instead of letting a 0 or 1 value
    // disappear into its boundary or selection outline.
    double drawableHeight() const;
    double valueAt(double y) const;
    QPointF map(clipautomation::Point point) const;
    /// Where a point's dot is drawn and grabbed: map(), with the clip's first
    /// and last points moved a little inside the clip.
    QPointF nodePosition(int node) const;
    double timeAt(double x) const;
    int hitSegment(QPointF position) const;
    int hitNode(QPointF position) const;
    double nodeValue(int node) const;
    //! GlideMix: false for points that only repeat the flat line around them.
    bool nodeShown(int node) const;
    void cancel();
    //! GlideMix: the Shift-picked points, moved together from m_before.
    void movePicked(double dt, double dv);
    void setActivePoint(QPointF position, double value);
    void clearActivePoint();
    clipautomation::Curve m_curve;
    clipautomation::Curve m_before;
    double m_start{}, m_end{1};
    double m_selectionStart{}, m_selectionEnd{};
    QColor m_color{QStringLiteral("#F5BB42")};
    double m_bottomInset{10.0};
    bool m_dashed = false;
    bool m_stepped = false;
    QPointF m_press;
    QPointF m_hover;
    int m_segment{-1};
    int m_node{-1};
    bool m_bending{}, m_shelf{}, m_inserting{}, m_dragging{}, m_hoverPoint{};
    bool m_hasActivePoint{};
    int m_hoverNode{-1};
    int m_selectedNode{-1};   ///< the point whose menu is open
    QSet<int> m_picked;       ///< GlideMix: Shift-clicked points that move together
    int m_lastClickedNode{-1};
    bool m_groupDrag{};
    QPointF m_activePoint;
    double m_activePointValue{};
};
}
