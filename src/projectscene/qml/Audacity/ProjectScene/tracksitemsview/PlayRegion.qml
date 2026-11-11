/*
* Audacity: A Digital Audio Editor
*/

import QtQuick

import Audacity.ProjectScene

Rectangle {
    id: root

    required property TimelineContext context

    property alias start: playRegionModel.start
    property alias end: playRegionModel.end
    property alias active: playRegionModel.active

    // GlideMix (Chris, plan A5): the loop lives in the strip under the time
    // numbers, next to the timeline it loops; the numbers above only seek.
    y: parent.height / 2
    height: parent.height / 2

    color: ui.theme.accentColor
    opacity: active ? ui.theme.accentOpacityNormal : ui.theme.accentOpacityHover

    function updatePosition() {
        let newX = context.timeToPosition(start)
        x = newX > 0 ? newX : 0
        width = context.timeToPosition(end) - x
    }

    onStartChanged: updatePosition()
    onEndChanged: updatePosition()

    Component.onCompleted: {
        playRegionModel.init()
    }

    Connections {
        target: context

        function onFrameStartTimeChanged() {
            updatePosition()
        }

        function onFrameEndTimeChanged() {
            updatePosition()
        }
    }

    PlayRegionModel {
        id: playRegionModel
    }

    MouseArea {
        anchors.verticalCenter: parent.verticalCenter

        anchors.left: mouseAreaResizeLeft.right
        anchors.right: mouseAreaResizeRight.left

        width: 6
        height: parent.height

        hoverEnabled: true
        cursorShape: Qt.OpenHandCursor
        acceptedButtons: Qt.NoButton
    }

    MouseArea {
        id: mouseAreaResizeLeft

        anchors.verticalCenter: parent.verticalCenter

        x: -width / 2
        width: 6
        height: parent.height

        hoverEnabled: true
        cursorShape: Qt.SizeHorCursor
        acceptedButtons: Qt.NoButton
    }

    MouseArea {
        id: mouseAreaResizeRight

        anchors.verticalCenter: parent.verticalCenter

        x: parent.width - width / 2
        width: 6
        height: parent.height

        hoverEnabled: true
        cursorShape: Qt.SizeHorCursor
        acceptedButtons: Qt.NoButton
    }
}
