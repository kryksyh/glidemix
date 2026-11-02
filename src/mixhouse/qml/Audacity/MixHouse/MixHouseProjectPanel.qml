/*
 * GlideMix -- the Project tab: the project's songs in mix order, the way
 * MixMeister's playlist shows them. No transport buttons (GlideMix's own stay
 * on top). Click a row to scroll the timeline to it, drag a row to reorder the
 * mix, drop library songs here to add them: each lands linked, IN on OUT.
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

import Muse.Ui 1.0
import Muse.UiComponents 1.0

import Audacity.MixHouse

import "MixHouseOverviewPaint.js" as OverviewPaint

Item {
    id: root

    property NavigationSection navigationSection: null

    // The library table's columns; only the transition stays pinned at the right.
    readonly property int rowHeight: Math.max(28, projectModel.rowHeight)
    readonly property int transitionWidth: 190
    readonly property var columns: [
        { key: "number", label: "#", width: 44 },
        { key: "coverArt", label: qsTrc("mixhouse", "Cover Art"), width: 90 },
        { key: "overview", label: qsTrc("mixhouse", "Waveform Overview"), width: 330 },
        { key: "artist", label: qsTrc("mixhouse", "Artist"), width: 180 },
        { key: "year", label: qsTrc("mixhouse", "Year"), width: 70 },
        { key: "title", label: qsTrc("mixhouse", "Title"), width: 300 },
        { key: "musicalKey", label: qsTrc("mixhouse", "Key"), width: 70 },
        { key: "genre", label: qsTrc("mixhouse", "Genre"), width: 150 },
        { key: "bpm", label: qsTrc("mixhouse", "BPM (stretch)"), width: 120 },
        { key: "energy", label: qsTrc("mixhouse", "Energy"), width: 100 },
        { key: "durationSec", label: qsTrc("mixhouse", "Duration"), width: 80 },
        { key: "rating", label: qsTrc("mixhouse", "Rating"), width: 110 },
        { key: "comment", label: qsTrc("mixhouse", "Comment"), width: 220 },
        { key: "dateAdded", label: qsTrc("mixhouse", "Date Added"), width: 110 }
    ]
    readonly property real columnsWidth: {
        var total = 8
        for (var i = 0; i < columns.length; ++i) {
            total += columns[i].width
        }
        return total
    }
    readonly property real columnsViewWidth: Math.max(0, width - transitionWidth)
    readonly property real maxScroll: Math.max(0, columnsWidth - columnsViewWidth)
    property real horizontalScroll: 0
    onMaxScrollChanged: horizontalScroll = Math.min(horizontalScroll, maxScroll)

    function scrollBy(delta) {
        horizontalScroll = Math.max(0, Math.min(horizontalScroll + delta, maxScroll))
    }

    function cellText(row, key) {
        switch (key) {
        case "number": return row.label
        case "artist": return row.artist
        case "year": return row.year
        case "title": return row.analysing ? row.title + "  " + qsTrc("mixhouse", "(waiting for analysis)") : row.title
        case "genre": return row.genre
        case "musicalKey": return row.songKey
        case "bpm":
            return row.bpm > 0
                    ? row.bpm.toFixed(1) + (Math.abs(row.stretch) >= 0.05
                                            ? " (" + (row.stretch > 0 ? "+" : "") + row.stretch.toFixed(1) + "%)" : "")
                    : ""
        case "durationSec":
            if (row.durationSec <= 0) {
                return ""
            }
            var seconds = Math.round(row.durationSec)
            var rest = seconds % 60
            return Math.floor(seconds / 60) + ":" + (rest < 10 ? "0" : "") + rest
        case "rating":
            var stars = Math.max(0, Math.min(5, row.rating))
            return "★".repeat(stars) + "☆".repeat(5 - stars)
        case "comment": return row.comment
        case "dateAdded": return row.dateAdded ? row.dateAdded.split(" ")[0] : ""
        default: return ""
        }
    }

    // Only for the key colours: never shown.
    MixHouseKeyWheel {
        id: keyColors
        visible: false
    }

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

    // The header row.
    Rectangle {
        id: header
        anchors.left: parent.left
        anchors.right: parent.right
        height: 24
        color: ui.theme.backgroundSecondaryColor

        Item {
            width: root.columnsViewWidth
            height: parent.height
            clip: true

            Row {
                x: 8 - root.horizontalScroll
                height: parent.height
                Repeater {
                    model: root.columns
                    StyledTextLabel {
                        width: modelData.width
                        height: header.height
                        horizontalAlignment: Text.AlignLeft
                        text: modelData.label
                        font: ui.theme.bodyBoldFont
                        elide: Text.ElideRight
                    }
                }
            }
        }

        StyledTextLabel {
            anchors.right: parent.right
            width: root.transitionWidth
            height: parent.height
            horizontalAlignment: Text.AlignLeft
            text: qsTrc("mixhouse", "Transition")
            font: ui.theme.bodyBoldFont
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
        anchors.bottom: columnsScrollBar.visible ? columnsScrollBar.top : parent.bottom
        clip: true
        model: projectModel

        WheelHandler {
            orientation: Qt.Horizontal
            onWheel: function(event) {
                root.scrollBy(-event.angleDelta.x)
            }
        }

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

            readonly property var rowData: model

            Item {
                width: root.columnsViewWidth
                height: parent.height - 3
                clip: true

                Row {
                    x: (rowItem.rowOverlay ? 20 : 8) - root.horizontalScroll
                    height: parent.height

                    Repeater {
                        model: root.columns

                        Item {
                            width: modelData.width
                            height: parent.height

                            readonly property string columnKey: modelData.key

                            StyledTextLabel {
                                anchors.fill: parent
                                anchors.rightMargin: 8
                                visible: !keyBadge.visible && !energyBlocks.visible
                                horizontalAlignment: Text.AlignLeft
                                elide: Text.ElideRight
                                text: root.cellText(rowItem.rowData, parent.columnKey)
                            }

                            Image {
                                visible: parent.columnKey === "coverArt" && status === Image.Ready
                                anchors.fill: parent
                                anchors.margins: 1
                                fillMode: Image.PreserveAspectCrop
                                clip: true
                                asynchronous: false
                                cache: true
                                source: parent.columnKey !== "coverArt" ? ""
                                        : rowItem.rowData.trackId > 0 ? ("image://mixhousecovers/" + rowItem.rowData.trackId)
                                        : rowItem.rowData.filePath ? ("image://mixhousecovers/file/" + encodeURIComponent(rowItem.rowData.filePath))
                                        : ""
                            }

                            Canvas {
                                id: overview
                                visible: parent.columnKey === "overview"
                                anchors.fill: parent
                                anchors.margins: 1
                                anchors.rightMargin: 8

                                property var buckets: null
                                property real bucketDpr: 1
                                readonly property string file: rowItem.rowData.filePath
                                readonly property double energy: rowItem.rowData.energy

                                function refresh() {
                                    if (!visible || width <= 1) {
                                        return
                                    }
                                    bucketDpr = Math.max(1, Screen.devicePixelRatio)
                                    buckets = projectModel.overviewBuckets(rowItem.rowIndex, Math.floor(width * bucketDpr))
                                    requestPaint()
                                }

                                onFileChanged: refresh()
                                onEnergyChanged: refresh()   // analysed just now
                                onWidthChanged: refresh()
                                onVisibleChanged: if (visible) refresh()   // the tab was behind the library
                                Component.onCompleted: refresh()

                                onPaint: {
                                    var ctx = getContext("2d")
                                    ctx.clearRect(0, 0, width, height)
                                    if (buckets && buckets.length > 0) {
                                        OverviewPaint.drawBuckets(ctx, width, height, buckets, bucketDpr, projectModel.waveformOverviewType)
                                    }
                                }
                            }

                            Rectangle {
                                id: keyBadge
                                readonly property var keyColor: keyColors.colorForKeyId(rowItem.rowData.keyId)
                                visible: parent.columnKey === "musicalKey" && rowItem.rowData.songKey !== "" && !!keyColor
                                anchors.verticalCenter: parent.verticalCenter
                                height: Math.min(parent.height - 6, badgeText.implicitHeight + 6)
                                width: Math.max(38, badgeText.implicitWidth + 16)
                                radius: 4
                                color: keyColor ? keyColor : "transparent"

                                Text {
                                    id: badgeText
                                    anchors.centerIn: parent
                                    text: rowItem.rowData.songKey
                                    color: "#1E1E24"
                                    font.bold: true
                                }
                            }

                            Row {
                                id: energyBlocks
                                readonly property int level: parent.columnKey === "energy" ? Math.round(rowItem.rowData.energy) : 0
                                visible: level > 0
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 6

                                Row {
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 1
                                    Repeater {
                                        model: energyBlocks.level
                                        Rectangle {
                                            width: 4
                                            height: 12
                                            radius: 1
                                            color: index >= 7 ? "#EF476F" : (index >= 4 ? "#FFE100" : "#50DF46")
                                        }
                                    }
                                }
                                StyledTextLabel {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: energyBlocks.level
                                }
                            }
                        }
                    }
                }
            }

            // Pinned: stays in view however far the columns scroll.
            Item {
                anchors.right: parent.right
                width: root.transitionWidth
                height: parent.height - 3

                StyledDropdown {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - 8
                    height: Math.min(parent.height - 4, 22)
                    visible: rowItem.rowHasTransition
                    model: root.transitionChoices
                    indeterminateText: rowItem.rowTransition === "" ? qsTrc("mixhouse", "None")
                                                                     : rowItem.rowTransition
                    currentIndex: indexOfValue(rowItem.rowTransition)
                    onActivated: function(i, value) { projectModel.setTransition(rowItem.rowIndex, value) }
                }
            }

            readonly property int rowIndex: index
            readonly property bool rowOverlay: model.overlay
            // The dropdown has a `model` of its own.
            readonly property string rowTransition: model.transition
            readonly property bool rowHasTransition: model.hasTransition

            // Click: scroll the timeline to the song. Drag: reorder.
            MouseArea {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: progressTrack.top
                width: root.columnsViewWidth
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

            // Right-click: find the song in the library.
            MouseArea {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: progressTrack.top
                width: root.columnsViewWidth
                acceptedButtons: Qt.RightButton
                onClicked: function(mouse) {
                    const inLibrary = model.trackId > 0
                    songMenu.show(Qt.point(mouse.x, mouse.y), [
                        { id: "0", title: qsTrc("mixhouse", "Show in Library"), enabled: inLibrary },
                        { id: "1", title: qsTrc("mixhouse", "Show Album in Library"), enabled: inLibrary },
                        { id: "2", title: qsTrc("mixhouse", "Show Artist in Library"), enabled: inLibrary }
                    ])
                }
                ContextMenuLoader {
                    id: songMenu
                    onHandleMenuItem: function(itemId) { projectModel.showInLibrary(index, parseInt(itemId)) }
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

    StyledScrollBar {
        id: columnsScrollBar
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        width: root.columnsViewWidth
        orientation: Qt.Horizontal
        visible: root.maxScroll > 0
        size: root.columnsViewWidth / root.columnsWidth
        position: root.horizontalScroll / root.columnsWidth
        policy: ScrollBar.AlwaysOn
        onPositionChanged: {
            if (pressed) {
                root.horizontalScroll = Math.max(0, Math.min(position * root.columnsWidth, root.maxScroll))
            }
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
