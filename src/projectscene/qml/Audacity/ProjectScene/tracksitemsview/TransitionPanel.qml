/*
 * GlideMix
 *
 * The transition panel (DropLab's bottom panel): the transition's preset and
 * length, the side the controls work on (the song going OUT or the one coming
 * IN), EQ knobs, a volume fader and the Color knob, the lanes, and ADD FX.
 *
 * The knobs show the lane's value at the playhead; turning one sets a point
 * there (inside the overlap), so a transition can be played by hand: move the
 * playhead, turn, move on. While playing, a turn is recorded as it happens.
 * The line is written while the control moves (so it is heard), and each
 * turn is one undo step.
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents

import Audacity.Effects

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
    property bool isPlaying: false
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

    // The playhead in project seconds, just inside the overlap: the model
    // keeps only points strictly inside it, so a point pinned to an edge
    // (playhead outside) would be lost.
    function playheadTime() {
        if (!timelineContext || !zoneItem) {
            return 0
        }
        var t = timelineContext.frameStartTime + playheadX / timelineContext.zoom
        var inset = Math.min(0.01, (zoneEnd - zoneStart) / 4)
        return Math.max(zoneStart + inset, Math.min(zoneEnd - inset, t))
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

    // GlideMix: each lane's lines, read from the model once per change. Read
    // per knob per frame, they took most of the CPU during playback.
    property var linesCache: ({})
    onRevisionChanged: linesCache = ({})
    onRowChanged: linesCache = ({})
    onZoneStartChanged: linesCache = ({})
    onZoneEndChanged: linesCache = ({})
    function cachedLines(lane) {
        var cache = root.linesCache
        if (!(lane in cache)) {
            cache[lane] = zonesModel.laneLines(row, lane)
        }
        return cache[lane]
    }

    // The lane's value on the chosen side at the playhead.
    function valueAt(lane) {
        // A closed panel does not follow the playhead: every transition has
        // one, and each re-read its knobs on every frame.
        if (!root.isOpened) {
            return 0
        }
        root.revision  // re-read when the lines change
        root.playheadX
        root.side
        // GlideMix: the control being moved shows where the hand is, not the
        // line (which is only written when it is let go).
        if (root.live && root.live.lane === lane) {
            return root.live.value
        }
        if (!zonesModel || row < 0) {
            return 0
        }
        var lines = cachedLines(lane)
        return lineValue(side === "out" ? lines.out : lines.in, playheadTime())
    }

    // Writes `samples` ([[t, v], ...]) into the chosen side's lane: a single
    // point replaces any within a tenth of a second; a recorded move replaces
    // the points over the time it covers.
    function writeSamples(lane, samples, consolidate) {
        if (!zonesModel || row < 0 || samples.length === 0) {
            return
        }
        var lines = zonesModel.laneLines(row, lane)
        var line = side === "out" ? lines.out : lines.in
        if (!line) {
            return
        }
        var t0 = samples[0][0], t1 = samples[samples.length - 1][0]
        var tol = samples.length === 1 ? 0.1 : 0.02
        var points = []
        for (var i = 0; i < line.points.length; ++i) {
            var t = line.points[i][0]
            if (t < t0 - tol || t > t1 + tol) {
                points.push(line.points[i])
            }
        }
        for (var k = 0; k < samples.length; ++k) {
            points.push(samples[k])
        }
        points.sort(function(a, b) { return a[0] - b[0] })
        zonesModel.setLanePoints(row, lane, side === "out", points, consolidate === true)
        if (zoneItem) {
            zoneItem.lane = lane
            zonesModel.shownLanes[zoneItem.laneKey] = lane
        }
    }

    // GlideMix: one move of one control, the mixer view's way: pressed ->
    // moved -> let go. The knobs used to overwrite their own binding on the
    // first turn (so they stopped following the playhead and the lines) and
    // wrote only after a 350 ms pause, one point, wherever the playhead had
    // got to by then. Now: stopped, one point at the playhead; playing, the
    // whole move as it was played. Written while it moves, so the change is
    // heard at once; the writes of one move are one undo step.
    // live: { lane, value } while a control is held, else null.
    property var live: null
    property var samples: []
    // Writes made for the current move: the first starts its undo step, the
    // rest join it.
    property int liveWrites: 0

    function beginMove(lane) {
        commitTimer.stop()
        samples = []
        liveWrites = 0
        live = { lane: lane, value: valueAt(lane) }
    }
    function flushLive() {
        if (!live || live.wheel || samples.length === 0) {
            return
        }
        writeSamples(live.lane, samples, liveWrites > 0)
        liveWrites++
    }
    // At most ~15 writes a second while a control moves.
    Timer {
        id: liveTimer
        interval: 66
        onTriggered: root.flushLive()
    }
    function moveTo(lane, v) {
        if (!live || live.lane !== lane) {
            // Not held (the scroll wheel): one point, a moment after it stops.
            commitTimer.lane = lane
            commitTimer.value = v
            commitTimer.restart()
            live = { lane: lane, value: v, wheel: true }
            return
        }
        live = { lane: lane, value: v, wheel: live.wheel }
        var t = playheadTime()
        if (isPlaying) {
            // One sample every 30 ms of playback.
            if (samples.length > 0 && Math.abs(t - samples[samples.length - 1][0]) < 0.03) {
                samples[samples.length - 1] = [samples[samples.length - 1][0], v]
            } else {
                samples.push([t, v])
            }
        } else {
            samples = [[t, v]]
        }
        if (!liveTimer.running) {
            liveTimer.start()
        }
    }
    function endMove() {
        liveTimer.stop()
        var l = live
        var s = samples
        var joined = liveWrites > 0
        samples = []
        liveWrites = 0
        live = null
        if (l && !l.wheel && s.length > 0) {
            writeSamples(l.lane, s, joined)
        }
    }
    // While playing, a control held still keeps being written.
    Timer {
        interval: 50
        repeat: true
        running: root.live !== null && !root.live.wheel && root.isPlaying
        onTriggered: if (root.samples.length > 0) root.moveTo(root.live.lane, root.live.value)
    }
    // The scroll wheel has no press or release: one point once it stops.
    Timer {
        id: commitTimer
        interval: 350
        property int lane: -1
        property real value: 0
        onTriggered: {
            root.live = null
            if (lane >= 0) root.writeSamples(lane, [[root.playheadTime(), value]])
        }
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
                            onMousePressed: root.beginMove(modelData.lane)
                            onMouseReleased: root.endMove()
                            onNewValueRequested: function(v) {
                                root.moveTo(modelData.lane, Math.max(0, Math.min(1, v)))
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
                // GlideMix: the mixer view's fader; Qt's bare Slider had only
                // a small dark square to grab.
                DjFader {
                    id: volSlider
                    anchors.horizontalCenter: parent.horizontalCenter
                    implicitHeight: 120
                    defaultValue: 1
                    value: root.valueAt(0)
                    onGrabbed: function(down) { down ? root.beginMove(0) : root.endMove() }
                    onMoved: function(v) { root.moveTo(0, v) }
                }
                StyledTextLabel {
                    anchors.horizontalCenter: parent.horizontalCenter
                    opacity: 0.7
                    text: Math.round(volSlider.shown * 100) + "%"
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
                    onMousePressed: root.beginMove(2)
                    onMouseReleased: root.endMove()
                    onNewValueRequested: function(v) {
                        root.moveTo(2, Math.max(0, Math.min(1, v)))
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

        // ---- effects: the track effect list's look -------------------------
        // GlideMix: each effect ADD FX put on this transition is a row like a
        // track's effect -- power, name, its amount, a menu to remove it --
        // and acts inside the transition only.
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6

            Repeater {
                id: fxRepeater
                model: {
                    root.revision
                    return root.zonesModel && root.row >= 0 ? root.zonesModel.addedEffects(root.row) : []
                }
                delegate: RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.preferredHeight: 28
                    spacing: 6

                    BypassEffectButton {
                        size: 28
                        accentButton: modelData.enabled
                        toolTipTitle: qsTrc("mixhouse", "Switch this effect on or off")
                        onClicked: root.zonesModel.setEffectEnabled(root.row, modelData.kind, !modelData.enabled)
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: 3
                        color: ui.theme.backgroundPrimaryColor
                        border.color: ui.theme.strokeColor
                        opacity: modelData.enabled ? 1 : 0.6
                        StyledTextLabel {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            horizontalAlignment: Text.AlignLeft
                            text: modelData.name
                            elide: Text.ElideRight
                        }
                    }
                    KnobControl {
                        id: amountKnob
                        radius: 12
                        from: 0
                        to: 1
                        stepSize: 0.02
                        value: modelData.amount
                        onNewValueRequested: function(v) {
                            value = v
                            amountTimer.restart()
                        }
                        // One turn, one undo step.
                        Timer {
                            id: amountTimer
                            interval: 350
                            onTriggered: root.zonesModel.setEffectAmount(root.row, modelData.kind, amountKnob.value)
                        }
                    }
                    StyledTextLabel {
                        Layout.preferredWidth: 36
                        opacity: 0.7
                        text: Math.round(amountKnob.value * 100) + "%"
                    }
                    // No pop-up menus in this window: on Windows a menu opened from
                    // a floating window closes without running the chosen item.
                    FlatButton {
                        Layout.preferredWidth: 28
                        Layout.fillHeight: true
                        icon: IconCode.CLOSE_X_ROUNDED
                        toolTipTitle: qsTrc("mixhouse", "Remove effect")
                        onClicked: root.zonesModel.removeEffect(root.row, modelData.kind)
                    }
                }
            }

            FlatButton {
                id: addEffectButton
                property bool open: false
                Layout.fillWidth: true
                Layout.preferredHeight: 28
                text: open ? qsTrc("mixhouse", "Close") : qsTrc("mixhouse", "Add effect")
                onClicked: open = !open
            }

            // The effects to add, listed in the panel (no pop-up menu, see above).
            GridLayout {
                Layout.fillWidth: true
                visible: addEffectButton.open
                columns: 2
                columnSpacing: 6
                rowSpacing: 4
                Repeater {
                    model: [
                        { id: "filter-out", title: qsTrc("mixhouse", "Filter: song out thins out") },
                        { id: "filter-in", title: qsTrc("mixhouse", "Filter: song in opens up") },
                        { id: "echo", title: qsTrc("mixhouse", "Echo out") },
                        { id: "reverb", title: qsTrc("mixhouse", "Reverb out") },
                        { id: "riser", title: qsTrc("mixhouse", "Noise riser") },
                        { id: "downsweep", title: qsTrc("mixhouse", "Noise downsweep") },
                        { id: "vinyl-brake", title: qsTrc("mixhouse", "Vinyl brake (last beat)") },
                        { id: "tape-stop", title: qsTrc("mixhouse", "Tape stop (last bar)") },
                        { id: "roll-out", title: qsTrc("mixhouse", "Beat roll out (last 2 beats)") }
                    ]
                    delegate: FlatButton {
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.preferredHeight: 26
                        text: modelData.title
                        onClicked: {
                            root.zonesModel.addEffect(root.row, modelData.id)
                            addEffectButton.open = false
                        }
                    }
                }
            }

            StyledTextLabel {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignLeft
                wrapMode: Text.WordWrap
                opacity: 0.6
                text: qsTrc("mixhouse", "Turning a control sets a point at the playhead; while playing, the move is recorded.")
            }
        }
    }
}
