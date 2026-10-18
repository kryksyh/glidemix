/*
* Audacity: A Digital Audio Editor
*/
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

import Muse.Ui
import Muse.UiComponents

import Audacity.ProjectScene 1.0
import Audacity.Playback 1.0

Item {
    id: root

    property alias navigationSection: navPanel.section
    property alias navigationOrderStart: navPanel.order

    //! The Perform button's and the red playhead's colour.
    readonly property color roomColor: "#E5484D"
    //! Read through root inside the Repeater, where `model` is the delegate's.
    readonly property bool showsCue: model.showsCueOutput
    function showCue(cue) { model.showCueOutput(cue) }

    NavigationPanel {
        id: navPanel

        name: "PlaybackMeterPanel"
        enabled: root.enabled && root.visible
        direction: NavigationPanel.Horizontal

        accessible.name: qsTrc("playback", "Playback meter")
    }

    PlaybackMeterPanelModel {
        id: model

        onIsPlayingChanged: {
            if (model.isPlaying) {
                leftVolumePressure.reset()
                leftVolumePressure.resetClipped()
                rightVolumePressure.reset()
                rightVolumePressure.resetClipped()
            }
        }
    }

    Component.onCompleted: {
        model.init()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.bottomMargin: 2
        spacing: 0

        FlatButton {
            id: meterOptionsBtn

            Layout.preferredWidth: 28
            Layout.preferredHeight: 28
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 12
            Layout.bottomMargin: 8

            icon: model.showsCueOutput ? IconCode.NONE : IconCode.AUDIO
            //: Tooltip of the playback meter settings button
            toolTipTitle: model.showsCueOutput ? qsTrc("playback", "Headphone cue meter settings")
                                               : qsTrc("playback", "Playback meter settings")
            accentButton: popup.isOpened

            navigation.name: "PlaybackMeterSettings"
            navigation.panel: navPanel
            navigation.order: 1

            HeadphonesIcon {
                button: meterOptionsBtn
                visible: model.showsCueOutput
            }

            onClicked: {
                popup.toggleOpened()
            }

            PlaybackMeterCustomisePopup {
                id: popup

                placementPolicies: PopupView.PreferLeft
                y: -28

                model: model.meterModel
            }
        }

        // GlideMix: what the one meter and its fader are for -- Room
        // (the Perform output, red like its playhead) or Cue (the headphones,
        // white like the Play playhead). Perform switches it to Room.
        Repeater {
            model: [
                { cue: false, label: qsTrc("playback", "ROOM"), color: root.roomColor,
                  tip: qsTrc("playback", "Meter and fader: what the audience hears (Perform)") },
                { cue: true, label: qsTrc("playback", "CUE"), color: ui.theme.fontPrimaryColor,
                  tip: qsTrc("playback", "Meter and fader: your headphones (Play)") }
            ]

            delegate: Rectangle {
                id: sourceBtn

                required property var modelData
                required property int index
                readonly property bool selected: root.showsCue === modelData.cue

                Layout.preferredWidth: 44
                Layout.preferredHeight: 20
                Layout.alignment: Qt.AlignHCenter
                Layout.bottomMargin: index === 1 ? 8 : 4

                radius: 3
                color: selected ? modelData.color : "transparent"
                border.width: 1
                border.color: selected ? modelData.color : ui.theme.strokeColor

                StyledTextLabel {
                    anchors.centerIn: parent
                    text: sourceBtn.modelData.label
                    font: ui.theme.bodyBoldFont
                    color: sourceBtn.selected ? (sourceBtn.modelData.cue ? ui.theme.backgroundPrimaryColor : "#FFFFFF")
                                              : ui.theme.fontPrimaryColor
                }

                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: root.showCue(sourceBtn.modelData.cue)
                    onContainsMouseChanged: {
                        if (containsMouse) {
                            ui.tooltip.show(sourceBtn, sourceBtn.modelData.tip)
                        } else {
                            ui.tooltip.hide(sourceBtn)
                        }
                    }
                }
            }
        }

        Item {
            id: meterContainer

            Layout.fillHeight: true
            Layout.preferredWidth: root.width

            Row {
                id: meterChannelRow

                spacing: 2

                anchors.fill: parent
                anchors.leftMargin: 4
                anchors.rightMargin: 4
                anchors.topMargin: 2
                anchors.bottomMargin: 6

                VolumePressureMeter {
                    id: leftVolumePressure

                    anchors.top: parent.top
                    height: parent.height - ruler.bottomTextMargin

                    overloadHeight: 10

                    currentVolumePressure: model.leftChannelPressure
                    currentRMS: model.leftChannelRMS

                    meterModel: model.meterModel

                    indicatorWidth: 10
                }

                VolumePressureMeter {
                    id: rightVolumePressure

                    anchors.top: parent.top
                    height: parent.height - ruler.bottomTextMargin

                    overloadHeight: 10

                    currentVolumePressure: model.rightChannelPressure
                    currentRMS: model.rightChannelRMS

                    meterModel: model.meterModel

                    indicatorWidth: 10
                }

                VolumePressureRuler {
                    id: ruler

                    meterModel: model.meterModel

                    anchors.top: parent.top
                    anchors.topMargin: leftVolumePressure.overloadHeight - topTextMargin
                    anchors.bottom: parent.bottom
                }
            }

            VerticalVolumeSlider {
                id: volumeSlider

                orientation: Qt.Vertical

                meterModel: model.meterModel

                volumeLevel: model.level

                anchors.top: parent.top
                anchors.topMargin: 2 + leftVolumePressure.overloadHeight - (handleWidth / 2)
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.leftMargin: 3

                handleWidth: 24
                handleColor: model.showsCueOutput ? ui.theme.fontPrimaryColor : root.roomColor

                navigation.name: "PlaybackVolume"
                navigation.panel: navPanel
                navigation.order: meterOptionsBtn.navigation.order + 1

                onVolumeLevelMoved: function (level) {
                    leftVolumePressure.reset()
                    leftVolumePressure.resetClipped()
                    rightVolumePressure.reset()
                    rightVolumePressure.resetClipped()

                    model.volumeLevelChangeRequested(Math.round(level * 100) / 100)
                }

                onHandlePressed: function () {
                    leftVolumePressure.reset()
                    leftVolumePressure.resetClipped()
                    rightVolumePressure.reset()
                    rightVolumePressure.resetClipped()
                }

                onDecreaseRequested: {
                    if (volumeLevel <= from) {
                        return
                    }
                    model.volumeLevelChangeRequested(Math.round(volumeLevel * 100) / 100 - stepSize)
                }

                onIncreaseRequested: {
                    if (volumeLevel >= to) {
                        return
                    }
                    model.volumeLevelChangeRequested(Math.round(volumeLevel * 100) / 100 + stepSize)
                }
            }

            MouseArea {
                id: overloadClickArea

                anchors.top: meterChannelRow.top
                anchors.left: meterContainer.left
                anchors.right: meterContainer.right

                height: leftVolumePressure.overloadHeight + 2

                z: 10

                // This will avoid to reset the volume levels when clicking on the overload area
                enabled: (volumeSlider.handleY >= leftVolumePressure.overloadHeight)

                onClicked: function (mouse) {
                    leftVolumePressure.reset()
                    leftVolumePressure.resetClipped()
                    rightVolumePressure.reset()
                    rightVolumePressure.resetClipped()
                }
            }
        }
    }
}
