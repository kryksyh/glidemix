/*
 * GlideMix
 */

/*
 * Transitions preferences.
 *
 * WIRED, all of it: what Mix Now does (where it lands the next track, and which transition it
 * applies), the list of transitions, and a builder for your own.
 *
 * A transition is a length and two lanes, volume and bass. Each lane has a curve, the same
 * vocabulary DJ.Studio's transition editor uses because DJs already know it:
 *   Crossfade  the outgoing track's value falls as the incoming one rises (equal power)
 *   Fade in    only the incoming track's value rises
 *   Fade out   only the outgoing track's value falls
 *   Swap       the two trade places at one moment, the crossing point
 * For a bass swap, "Leave out" keeps the incoming bass back for a few beats after the crossing,
 * so for that long neither track has the low end.
 *
 * The preview draws exactly the lines that will be written onto the clips (it asks the same
 * code), so what you see is what the mix does. The chrome is Audacity's own -- do not restyle it.
 */
import QtQuick
import QtQuick.Controls 2.15

import Muse.Ui 1.0
import Muse.UiComponents

import Audacity.Preferences
import Audacity.UiComponents 1.0

PreferencesPage {
    id: root

    TransitionsPreferencesModel {
        id: transitionsModel
    }

    // The transition open in the builder, a plain copy so a half-finished edit changes nothing
    // until it is saved. `editing` is the name it had when it was opened ("" for a new one).
    property var edit: transitionsModel.newTransition()
    property string editing: ""
    property string message: ""
    property var previewData: ({})

    readonly property var curveChoices: [
        { text: qsTrc("preferences", "None"), value: 0 },
        { text: qsTrc("preferences", "Crossfade"), value: 1 },
        { text: qsTrc("preferences", "Fade in (incoming only)"), value: 2 },
        { text: qsTrc("preferences", "Fade out (outgoing only)"), value: 3 },
        { text: qsTrc("preferences", "Swap"), value: 4 }
    ]

    readonly property var shapeChoices: [
        { text: qsTrc("preferences", "Equal power (steady level)"), value: 0 },
        { text: qsTrc("preferences", "Linear"), value: 1 },
        { text: qsTrc("preferences", "S-curve (smooth ends)"), value: 2 },
        { text: qsTrc("preferences", "Early (changes at the start)"), value: 3 },
        { text: qsTrc("preferences", "Late (changes at the end)"), value: 4 }
    ]

    readonly property var filterChoices: [
        { text: qsTrc("preferences", "None"), value: 0 },
        { text: qsTrc("preferences", "Outgoing thins out (high-pass)"), value: 1 },
        { text: qsTrc("preferences", "Outgoing goes dull (low-pass)"), value: 2 },
        { text: qsTrc("preferences", "Incoming opens up from thin (high-pass)"), value: 3 },
        { text: qsTrc("preferences", "Incoming opens up from dull (low-pass)"), value: 4 }
    ]

    readonly property var noiseChoices: [
        { text: qsTrc("preferences", "None"), value: 0 },
        { text: qsTrc("preferences", "Riser (over the outgoing track)"), value: 1 },
        { text: qsTrc("preferences", "Downsweep (after the drop)"), value: 2 }
    ]

    readonly property var effectOutChoices: [
        { text: qsTrc("preferences", "None"), value: 0 },
        { text: qsTrc("preferences", "Echo out"), value: 1 },
        { text: qsTrc("preferences", "Reverb out"), value: 2 }
    ]

    function indexOfValue(list, value) {
        for (var i = 0; i < list.length; ++i) {
            if (list[i].value === value) {
                return i
            }
        }
        return 0
    }

    function refreshPreview() {
        previewData = transitionsModel.preview(edit)
    }

    function open(name) {
        var opened = transitionsModel.transition(name)
        if (!opened.name) {
            return
        }
        edit = opened
        editing = name
        message = ""
        refreshPreview()
    }

    function startNew() {
        edit = transitionsModel.newTransition()
        editing = ""
        message = ""
        refreshPreview()
    }

    function setField(key, value) {
        var copy = JSON.parse(JSON.stringify(edit))
        copy[key] = value
        edit = copy
        refreshPreview()
    }

    function setLane(lane, key, value) {
        var copy = JSON.parse(JSON.stringify(edit))
        copy[lane][key] = value
        edit = copy
        refreshPreview()
    }

    Component.onCompleted: {
        if (transitionsModel.transitions.length > 0) {
            open(transitionsModel.transitions[0].name)
        } else {
            refreshPreview()
        }
    }

    Column {
        width: parent.width
        spacing: root.sectionsSpacing

        BaseSection {
            id: mixNowSection

            title: qsTrc("preferences", "Mix Now")

            navigation.section: root.navigationSection
            navigation.order: root.navigationOrderStart

            onFocusChanged: {
                if (activeFocus) {
                    root.ensureContentVisibleRequested(Qt.rect(x, y, width, height))
                }
            }

            StyledTextLabel {
                width: parent.width
                horizontalAlignment: Qt.AlignLeft
                wrapMode: Text.WordWrap
                text: qsTrc("preferences", "Mix Now pulls the next track forward so it mixes in to the one that is playing. The next track's Intro anchor lands where it says below, the playing track's Outro anchor moves there too, and the transition starts at that anchor.")
            }

            ComboBoxWithTitle {
                title: qsTrc("preferences", "Lands on:")
                columnWidth: 208

                currentIndex: transitionsModel.mixNowLanding
                model: [
                    { text: qsTrc("preferences", "The next bar"), value: 0 },
                    { text: qsTrc("preferences", "The next phrase (phrase-matched)"), value: 1 }
                ]
                textRole: "text"
                valueRole: "value"

                onValueEdited: function(newIndex, newValue) {
                    transitionsModel.mixNowLanding = newValue
                }

                navigation.name: "TransitionsMixNowLanding"
                navigation.panel: mixNowSection.navigation
                navigation.row: 0
            }

            ComboBoxWithTitle {
                title: qsTrc("preferences", "Transition:")
                columnWidth: 208

                currentIndex: root.indexOfValue(transitionsModel.mixNowChoices, transitionsModel.mixNowTransition)
                model: transitionsModel.mixNowChoices
                textRole: "text"
                valueRole: "value"

                onValueEdited: function(newIndex, newValue) {
                    transitionsModel.mixNowTransition = newValue
                }

                navigation.name: "TransitionsMixNowTransition"
                navigation.panel: mixNowSection.navigation
                navigation.row: 1
            }
        }

        SeparatorLine {}

        BaseSection {
            id: viewSection

            title: qsTrc("preferences", "Transition controls")

            navigation.section: root.navigationSection
            navigation.order: mixNowSection.navigationOrderEnd + 1

            ComboBoxWithTitle {
                title: qsTrc("preferences", "The transition pill opens:")
                columnWidth: 208

                currentIndex: transitionsModel.panelView
                model: [
                    { text: qsTrc("preferences", "The transition window"), value: 0 },
                    { text: qsTrc("preferences", "The DJ mixer"), value: 1 }
                ]
                textRole: "text"
                valueRole: "value"

                onValueEdited: function(newIndex, newValue) {
                    transitionsModel.panelView = newValue
                }

                navigation.name: "TransitionsPanelView"
                navigation.panel: viewSection.navigation
                navigation.row: 0
            }
        }

        SeparatorLine {}

        BaseSection {
            id: buildSection

            title: qsTrc("preferences", "Transitions")

            navigation.section: root.navigationSection
            navigation.order: viewSection.navigationOrderEnd + 1

            onFocusChanged: {
                if (activeFocus) {
                    root.ensureContentVisibleRequested(Qt.rect(x, y, width, height))
                }
            }

            Row {
                width: parent.width
                spacing: 24

                // ---- the list ---------------------------------------------------------
                Column {
                    width: 460   // as wide as the three buttons below it
                    spacing: 8

                    StyledListView {
                        id: transitionList
                        width: parent.width
                        height: 260
                        clip: true
                        ScrollBar.vertical: StyledScrollBar {}

                        model: transitionsModel.transitions

                        delegate: ListItemBlank {
                            width: transitionList.width
                            height: 44
                            isSelected: modelData.name === root.editing

                            onClicked: root.open(modelData.name)

                            Column {
                                anchors.fill: parent
                                anchors.leftMargin: 6
                                anchors.rightMargin: 6
                                anchors.topMargin: 4

                                StyledTextLabel {
                                    width: parent.width
                                    horizontalAlignment: Qt.AlignLeft
                                    elide: Text.ElideRight
                                    font: ui.theme.bodyBoldFont
                                    text: modelData.name + (modelData.builtIn ? "" : qsTrc("preferences", "  (yours)"))
                                }

                                StyledTextLabel {
                                    width: parent.width
                                    horizontalAlignment: Qt.AlignLeft
                                    elide: Text.ElideRight
                                    opacity: 0.6
                                    text: modelData.summary
                                }
                            }
                        }
                    }

                    Row {
                        spacing: 8

                        FlatButton {
                            text: qsTrc("preferences", "New")
                            onClicked: root.startNew()

                            navigation.name: "TransitionsNew"
                            navigation.panel: buildSection.navigation
                            navigation.row: 0
                        }

                        FlatButton {
                            text: qsTrc("preferences", "Duplicate")
                            enabled: root.editing !== ""
                            onClicked: {
                                var copy = JSON.parse(JSON.stringify(root.edit))
                                copy.name = root.edit.name + qsTrc("preferences", " copy")
                                copy.builtIn = false
                                root.edit = copy
                                root.editing = ""
                                root.message = qsTrc("preferences", "This is a copy. Save it to keep it.")
                                root.refreshPreview()
                            }

                            navigation.name: "TransitionsDuplicate"
                            navigation.panel: buildSection.navigation
                            navigation.row: 1
                        }

                        FlatButton {
                            text: qsTrc("preferences", "Delete")
                            enabled: root.editing !== "" && !root.edit.builtIn
                            onClicked: {
                                transitionsModel.remove(root.editing)
                                root.startNew()
                            }

                            navigation.name: "TransitionsDelete"
                            navigation.panel: buildSection.navigation
                            navigation.row: 2
                        }
                    }
                }

                // ---- the builder --------------------------------------------------------
                Column {
                    width: parent.width - 460 - 24
                    spacing: 12

                    StyledTextLabel {
                        width: parent.width
                        horizontalAlignment: Qt.AlignLeft
                        visible: root.edit.builtIn === true
                        opacity: 0.6
                wrapMode: Text.WordWrap
                        text: qsTrc("preferences", "This one is built in and cannot be changed. Give it another name to save your own version.")
                    }

                    StyledTextLabel {
                        horizontalAlignment: Qt.AlignLeft
                        text: qsTrc("preferences", "Name")
                    }

                    TextInputField {
                        width: 260

                        currentText: root.edit.name ?? ""
                        hint: qsTrc("preferences", "Name of the transition")

                        onTextEditingFinished: function(newTextValue) {
                            root.setField("name", newTextValue)
                        }

                        navigation.name: "TransitionsName"
                        navigation.panel: buildSection.navigation
                        navigation.row: 3
                        navigation.accessible.name: qsTrc("preferences", "Name of the transition")
                    }

                    IncrementalPropertyControlWithTitle {
                        title: qsTrc("preferences", "Length:")

                        currentValue: root.edit.lengthBars ?? 2
                        minValue: 0.5
                        maxValue: 64
                        step: 0.5
                        decimals: 1
                        measureUnitsSymbol: qsTrc("preferences", "bars")

                        onValueEdited: function(newValue) {
                            root.setField("lengthBars", newValue)
                        }

                        navigation.name: "TransitionsLength"
                        navigation.panel: buildSection.navigation
                        navigation.row: 4
                    }

                    Repeater {
                        model: [
                            { key: "volume", title: qsTrc("preferences", "Volume") },
                            { key: "bass", title: qsTrc("preferences", "Bass") },
                            { key: "mid", title: qsTrc("preferences", "Mids") },
                            { key: "high", title: qsTrc("preferences", "Highs") }
                        ]

                        delegate: Column {
                            id: lane

                            required property var modelData
                            required property int index
                            readonly property var settings: root.edit[modelData.key] ?? ({})

                            width: parent.width
                            spacing: 8

                            StyledTextLabel {
                                horizontalAlignment: Qt.AlignLeft
                                font: ui.theme.bodyBoldFont
                                text: lane.modelData.title
                            }

                            ComboBoxWithTitle {
                                title: qsTrc("preferences", "Curve:")
                                columnWidth: 208

                                currentIndex: root.indexOfValue(root.curveChoices, lane.settings.curve ?? 0)
                                model: root.curveChoices
                                textRole: "text"
                                valueRole: "value"

                                onValueEdited: function(newIndex, newValue) {
                                    root.setLane(lane.modelData.key, "curve", newValue)
                                }

                                navigation.name: "TransitionsCurve" + lane.index
                                navigation.panel: buildSection.navigation
                                navigation.row: 10 + lane.index * 10
                            }

                            ComboBoxWithTitle {
                                readonly property int curve: lane.settings.curve ?? 0
                                visible: curve >= 1 && curve <= 3
                                title: qsTrc("preferences", "Shape:")
                                columnWidth: 208

                                currentIndex: root.indexOfValue(root.shapeChoices, lane.settings.shape ?? 0)
                                model: root.shapeChoices
                                textRole: "text"
                                valueRole: "value"

                                onValueEdited: function(newIndex, newValue) {
                                    root.setLane(lane.modelData.key, "shape", newValue)
                                }

                                navigation.name: "TransitionsShape" + lane.index
                                navigation.panel: buildSection.navigation
                                navigation.row: 14 + lane.index * 10
                            }

                            IncrementalPropertyControlWithTitle {
                                visible: (lane.settings.curve ?? 0) !== 0
                                title: qsTrc("preferences", "Strength:")

                                currentValue: lane.settings.strength ?? 100
                                minValue: 0
                                maxValue: 100
                                step: 5
                                decimals: 0
                                measureUnitsSymbol: qsTrc("global", "%")

                                onValueEdited: function(newValue) {
                                    root.setLane(lane.modelData.key, "strength", newValue)
                                }

                                navigation.name: "TransitionsStrength" + lane.index
                                navigation.panel: buildSection.navigation
                                navigation.row: 11 + lane.index * 10
                            }

                            IncrementalPropertyControlWithTitle {
                                visible: (lane.settings.curve ?? 0) === 4
                                title: qsTrc("preferences", "Crossing point:")

                                currentValue: lane.settings.crossing ?? 50
                                minValue: 0
                                maxValue: 100
                                step: 5
                                decimals: 0
                                measureUnitsSymbol: qsTrc("global", "%")

                                onValueEdited: function(newValue) {
                                    root.setLane(lane.modelData.key, "crossing", newValue)
                                }

                                navigation.name: "TransitionsCrossing" + lane.index
                                navigation.panel: buildSection.navigation
                                navigation.row: 12 + lane.index * 10
                            }

                            IncrementalPropertyControlWithTitle {
                                visible: (lane.settings.curve ?? 0) === 4
                                title: qsTrc("preferences", "Leave out:")

                                currentValue: lane.settings.leaveOutBeats ?? 0
                                minValue: 0
                                maxValue: 16
                                step: 1
                                decimals: 0
                                measureUnitsSymbol: qsTrc("preferences", "beats")

                                onValueEdited: function(newValue) {
                                    root.setLane(lane.modelData.key, "leaveOutBeats", newValue)
                                }

                                navigation.name: "TransitionsLeaveOut" + lane.index
                                navigation.panel: buildSection.navigation
                                navigation.row: 13 + lane.index * 10
                            }
                        }
                    }

                    // ---- the effect lanes: each is played by the track's own DJ effect ----------
                    StyledTextLabel {
                        horizontalAlignment: Qt.AlignLeft
                        font: ui.theme.bodyBoldFont
                        text: qsTrc("preferences", "Effects")
                    }

                    StyledTextLabel {
                        width: parent.width
                        horizontalAlignment: Qt.AlignLeft
                        wrapMode: Text.WordWrap
                        opacity: 0.7
                        text: qsTrc("preferences", "Played by the track's own Filter, White Noise, Echo or Reverb DJ effect, which is added to the track if it has none. Their lines can be edited afterwards like any effect automation.")
                    }

                    Repeater {
                        model: [
                            { key: "filter", title: qsTrc("preferences", "Filter:"), choices: root.filterChoices },
                            { key: "noise", title: qsTrc("preferences", "Noise:"), choices: root.noiseChoices },
                            { key: "effectOut", title: qsTrc("preferences", "Effect out:"), choices: root.effectOutChoices }
                        ]

                        delegate: ComboBoxWithTitle {
                            required property var modelData
                            required property int index

                            title: modelData.title
                            columnWidth: 300

                            currentIndex: root.indexOfValue(modelData.choices, root.edit[modelData.key] ?? 0)
                            model: modelData.choices
                            textRole: "text"
                            valueRole: "value"

                            onValueEdited: function(newIndex, newValue) {
                                root.setField(modelData.key, newValue)
                            }

                            navigation.name: "TransitionsEffect" + index
                            navigation.panel: buildSection.navigation
                            navigation.row: 40 + index
                        }
                    }

                    // ---- the preview: the lines that will be written onto the two clips --------
                    StyledTextLabel {
                        horizontalAlignment: Qt.AlignLeft
                        text: qsTrc("preferences", "Preview")
                    }

                    Canvas {
                        id: preview

                        width: parent.width
                        height: 150

                        Connections {
                            target: root
                            function onPreviewDataChanged() { preview.requestPaint() }
                        }

                        onPaint: {
                            var ctx = getContext("2d")
                            ctx.clearRect(0, 0, width, height)
                            var data = root.previewData
                            var length = data.lengthBeats > 0 ? data.lengthBeats : 8
                            var left = 4, right = width - 4, top = 6, bottom = height - 6

                            function x(beat) { return left + (right - left) * Math.max(0, Math.min(1, beat / length)) }
                            function y(value) { return bottom - (bottom - top) * Math.max(0, Math.min(1, value)) }

                            // a light grid: one vertical line per bar
                            ctx.strokeStyle = ui.theme.strokeColor
                            ctx.lineWidth = 1
                            for (var b = 0; b <= length + 0.001; b += 4) {
                                ctx.beginPath()
                                ctx.moveTo(x(b), top)
                                ctx.lineTo(x(b), bottom)
                                ctx.stroke()
                            }
                            ctx.beginPath()
                            ctx.moveTo(left, bottom)
                            ctx.lineTo(right, bottom)
                            ctx.stroke()

                            // A line holds its first value back to the start and its last one on to the end.
                            function line(points, color, width, dashed) {
                                if (!points || points.length === 0) {
                                    return
                                }
                                ctx.strokeStyle = color
                                ctx.lineWidth = width
                                ctx.setLineDash(dashed ? [5, 4] : [])
                                ctx.beginPath()
                                ctx.moveTo(left, y(points[0][1]))
                                for (var i = 0; i < points.length; ++i) {
                                    ctx.lineTo(x(points[i][0]), y(points[i][1]))
                                }
                                ctx.lineTo(right, y(points[points.length - 1][1]))
                                ctx.stroke()
                            }

                            line(data.volumeOut, ui.theme.accentColor, 2, false)
                            line(data.volumeIn, ui.theme.fontPrimaryColor, 2, false)
                            line(data.bassOut, ui.theme.accentColor, 2, true)
                            line(data.bassIn, ui.theme.fontPrimaryColor, 2, true)
                        }
                    }

                    StyledTextLabel {
                        width: parent.width
                        horizontalAlignment: Qt.AlignLeft
                        opacity: 0.6
                wrapMode: Text.WordWrap
                        text: qsTrc("preferences", "Solid: volume. Dashed: bass. Highlighted: the outgoing track, white: the incoming one. Grid lines are bars.")
                    }

                    StyledTextLabel {
                        width: parent.width
                        horizontalAlignment: Qt.AlignLeft
                        visible: (root.previewData.error ?? "") !== ""
                        text: root.previewData.error ?? ""
                    }

                    Row {
                        spacing: 8

                        FlatButton {
                            text: qsTrc("preferences", "Save")
                            accentButton: true
                            onClicked: {
                                var error = transitionsModel.save(root.edit, root.editing)
                                if (error !== "") {
                                    root.message = error
                                    return
                                }
                                root.open(root.edit.name.trim())   // the model trims the name it stores
                                root.message = qsTrc("preferences", "Saved.")
                            }

                            navigation.name: "TransitionsSave"
                            navigation.panel: buildSection.navigation
                            navigation.row: 40
                        }

                        FlatButton {
                            text: qsTrc("preferences", "Use for Mix Now")
                            enabled: root.editing !== ""
                            onClicked: {
                                transitionsModel.mixNowTransition = root.editing
                                root.message = qsTrc("preferences", "Mix Now will use this transition.")
                            }

                            navigation.name: "TransitionsUse"
                            navigation.panel: buildSection.navigation
                            navigation.row: 41
                        }
                    }

                    StyledTextLabel {
                        width: parent.width
                        horizontalAlignment: Qt.AlignLeft
                        visible: root.message !== ""
                        text: root.message
                    }
                }
            }
        }
    }
}
