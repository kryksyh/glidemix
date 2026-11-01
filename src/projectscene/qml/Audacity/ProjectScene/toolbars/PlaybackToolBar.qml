/*
* Audacity: A Digital Audio Editor
*/
import QtQuick 2.15

import Muse.Ui 1.0
import Muse.UiComponents

import Audacity.Playback 1.0
import Audacity.Record 1.0
import Audacity.ProjectScene 1.0
import Audacity.UiComponents 1.0

import "internal"

Item {
    id: root

    property bool floating: false
    //! GlideMix: only the clocks, for the centered row above the toolbar
    property bool clocksOnly: false

    property int maximumWidth: 0
    property int maximumHeight: 0

    property alias navigationPanel: view.navigationPanel

    width: {
        let contentWidth = view.width + prv.customizeButtonSpaceWidth
        return maximumWidth > 0 ? Math.max(contentWidth, maximumWidth) : contentWidth
    }
    height: view.height

    signal relayoutRequested

    QtObject {
        id: prv

        property int customizeButtonSpaceWidth: root.clocksOnly ? 0 : 8 /* spacing */ + customizeButton.width + customizeButton.anchors.rightMargin
                                                                       + (rightRow.width > 0 ? rightRow.width + 12 : 0)
    }

    StyledToolBarView {
        id: view

        navigationPanel.name: "PlaybackToolBar"
        navigationPanel.accessible.name: qsTrc("projectscene", "Playback toolbar")

        anchors.verticalCenter: parent.verticalCenter

        spacing: 2

        rowHeight: (isMultiline || root.clocksOnly) ? 32 : 48
        topPadding: isMultiline ? 8 : 0
        bottomPadding: isMultiline ? 8 : 0

        useSpacersAsSeparators: true
        // The clocks row has no width limit: it only holds three fixed-size items
        maximumWidth: root.clocksOnly ? -1 : root.maximumWidth - prv.customizeButtonSpaceWidth

        model: PlaybackToolBarModel {
            clocksOnly: root.clocksOnly

            onItemsChanged: {
                root.relayoutRequested()
            }
        }

        sourceComponentCallback: function (type) {
            switch (type) {
            case PlaybackToolBarModel.PLAYBACK_CONTROL:
                return controlComp
            case PlaybackToolBarModel.PLAYBACK_LEVEL:
                return playbackLevelComp
            case PlaybackToolBarModel.PLAYBACK_TIME:
                return playbackTimeComp
            case PlaybackToolBarModel.PLAYBACK_BPM:
                return playbackBPMComp
            case PlaybackToolBarModel.PLAYBACK_TIME_SIGNATURE:
                return playbackTimeSignatureComp
            case PlaybackToolBarModel.RECORD_LEVEL:
                return recordLevelComp
            case PlaybackToolBarModel.PROJECT_CONTROL:
                return projectControlComp
            case PlaybackToolBarModel.SNAP:
                return snapComp
            case PlaybackToolBarModel.METRONOME:
                return metronomeComp
            case PlaybackToolBarModel.TEXT_TOGGLE:
                return textToggleComp
            case PlaybackToolBarModel.BROADCAST:
                return broadcastComp
            case PlaybackToolBarModel.SLIDER:
                return sliderComp
            }

            return null
        }

        Component {
            id: controlComp

            StyledToolBarItem {
                width: 28
                height: width

                backgroundRadius: 2

                iconFont: ui.theme.iconsFont
                iconColor: Boolean(itemData) ? itemData.iconColor : ui.theme.fontPrimaryColor
                accentColor: Boolean(itemData) ? itemData.backgroundColor : ui.theme.buttonColor
                accentButton: Boolean(itemData) ? itemData.selected : false
            }
        }

        Component {
            id: projectControlComp

            StyledToolBarItem {
                width: 28
                height: width

                backgroundRadius: 2

                iconFont: ui.theme.iconsFont
            }
        }

        Component {
            id: playbackLevelComp

            PlaybackLevel {
                id: playbackLevel

                property var itemData: null

                width: Boolean(itemData) ? Math.max(288, Math.min(itemData.meterSize, view.maximumWidth)) : 288
                height: 28

                backgroundRadius: 2

                volumeLevel: Boolean(itemData) ? itemData.level : 0
                leftCurrentVolumePressure: Boolean(itemData) ? itemData.leftChannelPressure : -145
                leftCurrentRMS: Boolean(itemData) ? itemData.leftChannelRMS : -145
                rightCurrentVolumePressure: Boolean(itemData) ? itemData.rightChannelPressure : -145
                rightCurrentRMS: Boolean(itemData) ? itemData.rightChannelRMS : -145
                isPlaying: Boolean(itemData) ? itemData.isPlaying : false
                showsPerformOutput: Boolean(itemData) ? itemData.showsPerformOutput : true

                enabled: Boolean(itemData) ? itemData.enabled : false

                onVolumeLevelChangeRequested: function (level) {
                    if (itemData) {
                        itemData.level = level
                    }
                }

                onWidthChangeRequested: function (x, y) {
                    let toolbarViewPosition = mapToItem(view, x, y)

                    if (toolbarViewPosition.x > view.maximumWidth) {
                        // Do not allow the component to exceed the maximum width of the toolbar view
                        return
                    }

                    if (itemData) {
                        itemData.meterSize = Math.max(288, x)
                    }
                }
            }
        }

        Component {
            id: playbackTimeComp

            Timecode {
                property var itemData: null

                accessibleName: qsTrc("projectscene", "Playback position:")

                value: Boolean(itemData) ? itemData.currentValue : 0

                mode: TimecodeModeSelector.TimePoint

                sampleRate: Boolean(itemData) ? itemData.sampleRate : 0
                tempo: Boolean(itemData) ? itemData.tempo : 0
                upperTimeSignature: Boolean(itemData) ? itemData.upperTimeSignature : -1
                lowerTimeSignature: Boolean(itemData) ? itemData.lowerTimeSignature : -1

                currentFormat: Boolean(itemData) ? itemData.currentFormat : -1

                enabled: Boolean(itemData) ? itemData.enabled : false

                onValueChangeRequested: function (newValue) {
                    if (!Boolean(itemData)) {
                        return
                    }

                    itemData.currentValue = newValue
                }

                onCurrentFormatChanged: {
                    if (!Boolean(itemData)) {
                        return
                    }

                    itemData.currentFormat = currentFormat
                }
            }
        }

        Component {
            id: playbackBPMComp

            BPM {
                property var itemData: null

                value: Boolean(itemData) ? itemData.currentValue : 0

                enabled: Boolean(itemData) ? itemData.enabled : false

                onValueChangeRequested: function (newValue) {
                    if (!Boolean(itemData)) {
                        return
                    }

                    itemData.currentValue = newValue
                }
            }
        }

        Component {
            id: playbackTimeSignatureComp

            TimeSignature {
                property var itemData: null

                upper: Boolean(itemData) ? itemData.upper : 0
                lower: Boolean(itemData) ? itemData.lower : 0

                enabled: Boolean(itemData) ? itemData.enabled : false

                onUpperChangeRequested: function (newValue) {
                    if (!Boolean(itemData)) {
                        return
                    }

                    itemData.upper = newValue
                }

                onLowerChangeRequested: function (newValue) {
                    if (!Boolean(itemData)) {
                        return
                    }

                    itemData.lower = newValue
                }
            }
        }

        Component {
            id: recordLevelComp

            RecordLevel {
                property var itemData: null

                width: 28
                height: width

                backgroundRadius: 2

                meterStyle: Boolean(itemData) ? itemData.meterStyle : PlaybackMeterStyle.Default

                icon: Boolean(itemData) ? itemData.icon : IconCode.NONE
                iconFont: ui.theme.iconsFont

                toolTipTitle: Boolean(itemData) ? itemData.title : ""
                toolTipDescription: Boolean(itemData) ? itemData.description : ""

                volumeLevel: Boolean(itemData) ? itemData.level : 0
                leftCurrentVolumePressure: Boolean(itemData) ? itemData.leftChannelPressure : 0
                rightCurrentVolumePressure: Boolean(itemData) ? itemData.rightChannelPressure : 0

                recordingChannelsCount: Boolean(itemData) ? itemData.recordingChannelsCount : 0

                isInputMonitoringOn: Boolean(itemData) ? itemData.isInputMonitoringOn : false
                isMicMeteringOn: Boolean(itemData) ? itemData.isMicMeteringOn : false

                enabled: Boolean(itemData) ? itemData.enabled : false

                onVolumeLevelChangeRequested: function (level) {
                    if (itemData) {
                        itemData.level = level
                    }
                }

                onIsInputMonitoringOnChangeRequested: function (enable) {
                    if (itemData) {
                        itemData.isInputMonitoringOn = enable
                    }
                }

                onIsMicMeteringOnChangeRequested: function (enable) {
                    if (itemData) {
                        itemData.isMicMeteringOn = enable
                    }
                }

                onIsPopupOpened: function (opened) {
                    if (itemData) {
                        itemData.listenMainAudioInput(opened)
                    }
                }
            }
        }

        // GlideMix: the parked MixHouse DJ app's header controls.
        // Icons are Tabler Icons (MIT), recoloured per theme at build time.
        Component {
            id: metronomeComp

            FlatButton {
                id: metronomeButton

                property var itemData: null

                width: 28
                height: width

                backgroundRadius: 2

                accentButton: Boolean(itemData) && itemData.isOn
                enabled: Boolean(itemData) ? itemData.enabled : false

                toolTipTitle: Boolean(itemData) ? itemData.title : ""
                toolTipDescription: qsTrc("projectscene", "Click to switch on or off. Right-click for click level, sound and tuning.")

                mouseArea.acceptedButtons: Qt.LeftButton | Qt.RightButton

                onClicked: function(mouse) {
                    if (!Boolean(itemData)) {
                        return
                    }
                    if (mouse.button === Qt.RightButton || metronomeMenu.isMenuOpened) {
                        metronomeMenu.toggleOpened(itemData.settingsMenu)
                        return
                    }
                    itemData.toggle()
                }

                Image {
                    anchors.centerIn: parent
                    width: 18
                    height: 18
                    sourceSize.width: 36
                    sourceSize.height: 36
                    source: "qrc:/resources/toolbaricons/metronome-"
                            + ((ui.theme.isDark || metronomeButton.accentButton) ? "dark" : "light") + ".svg"
                }

                // Same corner mark StyledToolBarItem draws for a secondary menu.
                Canvas {
                    property color fillColor: ui.theme.fontPrimaryColor
                    onFillColorChanged: requestPaint()

                    width: 4
                    height: 4

                    anchors.margins: 2
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom

                    onPaint: {
                        const ctx = getContext("2d")
                        ctx.fillStyle = fillColor
                        ctx.moveTo(width, 0)
                        ctx.lineTo(width, height)
                        ctx.lineTo(0, height)
                        ctx.closePath()
                        ctx.fill()
                    }
                }

                StyledMenuLoader {
                    id: metronomeMenu

                    onHandleMenuItem: function(itemId) {
                        metronomeButton.itemData.handleSettingsMenuItem(itemId)
                    }
                }
            }
        }

        Component {
            id: textToggleComp

            StyledToolBarItem {
                height: 28
                // SYNC / TEMPO fit 60; KEYLOCK needs a little more.
                width: Boolean(itemData) && itemData.title.length > 5 ? 78 : 60

                backgroundRadius: 2

                text: Boolean(itemData) ? itemData.title.toUpperCase() : ""
                textFont: ui.theme.bodyBoldFont

                // Performing (audience playback) is red, like a record light.
                accentColor: Boolean(itemData) && itemData.code === "mixhouse-perform" ? "#E5484D" : ui.theme.accentColor
            }
        }

        // GlideMix: the radio tower opens the broadcast window. Small
        // and icon-only: everything about the stream lives in that window.
        Component {
            id: broadcastComp

            StyledToolBarItem {
                id: broadcastItem

                height: 28
                width: 28

                backgroundRadius: 2
                text: ""
                icon: IconCode.NONE

                readonly property int broadcastState: Boolean(itemData) ? itemData.broadcastState : 0

                accentButton: broadcastState === 2
                accentColor: "#E5484D"

                BroadcastTower {
                    anchors.centerIn: parent
                    width: 17
                    height: 17
                    broadcastState: broadcastItem.broadcastState
                    baseColor: broadcastItem.broadcastState === 2 ? "#FFFFFF" : ui.theme.fontPrimaryColor
                    liveColor: "#FFFFFF"
                }
            }
        }

        Component {
            id: sliderComp

            Row {
                id: sliderRow

                property var itemData: null

                height: 28
                spacing: 4

                Image {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 16
                    height: 16
                    sourceSize.width: 32
                    sourceSize.height: 32
                    opacity: slider.enabled ? 1.0 : ui.theme.itemOpacityDisabled
                    source: "qrc:/resources/toolbaricons/"
                            + ((Boolean(sliderRow.itemData) && sliderRow.itemData.isTimeZoom) ? "arrows-horizontal-" : "arrows-vertical-")
                            + (ui.theme.isDark ? "dark" : "light") + ".svg"
                }

                // GlideMix: the DJ fader look (mock A), like the track volume.
                DjSlider {
                    id: slider

                    anchors.verticalCenter: parent.verticalCenter
                    width: 80
                    height: 20

                    from: 0
                    to: 1
                    stepSize: 0.01

                    value: Boolean(sliderRow.itemData) ? sliderRow.itemData.value : 0
                    enabled: Boolean(sliderRow.itemData) ? sliderRow.itemData.enabled : false

                    onMoved: {
                        if (Boolean(sliderRow.itemData)) {
                            sliderRow.itemData.value = value
                        }
                    }
                }
            }
        }

        Component {
            id: snapComp

            DropdownWithTitle {
                property var itemData: null

                width: 143
                height: 28

                //: Noun: label of the snapping control in the playback toolbar
                title: qsTrc("projectscene", "Snap")
                toggleAccessibleName: qsTrc("projectscene", "Snapping")
                dropdownAccessibleName: qsTrc("projectscene", "Snap to")
                toggleToolTipTitle: qsTrc("projectscene", "Snap")
                // GlideMix: SYNC locks library tracks to the grid even
                // with Snap off, which surprised people.
                toggleToolTipDescription: qsTrc("projectscene", "With SYNC on, tracks move by whole bars when Snap is off. Hold %1 while dragging to move freely.")
                                          .arg(Qt.platform.os === "osx" ? "⌘" : "Ctrl")

                current: Boolean(itemData) ? itemData.currentValue : ""
                model: Boolean(itemData) ? itemData.availableSnapTypes : null

                isOptionEnabled: Boolean(itemData) ? itemData.isSnapEnabled : false

                enabled: Boolean(itemData) ? itemData.enabled : false

                onIsOptionEnableChangeRequested: function (enabled) {
                    itemData.isSnapEnabled = enabled
                }

                onHandleMenuItem: function (itemId) {
                    itemData.handleMenuItem(itemId)
                }
            }
        }
    }

    // GlideMix: the zoom sliders, at the right end beside the customize button.
    Row {
        id: rightRow

        visible: !root.clocksOnly
        anchors.right: customizeButton.left
        anchors.rightMargin: 12
        anchors.verticalCenter: customizeButton.verticalCenter
        spacing: 12

        FlatButton {
            anchors.verticalCenter: parent.verticalCenter

            width: 28
            height: width

            icon: IconCode.FIT_PROJECT
            iconFont: ui.theme.iconsFont
            //: Tooltip of the toolbar button that resets zoom, lane heights and scrolling
            toolTipTitle: qsTrc("projectscene", "Reset view")
            toolTipDescription: qsTrc("projectscene", "Lanes at their default height, the first songs readable")

            enabled: view.model.isEnabled

            navigation.panel: root.navigationPanel
            navigation.order: 9999
            navigation.accessible.name: qsTrc("projectscene", "Reset view")

            onClicked: view.model.resetView()
        }

        Repeater {
            model: root.clocksOnly ? [] : view.model.rightItems

            Loader {
                required property var modelData
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                sourceComponent: sliderComp
                onLoaded: item.itemData = modelData
            }
        }
    }

    FlatButton {
        id: customizeButton

        visible: !root.clocksOnly

        anchors.right: parent.right
        anchors.rightMargin: 12
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 10

        width: 28
        height: width

        icon: IconCode.SETTINGS_COG
        iconFont: ui.theme.iconsFont
        //: Tooltip of the toolbar customization button
        toolTipTitle: qsTrc("projectscene", "Customize toolbar")
        toolTipDescription: qsTrc("projectscene", "Show/hide toolbar buttons")

        enabled: view.model.isEnabled

        navigation.panel: root.navigationPanel
        navigation.order: 10000
        navigation.accessible.name: qsTrc("projectscene", "Customize toolbar")

        onClicked: {
            customizePopup.toggleOpened()
        }

        PlaybackToolBarCustomisePopup {
            id: customizePopup

            anchorItem: !root.floating ? ui.rootItem : null
        }
    }
}
