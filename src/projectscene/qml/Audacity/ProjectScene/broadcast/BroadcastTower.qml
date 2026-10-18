/*
 * GlideMix
 */
import QtQuick

import Muse.Ui 1.0

//! The broadcast tower: a mast with two cross-braces and two arcs of signal
//! either side. Drawn rather than loaded as an image so it takes the theme's
//! colour and stays crisp at any size.
Canvas {
    id: root

    //! 0 off, 1 connecting, 2 live, 3 failed -- the arcs light up on air.
    property int broadcastState: 0
    property color baseColor: ui.theme.fontPrimaryColor
    property color liveColor: "#E5484D"

    implicitWidth: 18
    implicitHeight: 18

    onBroadcastStateChanged: requestPaint()
    onBaseColorChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()

    onPaint: {
        var ctx = getContext("2d")
        ctx.reset()

        var w = width
        var h = height
        var cx = w / 2
        // The mast stands in the lower half; the arcs wrap its top.
        var feed = h * 0.42
        ctx.lineCap = "round"
        ctx.lineJoin = "round"
        ctx.lineWidth = Math.max(1.2, w / 14)

        var live = root.broadcastState === 2
        ctx.strokeStyle = live ? root.liveColor : root.baseColor
        ctx.globalAlpha = live ? 1.0 : (root.broadcastState === 0 ? 0.55 : 0.85)
        for (var i = 0; i < 2; ++i) {
            var r = w * (0.22 + i * 0.16)
            ctx.beginPath()
            ctx.arc(cx, feed, r, Math.PI * 1.15, Math.PI * 1.85)
            ctx.stroke()
            ctx.beginPath()
            ctx.arc(cx, feed, r, Math.PI * 0.15, Math.PI * 0.85)
            ctx.stroke()
        }

        ctx.globalAlpha = 1.0
        ctx.strokeStyle = root.baseColor
        var footY = h * 0.94
        var spread = w * 0.17
        ctx.beginPath()
        ctx.moveTo(cx - spread, footY)
        ctx.lineTo(cx, feed)
        ctx.lineTo(cx + spread, footY)
        ctx.stroke()

        for (var b = 0; b < 2; ++b) {
            var t = 0.45 + b * 0.32
            var y = feed + (footY - feed) * t
            var half = spread * t
            ctx.beginPath()
            ctx.moveTo(cx - half, y)
            ctx.lineTo(cx + half, y)
            ctx.stroke()
        }

        ctx.fillStyle = live ? root.liveColor : root.baseColor
        ctx.beginPath()
        ctx.arc(cx, feed, Math.max(1.4, w * 0.09), 0, 2 * Math.PI)
        ctx.fill()
    }
}
