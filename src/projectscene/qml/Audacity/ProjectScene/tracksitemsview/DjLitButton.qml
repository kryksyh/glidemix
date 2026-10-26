/*
 * GlideMix
 *
 * A DJ-mixer push button in GlideMix's own look: dark rubber that glows in its
 * colour when lit (orange CUE, blue FX, the big blue ON/OFF). Disabled means
 * GlideMix has nothing behind it yet: it stays dim and does not react.
 */
import QtQuick

Item {
    id: root

    property string text: ""
    property bool lit: false
    property color litColor: "#3fa9ff"
    property int fontSize: 9
    property bool round: false

    signal clicked()

    implicitWidth: Math.max(40, label.implicitWidth + 14)
    implicitHeight: 22
    opacity: enabled ? 1 : 0.35

    // the glow around a lit button
    Rectangle {
        anchors.fill: cap
        anchors.margins: -3
        radius: cap.radius + 3
        color: "transparent"
        border.color: root.litColor
        border.width: 3
        opacity: root.lit ? 0.35 : 0
    }

    Rectangle {
        id: cap
        anchors.fill: parent
        radius: root.round ? height / 2 : 3
        border.color: root.lit ? Qt.lighter(root.litColor, 1.3) : "#3a3e43"
        gradient: Gradient {
            GradientStop { position: 0; color: root.lit ? Qt.lighter(root.litColor, 1.25) : "#33363a" }
            GradientStop { position: 1; color: root.lit ? Qt.darker(root.litColor, 1.35) : "#1c1e21" }
        }
    }

    Text {
        id: label
        anchors.centerIn: parent
        text: root.text
        font.pixelSize: root.fontSize
        font.bold: true
        font.letterSpacing: root.fontSize <= 7 ? 0.2 : 0.8
        color: root.lit ? "#0d0e10" : "#c9c6be"
    }

    MouseArea {
        anchors.fill: parent
        enabled: root.enabled
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
