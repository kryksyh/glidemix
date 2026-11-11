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

    // GlideMix's own icons for its toolbar buttons (resources/toolbaricons/gm-*).
    function gmIconFor(code) {
        switch (code) {
        case "mixhouse-sync": return "gm-sync"
        case "mixhouse-tempo-lane": return "gm-tempo"
        case "mixhouse-energy-lane": return "gm-energy"
        case "mixhouse-keylock": return "gm-keylock"
        case "mixhouse-link": return "gm-link"
        case "mixhouse-perform": return "gm-perform"
        case "mixhouse-mix-now": return "gm-mixnow"
        case "mixhouse-automation-arm": return "gm-arm"
        case "mixhouse-reenable-automation": return "gm-reenable"
        case "glidemix-flexible-space": return "gm-flexspace"
        }
        return ""
    }

    QtObject {
        id: prv

        property int customizeButtonSpaceWidth: root.clocksOnly ? 0 : 8 /* spacing */ + customizeButton.width + customizeButton.anchors.rightMargin
                                                                       + (rightRow.width > 0 ? rightRow.width + 12 : 0)
    }

    // GlideMix: right-click an empty part of the toolbar > Customize
    // Toolbar…, as in Safari. Below the buttons, so they keep their clicks.
    MouseArea {
        anchors.fill: parent
        visible: !root.clocksOnly
        acceptedButtons: Qt.RightButton
        onClicked: function(mouse) {
            toolbarMenu.toggleOpened([{ id: "customize", title: qsTrc("projectscene", "Customize Toolbar…") }],
                                     mouse.x, mouse.y)
        }

        StyledMenuLoader {
            id: toolbarMenu
            onHandleMenuItem: function(itemId) {
                if (itemId === "customize") {
                    customizePopup.open()
                }
            }
        }
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
        // GlideMix modern look: each group of buttons in its own capsule
        capsuleGroups: !root.clocksOnly
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
            case PlaybackToolBarModel.FLEX_SPACE:
                return flexSpaceComp
            }

            return null
        }

        Component {
            id: controlComp

            StyledToolBarItem {
                width: 28
                height: width

                backgroundRadius: 7

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

                backgroundRadius: 7

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

                backgroundRadius: 7

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

                backgroundRadius: 7

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

                backgroundRadius: 7

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

        // GlideMix: Customize Toolbar's Flexible Space takes the room left.
        Component {
            id: flexSpaceComp

            Item {
                property var itemData
                width: view.flexibleItemWidth
                height: 28
            }
        }

        Component {
            id: textToggleComp

            StyledToolBarItem {
                height: 28
                // GlideMix (Chris, 2026-11-11): Re-enable is greyed out until a
                // line is overridden, but its tooltip must still explain it, so
                // it keeps the hover (a click with nothing overridden does nothing).
                readonly property bool alwaysHover: Boolean(itemData) && itemData.code === "mixhouse-reenable-automation"
                enabled: alwaysHover || (Boolean(itemData) ? itemData.enabled : false)
                opacity: alwaysHover && !itemData.enabled ? 0.45 : 1.0
                // 0 icon and text, 1 icon only, 2 text only (Customize Toolbar).
                readonly property int displayStyle: view.model ? view.model.displayStyle : 1
                readonly property string svgName: Boolean(itemData) ? root.gmIconFor(itemData.code) : ""
                readonly property bool showIcon: svgName !== "" && displayStyle !== 2
                readonly property bool showText: displayStyle !== 1 || svgName === ""
                readonly property string label: Boolean(itemData) ? itemData.title.toUpperCase() : ""
                readonly property color lineColor: Qt.rgba(ui.theme.fontPrimaryColor.r, ui.theme.fontPrimaryColor.g,
                                                           ui.theme.fontPrimaryColor.b, ui.theme.isDark ? 0.26 : 0.34)

                TextMetrics {
                    id: labelMetrics
                    font: ui.theme.bodyBoldFont
                    text: textToggle.label
                }

                // Text only: a capsule sized to the word (SYNC / TEMPO fit 60;
                // KEYLOCK needs a little more). With an icon: a square tile
                // and the word beside it, outside the tile.
                width: !showIcon ? (Boolean(itemData) && itemData.title.length > 5 ? 78 : 60)
                       : height + (showText ? 7 + Math.ceil(labelMetrics.advanceWidth) + 4 : 0)

                id: textToggle

                // GlideMix modern look, text only: a capsule, filled only when
                // on, a thin outline when off (like the library pills).
                backgroundRadius: height / 2
                transparent: !accentButton || showIcon
                // With an icon the tile below is the whole look.
                Component { id: noBackground; Item {} }
                Component.onCompleted: {
                    const capsule = backgroundItem
                    backgroundItem = Qt.binding(function() { return textToggle.showIcon ? noBackground : capsule })
                }

                Rectangle {
                    anchors.fill: parent
                    visible: !textToggle.accentButton && !textToggle.showIcon
                    color: "transparent"
                    radius: height / 2
                    border.width: 1
                    border.color: textToggle.lineColor
                }

                text: showText && !showIcon ? label : ""
                textFont: ui.theme.bodyBoldFont

                // Icon tile: the same square button as Audacity's own toolbar
                // buttons (FlatButton's background: fill, opacity, border, top
                // sheen, hover and press), filled with the button's colour when on.
                Item {
                    id: tile
                    visible: textToggle.showIcon
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    width: textToggle.height
                    height: width

                    Rectangle {
                        anchors.fill: parent
                        radius: 6
                        color: textToggle.accentButton ? textToggle.accentColor : ui.theme.buttonColor
                        opacity: textToggle.accentButton ? 1.0
                                 : textToggle.mouseArea.pressed ? ui.theme.buttonOpacityHit
                                 : textToggle.mouseArea.containsMouse ? ui.theme.buttonOpacityHover
                                 : ui.theme.buttonOpacityNormal
                        border.width: ui.theme.borderWidth
                        border.color: ui.theme.strokeColor

                        Rectangle {
                            anchors.fill: parent
                            radius: parent.radius
                            gradient: Gradient {
                                GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, ui.theme.isDark ? 0.10 : 0.30) }
                                GradientStop { position: 0.5; color: Qt.rgba(1, 1, 1, 0) }
                            }
                        }
                    }

                    Image {
                        anchors.centerIn: parent
                        width: 18
                        height: 18
                        // Drawn at 4x and scaled down, so the lines stay sharp on Retina.
                        sourceSize.width: 72
                        sourceSize.height: 72
                        smooth: true
                        mipmap: true
                        // Light lines on the coloured fill when on; the theme's colour when off.
                        source: !textToggle.showIcon ? ""
                                : "qrc:/resources/toolbaricons/" + textToggle.svgName + "-"
                                  + (ui.theme.isDark || textToggle.accentButton ? "dark" : "light") + ".svg"
                    }
                }

                StyledTextLabel {
                    visible: textToggle.showIcon && textToggle.showText
                    anchors.left: tile.right
                    anchors.leftMargin: 7
                    anchors.verticalCenter: parent.verticalCenter
                    text: textToggle.label
                    font: ui.theme.bodyBoldFont
                    color: ui.theme.fontPrimaryColor
                }

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

                backgroundRadius: 7
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

            // GlideMix (Chris, 2026-11-11): its own reset icon on the standard
            // button background, like the other toolbar buttons.

            Image {
                anchors.centerIn: parent
                width: 18
                height: 18
                sourceSize.width: 72
                sourceSize.height: 72
                smooth: true
                mipmap: true
                source: "qrc:/resources/toolbaricons/gm-reset-" + (ui.theme.isDark ? "dark" : "light") + ".svg"
            }
            //: Tooltip of the toolbar button that resets zoom, lane heights and scrolling
            toolTipTitle: qsTrc("projectscene", "Reset view")
            toolTipDescription: qsTrc("projectscene", "Puts the view back to normal: tracks at their default height, zoomed so the first songs are readable, scrolled to the start")

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
        toolTipDescription: qsTrc("projectscene", "Drag buttons in and out of the toolbar")

        enabled: view.model.isEnabled

        navigation.panel: root.navigationPanel
        navigation.order: 10000
        navigation.accessible.name: qsTrc("projectscene", "Customize toolbar")

        onClicked: {
            customizePopup.toggleOpened()
        }

        // GlideMix: Safari-style Customize Toolbar (drag items in and out).
        CustomizeToolbarSheet {
            id: customizePopup

            anchorItem: !root.floating ? ui.rootItem : null
        }
    }
}
