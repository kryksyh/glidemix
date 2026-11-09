/*
 * GlideMix
 *
 * A DJ-mixer fader in GlideMix's modern look: a thin slot with the value in
 * the channel's colour and a glass capsule cap (see-through, light rim). Vertical for the channel faders, horizontal for
 * the crossfader. Drag or click to move, double-click to reset, arrow keys step.
 */
import QtQuick

Item {
    id: root

    property real value: 0.5
    property real defaultValue: 0.5
    property bool horizontal: false
    property string label: ""
    // The value's colour in the slot.
    property color accent: "#926BFF"

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
    onAccentChanged: canvas.requestPaint()

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
            // GlideMix modern look: a thin rounded slot, the value in the
            // accent colour, and a light rounded cap with a dark centre line.
            var g = getContext("2d")
            var w = width, h = height, horiz = root.horizontal, v = root.shown
            g.reset()
            var L = horiz ? w : h, T = horiz ? h : w
            var pad = root.pad, travel = L - pad * 2
            var mid = T / 2

            // ticks, faint
            g.fillStyle = "rgba(255,255,255,0.18)"
            for (var i = 0; i <= 10; i++) {
                var p = pad + travel * i / 10, len = i % 5 === 0 ? 6 : 3
                if (horiz) {
                    g.fillRect(p - 0.5, mid - 10 - len, 1, len)
                    g.fillRect(p - 0.5, mid + 10, 1, len)
                } else {
                    g.fillRect(mid - 10 - len, p - 0.5, len, 1)
                    g.fillRect(mid + 10, p - 0.5, len, 1)
                }
            }

            // slot
            var sw = 4
            g.fillStyle = "#2a2c31"
            if (horiz) {
                roundRect(g, pad - 2, mid - sw / 2, travel + 4, sw, sw / 2)
            } else {
                roundRect(g, mid - sw / 2, pad - 2, sw, travel + 4, sw / 2)
            }
            g.fill()

            // the value: from the bottom on a channel fader, from the centre
            // on the crossfader
            var pos = pad + travel * (horiz ? v : 1 - v)
            var from = horiz ? pad + travel * 0.5 : pad + travel
            var lo = Math.min(from, pos), len2 = Math.abs(pos - from)
            if (len2 > 0.5) {
                g.fillStyle = root.accent
                if (horiz) {
                    roundRect(g, lo, mid - sw / 2, len2, sw, sw / 2)
                } else {
                    roundRect(g, mid - sw / 2, lo, sw, len2, sw / 2)
                }
                g.fill()
            }

            // cap: a glass capsule -- see-through, a light rim, a sheen
            var capL = horiz ? 16 : 14, capT = horiz ? 30 : 34
            var cx = horiz ? pos - capL / 2 : mid - capT / 2, cy = horiz ? mid - capT / 2 : pos - capL / 2
            var cw = horiz ? capL : capT, ch = horiz ? capT : capL
            var cr = Math.min(cw, ch) / 2
            g.save()
            g.shadowColor = "rgba(0,0,0,0.35)"
            g.shadowBlur = 6
            g.shadowOffsetY = 2
            g.fillStyle = "rgba(255,255,255,0.16)"
            roundRect(g, cx, cy, cw, ch, cr)
            g.fill()
            g.restore()
            var gr = horiz ? g.createLinearGradient(cx, cy, cx + cw, cy) : g.createLinearGradient(cx, cy, cx, cy + ch)
            gr.addColorStop(0, "rgba(255,255,255,0.40)")
            gr.addColorStop(0.5, "rgba(255,255,255,0.04)")
            gr.addColorStop(1, "rgba(255,255,255,0)")
            g.fillStyle = gr
            roundRect(g, cx, cy, cw, ch, cr)
            g.fill()
            g.lineWidth = 1
            g.strokeStyle = "rgba(255,255,255,0.6)"
            roundRect(g, cx + 0.5, cy + 0.5, cw - 1, ch - 1, cr - 0.5)
            g.stroke()

            // centre line
            g.fillStyle = "rgba(255,255,255,0.9)"
            if (horiz) {
                g.fillRect(cx + cw / 2 - 0.75, cy + 6, 1.5, ch - 12)
            } else {
                g.fillRect(cx + 6, cy + ch / 2 - 0.75, cw - 12, 1.5)
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
        text: (root.held || faderMouse.containsMouse) && root.enabled ? Math.round(root.shown * 100) + "%" : root.label
        visible: root.label !== ""
        color: (root.held || faderMouse.containsMouse) && root.enabled ? "#ECECEF" : "#9a9890"
        font.pixelSize: 10
        font.letterSpacing: 1.4
        font.bold: true
    }

    MouseArea {
        id: faderMouse
        anchors.fill: canvas
        hoverEnabled: true
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
