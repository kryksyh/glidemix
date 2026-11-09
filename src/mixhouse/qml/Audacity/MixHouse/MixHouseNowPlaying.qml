/*
 * GlideMix: the right half of the capsule display at the top of the project
 * window -- key, energy and place in the mix of the song under the playhead,
 * the processor meter and the play light. The clocks (time, BPM, signature)
 * sit to its left in the same capsule (ProjectPage.qml).
 */
import QtQuick

import Muse.Ui
import Muse.UiComponents

import Audacity.MixHouse

Row {
    id: root

    spacing: 0
    height: 32

    MixHouseNowPlayingModel {
        id: np
    }

    // The capsule's colour: the clock field is dark in every theme, so the
    // text follows the capsule's brightness, not the theme.
    property color background: ui.theme.backgroundQuarternaryColor
    readonly property bool darkBackground: 0.299 * background.r + 0.587 * background.g + 0.114 * background.b < 0.5
    readonly property color ink: darkBackground ? "#ECECEF" : "#1A1C1F"
    readonly property color labelColor: Qt.rgba(ink.r, ink.g, ink.b, 0.5)
    readonly property color lineColor: Qt.rgba(ink.r, ink.g, ink.b, 0.15)

    component Divider: Rectangle {
        width: 1
        height: 20
        anchors.verticalCenter: parent ? parent.verticalCenter : undefined
        color: root.lineColor
    }

    component Segment: Item {
        id: seg
        property string label: ""
        default property alias content: valueBox.data
        width: Math.max(valueBox.childrenRect.width, labelText.implicitWidth) + 24
        height: root.height

        Item {
            id: valueBox
            anchors.horizontalCenter: parent.horizontalCenter
            y: 2
            width: childrenRect.width
            height: 17
        }
        StyledTextLabel {
            id: labelText
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 1
            text: seg.label
            font.pixelSize: 9
            font.bold: true
            font.letterSpacing: 0.8
            color: root.labelColor
        }
    }

    Divider {}

    Segment {
        label: qsTrc("mixhouse", "KEY")
        StyledTextLabel {
            text: np.keyText !== "" ? np.keyText : "—"
            font: ui.theme.bodyBoldFont
            color: !np.keyColor.valid || np.keyText === "" ? root.ink
                   : (root.darkBackground ? np.keyColor : Qt.darker(np.keyColor, 1.8))
        }
    }

    Divider {}

    Segment {
        label: np.energy > 0 ? qsTrc("mixhouse", "ENERGY %1").arg(np.energy) : qsTrc("mixhouse", "ENERGY")
        Item {
            width: 48
            height: 15
            Repeater {
                model: 10
                Rectangle {
                    x: index * 5
                    width: 3
                    height: 6 + index
                    y: parent.height - height
                    radius: 1
                    color: index < np.energy ? (index < 7 ? "#5FD068" : "#E7C94A") : root.lineColor
                }
            }
        }
    }

    Divider {}

    Segment {
        label: qsTrc("mixhouse", "SONG")
        StyledTextLabel {
            text: np.songCount > 0 ? (np.songIndex > 0 ? np.songIndex : "–") + " / " + np.songCount : "—"
            font: ui.theme.bodyBoldFont
            color: root.ink
        }
    }

    Divider {}

    Segment {
        label: qsTrc("mixhouse", "CPU %1%").arg(np.cpuPercent)
        Rectangle {
            y: 6
            width: 46
            height: 4
            radius: 2
            color: root.lineColor
            Rectangle {
                width: Math.max(2, parent.width * Math.min(np.cpuPercent, 100) / 100)
                height: parent.height
                radius: 2
                color: np.cpuPercent >= 80 ? "#E2574C" : np.cpuPercent >= 50 ? "#E7C94A" : ui.theme.accentColor
            }
        }
    }

    Divider {}

    // What GlideMix itself reads from disk; the bar is full at 20 MB/s.
    Segment {
        label: np.diskMBps >= 10 ? qsTrc("mixhouse", "DISK %1 MB/s").arg(Math.round(np.diskMBps))
                                 : qsTrc("mixhouse", "DISK %1 MB/s").arg(np.diskMBps.toFixed(1))
        Rectangle {
            y: 6
            width: 46
            height: 4
            radius: 2
            color: root.lineColor
            Rectangle {
                width: Math.max(2, parent.width * Math.min(np.diskMBps / 20, 1))
                height: parent.height
                radius: 2
                color: np.diskMBps >= 16 ? "#E7C94A" : ui.theme.accentColor
            }
        }
    }

    Item {
        width: 22
        height: root.height
        Rectangle {
            anchors.centerIn: parent
            width: 8
            height: 8
            radius: 4
            color: np.playing ? "#53D07C" : root.lineColor
        }
    }
}
