/*
 * GlideMix: Home > Learn -- where to start, what's new, and where to get help.
 */
import QtQuick 2.15
import QtQuick.Layouts 1.15

import Muse.Ui 1.0
import Muse.UiComponents

import Audacity.AppShell 1.0

Rectangle {
    id: root

    color: ui.theme.backgroundSecondaryColor

    HomeLearnModel {
        id: learnModel
    }

    readonly property string releasesUrl: "https://github.com/cristitm76/glidemix-releases"

    readonly property var cards: [
        { icon: IconCode.LEARN, title: qsTrc("appshell", "Getting started"),
          text: qsTrc("appshell", "What GlideMix does and how a mix comes together, with pictures."),
          button: qsTrc("appshell", "Open the guide"), url: root.releasesUrl + "#readme" },
        { icon: IconCode.STAR, title: qsTrc("appshell", "What's new"),
          text: qsTrc("appshell", "Every version and what changed, from the first build on."),
          button: qsTrc("appshell", "Read the changelog"), url: root.releasesUrl + "/blob/main/CHANGELOG.md" },
        { icon: IconCode.SHORTCUTS, title: qsTrc("appshell", "Keyboard shortcuts"),
          text: qsTrc("appshell", "Every shortcut in one list, searchable, and yours to change."),
          button: qsTrc("appshell", "Show shortcuts"), action: "show-shortcuts" },
        { icon: IconCode.GLOBE, title: qsTrc("appshell", "Community"),
          text: qsTrc("appshell", "Ask questions, share mixes and ideas with other DJs on Discord."),
          button: qsTrc("appshell", "Join on Discord"), url: "https://discord.gg/EvWwBAvfuE" },
        { icon: IconCode.QUESTION_MARK, title: qsTrc("appshell", "Help and support"),
          text: qsTrc("appshell", "Something not working? Write to us, we answer every message."),
          button: "support@glidemix.com", url: "mailto:support@glidemix.com" },
        { icon: IconCode.INFO, title: qsTrc("appshell", "Licence"),
          text: qsTrc("appshell", "Enter your key, buy GlideMix, or free this computer for another one."),
          button: qsTrc("appshell", "Open licence"), action: "show-licence" }
    ]

    StyledFlickable {
        anchors.fill: parent
        contentHeight: content.height + 96

        Column {
            id: content

            x: 48
            y: 48
            width: parent.width - 96
            spacing: 24

            StyledTextLabel {
                text: qsTrc("appshell", "Learn")
                font: ui.theme.titleBoldFont
                horizontalAlignment: Text.AlignLeft
            }

            Flow {
                width: parent.width
                spacing: 16

                Repeater {
                    model: root.cards

                    delegate: Rectangle {
                        required property var modelData

                        width: 300
                        height: 172
                        radius: 6
                        color: ui.theme.backgroundPrimaryColor
                        border.color: ui.theme.strokeColor
                        border.width: 1

                        Column {
                            anchors.fill: parent
                            anchors.margins: 16
                            spacing: 10

                            Row {
                                spacing: 10

                                StyledIconLabel {
                                    iconCode: modelData.icon
                                    font.pixelSize: 20
                                }

                                StyledTextLabel {
                                    text: modelData.title
                                    font: ui.theme.bodyBoldFont
                                }
                            }

                            StyledTextLabel {
                                width: parent.width
                                height: 48
                                text: modelData.text
                                wrapMode: Text.WordWrap
                                maximumLineCount: 3
                                horizontalAlignment: Text.AlignLeft
                                verticalAlignment: Text.AlignTop
                            }

                            FlatButton {
                                text: modelData.button
                                accentButton: true
                                onClicked: {
                                    if (modelData.action) {
                                        learnModel.dispatch(modelData.action)
                                    } else {
                                        Qt.openUrlExternally(modelData.url)
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
