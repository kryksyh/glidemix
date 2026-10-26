/*
 * GlideMix
 *
 * A DJ-mixer fader in GlideMix's own look: a dark slot with ticks and a ridged
 * cap with a white centre line. Vertical for the channel faders, horizontal for
 * the crossfader. Drag or click to move, double-click to reset, arrow keys step.
 */
import QtQuick

Item {
    id: root

    property real value: 0.5
    property real defaultValue: 0.5
    property bool horizontal: false
    property string label: ""

    signal moved(real v)
    signal grabbed(bool down)

    readonly property int pad: 14

    implicitWidth: horizontal ? 190 : 44
    implicitHeight: (horizontal ? 44 : 150) + (label !== "" ? 14 : 0)

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
    onWidthChanged: canvas.requestPaint()
    onHeightChanged: canvas.requestPaint()

    Canvas {
        id: canvas
        width: root.width
        height: root.height - (root.label !== "" ? 14 : 0)
        antialiasing: true

        function roundRect(g, x, y, w, h, r) {
            g.beginPath()
            g.moveTo(x + r, y)
            g.arcTo(x + w, y, x + w, y + h, r)
            g.arcTo(x + w, y + h, x, y + h, r)
            g.arcTo(x, y + h, x, y, r)
            g.arcTo(x, y, x + w, y, r)
            g.closePath()
        }

        onPaint: {
            var g = getContext("2d")
            var w = width, h = height, horiz = root.horizontal, v = root.shown
            g.reset()
            var L = horiz ? w : h, T = horiz ? h : w
            var pad = root.pad, travel = L - pad * 2
            var mid = T / 2

            // ticks
            g.fillStyle = "#6c6f73"
            for (var i = 0; i <= 10; i++) {
                var p = pad + travel * i / 10, len = i % 5 === 0 ? 7 : 4
                if (horiz) {
                    g.fillRect(p - 0.5, mid - 11 - len, 1, len)
                    g.fillRect(p - 0.5, mid + 11, 1, len)
                } else {
                    g.fillRect(mid - 11 - len, p - 0.5, len, 1)
                    g.fillRect(mid + 11, p - 0.5, len, 1)
                }
            }

            // slot plate
            var sx = horiz ? pad - 6 : mid - 4, sy = horiz ? mid - 4 : pad - 6
            var sw = horiz ? travel + 12 : 8, sh = horiz ? 8 : travel + 12
            var gr = horiz ? g.createLinearGradient(0, sy, 0, sy + sh) : g.createLinearGradient(sx, 0, sx + sw, 0)
            gr.addColorStop(0, "#050506")
            gr.addColorStop(0.5, "#141518")
            gr.addColorStop(1, "#2a2c30")
            g.fillStyle = gr
            roundRect(g, sx, sy, sw, sh, 3)
            g.fill()

            // cap
            var pos = pad + travel * (horiz ? v : 1 - v)
            var capL = horiz ? 16 : 22, capT = horiz ? 30 : 34
            var cx = horiz ? pos - capL / 2 : mid - capT / 2, cy = horiz ? mid - capT / 2 : pos - capL / 2
            var cw = horiz ? capL : capT, ch = horiz ? capT : capL
            g.save()
            g.shadowColor = "rgba(0,0,0,0.8)"
            g.shadowBlur = 8
            g.shadowOffsetY = 4
            g.fillStyle = "#101113"
            roundRect(g, cx, cy, cw, ch, 3)
            g.fill()
            g.restore()
            gr = g.createLinearGradient(cx, cy, cx, cy + ch)
            gr.addColorStop(0, "#4a4d52")
            gr.addColorStop(0.18, "#26282b")
            gr.addColorStop(0.82, "#17181a")
            gr.addColorStop(1, "#0a0a0b")
            g.fillStyle = gr
            roundRect(g, cx, cy, cw, ch, 3)
            g.fill()

            // ridges across the grip
            g.strokeStyle = "rgba(255,255,255,0.09)"
            g.lineWidth = 1
            for (var k = 1; k < 6; k++) {
                var o = (horiz ? cw : ch) * k / 6
                g.beginPath()
                if (horiz) {
                    g.moveTo(cx + o, cy + 3); g.lineTo(cx + o, cy + ch - 3)
                } else {
                    g.moveTo(cx + 3, cy + o); g.lineTo(cx + cw - 3, cy + o)
                }
                g.stroke()
            }

            // centre line
            g.fillStyle = "#f4f2ec"
            if (horiz) {
                g.fillRect(cx + cw / 2 - 1, cy + 2, 2, ch - 4)
            } else {
                g.fillRect(cx + 2, cy + ch / 2 - 1, cw - 4, 2)
            }
        }
    }

    Rectangle {
        anchors.fill: canvas
        radius: 4
        color: "transparent"
        border.color: "#f0a43a"
        border.width: 1
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
        cursorShape: Qt.PointingHandCursor
        preventStealing: true
        function valueAt(mouse) {
            return root.horizontal ? (mouse.x - root.pad) / (width - root.pad * 2)
                                   : 1 - (mouse.y - root.pad) / (height - root.pad * 2)
        }
        onPressed: function(mouse) {
            root.forceActiveFocus()
            root.begin()
            root.set(valueAt(mouse))
        }
        onPositionChanged: function(mouse) {
            if (pressed) {
                root.set(valueAt(mouse))
            }
        }
        onReleased: root.end()
        onCanceled: root.end()
        onDoubleClicked: {
            root.begin()
            root.set(root.defaultValue)
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
