/*
* Audacity: A Digital Audio Editor
*/
import QtQuick 2.15
import QtQuick.Layouts 1.15

import Muse.Ui 1.0
import Muse.UiComponents

import Audacity.AppShell

ColumnLayout {
    id: root

    required property AboutModel model

    spacing: prv.contentSpacing

    QtObject {
        id: prv

        readonly property int logoSize: 96
        readonly property int headerSpacing: 16
        readonly property int versionTextSpacing: 4
        readonly property int contentSpacing: 16
        readonly property int contentMargin: 16
        readonly property int contentTextMargin: 12
        readonly property int contentTextSpacing: 8
    }

    RowLayout {
        Layout.alignment: Qt.AlignHCenter
        Layout.topMargin: prv.contentMargin

        spacing: prv.headerSpacing

        Image {
            Layout.preferredWidth: prv.logoSize
            Layout.preferredHeight: prv.logoSize

            source: ui.theme.isDark ? "qrc:/resources/about/AppIconDark.png" : "qrc:/resources/about/AppIconLight.png"
            sourceSize.width: prv.logoSize * 2
            sourceSize.height: prv.logoSize * 2
            smooth: true
            mipmap: true

            MouseArea {
                anchors.fill: parent

                property int clickCount: 0

                onClicked: {
                    clickCount++
                    if (clickCount % 3 === 0) {
                        root.model.toggleDevMode()
                    }
                }
            }
        }

        ColumnLayout {
            Layout.alignment: Qt.AlignVCenter
            spacing: prv.versionTextSpacing

            StyledTextLabel {
                horizontalAlignment: Text.AlignLeft
                text: root.model.appName()
                font: ui.theme.titleBoldFont
            }

            StyledTextLabel {
                horizontalAlignment: Text.AlignLeft
                text: root.model.appVersion()
                font: ui.theme.tabBoldFont
            }

            StyledTextLabel {
                horizontalAlignment: Text.AlignLeft
                text: qsTrc("appshell/about", "Cut it, beatmatch it, put it on the radio.")
                font: ui.theme.bodyFont
            }
        }
    }

    StyledFlickable {
        id: creditsFlickable

        Layout.fillWidth: true
        Layout.fillHeight: true

        contentHeight: creditsContainer.height

        Rectangle {
            id: creditsContainer

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: prv.contentMargin
            anchors.rightMargin: prv.contentMargin

            height: creditsInner.height + prv.contentTextMargin * 2

            color: ui.theme.backgroundSecondaryColor

            Column {
                id: creditsInner

                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right

                anchors.margins: prv.contentTextMargin

                spacing: prv.contentTextSpacing

                //! What the app is for, before whose code it is built on. Someone
                //! opening this box for the first time wants the former.
                Column {
                    width: parent.width
                    spacing: prv.contentTextSpacing

                    StyledTextLabel {
                        width: parent.width
                        horizontalAlignment: Text.AlignLeft
                        wrapMode: Text.WordWrap
                        font: ui.theme.bodyFont
                        text: qsTrc("appshell/about", "One app for the whole thing. Most setups make you pick: a DJ deck that cannot edit, or an editor that cannot mix. This does both, then hands the result to your listeners.")
                    }

                    Repeater {
                        model: [
                            {
                                "title": qsTrc("appshell/about", "Chop it up"),
                                "body": qsTrc("appshell/about", "A full audio editor underneath. Record, trim, fade, run effects, export. Nothing about the DJ side takes that away.")
                            },
                            {
                                "title": qsTrc("appshell/about", "Know your tracks"),
                                "body": qsTrc("appshell/about", "Tempo, key and beat grids worked out for you, and a key wheel that shows which track will actually sit next to this one. Bring your rekordbox library along.")
                            },
                            {
                                "title": qsTrc("appshell/about", "Build the mix"),
                                "body": qsTrc("appshell/about", "Drop tracks on a timeline, snap them to the bar, and let the transitions blend. Take an hour over a blend or hit Mix Now and move on.")
                            },
                            {
                                "title": qsTrc("appshell/about", "Play it out"),
                                "body": qsTrc("appshell/about", "Perform goes to the room, Play goes to your headphones, on their own outputs. Cue the next one while the floor hears none of it.")
                            },
                            {
                                "title": qsTrc("appshell/about", "Go on air"),
                                "body": qsTrc("appshell/about", "Stream it straight to your Icecast or Shoutcast server. That is it. That is the whole step. You have a radio station.")
                            }
                        ]

                        Column {
                            width: parent.width
                            spacing: 2

                            StyledTextLabel {
                                width: parent.width
                                horizontalAlignment: Text.AlignLeft
                                text: modelData.title
                                font: ui.theme.bodyBoldFont
                            }

                            StyledTextLabel {
                                width: parent.width
                                horizontalAlignment: Text.AlignLeft
                                wrapMode: Text.WordWrap
                                text: modelData.body
                                font: ui.theme.bodyFont
                            }
                        }
                    }

                    StyledTextLabel {
                        width: parent.width
                        horizontalAlignment: Text.AlignLeft
                        wrapMode: Text.WordWrap
                        font: ui.theme.bodyFont
                        color: ui.theme.fontSecondaryColor
                        text: qsTrc("appshell/about", "Open source under the GPL, and staying that way. No account, no telemetry, nothing phoning home. Go and make something.")
                    }
                }

                SeparatorLine { width: parent.width }

                StyledTextLabel {
                    width: parent.width
                    text: qsTrc("appshell/about", "Credits")
                    font: ui.theme.largeBodyBoldFont
                    horizontalAlignment: Text.AlignLeft
                }

                Repeater {
                    model: root.model.creditList()

                    Column {
                        width: parent.width
                        spacing: prv.contentTextSpacing

                        StyledTextLabel {
                            width: parent.width

                            horizontalAlignment: Text.AlignLeft

                            text: modelData.title
                            font: ui.theme.bodyBoldFont
                        }

                        StyledTextLabel {
                            width: parent.width
                            visible: Boolean(modelData.subtitle)

                            horizontalAlignment: Text.AlignLeft
                            wrapMode: Text.WordWrap

                            text: modelData.subtitle || ""
                            font: ui.theme.bodyFont
                        }

                        StyledTextLabel {
                            width: parent.width

                            horizontalAlignment: Text.AlignLeft
                            wrapMode: Text.WordWrap
                            textFormat: Text.RichText
                            font: ui.theme.bodyFont

                            text: modelData.credits.map(function (c) {
                                if (c.raw && c.raw.length > 0) {
                                    return c.raw
                                }

                                let name = (c.url && c.url.length > 0) ? '<a href="' + c.url + '">' + c.name + '</a>' : c.name
                                return c.role ? name + ", " + c.role : name
                            }).join("<br>")
                        }
                    }
                }

                Column {
                    width: parent.width
                    spacing: prv.contentTextSpacing

                    StyledTextLabel {
                        width: parent.width
                        horizontalAlignment: Text.AlignLeft
                        wrapMode: Text.WordWrap
                        text: qsTrc("appshell/about", "<b>GlideMix</b> is free software, released under the GNU General Public License version 3. The licenses of every bundled component are in the Legal tab.")
                        font: ui.theme.bodyFont
                    }

                    StyledTextLabel {
                        width: parent.width
                        horizontalAlignment: Text.AlignLeft
                        wrapMode: Text.WordWrap
                        text: qsTrc("appshell/about", "Audacity®, MuseScore®, Mixxx and VST® are trademarks of their respective owners. GlideMix is an independent project, not affiliated with or endorsed by them.")
                        font: ui.theme.bodyFont
                        color: ui.theme.fontSecondaryColor
                    }
                }
            }
        }
    }
}
