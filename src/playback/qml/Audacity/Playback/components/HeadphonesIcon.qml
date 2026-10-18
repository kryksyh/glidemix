/*
* GlideMix
*/
import QtQuick

import Muse.Ui

//! Headphones, drawn because the icon font has no such glyph: a headband
//! arc over two ear cups. Put it inside a FlatButton.
Canvas {
    id: root

    property Item button: null

    anchors.centerIn: parent
    width: 16
    height: 16

    property color strokeColor: button && button.contentItem && button.contentItem.color
                                ? button.contentItem.color : ui.theme.fontPrimaryColor
    onStrokeColorChanged: requestPaint()
    onVisibleChanged: if (visible) requestPaint()

    onPaint: {
        var ctx = getContext("2d")
        ctx.reset()
        ctx.strokeStyle = strokeColor
        ctx.fillStyle = strokeColor
        ctx.lineWidth = 1.5
        ctx.lineCap = "round"

        ctx.beginPath()
        ctx.arc(width / 2, height * 0.58, width * 0.38, Math.PI, 2 * Math.PI)
        ctx.stroke()

        var cupW = width * 0.18
        var cupH = height * 0.30
        var cupY = height * 0.55
        ctx.beginPath()
        ctx.roundedRect(width / 2 - width * 0.38 - cupW / 2, cupY, cupW, cupH, 2, 2)
        ctx.fill()
        ctx.beginPath()
        ctx.roundedRect(width / 2 + width * 0.38 - cupW / 2, cupY, cupW, cupH, 2, 2)
        ctx.fill()
    }
}
