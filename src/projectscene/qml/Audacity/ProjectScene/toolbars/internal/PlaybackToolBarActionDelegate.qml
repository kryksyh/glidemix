/*
* Audacity: A Digital Audio Editor
*/
import QtQuick 2.15
import QtQuick.Layouts 1.12

import Muse.Ui 1.0
import Muse.UiComponents

Item {
    id: root

    height: parent ? parent.height : implicitHeight
    width: parent ? parent.width : implicitWidth

    property var item

    property var navigationPanel: null
    property int navigationRow: 0

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 6
        anchors.rightMargin: 6

        spacing: 16

        StyledIconLabel {
            Layout.alignment: Qt.AlignLeft

            width: 36
            height: width

            iconCode: Boolean(root.item) && Boolean(root.item.checked) ? IconCode.EYE_OPEN : IconCode.EYE_CLOSED
        }

        Item {
            Layout.alignment: Qt.AlignLeft

            width: 36
            height: width

            // GlideMix's own toolbar items use Tabler Icons (MIT)
            readonly property string svgName: {
                switch (Boolean(root.item) ? root.item.id() : "") {
                case "metronome": return "metronome"
                case "mixhouse-sync": return "arrows-exchange"
                case "mixhouse-tempo-lane": return "gauge"
                case "mixhouse-keylock": return "lock"
                case "mixtape-track-height": return "arrows-vertical"
                case "mixtape-zoom-slider": return "arrows-horizontal"
                }
                return ""
            }

            StyledIconLabel {
                anchors.fill: parent
                visible: parent.svgName === ""

                iconCode: Boolean(root.item) ? root.item.icon : IconCode.NONE
                color: Boolean(root.item) ? root.item.iconColor : ui.theme.fontPrimaryColor
            }

            Image {
                anchors.centerIn: parent
                visible: parent.svgName !== ""
                width: 18
                height: 18
                sourceSize.width: 36
                sourceSize.height: 36
                source: parent.svgName === "" ? ""
                        : "qrc:/resources/toolbaricons/" + parent.svgName + "-" + (ui.theme.isDark ? "dark" : "light") + ".svg"
            }
        }

        StyledTextLabel {
            id: titleLabel

            Layout.fillWidth: true

            horizontalAlignment: Qt.AlignLeft
            text: Boolean(root.item) ? root.item.title : ""
        }
    }
}
