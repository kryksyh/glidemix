/*
 * GlideMix
 *
 * A DJ-mixer knob in GlideMix's own look: a knurled rubber skirt and a white
 * pointer that turn, under a satin cap whose sheen and highlight stay put.
 * `big` adds the metal centre and starts the value ring at the left end (TRIM,
 * LEVEL); otherwise the ring grows from the top (EQ, COLOR).
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
            var g = getContext("2d")
            var s = root.size, r = s / 2, cx = r, cy = r
            g.reset()

            // value ring on the faceplate
            var ringR = r - 2
            g.lineWidth = 2
            g.lineCap = "round"
            g.strokeStyle = "#2e3135"
            g.beginPath()
            g.arc(cx, cy, ringR, (root.minAngle - 90) * Math.PI / 180, (root.maxAngle - 90) * Math.PI / 180, false)
            g.stroke()
            var a = root.minAngle + root.shown * (root.maxAngle - root.minAngle)
            var zero = root.big ? root.minAngle : 0
            if (Math.abs(a - zero) > 0.5) {
                g.strokeStyle = root.accent
                g.beginPath()
                g.arc(cx, cy, ringR, (Math.min(zero, a) - 90) * Math.PI / 180, (Math.max(zero, a) - 90) * Math.PI / 180, false)
                g.stroke()
            }

            // drop shadow
            var kr = r * 0.74
            g.save()
            g.shadowColor = "rgba(0,0,0,0.75)"
            g.shadowBlur = s * 0.12
            g.shadowOffsetY = s * 0.06
            g.fillStyle = "#0c0d0e"
            g.beginPath()
            g.arc(cx, cy, kr, 0, 2 * Math.PI, false)
            g.fill()
            g.restore()

            // knurled rubber skirt: the ridges turn, the light (upper left) does not
            var rot = a * Math.PI / 180
            var ridges = 36
            for (var i = 0; i < ridges; i++) {
                var t = rot + i * 2 * Math.PI / ridges
                var lit = 0.5 + 0.5 * Math.cos(t + 2.2)
                g.fillStyle = Qt.rgba((18 + lit * 34) / 255, (19 + lit * 35) / 255, (21 + lit * 36) / 255, 1)
                g.beginPath()
                g.moveTo(cx, cy)
                g.arc(cx, cy, kr, t, t + Math.PI / ridges, false)
                g.closePath()
                g.fill()
            }

            // cap: satin black with a fixed sheen
            var cr = kr * 0.78
            var grad = g.createLinearGradient(cx - cr, cy - cr, cx + cr, cy + cr)
            grad.addColorStop(0, "#3d4044")
            grad.addColorStop(0.45, "#1b1c1f")
            grad.addColorStop(1, "#08090a")
            g.fillStyle = grad
            g.beginPath()
            g.arc(cx, cy, cr, 0, 2 * Math.PI, false)
            g.fill()

            // bevel
            g.lineWidth = 1
            g.strokeStyle = "rgba(255,255,255,0.12)"
            g.beginPath()
            g.arc(cx, cy, cr - 0.5, Math.PI * 0.95, Math.PI * 1.7, false)
            g.stroke()
            g.strokeStyle = "rgba(0,0,0,0.6)"
            g.beginPath()
            g.arc(cx, cy, cr - 0.5, Math.PI * -0.05, Math.PI * 0.7, false)
            g.stroke()

            // metal centre disc on big knobs (brushed: alternating light/dark sweep)
            if (root.big) {
                var mr = cr * 0.42
                var cg = g.createConicalGradient(cx, cy, 0.6)
                cg.addColorStop(0, "#d9dcdf")
                cg.addColorStop(0.25, "#8d9196")
                cg.addColorStop(0.5, "#d9dcdf")
                cg.addColorStop(0.75, "#8d9196")
                cg.addColorStop(1, "#d9dcdf")
                g.fillStyle = cg
                g.beginPath()
                g.arc(cx, cy, mr, 0, 2 * Math.PI, false)
                g.fill()
            }

            // soft specular highlight, fixed
            grad = g.createRadialGradient(cx - cr * 0.45, cy - cr * 0.55, 0, cx - cr * 0.45, cy - cr * 0.55, cr * 0.9)
            grad.addColorStop(0, "rgba(255,255,255,0.16)")
            grad.addColorStop(1, "rgba(255,255,255,0)")
            g.fillStyle = grad
            g.beginPath()
            g.arc(cx, cy, cr, 0, 2 * Math.PI, false)
            g.fill()

            // pointer line
            g.save()
            g.translate(cx, cy)
            g.rotate(rot)
            g.strokeStyle = "#f4f2ec"
            g.lineWidth = Math.max(1.6, s * 0.045)
            g.lineCap = "round"
            g.beginPath()
            g.moveTo(0, -cr * (root.big ? 0.5 : 0.25))
            g.lineTo(0, -kr * 0.96)
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
        text: root.label
        visible: root.label !== ""
        color: "#9a9890"
        font.pixelSize: 10
        font.letterSpacing: 1.4
        font.bold: true
    }

    MouseArea {
        anchors.fill: canvas
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
