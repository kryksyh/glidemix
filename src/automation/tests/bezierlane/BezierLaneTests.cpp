/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../../view/bezierautomationlane.h"
#include <QGuiApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QImage>
#include <QKeyEvent>
#include <cassert>
#include <iostream>

class Lane : public au::automation::BezierAutomationLane {
public:
    using BezierAutomationLane::mousePressEvent;
    using BezierAutomationLane::mouseMoveEvent;
    using BezierAutomationLane::mouseReleaseEvent;
    using BezierAutomationLane::mouseDoubleClickEvent;
    using BezierAutomationLane::keyPressEvent;
};
static QMouseEvent event(QEvent::Type type, QPointF p, Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    return QMouseEvent(type, p, p, Qt::LeftButton, Qt::LeftButton, mods);
}
int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    Lane lane;
    lane.setWidth(300); lane.setHeight(100); lane.setViewEnd(3);
    const QVariantList initial{QVariantMap{{"startTime", 0.0}, {"startValue", .2},
        {"endTime", 3.0}, {"endValue", .8}, {"curvature", 0.0}}};
    lane.setSegments(initial);
    int commits = 0;
    QObject::connect(&lane, &Lane::editCommitted, [&] { ++commits; });
    // Holding Option selects the curve tool and bends the segment.
    auto press = event(QEvent::MouseButtonPress, {150, 50}, Qt::AltModifier);
    lane.mousePressEvent(&press);
    auto move = event(QEvent::MouseMove, {150, 25}, Qt::AltModifier);
    lane.mouseMoveEvent(&move);
    assert(lane.segments().front().toMap()["curvature"].toDouble() == 1);
    auto release = event(QEvent::MouseButtonRelease, {150, 25}, Qt::AltModifier);
    lane.mouseReleaseEvent(&release);
    assert(commits == 1);

    // Endpoint hit-testing must not depend on also hitting the curve. This is
    // especially important for shared nodes and high-DPI displays. The first
    // point's dot sits 16 px inside the clip, clear of the trim handles.
    lane.setSegments(initial);
    auto nodePress = event(QEvent::MouseButtonPress, {16, 74}); lane.mousePressEvent(&nodePress);
    auto nodeMove = event(QEvent::MouseMove, {16, 50}); lane.mouseMoveEvent(&nodeMove);
    auto nodeRelease = event(QEvent::MouseButtonRelease, {16, 50}); lane.mouseReleaseEvent(&nodeRelease);
    assert(lane.segments().front().toMap()["startValue"].toDouble() > .45);
    assert(commits == 2);
    QImage image(300, 100, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    { QPainter p(&image); lane.paint(&p); }
    // Same quadratic used by the evaluator: .5 + .6 * .25 = .65. The lane
    // reserves a 10px vertical inset so extrema never merge into clip edges.
    assert(qAlpha(image.pixel(150, 39)) > 0);

    lane.setSegments(initial);
    lane.setProperty("selectionStart", 1.0); lane.setProperty("selectionEnd", 2.0);
    auto shelfPress = event(QEvent::MouseButtonPress, {150, 50}); lane.mousePressEvent(&shelfPress);
    auto shelfMove = event(QEvent::MouseMove, {150, 80}); lane.mouseMoveEvent(&shelfMove);
    const auto shelf = lane.segments();
    assert(shelf.size() == 5); // left ramp, drop, shelf, rise, right ramp
    assert(shelf[1].toMap()["startTime"] == shelf[1].toMap()["endTime"]);
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier); lane.keyPressEvent(&escape);
    assert(lane.segments() == initial);
    assert(commits == 2); // cancellation must not become an undo step

    lane.setProperty("selectionStart", 0.0); lane.setProperty("selectionEnd", 0.0);
    auto insertPress = event(QEvent::MouseButtonPress, {150, 50}); lane.mousePressEvent(&insertPress);
    auto insertRelease = event(QEvent::MouseButtonRelease, {150, 50}); lane.mouseReleaseEvent(&insertRelease);
    assert(lane.segments().size() == 2); assert(commits == 3);
    std::cout << "PASS: Option bend, independent node drag, normal-click insertion, painted curve matches evaluator, four-node shelf, Escape cancel\n";
}
