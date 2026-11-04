import QtQuick
import QtQuick.Controls

import Muse.Ui
import Muse.UiComponents

import Audacity.ProjectScene
import Audacity.Project
import Audacity.Playback
import Audacity.Spectrogram
import Audacity.Record
import Audacity.MixHouse

Rectangle {
    id: root

    property var navPanels: null

    //! NOTE: the control of the empty project, it has no item of its own,
    //! so the tracks area draws the navigation focus border for it
    property NavigationControl navDefaultControl: null

    readonly property alias tracksAreaItem: content

    property bool itemHovered: false
    property bool itemHeaderHovered: false
    property var hoveredItemKey: null

    property var hoveredTrackId: -1
    property double hoveredTrackVerticalPosition
    property double hoveredTrackHeight
    property bool tracksHovered: false

    property bool guidelineVisible: false
    property double guidelinePos: -1

    property alias altPressed: tracksViewState.altPressed
    property alias ctrlPressed: tracksViewState.ctrlPressed
    property alias isSplitMode: splitToolController.active

    signal externalDropAreaEntered(var drop)
    signal externalDropAreaExited
    signal externalDropAreaDropped(var drop)

    onExternalDropAreaEntered: drop => {
        importDropArea.externalDropAreaEntered(drop)
    }

    onExternalDropAreaExited: {
        importDropArea.externalDropAreaExited()
    }

    onExternalDropAreaDropped: drop => {
        importDropArea.externalDropAreaDropped(drop)
    }

    color: ui.theme.backgroundPrimaryColor

    clip: true

    enum State {
        Idle,
        DraggingItem
    }

    property int interactionState: TracksItemsView.State.Idle

    MouseHelper {
        id: mouseHelper
    }

    QtObject {
        id: prv

        property bool playRegionActivated: false
        readonly property int headerHeight: 20
        readonly property int listHeaderHeight: 2

        function cancelItemDragEdit() {
            if (mainMouseArea.pressed) {
                // This will lead to a cancel signal on `mainMouseArea` that will call back into this function,
                // but this time in released state.
                mouseHelper.callUngrabMouseOnItem(mainMouseArea)
                return
            }
            if (root.hoveredItemKey) {
                tracksItemsView.cancelItemDragEditRequested(root.hoveredItemKey)
            }
        }

        function setGuidelinePosition(time) {
            root.guidelinePos = timeline.context.timeToPosition(time)
        }

        function updateGuidelineVisibility() {
            root.guidelineVisible = root.guidelinePos >= 0
        }
    }

    PlaybackStateModel {
        id: playbackState
    }

    PlayRegionModel {
        id: playRegionModel
    }

    LeadInRecordingIndicatorModel {
        id: leadInIndicator
    }

    ViewTracksListModel {
        id: tracksModel

        onTotalTracksHeightChanged: {
            let totalContentHeight = tracksModel.totalTracksHeight + tracksViewState.tracksVerticalScrollPadding
            timeline.context.onResizeFrameContentHeight(totalContentHeight)

            // GlideMix: the scroll is measured from the list's top (originY); the
            // lanes getting shorter (View > Track view) must not leave it past the
            // end or above the start, where only empty space shows.
            let maxOffset = Math.max(totalContentHeight - tracksItemsView.height, 0)
            let offset = tracksItemsView.contentY - tracksItemsView.originY
            if (offset > maxOffset || offset < 0) {
                tracksItemsView.contentY = Math.max(0, Math.min(offset, maxOffset)) + tracksItemsView.originY
            }
        }

        onEscapePressed: {
            prv.cancelItemDragEdit()
        }
    }

    ProjectPropertiesModel {
        id: project

        onCaptureThumbnail: function captureThumbnail() {
            // hide playCursor for the time grabbing image
            playCursor.visible = false
            content.grabToImage(function (result) {
                playCursor.visible = true
                project.onThumbnailCreated(result.image)
            })
        }
    }

    SelectionContextMenuModel {
        id: selectionContextMenuModel
    }

    ContextMenuLoader {
        id: selectionContextMenuLoader

        onHandleMenuItem: function (itemId) {
            selectionContextMenuModel.handleMenuItem(itemId)
        }
    }

    CanvasContextMenuModel {
        id: canvasContextMenuModel
    }

    ContextMenuLoader {
        id: canvasContextMenuLoader

        onHandleMenuItem: function (itemId) {
            canvasContextMenuModel.handleMenuItem(itemId)
        }
    }

    //! NOTE Sync with TracksPanel
    TracksViewStateModel {
        id: tracksViewState
        onTracksVerticalOffsetChanged: {
            tracksItemsView.contentY = tracksViewState.tracksVerticalOffset + tracksItemsView.originY
        }
    }

    PlayCursorController {
        id: playCursorController
        context: timeline.context
    }

    PlayPositionActionController {
        id: playPositionActionController
        context: timeline.context
    }

    PlayRegionController {
        id: playRegionController
        context: timeline.context
    }

    SplitToolController {
        id: splitToolController
        context: timeline.context

        clipHovered: root.itemHovered
        hoveredTrack: root.hoveredTrackId

        onActiveChanged: {
            if (active) {
                selectionViewController.cancelSpectrogramEdit()
                itemsSelection.visible = false
            }
        }
    }

    SelectionViewController {
        id: selectionViewController
        context: timeline.context
        resistancePx: prv.headerHeight
    }

    Component.onCompleted: {
        //! NOTE Models depend on geometry, so let's create a page first and then initialize the models
        Qt.callLater(root.init)

        playbackState.init()
        playRegionModel.init()
        leadInIndicator.init()
        selectionViewController.load()
        selectionContextMenuModel.load()
        canvasContextMenuModel.load()

        splitToolController.init(root)
    }

    function init() {
        timeline.init()
        playRegionController.init()
        playCursorController.init()
        playPositionActionController.init()
        tracksViewState.init()
        project.init();

        //! NOTE Loading tracks, or rather clips, is the most havy operation.
        // Let's make sure that everything is loaded and initialized before this,
        // to avoid double loading at the beginning, when some parameters are initialized.
        Qt.callLater(tracksModel.load);

        //! NOTE setting verticalY has to be done after tracks are loaded,
        // otherwise project always starts at the very top
        Qt.callLater(() => tracksItemsView.contentY = tracksViewState.tracksVerticalOffset + tracksItemsView.originY)
    }

    Rectangle {
        id: canvasIndent
        anchors.top: timelineHeader.bottom
        anchors.bottom: parent.bottom
        height: timelineHeader.height
        width: content.anchors.leftMargin
        color: ui.theme.backgroundQuarternaryColor
    }

    MixHouseTempoLaneModel {
        id: tempoLaneModel
    }

    Item {
        id: timelineHeader

        // GlideMix: the TEMPO lane sits under the ruler when shown.
        // GlideMix: the same field shows the mix's energy (ENERGY button).
        readonly property bool tempoFieldVisible: tempoLaneModel.laneVisible || tempoLaneModel.energyLaneVisible
        readonly property int tempoLaneHeight: tempoFieldVisible ? tempoLaneModel.laneHeight : 0

        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: timeline.height + tempoLaneHeight
        z: content.z + 1 // to prevent clips overlapping timeline ruler

        clip: true

        Rectangle {
            id: timelineIndent

            anchors.top: parent.top
            anchors.left: parent.left

            height: timeline.height
            width: content.anchors.leftMargin

            color: timeline.color

            SeparatorLine {
                id: topBorder

                anchors.bottom: parent.bottom

                width: parent.width

                color: ui.theme.strokeColor
            }
        }

        MouseArea {
            id: timelineMouseArea
            anchors.fill: parent
            hoverEnabled: true

            property bool playRegionActivated: false

            function convertToTimelinePosition(e) {
                let position = mapToItem(timeline, Qt.point(e.x, e.y))
                position.x = Math.max(0, position.x)
                position.y = Math.max(0, position.y)
                return position
            }

            onPositionChanged: function (e) {
                let position = convertToTimelinePosition(e)
                timeline.updateCursorPosition(position.x, position.y)
                playRegionController.updatePosition(position.x)
            }

            onPressed: function (e) {
                let position = convertToTimelinePosition(e)
                playRegionActivated = false
                if (timeline.isMajorSection(position.y)) {
                    playRegionController.startInteraction(position.x, root.ctrlPressed)
                }
            }

            onReleased: function (e) {
                let position = convertToTimelinePosition(e)
                // GlideMix: only a real loop drag (or a click on the loop) keeps
                // the click from moving the play position.
                playRegionActivated = playRegionController.finishInteraction(position.x)
            }

            onClicked: function (e) {
                let position = convertToTimelinePosition(e)
                if (!playRegionActivated) {
                    let time = timeline.context.positionToTime(position.x)
                    playCursorController.seekToTime(time, playbackState.isPlaying || timeline.context.playbackOnRulerClickEnabled)
                    playCursorController.setPlaybackRegionByTime(time, time)
                }
                playRegionActivated = false
            }

            Connections {
                target: root

                function onCtrlPressedChanged() {
                    if (!root.ctrlPressed) {
                        playRegionController.finishInteraction(timelineMouseArea.mouseX)
                    }
                }
            }
        }

        Timeline {
            id: timeline

            anchors.top: parent.top
            anchors.left: timelineIndent.right
            anchors.right: verticalRulerPanelHeader.left

            height: 40

            Timer {
                id: playCursorReleaseTimer
                interval: 100
                repeat: false
                onTriggered: {
                    head.dragActive = false
                    timeline.displayedPlayCursorX = playCursorController.positionX
                }
            }

            property double displayedPlayCursorX: playCursorController.positionX

            function updateCursorPosition(x, y) {
                const snappedTime = timeline.context.applyDetectedSnap(timeline.context.positionToTime(x))
                lineCursor.x = timeline.context.timeToPosition(snappedTime)
                timeline.context.updateMousePositionTime(x)
                tracksViewState.setMouseY(Math.max(0, Math.min(y, mainMouseArea.height)))
            }

            Connections {
                target: playCursorController

                function onPositionXChanged() {
                    if (head.dragActive) {
                        return
                    }

                    timeline.displayedPlayCursorX = playCursorController.positionX
                }
            }

            Rectangle {
                id: lineCursor

                y: parent.top
                height: timeline.height
                width: 1

                color: ui.theme.fontPrimaryColor
            }

            Rectangle {
                id: timelineSelRect

                x: timeline.context.singleItemSelected ? timeline.context.selectedItemStartPosition : timeline.context.selectionStartPosition
                width: timeline.context.singleItemSelected ? timeline.context.selectedItemEndPosition - x : timeline.context.selectionEndPosition - x

                anchors.top: parent.verticalCenter
                anchors.bottom: parent.bottom

                color: ui.theme.extra["selection_highlight_color"]
                opacity: 0.3
            }

            // GlideMix: the performance playhead (what the audience hears):
            // a red head in the ruler and its line down to the tracks.
            Rectangle {
                x: performanceOverlay.redX - 1
                y: 24 + 14
                width: 2
                height: parent.height - y
                color: "#FF2A2A"
                visible: performanceOverlay.visible && x >= -2 && x <= parent.width
            }

            PlayCursorHead {
                id: performanceHead
                anchors.top: parent.top
                anchors.topMargin: 24
                x: performanceOverlay.redX - (width / 2)
                fillColor: "#FF2A2A"
                visible: performanceOverlay.visible && x >= -width && x <= parent.width
                z: 1
            }

            PlayCursorHead {
                id: head

                anchors.top: parent.top
                anchors.topMargin: 24

                property bool dragActive: false
                property double dragPositionX: timeline.displayedPlayCursorX

                x: timeline.displayedPlayCursorX - (width / 2)

                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: pressed || timelineMouseArea.pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor

                    onPressed: function (e) {
                        head.dragActive = true
                        head.dragPositionX = mapToItem(timeline, e.x, e.y).x
                        timeline.displayedPlayCursorX = head.dragPositionX
                    }

                    onPositionChanged: function (e) {
                        var ix = mapToItem(timeline, e.x, e.y).x
                        if (pressed) {
                            head.dragPositionX = ix
                            timeline.displayedPlayCursorX = ix
                            playCursorController.seekToTime(timeline.context.positionToTime(ix))
                        }
                        timeline.updateCursorPosition(ix, 0)
                    }

                    onReleased: function (e) {
                        var ix = mapToItem(timeline, e.x, e.y).x
                        let ixTime = timeline.context.positionToTime(ix)
                        playCursorController.seekToTime(ixTime)
                        if (!timelineMouseArea.playRegionActivated) {
                            playCursorController.seekToTime(ixTime, playbackState.isPlaying || timeline.context.playbackOnRulerClickEnabled)
                            playCursorController.setPlaybackRegionByTime(ixTime, ixTime)
                        }
                        playCursorReleaseTimer.restart()
                    }

                    onCanceled: {
                        playCursorReleaseTimer.stop()
                        head.dragActive = false
                        timeline.displayedPlayCursorX = playCursorController.positionX
                    }
                }
            }
        }

        // GlideMix: the master tempo line (IMixHouseTempoMap), edited the
        // way the parked MixHouse DJ app's tempo lane was.
        Rectangle {
            id: tempoLaneIndent

            anchors.top: timeline.bottom
            anchors.left: parent.left
            width: content.anchors.leftMargin
            height: timelineHeader.tempoLaneHeight
            visible: timelineHeader.tempoFieldVisible
            color: tempoLane.backgroundColor
        }

        MixHouseTempoLane {
            id: tempoLane

            anchors.top: timeline.bottom
            anchors.left: timelineIndent.right
            anchors.right: verticalRulerPanelHeader.left
            height: timelineHeader.tempoLaneHeight
            visible: timelineHeader.tempoFieldVisible
            showTempo: tempoLaneModel.laneVisible
            showEnergy: tempoLaneModel.energyLaneVisible

            frameStartTime: timeline.context.frameStartTime
            zoom: timeline.context.zoom
            backgroundColor: ui.theme.backgroundSecondaryColor
            pointColor: ui.theme.accentColor
            labelColor: ui.theme.fontPrimaryColor

            // GlideMix: right-click on a point > Delete point.
            ContextMenuLoader {
                id: tempoPointMenu
                property int node: -1
                onHandleMenuItem: function(itemId) {
                    if (itemId === "delete-point") {
                        tempoLane.deletePoint(node)
                    } else if (itemId === "glide-straight") {
                        tempoLane.setGlideShape(node, false)
                    } else if (itemId === "glide-s-curve") {
                        tempoLane.setGlideShape(node, true)
                    }
                }
                onClosed: tempoLane.clearPointHighlight()
            }
            onPointMenuRequested: function(node, x, y) {
                tempoPointMenu.node = node
                let items = []
                // GlideMix: how the tempo glides on to the next point.
                if (tempoLane.hasNextPoint(node)) {
                    const sCurve = tempoLane.isSCurve(node)
                    items.push({ id: "glide-straight", title: qsTrc("projectscene", "Glide: straight"),
                                 checkable: true, checked: !sCurve })
                    items.push({ id: "glide-s-curve", title: qsTrc("projectscene", "Glide: S-curve (smooth start and end)"),
                                 checkable: true, checked: sCurve })
                    items.push({})
                }
                items.push({ id: "delete-point", title: qsTrc("projectscene", "Delete point") })
                tempoPointMenu.show(Qt.point(x, y), items)
            }

            Rectangle {
                x: timeline.displayedPlayCursorX
                width: 1
                height: parent.height
                color: ui.theme.fontPrimaryColor
                opacity: 0.5
                visible: x >= 0 && x <= parent.width
            }

            SeparatorLine {
                anchors.bottom: parent.bottom
                width: parent.width
                color: ui.theme.strokeColor
            }
        }

        Rectangle {
            id: verticalRulerPanelHeader

            anchors.top: parent.top
            anchors.right: parent.right
            height: timelineHeader.height
            width: tracksModel.isVerticalRulersVisible ? tracksModel.verticalRulerWidth : 0

            color: ui.theme.backgroundSecondaryColor

            SeparatorLine {
                anchors.left: parent.left
                anchors.bottom: parent.bottom

                width: parent.width

                color: ui.theme.strokeColor
            }

            SeparatorLine {
                anchors.top: parent.top
                anchors.left: parent.left

                orientation: Qt.Vertical

                height: parent.height

                color: ui.theme.strokeColor
            }
        }
    }

    CustomCursor {
        id: customCursor

        readonly property string pencilShape: ":/images/customCursorShapes/Pencil.png"
        readonly property string smoothShape: ":/images/customCursorShapes/Smooth.png"
        readonly property string leftTrimShape: ":/images/customCursorShapes/ClipTrimLeft.png"
        readonly property string rightTrimShape: ":/images/customCursorShapes/ClipTrimRight.png"
        readonly property string leftStretchShape: ":/images/customCursorShapes/ClipStretchLeft.png"
        readonly property string rightStretchShape: ":/images/customCursorShapes/ClipStretchRight.png"

        active: {
            // Don't show custom cursor during playback for sample editing
            if ((content.isNearSample || content.isIsolationMode) && playbackState.isPlaying) {
                return false
            }

            return (content.isIsolationMode || content.isNearSample || content.leftTrimContainsMouse || content.rightTrimContainsMouse || content.leftTrimPressedButtons || content.rightTrimPressedButtons || (content.isBrush && root.itemHovered))
        }
        source: {
            if (content.isBrush) {
                return smoothShape
            }

            if (content.isNearSample || content.isIsolationMode) {
                return pencilShape
            }

            var isLeft = content.leftTrimContainsMouse || content.leftTrimPressedButtons
            if (root.altPressed) {
                return isLeft ? leftStretchShape : rightStretchShape
            }
            return isLeft ? leftTrimShape : rightTrimShape
        }
        size: content.isIsolationMode || (!content.isBrush && content.isNearSample) ? 36 : 26
    }

    Rectangle {
        id: content
        objectName: "ItemsView"
        // GlideMix: room before time 0 for a clip's edge handles (trim,
        // stretch) -- twice Audacity's 12 px, about 5 mm (a deliberate choice).
        anchors.leftMargin: 24
        anchors.top: timelineHeader.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right

        property bool isBrush: false
        property bool isIsolationMode: false
        property bool isNearSample: false
        property bool leftTrimContainsMouse: false
        property bool rightTrimContainsMouse: false
        property bool leftTrimPressedButtons: false
        property bool rightTrimPressedButtons: false

        GridLines {
            timelineRuler: timeline.ruler
            anchors.fill: parent
        }

        MouseArea {
            id: mainMouseArea
            anchors.fill: parent

            preventStealing: true
            acceptedButtons: Qt.LeftButton | Qt.RightButton

            Component.onCompleted: CustomCursorProvider.setCursorShape(mainMouseArea, ":/images/customCursorShapes/IBeamCursor.png", 26)

            hoverEnabled: true

            property var lastItemClickKey: null
            property bool itemWasMoved: false
            property point pressStartPosition: Qt.point(0, 0)
            readonly property int moveThreshold: 5

            onWheel: function (wheelEvent) {
                timeline.onWheel(wheelEvent.x, wheelEvent.pixelDelta, wheelEvent.angleDelta)
            }

            onPressed: function (e) {
                if (root.altPressed) {
                    return
                }

                if (e.button === Qt.LeftButton) {
                    if (root.itemHeaderHovered && !(splitToolController.active && splitToolController.hoveredTrackSplittable)) {
                        tracksItemsView.itemStartEditRequested(hoveredItemKey)
                        root.interactionState = TracksItemsView.State.DraggingItem
                        lastItemClickKey = root.hoveredItemKey
                    } else {
                        content.forceActiveFocus()

                        const pressBelongsToSplitTool = splitToolController.active && splitToolController.trackSplittable(tracksViewState.trackAtPosition(e.x, e.y))

                        if (!((e.modifiers & (Qt.ControlModifier | Qt.ShiftModifier)) || pressBelongsToSplitTool)) {
                            // Don't move the playhead yet: the seek is deferred to the release
                            // and dropped if this press turns into a drag
                            playCursorController.beginSeekGesture(timeline.context.positionToTime(e.x), e.x, e.y)
                        }

                        if (!pressBelongsToSplitTool) {
                            const spectrogramHit = tracksItemsView.getSpectrogramHit(e.y)
                            selectionViewController.onPressed(timeline.context.positionToTime(e.x), e.y, spectrogramHit)
                            selectionViewController.resetSelectedItems()
                            itemsSelection.visible = true
                        }
                        snapGuidelineToPosition(e.x)

                        splitToolController.mouseDown(e.x)
                        lastItemClickKey = null
                    }

                    itemWasMoved = false
                    pressStartPosition = Qt.point(e.x, e.y)
                } else if (e.button === Qt.RightButton) {
                    if (tracksHovered)
                    //! TODO AU4: handle context menu over empty track area
                    {} else {
                        canvasContextMenuLoader.show(Qt.point(e.x + timelineIndent.width, e.y + timelineHeader.height), canvasContextMenuModel.items)
                    }
                }
            }

            onPositionChanged: function (e) {
                timeline.updateCursorPosition(e.x, e.y)
                splitToolController.mouseMove(e.x)
                playCursorController.updateSeekGesture(e.x, e.y)

                if (root.interactionState === TracksItemsView.State.DraggingItem && !itemWasMoved) {
                    var dx = Math.abs(e.x - pressStartPosition.x)
                    var dy = Math.abs(e.y - pressStartPosition.y)
                    if (dx > moveThreshold || dy > moveThreshold) {
                        itemWasMoved = true
                    }
                }

                if (root.interactionState === TracksItemsView.State.DraggingItem && itemWasMoved) {
                    tracksItemsView.itemMoveRequested(hoveredItemKey, false)
                    tracksItemsView.startAutoScroll()
                } else {
                    selectionViewController.onPositionChanged(timeline.context.positionToTime(e.x), e.y)
                    let trackId = tracksViewState.trackAtPosition(e.x, e.y)

                    snapGuidelineToPosition(e.x)
                }
            }

            onReleased: function (e) {
                if (e.button !== Qt.LeftButton) {
                    return
                }

                if (!itemWasMoved) {
                    tracksItemsView.itemReleaseRequested(hoveredItemKey)
                    itemWasMoved = false
                }

                if (root.interactionState === TracksItemsView.State.DraggingItem) {
                    root.interactionState = TracksItemsView.State.Idle
                    if (itemWasMoved) {
                        tracksItemsView.itemMoveRequested(hoveredItemKey, true)
                        tracksItemsView.stopAutoScroll()
                    }
                    tracksItemsView.itemEndEditRequested(hoveredItemKey)
                } else {
                    splitToolController.mouseUp(e.x)

                    if (selectionViewController.selectionInProgress) {
                        let releaseTime = timeline.context.positionToTime(e.x)
                        selectionViewController.onReleased(releaseTime, e.y);

                        // Deferred from the press. Only a plain click repositions the playhead
                        // and the next playback start; a drag selection leaves both alone —
                        // playback derives its region from the selection when it starts.
                        if (playCursorController.endSeekGesture()) {
                            playCursorController.setPlaybackRegionByTime(timeline.context.selectionStartTime, timeline.context.selectionEndTime)
                        }
                    } else {
                        playCursorController.cancelSeekGesture()
                    }

                    itemsSelection.visible = false
                    hideGuideline()
                }
            }

            onCanceled: e => {
                root.interactionState = TracksItemsView.State.Idle
                playCursorController.cancelSeekGesture()
                prv.cancelItemDragEdit()
            }

            onClicked: e => {
                if (e.button !== Qt.LeftButton) {
                    return
                }

                if (!root.itemHovered && !itemWasMoved) {
                    selectionViewController.resetSelectedItems()
                }
            }

            onDoubleClicked: e => {
                if (e.button !== Qt.LeftButton) {
                    return
                }

                if (root.isSplitMode && splitToolController.trackSplittable(tracksViewState.trackAtPosition(e.x, e.y))) {
                    return
                }

                if (root.itemHovered) {
                    selectionViewController.selectItemData(root.hoveredItemKey)
                    playCursorController.animatedSeekToTime(timeline.context.selectedItemStartTime)
                    playCursorController.setPlaybackRegionByTime(timeline.context.selectedItemStartTime, timeline.context.selectedItemEndTime)
                } else {
                    selectionViewController.selectTrackAudioData(e.y)
                    playCursorController.animatedSeekToTime(timeline.context.selectionStartTime)
                    playCursorController.setPlaybackRegionByTime(timeline.context.selectionStartTime, timeline.context.selectionEndTime)
                }
                itemsSelection.visible = false
            }
        }

        // Drives the snap guideline while hovering empty project space below the last track.
        HoverHandler {
            id: emptyAreaGuidelineHandler

            enabled: root.interactionState !== TracksItemsView.State.DraggingItem

            function processHover() {
                let pos = point.position
                timeline.updateCursorPosition(pos.x, pos.y)

                if (tracksViewState.trackAtPosition(pos.x, pos.y) !== -1) {
                    // Over a track: let the track container own the guideline.
                    return
                }

                root.snapGuidelineToPosition(pos.x)
            }

            onPointChanged: {
                if (hovered) {
                    processHover()
                }
            }

            onHoveredChanged: {
                if (!hovered) {
                    root.hideGuideline()
                }
            }
        }

        Connections {
            target: timeline.context

            function onFrameTimeChanged() {
                if (emptyAreaGuidelineHandler.hovered && !mainMouseArea.pressed && !tracksViewState.isItemEditInProgress()) {
                    emptyAreaGuidelineHandler.processHover()
                }
            }
        }

        StyledViewScrollAndZoomArea {
            id: tracksItemsViewArea

            anchors.fill: parent
            anchors.rightMargin: tracksModel.isVerticalRulersVisible ? verticalRulerPanelHeader.width : 0

            view: tracksItemsView

            horizontalScrollbarSize: timeline.context.horizontalScrollbarSize
            startHorizontalScrollPosition: timeline.context.startHorizontalScrollPosition

            verticalScrollbarSize: timeline.context.verticalScrollbarSize
            startVerticalScrollPosition: timeline.context.startVerticalScrollPosition

            scrollbarThickness: 12
            scrollbarAlwaysVisibleWhenNeeded: true
            scrollbarOpacityNormal: 0.5
            scrollbarOpacityPressed: 0.8

            StyledListView {
                id: tracksItemsView

                anchors.fill: parent
                clip: false // do not clip so clip handles are visible

                property bool mouseMoveActive: false

                onMouseMoveActiveChanged: {
                    if (mouseMoveActive) {
                        CustomCursorProvider.overrideStandardCursor(Qt.ClosedHandCursor)
                    } else {
                        CustomCursorProvider.restoreCursor()
                    }
                }

                ScrollBar.horizontal: null
                ScrollBar.vertical: null

                property real lockedVerticalScrollPosition
                property bool verticalScrollLocked: tracksViewState.tracksVerticalScrollLocked

                // GlideMix: every lane stays built, so the list knows its real
                // height; estimated from the lanes in view, the lanes slid away
                // from the headers with 30+ songs after View > Track view.
                cacheBuffer: Math.max(3000, tracksModel.totalTracksHeight)

                // The list moves its origin when lanes above the view change
                // height. The scroll position is kept from the top of the list,
                // so it (and the track headers, which sync to it) stay put.
                onOriginYChanged: {
                    if (!verticalScrollLocked) {
                        contentY = tracksViewState.tracksVerticalOffset + originY
                    }
                }

                function checkIfAnyTrack(f) {
                    for (let i = 0; i < tracksItemsView.count; i++) {
                        let trackLoader = tracksItemsView.itemAtIndex(i)
                        if (trackLoader && trackLoader.item) {
                            if (f(trackLoader.item)) {
                                return true
                            }
                        }
                    }
                    return false
                }

                function getSpectrogramHit(y) {
                    var spectrogramHit = null
                    checkIfAnyTrack(function (trackItem) {
                        if (trackItem.getSpectrogramHit) {
                            spectrogramHit = trackItem.getSpectrogramHit(y)
                            if (spectrogramHit) {
                                return true
                            }
                        }
                        return false
                    })
                    if (spectrogramHit) {
                        return spectrogramHit
                    } else {
                        return SpectrogramHitFactory.createNullSpectrogramHit()
                    }
                }

                function insureVerticallyVisible(item) {
                    var itemViewY = item.mapToItem(tracksItemsView.contentItem, Qt.point(0, 0)).y
                    tracksViewState.insureVerticallyVisible(tracksItemsView.contentY - tracksItemsView.originY, tracksItemsView.height, itemViewY - tracksItemsView.originY, item.height)
                }

                signal itemMoveRequested(var itemKey, bool completed)
                signal itemStartEditRequested(var itemKey)
                signal itemEndEditRequested(var itemKey)
                signal itemReleaseRequested(var itemKey)
                signal cancelItemDragEditRequested(var itemKey)
                signal startAutoScroll
                signal stopAutoScroll

                signal previewImportClipRequested(var trackIds, real startPos, var durations, var titles)
                signal clearPreviewImportClip(var excludeTrackIds)

                header: Rectangle {
                    height: prv.listHeaderHeight
                    width: parent.width
                    color: "transparent"
                }

                footer: Item {
                    height: tracksViewState.tracksVerticalScrollPadding
                }

                onVerticalScrollLockedChanged: {
                    lockedVerticalScrollPosition = contentY
                }

                onContentYChanged: {
                    if (verticalScrollLocked) {
                        contentY = lockedVerticalScrollPosition
                    } else {
                        tracksViewState.changeTracksVerticalOffset(tracksItemsView.contentY - tracksItemsView.originY)
                        timeline.context.startVerticalScrollPosition = tracksItemsView.contentY
                    }
                }

                onHeightChanged: {
                    timeline.context.onResizeFrameHeight(tracksItemsView.height)
                }

                Connections {
                    target: timeline.context

                    function onViewContentYChangeRequested(delta) {
                        let totalContentHeight = tracksModel.totalTracksHeight + tracksViewState.tracksVerticalScrollPadding
                        let canMove = totalContentHeight > tracksItemsView.height
                        if (!canMove) {
                            return
                        }

                        // From the list's top: a moved origin must not stop the
                        // scroll short or push it past either end.
                        let origin = tracksItemsView.originY
                        let contentYOffset = tracksItemsView.contentY - origin + delta

                        let maxContentY = totalContentHeight - tracksItemsView.height
                        contentYOffset = Math.max(Math.min(contentYOffset, maxContentY), 0)

                        tracksItemsView.contentY = contentYOffset + origin
                    }
                }

                interactive: false

                model: tracksModel

                // GlideMix: a lane added (a dropped or loaded song, a song moved
                // to a new row) scrolls into view; lanes 4, 5, 6... were made
                // below the visible ones and looked like nothing happened.
                Connections {
                    target: tracksModel
                    function onRowsInserted(parent, first, last) {
                        // Not the empty lane a library drag makes under the
                        // last one: scrolling to it moved the lanes under the
                        // cursor, which removed it again, and the view jumped
                        // back and forth. The drop reveals its own lane.
                        if (importDropArea.containsDrag) {
                            return
                        }
                        tracksItemsView.revealTrack(tracksModel.trackIdAt(last))
                    }
                }

                function revealTrack(trackId) {
                    if (trackId < 0) {
                        return
                    }
                    revealLaneTimer.trackId = trackId
                    revealLaneTimer.tries = 0
                    revealLaneTimer.restart()
                }

                // After the import has settled (it locks and restores the
                // vertical scroll), so the scroll is not undone -- but in the
                // same frame as the drop's zoom: a quarter second later it
                // showed as a second jump, a blink.
                Timer {
                    id: revealLaneTimer
                    property int trackId: -1
                    property int tries: 0
                    interval: tries === 0 ? 0 : 30
                    onTriggered: {
                        if (tracksItemsView.verticalScrollLocked && tries < 100) {
                            tries++
                            restart()
                            return
                        }
                        // Lane top relative to the top of the visible lanes.
                        const top = tracksViewState.trackVerticalPosition(trackId)
                        const height = tracksViewState.trackHeight(trackId)
                        const visible = tracksItemsView.height - prv.listHeaderHeight
                        if (top < 0 || height <= 0) {
                            if (top < 0 && height > 0) {
                                tracksItemsView.contentY += top
                            }
                            return
                        }
                        // The last lane keeps a lane's worth of empty space in
                        // view below it: that's where the next song is dropped.
                        const isLast = tracksModel.trackIdAt(tracksModel.rowCount() - 1) === trackId
                        const room = isLast ? Math.min(height, tracksViewState.tracksVerticalScrollPadding) : 0
                        if (top + height + room > visible) {
                            tracksItemsView.contentY += Math.min(top, top + height + room - visible)
                        }
                    }
                }

                delegate: Loader {
                    id: trackItemLoader

                    property var itemData: model
                    property int index: model.index

                    width: tracksItemsView.width

                    sourceComponent: trackType === TrackType.LABEL ? trackLabelsContainer : trackClipsContainerComp

                    onLoaded: {
                        trackItemLoader.item.init()
                    }

                    Component {
                        id: trackClipsContainerComp

                        TrackClipsContainer {
                            id: trackClipsContainer

                            property var itemData: trackItemLoader.itemData
                            property int index: trackItemLoader.index

                            width: trackItemLoader.width

                            context: timeline.context

                            container: tracksItemsView
                            canvas: content

                            trackId: itemData.trackId
                            trackTitle: itemData.trackTitle
                            trackColor: itemData.color
                            headerHeight: prv.headerHeight
                            sampleRate: itemData.trackSampleRate

                            isLeadInRecordingTrack: leadInIndicator.visible && leadInIndicator.trackIds.indexOf(itemData.trackId) !== -1
                            leadInRecordingStartTime: leadInIndicator.startTime

                            isDataSelected: itemData.isDataSelected
                            isTrackSelected: itemData.isTrackSelected
                            isTrackFocused: itemData.isTrackFocused
                            blackCanvas: tracksViewState.blackCanvas
                            isMultiSelectionActive: itemData.isMultiSelectionActive
                            isTrackAudible: itemData.isTrackAudible
                            dbRange: itemData.dbRange
                            isAutomationEnabled: itemData.isAutomationEnabled
                            isWaveformViewVisible: itemData.isWaveformViewVisible
                            isSpectrogramViewVisible: itemData.isSpectrogramViewVisible

                            moveActive: tracksItemsView.mouseMoveActive || tracksViewState.keyboardMoveActive

                            altPressed: root.altPressed
                            ctrlPressed: root.ctrlPressed

                            selectionInProgress: selectionViewController.selectionInProgress
                            selectionEditInProgress: selectionViewController.selectionEditInProgress
                            verticalSelectionEditInProgress: selectionViewController.verticalSelectionEditInProgress

                            onHoverChanged: function () {
                                root.itemHovered = tracksItemsView.checkIfAnyTrack(function (trackItem) {
                                    return trackItem && trackItem.hover
                                })
                            }

                            selectionStartFrequency: itemData.frequencySelection.startFrequency
                            selectionEndFrequency: itemData.frequencySelection.endFrequency
                            pressedSpectrogram: selectionViewController.pressedSpectrogram
                            spectralSelectionEnabled: selectionViewController.spectralSelectionEnabled
                            selectionController: selectionViewController
                            splitToolActive: splitToolController.active

                            navigationPanel: navPanels && navPanels[index] ? navPanels[index] : null

                            onTrackMousePositionChanged: function (xWithinTrack, yWithinTrack) {
                                let xGlobalPosition = xWithinTrack
                                let yGlobalPosition = y + yWithinTrack - tracksItemsView.contentY

                                timeline.updateCursorPosition(xGlobalPosition, yGlobalPosition)
                            }

                            onTrackItemMousePositionChanged: function (xWithinTrack, yWithinTrack, itemKey) {
                                let xGlobalPosition = xWithinTrack
                                let yGlobalPosition = y + yWithinTrack - tracksItemsView.contentY

                                timeline.updateCursorPosition(xGlobalPosition, yGlobalPosition)

                                root.hoveredTrackId = trackId
                                root.hoveredItemKey = itemKey
                                root.hoveredTrackHeight = tracksViewState.trackHeight(trackId)
                                root.hoveredTrackVerticalPosition = tracksViewState.trackVerticalPosition(trackId)

                                splitToolController.mouseMove(xWithinTrack)
                            }

                            onSetHoveredItemKey: function (itemKey) {
                                root.hoveredItemKey = itemKey
                            }

                            onItemHeaderHoveredChanged: function (val) {
                                root.itemHeaderHovered = val
                            }

                            onItemSelectedRequested: {
                                selectionViewController.resetDataSelection()
                                itemsSelection.visible = false
                            }

                            onSelectionResetRequested: {
                                selectionViewController.resetDataSelection()
                            }

                            onUpdateMouseMoveActive: function (completed) {
                                if (tracksItemsView.mouseMoveActive !== completed) {
                                    return
                                }
                                tracksItemsView.mouseMoveActive = !completed
                            }

                            onRequestSelectionContextMenu: function (x, y) {
                                let mapped = trackClipsContainer.mapToItem(tracksItemsView, x, y)
                                selectionContextMenuLoader.show(Qt.point(mapped.x + canvasIndent.width, mapped.y + timelineHeader.height), selectionContextMenuModel.items)
                            }

                            onSelectionResize: function (x1, x2, completed) {
                                selectionViewController.onSelectionHorizontalResize(timeline.context.positionToTime(x1), timeline.context.positionToTime(x2), completed)
                            }

                            onInsureVerticallyVisible: function () {
                                tracksItemsView.insureVerticallyVisible(this)
                            }

                            onInteractionStarted: {
                                tracksViewState.requestVerticalScrollLock()
                            }

                            onInteractionEnded: {
                                tracksViewState.requestVerticalScrollUnlock()
                            }

                            onItemDragEditCanceled: {
                                root.hoveredItemKey = null
                                root.itemHeaderHovered = false
                                tracksItemsView.mouseMoveActive = false
                                timeline.context.updateSelectedItemTime()
                            }

                            onIsBrushChanged: function () {
                                content.isBrush = tracksItemsView.checkIfAnyTrack(function (track) {
                                    return track && track.isBrush
                                })
                            }

                            onIsIsolationModeChanged: function () {
                                content.isIsolationMode = tracksItemsView.checkIfAnyTrack(function (track) {
                                    return track && track.isIsolationMode
                                })
                            }

                            onIsNearSampleChanged: function () {
                                content.isNearSample = tracksItemsView.checkIfAnyTrack(function (track) {
                                    return track && track.isNearSample
                                })
                            }

                            onLeftTrimContainsMouseChanged: function () {
                                content.leftTrimContainsMouse = tracksItemsView.checkIfAnyTrack(function (track) {
                                    return track && track.leftTrimContainsMouse
                                })
                            }

                            onRightTrimContainsMouseChanged: function () {
                                content.rightTrimContainsMouse = tracksItemsView.checkIfAnyTrack(function (track) {
                                    return track && track.rightTrimContainsMouse
                                })
                            }

                            onLeftTrimPressedButtonsChanged: function () {
                                content.leftTrimPressedButtons = tracksItemsView.checkIfAnyTrack(function (track) {
                                    return track && track.leftTrimPressedButtons
                                })
                            }

                            onRightTrimPressedButtonsChanged: function () {
                                content.rightTrimPressedButtons = tracksItemsView.checkIfAnyTrack(function (track) {
                                    return track && track.rightTrimPressedButtons
                                })
                            }

                            onUpdateItemGuideline: function (time) {
                                root.updateGuidelineAtTime(time)
                            }

                            onHandleTimeGuideline: function (x) {
                                root.snapGuidelineToPosition(x)
                            }

                            Connections {
                                target: tracksItemsView

                                function onPreviewImportClipRequested(tracksIds, startPos, durations, titles) {
                                    for (let i = 0; i < tracksIds.length; i++) {
                                        if (tracksIds[i] == trackClipsContainer.trackId) {
                                            const startTime = timeline.context.positionToTime(startPos)
                                            const endPos = timeline.context.timeToPosition(startTime + durations[i])
                                            trackClipsContainer.movePreviewClip(startPos, endPos - startPos, titles[i])
                                        }
                                    }
                                }

                                function onClearPreviewImportClip(excludeTrackIds) {
                                    if (!excludeTrackIds.includes(trackClipsContainer.trackId)) {
                                        trackClipsContainer.clearPreviewClip()
                                    }
                                }
                            }
                        }
                    }

                    Component {
                        id: trackLabelsContainer

                        TrackLabelsContainer {
                            property var itemData: trackItemLoader.itemData
                            property int index: trackItemLoader.index

                            width: trackItemLoader.width

                            context: timeline.context
                            container: tracksItemsView
                            canvas: content
                            canvasIndentWidth: content.anchors.leftMargin

                            trackId: itemData.trackId
                            trackTitle: itemData.trackTitle
                            isDataSelected: itemData.isDataSelected
                            isTrackSelected: itemData.isTrackSelected
                            isTrackFocused: itemData.isTrackFocused
                            isMultiSelectionActive: itemData.isMultiSelectionActive
                            isTrackAudible: itemData.isTrackAudible

                            moveActive: tracksItemsView.mouseMoveActive || tracksViewState.keyboardMoveActive

                            altPressed: root.altPressed
                            ctrlPressed: root.ctrlPressed

                            selectionInProgress: selectionViewController.selectionInProgress
                            selectionEditInProgress: selectionViewController.selectionEditInProgress
                            verticalSelectionEditInProgress: selectionViewController.verticalSelectionEditInProgress

                            navigationPanel: navPanels && navPanels[index] ? navPanels[index] : null

                            onHoverChanged: function () {
                                root.itemHovered = tracksItemsView.checkIfAnyTrack(function (trackItem) {
                                    return trackItem && trackItem.hover
                                })
                            }

                            onTrackItemMousePositionChanged: function (xWithinTrack, yWithinTrack, itemKey) {
                                let xGlobalPosition = xWithinTrack
                                let yGlobalPosition = y + yWithinTrack - tracksItemsView.contentY

                                timeline.updateCursorPosition(xGlobalPosition, yGlobalPosition)

                                root.hoveredTrackId = trackId
                                root.hoveredItemKey = itemKey
                                root.hoveredTrackHeight = tracksViewState.trackHeight(trackId)
                                root.hoveredTrackVerticalPosition = tracksViewState.trackVerticalPosition(trackId)

                                splitToolController.mouseMove(xWithinTrack)
                            }

                            onSetHoveredItemKey: function (itemKey) {
                                root.hoveredItemKey = itemKey
                            }

                            onItemHeaderHoveredChanged: function (val) {
                                root.itemHeaderHovered = val
                            }

                            onInteractionStarted: {
                                tracksViewState.requestVerticalScrollLock()
                            }

                            onInteractionEnded: {
                                tracksViewState.requestVerticalScrollUnlock()
                            }

                            onItemDragEditCanceled: {
                                root.hoveredItemKey = null
                                root.itemHeaderHovered = false
                                tracksItemsView.mouseMoveActive = false
                            }

                            onSelectionResize: function (x1, x2, completed) {
                                selectionViewController.onSelectionHorizontalResize(timeline.context.positionToTime(x1), timeline.context.positionToTime(x2), completed)
                            }

                            onRequestSelectionContextMenu: function (x, y) {
                                let mapped = trackItemLoader.item.mapToItem(tracksItemsView, x, y)
                                selectionContextMenuLoader.show(Qt.point(mapped.x + canvasIndent.width, mapped.y + timelineHeader.height), selectionContextMenuModel.items)
                            }

                            onItemSelectedRequested: {
                                selectionViewController.resetDataSelection()
                                itemsSelection.visible = false
                            }

                            onSelectionResetRequested: {
                                selectionViewController.resetDataSelection()
                            }

                            onUpdateMouseMoveActive: function (completed) {
                                if (tracksItemsView.mouseMoveActive !== completed) {
                                    return
                                }
                                tracksItemsView.mouseMoveActive = !completed
                            }

                            onUpdateItemGuideline: function (time) {
                                root.updateGuidelineAtTime(time)
                            }

                            onHandleTimeGuideline: function (x) {
                                root.snapGuidelineToPosition(x)
                            }

                            onInsureVerticallyVisible: function () {
                                tracksItemsView.insureVerticallyVisible(this)
                            }
                        }
                    }
                }
            }

            onPinchToZoom: function (scale, pos) {
                timeline.context.pinchToZoom(scale, pos)
            }

            onScrollHorizontal: function (newPos) {
                timeline.context.scrollHorizontal(newPos)
            }

            onScrollVertical: function (newPos) {
                timeline.context.scrollVertical(newPos)
            }
        }

        Rectangle {
            id: itemsSelection

            anchors.top: parent.top
            anchors.bottom: parent.bottom
            color: ui.theme.extra["selection_highlight_color"]
            opacity: 0.05
            visible: false

            x: Math.max(timeline.context.selectionStartPosition, 0.0)
            width: timeline.context.selectionEndPosition - x
        }

        PlaybackSeekLine {
            anchors.top: tracksItemsViewArea.top
            anchors.bottom: parent.bottom

            x: timeline.context.lastPlaybackSeekPosition
            // The seek line marks "where playback will resume" — meaningless
            // while recording (the playhead is being driven by record position),
            // so hide it to avoid showing a stale marker at the pre-record cursor.
            visible: !playbackState.isRecording
        }

        // GlideMix: the performance (what the audience hears, MixMeister
        // Perform). Red playhead; what it has played is tinted and locked.
        MixHousePerformanceModel {
            id: performanceModel
        }

        Item {
            id: performanceOverlay
            anchors.top: tracksItemsViewArea.top
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            visible: performanceModel.active || performanceModel.position > 0
            readonly property real redX: {
                // re-evaluate on scroll and zoom
                timeline.context.frameStartTime; timeline.context.zoom
                return timeline.context.timeToPosition(performanceModel.position)
            }
            readonly property real startX: {
                timeline.context.frameStartTime; timeline.context.zoom
                return timeline.context.timeToPosition(0)
            }

            Rectangle {
                x: Math.max(0, performanceOverlay.startX)
                width: Math.max(0, Math.min(parent.width, performanceOverlay.redX) - x)
                height: parent.height
                // Dark red over what the audience has heard: it dims the played
                // part as well as tinting it (MixMeister's played region).
                color: "#3A0508"
                opacity: 0.55
            }

            Rectangle {
                x: performanceOverlay.redX - 1
                width: 2
                height: parent.height
                color: "#FF2A2A"
                visible: x >= -2 && x <= parent.width
            }
        }

        PlayCursorLine {
            id: playCursor

            anchors.top: tracksItemsViewArea.top
            anchors.bottom: parent.bottom

            x: timeline.displayedPlayCursorX
        }

        Rectangle {
            id: snapGuideline

            anchors.top: content.top
            anchors.bottom: content.bottom

            width: 1
            x: playRegionController.guidelineVisible ? playRegionController.guidelinePosition : (root.guidelineVisible ? root.guidelinePos : -1)

            color: tracksViewState.snapEnabled ? ui.theme.extra["guideline_snap_enabled_color"] : ui.theme.extra["guideline_snap_disabled_color"]

            visible: root.guidelineVisible || playRegionController.guidelineVisible
        }

        // GlideMix: BEATMATCH zones -- where a clip on one track overlaps a
        // clip on another (the parked MixHouse DJ app's orange zone and pill).
        // Only the pill is drawn; it applies a crossfade across the overlap.
        MixHouseTransitionZonesModel {
            id: transitionZonesModel
            // The lane shown on each transition's pill, by its two tracks: the
            // zone items are rebuilt on every edit, and would otherwise go
            // back to Volume each time.
            property var shownLanes: ({})
        }

        Repeater {
            model: transitionZonesModel

            delegate: Item {
                id: zoneItem

                // GlideMix: grey while the transition is switched off.
                readonly property color accent: model.disabled ? "#8a8a8a" : "#FFAE5A"

                function menuItems() {
                    return transitionZonesModel.presetMenu(model.index).concat([
                        {},
                        model.disabled
                            ? { id: "enable", title: qsTrc("mixhouse", "Turn transition on") }
                            : { id: "disable", title: qsTrc("mixhouse", "Turn transition off (right-click the pill)") },
                        { id: "remove", title: qsTrc("mixhouse", "Remove transition"), enabled: model.crossfaded }
                    ])
                }
                // Right-click on the pill: the transition off (grey, everything it
                // wrote removed) or back on. A left click opens its menu.
                function pillClicked(mouse) {
                    if (mouse.button === Qt.RightButton) {
                        transitionZonesModel.setTransitionEnabled(model.index, model.disabled)
                    } else {
                        zoneMenu.toggleOpened(menuItems())
                    }
                }
                // GlideMix: placed from the list's own contentY, the scroll the
                // clips are drawn with. The view state's saved offset can lag
                // it (after a vertical zoom it stayed at the old scroll, and
                // the pills floated above their transitions for good).
                // trackVerticalPosition() + tracksVerticalOffset is the track's
                // top in the whole list, whatever the scroll; tracksLayoutRevision
                // is read so it follows tracks changing height or place.
                readonly property int layout: tracksViewState.tracksLayoutRevision
                // contentY less originY: the list moves its origin when rows
                // above shrink (zooming the tracks back out), and the clips are
                // drawn from it.
                readonly property real listY: tracksItemsView.contentY - tracksItemsView.originY
                function contentTop(trackId) {
                    return layout * 0 + tracksViewState.trackVerticalPosition(trackId) + tracksViewState.tracksVerticalOffset
                }
                readonly property int outTop: contentTop(model.outgoingTrackId) - listY
                readonly property int inTop: contentTop(model.incomingTrackId) - listY
                readonly property int outBottom: outTop + layout * 0 + tracksViewState.trackHeight(model.outgoingTrackId)
                readonly property int inBottom: inTop + layout * 0 + tracksViewState.trackHeight(model.incomingTrackId)

                // Same mapping as TimelineContext::timeToPosition, written out
                // so the binding follows zoom and horizontal scroll.
                x: (model.startTime - timeline.context.frameStartTime) * timeline.context.zoom
                width: Math.max(1, (model.endTime - model.startTime) * timeline.context.zoom)
                y: Math.min(outTop, inTop)
                height: Math.max(outBottom, inBottom) - y
                visible: x + width > 0 && x < content.width

                // No shading or edge bars over the overlap: the IN and OUT
                // anchors already say where it starts and ends, and a wash of
                // orange over both waveforms only hid them. The pill stays --
                // it is the transition's menu, not decoration.

                // GlideMix (DropLab): the transition's lanes. A dot on the pill
                // picks one; its line is drawn over both clips across the
                // overlap, with a glow in its colour fading down under it.
                // -1 = none shown.
                readonly property string laneKey: model.outgoingTrackId + ":" + model.incomingTrackId
                // GlideMix: no lane shown until a dot is clicked -- the volume
                // curve's gradient used to cover every transition by default.
                property int lane: transitionZonesModel.shownLanes[laneKey] !== undefined
                                   ? transitionZonesModel.shownLanes[laneKey] : -1
                // By lane id: 0 volume, 1 low, 2 color, 3 mid, 4 high.
                readonly property var laneColors: ["#5CC8F0", "#F0506E", "#A07CF5", "#F5A04A", "#EDD85A"]
                readonly property var laneNames: [qsTrc("mixhouse", "Volume"), qsTrc("mixhouse", "Low"), qsTrc("mixhouse", "Color"),
                    qsTrc("mixhouse", "Mid"), qsTrc("mixhouse", "High")]
                // The dots' order on the pill.
                readonly property var laneOrder: [0, 1, 3, 4, 2]
                onLaneChanged: laneCanvas.reload()
                onOutTopChanged: laneCanvas.requestPaint()
                onInTopChanged: laneCanvas.requestPaint()
                onXChanged: laneCanvas.requestPaint()
                onWidthChanged: laneCanvas.requestPaint()

                Connections {
                    target: transitionZonesModel
                    function onLinesChanged() { if (!laneCanvas.drag) laneCanvas.reload() }
                }
                Component.onCompleted: laneCanvas.reload()

                Canvas {
                    id: laneCanvas

                    // Only the part of the zone on screen: a long overlap zoomed in
                    // would otherwise ask for a canvas far wider than the view.
                    x: Math.max(0, -zoneItem.x)
                    width: Math.max(1, Math.min(zoneItem.width, content.width - zoneItem.x) - x)
                    height: zoneItem.height
                    visible: zoneItem.lane >= 0 && width > 1
                    renderStrategy: Canvas.Cooperative

                    readonly property real clipHeaderHeight: 20
                    readonly property real inset: 4
                    // The lane's lines (laneLines), fetched when the lane or the
                    // clips change; edited in place while a point is dragged.
                    property var lineData: null
                    // A point being dragged: { side: "out"|"in", index }.
                    property var drag: null

                    function reload() {
                        lineData = zoneItem.lane >= 0 ? transitionZonesModel.laneLines(model.index, zoneItem.lane) : null
                        requestPaint()
                    }

                    function sideTop(side) {
                        return (side === "out" ? zoneItem.outTop : zoneItem.inTop) - zoneItem.y + clipHeaderHeight + inset
                    }
                    function sideBottom(side) {
                        return (side === "out" ? zoneItem.outBottom : zoneItem.inBottom) - zoneItem.y - inset
                    }
                    function px(t) { return (t - model.startTime) * timeline.context.zoom - laneCanvas.x }
                    function timeAt(x) { return model.startTime + (x + laneCanvas.x) / timeline.context.zoom }
                    function py(side, v) { return sideBottom(side) - v * (sideBottom(side) - sideTop(side)) }
                    function valueAt(side, y) {
                        return Math.max(0, Math.min(1, (sideBottom(side) - y) / (sideBottom(side) - sideTop(side))))
                    }
                    function sideAt(y) {
                        if (y >= sideTop("out") - inset && y <= sideBottom("out") + inset) return "out"
                        if (y >= sideTop("in") - inset && y <= sideBottom("in") + inset) return "in"
                        return ""
                    }

                    // The line as drawn: the zone's edge values and the points
                    // between them (while editing), else the sampled line.
                    function polyline(line) {
                        if (!drag && !editedSide) {
                            return line.samples
                        }
                        var s = line.samples
                        var result = [[s[0][0], s[0][1]]]
                        for (var i = 0; i < line.points.length; ++i) {
                            result.push(line.points[i])
                        }
                        result.push([s[s.length - 1][0], s[s.length - 1][1]])
                        return result
                    }
                    property string editedSide: ""

                    function drawLane(ctx, line, side, colour, centred) {
                        var top = sideTop(side)
                        var bottom = sideBottom(side)
                        if (!line || !line.samples || line.samples.length < 2 || bottom - top < 6) {
                            return
                        }
                        var poly = polyline(line)

                        // A faint wash of the lane's colour over the overlap.
                        ctx.fillStyle = Qt.rgba(colour.r, colour.g, colour.b, 0.07)
                        ctx.fillRect(0, top, width, bottom - top)

                        var highest = bottom
                        ctx.beginPath()
                        ctx.moveTo(px(poly[0][0]), py(side, poly[0][1]))
                        for (var i = 1; i < poly.length; ++i) {
                            var y = py(side, poly[i][1])
                            highest = Math.min(highest, y)
                            ctx.lineTo(px(poly[i][0]), y)
                        }
                        // The glow: filled down to the bottom (Color: to its
                        // centre line, where the filter is open), strong at the line.
                        var base = centred ? py(side, 0.5) : bottom
                        ctx.lineTo(px(poly[poly.length - 1][0]), base)
                        ctx.lineTo(px(poly[0][0]), base)
                        ctx.closePath()
                        var g = ctx.createLinearGradient(0, centred ? top : highest, 0, base)
                        g.addColorStop(0, Qt.rgba(colour.r, colour.g, colour.b, 0.55))
                        g.addColorStop(1, Qt.rgba(colour.r, colour.g, colour.b, 0.04))
                        ctx.fillStyle = g
                        ctx.fill()

                        ctx.beginPath()
                        ctx.moveTo(px(poly[0][0]), py(side, poly[0][1]))
                        for (var j = 1; j < poly.length; ++j) {
                            ctx.lineTo(px(poly[j][0]), py(side, poly[j][1]))
                        }
                        ctx.lineWidth = 2
                        ctx.strokeStyle = colour
                        ctx.stroke()

                        for (var k = 0; k < line.points.length; ++k) {
                            var active = drag && drag.side === side && drag.index === k
                            ctx.beginPath()
                            ctx.arc(px(line.points[k][0]), py(side, line.points[k][1]), active ? 5 : 3.5, 0, 2 * Math.PI)
                            ctx.fillStyle = active ? "#FFFFFF" : colour
                            ctx.fill()
                            ctx.lineWidth = 1.5
                            ctx.strokeStyle = "#101014"
                            ctx.stroke()
                        }
                    }

                    onPaint: {
                        var ctx = getContext("2d")
                        ctx.reset()
                        if (zoneItem.lane < 0 || !lineData) {
                            return
                        }
                        var colour = Qt.color(zoneItem.laneColors[zoneItem.lane])
                        var centred = zoneItem.lane === 2
                        drawLane(ctx, lineData.out, "out", colour, centred)
                        drawLane(ctx, lineData.in, "in", colour, centred)
                    }

                    // The nearest point to (x, y) within 8 px, as { side, index }.
                    function pointAt(x, y) {
                        var side = sideAt(y)
                        if (!side || !lineData) return null
                        var points = lineData[side].points
                        for (var i = 0; i < points.length; ++i) {
                            if (Math.abs(px(points[i][0]) - x) <= 8 && Math.abs(py(side, points[i][1]) - y) <= 8) {
                                return { side: side, index: i }
                            }
                        }
                        return null
                    }
                    // Whether (x, y) is within 8 px of the side's line.
                    function onLine(x, y) {
                        var side = sideAt(y)
                        if (!side || !lineData) return ""
                        var s = lineData[side].samples
                        var t = timeAt(x)
                        for (var i = 1; i < s.length; ++i) {
                            if (t <= s[i][0]) {
                                var span = s[i][0] - s[i - 1][0]
                                var u = span > 0 ? (t - s[i - 1][0]) / span : 0
                                var v = s[i - 1][1] + u * (s[i][1] - s[i - 1][1])
                                return Math.abs(py(side, v) - y) <= 8 ? side : ""
                            }
                        }
                        return ""
                    }

                    function commit(side) {
                        var pts = lineData[side].points.slice()
                        pts.sort(function(a, b) { return a[0] - b[0] })
                        editedSide = ""
                        transitionZonesModel.setLanePoints(model.index, zoneItem.lane, side === "out", pts)
                    }

                    MouseArea {
                        id: laneMouse
                        anchors.fill: parent
                        enabled: zoneItem.lane >= 0
                        hoverEnabled: true
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        cursorShape: laneCanvas.pointAt(mouseX, mouseY) ? Qt.SizeAllCursor
                                     : (laneCanvas.onLine(mouseX, mouseY) ? Qt.CrossCursor : Qt.ArrowCursor)

                        // Only presses on a point or the line are the lane's; the rest
                        // go through to the clips underneath.
                        onPressed: function(mouse) {
                            // Option (Alt) is the automation lane's own: it bends
                            // the line into a curve. Let it through.
                            if (mouse.modifiers & Qt.AltModifier) {
                                mouse.accepted = false
                                return
                            }
                            var hit = laneCanvas.pointAt(mouse.x, mouse.y)
                            if (hit && mouse.button === Qt.RightButton) {
                                laneCanvas.lineData[hit.side].points.splice(hit.index, 1)
                                laneCanvas.editedSide = hit.side
                                laneCanvas.commit(hit.side)
                                return
                            }
                            if (hit) {
                                laneCanvas.drag = hit
                                laneCanvas.editedSide = hit.side
                                return
                            }
                            mouse.accepted = mouse.button === Qt.LeftButton && laneCanvas.onLine(mouse.x, mouse.y) !== ""
                        }
                        onPositionChanged: function(mouse) {
                            var d = laneCanvas.drag
                            if (!d) return
                            var points = laneCanvas.lineData[d.side].points
                            var s = laneCanvas.lineData[d.side].samples
                            // Between its neighbours, inside the zone.
                            var lo = d.index > 0 ? points[d.index - 1][0] : s[0][0]
                            var hi = d.index + 1 < points.length ? points[d.index + 1][0] : s[s.length - 1][0]
                            var t = Math.max(lo + 1e-3, Math.min(hi - 1e-3, laneCanvas.timeAt(mouse.x)))
                            points[d.index] = [t, laneCanvas.valueAt(d.side, mouse.y)]
                            laneCanvas.requestPaint()
                        }
                        onReleased: {
                            if (laneCanvas.drag) {
                                var side = laneCanvas.drag.side
                                laneCanvas.drag = null
                                laneCanvas.commit(side)
                            }
                        }
                        onDoubleClicked: function(mouse) {
                            var side = laneCanvas.onLine(mouse.x, mouse.y)
                            if (!side) return
                            laneCanvas.lineData[side].points.push([laneCanvas.timeAt(mouse.x), laneCanvas.valueAt(side, mouse.y)])
                            laneCanvas.editedSide = side
                            laneCanvas.commit(side)
                        }
                    }

                    Connections {
                        target: timeline.context
                        function onFrameTimeChanged() { laneCanvas.requestPaint() }
                        function onZoomChanged() { laneCanvas.requestPaint() }
                    }
                }

                Rectangle {
                    id: pill

                    // On the boundary between the two tracks, near the start of the
                    // part of the zone that is on screen (long overlaps run off
                    // the right edge).
                    readonly property real visibleStart: Math.max(0, -zoneItem.x)
                    readonly property real visibleEnd: Math.min(zoneItem.width, content.width - zoneItem.x)
                    x: Math.max(4, Math.min(visibleEnd - width - 4, visibleStart + 12))
                    y: Math.max(zoneItem.outTop, zoneItem.inTop) - zoneItem.y - height / 2
                    width: pillRow.implicitWidth + 16
                    height: 18
                    radius: 9
                    color: "#E6140C06"
                    border.color: zoneItem.accent
                    border.width: 1

                    // The whole pill is the pill's: a press on its edge used to fall
                    // through to the song underneath and start dragging it, and the
                    // menu that opened then swallowed the release, so the song
                    // followed the mouse. Anywhere on it opens the menu.
                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        cursorShape: Qt.PointingHandCursor
                        onClicked: function(mouse) { zoneItem.pillClicked(mouse) }
                    }

                    Row {
                        id: pillRow
                        anchors.centerIn: parent
                        spacing: 6

                        StyledTextLabel {
                            id: pillLabel
                            anchors.verticalCenter: parent.verticalCenter
                            // GlideMix: the chosen transition's name, cut short when long.
                            text: !model.crossfaded ? qsTrc("mixhouse", "BEATMATCH")
                                  : model.preset ? model.preset.toUpperCase() : qsTrc("mixhouse", "CROSSFADE")
                            width: Math.min(implicitWidth, 160)
                            elide: Text.ElideRight
                            color: zoneItem.accent
                            font.pixelSize: 10
                            font.bold: true

                            MouseArea {
                                anchors.fill: parent
                                anchors.margins: -4
                                acceptedButtons: Qt.LeftButton | Qt.RightButton
                                cursorShape: Qt.PointingHandCursor
                                // Crossfade, the transitions from Preferences > Transitions, then Remove.
                                onClicked: function(mouse) { zoneItem.pillClicked(mouse) }
                            }
                        }

                        Repeater {
                            model: zoneItem.laneOrder

                            delegate: Rectangle {
                                required property int modelData
                                readonly property int index: modelData
                                readonly property bool chosen: zoneItem.lane === index
                                anchors.verticalCenter: parent.verticalCenter
                                width: chosen ? 12 : 8
                                height: width
                                radius: width / 2
                                color: chosen ? zoneItem.laneColors[index] : "transparent"
                                border.color: zoneItem.laneColors[index]
                                border.width: chosen ? 0 : 1.5

                                MouseArea {
                                    anchors.fill: parent
                                    anchors.margins: -3
                                    cursorShape: Qt.PointingHandCursor
                                    hoverEnabled: true
                                    // Clicking the shown lane again hides it.
                                    onClicked: {
                                        zoneItem.lane = zoneItem.lane === parent.index ? -1 : parent.index
                                        transitionZonesModel.shownLanes[zoneItem.laneKey] = zoneItem.lane
                                    }
                                    onContainsMouseChanged: {
                                        if (containsMouse) {
                                            ui.tooltip.show(parent, zoneItem.laneNames[parent.index])
                                        } else {
                                            ui.tooltip.hide(parent)
                                        }
                                    }
                                }
                            }
                        }

                        // Opens the transition panel (DropLab's bottom panel), or
                        // the two-channel mixer when Preferences says so.
                        StyledIconLabel {
                            anchors.verticalCenter: parent.verticalCenter
                            iconCode: IconCode.SETTINGS_COG
                            font.pixelSize: 12
                            color: zoneItem.accent

                            MouseArea {
                                anchors.fill: parent
                                anchors.margins: -3
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    var panel = transitionZonesModel.mixerView ? transitionMixer : transitionPanel
                                    panel.isOpened ? panel.close() : panel.open()
                                }
                            }
                        }
                    }

                    TransitionPanel {
                        id: transitionPanel
                        parent: ui.rootItem
                        zoneItem: zoneItem
                        zonesModel: transitionZonesModel
                        row: model.index
                        timelineContext: timeline.context
                        playheadX: playCursorController.positionX
                        isPlaying: playbackState.isPlaying
                        zoneStart: model.startTime
                        zoneEnd: model.endTime
                        presetName: model.preset
                        onPresetMenuRequested: {
                            zoneMenu.toggleOpened(zoneItem.menuItems())
                        }
                    }

                    TransitionMixer {
                        id: transitionMixer
                        parent: ui.rootItem
                        zoneItem: zoneItem
                        zonesModel: transitionZonesModel
                        row: model.index
                        timelineContext: timeline.context
                        playheadX: playCursorController.positionX
                        isPlaying: playbackState.isPlaying
                        zoneStart: model.startTime
                        zoneEnd: model.endTime
                        presetName: model.preset
                        onPresetMenuRequested: {
                            zoneMenu.toggleOpened(zoneItem.menuItems())
                        }
                    }

                    StyledMenuLoader {
                        id: zoneMenu

                        onHandleMenuItem: function(itemId) {
                            if (itemId.indexOf("preset:") === 0) {
                                transitionZonesModel.applyPreset(model.index, parseInt(itemId.substring(7)))
                            } else if (itemId.indexOf("transition:") === 0) {
                                transitionZonesModel.applyTransition(model.index, itemId.substring(11))
                            } else if (itemId === "remove") {
                                transitionZonesModel.removeCrossfade(model.index)
                            } else if (itemId === "disable") {
                                transitionZonesModel.setTransitionEnabled(model.index, false)
                            } else if (itemId === "enable") {
                                transitionZonesModel.setTransitionEnabled(model.index, true)
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            id: splitGuideline

            x: splitToolController.guidelinePosition
            y: splitToolController.singleTrack ? hoveredTrackVerticalPosition : 0

            width: 1
            height: splitToolController.singleTrack ? hoveredTrackHeight : content.height

            color: ui.theme.extra["guideline_split_color"]

            visible: splitToolController.guidelineVisible
        }

        VerticalRulersPanel {
            id: verticalRulers

            model: tracksModel
            context: timeline.context

            height: parent.height
            width: verticalRulerPanelHeader.width
            anchors.right: parent.right
            anchors.bottom: parent.bottom
        }

        NavigationFocusBorder {
            navigationCtrl: root.navDefaultControl
            drawOutsideParent: false
        }
    }

    LoopRegionGuideline {
        id: loopRegionGuidelineLeft
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        z: timelineHeader.z + 1
        context: timeline.context
        time: playRegionModel.start
        contentX: content.x
        visible: playRegionModel.active
    }

    LoopRegionGuideline {
        id: loopRegionGuidelineRight
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        z: timelineHeader.z + 1
        context: timeline.context
        time: playRegionModel.end
        contentX: content.x
        visible: playRegionModel.active
    }

    ImportDropArea {
        id: importDropArea

        anchors.fill: root

        tracksItemsView: tracksItemsView
        tracksViewState: tracksViewState
        timeline: timeline
        headerHeight: timelineHeader.height

        onSetGuidelineRequested: function (pos, visibility) {
            root.guidelinePos = pos
            root.guidelineVisible = visibility
        }
    }

    function snapGuidelineToPosition(x) {
        let time = timeline.context.applyDetectedSnap(timeline.context.positionToTime(x))
        root.updateGuidelineAtTime(timeline.context.findGuideline(time))
    }

    function updateGuidelineAtTime(time) {
        prv.setGuidelinePosition(time)
        prv.updateGuidelineVisibility()
    }

    function hideGuideline() {
        root.guidelineVisible = false
    }
}
