/*
 * GlideMix
 *
 * The radio operator's desk: what is needed while on air, in one small window
 * that stays above the app. Opened by the tower in the header and kept open
 * for the show -- closing it asks first, because it takes the stream down.
 */
import QtQuick
import QtQuick.Layouts

import Muse.Ui 1.0
import Muse.UiComponents

import Audacity.ProjectScene 1.0

StyledDialogView {
    id: root

    objectId: "BroadcastHud"
    title: qsTrc("projectscene", "Broadcast")

    contentWidth: 320
    contentHeight: 452
    margins: 16

    modal: false
    //! The operator has to be able to see the stream while working in the app
    //! and in whatever else is on the desktop, so the HUD floats above the lot.
    alwaysOnTop: true
    alwaysAboveApp: true
    resizable: false

    BroadcastHudModel {
        id: hud
    }

    Component.onCompleted: hud.init()

    function requestClose() {
        if (hud.confirmClose()) {
            root.hide()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 62
            radius: 4
            color: hud.state === 2 ? "#3A1416" : ui.theme.backgroundSecondaryColor
            border.width: 1
            border.color: hud.state === 2 ? "#E5484D"
                          : hud.state === 3 ? "#7A2E31" : ui.theme.strokeColor

            RowLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 10

                BroadcastTower {
                    Layout.preferredWidth: 32
                    Layout.preferredHeight: 32
                    broadcastState: hud.state
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1

                    StyledTextLabel {
                        Layout.fillWidth: true
                        horizontalAlignment: Qt.AlignLeft
                        font: ui.theme.largeBodyBoldFont
                        color: hud.state === 2 ? "#FF6B6F" : ui.theme.fontPrimaryColor
                        text: {
                            if (hud.state === 1) {
                                return qsTrc("projectscene", "Connecting…")
                            }
                            if (hud.state !== 2) {
                                return qsTrc("projectscene", "Off air")
                            }
                            var s = hud.secondsOnAir
                            var mm = Math.floor(s / 60)
                            var ss = s % 60
                            return qsTrc("projectscene", "ON AIR") + "   "
                                   + (mm < 10 ? "0" : "") + mm + ":" + (ss < 10 ? "0" : "") + ss
                        }
                    }

                    StyledTextLabel {
                        Layout.fillWidth: true
                        horizontalAlignment: Qt.AlignLeft
                        wrapMode: Text.WordWrap
                        maximumLineCount: 2
                        opacity: 0.85
                        text: hud.statusText.length > 0 ? hud.statusText
                              : qsTrc("projectscene", "Not connected")
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            FlatButton {
                Layout.fillWidth: true
                text: (hud.state === 2 || hud.state === 1)
                      ? qsTrc("projectscene", "Stop") : qsTrc("projectscene", "Go on air")
                accentButton: hud.state !== 2
                onClicked: hud.toggle()
            }

            FlatButton {
                text: qsTrc("projectscene", "Reconnect")
                enabled: hud.state !== 1
                onClicked: hud.reconnect()
            }
        }

        CheckBox {
            Layout.fillWidth: true
            text: qsTrc("projectscene", "Go back on air by itself if the server drops")
            checked: hud.autoReconnect
            onClicked: hud.autoReconnect = !checked
        }

        SeparatorLine {}

        // ---- what is going out --------------------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                StyledTextLabel {
                    horizontalAlignment: Qt.AlignLeft
                    font: ui.theme.bodyBoldFont
                    text: qsTrc("projectscene", "Stream level")
                }

                Item { Layout.fillWidth: true }

                //! What is actually going out, read off the stream itself -
                //! not the fader position. Red once it touches full scale,
                //! because that is the encoder clipping.
                StyledTextLabel {
                    readonly property real peak: Math.max(hud.peakLeft, hud.peakRight)

                    font: ui.theme.bodyBoldFont
                    color: peak >= -0.1 ? "#E5484D"
                           : peak > -3 ? "#E5A03A" : ui.theme.fontPrimaryColor
                    text: hud.state !== 2 ? "--"
                          : peak <= -60 ? qsTrc("projectscene", "silent")
                          : (peak > 0 ? "+" : "") + peak.toFixed(1) + " dBFS"
                }
            }

            //! Peak of what the listeners get. It reads like the app's own
            //! meters but runs to +12, so the gain above can be pushed and
            //! the headroom that is left stays visible.
            BroadcastMeter {
                Layout.fillWidth: true

                peakLeft: hud.peakLeft
                peakRight: hud.peakRight
                active: hud.state === 2
            }

            //! The trim on the way to the encoder, kept apart from the
            //! reading above it so the two numbers cannot be confused.
            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                StyledTextLabel {
                    horizontalAlignment: Qt.AlignLeft
                    opacity: 0.8
                    text: qsTrc("projectscene", "Gain")
                }

                StyledSlider {
                    Layout.fillWidth: true
                    from: -24
                    to: 12
                    stepSize: 0.5
                    value: hud.gainDb
                    onMoved: hud.gainDb = value
                }

                StyledTextLabel {
                    Layout.preferredWidth: 54
                    horizontalAlignment: Qt.AlignRight
                    opacity: 0.8
                    text: (hud.gainDb > 0 ? "+" : "") + hud.gainDb.toFixed(1) + " dB"
                }
            }
        }

        SeparatorLine {}

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6

            StyledTextLabel {
                horizontalAlignment: Qt.AlignLeft
                font: ui.theme.bodyBoldFont
                text: qsTrc("projectscene", "Source")
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                FlatButton {
                    Layout.fillWidth: true
                    text: qsTrc("projectscene", "Perform")
                    accentButton: hud.source === 0
                    onClicked: hud.source = 0
                }

                FlatButton {
                    Layout.fillWidth: true
                    text: qsTrc("projectscene", "Cue")
                    accentButton: hud.source === 1
                    onClicked: hud.source = 1
                }
            }

            StyledTextLabel {
                Layout.fillWidth: true
                horizontalAlignment: Qt.AlignLeft
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                opacity: 0.7
                text: hud.source === 0
                      ? qsTrc("projectscene", "The audience mix, what Perform plays.")
                      : qsTrc("projectscene", "The headphone cue, what Play plays.")
            }
        }

        SeparatorLine {}

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6

            StyledTextLabel {
                Layout.fillWidth: true
                horizontalAlignment: Qt.AlignLeft
                elide: Text.ElideMiddle
                opacity: 0.85
                text: hud.serverSummary.length > 0 ? hud.serverSummary
                      : qsTrc("projectscene", "No server set yet")
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                FlatButton {
                    Layout.fillWidth: true
                    text: qsTrc("projectscene", "Settings…")
                    onClicked: hud.openSettings()
                }

                FlatButton {
                    text: qsTrc("projectscene", "Close")
                    onClicked: root.requestClose()
                }
            }
        }

        Item { Layout.fillHeight: true }
    }
}
