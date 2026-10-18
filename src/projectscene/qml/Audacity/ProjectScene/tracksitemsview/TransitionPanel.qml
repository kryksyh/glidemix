/*
 * GlideMix
 *
 * The transition panel (DropLab's bottom panel): the transition's preset and
 * length, the side the controls work on (the song going OUT or the one coming
 * IN), EQ knobs, a volume fader and the Color knob, the lanes, and ADD FX.
 *
 * The knobs show the lane's value at the playhead; turning one sets a point
 * there (inside the overlap), so a transition can be played by hand: move the
 * playhead, turn, move on. Each turn is one undo step.
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents

import "../trackspanel/audio"

StyledDialogView {
    id: root

    // A floating window of its own: it stays open while you play and edit,
    // and can be moved anywhere (the title bar), until closed.
    title: qsTrc("mixhouse", "Transition")
    modal: false
    frameless: false
    resizable: false
    alwaysAboveApp: true

    property var zoneItem: null
    property var zonesModel: null
    property int row: -1
    property var timelineContext: null
    property real playheadX: 0
    property real zoneStart: 0
    property real zoneEnd: 0
    property string presetName: ""

    signal presetMenuRequested()

    // "out": the outgoing song, "in": the incoming one.
    property string side: "out"
    // Bumped whenever the lines may have changed, so the knobs re-read them.
    property int revision: 0

    contentWidth: 460
    contentHeight: content.implicitHeight
    margins: 14

    Connections {
        target: root.zonesModel
        function onLinesChanged() { root.revision++ }
    }

    // The playhead in project seconds, kept inside the overlap.
    function playheadTime() {
        if (!timelineContext || !zoneItem) {
            return 0
        }
        var t = timelineContext.frameStartTime + playheadX / timelineContext.zoom
        return Math.max(zoneStart, Math.min(zoneEnd, t))
    }

    function lineValue(line, t) {
        if (!line || !line.samples || line.samples.length === 0) {
            return 1
        }
        var s = line.samples
        if (t <= s[0][0]) return s[0][1]
        for (var i = 1; i < s.length; ++i) {
            if (t <= s[i][0]) {
                var span = s[i][0] - s[i - 1][0]
                var u = span > 0 ? (t - s[i - 1][0]) / span : 0
                return s[i - 1][1] + u * (s[i][1] - s[i - 1][1])
            }
        }
        return s[s.length - 1][1]
    }

    // The lane's value on the chosen side at the playhead.
    function valueAt(lane) {
        root.revision  // re-read when the lines change
        if (!zonesModel || row < 0) {
            return 0
        }
        var lines = zonesModel.laneLines(row, lane)
        return lineValue(side === "out" ? lines.out : lines.in, playheadTime())
    }

    // A point at the playhead with value `v`; a point already within a
    // tenth of a second of it is replaced.
    function setAtPlayhead(lane, v) {
        var lines = zonesModel.laneLines(row, lane)
        var line = side === "out" ? lines.out : lines.in
        if (!line) {
            return
        }
        var t = playheadTime()
        var points = []
        for (var i = 0; i < line.points.length; ++i) {
            if (Math.abs(line.points[i][0] - t) > 0.1) {
                points.push(line.points[i])
            }
        }
        points.push([t, v])
        points.sort(function(a, b) { return a[0] - b[0] })
        zonesModel.setLanePoints(row, lane, side === "out", points)
        if (zoneItem) {
            zoneItem.lane = lane
            zonesModel.shownLanes[zoneItem.laneKey] = lane
        }
    }

    // A knob is committed a moment after it stops turning, so one turn is
    // one undo step.
    Timer {
        id: commitTimer
        interval: 350
        property int lane: -1
        property real value: 0
        onTriggered: if (lane >= 0) root.setAtPlayhead(lane, value)
    }
    function turned(lane, v) {
        commitTimer.lane = lane
        commitTimer.value = v
        commitTimer.restart()
    }

    ColumnLayout {
        id: content
        width: 460
        spacing: 12

        // ---- header: the transition, its length, the preset menu --------
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            StyledTextLabel {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignLeft
                font: ui.theme.bodyBoldFont
                text: root.presetName !== "" ? root.presetName : qsTrc("mixhouse", "Transition")
                elide: Text.ElideRight
            }

            StyledTextLabel {
                text: {
                    root.revision
                    var bars = zonesModel && row >= 0 ? zonesModel.lengthBars(row) : 0
                    return qsTrc("mixhouse", "%1 bars").arg(bars.toFixed(bars < 10 ? 1 : 0))
                }
                opacity: 0.7
            }

            FlatButton {
                text: qsTrc("mixhouse", "Presets")
                onClicked: root.presetMenuRequested()
            }
        }

        // ---- which song the controls work on ----------------------------
        RowLayout {
            spacing: 6

            StyledTextLabel {
                text: qsTrc("mixhouse", "Controls:")
                opacity: 0.7
            }
            FlatButton {
                Layout.preferredWidth: 90
                Layout.preferredHeight: 24
                text: qsTrc("mixhouse", "Song out")
                accentButton: root.side === "out"
                onClicked: root.side = "out"
            }
            FlatButton {
                Layout.preferredWidth: 90
                Layout.preferredHeight: 24
                text: qsTrc("mixhouse", "Song in")
                accentButton: root.side === "in"
                onClicked: root.side = "in"
            }
        }

        // ---- EQ, volume, Color, lanes -----------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: 18

            // EQ: High, Mid, Low (lanes 4, 3, 1)
            Column {
                spacing: 6
                Repeater {
                    model: [
                        { name: qsTrc("mixhouse", "High"), lane: 4 },
                        { name: qsTrc("mixhouse", "Mid"), lane: 3 },
                        { name: qsTrc("mixhouse", "Low"), lane: 1 }
                    ]
                    delegate: Row {
                        required property var modelData
                        spacing: 8
                        StyledTextLabel {
                            width: 34
                            anchors.verticalCenter: parent.verticalCenter
                            horizontalAlignment: Text.AlignLeft
                            text: modelData.name
                        }
                        KnobControl {
                            id: eqKnob
                            radius: 17
                            from: 0
                            to: 1
                            stepSize: 0.02
                            value: root.valueAt(modelData.lane)
                            onNewValueRequested: function(v) {
                                value = v
                                root.turned(modelData.lane, v)
                            }
                        }
                        StyledTextLabel {
                            width: 44
                            anchors.verticalCenter: parent.verticalCenter
                            opacity: 0.7
                            text: eqKnob.value <= 0.001 ? qsTrc("mixhouse", "kill") : Math.round(eqKnob.value * 100) + "%"
                        }
                    }
                }
            }

            // VOL: the clip volume line (lane 0)
            Column {
                spacing: 4
                StyledTextLabel {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTrc("mixhouse", "VOL")
                }
                Slider {
                    id: volSlider
                    anchors.horizontalCenter: parent.horizontalCenter
                    orientation: Qt.Vertical
                    height: 110
                    from: 0
                    to: 1
                    value: root.valueAt(0)
                    onMoved: root.turned(0, value)
                }
                StyledTextLabel {
                    anchors.horizontalCenter: parent.horizontalCenter
                    opacity: 0.7
                    text: Math.round(volSlider.value * 100) + "%"
                }
            }

            // Color: the filter as one knob (lane 2)
            Column {
                spacing: 4
                StyledTextLabel {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTrc("mixhouse", "Color")
                }
                KnobControl {
                    id: colorKnob
                    anchors.horizontalCenter: parent.horizontalCenter
                    radius: 22
                    isPanKnob: true
                    from: 0
                    to: 1
                    stepSize: 0.02
                    value: root.valueAt(2)
                    onNewValueRequested: function(v) {
                        value = v
                        root.turned(2, v)
                    }
                }
                StyledTextLabel {
                    anchors.horizontalCenter: parent.horizontalCenter
                    opacity: 0.7
                    text: colorKnob.value < 0.49 ? qsTrc("mixhouse", "low-pass")
                          : (colorKnob.value > 0.51 ? qsTrc("mixhouse", "high-pass") : qsTrc("mixhouse", "open"))
                }
            }

            // The lanes: click one to see and edit it on the timeline.
            Column {
                Layout.fillWidth: true
                spacing: 2
                Repeater {
                    model: root.zoneItem ? root.zoneItem.laneOrder : []
                    delegate: Rectangle {
                        required property int modelData
                        readonly property bool chosen: root.zoneItem && root.zoneItem.lane === modelData
                        width: 110
                        height: 22
                        radius: 4
                        color: chosen ? Qt.rgba(1, 1, 1, 0.08) : "transparent"
                        border.color: chosen ? root.zoneItem.laneColors[modelData] : "transparent"

                        Row {
                            anchors.verticalCenter: parent.verticalCenter
                            x: 8
                            spacing: 8
                            Rectangle {
                                anchors.verticalCenter: parent.verticalCenter
                                width: 8
                                height: 8
                                radius: 4
                                color: root.zoneItem ? root.zoneItem.laneColors[modelData] : "white"
                            }
                            StyledTextLabel {
                                text: root.zoneItem ? root.zoneItem.laneNames[modelData] : ""
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                root.zoneItem.lane = modelData
                                root.zonesModel.shownLanes[root.zoneItem.laneKey] = modelData
                            }
                        }
                    }
                }
            }
        }

        // ---- ADD FX -------------------------------------------------------
        RowLayout {
            Layout.fillWidth: true

            FlatButton {
                text: qsTrc("mixhouse", "+ ADD FX")
                onClicked: fxMenu.toggleOpened([
                    { id: "filter-out", title: qsTrc("mixhouse", "Filter: song out thins out") },
                    { id: "filter-in", title: qsTrc("mixhouse", "Filter: song in opens up") },
                    { id: "echo", title: qsTrc("mixhouse", "Echo out") },
                    { id: "reverb", title: qsTrc("mixhouse", "Reverb out") },
                    { id: "riser", title: qsTrc("mixhouse", "Noise riser") },
                    { id: "downsweep", title: qsTrc("mixhouse", "Noise downsweep") },
                    {},
                    { id: "vinyl-brake", title: qsTrc("mixhouse", "Vinyl brake (last beat)") },
                    { id: "tape-stop", title: qsTrc("mixhouse", "Tape stop (last bar)") },
                    { id: "roll-out", title: qsTrc("mixhouse", "Beat roll out (last 2 beats)") }
                ])

                StyledMenuLoader {
                    id: fxMenu
                    onHandleMenuItem: function(itemId) {
                        root.zonesModel.addEffect(root.row, itemId)
                    }
                }
            }

            StyledTextLabel {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignRight
                wrapMode: Text.WordWrap
                opacity: 0.6
                text: qsTrc("mixhouse", "Turning a control sets a point at the playhead.")
            }
        }
    }
}
