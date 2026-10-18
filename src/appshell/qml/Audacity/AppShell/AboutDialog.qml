/*
* Audacity: A Digital Audio Editor
*/
import QtQuick 2.15
import QtQuick.Layouts 1.15

import Muse.Ui 1.0
import Muse.UiComponents

import Audacity.AppShell

StyledDialogView {
    id: root

    title: qsTrc("appshell/about", "About GlideMix")

    contentHeight: 640
    contentWidth: 760

    AboutModel {
        id: aboutModel
    }

    QtObject {
        id: prv

        readonly property int tabSpacing: 16
        readonly property int tabButtonSpacing: 32
        readonly property int btnMargins: 8
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: prv.tabSpacing

        StyledTabBar {
            id: tabBar

            Layout.alignment: Qt.AlignHCenter
            spacing: prv.tabButtonSpacing

            StyledTabButton {
                text: aboutModel.appName()
            }

            StyledTabButton {
                text: qsTrc("appshell/about", "Legal")
            }
        }

        StackLayout {
            id: stackLayout

            Layout.fillWidth: true
            Layout.fillHeight: true

            currentIndex: tabBar.currentIndex

            AboutDialogAppTab {
                model: aboutModel
            }

            AboutDialogLegalTab {
                model: aboutModel
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0

            SeparatorLine {
                Layout.fillWidth: true
            }

            FlatButton {
                Layout.alignment: Qt.AlignRight
                Layout.margins: prv.btnMargins

                //: Label of a dialog button
                text: qsTrc("global", "Close")

                onClicked: {
                    root.hide()
                }
            }
        }
    }
}
