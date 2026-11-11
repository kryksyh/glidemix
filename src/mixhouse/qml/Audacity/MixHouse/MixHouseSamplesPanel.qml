/*
 * GlideMix Samples tab (plan B1): the user's own sample and sound-effect
 * folders. Category tiles on top (the first folder level), a flat list of
 * every file below with its own columns (not the song columns), search,
 * pack filter and sorting. Our own layout; the idea of a samples browser
 * next to the music library was learnt from other DJ apps, nothing copied.
 */
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls 2.15
import Qt.labs.settings 1.1

import Muse.Ui 1.0
import Muse.UiComponents 1.0

import Audacity.MixHouse
import Audacity.UiComponents

Item {
    id: root

    property NavigationSection navigationSection: null

    // The master play button (Chris): while on, every sample selected (Up /
    // Down or a click) plays at once, also after a short one-shot ended.
    property bool autoPreview: false
    onAutoPreviewChanged: {
        if (autoPreview) {
            if (sampleList.count > 0) {
                if (sampleList.currentIndex < 0) sampleList.currentIndex = 0
                samplesModel.previewAt(sampleList.currentIndex, 0)
            }
        } else {
            samplesModel.stopPreview()
        }
    }

    MixHouseSamplesModel {
        id: samplesModel
    }

    FilePickerModel {
        id: folderPicker
        title: qsTrc("mixhouse", "Choose a folder with your samples")
    }

    function addFolder() {
        var chosen = folderPicker.selectDirectory()
        if (chosen) {
            samplesModel.addFolder(chosen)
        }
    }

    // Thin bars (Chris): a 1 px line every 2 px, each the loudest peak it covers.
    function paintThinBars(ctx, peaks, width, height, playedX, playedColor, restColor, restAlpha) {
        if (!peaks || peaks.length < 2) return
        var mid = height / 2
        var bars = Math.max(1, Math.floor(width / 2))
        for (var b = 0; b < bars; ++b) {
            var from = Math.floor(b * peaks.length / bars)
            var to = Math.max(from + 1, Math.floor((b + 1) * peaks.length / bars))
            var peak = 0
            for (var i = from; i < to && i < peaks.length; ++i) peak = Math.max(peak, peaks[i])
            var h = Math.max(1, peak / 255 * (height - 2))
            var x = b * 2
            var played = x < playedX
            ctx.fillStyle = played ? playedColor : restColor
            ctx.globalAlpha = played ? 1.0 : restAlpha
            ctx.fillRect(x, mid - h / 2, 1, h)
        }
    }

    function lengthText(seconds) {
        if (!(seconds > 0)) return "—"
        if (seconds < 10) return seconds.toFixed(2) + " s"
        var s = Math.round(seconds)
        return Math.floor(s / 60) + ":" + (s % 60 < 10 ? "0" : "") + (s % 60)
    }

    // The sample columns (Chris: not the song table's Artist, Title, Year...).
    readonly property var defaultColumns: [
        { key: "play",      title: "",                              width: 34 },
        { key: "wave",      title: qsTrc("mixhouse", "Waveform"),   width: 200 },
        { key: "name",      title: qsTrc("mixhouse", "Name"),       width: 300 },
        { key: "category",  title: qsTrc("mixhouse", "Category"),   width: 110 },
        { key: "pack",      title: qsTrc("mixhouse", "Pack"),       width: 220 },
        { key: "type",      title: qsTrc("mixhouse", "Type"),       width: 80 },
        { key: "bpm",       title: qsTrc("mixhouse", "BPM"),        width: 75 },
        { key: "key",       title: qsTrc("mixhouse", "Key"),        width: 70 },
        { key: "bars",      title: qsTrc("mixhouse", "Bars"),       width: 70 },
        { key: "length",    title: qsTrc("mixhouse", "Length"),     width: 70 },
        { key: "format",    title: qsTrc("mixhouse", "Format"),     width: 85 },
        { key: "location",  title: qsTrc("mixhouse", "Location"),   width: 380 },
        { key: "dateAdded", title: qsTrc("mixhouse", "Date Added"), width: 100 }
    ]
    // Like the library's header: drag a header to move its column, drag its
    // right edge to resize, right-click to show or hide columns. Saved.
    property var columns: defaultColumns
    readonly property var shownColumns: columns.filter(function (c) { return c.visible !== false })
    readonly property int tableWidth: {
        var w = 0
        for (var i = 0; i < shownColumns.length; ++i) w += shownColumns[i].width
        return w
    }

    Settings {
        id: columnSettings
        category: "MixHouseSamples"
        property string layoutJson: ""
    }

    function saveColumns() {
        columnSettings.layoutJson = JSON.stringify(columns.map(function (c) {
            return { key: c.key, width: c.width, visible: c.visible !== false }
        }))
    }

    function restoreColumns() {
        var byKey = {}
        for (var i = 0; i < defaultColumns.length; ++i) byKey[defaultColumns[i].key] = defaultColumns[i]
        var out = []
        try {
            var saved = JSON.parse(columnSettings.layoutJson || "[]")
            for (var j = 0; j < saved.length; ++j) {
                var d = byKey[saved[j].key]
                if (!d) continue
                out.push(Object.assign({}, d, { width: Math.max(30, saved[j].width), visible: saved[j].visible }))
                delete byKey[saved[j].key]
            }
        } catch (e) {}
        // Columns added in a newer version: after the one they follow.
        for (var k = 0; k < defaultColumns.length; ++k) {
            var extra = byKey[defaultColumns[k].key]
            if (!extra) continue
            var at = out.length
            if (k > 0) {
                for (var m = 0; m < out.length; ++m) if (out[m].key === defaultColumns[k - 1].key) at = m + 1
            }
            out.splice(at, 0, Object.assign({}, extra))
        }
        columns = out
    }

    function moveColumn(fromKey, toKey) {
        if (fromKey === toKey) return
        var c = columns.slice()
        var from = c.findIndex(function (x) { return x.key === fromKey })
        var to = c.findIndex(function (x) { return x.key === toKey })
        if (from < 0 || to < 0) return
        var moved = c.splice(from, 1)[0]
        c.splice(to, 0, moved)
        columns = c
        saveColumns()
    }

    function setColumnWidth(key, w) {
        var c = columns.slice()
        for (var i = 0; i < c.length; ++i) if (c[i].key === key) c[i] = Object.assign({}, c[i], { width: Math.max(30, Math.round(w)) })
        columns = c
    }

    function toggleColumn(key) {
        var c = columns.slice()
        for (var i = 0; i < c.length; ++i) if (c[i].key === key) c[i] = Object.assign({}, c[i], { visible: c[i].visible === false })
        columns = c
        saveColumns()
    }

    Component.onCompleted: restoreColumns()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 8

        // Folders, search and the scan state.
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            FlatButton {
                text: qsTrc("mixhouse", "Add sample folder…")
                icon: IconCode.PLUS
                orientation: Qt.Horizontal
                onClicked: root.addFolder()
            }

            FlatButton {
                id: foldersButton
                text: qsTrc("mixhouse", "Folders")
                enabled: samplesModel.folders.length > 0
                onClicked: {
                    var items = []
                    for (var i = 0; i < samplesModel.folders.length; ++i) {
                        items.push({ id: "remove:" + samplesModel.folders[i],
                                     title: qsTrc("mixhouse", "Remove %1").arg(samplesModel.folders[i]) })
                    }
                    items.push({})
                    items.push({ id: "rescan", title: qsTrc("mixhouse", "Scan all folders again") })
                    foldersMenu.toggleOpened(items)
                }

                StyledMenuLoader {
                    id: foldersMenu
                    onHandleMenuItem: function(itemId) {
                        if (itemId === "rescan") {
                            samplesModel.rescan()
                        } else if (itemId.indexOf("remove:") === 0) {
                            // Forgets the folder only; the files stay on disk.
                            samplesModel.removeFolder(itemId.slice(7))
                        }
                    }
                }
            }

            TextInputField {
                Layout.preferredWidth: 280
                hint: qsTrc("mixhouse", "Search samples")
                onTextChanged: function(newTextValue) { samplesModel.search = newTextValue }
            }

            StyledDropdown {
                Layout.preferredWidth: 260
                model: [qsTrc("mixhouse", "All packs")].concat(samplesModel.packs)
                currentIndex: samplesModel.pack === "" ? 0 : samplesModel.packs.indexOf(samplesModel.pack) + 1
                onActivated: function(index, value) { samplesModel.pack = index === 0 ? "" : samplesModel.packs[index - 1] }
            }

            StyledTextLabel {
                text: qsTrc("mixhouse", "%1 samples").arg(sampleList.count)
                opacity: 0.7
            }

            // The library's preview level (one setting for songs and samples).
            StyledTextLabel {
                Layout.leftMargin: 12
                text: qsTrc("mixhouse", "Preview volume")
                opacity: 0.7
            }
            DjSlider {
                Layout.preferredWidth: 130
                from: 0
                to: 1
                stepSize: 0.01
                value: samplesModel.previewVolume
                onMoved: samplesModel.previewVolume = value
                ToolTip.visible: hovered || pressed
                ToolTip.text: qsTrc("mixhouse", "Preview volume: %1%").arg(Math.round(value * 100))
            }

            Item { Layout.fillWidth: true }

            StyledTextLabel {
                visible: samplesModel.scanning
                text: samplesModel.scanText !== "" ? samplesModel.scanText : qsTrc("mixhouse", "Scanning…")
                opacity: 0.7
            }
        }

        // Category tiles: the first folder level of the added folders.
        Flow {
            Layout.fillWidth: true
            spacing: 6
            visible: samplesModel.totalCount > 0

            Repeater {
                model: samplesModel.categories

                Rectangle {
                    required property var modelData
                    readonly property bool selected: samplesModel.category === modelData.name

                    width: tileRow.implicitWidth + 20
                    height: 28
                    radius: 6
                    color: selected ? ui.theme.accentColor : ui.theme.buttonColor
                    opacity: selected ? 1.0 : 0.9
                    border.width: 1
                    border.color: ui.theme.strokeColor

                    Row {
                        id: tileRow
                        anchors.centerIn: parent
                        spacing: 8
                        StyledTextLabel { text: modelData.title; font.bold: selected }
                        StyledTextLabel { text: modelData.count; opacity: 0.6 }
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: samplesModel.category = modelData.name
                    }
                }
            }
        }

        // The selected sample (our own layout, after the sample players
        // Chris showed): play/pause and time, its name, folder and chips,
        // and a big waveform to click into.
        Rectangle {
            id: detail
            Layout.fillWidth: true
            Layout.preferredHeight: 92
            visible: samplesModel.totalCount > 0 && sampleList.count > 0
            radius: 8
            color: ui.theme.backgroundSecondaryColor
            border.width: 1
            border.color: ui.theme.strokeColor

            readonly property int row: sampleList.currentIndex >= 0 ? sampleList.currentIndex : 0
            // Re-read when the row, the list or a waveform changes.
            readonly property var info: {
                samplesModel.overviewsReady
                sampleList.count
                return samplesModel.sampleInfo(detail.row)
            }
            readonly property bool playing: info.path !== undefined && samplesModel.previewPath === info.path
            readonly property real seconds: info.length !== undefined ? info.length : 0

            RowLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 14

                // Play / pause and the time.
                ColumnLayout {
                    spacing: 4
                    Layout.alignment: Qt.AlignVCenter
                    // Master play: on = every selected sample plays (Up / Down).
                    FlatButton {
                        Layout.alignment: Qt.AlignHCenter
                        width: 44
                        height: 44
                        icon: root.autoPreview ? IconCode.STOP : IconCode.PLAY
                        accentButton: root.autoPreview
                        toolTipTitle: root.autoPreview ? qsTrc("mixhouse", "Stop previewing")
                                                       : qsTrc("mixhouse", "Preview every sample you select")
                        toolTipDescription: qsTrc("mixhouse", "While on, Up and Down play each sample as you reach it. Space switches it on and off.")
                        onClicked: {
                            sampleList.currentIndex = detail.row
                            sampleList.forceActiveFocus()
                            root.autoPreview = !root.autoPreview
                        }
                    }
                    StyledTextLabel {
                        Layout.alignment: Qt.AlignHCenter
                        text: (detail.playing ? root.lengthText(samplesModel.previewPosition * detail.seconds) : "0:00")
                              + " / " + root.lengthText(detail.seconds)
                        opacity: 0.7
                        font.pixelSize: 11
                    }
                }

                // Name, folder, chips.
                ColumnLayout {
                    Layout.preferredWidth: 300
                    Layout.maximumWidth: 300
                    spacing: 4
                    StyledTextLabel {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignLeft
                        text: detail.info.name !== undefined ? detail.info.name : ""
                        font.bold: true
                    }
                    StyledTextLabel {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignLeft
                        elide: Text.ElideLeft
                        text: detail.info.location !== undefined ? detail.info.location : ""
                        opacity: 0.6
                        font.pixelSize: 11
                    }
                    Row {
                        spacing: 6
                        Repeater {
                            model: {
                                var i = detail.info, chips = []
                                if (i.type) chips.push(i.type)
                                if (i.bpm > 0) chips.push((Math.round(i.bpm * 10) / 10) + " BPM")
                                if (i.key) chips.push(i.key)
                                if (i.bars > 0) chips.push(i.bars + (i.bars === 1 ? qsTrc("mixhouse", " bar") : qsTrc("mixhouse", " bars")))
                                if (i.length > 0) chips.push(root.lengthText(i.length))
                                if (i.format) chips.push(String(i.format).toUpperCase())
                                return chips
                            }
                            Rectangle {
                                required property var modelData
                                width: chipText.implicitWidth + 14
                                height: 20
                                radius: 10
                                color: "transparent"
                                border.width: 1
                                border.color: ui.theme.strokeColor
                                StyledTextLabel {
                                    id: chipText
                                    anchors.centerIn: parent
                                    text: modelData
                                    font.pixelSize: 11
                                }
                            }
                        }
                    }
                }

                // The big waveform: played part in the accent colour, a
                // playhead, click to play from there.
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: 6
                    color: Qt.rgba(0, 0, 0, 0.25)

                    Canvas {
                        id: bigWave
                        anchors.fill: parent
                        anchors.margins: 6
                        readonly property var peaks: detail.info.overview
                        readonly property real played: detail.playing ? samplesModel.previewPosition : 0
                        onPeaksChanged: requestPaint()
                        onPlayedChanged: requestPaint()
                        onWidthChanged: requestPaint()
                        onPaint: {
                            var ctx = getContext("2d")
                            ctx.reset()
                            root.paintThinBars(ctx, bigWave.peaks, width, height, bigWave.played * width,
                                               ui.theme.accentColor, ui.theme.fontPrimaryColor, 0.45)
                        }
                        Rectangle {
                            visible: detail.playing
                            x: samplesModel.previewPosition * bigWave.width
                            width: 2
                            height: bigWave.height
                            color: "#FF4D4D"
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            enabled: !detail.info.missing
                            onClicked: function(mouse) {
                                sampleList.currentIndex = detail.row
                                sampleList.forceActiveFocus()
                                samplesModel.previewAt(detail.row, mouse.x / width)
                            }
                        }
                    }
                }
            }
        }

        // The list.
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            Column {
                anchors.centerIn: parent
                spacing: 12
                visible: samplesModel.totalCount === 0

                StyledTextLabel {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: samplesModel.scanning ? qsTrc("mixhouse", "Reading your samples…")
                                                : qsTrc("mixhouse", "No samples yet")
                    font: ui.theme.largeBodyBoldFont
                }
                StyledTextLabel {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 460
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTrc("mixhouse", "Add the folders with your own samples and sound effects. Their subfolders become the categories (Kicks, Risers, FX…).")
                }
                FlatButton {
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: !samplesModel.scanning
                    accentButton: true
                    text: qsTrc("mixhouse", "Add sample folder…")
                    onClicked: root.addFolder()
                }
            }

            ListView {
                id: sampleList

                anchors.fill: parent
                visible: samplesModel.totalCount > 0
                clip: true
                model: samplesModel
                contentWidth: root.tableWidth
                flickableDirection: Flickable.AutoFlickIfNeeded
                boundsBehavior: Flickable.StopAtBounds
                headerPositioning: ListView.OverlayHeader
                // Up / Down move the selection; while a sample plays, the
                // preview moves with it. Space plays or stops the selected one.
                focus: true
                keyNavigationEnabled: false
                highlightMoveDuration: 0
                currentIndex: -1
                onCurrentIndexChanged: {
                    if (currentIndex < 0) return
                    if (root.autoPreview) {
                        samplesModel.previewAt(currentIndex, 0)
                    } else if (samplesModel.previewPath !== "" && samplesModel.previewRow() !== currentIndex) {
                        samplesModel.togglePreview(currentIndex)
                    }
                }
                // The app's own shortcuts (track up/down, play/stop on Space,
                // go to start/end) take these keys first; claiming them here
                // gives them to the list, as the library does.
                activeFocusOnTab: true
                Keys.onShortcutOverride: function(event) {
                    if ([Qt.Key_Up, Qt.Key_Down, Qt.Key_Left, Qt.Key_Right, Qt.Key_PageUp, Qt.Key_PageDown, Qt.Key_Home, Qt.Key_End, Qt.Key_Space].indexOf(event.key) >= 0
                            && !(event.modifiers & (Qt.ControlModifier | Qt.MetaModifier | Qt.AltModifier))) {
                        event.accepted = true
                    }
                }
                Keys.onPressed: function(event) {
                    // Left / Right: the previous or next category tile.
                    if (event.key === Qt.Key_Left || event.key === Qt.Key_Right) {
                        const cats = samplesModel.categories
                        let at = 0
                        for (let c = 0; c < cats.length; ++c) {
                            if (cats[c].name === samplesModel.category) at = c
                        }
                        at = event.key === Qt.Key_Left ? Math.max(0, at - 1) : Math.min(cats.length - 1, at + 1)
                        samplesModel.category = cats[at].name
                        currentIndex = sampleList.count > 0 ? 0 : -1
                        positionViewAtBeginning()
                        event.accepted = true
                        return
                    }
                    const rows = sampleList.count
                    if (rows <= 0) {
                        return
                    }
                    const page = Math.max(1, Math.floor(height / 26) - 2)
                    const from = currentIndex
                    let to = -1
                    switch (event.key) {
                    case Qt.Key_Up: to = Math.max(0, from - 1); break
                    case Qt.Key_Down: to = Math.min(rows - 1, from + 1); break
                    case Qt.Key_PageUp: to = Math.max(0, from - page); break
                    case Qt.Key_PageDown: to = Math.min(rows - 1, Math.max(0, from) + page); break
                    case Qt.Key_Home: to = 0; break
                    case Qt.Key_End: to = rows - 1; break
                    case Qt.Key_Space:
                        // The master play button's key.
                        if (from < 0) currentIndex = 0
                        root.autoPreview = !root.autoPreview
                        event.accepted = true
                        return
                    default: return
                    }
                    currentIndex = to          // a running preview follows (onCurrentIndexChanged)
                    positionViewAtIndex(to, ListView.Contain)
                    event.accepted = true
                }
                ScrollBar.vertical: StyledScrollBar {}
                ScrollBar.horizontal: StyledScrollBar {}

                header: Rectangle {
                    id: headerBar
                    z: 3
                    width: Math.max(sampleList.width, root.tableWidth)
                    height: 28
                    color: ui.theme.backgroundSecondaryColor

                    property string dragKey: ""
                    property real dragX: 0

                    // Right-click: show or hide columns.
                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.RightButton
                        onClicked: function(mouse) {
                            var items = []
                            for (var i = 0; i < root.columns.length; ++i) {
                                var c = root.columns[i]
                                if (c.key === "play") continue
                                items.push({ id: c.key, title: c.title, checkable: true, checked: c.visible !== false })
                            }
                            columnMenu.show(Qt.point(mouse.x, mouse.y), items)
                        }
                    }
                    ContextMenuLoader {
                        id: columnMenu
                        onHandleMenuItem: function(itemId) { root.toggleColumn(itemId) }
                    }

                    Row {
                        id: headerRow
                        x: -sampleList.contentX
                        height: parent.height
                        Repeater {
                            model: root.shownColumns
                            Item {
                                id: headerCell
                                required property var modelData
                                width: modelData.width
                                height: parent.height

                                // Where a dragged column would land.
                                Rectangle {
                                    visible: headerBar.dragKey !== "" && headerBar.dragKey !== headerCell.modelData.key
                                             && headerBar.dragX >= headerCell.x && headerBar.dragX < headerCell.x + headerCell.width
                                    width: 2
                                    height: parent.height
                                    color: ui.theme.accentColor
                                }
                                StyledTextLabel {
                                    anchors.left: parent.left
                                    anchors.leftMargin: 8
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: parent.width - 14
                                    horizontalAlignment: Text.AlignLeft
                                    elide: Text.ElideRight
                                    text: (modelData.key === "play" ? "" : "⠿ ") + modelData.title
                                          + (samplesModel.sortKey === modelData.key ? (samplesModel.ascending ? "  ▲" : "  ▼") : "")
                                    font.bold: samplesModel.sortKey === modelData.key
                                    opacity: headerBar.dragKey === modelData.key ? 0.5 : 1.0
                                }
                                // Click sorts; drag moves the column.
                                MouseArea {
                                    anchors.fill: parent
                                    anchors.rightMargin: 6
                                    cursorShape: pressed && headerBar.dragKey !== "" ? Qt.ClosedHandCursor : Qt.PointingHandCursor
                                    property real pressX: 0
                                    onPressed: function(mouse) { pressX = mouse.x }
                                    onPositionChanged: function(mouse) {
                                        if (headerCell.modelData.key === "play") return
                                        if (headerBar.dragKey === "" && Math.abs(mouse.x - pressX) > 6) {
                                            headerBar.dragKey = headerCell.modelData.key
                                        }
                                        if (headerBar.dragKey !== "") {
                                            headerBar.dragX = headerCell.x + mouse.x
                                        }
                                    }
                                    onReleased: {
                                        if (headerBar.dragKey === "") {
                                            if (headerCell.modelData.key !== "play" && headerCell.modelData.key !== "wave") {
                                                samplesModel.sortBy(headerCell.modelData.key)
                                            }
                                            return
                                        }
                                        var target = ""
                                        for (var i = 0; i < headerRow.children.length; ++i) {
                                            var cell = headerRow.children[i]
                                            if (cell.modelData && headerBar.dragX >= cell.x && headerBar.dragX < cell.x + cell.width) {
                                                target = cell.modelData.key
                                            }
                                        }
                                        // Cleared first: moving rebuilds these header cells.
                                        const dragged = headerBar.dragKey
                                        headerBar.dragKey = ""
                                        if (target !== "" && target !== "play") {
                                            root.moveColumn(dragged, target)
                                        }
                                    }
                                }
                                // Right edge: resize.
                                MouseArea {
                                    anchors.right: parent.right
                                    width: 6
                                    height: parent.height
                                    cursorShape: Qt.SizeHorCursor
                                    property real startX: 0
                                    property real startWidth: 0
                                    onPressed: function(mouse) {
                                        startX = mapToItem(headerBar, mouse.x, 0).x
                                        startWidth = headerCell.width
                                    }
                                    onPositionChanged: function(mouse) {
                                        if (pressed) {
                                            root.setColumnWidth(headerCell.modelData.key,
                                                                startWidth + mapToItem(headerBar, mouse.x, 0).x - startX)
                                        }
                                    }
                                    onReleased: root.saveColumns()
                                }
                                Rectangle {
                                    anchors.right: parent.right
                                    width: 1
                                    height: parent.height - 10
                                    anchors.verticalCenter: parent.verticalCenter
                                    color: ui.theme.strokeColor
                                }
                            }
                        }
                    }
                }

                delegate: Rectangle {
                    id: rowItem

                    required property int index
                    required property string name
                    required property string category
                    required property string pack
                    required property string type
                    required property real bpm
                    required property string key
                    required property int bars
                    required property real length
                    required property string format
                    required property string location
                    required property string dateAdded
                    required property bool missing
                    required property string path
                    required property var overview

                    width: Math.max(sampleList.width, root.tableWidth)
                    height: 26
                    color: ListView.isCurrentItem ? Qt.rgba(ui.theme.accentColor.r, ui.theme.accentColor.g, ui.theme.accentColor.b, 0.35)
                                                  : (index % 2 ? ui.theme.backgroundSecondaryColor : "transparent")
                    opacity: missing ? 0.45 : 1.0

                    function cell(col) {
                        switch (col) {
                        case "name": return name
                        case "category": return category
                        case "pack": return pack
                        case "type": return type
                        case "bpm": return bpm > 0 ? (Math.round(bpm * 10) / 10).toString() : "—"
                        case "key": return key !== "" ? key : "—"
                        case "bars": return bars > 0 ? bars.toString() : "—"
                        case "length": return root.lengthText(length)
                        case "format": return format.toUpperCase()
                        case "location": return location
                        case "dateAdded": return dateAdded
                        }
                        return ""
                    }

                    Row {
                        height: parent.height
                        Repeater {
                            model: root.shownColumns
                            Item {
                                required property var modelData
                                width: modelData.width
                                height: parent.height
                                // Play / stop (B1b): a loop at the mix's tempo, over and over.
                                FlatButton {
                                    visible: modelData.key === "play"
                                    anchors.centerIn: parent
                                    width: 24
                                    height: 20
                                    transparent: true
                                    icon: samplesModel.previewPath === rowItem.path ? IconCode.STOP : IconCode.PLAY
                                    accentButton: samplesModel.previewPath === rowItem.path
                                    enabled: !rowItem.missing
                                    onClicked: {
                                        // Selecting the row already moves a running preview to it.
                                        const movesPreview = samplesModel.previewPath !== "" && samplesModel.previewPath !== rowItem.path
                                        sampleList.currentIndex = rowItem.index
                                        sampleList.forceActiveFocus()
                                        if (!movesPreview) {
                                            samplesModel.togglePreview(rowItem.index)
                                        }
                                    }
                                }
                                // Waveform (B1b, like the library's): click to play from there.
                                Canvas {
                                    id: wave
                                    visible: modelData.key === "wave"
                                    anchors.fill: parent
                                    anchors.margins: 3
                                    readonly property var peaks: rowItem.overview
                                    readonly property bool playing: samplesModel.previewPath === rowItem.path
                                    onPeaksChanged: requestPaint()
                                    onPlayingChanged: requestPaint()
                                    onWidthChanged: requestPaint()
                                    onPaint: {
                                        var ctx = getContext("2d")
                                        ctx.reset()
                                        root.paintThinBars(ctx, wave.peaks, width, height, wave.playing ? width + 1 : -1,
                                                           ui.theme.accentColor, ui.theme.fontPrimaryColor, 0.55)
                                    }
                                    Rectangle {
                                        visible: wave.playing
                                        x: samplesModel.previewPosition * wave.width
                                        width: 2
                                        height: wave.height
                                        color: "#FF4D4D"
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: function(mouse) {
                                            const movesPreview = samplesModel.previewPath !== "" && samplesModel.previewPath !== rowItem.path
                                            sampleList.currentIndex = rowItem.index
                                            sampleList.forceActiveFocus()
                                            // A preview moved here by the selection starts at the top; then jump.
                                            samplesModel.previewAt(rowItem.index, mouse.x / width)
                                        }
                                    }
                                }
                                StyledTextLabel {
                                    visible: modelData.key !== "play" && modelData.key !== "wave"
                                    anchors.left: parent.left
                                    anchors.leftMargin: 8
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: parent.width - 12
                                    horizontalAlignment: Text.AlignLeft
                                    // Location: the end of the path matters, so it shortens from the left.
                                    elide: modelData.key === "location" ? Text.ElideLeft : Text.ElideRight
                                    text: visible ? rowItem.cell(modelData.key) : ""
                                }
                            }
                        }
                    }

                    // Double-click plays (or stops) the sample too.
                    TapHandler {
                        acceptedButtons: Qt.LeftButton
                        onTapped: {
                            sampleList.currentIndex = rowItem.index
                            sampleList.forceActiveFocus()
                        }
                        onDoubleTapped: {
                            if (samplesModel.previewRow() !== rowItem.index) {
                                samplesModel.togglePreview(rowItem.index)
                            }
                        }
                    }

                    MouseArea {
                        id: rowMouse
                        anchors.fill: parent
                        acceptedButtons: Qt.RightButton
                        hoverEnabled: true
                        onClicked: function(mouse) {
                            rowMenu.row = rowItem.index
                            rowMenu.show(Qt.point(mouse.x, mouse.y), [
                                { id: "reveal", title: Qt.platform.os === "windows" ? qsTrc("mixhouse", "Show in Explorer")
                                                                                   : qsTrc("mixhouse", "Show in Finder") }
                            ])
                        }
                        // The full folder (the Location column is shortened from the left).
                        ToolTip.visible: containsMouse
                        ToolTip.text: rowItem.missing ? qsTrc("mixhouse", "Its disk is not connected: %1").arg(rowItem.location)
                                                      : rowItem.location
                        ToolTip.delay: 900
                    }
                }

                ContextMenuLoader {
                    id: rowMenu
                    property int row: -1
                    onHandleMenuItem: function(itemId) {
                        if (itemId === "reveal") {
                            samplesModel.revealInFileBrowser(row)
                        }
                    }
                }
            }
        }
    }
}
