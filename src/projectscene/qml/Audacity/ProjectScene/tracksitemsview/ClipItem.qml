import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

import Muse.Ui
import Muse.UiComponents
import Muse.GraphicalEffects

import Audacity.ProjectScene
import Audacity.Playback
import Audacity.Spectrogram
import Audacity.UiComponents
import Audacity.Automation
import "../trackspanel" as TracksPanel

Rectangle {
    id: root

    property alias context: waveView.context
    property alias clipKey: waveView.clipKey
    property alias clipTime: waveView.clipTime
    property alias title: titleLabel.text
    required property bool isAutomationEnabled
    required property bool isWaveformViewVisible
    required property bool isSpectrogramViewVisible
    property int pitch: 0
    property bool isPitchModified: false
    property int speedPercentage: 0
    property bool isSpeedModified: false
    property bool showChannelSplitter: false
    property alias channelHeightRatio: waveChannelSplitter.channelHeightRatio
    property var canvas: null
    required property int headerHeight
    property color clipColor: ui.theme.extra["clip_color_1"]
    property color clipSelectedColor: ui.theme.extra["clip_selected_color_1"]
    // GlideMix: the track's colour intensity (clip menu > Color > Intensity…).
    property real colorIntensity: 1.0
    property color normalHeaderColor: root.currentClipStyle == ClipStyle.COLORFUL ? root.clipColor : root.classicHeaderColor
    property color selectedHeaderColor: root.currentClipStyle == ClipStyle.COLORFUL ? ui.blendColors(ui.theme.extra["white_color"], root.clipColor, 0.3) : classicHeaderColor
    property color normalHeaderHoveredColor: root.currentClipStyle == ClipStyle.COLORFUL ? ui.blendColors(ui.theme.extra["white_color"], root.clipColor, 0.8) : classicHeaderHoveredColor
    property color selectedHeaderHoveredColor: root.currentClipStyle == ClipStyle.COLORFUL ? ui.blendColors(ui.theme.extra["white_color"], root.clipColor, 0.2) : classicHeaderHoveredColor
    readonly property color classicHeaderColor: ui.theme.extra["classic_clip_header_color"]
    readonly property color classicHeaderHoveredColor: ui.theme.extra["classic_clip_header_hover_color"]
    property int currentClipStyle: ClipStyle.COLORFUL
    property bool isGrouped: false
    property bool clipSelected: false
    property bool clipIntersectsSelection: false
    property bool clipFocused: false
    property bool isDataSelected: false
    property bool isMultiSelectionActive: false
    property bool multiClipsSelected: root.isMultiSelectionActive && root.clipSelected
    property bool moveActive: false
    property bool isAudible: true
    property bool isLinear: false
    property real dbRange: -60.0
    property var displayBounds: ({
            "min": -1.0,
            "max": 1.0
        })
    property real selectionStart: 0
    property real selectionWidth: 0
    required property bool selectionInProgress
    required property bool selectionEditInProgress
    required property bool verticalSelectionEditInProgress
    property bool enableCursorInteraction: !selectionInProgress && !selectionEditInProgress && !verticalSelectionEditInProgress && !isBrush
    property bool isContrastFocusBorderEnabled: false

    required property real selectionStartFrequency
    required property real selectionEndFrequency
    required property bool spectralSelectionEnabled
    required property var pressedSpectrogram
    required property bool splitToolActive

    property real distanceToLeftNeighbor: -1
    property real distanceToRightNeighbor: -1

    property real leftVisibleMargin: 0
    property real rightVisibleMargin: 0

    property bool collapsed: false

    property bool multiSampleEdit: false

    property bool asymmetricStereoHeightsPossible: false

    signal clipStartEditRequested
    signal clipEndEditRequested
    signal cancelClipDragEditRequested

    signal clipLeftTrimRequested(bool completed, int action)
    signal clipRightTrimRequested(bool completed, int action)
    signal clipLeftStretchRequested(bool completed, int action)
    signal clipRightStretchRequested(bool completed, int action)

    signal requestSelected
    signal requestSelectionReset
    signal splitterPositionChangeRequested(int position)

    signal pitchChangeRequested
    signal pitchResetRequested

    signal speedChangeRequested
    signal speedResetRequested

    signal titleEditStarted
    signal titleEditAccepted(var newTitle)
    signal titleEditCanceled

    signal startAutoScroll
    signal stopAutoScroll

    // mouse position event is not propagated on overlapping mouse areas
    // so we are handling it manually
    signal clipItemMousePositionChanged(real x, real y)

    property alias navigation: navCtrl

    radius: 4
    color: clipSelected ? clipSelectedColor : clipColor
    border.color: ui.theme.extra["black_color"]
    opacity: root.moveActive && (clipSelected || clipIntersectsSelection) ? 0.5 : isAudible ? 1.0 : 0.3

    onMoveActiveChanged: {
        root.parent.z = moveActive && (clipSelected || clipIntersectsSelection) ? 1 : 0
    }

    property int borderWidth: 1
    property bool hover: root.containsMouse || root.headerHovered
    property bool headerHovered: false
    property var lastSample: undefined
    property bool altPressed: false
    property bool isBrush: waveView.isStemPlot && root.altPressed
    property bool isIsolationMode: false
    property bool containsMouse: false
    property bool automationMouseHoverLetThrough: false
    property alias isNearSample: waveView.isNearSample
    property alias currentChannel: waveView.currentChannel
    property bool leftTrimContainsMouse: false
    property bool rightTrimContainsMouse: false
    property alias leftTrimPressedButtons: leftTrimStretchEdgeHover.pressedButtons
    property alias rightTrimPressedButtons: rightTrimStretchEdgeHover.pressedButtons
    property bool enableFocusBorder: (navCtrl ? navCtrl.highlight : false) || root.clipSelected
    property bool enableDefaultBorder: !isContrastFocusBorderEnabled && enableFocusBorder
    property bool enableContrastBorder: isContrastFocusBorderEnabled && enableFocusBorder

    PlaybackStateModel {
        id: playbackState
    }

    ClipGainModel {
        id: clipGainModel

        clipKey: root.clipKey
    }
    ClipPluginAutomationModel {
        id: pluginAutomationModel
        clipKey: root.clipKey
        Component.onCompleted: init()
    }

    // for navigating between clips
    NavigationControl {
        id: navCtrl
        name: root.name
        enabled: root.enabled && root.visible

        accessible.role: MUAccessible.Button
        accessible.name: qsTrc("projectscene", "Clip: %1").arg(root.title)

        onActiveChanged: function (active) {
            // Make sure the focus navigation border is visible on top of other clips
            root.parent.z = active ? 1 : 0
            if (active) {
                root.forceActiveFocus()
            }
        }

        onTriggered: {
            clipNavigationPanel.requestActive()
        }
    }

    Rectangle {
        id: borderRect

        anchors.fill: parent
        color: "transparent"
        border.width: root.enableContrastBorder ? 2 : 1
        border.color: ui.theme.extra["black_color"]
        radius: root.enableContrastBorder ? 0 : 4
        z: root.parent.z + 1
    }

    NavigationFocusBorder {
        id: focusBorder

        navigationCtrl: navCtrl

        border.color: ui.theme.fontPrimaryColor
        border.width: 2
        radius: 4

        visible: root.enableDefaultBorder
    }

    NavigationFocusBorder {
        id: contrastFocusBorder

        navigationCtrl: navCtrl

        border.color: ui.theme.extra["white_color"]
        border.width: 2
        radius: 4

        visible: root.enableContrastBorder
    }

    QtObject {
        id: prv

        readonly property int doubleClickInterval: 400
        readonly property int doubleClickMaxDistance: 5

        property bool singleMenuLoaded: false
        //! GlideMix: where on the waveform the clip menu was opened,
        //! for Anchors > Set IN / OUT anchor here.
        property real menuWaveX: 0
        property bool multiMenuLoaded: false

        function ensureSingleMenuLoaded() {
            if (!singleMenuLoaded) {
                singleClipContextMenuModel.load()
                singleMenuLoaded = true
            }
        }

        function ensureMultiMenuLoaded() {
            if (!multiMenuLoaded) {
                multiClipContextMenuModel.load()
                multiMenuLoaded = true
            }
        }
    }

    // panel for navigating within the clip's items
    property NavigationPanel clipNavigationPanel: NavigationPanel {
        name: "ClipNavigationPanel"
        enabled: navCtrl.active
        direction: NavigationPanel.Horizontal
        section: navigation.panel ? navigation.panel.section : null
        onActiveChanged: function (active) {
            if (active) {
                root.forceActiveFocus()
            }
        }

        onNavigationEvent: function (event) {
            if (event.type === NavigationEvent.Escape && !clipHandles.leftTrimActive && !clipHandles.rightTrimActive && !clipHandles.leftStretchActive && !clipHandles.rightStretchActive) {
                navCtrl.requestActive()
            }
        }
    }

    onAsymmetricStereoHeightsPossibleChanged: {
        if (!asymmetricStereoHeightsPossible) {
            root.ratioChanged(showChannelSplitter ? 0.5 : 1)
        }
    }

    function editTitle() {
        editLoader.edit(titleLabel.text)
    }

    function acceptEditTitle(newTitle) {
        Qt.callLater(root.titleEditAccepted, newTitle)
    }

    function openContextMenu() {
        if (root.multiClipsSelected || root.isGrouped) {
            prv.ensureMultiMenuLoaded()
        } else {
            prv.ensureSingleMenuLoaded()
        }
        menuBtn.toggleMenu(menuBtn)
    }

    function mousePositionChanged(x, y) {
        clipItemMousePositionChanged(x, y)
        waveView.onWaveViewPositionChanged(x, y - header.height)
    }

    function mousePressAndHold(x, y) {
        root.altPressed ? waveView.smoothLastClickPos(x, y - header.height) : waveView.setLastClickPos(x, y - header.height, x, y - header.height)
        waveView.update()
    }

    function mouseReleased() {
        waveView.isNearSample = false
        waveView.onWaveViewPositionChanged(lastSample.x, lastSample.y)
    }

    function getSpectrogramHit(y /* relative to tracks canvas */) {
        return spectrogramViewLoader.item ? spectrogramViewLoader.item.getSpectrogramHit(y) : null
    }

    function setLastSample(x, y) {
        lastSample = {
            x: x,
            y: y - header.height
        }
    }

    function setContainsMouse(containsMouse) {
        if (!root.enableCursorInteraction) {
            return
        }

        root.containsMouse = containsMouse || root.automationMouseHoverLetThrough
        if (!root.containsMouse && !root.multiSampleEdit) {
            waveView.isNearSample = false
        }
    }

    function updateViews() {
        waveView.update()
        if (spectrogramViewLoader.item) {
            spectrogramViewLoader.item.update()
        }
    }

    ClipContextMenuModel {
        id: singleClipContextMenuModel
        clipKey: root.clipKey

        onClipTitleEditRequested: {
            root.editTitle()
        }

        onClipColorPickerRequested: {
            Qt.callLater(function () { clipColorPicker.open() })
        }

        onTrackColorPickerRequested: {
            Qt.callLater(function () { trackColorPicker.open() })
        }

        onResetColorsRequested: trackColorModel.resetColors()

        onClipColorIntensityRequested: {
            Qt.callLater(function () { clipColorIntensityPopup.open() })
        }

        onAnchorActionRequested: function(action) {
            if (action === "reset") {
                waveView.resetAnchors()
            } else {
                waveView.dragAnchor(action === "set-in" ? 0 : 1, prv.menuWaveX, true)
            }
        }
    }

    // GlideMix: Color > Intensity -- how strongly this track's clips
    // carry their colour. Color by key is a ticked item in the same submenu.
    // Where the clip menu was opened; the intensity popup appears there.
    Item {
        id: clipColorIntensityAnchor
        width: 1
        height: 1
    }

    StyledPopupView {
        id: clipColorIntensityPopup

        parent: clipColorIntensityAnchor
        contentWidth: 240
        contentHeight: intensityColumn.implicitHeight
        margins: 14
        // Closes on a click outside it or Esc, like the parked app's menu.
        closePolicies: PopupView.CloseOnPressOutsideParent

        Column {
            id: intensityColumn

            width: 240
            spacing: 12

            Item {
                width: parent.width
                height: 20

                StyledTextLabel {
                    anchors.left: parent.left
                    anchors.right: resetIntensityButton.left
                    anchors.verticalCenter: parent.verticalCenter
                    horizontalAlignment: Text.AlignLeft
                    text: qsTrc("projectscene", "Color intensity")
                }

                FlatButton {
                    id: resetIntensityButton
                    anchors.right: closeIntensityButton.left
                    anchors.rightMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    width: 20
                    height: 20
                    icon: IconCode.UNDO
                    transparent: true
                    toolTipTitle: qsTrc("projectscene", "Reset to full")
                    onClicked: singleClipContextMenuModel.colorIntensity = 255
                }

                // A red dot in the corner closes it.
                Rectangle {
                    id: closeIntensityButton
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: 14
                    height: 14
                    radius: 7
                    color: closeArea.containsMouse ? "#FF5F57" : "#E0443E"

                    StyledTextLabel {
                        anchors.centerIn: parent
                        text: "\u00D7"
                        color: "#4A0002"
                        font.pixelSize: 11
                        font.bold: true
                        visible: closeArea.containsMouse
                    }

                    MouseArea {
                        id: closeArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: clipColorIntensityPopup.close()
                    }
                }
            }

            StyledSlider {
                width: parent.width
                from: 0
                to: 255
                stepSize: 1
                value: singleClipContextMenuModel.colorIntensity
                onMoved: singleClipContextMenuModel.colorIntensity = Math.round(value)
            }
        }
    }

    TracksPanel.TrackColorPalettePopup {
        id: clipColorPicker
        parent: ui.rootItem
        colors: singleClipContextMenuModel.colorPalette
        currentColorIndex: singleClipContextMenuModel.currentColorIndex
        showReset: true

        onColorSelected: function(colorIndex) {
            singleClipContextMenuModel.setClipColor(colorIndex)
        }
        onResetRequested: singleClipContextMenuModel.resetClipColor()
    }

    // GlideMix: the clip's track colour, from the clip's Color menu.
    TrackContextMenuModel {
        id: trackColorModel
        trackId: root.clipKey ? root.clipKey.trackId() : -1
    }

    TracksPanel.TrackColorPalettePopup {
        id: trackColorPicker
        parent: ui.rootItem
        colors: trackColorModel.colorPalette
        currentColorIndex: trackColorModel.currentColorIndex

        onColorSelected: function(colorIndex) {
            trackColorModel.setTrackColor(colorIndex)
        }
    }

    ContextMenuLoader {
        id: singleClipContextMenuLoader

        onHandleMenuItem: function (itemId) {
            singleClipContextMenuModel.handleMenuItem(itemId)
        }
    }

    MultiClipContextMenuModel {
        id: multiClipContextMenuModel
    }

    ContextMenuLoader {
        id: multiClipContextMenuLoader

        onHandleMenuItem: function (itemId) {
            multiClipContextMenuModel.handleMenuItem(itemId)
        }
    }

    Component.onCompleted: {
        playbackState.init()
        clipGainModel.init()
    }

    Component.onDestruction: {
        //! NOTE The outer component uses this information to handle the current cursor.
        // It is important to cleanup this state before removing the component
        // to prevent the cursor from being stuck in the wrong state.
        waveView.isNearSample = false
        waveView.isStemPlot = false
        root.leftTrimContainsMouse = false
        root.rightTrimContainsMouse = false
        root.headerHovered = false
    }

    MouseArea {
        id: hoverArea
        anchors.fill: parent

        hoverEnabled: true

        readonly property bool forbidden: (root.isNearSample || root.isIsolationMode) && playbackState.isPlaying
        cursorShape: forbidden ? Qt.ForbiddenCursor : Qt.BlankCursor

        function updateCustomCursor() {
            if (!forbidden) {
                CustomCursorProvider.setCursorShape(hoverArea, ":/images/customCursorShapes/IBeamCursor.png", 26)
            }
        }

        Component.onCompleted: updateCustomCursor()
        onForbiddenChanged: updateCustomCursor()

        acceptedButtons: Qt.RightButton

        visible: root.enableCursorInteraction

        onVisibleChanged: {
            root.setContainsMouse(containsMouse)
        }

        onClicked: function (e) {
            if (root.multiClipsSelected) {
                prv.ensureMultiMenuLoaded()
                if (e.modifiers & (Qt.ShiftModifier | Qt.ControlModifier)) {
                    if (!root.clipSelected) {
                        root.requestSelectionReset()
                    }
                    root.requestSelected()
                }
                multiClipContextMenuLoader.show(Qt.point(e.x, e.y), multiClipContextMenuModel.items)
            } else if (root.isGrouped) {
                prv.ensureMultiMenuLoaded()
                if (!root.clipSelected) {
                    root.requestSelectionReset()
                }
                root.requestSelected()
                multiClipContextMenuLoader.show(Qt.point(e.x, e.y), multiClipContextMenuModel.items)
            } else {
                prv.ensureSingleMenuLoaded()
                const menuPoint = mapToItem(root, e.x, e.y)
                prv.menuWaveX = mapToItem(waveView, e.x, e.y).x
                clipColorIntensityAnchor.x = menuPoint.x
                clipColorIntensityAnchor.y = menuPoint.y
                singleClipContextMenuLoader.show(Qt.point(e.x, e.y), singleClipContextMenuModel.items)
                root.requestSelected()
            }
        }

        onPositionChanged: function (e) {
            clipItemMousePositionChanged(e.x, e.y);

            // propagate mouse position to the wave view adjusting the y position
            waveView.onWaveViewPositionChanged(e.x, e.y - header.height)
        }

        onContainsMouseChanged: {
            if (!root.visible) {
                return
            }

            root.setContainsMouse(containsMouse)
        }
    }

    // NOTE: hover events from polyline are not visible in MouseArea
    // so we need to handle them manually
    HoverHandler {
        id: passiveHoverHandler

        enabled: root.isAutomationEnabled

        onHoveredChanged: {
            automationMouseHoverLetThrough = hovered
            root.setContainsMouse(hoverArea.containsMouse)
        }
    }

    MouseArea {
        id: leftTrimStretchEdgeHover

        x: distanceToLeftNeighbor >= -0.5 && distanceToLeftNeighbor <= 10 ? root.x - Math.min(distanceToLeftNeighbor / 2, 5) : root.x - 5
        width: distanceToLeftNeighbor >= -0.5 && distanceToLeftNeighbor <= 10 ? 6 + Math.min(distanceToLeftNeighbor / 2, 5) : 11
        z: headerDragArea.z + 1
        height: !root.collapsed ? root.height / 3 : root.height / 2

        anchors.top: root.top

        hoverEnabled: true
        visible: !root.clipSelected && root.enableCursorInteraction

        cursorShape: Qt.BlankCursor

        // make sure cursor is visible on top of nearby clips
        onContainsMouseChanged: {
            root.leftTrimContainsMouse = containsMouse
            if (containsMouse || pressedButtons) {
                root.parent.z = 1
            } else {
                root.parent.z = 0
            }
        }

        onPressed: function (e) {
            root.clipStartEditRequested()
        }

        onReleased: function (e) {
            if (e.modifiers & (Qt.AltModifier | Qt.MetaModifier)) {
                root.clipLeftStretchRequested(true, ClipBoundaryAction.Shrink)
            } else {
                root.clipLeftTrimRequested(true, ClipBoundaryAction.Shrink)
            }

            root.stopAutoScroll()
            // this needs to be always at the very end
            root.clipEndEditRequested()
        }

        onPositionChanged: function (e) {
            let mousePos = mapToItem(root, e.x, e.y)
            clipItemMousePositionChanged(mousePos.x, mousePos.y)

            if (e.modifiers & (Qt.AltModifier | Qt.MetaModifier)) {
                if (pressed) {
                    root.clipLeftStretchRequested(false, ClipBoundaryAction.Shrink)
                }
            } else {
                if (pressed) {
                    root.clipLeftTrimRequested(false, ClipBoundaryAction.Shrink)
                }
            }
        }

        onCanceled: e => {
            root.cancelClipDragEditRequested()
        }
    }

    MouseArea {
        id: rightTrimStretchEdgeHover

        x: root.width - 5
        z: headerDragArea.z + 1
        width: distanceToRightNeighbor >= -0.5 && distanceToRightNeighbor <= 10 ? 6 + Math.min(distanceToRightNeighbor / 2, 5) : 11
        height: !root.collapsed ? root.height / 3 : root.height / 2

        anchors.top: root.top

        hoverEnabled: true
        visible: !root.clipSelected && root.enableCursorInteraction

        cursorShape: Qt.BlankCursor

        // make sure cursor is visible on top of nearby clips
        onContainsMouseChanged: {
            root.rightTrimContainsMouse = containsMouse
            if (containsMouse || pressedButtons) {
                root.parent.z = 1
            } else {
                root.parent.z = 0
            }
        }

        onPressed: function (e) {
            root.clipStartEditRequested()
        }

        onReleased: function (e) {
            if (e.modifiers & (Qt.AltModifier | Qt.MetaModifier)) {
                root.clipRightStretchRequested(true, ClipBoundaryAction.Shrink)
            } else {
                root.clipRightTrimRequested(true, ClipBoundaryAction.Shrink)
            }

            root.stopAutoScroll()
            // this needs to be always at the very end
            root.clipEndEditRequested()
        }

        onPositionChanged: function (e) {
            let mousePos = mapToItem(root, e.x, e.y)
            clipItemMousePositionChanged(mousePos.x, mousePos.y)

            if (e.modifiers & (Qt.AltModifier | Qt.MetaModifier)) {
                if (pressed) {
                    root.clipRightStretchRequested(false, ClipBoundaryAction.Shrink)
                }
            } else {
                if (pressed) {
                    root.clipRightTrimRequested(false, ClipBoundaryAction.Shrink)
                }
            }
        }

        onCanceled: e => {
            root.cancelClipDragEditRequested()
        }
    }

    Rectangle {
        id: inner

        anchors.fill: parent

        layer.enabled: true
        layer.effect: RoundedCornersEffect {
            radius: root.radius
        }

        Rectangle {
            id: header
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right

            height: root.headerHeight
            z: 2

            visible: !root.collapsed || root.hover

            Rectangle {
                id: headerSelectionRectangle

                x: root.selectionStart
                z: 0 // Ensure this is below the header content
                width: Math.min(root.selectionWidth, header.width)

                anchors.top: header.top
                anchors.bottom: header.bottom

                color: waveView.transformColor(clipColor)
                visible: root.isDataSelected && currentClipStyle == ClipStyle.COLORFUL
            }

            MouseArea {
                id: headerDragArea
                anchors.fill: parent

                onContainsMouseChanged: {
                    if (!root.visible) {
                        return
                    }

                    root.headerHovered = containsMouse
                }

                visible: root.enableCursorInteraction

                acceptedButtons: Qt.LeftButton
                hoverEnabled: true
                cursorShape: root.splitToolActive ? Qt.ArrowCursor : Qt.OpenHandCursor

                property var lastClickTime: 0
                property point doubleClickStartPosition

                //! IMPORTANT NOTE: clip moving is handled in TracksItemsView (because
                // Clip UI element will be destroyed along with its MouseArea if the clip is moved
                // to the other track and we're gonna loose the ability to handle this MouseArea's events)
                // hence we need to let simple events pass (e.accepted = false). Unfortunately this breaks
                // detecting composed events like doubleClick so we need to take care of it manually.
                onPressed: function (e) {
                    if (root.splitToolActive) {
                        //! NOTE Let the press through so the split tool handles it
                        e.accepted = false
                        return
                    }

                    var currentTime = Date.now()
                    if (currentTime - lastClickTime < prv.doubleClickInterval) {
                        //! NOTE Handle doubleClick logic
                        root.editTitle()
                    } else {
                        //! NOTE Handle singleClick logic
                        if ((!root.multiClipsSelected || (e.modifiers & (Qt.ShiftModifier | Qt.ControlModifier))) && !(root.isDataSelected && isWithinRange(e.x, headerSelectionRectangle.x, headerSelectionRectangle.width))) {
                            root.requestSelected()
                        }

                        lastClickTime = currentTime
                        doubleClickStartPosition = Qt.point(e.x, e.y)
                    }

                    e.accepted = false
                }

                onPositionChanged: function (e) {
                    // Reset double click timer if the mouse has moved,
                    // to prevent rapid clip movement activate title editing
                    if (Math.abs(e.x - doubleClickStartPosition.x) > prv.doubleClickMaxDistance || Math.abs(e.y - doubleClickStartPosition.y) > prv.doubleClickMaxDistance) {
                        lastClickTime = 0
                    }

                    root.clipItemMousePositionChanged(e.x, e.y)

                    e.accepted = false
                }

                onReleased: function (e) {
                    e.accepted = false
                }

                function isWithinRange(val, x, w) {
                    return val >= x && val <= (x + w)
                }
            }

            StyledTextLabel {
                id: titleLabel
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: buttonsRow.left
                anchors.leftMargin: root.leftVisibleMargin + 4
                anchors.rightMargin: 8
                horizontalAlignment: Qt.AlignLeft

                NavigationControl {
                    id: titleEditNavCtrl
                    name: "TitleEditNavCtrl"
                    enabled: root.enabled && root.visible
                    panel: root.clipNavigationPanel
                    column: 3

                    accessible.role: MUAccessible.EditableText
                    accessible.name: qsTrc("projectscene", "Clip name: %1").arg(root.title)
                    accessible.enabled: titleEditNavCtrl.enabled

                    onTriggered: {
                        root.editTitle()
                    }
                }

                NavigationFocusBorder {
                    navigationCtrl: titleEditNavCtrl

                    anchors.topMargin: 1
                    anchors.bottomMargin: 0
                    radius: 5
                }
            }

            Loader {
                id: editLoader

                anchors.fill: titleLabel

                property bool isEditState: false
                sourceComponent: editLoader.isEditState ? titleEditComp : null

                function edit(text) {
                    root.titleEditStarted()
                    editLoader.isEditState = true
                    editLoader.item.currentText = text
                    editLoader.item.newTitle = text
                    editLoader.item.visible = true
                    editLoader.item.ensureActiveFocus()
                }
            }

            Component {
                id: titleEditComp

                TextInputField {
                    id: titleEdit

                    property string newTitle: ""

                    anchors.fill: parent
                    background.color: header.color
                    background.border.width: 0
                    background.radius: 0
                    inputField.color: titleLabel.color
                    textSidePadding: 0
                    visible: false

                    onTextChanged: function (text) {
                        titleEdit.newTitle = text
                    }

                    onAccepted: {
                        root.acceptEditTitle(titleEdit.newTitle)
                        editLoader.isEditState = false
                    }

                    onEscaped: {
                        editLoader.isEditState = false
                    }

                    onFocusChanged: {
                        if (!titleEdit.focus) {
                            titleEdit.visible = false
                            titleEdit.accepted()
                        }
                    }
                }
            }

            Row {
                id: buttonsRow

                anchors.right: parent.right
                anchors.rightMargin: 4
                anchors.verticalCenter: parent.verticalCenter

                spacing: 2

                ClipItemPropertyButton {
                    id: pitchBtn

                    mouseArea.visible: root.enableCursorInteraction

                    text: {
                        let semis = Math.trunc(root.pitch / 100)
                        let cents = Math.abs(root.pitch % 100).toString().padStart(2, "0")
                        return semis + (cents > "00" ? "." + cents : "")
                    }
                    icon: root.pitch > 0 ? IconCode.ARROW_UP : IconCode.ARROW_DOWN

                    visible: root.isPitchModified && header.width > (60 + pitchBtn.implicitWidth + speedBtn.implicitWidth * (root.isSpeedModified ? 1 : 0) + menuBtn.implicitWidth)

                    onClicked: function (mouse) {
                        if (mouse.modifiers & Qt.ControlModifier) {
                            root.pitchResetRequested()
                        } else {
                            root.pitchChangeRequested()
                        }
                    }
                }

                // GlideMix (Marc Mont): the song's key (as it sounds now), by the tempo.
                StyledTextLabel {
                    id: keyLabel
                    anchors.verticalCenter: parent.verticalCenter
                    // root.pitch: read again when the song is transposed.
                    text: root.pitch !== undefined && waveView ? waveView.keyText() : ""
                    visible: text.length > 0 && header.width > 120
                    font.pixelSize: 10
                    font.bold: true
                    opacity: 0.85
                }

                ClipItemPropertyButton {
                    id: speedBtn

                    mouseArea.visible: root.enableCursorInteraction

                    icon: IconCode.CLOCK
                    text: root.speedPercentage + "%"

                    visible: root.isSpeedModified && header.width > (60 + speedBtn.implicitWidth + menuBtn.implicitWidth)

                    onClicked: function (mouse) {
                        if (mouse.modifiers & Qt.ControlModifier) {
                            root.speedResetRequested()
                        } else {
                            root.speedChangeRequested()
                        }
                    }
                }

                MenuButton {
                    id: menuBtn
                    width: 16
                    height: 16
                    anchors.verticalCenter: parent.verticalCenter

                    mouseArea.visible: root.enableCursorInteraction

                    menuModel: (root.multiClipsSelected || root.isGrouped) ? multiClipContextMenuModel : singleClipContextMenuModel

                    accentColor: root.clipColor
                    hoverHitColor: root.clipSelectedColor

                    visible: header.width > (60 + menuBtn.implicitWidth)

                    navigation.name: "ClipMenuBtn"
                    navigation.panel: root.clipNavigationPanel
                    navigation.column: 4
                    navigation.accessible.name: qsTrc("projectscene", "Clip menu")

                    onHandleMenuItem: function (itemId) {
                        Qt.callLater(menuModel.handleMenuItem, itemId)
                    }

                    //! NOTE: Override doClicked function from FlatButton to run clip selection logic before the base
                    //! menu button emits clicked
                    function doClicked(mouse) {
                        if (root.multiClipsSelected || root.isGrouped) {
                            prv.ensureMultiMenuLoaded()
                        } else {
                            prv.ensureSingleMenuLoaded()
                        }

                        if (!root.multiClipsSelected || (mouse.modifiers & (Qt.ShiftModifier | Qt.ControlModifier))) {
                            if (!root.clipSelected) {
                                root.requestSelectionReset()
                            }
                            root.requestSelected()
                        }

                        Qt.callLater(menuBtn.clicked, mouse)
                    }
                }
            }
        }

        ColumnLayout {
            id: viewsColumn

            anchors.top: (!root.collapsed && header.visible) ? header.bottom : parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom

            spacing: 0

            WaveView {
                id: waveView
                visible: root.isWaveformViewVisible

                Layout.fillWidth: true
                Layout.fillHeight: true

                fillColor: waveView.backgroundColor

                channelHeightRatio: showChannelSplitter ? root.channelHeightRatio : 1

                clipColor: root.clipColor
                clipSelectedColor: root.clipSelectedColor
                colorIntensity: root.colorIntensity
                canvasColor: ui.theme.backgroundQuarternaryColor
                clipSelected: root.clipSelected
                isIsolationMode: root.isIsolationMode
                multiSampleEdit: root.multiSampleEdit
                isBrush: root.isBrush
                isLinear: root.isLinear
                dbRange: root.dbRange
                displayBounds: root.displayBounds

                function onWaveViewPositionChanged(x, y) {
                    if (waveView.isIsolationMode) {
                        waveView.setIsolatedPoint(x, y)
                        waveView.update()
                    } else if (root.multiSampleEdit && !root.altPressed) {
                        var lastX = root.lastSample.x
                        var lastY = root.lastSample.y
                        waveView.setLastClickPos(lastX, lastY, x, y)
                        waveView.update()
                    } else {
                        waveView.setLastMousePos(x, y)
                    }
                }

                // GlideMix: drag the IN / OUT anchors (flag or line).
                Item {
                    id: anchorHandles
                    anchors.fill: parent
                    z: 1000
                    property var positions: waveView.anchorPositions()
                    Connections {
                        target: waveView
                        function onAnchorsChanged() { anchorHandles.positions = waveView.anchorPositions() }
                        function onClipTimeChanged() { anchorHandles.positions = waveView.anchorPositions() }
                    }
                    Connections {
                        target: root.context
                        function onFrameTimeChanged() { anchorHandles.positions = waveView.anchorPositions() }
                        function onZoomChanged() { anchorHandles.positions = waveView.anchorPositions() }
                    }
                    Repeater {
                    model: 2
                    Item {
                        id: anchorHandle
                        readonly property real anchorX: anchorHandles.positions[index]
                        readonly property bool shown: anchorX >= -30 && anchorX <= waveView.width + 30
                        property real grabOffset: 0
                        anchors.fill: parent

                        function press(area, mouse) {
                            grabOffset = mouse.x + area.x - anchorX
                        }
                        function move(area, mouse, completed) {
                            waveView.dragAnchor(index, mouse.x + area.x - grabOffset, completed)
                        }

                        // The flag, and the whole line: either drags the anchor.
                        Repeater {
                            model: [
                                { dx: index === 0 ? -3 : -27, y: 12, w: 30, h: 17 },
                                { dx: -5, y: 0, w: 10, h: -1 }
                            ]
                            MouseArea {
                                visible: anchorHandle.shown
                                x: anchorHandle.anchorX + modelData.dx
                                y: modelData.y
                                width: modelData.w
                                height: modelData.h < 0 ? waveView.height : modelData.h
                                hoverEnabled: true
                                preventStealing: true
                                cursorShape: Qt.SizeHorCursor
                                acceptedButtons: Qt.LeftButton
                                onPressed: function(mouse) {
                                    // GlideMix: a volume point or line under the anchor line wins;
                                    // the IN / OUT flag still drags the anchor.
                                    if (modelData.h < 0) {
                                        const lane = pluginAutomationLane.visible ? pluginAutomationLane : automation
                                        const p = mapToItem(lane, mouse.x, mouse.y)
                                        if (lane.visible && lane.grabsAt(p.x, p.y)) {
                                            mouse.accepted = false
                                            return
                                        }
                                    }
                                    anchorHandle.press(this, mouse)
                                }
                                onPositionChanged: function(mouse) {
                                    if (pressed) {
                                        anchorHandle.move(this, mouse, false)
                                    }
                                }
                                onReleased: function(mouse) { anchorHandle.move(this, mouse, true) }
                                onDoubleClicked: waveView.resetAnchors()
                            }
                        }
                    }
                    }
                }

                ChannelSplitter {
                    id: waveChannelSplitter

                    anchors.fill: parent

                    visible: root.showChannelSplitter

                    editable: root.enableCursorInteraction && root.asymmetricStereoHeightsPossible
                    asymmetricStereoHeightsPossible: root.asymmetricStereoHeightsPossible

                    color: ui.theme.extra["black_color"]
                    opacity: 0.10

                    onPositionChangeRequested: function (position) {
                        root.splitterPositionChangeRequested(position)
                    }
                }

                onIsNearSampleChanged: {
                    if (root.isNearSample) {
                        waveView.forceActiveFocus()
                    }
                }

                onIsIsolationModeChanged: {
                    if (waveView.isIsolationMode) {
                        waveView.forceActiveFocus()
                    }
                }

                onIsStemPlotChanged: {
                    if (waveView.isStemPlot && hoverArea.containsMouse) {
                        // force mouse position update will update isNearSample
                        waveView.onWaveViewPositionChanged(hoverArea.mouseX, hoverArea.mouseY - header.height)
                    }
                }

                // Every other automated parameter of the clip, faint and not
                // editable, so all its lines (all EQ bands ...) show at once.
                Repeater {
                    model: pluginAutomationModel.visible ? pluginAutomationModel.otherLines : []
                    BezierAutomationLane {
                        anchors.fill: waveView
                        anchors.bottomMargin: 1
                        z: 9
                        enabled: false
                        opacity: 0.55
                        segments: modelData.segments
                        viewStart: waveView.itemStartTime - pluginAutomationModel.sequenceStart
                        viewEnd: waveView.itemEndTime - pluginAutomationModel.sequenceStart
                        lineColor: modelData.active ? modelData.color : "#8a8a8a"
                        stepped: modelData.stepped
                    }
                }

                BezierAutomationLane {
                    id: pluginAutomationLane
                    bottomInset: 10 + Math.min(16, Math.max(4, waveView.height * 0.14))
                    anchors.fill: waveView
                    anchors.bottomMargin: 1
                    z: 10
                    visible: pluginAutomationModel.visible
                    enabled: true
                    segments: pluginAutomationModel.segments
                    viewStart: waveView.itemStartTime - pluginAutomationModel.sequenceStart
                    viewEnd: waveView.itemEndTime - pluginAutomationModel.sequenceStart
                    selectionStart: root.context.selectionStartTime - pluginAutomationModel.sequenceStart
                    selectionEnd: root.context.selectionEndTime - pluginAutomationModel.sequenceStart
                    // Same envelope colour as stock Audacity's volume line
                    // (theme audio_envelope_line, #B7FF01 in dark.cfg); grey
                    // while overridden by its control, as Ableton draws it.
                    // Each parameter its own colour; grey while overridden, as Ableton draws it.
                    lineColor: pluginAutomationModel.active ? pluginAutomationModel.color : "#8a8a8a"
                    dashed: !pluginAutomationModel.automated
                    stepped: pluginAutomationModel.stepped
                    // The header's Low/Mid/High/Color lines glow like the pill's lanes.
                    glow: pluginAutomationModel.bandLine
                    glowBase: pluginAutomationModel.colorLine ? 0.5 : 0.0
                    onEditCommitted: function(data) { pluginAutomationModel.commit(data) }

                    // GlideMix: right-click on a point > Delete point.
                    ContextMenuLoader {
                        id: pluginPointMenu
                        property int node: -1
                        property int valueNode: -1   // the point itself (node is the line after it)
                        onHandleMenuItem: function(itemId) {
                            if (itemId === "set-value") {
                                pointValuePopup.openFor(pluginAutomationLane, pluginPointMenu.valueNode, false)
                            } else if (itemId === "delete-point") {
                                pluginAutomationLane.deletePoint(node)
                            } else if (itemId === "line-straight") {
                                pluginAutomationLane.setSCurve(node, false)
                            } else if (itemId === "line-s-curve") {
                                pluginAutomationLane.setSCurve(node, true)
                            }
                        }
                        onClosed: pluginAutomationLane.clearPointHighlight()
                    }
                    onPointMenuRequested: function(node, x, y) {
                        // GlideMix: the last point has no line after it -- its menu shapes the line into it.
                        const lastPoint = node === pluginAutomationLane.nodeCount() - 1
                        const pointNode = node
                        node = lastPoint ? node - 1 : node
                        pluginPointMenu.node = node
                        pluginPointMenu.valueNode = pointNode
                        // GlideMix: the line on to the next point, like the tempo line's glides.
                        const sCurve = pluginAutomationLane.isSCurve(node)
                        let items = [
                            { id: "set-value", title: qsTrc("projectscene", "Set value…") },
                            {},
                            { id: "line-straight", title: qsTrc("projectscene", "Line: straight"), checkable: true, checked: !sCurve },
                            { id: "line-s-curve", title: qsTrc("projectscene", "Line: S-curve (smooth start and end)"), checkable: true, checked: sCurve }
                        ]
                        if (pointNode > 0 && !lastPoint) {
                            items.push({})
                            items.push({ id: "delete-point", title: qsTrc("projectscene", "Delete point") })
                        }
                        pluginPointMenu.show(Qt.point(x, y), items)
                    }

                    // Readable over the waveform, and below the IN/OUT markers.
                    Rectangle {
                        anchors.fill: pluginLaneLabel
                        anchors.margins: -3
                        radius: 3
                        color: "#CC1E1E24"
                    }

                    StyledTextLabel {
                        id: pluginLaneLabel
                        anchors.bottom: parent.bottom
                        anchors.left: parent.left
                        anchors.leftMargin: 8
                        anchors.bottomMargin: 20
                        text: pluginAutomationModel.active ? pluginAutomationModel.label
                                                           : pluginAutomationModel.label + qsTrc("projectscene", "  (overridden)")
                        color: pluginAutomationModel.active ? pluginAutomationModel.color : "#8a8a8a"
                    }

                    // GlideMix: Ableton's "Show Automation in New Lane".
                    FlatButton {
                        anchors.verticalCenter: pluginLaneLabel.verticalCenter
                        anchors.left: pluginLaneLabel.right
                        anchors.leftMargin: 8
                        height: 18
                        text: qsTrc("projectscene", "Show in own lane")
                        toolTipTitle: qsTrc("projectscene", "Show this line in a lane of its own under the track")
                        onClicked: pluginAutomationModel.showInOwnLane()
                    }
                }

                BezierAutomationLane {
                    id: automation

                    anchors.fill: waveView
                    anchors.bottomMargin: 1
                    bottomInset: 10 + Math.min(16, Math.max(4, waveView.height * 0.14))
                    z: 10

                    visible: root.isAutomationEnabled && !pluginAutomationModel.visible

                    // Stock Audacity's own volume-envelope colour (master's
                    // ClipItem.qml: ui.theme.extra["audio_envelope_line"]).
                    lineColor: ui.theme.extra["audio_envelope_line"]
                    segments: clipGainModel.segments
                    viewStart: waveView.itemStartTime
                    viewEnd: waveView.itemEndTime
                    selectionStart: root.context.selectionStartTime
                    selectionEnd: root.context.selectionEndTime

                    // Live while dragging, like stock Audacity's
                    // PolylinePlot onPointMoved -> setPoint(..., completed=false):
                    // the envelope (and so the waveform) follows the point.
                    onEditChanged: function(data) {
                        clipGainModel.commitSegments(data, false)
                    }
                    onEditCommitted: function(data) {
                        clipGainModel.commitSegments(data, true)
                        tooltip.hide(true)
                    }

                    // GlideMix: right-click on a point > Delete point.
                    ContextMenuLoader {
                        id: gainPointMenu
                        property int node: -1
                        property int valueNode: -1   // the point itself (node is the line after it)
                        onHandleMenuItem: function(itemId) {
                            if (itemId === "set-value") {
                                pointValuePopup.openFor(automation, gainPointMenu.valueNode, true)
                            } else if (itemId === "delete-point") {
                                automation.deletePoint(node)
                            } else if (itemId === "line-straight") {
                                automation.setSCurve(node, false)
                            } else if (itemId === "line-s-curve") {
                                automation.setSCurve(node, true)
                            }
                        }
                        onClosed: automation.clearPointHighlight()
                    }
                    onPointMenuRequested: function(node, x, y) {
                        // GlideMix: the last point has no line after it -- its menu shapes the line into it.
                        const lastPoint = node === automation.nodeCount() - 1
                        const pointNode = node
                        node = lastPoint ? node - 1 : node
                        gainPointMenu.node = node
                        gainPointMenu.valueNode = pointNode
                        // GlideMix: the line on to the next point, like the tempo line's glides.
                        const sCurve = automation.isSCurve(node)
                        let items = [
                            { id: "set-value", title: qsTrc("projectscene", "Set value…") },
                            {},
                            { id: "line-straight", title: qsTrc("projectscene", "Line: straight"), checkable: true, checked: !sCurve },
                            { id: "line-s-curve", title: qsTrc("projectscene", "Line: S-curve (smooth start and end)"), checkable: true, checked: sCurve }
                        ]
                        if (pointNode > 0 && !lastPoint) {
                            items.push({})
                            items.push({ id: "delete-point", title: qsTrc("projectscene", "Delete point") })
                        }
                        gainPointMenu.show(Qt.point(x, y), items)
                    }


                    // The live dB readout while dragging a gain point -- real
                    // stock Audacity behaviour that PolylinePlot used to drive
                    // (onPointMoved/onActivePointChanged) before it was
                    // replaced by this component, which dropped it. Restored
                    // here using the same activePoint* properties BUT sourced
                    // from BezierAutomationLane's own new activePointChanged
                    // signal, converting its normalized 0..1 value back to a
                    // real gain via clipGainModel's own mapping (the lane
                    // itself doesn't know min/max/split).
                    onActivePointChanged: {
                        if (automation.hasActivePoint) {
                            fake.x = automation.activePointX
                            fake.y = automation.activePointY - 8
                            tooltip.value = gainToDb(clipGainModel.gainFromNormalized(automation.activePointValue))
                            tooltip.show(true)
                        } else {
                            tooltip.hide(true)
                        }
                    }

                    function gainToDb(g) {
                        if (g < 0.001) { // -60 dB
                            return "-∞"
                        }
                        const db = 20 * Math.log10(g)
                        return db.toFixed(1)
                    }

                    Item {
                        // NOTE: fakeItem for tooltip to follow
                        id: fake

                        height: 1
                        width: 1

                        enabled: false // so it doesn't steal mouse events

                        ValueTooltip {
                            id: tooltip

                            unitText: "dB"
                            sizingText: "-60.0dB"
                        }
                    }
                }
            }

            Loader {
                id: spectrogramViewLoader

                active: root.isSpectrogramViewVisible
                visible: active

                Layout.fillWidth: true
                Layout.fillHeight: true

                sourceComponent: ClipSpectrogramView {
                    canvas: root.canvas
                    clipId: root.clipKey.itemId()
                    trackId: root.clipKey.trackId()
                    selectionInProgress: root.selectionInProgress
                    selectionEditInProgress: root.selectionEditInProgress
                    verticalSelectionEditInProgress: root.verticalSelectionEditInProgress
                    spectralSelectionEnabled: root.spectralSelectionEnabled
                    pressedSpectrogram: root.pressedSpectrogram
                    isStereo: root.showChannelSplitter
                    channelHeightRatio: showChannelSplitter ? root.channelHeightRatio : 1

                    timelineIndentWidth: root.canvas.anchors.leftMargin
                    zoom: root.context.zoom
                    frameStartTime: root.context.frameStartTime
                    frameEndTime: root.context.frameEndTime
                    selectionStartTime: root.context.selectionStartTime
                    selectionEndTime: root.context.selectionEndTime
                    selectionStartFrequency: root.selectionStartFrequency
                    selectionEndFrequency: root.selectionEndFrequency
                    clipSelected: root.clipSelected

                    enabled: !root.splitToolActive

                    ChannelSplitter {
                        anchors.fill: parent

                        visible: root.showChannelSplitter

                        channelHeightRatio: parent.channelHeightRatio
                        editable: root.enableCursorInteraction && root.asymmetricStereoHeightsPossible
                        asymmetricStereoHeightsPossible: root.asymmetricStereoHeightsPossible

                        color: ui.theme.extra["black_color"]
                        opacity: 0.10

                        onPositionChangeRequested: function (position) {
                            root.splitterPositionChangeRequested(position)
                        }
                    }
                }
            }
        }

        FlatButton {
            id: accessibilitySelectBtn

            anchors.horizontalCenter: viewsColumn.horizontalCenter
            anchors.bottom: viewsColumn.bottom
            anchors.bottomMargin: 10

            navigation.name: "SelectBtn"
            navigation.panel: root.clipNavigationPanel
            navigation.column: 0

            width: 55
            height: 20
            text: !root.clipSelected ? qsTrc("clips", "Select") : qsTrc("clips", "Deselect")
            visible: root.clipNavigationPanel.highlight
            normalColor: ui.theme.extra["accessibility_clip_select_button_color"]

            onClicked: {
                if (!root.clipSelected) {
                    root.requestSelected()
                } else {
                    root.requestSelectionReset()
                }
            }
        }
    }

    // make sure clip and its handles are visible on top of nearby clips
    onClipSelectedChanged: {
        if (clipSelected) {
            root.parent.z = 1
        } else {
            root.parent.z = 0
        }
    }

    ClipHandles {
        id: clipHandles

        // +1 not to overlap with header
        y: !root.collapsed ? header.height + 1 : 0
        width: root.width
        handlesVisible: root.clipSelected && !root.moveActive
        canvas: root.canvas
        collapsed: root.collapsed
        clipHeight: root.height
        headerHeight: header.height
        altPressed: root.altPressed

        clipNavigationPanel: root.clipNavigationPanel

        onClipHandlesMousePositionChanged: function (xWithinClipHandles, yWithinClipHandles) {
            var xWithinClipItem = xWithinClipHandles
            var yWithinClipItem = header.height + 1 + yWithinClipHandles
            clipItemMousePositionChanged(xWithinClipItem, yWithinClipItem)
        }

        onClipStartEditRequested: function () {
            root.clipStartEditRequested()
        }

        onClipEndEditRequested: function () {
            root.clipEndEditRequested()
        }

        onCancelClipDragEditRequested: function () {
            root.cancelClipDragEditRequested()
        }

        onTrimLeftRequested: function (completed, action) {
            root.clipLeftTrimRequested(completed, action)
        }

        onTrimRightRequested: function (completed, action) {
            root.clipRightTrimRequested(completed, action)
        }

        onStretchLeftRequested: function (completed, action) {
            root.clipLeftStretchRequested(completed, action)
        }

        onStretchRightRequested: function (completed, action) {
            root.clipRightStretchRequested(completed, action)
        }

        onStopAutoScroll: {
            root.stopAutoScroll()
        }
    }

    state: "NORMAL"
    states: [
        State {
            name: "NORMAL"
            when: !root.clipSelected && !headerDragArea.containsMouse
            PropertyChanges {
                target: header
                color: root.normalHeaderColor
            }
            PropertyChanges {
                target: titleLabel
                color: ui.theme.extra["black_color"]
            }
            PropertyChanges {
                target: pitchBtn
                textColor: ui.theme.extra["black_color"]
                iconColor: ui.theme.extra["black_color"]
            }
            PropertyChanges {
                target: speedBtn
                textColor: ui.theme.extra["black_color"]
                iconColor: ui.theme.extra["black_color"]
            }
            PropertyChanges {
                target: menuBtn
                iconColor: ui.theme.extra["black_color"]
            }
        },
        State {
            name: "SELECTED"
            when: root.clipSelected && !headerDragArea.containsMouse
            PropertyChanges {
                target: header
                color: root.selectedHeaderColor
            }
            PropertyChanges {
                target: titleLabel
                color: ui.theme.extra["black_color"]
            }
            PropertyChanges {
                target: pitchBtn
                textColor: ui.theme.extra["black_color"]
                iconColor: ui.theme.extra["black_color"]
            }
            PropertyChanges {
                target: speedBtn
                textColor: ui.theme.extra["black_color"]
                iconColor: ui.theme.extra["black_color"]
            }
            PropertyChanges {
                target: menuBtn
                iconColor: ui.theme.extra["black_color"]
            }
        },
        State {
            name: "NORMAL_HEADER_HOVERED"
            when: !root.clipSelected && headerDragArea.containsMouse
            PropertyChanges {
                target: header
                color: root.normalHeaderHoveredColor
            }
            PropertyChanges {
                target: titleLabel
                color: ui.theme.extra["black_color"]
            }
            PropertyChanges {
                target: pitchBtn
                textColor: ui.theme.extra["black_color"]
                iconColor: ui.theme.extra["black_color"]
            }
            PropertyChanges {
                target: speedBtn
                textColor: ui.theme.extra["black_color"]
                iconColor: ui.theme.extra["black_color"]
            }
            PropertyChanges {
                target: menuBtn
                iconColor: ui.theme.extra["black_color"]
            }
        },
        State {
            name: "SELECTED_HEADER_HOVERED"
            when: root.clipSelected && headerDragArea.containsMouse
            PropertyChanges {
                target: header
                color: root.selectedHeaderHoveredColor
            }
            PropertyChanges {
                target: titleLabel
                color: ui.theme.extra["black_color"]
            }
            PropertyChanges {
                target: pitchBtn
                textColor: ui.theme.extra["black_color"]
                iconColor: ui.theme.extra["black_color"]
            }
            PropertyChanges {
                target: speedBtn
                textColor: ui.theme.extra["black_color"]
                iconColor: ui.theme.extra["black_color"]
            }
            PropertyChanges {
                target: menuBtn
                iconColor: ui.theme.extra["black_color"]
            }
        }
    ]

    // GlideMix: right-click a point > Set value… -- type it exactly. The
    // volume line in dB; the other lines in %, 100% being their normal level
    // (drawn at 80% of the height).
    Popup {
        id: pointValuePopup
        property var lane: null
        property int node: -1
        property bool isGain: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        padding: 12

        function textFor(v) {
            if (isGain) {
                const g = clipGainModel.gainFromNormalized(v)
                return g < 0.001 ? "-inf" : (20 * Math.log10(g)).toFixed(1)
            }
            return Math.round(v / 0.8 * 100).toString()
        }
        function openFor(laneItem, pointNode, gain) {
            lane = laneItem
            node = pointNode
            isGain = gain
            valueField.currentText = textFor(lane.pointValue(node))
            open()
            valueField.ensureActiveFocus()
            valueField.selectAll()
        }
        function apply() {
            const text = String(valueField.inputField.text).trim().replace(",", ".").replace(/(dB|%)$/i, "").trim()
            let v
            if (isGain) {
                if (/^-?inf/i.test(text)) {
                    v = 0
                } else {
                    const db = parseFloat(text)
                    if (isNaN(db)) { return }
                    v = clipGainModel.normalizedFromGain(Math.pow(10, db / 20))
                }
            } else {
                const pct = parseFloat(text)
                if (isNaN(pct)) { return }
                v = Math.max(0, Math.min(1, pct / 100 * 0.8))
            }
            lane.setPointValue(node, v)
            close()
        }

        background: Rectangle {
            color: ui.theme.backgroundPrimaryColor
            border.color: ui.theme.strokeColor
            radius: 4
        }
        contentItem: RowLayout {
            spacing: 8
            StyledTextLabel { text: qsTrc("projectscene", "Value") }
            TextInputField {
                id: valueField
                Layout.preferredWidth: 90
                onAccepted: pointValuePopup.apply()
                onEscaped: pointValuePopup.close()
            }
            StyledTextLabel { text: pointValuePopup.isGain ? "dB" : "%" }
            FlatButton {
                text: qsTrc("global", "OK")
                accentButton: true
                onClicked: pointValuePopup.apply()
            }
        }
    }

}
