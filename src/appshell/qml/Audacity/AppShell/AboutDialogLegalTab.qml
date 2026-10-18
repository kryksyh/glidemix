/*
* Audacity: A Digital Audio Editor
*/
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import Muse.Ui 1.0
import Muse.UiComponents

import Audacity.AppShell

RowLayout {
    id: root

    required property AboutModel model

    spacing: prv.contentSpacing

    QtObject {
        id: prv

        readonly property int listWidth: 240
        readonly property int contentSpacing: 12
        readonly property int contentMargin: 16
        readonly property int contentTextMargin: 12
        readonly property int headerSpacing: 4

        readonly property var entries: root.model.legalList()
        readonly property var current: entries[listView.currentIndex] || entries[0]

        readonly property string currentText: {
            let files = prv.current.files
            let texts = []
            for (let i = 0; i < files.length; ++i) {
                texts.push(root.model.licenseText(files[i]))
            }
            return texts.join("\n\n────────────────────────────────────────\n\n")
        }
        readonly property bool currentIsRichText: prv.current.files.length === 1 && prv.current.files[0].endsWith(".html")
    }

    Rectangle {
        Layout.preferredWidth: prv.listWidth
        Layout.fillHeight: true
        Layout.leftMargin: prv.contentMargin

        color: ui.theme.backgroundSecondaryColor

        StyledListView {
            id: listView

            anchors.fill: parent

            model: prv.entries
            currentIndex: 0
            scrollBarPolicy: ScrollBar.AsNeeded

            delegate: ListItemBlank {
                id: item

                required property var modelData
                required property int index

                implicitHeight: 42
                isSelected: ListView.isCurrentItem

                navigation.panel: listView.navigation
                navigation.order: index
                navigation.accessible.name: modelData.name

                onClicked: {
                    listView.currentIndex = index
                }

                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10

                    spacing: 2

                    StyledTextLabel {
                        width: parent.width
                        horizontalAlignment: Text.AlignLeft
                        text: item.modelData.name
                        font: ui.theme.bodyBoldFont
                    }

                    StyledTextLabel {
                        width: parent.width
                        horizontalAlignment: Text.AlignLeft
                        text: item.modelData.license
                        color: ui.theme.fontSecondaryColor
                    }
                }
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.rightMargin: prv.contentMargin

        spacing: prv.contentSpacing

        Column {
            Layout.fillWidth: true
            spacing: prv.headerSpacing

            StyledTextLabel {
                width: parent.width
                horizontalAlignment: Text.AlignLeft
                text: prv.current.name
                font: ui.theme.tabBoldFont
            }

            StyledTextLabel {
                width: parent.width
                horizontalAlignment: Text.AlignLeft
                wrapMode: Text.WordWrap
                text: prv.current.copyright
            }

            StyledTextLabel {
                width: parent.width
                horizontalAlignment: Text.AlignLeft
                textFormat: Text.RichText
                text: {
                    let url = prv.current.url
                    let license = qsTrc("appshell/about", "License: %1").arg(prv.current.license)
                    return url ? license + ' · <a href="' + url + '">' + url + '</a>' : license
                }
            }
        }

        StyledFlickable {
            id: licenseFlickable

            Layout.fillWidth: true
            Layout.fillHeight: true

            contentHeight: licenseContainer.height
            contentWidth: licenseContainer.width
            flickableDirection: prv.currentIsRichText ? Flickable.VerticalFlick : Flickable.AutoFlickIfNeeded

            Rectangle {
                id: licenseContainer

                //! NOTE Plain-text licenses are hard-wrapped at ~80 columns (some have
                //! ASCII tables), so they keep their lines and scroll sideways if needed
                width: prv.currentIsRichText
                       ? licenseFlickable.width
                       : Math.max(licenseFlickable.width, plainLicenseLabel.implicitWidth + prv.contentTextMargin * 2)
                height: (prv.currentIsRichText ? richLicenseLabel.height : plainLicenseLabel.height) + prv.contentTextMargin * 2

                color: ui.theme.backgroundSecondaryColor

                StyledTextLabel {
                    id: richLicenseLabel

                    x: prv.contentTextMargin
                    y: prv.contentTextMargin
                    width: licenseFlickable.width - prv.contentTextMargin * 2
                    visible: prv.currentIsRichText

                    horizontalAlignment: Text.AlignLeft
                    elide: Text.ElideNone
                    wrapMode: Text.WordWrap
                    textFormat: Text.RichText

                    text: prv.currentIsRichText ? prv.currentText : ""
                }

                StyledTextLabel {
                    id: plainLicenseLabel

                    x: prv.contentTextMargin
                    y: prv.contentTextMargin
                    visible: !prv.currentIsRichText

                    horizontalAlignment: Text.AlignLeft
                    elide: Text.ElideNone
                    wrapMode: Text.NoWrap
                    textFormat: Text.PlainText
                    font.family: "Menlo"
                    font.pixelSize: ui.theme.bodyFont.pixelSize - 1

                    text: prv.currentIsRichText ? "" : prv.currentText
                }
            }
        }
    }

    Connections {
        target: listView

        function onCurrentIndexChanged() {
            licenseFlickable.contentY = 0
            licenseFlickable.contentX = 0
        }
    }
}
