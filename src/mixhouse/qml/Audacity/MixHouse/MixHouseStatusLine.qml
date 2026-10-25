/*
 * GlideMix: the status line in the bottom bar, like Ableton's and Logic's.
 * Left: what the app is doing (scan, analysis, ...) or the last thing it did.
 * Middle: what the control under the mouse does. Right: a new version.
 */
import QtQuick
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents

import Audacity.MixHouse

Item {
    id: root

    MixHouseStatusModel {
        id: status
    }

    // What the mouse is over, from every control's tooltip text.
    property string hoverTitle: ""
    property string hoverDescription: ""
    property string hoverShortcut: ""

    Connections {
        target: ui.tooltip
        function onHoverChanged(title, description, shortcut) {
            if (title === "") {
                hoverClear.restart()   // moving between buttons should not flicker
                return
            }
            hoverClear.stop()
            root.hoverTitle = title
            root.hoverDescription = description
            root.hoverShortcut = shortcut
        }
    }
    Timer {
        id: hoverClear
        interval: 250
        onTriggered: {
            root.hoverTitle = ""
            root.hoverDescription = ""
            root.hoverShortcut = ""
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 12

        // Activity
        Rectangle {
            Layout.alignment: Qt.AlignVCenter
            width: 8
            height: 8
            radius: 4
            color: status.busy ? ui.theme.accentColor : ui.theme.strokeColor
            visible: status.text !== ""

            SequentialAnimation on opacity {
                running: status.busy
                loops: Animation.Infinite
                alwaysRunToEnd: true
                NumberAnimation { from: 1.0; to: 0.3; duration: 600 }
                NumberAnimation { from: 0.3; to: 1.0; duration: 600 }
            }
        }

        StyledTextLabel {
            id: activityLabel
            Layout.alignment: Qt.AlignVCenter
            Layout.maximumWidth: root.width * (root.hoverTitle !== "" ? 0.38 : 0.62)
            text: status.text
            horizontalAlignment: Text.AlignLeft
            elide: Text.ElideRight
            opacity: status.busy ? 1.0 : 0.75
        }

        Rectangle {
            Layout.alignment: Qt.AlignVCenter
            visible: status.busy && status.percent >= 0
            width: 90
            height: 4
            radius: 2
            color: ui.theme.strokeColor

            Rectangle {
                width: parent.width * Math.max(0, Math.min(100, status.percent)) / 100
                height: parent.height
                radius: parent.radius
                color: ui.theme.accentColor
            }
        }

        FlatButton {
            Layout.alignment: Qt.AlignVCenter
            visible: status.canStop
            text: qsTrc("mixhouse", "Stop")
            height: 22
            onClicked: status.stop()
        }

        // Info about what the mouse is over
        Rectangle {
            Layout.alignment: Qt.AlignVCenter
            visible: root.hoverTitle !== "" && status.text !== ""
            width: 1
            height: 16
            color: ui.theme.strokeColor
        }

        StyledTextLabel {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            horizontalAlignment: Text.AlignLeft
            elide: Text.ElideRight
            textFormat: Text.StyledText
            text: {
                if (root.hoverTitle === "") {
                    return ""
                }
                let s = "<b>" + root.hoverTitle + "</b>"
                if (root.hoverDescription !== "" && root.hoverDescription !== root.hoverTitle) {
                    s += "  " + root.hoverDescription
                }
                if (root.hoverShortcut !== "") {
                    s += "  (" + root.hoverShortcut + ")"
                }
                return s
            }
        }

        // A new version
        Rectangle {
            id: updatePill
            Layout.alignment: Qt.AlignVCenter
            visible: status.updateVersion !== ""
            implicitWidth: updateLabel.implicitWidth + 24
            height: 24
            radius: 12
            color: updateArea.containsMouse ? Qt.lighter(ui.theme.accentColor, 1.15) : ui.theme.accentColor

            StyledTextLabel {
                id: updateLabel
                anchors.centerIn: parent
                color: "#ffffff"
                font.bold: true
                text: qsTrc("mixhouse", "GlideMix %1 is out · Click to update").arg(status.updateVersion)
            }

            MouseArea {
                id: updateArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: status.openUpdate()
            }
        }
    }
}
