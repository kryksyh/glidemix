/*
 * GlideMix
 *
 * A DJ-mixer knob in GlideMix's modern look: a flat disc with a white
 * pointer, and a thin arc in the channel's colour for the value. `big` starts
 * the arc at the left end (TRIM, LEVEL); otherwise it grows from the top
 * (EQ, COLOR). While turned or hovered the label shows the value.
 *
 * Drag up or down to turn, double-click to reset, arrow keys step.
 */
import QtQuick

Item {
    id: root

    property real value: 0.5
    property real defaultValue: 0.5
    property color accent: "#f0a43a"
    property bool big: false
    property string label: ""
    property int size: 40

    // The user turned it (not a value bound from outside).
    signal moved(real v)
    // A drag or key press began / ended, for REC and one-undo-step-per-turn.
    signal grabbed(bool down)

    readonly property real minAngle: -135
    readonly property real maxAngle: 135

    implicitWidth: size
    implicitHeight: size + (label !== "" ? 14 : 0)

    activeFocusOnTab: true

    // While held (and a moment after, until the line is written back) the
    // control shows where the hand put it, not the bound value.
    property bool held: false
    property real local: 0
    readonly property real shown: held ? local : value

    // The value under the label while turning: centred knobs (EQ at 0.5,
    // COLOR) from -100 to +100, the rest 0-100 %. Set to say it in units.
    property var formatValue: null
    function valueText() {
        if (formatValue) {
            return formatValue(shown)
        }
        if (defaultValue === 0.5) {
            var c = Math.round((shown - 0.5) * 200)
            return (c > 0 ? "+" : "") + c
        }
        return Math.round(shown * 100) + "%"
    }

    function begin() {
        releaseTimer.stop()
        if (!held) {
            local = value
            held = true
        }
        grabbed(true)
    }
    function end() {
        grabbed(false)
        releaseTimer.restart()
    }
    function set(v) {
        v = Math.max(0, Math.min(1, v))
        if (v !== local) {
            local = v
            moved(v)
        }
    }

    Timer {
        id: releaseTimer
        interval: 600
        onTriggered: root.held = false
    }

    onShownChanged: canvas.requestPaint()
    onAccentChanged: canvas.requestPaint()

    Canvas {
        id: canvas
        width: root.size
        height: root.size
        antialiasing: true

        onPaint: {
            // GlideMix modern look: a flat disc, a thin coloured arc for the
            // value and a white pointer (was a knurled rubber knob).
            var g = getContext("2d")
            var s = root.size, r = s / 2, cx = r, cy = r
            g.reset()

            var ringW = Math.max(2, s * 0.06)
            var ringR = r - ringW / 2 - 0.5
            var a = root.minAngle + root.shown * (root.maxAngle - root.minAngle)
            var zero = root.big ? root.minAngle : 0
            var rad = function(d) { return (d - 90) * Math.PI / 180 }

            // the track the arc runs in
            g.lineCap = "round"
            g.lineWidth = ringW
            g.strokeStyle = "#2a2c31"
            g.beginPath()
            g.arc(cx, cy, ringR, rad(root.minAngle), rad(root.maxAngle), false)
            g.stroke()
            // the value
            if (Math.abs(a - zero) > 0.5 && root.accent.a > 0) {
                g.strokeStyle = root.accent
                g.beginPath()
                g.arc(cx, cy, ringR, rad(Math.min(zero, a)), rad(Math.max(zero, a)), false)
                g.stroke()
            }

            // the disc, with a soft shadow and a faint lighter top edge
            var kr = r - ringW - Math.max(2, s * 0.07)
            g.save()
            g.shadowColor = "rgba(0,0,0,0.45)"
            g.shadowBlur = s * 0.08
            g.shadowOffsetY = s * 0.03
            g.fillStyle = "#232528"
            g.beginPath()
            g.arc(cx, cy, kr, 0, 2 * Math.PI, false)
            g.fill()
            g.restore()
            var grad = g.createLinearGradient(0, cy - kr, 0, cy + kr)
            grad.addColorStop(0, "rgba(255,255,255,0.10)")
            grad.addColorStop(0.5, "rgba(255,255,255,0)")
            g.fillStyle = grad
            g.beginPath()
            g.arc(cx, cy, kr, 0, 2 * Math.PI, false)
            g.fill()
            g.lineWidth = 1
            g.strokeStyle = "rgba(255,255,255,0.08)"
            g.beginPath()
            g.arc(cx, cy, kr - 0.5, 0, 2 * Math.PI, false)
            g.stroke()

            // pointer
            g.save()
            g.translate(cx, cy)
            g.rotate(a * Math.PI / 180)
            g.strokeStyle = "#ECECEF"
            g.lineWidth = Math.max(1.6, s * 0.05)
            g.lineCap = "round"
            g.beginPath()
            g.moveTo(0, -kr * (root.big ? 0.3 : 0.2))
            g.lineTo(0, -kr * 0.85)
            g.stroke()
            g.restore()
        }
    }

    Rectangle {
        anchors.fill: canvas
        radius: width / 2
        color: "transparent"
        border.color: "#f0a43a"
        border.width: 2
        visible: root.activeFocus
    }

    Text {
        anchors.top: canvas.bottom
        anchors.topMargin: 1
        anchors.horizontalCenter: canvas.horizontalCenter
        text: (root.held || knobMouse.containsMouse) && root.enabled ? root.valueText() : root.label
        visible: root.label !== ""
        color: (root.held || knobMouse.containsMouse) && root.enabled ? "#ECECEF" : "#9a9890"
        font.pixelSize: 10
        font.letterSpacing: 1.4
        font.bold: true
    }

    MouseArea {
        id: knobMouse
        anchors.fill: canvas
        hoverEnabled: true
        cursorShape: Qt.SizeVerCursor
        preventStealing: true
        property real y0: 0
        property real v0: 0
        onPressed: function(mouse) {
            root.forceActiveFocus()
            y0 = mouse.y
            v0 = root.shown
            root.begin()
        }
        onPositionChanged: function(mouse) {
            if (pressed) {
                root.set(v0 + (y0 - mouse.y) / 160)
            }
        }
        onReleased: root.end()
        onCanceled: root.end()
        onDoubleClicked: {
            root.begin()
            root.set(root.defaultValue)
            root.end()
        }
        onWheel: function(wheel) {
            root.begin()
            root.set(root.shown + (wheel.angleDelta.y > 0 ? 0.02 : -0.02))
            root.end()
        }
    }

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Up || event.key === Qt.Key_Right) {
            root.begin(); root.set(root.shown + 0.05); root.end(); event.accepted = true
        } else if (event.key === Qt.Key_Down || event.key === Qt.Key_Left) {
            root.begin(); root.set(root.shown - 0.05); root.end(); event.accepted = true
        }
    }
}
