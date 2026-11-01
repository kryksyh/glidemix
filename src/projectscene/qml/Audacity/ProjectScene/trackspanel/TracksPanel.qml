/*
* Audacity: A Digital Audio Editor
*/
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

import Muse.Ui
import Muse.UiComponents

import Audacity.ProjectScene

Item {
    id: root

    property alias tracksModel: tracksModel

    // property alias contextMenuModel: contextMenuModel
    property int effectsSectionWidth: 240 // TODO: can this be set as a constant that can be imported?
    property alias showEffectsSection: effectSectionModel.showEffectsSection
    property int selectedTrackIndex: -1

    property var navigationPanels: null
    property var headerNavigationPanels: null
    property NavigationPanel effectColumnNavigationPanel: null

    property NavigationSection trackEffectsNavigationSection: null
    property NavigationSection masterEffectsNavigationSection: null

    signal openEffectsRequested
    signal panelActive(var trackId)
    signal effectsSectionResizeStarted(real globalX)
    signal effectsSectionResizeMoved(real globalX)
    readonly property int minimumTrackEffectsHeight: 210

    PanelTracksListModel {
        id: tracksModel
    }

    //! NOTE Sync with TracksItemsView
    TracksViewStateModel {
        id: tracksViewState
        onTracksVerticalOffsetChanged: {
            view.contentY = tracksViewState.tracksVerticalOffset + view.originY
        }
    }

    Component.onCompleted: {
        tracksViewState.init()
        tracksModel.load()
        effectSectionModel.load()
    }

    QtObject {
        id: prv

        property string currentItemNavigationName: ""
        readonly property int listHeaderHeight: 2
    }

    Rectangle {
        anchors.fill: parent
        color: ui.theme.backgroundSecondaryColor
    }

    RowLayout {

        anchors.fill: parent
        spacing: 0

        ColumnLayout {
            id: effectColumn

            Layout.preferredWidth: root.effectsSectionWidth
            Layout.maximumWidth: root.effectsSectionWidth
            Layout.minimumWidth: root.effectsSectionWidth
            Layout.fillHeight: true

            visible: effectSectionModel.showEffectsSection
            spacing: 0

            RealtimeEffectSectionModel {
                id: effectSectionModel

                onFocusEffectsPanelRequested: {
                    Qt.callLater(function () {
                        if (root.effectColumnNavigationPanel && effectColumn.visible) {
                            root.effectColumnNavigationPanel.requestActive()
                        }
                    })
                }
            }

            SeparatorLine {}

            TrackEffectsSection {
                id: trackEffectsSection

                navigationSection: trackEffectsNavigationSection
                navigationPanel: root.effectColumnNavigationPanel
                navigationOrderStart: 1

                Layout.fillWidth: true
                Layout.fillHeight: true
                // Reserve room for several track-effect rows before the
                // Master section begins, so it never covers newly added rows.
                Layout.minimumHeight: Math.max(trackEffectsSection.minimumHeight, root.minimumTrackEffectsHeight)

                isMasterTrack: false

                onAutomationFocusRequested: tracksViewState.showPluginAutomation()
            }

            SeparatorLine {
                id: separator

                // This line is the vertical resize handle.  Use the same
                // theme-aware accent treatment as the other splitters.
                color: mouseArea.pressed || mouseArea.containsMouse
                       ? ui.theme.accentColor
                       : ui.theme.extra["track_header_separator_color"]
                thickness: mouseArea.pressed || mouseArea.containsMouse ? 2 : 1

                MouseArea {
                    id: mouseArea

                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    height: 10

                    cursorShape: Qt.SizeVerCursor
                    hoverEnabled: true
                    preventStealing: true

                    property real startY: 0
                    property real startHeight: 0

                    onPressed: mouse => {
                        startY = mouse.y
                        startHeight = trackEffectsSection.height
                    }

                    onPositionChanged: mouse => {
                        const deltaY = mouse.y - startY
                        const newMasterHeight = masterEffectsSection.height - deltaY
                        masterEffectsSection.Layout.preferredHeight = Math.min(newMasterHeight, effectColumn.height - root.minimumTrackEffectsHeight)
                    }
                }
            }

            TrackEffectsSection {
                id: masterEffectsSection

                navigationSection: masterEffectsNavigationSection
                navigationPanel: root.effectColumnNavigationPanel
                navigationOrderStart: trackEffectsSection.navigationOrderEnd + 1

                Layout.fillWidth: true
                Layout.preferredHeight: 300
                Layout.minimumHeight: masterEffectsSection.minimumHeight

                isMasterTrack: true
            }
        }

        // Keep this as a one-pixel divider, but give it a generous invisible
        // hit area so the realtime-effects width remains easy to resize.
        SeparatorLine {
            id: effectsSectionResizeHandle
            color: effectsSectionResizeMouseArea.pressed || effectsSectionResizeMouseArea.containsMouse
                   ? ui.theme.accentColor
                   : ui.theme.extra["track_header_separator_color"]
            thickness: 1

            MouseArea {
                id: effectsSectionResizeMouseArea

                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.horizontalCenter: parent.horizontalCenter
                width: 10

                enabled: effectSectionModel.showEffectsSection
                cursorShape: Qt.SizeHorCursor
                hoverEnabled: true
                preventStealing: true

                onPressed: mouse => {
                    const point = mapToGlobal(mouse.x, mouse.y)
                    root.effectsSectionResizeStarted(point.x)
                }

                onPositionChanged: mouse => {
                    if (!pressed) {
                        return
                    }
                    const point = mapToGlobal(mouse.x, mouse.y)
                    root.effectsSectionResizeMoved(point.x)
                }
            }
        }

        ColumnLayout {
            id: contentColumn

            Layout.fillWidth: true
            Layout.fillHeight: true

            readonly property int sideMargin: 12
            spacing: sideMargin

            StyledListView {
                id: view

                Layout.topMargin: 1
                Layout.fillWidth: true
                Layout.fillHeight: true

                spacing: 0
                // GlideMix: every header stays built, like the lanes (TracksItemsView).
                cacheBuffer: Math.max(3000, view.count * 300)

                ScrollBar.vertical: null

                property real lockedVerticalScrollPosition
                property bool verticalScrollLocked: tracksViewState.tracksVerticalScrollLocked

                onVerticalScrollLockedChanged: {
                    lockedVerticalScrollPosition = contentY
                }

                onOriginYChanged: {
                    if (!verticalScrollLocked) {
                        view.contentY = tracksViewState.tracksVerticalOffset + originY
                    }
                }

                onContentYChanged: {
                    if (verticalScrollLocked) {
                        view.contentY = lockedVerticalScrollPosition
                    } else {
                        tracksViewState.changeTracksVerticalOffset(view.contentY - view.originY)
                    }
                }

                interactive: false

                model: tracksModel

                header: Rectangle {
                    height: prv.listHeaderHeight
                    width: parent.width
                    color: "transparent"
                }

                footer: Item {
                    height: tracksViewState.tracksVerticalScrollPadding
                }

                function insureVerticallyVisible(item) {
                    var itemViewY = item.mapToItem(view.contentItem, Qt.point(0, 0)).y
                    tracksViewState.insureVerticallyVisible(view.contentY - view.originY, view.height, itemViewY - view.originY, item.height)
                }

                delegate: Loader {
                    id: trackItemLoader

                    property var trackItemData: itemData
                    property int index: model.index

                    width: view.width

                    sourceComponent: (itemData && itemData.trackType === TrackType.LABEL) ? labelItemComp : waveItemComp

                    onLoaded: {
                        trackItemLoader.item.init()
                    }

                    Component {
                        id: waveItemComp

                        WaveTrackItem {
                            property int index: trackItemLoader.index

                            item: trackItemLoader.trackItemData

                            isSelected: Boolean(item) ? item.isSelected : false
                            isFocused: Boolean(item) ? item.isFocused : false
                            container: view

                            navigation.name: Boolean(item) ? item.title + index : ""
                            navigation.panel: root.navigationPanels && root.navigationPanels[index] ? root.navigationPanels[index] : null
                            navigation.order: 0
                            headerNavigationPanel: root.headerNavigationPanels && root.headerNavigationPanels[index] ? root.headerNavigationPanels[index] : null
                            navigation.accessible.name: Boolean(item) ? (isSelected ? qsTrc("projectscene", "Track %1: %2, audio track, selected").arg(index + 1).arg(item.title) : qsTrc("projectscene", "Track %1: %2, audio track").arg(index + 1).arg(item.title)) : ""
                            navigation.accessible.description: qsTrc("projectscene", "Press Enter to select or deselect")
                            navigation.accessible.role: MUAccessible.Group
                            navigation.onActiveChanged: {
                                if (navigation.active) {
                                    prv.currentItemNavigationName = navigation.name

                                    view.insureVerticallyVisible(this)
                                }
                            }

                            onInteractionStarted: {
                                tracksViewState.requestVerticalScrollLock()
                            }

                            onInteractionEnded: {
                                tracksViewState.requestVerticalScrollUnlock()
                            }

                            onSelectionRequested: function (exclusive) {
                                tracksModel.selectRow(index, exclusive)
                            }

                            onDataSelectionRequested: {
                                tracksModel.selectAudioData(index)
                            }

                            onAutomationFocusRequested: {
                                tracksViewState.showClipGainAutomation()
                            }

                            onOpenEffectsRequested: {
                                tracksModel.focusTrack(index)
                                effectSectionModel.showEffectsSection = true
                                root.openEffectsRequested()
                            }

                            onRemoveSelectionRequested: {
                                tracksModel.removeSelection()
                            }

                            onMouseReleased: function (releasedItem, x, y) {
                                root.panelActive(item.trackId)
                            }

                            Component.onCompleted: {
                                mousePressed.connect(dragHandler.startDrag)
                                mouseReleased.connect(dragHandler.endDrag)
                                mouseMoved.connect(dragHandler.onMouseMove)
                            }
                        }
                    }

                    Component {
                        id: labelItemComp

                        LabelTrackItem {
                            property int index: trackItemLoader.index

                            item: trackItemLoader.trackItemData

                            isSelected: Boolean(item) ? item.isSelected : false
                            isFocused: Boolean(item) ? item.isFocused : false
                            container: view

                            navigation.name: Boolean(item) ? item.title + index : ""
                            navigation.panel: root.navigationPanels && root.navigationPanels[index] ? root.navigationPanels[index] : null
                            navigation.order: 0
                            headerNavigationPanel: root.headerNavigationPanels && root.headerNavigationPanels[index] ? root.headerNavigationPanels[index] : null
                            navigation.accessible.name: Boolean(item) ? (isSelected ? qsTrc("projectscene", "Track %1: %2, label track, selected").arg(index + 1).arg(item.title) : qsTrc("projectscene", "Track %1: %2, label track").arg(index + 1).arg(item.title)) : ""
                            navigation.accessible.description: qsTrc("projectscene", "Press Enter to select or deselect")
                            navigation.accessible.role: MUAccessible.Group
                            navigation.onActiveChanged: {
                                if (navigation.active) {
                                    prv.currentItemNavigationName = navigation.name

                                    view.insureVerticallyVisible(this)
                                }
                            }

                            onInteractionStarted: {
                                tracksViewState.requestVerticalScrollLock()
                            }

                            onInteractionEnded: {
                                tracksViewState.requestVerticalScrollUnlock()
                            }

                            onSelectionRequested: function (exclusive) {
                                tracksModel.selectRow(index, exclusive)
                            }

                            onDataSelectionRequested: {
                                tracksModel.selectAudioData(index)
                            }

                            onRemoveSelectionRequested: {
                                tracksModel.removeSelection()
                            }

                            onAddLabelToSelectionRequested: {
                                tracksModel.addLabelToSelection()
                            }

                            onMouseReleased: function (releasedItem, x, y) {
                                root.panelActive(item.trackId)
                            }

                            Component.onCompleted: {
                                mousePressed.connect(dragHandler.startDrag)
                                mouseReleased.connect(dragHandler.endDrag)
                                mouseMoved.connect(dragHandler.onMouseMove)
                            }
                        }
                    }
                }

                Item {
                    id: wheelHandler

                    anchors.fill: parent

                    WheelHandler {
                        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad

                        onWheel: function (wheelEvent) {
                            let delta = wheelEvent.pixelDelta.y !== 0 ? wheelEvent.pixelDelta.y : wheelEvent.angleDelta.y
                            let offset = view.contentY - view.originY - delta

                            let maxOffset = Math.max(view.contentHeight - view.height, 0)
                            offset = Math.max(Math.min(offset, maxOffset), 0)

                            view.contentY = offset + view.originY
                        }
                    }
                }

                Item {
                    id: dragHandler

                    anchors.fill: parent

                    property bool dragging: false
                    property int dragFirstIndex: -1
                    property int dragLastIndex: -1
                    property int dropIndex: -1

                    Rectangle {
                        id: dropCursor
                        width: parent.width
                        height: 2
                        color: ui.theme.accentColor
                        visible: dragHandler.dropIndex >= 0
                        y: {
                            if (dragHandler.dropIndex >= 0 && dragHandler.dropIndex < view.count) {
                                let loader = view.itemAtIndex(dragHandler.dropIndex)
                                return Boolean(loader) ? loader.y - view.contentY : 0
                            }
                            return view.contentHeight - view.contentY - tracksViewState.tracksVerticalScrollPadding
                        }
                    }

                    function startDrag(item, mouseX, mouseY) {
                        let selection = tracksModel.selectionModel().selectedIndexes
                        if (selection.length === 0) {
                            return
                        }

                        mouseY = view.mapFromItem(item, mouseX, mouseY).y + view.contentY

                        dragFirstIndex = selection[0].row
                        dragLastIndex = selection[selection.length - 1].row

                        dragging = true
                    }

                    function endDrag() {
                        dragging = false
                        setDraggedStateForTracks(false)

                        if (dragFirstIndex < dropIndex) {
                            dropIndex--
                        }

                        if (dragFirstIndex == dropIndex || dropIndex < 0) {
                            return
                        }

                        let selectedRows = tracksModel.selectionModel().selectedIndexes.map(index => index.row)

                        tracksModel.requestTracksMove(selectedRows, dropIndex)
                        dropIndex = -1
                    }

                    function onMouseMove(item, mouseX, mouseY) {
                        if (!dragging) {
                            return
                        }

                        mouseY = view.mapFromItem(item, mouseX, mouseY).y + view.contentY

                        let itemAtCursor = view.itemAt(0, mouseY)
                        let indexAtCursor = view.indexAt(0, mouseY)

                        if (itemAtCursor) {
                            if (itemAtCursor.height / 2 < view.mapToItem(itemAtCursor, 0, mouseY - view.contentY).y) {
                                indexAtCursor++
                            }
                        } else {
                            indexAtCursor = mouseY < 0 ? 0 : view.count
                        }

                        if (dragFirstIndex > indexAtCursor || (dragLastIndex + 1) < indexAtCursor) {
                            dropIndex = indexAtCursor
                            setDraggedStateForTracks(true)
                        } else {
                            dropIndex = -1
                            setDraggedStateForTracks(false)
                        }
                    }

                    function setDraggedStateForTracks(state) {
                        tracksModel.selectionModel().selectedIndexes.forEach(selectedIndex => {
                            let loader = view.itemAtIndex(selectedIndex.row)
                            if (Boolean(loader) && Boolean(loader.item)) {
                                loader.item.dragged = state
                            }
                        })
                    }
                }
            }
        }
    }

}
