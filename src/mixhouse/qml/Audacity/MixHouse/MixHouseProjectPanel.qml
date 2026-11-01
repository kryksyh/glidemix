/*
 * GlideMix -- the Project tab: the project's songs in mix order, the way
 * MixMeister's playlist shows them. No transport buttons (GlideMix's own stay
 * on top). Click a row to scroll the timeline to it, drag a row to reorder the
 * mix, drop library songs here to add them: each lands linked, IN on OUT.
 */
import QtQuick
import QtQuick.Layouts

import Muse.Ui 1.0
import Muse.UiComponents 1.0

import Audacity.MixHouse

Item {
    id: root

    property NavigationSection navigationSection: null

    readonly property int rowHeight: 28
    readonly property var columnWidths: [44, 0, 200, 110, 56, 190]   // 0 = takes the rest (Title)

    MixHouseProjectListModel {
        id: projectModel
    }

    readonly property var transitionChoices: {
        var list = []
        for (var i = 0; i < projectModel.transitionNames.length; ++i) {
            list.push({ text: projectModel.transitionNames[i], value: projectModel.transitionNames[i] })
        }
        return list
    }

    function titleWidth() {
        var used = 0
        for (var i = 0; i < columnWidths.length; ++i) {
            used += columnWidths[i]
        }
        return Math.max(120, listView.width - used - 16)
    }

    // The header row.
    Rectangle {
        id: header
        anchors.left: parent.left
        anchors.right: parent.right
        height: 24
        color: ui.theme.backgroundSecondaryColor

        Row {
            anchors.fill: parent
            anchors.leftMargin: 8
            Repeater {
                model: [ "#", qsTrc("mixhouse", "Title"), qsTrc("mixhouse", "Artist"), qsTrc("mixhouse", "BPM (stretch)"),
                    qsTrc("mixhouse", "Key"), qsTrc("mixhouse", "Transition") ]
                StyledTextLabel {
                    width: index === 1 ? root.titleWidth() : root.columnWidths[index]
                    height: header.height
                    horizontalAlignment: Text.AlignLeft
                    text: modelData
                    font: ui.theme.bodyBoldFont
                }
            }
        }

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: ui.theme.strokeColor
        }
    }

    StyledListView {
        id: listView
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        clip: true
        model: projectModel

        property int dragFrom: -1
        property int dropAt: -1   // the row a dragged song or a library drop goes in front of

        function rowAt(y) {
            var r = Math.floor((y + contentY) / root.rowHeight)
            return Math.max(0, Math.min(count, r))
        }

        delegate: Item {
            id: rowItem
            width: listView.width
            height: root.rowHeight

            Rectangle {
                anchors.fill: parent
                color: model.playing ? Qt.rgba(ui.theme.accentColor.r, ui.theme.accentColor.g, ui.theme.accentColor.b, 0.18)
                                     : (index % 2 ? ui.theme.backgroundPrimaryColor : ui.theme.backgroundSecondaryColor)
                opacity: model.overlay ? 0.75 : 1.0
            }

            // Progress through the song, from the playhead; click to seek there.
            Rectangle {
                id: progressTrack
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 3
                color: "transparent"
                Rectangle {
                    height: parent.height
                    width: parent.width * model.progress
                    color: ui.theme.accentColor
                }
                MouseArea {
                    anchors.fill: parent
                    anchors.topMargin: -3
                    cursorShape: Qt.PointingHandCursor
                    onClicked: function(mouse) { projectModel.seekRow(index, mouse.x / width) }
                }
            }

            Row {
                anchors.fill: parent
                anchors.leftMargin: model.overlay ? 20 : 8
                anchors.bottomMargin: 3

                StyledTextLabel {
                    width: root.columnWidths[0] - (model.overlay ? 12 : 0)
                    height: parent.height
                    horizontalAlignment: Text.AlignLeft
                    text: model.label
                }
                StyledTextLabel {
                    width: root.titleWidth()
                    height: parent.height
                    horizontalAlignment: Text.AlignLeft
                    text: model.analysing ? model.title + "  " + qsTrc("mixhouse", "(waiting for analysis)") : model.title
                    elide: Text.ElideRight
                }
                StyledTextLabel {
                    width: root.columnWidths[2]
                    height: parent.height
                    horizontalAlignment: Text.AlignLeft
                    text: model.artist
                    elide: Text.ElideRight
                }
                StyledTextLabel {
                    width: root.columnWidths[3]
                    height: parent.height
                    horizontalAlignment: Text.AlignLeft
                    text: model.bpm > 0
                          ? model.bpm.toFixed(1) + (Math.abs(model.stretch) >= 0.05
                                                    ? " (" + (model.stretch > 0 ? "+" : "") + model.stretch.toFixed(1) + "%)" : "")
                          : ""
                }
                StyledTextLabel {
                    width: root.columnWidths[4]
                    height: parent.height
                    horizontalAlignment: Text.AlignLeft
                    text: model.songKey
                }
                Item {
                    width: root.columnWidths[5]
                    height: parent.height
                    StyledDropdown {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - 8
                        height: root.rowHeight - 6
                        visible: rowItem.rowHasTransition
                        model: root.transitionChoices
                        indeterminateText: rowItem.rowTransition === "" ? qsTrc("mixhouse", "None")
                                                                         : rowItem.rowTransition
                        currentIndex: indexOfValue(rowItem.rowTransition)
                        onActivated: function(i, value) { projectModel.setTransition(rowItem.rowIndex, value) }
                    }
                }
            }

            readonly property int rowIndex: index
            // The dropdown has a `model` of its own.
            readonly property string rowTransition: model.transition
            readonly property bool rowHasTransition: model.hasTransition

            // Click: scroll the timeline to the song. Drag: reorder.
            MouseArea {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: progressTrack.top
                width: parent.width - root.columnWidths[5]
                property real pressY: 0
                property bool dragging: false
                onPressed: function(mouse) {
                    pressY = mouse.y
                    dragging = false
                }
                onPositionChanged: function(mouse) {
                    if (!dragging && !model.overlay && Math.abs(mouse.y - pressY) > 6) {
                        dragging = true
                        listView.dragFrom = index
                    }
                    if (dragging) {
                        var p = mapToItem(listView, mouse.x, mouse.y)
                        listView.dropAt = listView.rowAt(p.y + root.rowHeight / 2)
                    }
                }
                onReleased: {
                    if (dragging) {
                        var from = listView.dragFrom
                        var to = listView.dropAt
                        listView.dragFrom = -1
                        listView.dropAt = -1
                        dragging = false
                        projectModel.moveRow(from, to)
                    }
                }
                onClicked: {
                    if (!dragging) {
                        projectModel.revealRow(index)
                    }
                }
                onCanceled: {
                    dragging = false
                    listView.dragFrom = -1
                    listView.dropAt = -1
                }
            }
        }

        // Where a dragged row or dropped song goes.
        Rectangle {
            visible: listView.dropAt >= 0
            x: 0
            width: listView.width
            height: 2
            y: listView.dropAt * root.rowHeight - listView.contentY - 1
            color: ui.theme.accentColor
            z: 10
        }

        StyledTextLabel {
            anchors.centerIn: parent
            visible: listView.count === 0
            text: qsTrc("mixhouse", "Drop songs from the music library here to start a mix")
        }
    }

    // Library songs dropped here join the mix.
    DropArea {
        anchors.fill: listView
        onEntered: function(drag) {
            if (!drag.hasUrls) {
                drag.accepted = false
            }
        }
        onPositionChanged: function(drag) {
            listView.dropAt = listView.rowAt(drag.y + root.rowHeight / 2)
        }
        onExited: listView.dropAt = -1
        onDropped: function(drop) {
            var at = listView.dropAt >= listView.count ? -1 : listView.dropAt
            listView.dropAt = -1
            projectModel.addFiles(drop.urls, at)
            drop.accept()
        }
    }
}
