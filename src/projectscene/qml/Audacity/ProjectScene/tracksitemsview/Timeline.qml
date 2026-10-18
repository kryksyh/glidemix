import QtQuick

import Muse.UiComponents

import Audacity.ProjectScene

Rectangle {
    id: root

    property alias context: timelineContext
    property alias ruler: timelineRuler

    color: ui.theme.backgroundSecondaryColor

    //! NOTE This element must be the same width as the track wave visible area.
    //! If this is different, then appropriate changes must be made.
    onWidthChanged: {
        if (root.visible) {
            timelineContext.onResizeFrameWidth(root.width)
        }
    }

    function init() {
        timelineContext.init(root.width)
    }

    //! ~~~ TimelineContext ~~~
    //! NOTE See comment in TimelineContext (.h)
    function onWheel(mouseX, pixelDelta, angleDelta) {
        return timelineContext.onWheel(mouseX, pixelDelta, angleDelta)
    }

    function onSelection(x1, x2) {
        timelineContext.onSelection(x1, x2)
    }

    function resetSelection() {
        timelineContext.resetSelection()
    }

    function isMajorSection(y) {
        return timelineRuler.isMajorSection(y)
    }

    TimelineContextMenuModel {
        id: contextMenuModel
    }

    ContextMenuLoader {
        id: contextMenuLoader

        // GlideMix: where the ruler was right-clicked, for "Set bar 1 here".
        property double clickTime: 0.0

        onHandleMenuItem: function (itemId) {
            if (itemId === "mixtape-set-bar-one") {
                timelineContext.setBarOneAt(clickTime)
            } else if (itemId === "mixtape-reset-bar-one") {
                timelineContext.resetBarOne()
            } else {
                contextMenuModel.handleMenuItem(itemId)
            }
        }
    }

    TimelineContext {
        id: timelineContext
    }
    // ~~~~~~~~~~~~~~~~~~~~~~~~~~

    PlayRegion {
        id: playRegion

        context: timelineContext
    }

    TimelineRuler {
        id: timelineRuler

        anchors.fill: parent

        context: timelineContext
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.RightButton
        // MouseArea defaults cursorShape to ArrowCursor even with acceptedButtons: Qt.RightButton.
        // Setting undefined lets the underlying PlayRegion cursors through.
        cursorShape: undefined
        onClicked: function (e) {
            contextMenuLoader.clickTime = timelineContext.positionToTime(e.x)
            contextMenuModel.barOneSet = timelineContext.barOneTime > 0
            contextMenuModel.load()
            contextMenuLoader.show(Qt.point(e.x, e.y), contextMenuModel.items)
        }
    }

    SeparatorLine {
        anchors.bottom: parent.bottom
    }
}
