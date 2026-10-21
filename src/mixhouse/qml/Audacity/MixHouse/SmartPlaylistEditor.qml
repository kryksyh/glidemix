/*
 * GlideMix: the smart playlist editor -- rekordbox's Intelligent Playlist
 * window / Lexicon's Smartlist rules. A playlist matches all (or any) of its
 * rules; "or…" adds alternatives to one rule (Lexicon's OR clause).
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents

Popup {
    id: root

    property var tracksModel: null
    property var colorPalette: []
    //! Called with the playlist id after a save.
    signal saved(int playlistId)

    property var def: ({})
    property var fields: []
    property int matchCount: 0

    readonly property var sourceChoices: [
        { value: "collection", text: qsTrc("mixhouse", "Music Collection") },
        { value: "lexicon", text: qsTrc("mixhouse", "Lexicon DJ") },
        { value: "rekordbox", text: qsTrc("mixhouse", "Rekordbox XML") },
        { value: "all", text: qsTrc("mixhouse", "All libraries") }
    ]
    readonly property var matchChoices: [
        { value: "all", text: qsTrc("mixhouse", "all of these rules") },
        { value: "any", text: qsTrc("mixhouse", "any of these rules") }
    ]
    readonly property var unitChoices: [
        { value: "days", text: qsTrc("mixhouse", "days") },
        { value: "weeks", text: qsTrc("mixhouse", "weeks") },
        { value: "months", text: qsTrc("mixhouse", "months") },
        { value: "years", text: qsTrc("mixhouse", "years") }
    ]

    function openFor(playlistId) {
        fields = tracksModel.smartFieldList()
        def = tracksModel.smartPlaylistDefinition(playlistId)
        refreshCount()
        open()
    }

    function fieldType(fieldId) {
        for (var i = 0; i < fields.length; ++i) {
            if (fields[i].value === fieldId) {
                return fields[i].type
            }
        }
        return "text"
    }

    // Edits go through a copy: reassigning `def` redraws the rows.
    function update(mutator) {
        var d = JSON.parse(JSON.stringify(def))
        mutator(d)
        def = d
        countTimer.restart()
    }

    function setRule(g, r, key, value) {
        update(function(d) {
            var rule = d.groups[g][r]
            rule[key] = value
            if (key === "field") {
                // A new field: its first operator, and no stale value.
                var ops = tracksModel.smartOperatorList(fieldType(value))
                rule.op = ops.length > 0 ? ops[0].value : ""
                rule.value = ""
                rule.value2 = ""
            }
        })
    }

    function refreshCount() {
        matchCount = tracksModel ? tracksModel.smartPlaylistPreviewCount(def) : 0
    }

    function save() {
        var id = tracksModel.saveSmartPlaylist(def)
        if (id >= 0) {
            close()
            saved(id)
        }
    }

    Timer {
        id: countTimer
        interval: 350
        onTriggered: root.refreshCount()
    }

    anchors.centerIn: Overlay.overlay
    width: 760
    height: 560
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape

    background: Rectangle {
        color: ui.theme.backgroundPrimaryColor
        border.width: 1
        border.color: ui.theme.strokeColor
        radius: 4
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 10

        StyledTextLabel {
            Layout.fillWidth: true
            font.bold: true
            horizontalAlignment: Text.AlignLeft
            text: root.def.id >= 0 ? qsTrc("mixhouse", "Edit Smart Playlist") : qsTrc("mixhouse", "New Smart Playlist")
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            StyledTextLabel { text: qsTrc("mixhouse", "Name"); opacity: 0.7 }
            TextInputField {
                Layout.fillWidth: true
                currentText: root.def.name || ""
                onTextEdited: function(t) { root.def.name = t }
            }
            StyledTextLabel { text: qsTrc("mixhouse", "from"); opacity: 0.7 }
            StyledDropdown {
                Layout.preferredWidth: 170
                model: root.sourceChoices
                textRole: "text"
                valueRole: "value"
                currentIndex: indexOfValue(root.def.source || "collection")
                onActivated: function(index, value) { root.update(function(d) { d.source = value }) }
            }
        }

        RowLayout {
            spacing: 8
            StyledTextLabel { text: qsTrc("mixhouse", "Match"); opacity: 0.7 }
            StyledDropdown {
                Layout.preferredWidth: 200
                model: root.matchChoices
                textRole: "text"
                valueRole: "value"
                currentIndex: indexOfValue(root.def.match || "all")
                onActivated: function(index, value) { root.update(function(d) { d.match = value }) }
            }
        }

        SeparatorLine {}

        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentHeight: rulesColumn.height
            ScrollBar.vertical: StyledScrollBar {}

            Column {
                id: rulesColumn
                width: parent.width
                spacing: 8

                Repeater {
                    model: root.def.groups || []

                    delegate: Column {
                        id: groupItem
                        required property var modelData
                        required property int index
                        readonly property int groupIndex: index
                        width: rulesColumn.width
                        spacing: 4

                        Repeater {
                            model: groupItem.modelData

                            delegate: RowLayout {
                                id: ruleRow
                                required property var modelData
                                required property int index
                                readonly property string type: root.fieldType(modelData.field)
                                readonly property string op: modelData.op || ""
                                readonly property bool needsValue: ["isEmpty", "isNotEmpty", "isTrue", "isFalse"].indexOf(op) < 0
                                width: rulesColumn.width
                                spacing: 6

                                StyledTextLabel {
                                    Layout.preferredWidth: 22
                                    text: ruleRow.index > 0 ? qsTrc("mixhouse", "or") : ""
                                    opacity: 0.6
                                }
                                StyledDropdown {
                                    Layout.preferredWidth: 150
                                    model: root.fields
                                    textRole: "text"
                                    valueRole: "value"
                                    popupItemsCount: 14
                                    currentIndex: indexOfValue(ruleRow.modelData.field)
                                    onActivated: function(i, value) { root.setRule(groupItem.groupIndex, ruleRow.index, "field", value) }
                                }
                                StyledDropdown {
                                    Layout.preferredWidth: 170
                                    model: root.tracksModel ? root.tracksModel.smartOperatorList(ruleRow.type) : []
                                    textRole: "text"
                                    valueRole: "value"
                                    currentIndex: indexOfValue(ruleRow.op)
                                    onActivated: function(i, value) { root.setRule(groupItem.groupIndex, ruleRow.index, "op", value) }
                                }

                                // The value: a color, a playlist, or text / numbers.
                                StyledDropdown {
                                    visible: ruleRow.needsValue && ruleRow.type === "color"
                                    Layout.fillWidth: true
                                    model: root.colorPalette.map(function(c) { return { value: String(c.hex), text: c.name } })
                                    textRole: "text"
                                    valueRole: "value"
                                    currentIndex: indexOfValue(ruleRow.modelData.value)
                                    onActivated: function(i, value) { root.setRule(groupItem.groupIndex, ruleRow.index, "value", value) }
                                }
                                StyledDropdown {
                                    visible: ruleRow.needsValue && ruleRow.type === "playlist"
                                    Layout.fillWidth: true
                                    model: visible && root.tracksModel ? root.tracksModel.smartPlaylistChoices(root.def.source || "collection") : []
                                    textRole: "text"
                                    valueRole: "value"
                                    popupItemsCount: 14
                                    currentIndex: indexOfValue(ruleRow.modelData.value)
                                    onActivated: function(i, value) { root.setRule(groupItem.groupIndex, ruleRow.index, "value", value) }
                                }
                                TextInputField {
                                    visible: ruleRow.needsValue && ruleRow.type !== "color" && ruleRow.type !== "playlist"
                                    Layout.fillWidth: true
                                    currentText: ruleRow.modelData.value || ""
                                    hint: ruleRow.type === "date" && (ruleRow.op === "after" || ruleRow.op === "before") ? "2026-10-01"
                                          : ruleRow.type === "key" ? "8A" : ""
                                    onTextEdited: function(t) {
                                        root.def.groups[groupItem.groupIndex][ruleRow.index].value = t
                                        countTimer.restart()
                                    }
                                }
                                // Second value: "between", "within ±%", "in the last N units".
                                TextInputField {
                                    visible: ruleRow.op === "between" || ruleRow.op === "near"
                                    Layout.preferredWidth: 80
                                    currentText: ruleRow.modelData.value2 || ""
                                    hint: ruleRow.op === "near" ? "± %" : qsTrc("mixhouse", "and")
                                    onTextEdited: function(t) {
                                        root.def.groups[groupItem.groupIndex][ruleRow.index].value2 = t
                                        countTimer.restart()
                                    }
                                }
                                StyledDropdown {
                                    visible: ruleRow.op === "inLast" || ruleRow.op === "notInLast"
                                    Layout.preferredWidth: 100
                                    model: root.unitChoices
                                    textRole: "text"
                                    valueRole: "value"
                                    currentIndex: Math.max(0, indexOfValue(ruleRow.modelData.value2 || "days"))
                                    onActivated: function(i, value) { root.setRule(groupItem.groupIndex, ruleRow.index, "value2", value) }
                                }

                                FlatButton {
                                    text: qsTrc("mixhouse", "or…")
                                    toolTipTitle: qsTrc("mixhouse", "Add an alternative to this rule")
                                    onClicked: root.update(function(d) {
                                        var base = d.groups[groupItem.groupIndex][ruleRow.index]
                                        d.groups[groupItem.groupIndex].splice(ruleRow.index + 1, 0,
                                            { field: base.field, op: base.op, value: "", value2: base.value2 })
                                    })
                                }
                                FlatButton {
                                    icon: IconCode.DELETE_TANK
                                    toolTipTitle: qsTrc("mixhouse", "Remove this rule")
                                    onClicked: root.update(function(d) {
                                        d.groups[groupItem.groupIndex].splice(ruleRow.index, 1)
                                        if (d.groups[groupItem.groupIndex].length === 0) {
                                            d.groups.splice(groupItem.groupIndex, 1)
                                        }
                                    })
                                }
                            }
                        }

                        SeparatorLine { width: rulesColumn.width; opacity: 0.4 }
                    }
                }

                FlatButton {
                    text: qsTrc("mixhouse", "+ Add rule")
                    onClicked: root.update(function(d) {
                        d.groups = d.groups || []
                        d.groups.push([{ field: "genre", op: "contains", value: "", value2: "" }])
                    })
                }
            }
        }

        SeparatorLine {}

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            CheckBox {
                id: limitCheck
                checked: (root.def.limit || 0) > 0
                text: qsTrc("mixhouse", "Only the")
                onClicked: root.update(function(d) { d.limit = checked ? 0 : 50 })
            }
            TextInputField {
                Layout.preferredWidth: 60
                enabled: limitCheck.checked
                currentText: String(root.def.limit || 50)
                onTextEdited: function(t) { root.def.limit = parseInt(t) || 0; countTimer.restart() }
            }
            StyledTextLabel { text: qsTrc("mixhouse", "tracks, by"); opacity: 0.7 }
            StyledDropdown {
                Layout.preferredWidth: 150
                model: root.fields.filter(function(f) { return f.type !== "playlist" && f.type !== "bool" && f.type !== "color" })
                textRole: "text"
                valueRole: "value"
                popupItemsCount: 14
                currentIndex: indexOfValue(root.def.sort || "dateAdded")
                onActivated: function(i, value) { root.update(function(d) { d.sort = value }) }
            }
            StyledDropdown {
                Layout.preferredWidth: 130
                model: [ { value: false, text: qsTrc("mixhouse", "highest / newest") },
                         { value: true, text: qsTrc("mixhouse", "lowest / oldest") } ]
                textRole: "text"
                valueRole: "value"
                currentIndex: root.def.ascending ? 1 : 0
                onActivated: function(i, value) { root.update(function(d) { d.ascending = value }) }
            }
            Item { Layout.fillWidth: true }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            StyledTextLabel {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignLeft
                opacity: 0.8
                text: qsTrc("mixhouse", "%n track(s) match right now", "", root.matchCount)
            }
            FlatButton {
                text: qsTrc("mixhouse", "Cancel")
                onClicked: root.close()
            }
            FlatButton {
                text: qsTrc("mixhouse", "Save")
                accentButton: true
                onClicked: root.save()
            }
        }
    }
}
