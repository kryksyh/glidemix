/*
* Audacity: A Digital Audio Editor
*/
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import Muse.Ui 1.0
import Muse.UiComponents

import Audacity.Export 1.0
import "internal"

StyledDialogView {
    id: root

    title: qsTrc("export", "Metadata editor")

    contentWidth: 880
    contentHeight: 625

    modal: true

    property NavigationPanel navigation: NavigationPanel {
        name: root.title
        enabled: root.enabled && root.visible
        direction: NavigationPanel.Horizontal
        section: root.navigationSection
    }
    property int navigationOrder: 0

    MetadataModel {
        id: metadataModel
    }

    Component.onCompleted: {
        metadataModel.load()
    }

    ColumnLayout {
        id: mainColumn

        spacing: 0

        MetadataControlPanel {
            id: topButtonsBar

            Layout.fillHeight: true
            Layout.fillWidth: true

            Layout.leftMargin: 16
            Layout.rightMargin: 12
            Layout.topMargin: 10
            Layout.bottomMargin: 10

            metadataModel: metadataModel
            tagView: metadataView

            navigationPanel: root.navigation
            navigationOrder: root.navigationOrder
        }

        ValueList {
            id: metadataView

            width: root.contentWidth
            height: 444

            keyRoleName: "tag"
            keyTitle: qsTrc("metadata", "tag")
            valueRoleName: "value"
            valueTitle: qsTrc("metadata", "value")
            valueTypeRole: "roleType"
            readOnly: false
            isReadOnlyFunction: function (rowIndex) {
                return metadataModel.isStandardTag(rowIndex)
            }

            drawZebra: false
            headerColor: ui.theme.backgroundSecondaryColor
            keyColumnWidth: 100
            isKeyEditable: true
            headerCapitalization: Font.Capitalize
            sorterEnabled: false

            navigationSection: navigation.section
            navigationOrderStart: topButtonsBar.navigationOrder + 1

            model: metadataModel

            onKeyEdited: function (row, newKey) {
                metadataModel.renameTag(row, newKey)
            }

            onValueEdited: function (row, newVal) {
                metadataModel.setTagValue(row, newVal)
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 12
            Layout.topMargin: 8

            spacing: 12

            Rectangle {
                Layout.preferredWidth: 72
                Layout.preferredHeight: 72

                color: ui.theme.backgroundSecondaryColor
                border.color: ui.theme.strokeColor
                border.width: 1

                Image {
                    anchors.fill: parent
                    anchors.margins: 1

                    source: metadataModel.artworkUrl
                    sourceSize.width: 144
                    sourceSize.height: 144
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    clip: true
                }
            }

            StyledTextLabel {
                //: Title of the picture embedded in exported audio files
                text: qsTrc("metadata", "Artwork")
            }

            FlatButton {
                text: qsTrc("metadata", "Choose photo…")

                navigation.panel: root.navigation
                navigation.column: root.navigationOrder + 20

                onClicked: metadataModel.chooseArtwork()
            }

            FlatButton {
                text: qsTrc("metadata", "Remove")
                enabled: metadataModel.artworkUrl.toString() !== ""

                navigation.panel: root.navigation
                navigation.column: root.navigationOrder + 21

                onClicked: metadataModel.removeArtwork()
            }

            Item {
                Layout.fillWidth: true
            }
        }

        ButtonBox {
            id: buttonBox

            Layout.fillWidth: true

            padding: 8

            navigationPanel.section: root.navigation.section
            navigationPanel.order: metadataView.navigationOrderStart + 2

            FlatButton {
                id: cancelBtn

                minWidth: 80

                text: qsTrc("global", "Close")
                buttonRole: ButtonBoxModel.RejectRole
                buttonId: ButtonBoxModel.Cancel

                onClicked: root.reject()
            }

            FlatButton {
                id: okBtn

                minWidth: 80

                text: qsTrc("global", "Apply")
                buttonRole: ButtonBoxModel.AcceptRole
                buttonId: ButtonBoxModel.Apply
                accentButton: true

                onClicked: {
                    metadataModel.apply()
                    root.accept()
                }
            }
        }
    }
}
