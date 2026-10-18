/*
* Audacity: A Digital Audio Editor
*/
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents

import Audacity.ProjectScene

import "audio"

TrackItem {
    id: root

    TracksViewStateModel {
        id: automationViewState
        Component.onCompleted: init()
    }

    headerTrailingControlsComponent: Component {
        RowLayout {
            opacity: root.collapsed ? 1 : 0
            visible: opacity !== 0

            Behavior on opacity {
                OpacityAnimator {
                    duration: 100
                }
            }

            Loader {
                sourceComponent: trackControlButtons
            }
        }
    }

    extraControlsComponent: Component {
        ColumnLayout {
            spacing: 2

            RowLayout {
                id: topRow

                Layout.fillWidth: true

                spacing: 16

                opacity: root.collapsed ? 0 : 1
                visible: opacity !== 0
                Behavior on opacity {
                    OpacityAnimator {
                        duration: 100
                    }
                }

                PanKnob {
                    id: panKnob

                    value: Boolean(root.item) ? root.item.pan : 0

                    navigation.panel: root.headerNavigationPanel
                    navigation.order: root.extraControlsNavigationStart
                    navigation.enabled: !root.collapsed
                    //: Noun: accessibility name of the pan knob
                    navigation.accessible.name: qsTrc("projectscene", "Pan")

                    onNewPanRequested: function (newValue, completed) {
                        if (Boolean(root.item)) {
                            root.item.setPan(newValue, completed)
                        }
                    }
                }

                VolumeSlider {
                    id: volumeSlider

                    value: Boolean(root.item) ? root.item.volumeLevel : 0

                    navigation.panel: root.headerNavigationPanel
                    navigation.order: panKnob.navigation.order + 1
                    navigation.enabled: !root.collapsed
                    navigation.accessible.name: qsTrc("projectscene", "Track volume")

                    onNewVolumeRequested: function (newValue, completed) {
                        if (Boolean(root.item)) {
                            root.item.setVolumeLevel(newValue, completed)
                        }
                    }

                    Component.onCompleted: {
                        root.extraControlsNavigationEnd = Qt.binding(function () {
                            return volumeSlider.navigation.order
                        })
                    }
                }

                Loader {
                    sourceComponent: trackControlButtons
                }
            }

        }
    }

    rightSideContainerComponent: Component {
        Item {
            width: parent.width
            height: parent.height

            Row {
                id: volumePressureContainer

                property int indicatorWidth: 7

                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.horizontalCenter: parent.horizontalCenter

                spacing: 2

                function clearMeters() {
                    leftOrMonoVolumePressureMeter.reset()
                    leftOrMonoVolumePressureMeter.resetClipped()
                    rightVolumePressureMeter.reset()
                    rightVolumePressureMeter.resetClipped()
                }

                TapHandler {
                    onTapped: volumePressureContainer.clearMeters()
                }

                VolumePressureMeter {
                    id: leftOrMonoVolumePressureMeter
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    indicatorWidth: parent.indicatorWidth
                    meterModel: trackViewState.meterModel
                    currentVolumePressure: Boolean(root.item) ? root.item.leftChannelPressure : 0
                    currentRMS: Boolean(root.item) ? root.item.leftChannelRMS : 0
                }

                VolumePressureMeter {
                    id: rightVolumePressureMeter
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    indicatorWidth: parent.indicatorWidth
                    meterModel: trackViewState.meterModel
                    currentVolumePressure: Boolean(root.item) ? root.item.rightChannelPressure : 0
                    currentRMS: Boolean(root.item) ? root.item.rightChannelRMS : 0
                }

                Connections {
                    target: root.trackViewState

                    function onIsRecordingChanged() {
                        if (trackViewState.isRecording) {
                            leftOrMonoVolumePressureMeter.resetClipped()
                            rightVolumePressureMeter.resetClipped()
                        }

                        leftOrMonoVolumePressureMeter.reset()
                        rightVolumePressureMeter.reset()
                    }

                    function onIsPlayingChanged() {
                        if (trackViewState.isPlaying) {
                            volumePressureContainer.clearMeters()
                        }
                    }
                }

                Connections {
                    target: root

                    function onIsFocusedChanged() {
                        if (!root.isFocused) {
                            volumePressureContainer.clearMeters()
                        }
                    }
                }
            }

            states: [
                State {
                    when: Boolean(root.item) && root.item.channelCount === 1
                    name: "mono"
                    PropertyChanges {
                        target: volumePressureContainer
                        indicatorWidth: 8
                    }
                    PropertyChanges {
                        target: rightVolumePressureMeter
                        visible: false
                    }
                },
                State {
                    when: Boolean(root.item) && root.item.channelCount === 2
                    name: "stereo"
                    PropertyChanges {
                        target: volumePressureContainer
                        indicatorWidth: 7
                    }
                    PropertyChanges {
                        target: rightVolumePressureMeter
                        visible: true
                    }
                }
            ]
        }
    }

    Component {
        id: trackControlButtons

        RowLayout {
            spacing: 4

            // GlideMix: the effects panel, as a small button with the others
            // (it used to be a full-width bar of its own).
            FlatButton {
                id: effectsButton

                Layout.preferredWidth: 26
                Layout.preferredHeight: 20
                text: qsTrc("projectscene", "FX")
                textFont: ui.theme.bodyBoldFont
                toolTipTitle: qsTrc("projectscene", "Effects")
                toolTipDescription: qsTrc("projectscene", "Opens effects panel")

                navigation.panel: root.headerNavigationPanel
                navigation.order: root.headerTrailingControlsNavigationStart
                navigation.accessible.name: qsTrc("projectscene", "Effects")

                onClicked: {
                    root.openEffectsRequested()
                }
            }

            FlatToggleButton {
                id: automationButton

                Layout.preferredWidth: 20
                Layout.preferredHeight: Layout.preferredWidth
                icon: IconCode.AUTOMATION
                checked: automationViewState.clipGainAutomationEnabled
                // Make the track automation state unambiguous, matching the
                // active realtime-effect automation affordance.
                checkedColor: ui.theme.accentColor
                toolTipDescription: qsTrc("projectscene", "Automation")

                navigation.panel: root.headerNavigationPanel
                navigation.order: effectsButton.navigation.order + 1
                navigation.accessible.name: qsTrc("projectscene", "Automation")
                navigation.accessible.role: MUAccessible.CheckBox

                onToggled: {
                    automationViewState.toggleClipGainAutomation()
                }
            }

            FlatToggleButton {
                id: muteButton

                Layout.preferredWidth: 20
                Layout.preferredHeight: Layout.preferredWidth

                icon: IconCode.MUTE
                checked: Boolean(root.item) ? root.item.muted : false

                navigation.panel: root.headerNavigationPanel
                navigation.order: automationButton.navigation.order + 1
                //: Accessibility name of the mute button
                navigation.accessible.name: qsTrc("projectscene", "Mute")
                navigation.accessible.role: MUAccessible.CheckBox

                onToggled: {
                    if (Boolean(root.item)) {
                        root.item.muted = !checked
                    }
                }
            }

            FlatToggleButton {
                id: soloButton

                Layout.preferredWidth: 20
                Layout.preferredHeight: Layout.preferredWidth

                icon: IconCode.SOLO
                checked: Boolean(root.item) ? root.item.solo : false

                navigation.panel: root.headerNavigationPanel
                navigation.order: muteButton.navigation.order + 1
                //: Accessibility name of the solo button
                navigation.accessible.name: qsTrc("projectscene", "Solo")
                navigation.accessible.role: MUAccessible.CheckBox

                onToggled: {
                    if (Boolean(root.item)) {
                        root.item.solo = !checked
                    }
                }

                Component.onCompleted: {
                    root.headerTrailingControlsNavigationEnd = Qt.binding(function () {
                        return soloButton.navigation.order
                    })
                }
            }
        }
    }
}
