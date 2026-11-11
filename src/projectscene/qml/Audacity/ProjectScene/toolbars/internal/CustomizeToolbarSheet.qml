/*
* GlideMix: Customize Toolbar, the way Safari does it. The items not in the
* toolbar sit in a palette; drag one up into the toolbar row (where it lands
* is where it goes), drag a toolbar item along the row to move it, or out of
* the row to take it off. "Space" adds a gap. The default set at the bottom,
* dragged up (or clicked), puts the toolbar back as it came. Every change is
* saved at once, so the real toolbar changes behind the sheet.
*/
import QtQuick
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents

import Audacity.ProjectScene

StyledPopupView {
    id: root

    contentWidth: 980
    contentHeight: column.implicitHeight

    // Plain copies of the model's lists: { id, title, icon, shown, separator }.
    property var work: []
    property var defaults: []

    // What is being dragged: { from: "bar" | "palette" | "default", index }.
    property var dragging: null

    PlaybackToolBarCustomiseModel {
        id: customiseModel
    }

    onOpened: reload()

    property int displayStyle: 1

    function reload() {
        work = customiseModel.entries()
        defaults = customiseModel.defaultEntries()
        displayStyle = customiseModel.displayStyle()
    }

    // GlideMix's own icons (resources/toolbaricons/gm-*), as in the toolbar.
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

    function apply(list) {
        customiseModel.applyEntries(list)
        reload()
    }

    // Indexes into `work` of what the toolbar shows, in order.
    function barIndexes() {
        var out = []
        for (var i = 0; i < work.length; ++i) {
            if (!(work[i].shown || work[i].separator)) {
                continue
            }
            // A gap shows once, never first: as the toolbar draws it.
            if (work[i].separator && (out.length === 0 || work[out[out.length - 1]].separator)) {
                continue
            }
            out.push(i)
        }
        while (out.length && work[out[out.length - 1]].separator) {
            out.pop()
        }
        return out
    }

    function paletteIndexes() {
        var out = []
        for (var i = 0; i < work.length; ++i) {
            if (!work[i].shown && !work[i].separator && !work[i].flex) {
                out.push(i)
            }
        }
        return out
    }

    // Where in `work` a drop at (x, y) of the bar row goes: before the
    // nearest tile, or after it when dropped on its right half.
    function insertionIndex(p) {
        var best = -1, bestDist = 1e9, after = false
        for (var i = 0; i < barRepeater.count; ++i) {
            var tile = barRepeater.itemAt(i)
            if (!tile) {
                continue
            }
            var c = tile.mapToItem(barFlow, tile.width / 2, tile.height / 2)
            var d = Math.abs(c.x - p.x) + 2 * Math.abs(c.y - p.y)
            if (d < bestDist) {
                bestDist = d
                best = tile.workIndex
                after = p.x > c.x
            }
        }
        if (best < 0) {
            return work.length
        }
        return after ? best + 1 : best
    }

    function dropAt(scenePoint) {
        var d = dragging
        dragging = null
        if (!d) {
            return
        }
        var p = barFlow.mapFromItem(null, scenePoint.x, scenePoint.y)
        var onBar = p.x >= -20 && p.y >= -20 && p.x <= barFlow.width + 20 && p.y <= barFlow.height + 20

        if (d.from === "default") {
            if (onBar) {
                customiseModel.restoreDefault()
                reload()
            }
            return
        }

        var list = work.slice()
        if (d.from === "bar") {
            if (!onBar) {
                // Out of the toolbar: a space goes, an item goes back to the palette.
                if (list[d.index].separator || list[d.index].flex) {
                    list.splice(d.index, 1)
                } else {
                    list[d.index] = Object.assign({}, list[d.index], { shown: false })
                }
                apply(list)
                return
            }
            var to = insertionIndex(p)
            var moved = list.splice(d.index, 1)[0]
            if (to > d.index) {
                to -= 1
            }
            list.splice(to, 0, moved)
            apply(list)
            return
        }

        // From the palette (or the Space tile) into the toolbar.
        if (!onBar) {
            return
        }
        var at = insertionIndex(p)
        if (d.index === -2) {
            list.splice(at, 0, { id: "glidemix-flexible-space", title: "", icon: 0, shown: true, separator: false, flex: true })
        } else if (d.index < 0) {
            list.splice(at, 0, { id: "", title: "", icon: 0, shown: true, separator: true })
        } else {
            var item = Object.assign({}, list[d.index], { shown: true })
            list.splice(d.index, 1)
            if (at > d.index) {
                at -= 1
            }
            list.splice(at, 0, item)
        }
        apply(list)
    }

    // One toolbar item as it looks in the toolbar: its icon, or its name as a pill.
    component ItemFace: Item {
        property var entry: ({})
        readonly property string svgName: entry.flex ? "gm-flexspace" : root.gmIconFor(entry.id || "")
        readonly property bool hasIcon: entry.icon > 0 || svgName !== ""
        implicitWidth: entry.separator ? 14 : entry.flex ? 44 : (hasIcon ? 30 : Math.max(44, nameLabel.implicitWidth + 20))
        implicitHeight: 28

        Rectangle {
            anchors.fill: parent
            visible: !entry.separator
            radius: hasIcon ? 6 : height / 2
            color: entry.flex ? "transparent" : ui.theme.buttonColor
            border.color: ui.theme.strokeColor
            border.width: hasIcon && !entry.flex ? 0 : 1
        }
        StyledIconLabel {
            anchors.centerIn: parent
            visible: !entry.separator && entry.icon > 0 && svgName === ""
            iconCode: entry.icon || 0
        }
        Image {
            anchors.centerIn: parent
            visible: svgName !== ""
            width: 16
            height: 16
            sourceSize.width: 32
            sourceSize.height: 32
            source: svgName === "" ? "" : "qrc:/resources/toolbaricons/" + svgName + "-" + (ui.theme.isDark ? "dark" : "light") + ".svg"
        }
        StyledTextLabel {
            id: nameLabel
            anchors.centerIn: parent
            visible: !entry.separator && !hasIcon
            text: (entry.title || "").toUpperCase()
            font: ui.theme.bodyBoldFont
            elide: Text.ElideRight
            width: Math.min(implicitWidth, 140)
        }
        Rectangle {
            anchors.centerIn: parent
            visible: entry.separator === true
            width: 2
            height: parent.height - 6
            radius: 1
            color: ui.theme.strokeColor
        }
    }

    // A tile: the item's face and its name under it; press and drag to move it.
    component Tile: Item {
        id: tile
        property var entry: ({})
        property string from: "palette"
        property int workIndex: -1
        property bool showCaption: true
        width: Math.max(face.implicitWidth, showCaption ? 84 : 0)
        height: face.implicitHeight + (showCaption ? 30 : 0)
        opacity: root.dragging && root.dragging.from === from && root.dragging.index === workIndex ? 0.3 : 1

        ItemFace {
            id: face
            entry: tile.entry
            anchors.horizontalCenter: parent.horizontalCenter
            width: implicitWidth
            height: implicitHeight
        }
        StyledTextLabel {
            visible: tile.showCaption
            anchors.top: face.bottom
            anchors.topMargin: 4
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width
            text: tile.entry.separator ? qsTrc("projectscene", "Space")
                  : tile.entry.flex ? qsTrc("projectscene", "Flexible Space") : (tile.entry.title || "")
            font: ui.theme.bodyFont
            opacity: 0.7
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            horizontalAlignment: Text.AlignHCenter
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.OpenHandCursor
            preventStealing: true
            property bool moved: false
            onPressed: function(mouse) {
                moved = false
            }
            onPositionChanged: function(mouse) {
                var p = mapToItem(ghostLayer, mouse.x, mouse.y)
                if (!moved) {
                    moved = true
                    root.dragging = { from: tile.from, index: tile.workIndex }
                    ghost.entry = tile.entry
                }
                ghost.x = p.x - ghost.width / 2
                ghost.y = p.y - ghost.height / 2
            }
            onReleased: function(mouse) {
                if (moved) {
                    root.dropAt(mapToItem(null, mouse.x, mouse.y))
                } else if (tile.from === "palette") {
                    // A click adds it at the end of the toolbar.
                    root.dragging = { from: "palette", index: tile.workIndex }
                    root.dropAt(barFlow.mapToItem(null, barFlow.width + 10, barFlow.height - 5))
                }
            }
        }
    }

    ColumnLayout {
        id: column
        width: root.contentWidth
        spacing: 14

        StyledTextLabel {
            Layout.topMargin: 6
            text: qsTrc("projectscene", "Drag your favourite items into the toolbar…")
            font: ui.theme.largeBodyBoldFont
        }

        // Items not in the toolbar, and a Space.
        Flow {
            Layout.fillWidth: true
            spacing: 10

            Repeater {
                model: root.paletteIndexes()
                Tile {
                    required property var modelData
                    entry: root.work[modelData]
                    workIndex: modelData
                    from: "palette"
                }
            }
            Tile {
                entry: ({ separator: true })
                workIndex: -1
                from: "palette"
            }
            Tile {
                entry: ({ flex: true, id: "glidemix-flexible-space" })
                workIndex: -2
                from: "palette"
            }
        }

        StyledTextLabel {
            text: qsTrc("projectscene", "Toolbar (drag to move; drag out to remove)")
            font: ui.theme.bodyBoldFont
        }

        // The toolbar as it is now.
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: barFlow.implicitHeight + 20
            radius: 10
            color: ui.theme.backgroundSecondaryColor
            border.color: root.dragging ? ui.theme.accentColor : ui.theme.strokeColor
            border.width: 1

            Flow {
                id: barFlow
                x: 10
                y: 10
                width: parent.width - 20
                spacing: 6

                Repeater {
                    id: barRepeater
                    model: root.barIndexes()
                    Tile {
                        required property var modelData
                        entry: root.work[modelData]
                        workIndex: modelData
                        from: "bar"
                        showCaption: false
                    }
                }
            }
        }

        StyledTextLabel {
            text: qsTrc("projectscene", "…or drag the default set into the toolbar.")
            font: ui.theme.bodyBoldFont
        }

        // The default set, as one piece.
        Rectangle {
            id: defaultSet
            Layout.fillWidth: true
            implicitHeight: defaultRow.implicitHeight + 16
            radius: 10
            color: "transparent"
            border.color: ui.theme.strokeColor
            border.width: 1
            opacity: root.dragging && root.dragging.from === "default" ? 0.3 : 1

            Flow {
                id: defaultRow
                x: 8
                y: 8
                width: parent.width - 16
                spacing: 4
                Repeater {
                    model: root.defaults
                    ItemFace {
                        required property var modelData
                        visible: modelData.shown || modelData.separator
                        entry: modelData
                        width: visible ? implicitWidth : 0
                        height: implicitHeight
                    }
                }
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.OpenHandCursor
                preventStealing: true
                property bool moved: false
                onPressed: moved = false
                onPositionChanged: function(mouse) {
                    if (!moved) {
                        moved = true
                        root.dragging = { from: "default", index: -1 }
                        ghost.entry = ({ title: qsTrc("projectscene", "Default set") })
                    }
                    var p = mapToItem(ghostLayer, mouse.x, mouse.y)
                    ghost.x = p.x - ghost.width / 2
                    ghost.y = p.y - ghost.height / 2
                }
                onReleased: function(mouse) {
                    if (moved) {
                        root.dropAt(mapToItem(null, mouse.x, mouse.y))
                    } else {
                        customiseModel.restoreDefault()
                        root.reload()
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.bottomMargin: 6

            // Safari's Show: icon and text, icon only, text only.
            StyledTextLabel {
                text: qsTrc("projectscene", "Show:")
            }
            StyledDropdown {
                Layout.preferredWidth: 180
                model: [
                    { text: qsTrc("projectscene", "Icon and text"), value: 0 },
                    { text: qsTrc("projectscene", "Icon only"), value: 1 },
                    { text: qsTrc("projectscene", "Text only"), value: 2 }
                ]
                currentIndex: root.displayStyle
                onActivated: function(index, value) {
                    customiseModel.setDisplayStyle(value)
                    root.reload()
                }
            }

            Item { Layout.fillWidth: true }

            FlatButton {
                text: qsTrc("global", "Done")
                accentButton: true
                minWidth: 100
                onClicked: root.close()
            }
        }
    }

    // The dragged item follows the mouse above everything else.
    Item {
        id: ghostLayer
        anchors.fill: column
        z: 100

        ItemFace {
            id: ghost
            visible: root.dragging !== null
            width: implicitWidth
            height: implicitHeight
            opacity: 0.85
        }
    }
}
