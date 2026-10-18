/*
* Audacity: A Digital Audio Editor
*/
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents

import Audacity.ProjectScene

ListItemBlank {
    id: root

    property var item: null
    property var container: null
    property bool dragged: false
    property bool collapsed: trackViewState.isTrackCollapsed

    property bool isFocused: false

    //! NOTE: the track itself is a control of its own panel (root.navigation),
    //! all the header controls live in the next panel, so Tab goes: track -> header controls -> clips/labels
    property NavigationPanel headerNavigationPanel: null

    property alias headerTrailingControlsComponent: headerTrailingControls.sourceComponent
    property int headerTrailingControlsNavigationStart: collapsed ? title.navigation.order + 1 : extraControlsNavigationEnd + 1
    property int headerTrailingControlsNavigationEnd: 0

    property alias extraControlsComponent: extraControlsLoader.sourceComponent
    property int extraControlsNavigationStart: menuButton.navigation.order + 1
    property int extraControlsNavigationEnd: 0

    property alias rightSideContainerComponent: rightSideContainer.sourceComponent

    property alias bottomSeparatorHeight: bottomSeparator.height

    signal interactionStarted
    signal interactionEnded
    signal selectionRequested(bool exclusive)
    signal dataSelectionRequested
    signal automationFocusRequested

    signal mousePressed(var item, double x, double y)
    signal mouseReleased(var item, double x, double y)
    signal mouseMoved(var item, double x, double y)

    mouseArea.onPressed: {
        root.mousePressed(this, mouseArea.mouseX, mouseArea.mouseY)
    }

    mouseArea.onReleased: {
        root.mouseReleased(this, mouseArea.mouseX, mouseArea.mouseY)
    }

    mouseArea.onPositionChanged: {
        root.mouseMoved(this, mouseArea.mouseX, mouseArea.mouseY)
    }

    mouseArea.onDoubleClicked: {
        let titlePos = root.mapFromItem(title, 0, 0)
        if (mouseArea.mouseX >= titlePos.x && mouseArea.mouseX <= titlePos.x + title.width && mouseArea.mouseY >= titlePos.y && mouseArea.mouseY <= titlePos.y + title.height) {
            title.edit()
        }
    }

    height: trackViewState.trackHeight
    opacity: dragged ? 0.5 : 1

    focusBorder.anchors.leftMargin: spacer.width + 2
    focusBorder.anchors.rightMargin: 24 + separatorLine.width
    focusBorder.anchors.bottomMargin: 2

    background.color: root.isSelected ? ui.theme.extra["track_header_active_color"] : (hoverHandler.hovered ? ui.theme.extra["track_header_hover_color"] : ui.theme.extra["track_header_color"])

    background.anchors.leftMargin: spacer.width
    background.anchors.rightMargin: -background.radius
    background.anchors.bottomMargin: bottomSeparator.thickness
    background.radius: 4

    signal renameTrackRequested
    signal duplicateRequested
    signal deleteRequested

    signal openEffectsRequested

    property TrackViewStateModel trackViewState: TrackViewStateModel {
        trackId: root.item ? root.item.trackId : -1
    }

    TrackContextMenuModel {
        id: contextMenuModel
        trackId: root.item ? root.item.trackId : -1

        onTrackRenameRequested: title.edit()

        onTrackColorPickerRequested: {
            // Let the command menu finish closing; otherwise its outside-click
            // policy may immediately dismiss the palette it just opened.
            Qt.callLater(function () { trackColorPicker.open() })
        }

        onOpenRequested: {
            menuButton.toggleMenu(menuButton)
        }
    }

    TrackColorPalettePopup {
        id: trackColorPicker
        // Parent to the application root so the floating window is centred on
        // the Audacity window rather than on the track's ellipsis button.
        parent: ui.rootItem
        colors: contextMenuModel.colorPalette
        currentColorIndex: contextMenuModel.currentColorIndex

        onColorSelected: function (colorIndex) {
            contextMenuModel.setTrackColor(colorIndex)
        }
    }

    function init() {
        trackViewState.init()
        contextMenuModel.load()
        // Disable ancestor's states
        // to provide custom styling
        states = []
    }

    ContextMenuLoader {
        id: contextMenuLoader

        onHandleMenuItem: function (itemId) {
            contextMenuModel.handleMenuItem(itemId)
        }
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.RightButton

        onClicked: function (e) {
            if (!isSelected) {
                root.selectionRequested(true)
            }

            contextMenuLoader.show(Qt.point(e.x, e.y), contextMenuModel.items)
        }
    }

    MouseArea {
        anchors.fill: parent

        onPressed: function (e) {
            root.automationFocusRequested()

            // Pass the event forward to allow
            // child elements to handle the input
            e.accepted = false

            let multiSelectionModifier = e.modifiers & (Qt.ControlModifier | Qt.ShiftModifier)

            if (!multiSelectionModifier) {
                root.selectionRequested(true)
                root.dataSelectionRequested()
                return
            }

            root.selectionRequested(false)
        }
    }

    Item {
        anchors.fill: parent

        Rectangle {
            id: spacer

            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 9

            color: "transparent"
        }

        ColumnLayout {
            id: contentColumn
            anchors.left: spacer.right
            anchors.right: separatorLine.left
            anchors.top: parent.top
            anchors.topMargin: 7
            anchors.margins: 12

            RowLayout {
                Layout.fillWidth: true

                spacing: 8

                StyledIconLabel {
                    iconCode: Boolean(root.item) ? root.item.icon : 0
                }

                EditableLabel {
                    id: title

                    Layout.preferredHeight: title.implicitHeight
                    Layout.fillWidth: true

                    text: Boolean(root.item) ? root.item.title : ""

                    navigation.panel: root.headerNavigationPanel
                    navigation.order: root.navigation.order + 1

                    onTextEdited: function (text) {
                        if (Boolean(root.item)) {
                            root.item.title = text
                        }
                    }
                }

                Loader {
                    id: headerTrailingControls
                    visible: root.collapsed && item !== null
                }

                MenuButton {
                    id: menuButton

                    menuModel: contextMenuModel

                    navigation.panel: root.headerNavigationPanel
                    navigation.order: root.collapsed ? root.headerTrailingControlsNavigationEnd + 1 : title.navigation.order + 1
                    navigation.accessible.name: qsTrc("projectscene", "Track menu")

                    onClicked: {
                        root.selectionRequested(true)
                    }

                    onHandleMenuItem: function (itemId) {
                        contextMenuModel.handleMenuItem(itemId)
                    }
                }
            }

            Loader {
                id: extraControlsLoader
                Layout.fillWidth: true
                Layout.preferredHeight: implicitHeight
            }
        }

        SeparatorLine {
            id: separatorLine
            anchors.right: rightSideContainer.left
            anchors.bottomMargin: bottomSeparator.thickness
            orientation: Qt.Vertical
            color: ui.theme.extra["track_header_separator_color"]
        }

        Loader {
            id: rightSideContainer

            anchors.right: parent.right
            anchors.top: parent.top
            anchors.topMargin: 6
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 6

            width: 24
        }
    }

    // GlideMix: the names of the automation lanes under this track, at the
    // lanes' height, each with a close button (Ableton's lane headers).
    Column {
        id: automationLaneHeaders

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.bottomMargin: bottomSeparator.thickness
        visible: root.trackViewState.automationLanes.length > 0
        z: 5

        Repeater {
            model: root.trackViewState.automationLanes

            Rectangle {
                required property var modelData

                width: automationLaneHeaders.width
                height: root.trackViewState.automationLanesHeight / Math.max(1, root.trackViewState.automationLanes.length)
                color: ui.theme.backgroundSecondaryColor

                Rectangle {
                    anchors.top: parent.top
                    width: parent.width
                    height: 1
                    color: ui.theme.strokeColor
                }

                Rectangle {
                    id: laneDot
                    anchors.left: parent.left
                    anchors.leftMargin: 14
                    anchors.verticalCenter: parent.verticalCenter
                    width: 8
                    height: 8
                    radius: 4
                    color: modelData.color
                }

                StyledTextLabel {
                    anchors.left: laneDot.right
                    anchors.leftMargin: 8
                    anchors.right: laneClose.left
                    anchors.rightMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    horizontalAlignment: Text.AlignLeft
                    text: modelData.label
                }

                FlatButton {
                    id: laneClose
                    anchors.right: parent.right
                    anchors.rightMargin: 34
                    anchors.verticalCenter: parent.verticalCenter
                    width: 20
                    height: 20
                    icon: IconCode.CLOSE_X_ROUNDED
                    transparent: true
                    toolTipTitle: qsTrc("projectscene", "Close this automation lane")
                    onClicked: root.trackViewState.removeAutomationLane(modelData.instanceId, modelData.parameterId)
                }
            }
        }
    }

    HoverHandler {
        id: hoverHandler
    }

    MouseArea {
        id: resizeArea

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 4

        cursorShape: Qt.SizeVerCursor

        onPressed: {
            root.interactionStarted()
        }

        onPositionChanged: function (mouse) {
            const resizeVerticalMargin = 10
            mouse.accepted = true

            const currentY = mapToItem(container, 0, 0).y - container.y

            const maxPosition = container.height - resizeVerticalMargin - height
            const minPosition = resizeVerticalMargin
            const newPosition = Math.max(Math.min(currentY + mouse.y, maxPosition), minPosition)

            const delta = newPosition - currentY
            trackViewState.changeTrackHeight(delta)
        }

        onReleased: {
            root.interactionEnded()
        }
    }

    SeparatorLine {
        id: bottomSeparator

        anchors.bottom: parent.bottom

        color: "transparent"

        thickness: 2
    }

    Rectangle {
        id: trackFocusState

        anchors.fill: parent
        anchors.leftMargin: spacer.width - border.width
        anchors.rightMargin: -radius
        anchors.topMargin: -border.width
        anchors.bottomMargin: bottomSeparator.thickness - border.width

        visible: root.isFocused

        color: "transparent"

        border.color: ui.theme.extra["focus_state_color"]
        border.width: 1

        radius: 6
    }
}
