/*
 * GlideMix
 *
 * The stream meter. It reads like every other meter in the app -- the same
 * gradient, the same ruler, the same clip square and peak markers -- but its
 * scale runs past 0 dBFS, because the stream gain can be pushed above it and
 * the operator has to see by how much before the encoder starts clipping.
 */
import QtQuick

import Muse.Ui 1.0
import Muse.UiComponents

import Audacity.Playback 1.0

Item {
    id: root

    //! Peak of each channel, in dBFS.
    property real peakLeft: -120
    property real peakRight: -120
    //! Off air: the bars empty and the held peaks are forgotten.
    property bool active: false

    readonly property real minDb: -60
    readonly property real maxDb: 12

    implicitHeight: leftBar.height + spacing + rightBar.height + spacing + ruler.height

    readonly property int barHeight: 6
    readonly property int spacing: 2
    //! The clip square, set apart from the bar the way the app's meters do it.
    readonly property int clipWidth: 5
    readonly property int clipSpacing: 2

    function positionOf(db) {
        return Math.max(0, Math.min(1, (db - root.minDb) / (root.maxDb - root.minDb)))
    }

    onActiveChanged: {
        if (!root.active) {
            prv.heldLeft = -120
            prv.heldRight = -120
            prv.clipped = false
        }
    }

    QtObject {
        id: prv

        property real heldLeft: -120
        property real heldRight: -120
        property bool clipped: false
        property int holdTicks: 0
    }

    //! Peaks are held for a moment and then fall away, so a transient that is
    //! gone before the eye catches it still leaves a mark.
    Timer {
        running: root.active
        repeat: true
        interval: 80

        onTriggered: {
            if (root.peakLeft >= prv.heldLeft || root.peakRight >= prv.heldRight) {
                prv.holdTicks = 20
            } else if (prv.holdTicks > 0) {
                prv.holdTicks--
            }

            var decay = prv.holdTicks > 0 ? 0 : 1.2
            prv.heldLeft = Math.max(root.peakLeft, prv.heldLeft - decay)
            prv.heldRight = Math.max(root.peakRight, prv.heldRight - decay)

            if (root.peakLeft >= 0 || root.peakRight >= 0) {
                prv.clipped = true
            }
        }
    }

    // Half a label hangs over each end, as on the app's own ruler.
    readonly property real leftInset: firstLabelWidth.paintedWidth / 2
    readonly property real rightInset: lastLabelWidth.paintedWidth / 2
    readonly property real barWidth: width - leftInset - rightInset - clipWidth - clipSpacing

    Text { id: firstLabelWidth; text: "-60"; font.pixelSize: 10; visible: false }
    Text { id: lastLabelWidth; text: "+12"; font.pixelSize: 10; visible: false }

    Component {
        id: barComponent

        Item {
            id: bar

            property real peak: -120
            property real held: -120

            height: root.barHeight

            Rectangle {
                x: root.leftInset
                width: root.barWidth
                height: parent.height
                color: MeterStyle.meterBackgroundColor
                border.width: 1
                border.color: MeterStyle.meterStrokeColor

                // The lit part: a window onto a gradient that spans the whole
                // scale, so a given level always has the same colour.
                Item {
                    width: root.barWidth * root.positionOf(bar.peak)
                    height: parent.height
                    clip: true
                    visible: root.active && width > 0

                    Rectangle {
                        width: root.barWidth
                        height: parent.height

                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0.0; color: MeterStyle.gradientColorGreen }
                            GradientStop { position: root.positionOf(-18); color: MeterStyle.gradientColorGreen }
                            GradientStop { position: root.positionOf(-6); color: MeterStyle.gradientColorYellow }
                            GradientStop { position: root.positionOf(0); color: MeterStyle.gradientColorRed }
                            GradientStop { position: 1.0; color: MeterStyle.gradientColorRed }
                        }
                    }
                }

                // 0 dBFS: everything to the right of this is headroom the
                // encoder cannot actually carry.
                Rectangle {
                    x: root.barWidth * root.positionOf(0)
                    width: 1
                    height: parent.height
                    color: MeterStyle.meterStrokeColor
                    opacity: 0.9
                }

                Rectangle {
                    x: Math.min(root.barWidth - 1, root.barWidth * root.positionOf(bar.held))
                    width: 1
                    height: parent.height
                    color: MeterStyle.maxPeakMarkerColor
                    visible: root.active && bar.held > root.minDb
                }
            }

            // Clip square.
            Rectangle {
                x: root.leftInset + root.barWidth + root.clipSpacing
                width: root.clipWidth
                height: parent.height
                color: prv.clipped ? MeterStyle.clippedColor : MeterStyle.meterBackgroundColor
            }
        }
    }

    Loader {
        id: leftBar
        y: 0
        width: root.width
        sourceComponent: barComponent
        onLoaded: {
            item.peak = Qt.binding(function() { return root.peakLeft })
            item.held = Qt.binding(function() { return prv.heldLeft })
        }
    }

    Loader {
        id: rightBar
        y: leftBar.height + root.spacing
        width: root.width
        sourceComponent: barComponent
        onLoaded: {
            item.peak = Qt.binding(function() { return root.peakRight })
            item.held = Qt.binding(function() { return prv.heldRight })
        }
    }

    Item {
        id: ruler

        y: rightBar.y + rightBar.height + root.spacing
        width: root.width
        height: 12

        Repeater {
            model: [-60, -48, -36, -24, -12, 0, 12]

            delegate: Item {
                required property int modelData

                x: root.leftInset + root.barWidth * root.positionOf(modelData)
                width: 1
                height: parent.height

                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: modelData < 0 ? 2 : 0

                    //! The tick is the minus sign, which is how the app's own
                    //! ruler draws it - hence the bare number beside it.
                    Rectangle {
                        width: 4
                        height: 1
                        color: ui.theme.fontPrimaryColor
                        anchors.verticalCenter: parent.verticalCenter
                        visible: modelData < 0
                    }

                    Text {
                        text: modelData > 0 ? "+" + modelData : String(Math.abs(modelData))
                        color: modelData > 0 ? MeterStyle.gradientColorRed : ui.theme.fontPrimaryColor
                        font.pixelSize: 10
                    }
                }
            }
        }
    }
}
