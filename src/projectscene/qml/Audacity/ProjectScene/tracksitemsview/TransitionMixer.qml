/*
 * GlideMix
 *
 * The transition as a two-channel DJ mixer, laid out like a DJM-450: CH 1 is
 * the song going out, CH 2 the song coming in. Each channel has HI / MID / LOW
 * with KILL, COLOR (SOUND COLOR FX: Filter) and a channel fader; the crossfader
 * moves both volume lines along a curve (Smooth, Constant, Cut); BEAT FX adds
 * the transition's effects. Controls GlideMix has nothing behind yet (TRIM,
 * meters, the other SOUND COLOR modes, beat length, FX FREQUENCY, EQ CURVE) are
 * drawn dim and do nothing.
 *
 * It drives the same lanes as the transition window (Preferences picks which
 * one the pill opens). A move sets a point at the playhead. With REC on while
 * playing, the whole move is written as it was played, one undo step per move
 * (two for the crossfader, one per song).
 *
 * No pop-up menus in this window: on Windows a menu opened from a floating
 * window closes without running the chosen item.
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents

StyledDialogView {
    id: root

    title: qsTrc("mixhouse", "Transition mixer")
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

    // Bumped whenever the lines may have changed, so the controls re-read them.
    property int revision: 0
    property bool rec: false
    // 0 Smooth, 1 Constant, 2 Cut
    property int xfCurve: 1

    readonly property color outColor: "#f0a43a"
    readonly property color inColor: "#6fb7e8"

    contentWidth: 820
    contentHeight: plate.implicitHeight
    margins: 0

    Connections {
        target: root.zonesModel
        function onLinesChanged() { root.revision++ }
    }

    // ---- reading and writing the lanes --------------------------------------

    function playheadTime() {
        if (!timelineContext) {
            return 0
        }
        var t = timelineContext.frameStartTime + playheadX / timelineContext.zoom
        // Just inside the overlap: the model keeps only points strictly inside
        // it, so a point pinned to an edge (playhead outside) would be lost.
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

    // The lane's value for one song ("out" / "in") at the playhead.
    function valueAt(lane, side) {
        // A closed mixer does not follow the playhead: every transition has
        // one, and each re-read its knobs on every frame.
        if (!root.isOpened) {
            return 0
        }
        root.revision  // re-read when the lines change
        root.playheadX
        if (!zonesModel || row < 0) {
            return 0
        }
        var lines = cachedLines(lane)
        return lineValue(side === "out" ? lines.out : lines.in, playheadTime())
    }

    // Writes `samples` ([[t, v], ...]) into one side's lane: a single point
    // replaces any within a tenth of a second; a recorded move replaces the
    // points over the time it covers.
    function writeSamples(lane, side, samples) {
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
        zonesModel.setLanePoints(row, lane, side === "out", points)
    }

    // One move of one control. `targets` is [{ lane, side, map }] -- map turns
    // the control's value into that lane's value (the crossfader's curve).
    property var gesture: null

    function beginMove(targets) {
        gesture = { targets: targets, samples: [] }
    }
    property real heldValue: 0
    // While REC is on and playing, a control held still keeps being written.
    Timer {
        interval: 50
        repeat: true
        running: root.gesture !== null && root.rec && root.isPlaying
        onTriggered: if (root.gesture.samples.length > 0) root.moveTo(root.heldValue)
    }
    function moveTo(v) {
        if (!gesture) {
            return
        }
        heldValue = v
        var t = playheadTime()
        var s = gesture.samples
        if (root.rec && root.isPlaying) {
            // Thin to one sample every 30 ms of playback.
            if (s.length > 0 && Math.abs(t - s[s.length - 1][0]) < 0.03) {
                s[s.length - 1] = [s[s.length - 1][0], v]
            } else {
                s.push([t, v])
            }
        } else {
            gesture.samples = [[t, v]]
        }
    }
    function endMove() {
        var g = gesture
        gesture = null
        if (!g || g.samples.length === 0) {
            return
        }
        for (var i = 0; i < g.targets.length; ++i) {
            var tg = g.targets[i]
            var mapped = []
            for (var k = 0; k < g.samples.length; ++k) {
                var v = g.samples[k][1]
                mapped.push([g.samples[k][0], tg.map ? tg.map(v) : v])
            }
            writeSamples(tg.lane, tg.side, mapped)
        }
        if (zoneItem && g.targets.length > 0) {
            zoneItem.lane = g.targets[0].lane
            zonesModel.shownLanes[zoneItem.laneKey] = g.targets[0].lane
        }
    }

    // ---- the crossfader -------------------------------------------------------

    // The gain of one song at crossfader position x (0 = all CH 1, 1 = all CH 2).
    function xfGain(x, side) {
        var u = side === "out" ? 1 - x : x
        if (root.xfCurve === 0) {        // Smooth: equal power, both dip in the middle
            return Math.sin(u * Math.PI / 2)
        } else if (root.xfCurve === 1) { // Constant: both full across the middle
            return Math.min(1, 2 * u)
        }
        return Math.min(1, u / 0.06)     // Cut: full until the very end
    }
    // Where the crossfader sits, read back from the two volume lines.
    function xfPosition() {
        var o = valueAt(0, "out"), i = valueAt(0, "in")
        if (o + i <= 0.0001) {
            return 0.5
        }
        return i / (o + i)
    }

    // ---- what GlideMix has behind each FX control ---------------------------

    // BEAT FX, in the DJM's order; the ones GlideMix does not have are dim.
    // side: which channel the effect works on (fixed by the effect), beats: its length.
    readonly property var beatFx: [
        { id: "delay", title: qsTrc("mixhouse", "DELAY"), enabled: false },
        { id: "echo", title: qsTrc("mixhouse", "ECHO"), side: "1", beats: qsTrc("mixhouse", "ALL") },
        { id: "pingpong", title: qsTrc("mixhouse", "PING PONG"), enabled: false },
        { id: "spin", title: qsTrc("mixhouse", "SPIN"), enabled: false },
        { id: "reverb", title: qsTrc("mixhouse", "REVERB"), side: "1", beats: qsTrc("mixhouse", "ALL") },
        { id: "trans", title: qsTrc("mixhouse", "TRANS"), enabled: false },
        { id: "flanger", title: qsTrc("mixhouse", "FLANGER"), enabled: false },
        { id: "pitch", title: qsTrc("mixhouse", "PITCH"), enabled: false },
        { id: "roll-out", title: qsTrc("mixhouse", "ROLL"), side: "1", beats: "2" },
        { id: "vinyl-brake", title: qsTrc("mixhouse", "BRAKE"), side: "1", beats: "1" },
        { id: "helix", title: qsTrc("mixhouse", "HELIX"), enabled: false },
        { id: "tape-stop", title: qsTrc("mixhouse", "TAPE STOP"), side: "1", beats: "4" },
        { id: "riser", title: qsTrc("mixhouse", "RISER"), side: "M", beats: qsTrc("mixhouse", "ALL") },
        { id: "downsweep", title: qsTrc("mixhouse", "SWEEP"), side: "M", beats: qsTrc("mixhouse", "ALL") }
    ]
    property string fxChosen: "echo"
    readonly property var fxInfo: {
        for (var i = 0; i < beatFx.length; ++i) {
            if (beatFx[i].id === fxChosen) return beatFx[i]
        }
        return beatFx[1]
    }
    readonly property var fxAdded: {
        root.revision
        return root.zonesModel && root.row >= 0 ? root.zonesModel.addedEffects(root.row) : []
    }
    function fxFind(kind) {
        for (var i = 0; i < fxAdded.length; ++i) {
            if (fxAdded[i].kind === kind) return fxAdded[i]
        }
        return null
    }
    readonly property var fxCurrent: fxFind(fxChosen)

    // ---- pieces ---------------------------------------------------------------

    component SectionTitle: Text {
        color: "#e7e4dc"
        font.pixelSize: 10
        font.bold: true
        font.letterSpacing: 1.6
    }

    // One channel on a single vertical axis, as on the hardware: TRIM, HI,
    // MID, LOW, COLOR, the CUE-style number and the fader all share its centre.
    // KILL sits on the outer side so it does not push the knobs off the line.
    component ChannelColumn: Item {
        id: ch
        property string side: "out"
        property string number: "1"
        property color accent: "white"
        property var mixer: null
        readonly property real axis: width / 2
        readonly property bool outer: side === "out"   // KILL to the left on CH 1, to the right on CH 2

        width: 124
        height: 530

        // TRIM: no gain lane for it yet.
        DjKnob {
            x: ch.axis - width / 2
            y: 0
            size: 38
            big: true
            label: qsTrc("mixhouse", "TRIM")
            accent: ch.accent
            value: 0.5
            enabled: false
            opacity: 0.35
        }

        Repeater {
            // HI, MID, LOW: lanes 4, 3, 1
            model: [
                { label: qsTrc("mixhouse", "HI"), lane: 4 },
                { label: qsTrc("mixhouse", "MID"), lane: 3 },
                { label: qsTrc("mixhouse", "LOW"), lane: 1 }
            ]
            delegate: Item {
                required property var modelData
                required property int index
                x: 0
                y: 60 + index * 62
                width: ch.width
                height: 58

                DjKnob {
                    id: eq
                    x: ch.axis - width / 2
                    size: 42
                    label: modelData.label
                    accent: ch.accent
                    defaultValue: 1
                    value: ch.mixer.valueAt(modelData.lane, ch.side)
                    onGrabbed: function(down) {
                        if (down) {
                            ch.mixer.beginMove([{ lane: modelData.lane, side: ch.side }])
                        } else {
                            ch.mixer.endMove()
                        }
                    }
                    onMoved: function(v) { ch.mixer.moveTo(v) }
                }
                DjLitButton {
                    x: ch.outer ? ch.axis - 21 - width - 6 : ch.axis + 21 + 6
                    y: 21 - height / 2
                    implicitWidth: 28
                    implicitHeight: 16
                    fontSize: 7
                    text: qsTrc("mixhouse", "KILL")
                    lit: eq.shown <= 0.001
                    litColor: "#e0505a"
                    onClicked: {
                        ch.mixer.beginMove([{ lane: modelData.lane, side: ch.side }])
                        ch.mixer.moveTo(lit ? 1 : 0)
                        ch.mixer.endMove()
                    }
                }
            }
        }

        // COLOR (SOUND COLOR FX: Filter), on the band drawn behind both channels.
        DjKnob {
            x: ch.axis - width / 2
            y: 254
            size: 52
            big: true
            label: qsTrc("mixhouse", "COLOR")
            accent: ch.accent
            defaultValue: 0.5
            value: ch.mixer.valueAt(2, ch.side)
            onGrabbed: function(down) {
                if (down) {
                    ch.mixer.beginMove([{ lane: 2, side: ch.side }])
                } else {
                    ch.mixer.endMove()
                }
            }
            onMoved: function(v) { ch.mixer.moveTo(v) }
        }

        DjLitButton {
            x: ch.axis - width / 2
            y: 336
            implicitWidth: 40
            text: ch.number
            lit: true
            litColor: "#f39a2b"
            fontSize: 11
        }

        DjFader {
            id: fader
            x: ch.axis - width / 2
            y: 366
            defaultValue: 1
            value: ch.mixer.valueAt(0, ch.side)
            onGrabbed: function(down) {
                if (down) {
                    ch.mixer.beginMove([{ lane: 0, side: ch.side }])
                } else {
                    ch.mixer.endMove()
                }
            }
            onMoved: function(v) { ch.mixer.moveTo(v) }
        }
        // the 0-10 scale, beside the fader's ticks (does not move the fader)
        Repeater {
            model: 11
            delegate: Text {
                required property int index
                x: ch.outer ? fader.x - width + 2 : fader.x + fader.width - 2
                y: fader.y + fader.pad + (150 - 2 * fader.pad) * index / 10 - height / 2
                width: 12
                horizontalAlignment: ch.outer ? Text.AlignRight : Text.AlignLeft
                text: 10 - index
                color: "#6c6f73"
                font.pixelSize: 7
            }
        }
    }

    // An unlit LED ladder: GlideMix has no live level per channel here yet.
    component Meter: Column {
        id: meter
        property string label: ""
        spacing: 2
        Repeater {
            model: 15
            delegate: Rectangle {
                required property int index
                width: 8
                height: 5
                radius: 1
                color: index < 2 ? "#3a1416" : (index < 5 ? "#3a3214" : "#163a1c")
            }
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: meter.label
            color: "#6c6f73"
            font.pixelSize: 7
        }
    }

    // ---- the faceplate --------------------------------------------------------

    Rectangle {
        id: plate
        width: 820
        implicitHeight: body.implicitHeight + 28
        gradient: Gradient {
            GradientStop { position: 0; color: "#24272b" }
            GradientStop { position: 0.18; color: "#1d1f22" }
            GradientStop { position: 1; color: "#191b1e" }
        }
        border.color: "#2c2f33"

        ColumnLayout {
            id: body
            x: 14
            y: 14
            width: parent.width - 28
            spacing: 12

            // header: the transition, its length, presets, REC
            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Text {
                    Layout.fillWidth: true
                    text: root.presetName !== "" ? root.presetName : qsTrc("mixhouse", "Transition")
                    color: "#e7e4dc"
                    font.pixelSize: 13
                    font.bold: true
                    elide: Text.ElideRight
                }
                Text {
                    color: "#9a9890"
                    font.pixelSize: 11
                    text: {
                        root.revision
                        var bars = root.zonesModel && root.row >= 0 ? root.zonesModel.lengthBars(root.row) : 0
                        return qsTrc("mixhouse", "%1 bars").arg(bars.toFixed(bars < 10 ? 1 : 0))
                    }
                }
                DjLitButton {
                    text: qsTrc("mixhouse", "PRESETS")
                    onClicked: root.presetMenuRequested()
                }
                DjLitButton {
                    text: root.rec && root.isPlaying ? qsTrc("mixhouse", "● REC") : qsTrc("mixhouse", "REC")
                    lit: root.rec
                    litColor: "#e0505a"
                    onClicked: root.rec = !root.rec
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 18

                // ---- left: SOUND COLOR FX -----------------------------------
                ColumnLayout {
                    Layout.alignment: Qt.AlignTop
                    Layout.preferredWidth: 130
                    spacing: 8

                    SectionTitle {
                        text: qsTrc("mixhouse", "SOUND COLOR FX")
                    }
                    GridLayout {
                        columns: 2
                        columnSpacing: 4
                        rowSpacing: 4
                        // Only FILTER is in GlideMix: it is what the COLOR knobs do.
                        DjLitButton { Layout.fillWidth: true; text: qsTrc("mixhouse", "DUB ECHO"); enabled: false }
                        DjLitButton { Layout.fillWidth: true; text: qsTrc("mixhouse", "SWEEP"); enabled: false }
                        DjLitButton { Layout.fillWidth: true; text: qsTrc("mixhouse", "NOISE"); enabled: false }
                        DjLitButton { Layout.fillWidth: true; text: qsTrc("mixhouse", "FILTER"); lit: true }
                    }
                    DjKnob {
                        Layout.alignment: Qt.AlignHCenter
                        size: 40
                        label: qsTrc("mixhouse", "PARAMETER")
                        value: 0.5
                        enabled: false
                        opacity: 0.35
                    }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: "#6c6f73"
                        font.pixelSize: 8
                        text: qsTrc("mixhouse", "COLOR: left low-pass, right high-pass.")
                    }
                }

                // ---- the channel section: CH 1 | meters and curves | CH 2 ----
                Item {
                    id: section
                    Layout.alignment: Qt.AlignTop
                    implicitWidth: 380
                    implicitHeight: 620
                    readonly property real axis1: 62
                    readonly property real axis2: 318
                    readonly property real centre: 190

                    // the SOUND COLOR FX band behind both COLOR knobs
                    Rectangle {
                        x: 0
                        y: 246
                        width: parent.width
                        height: 82
                        radius: 4
                        color: "#15171a"
                        border.color: "#2c2f33"
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            y: 8
                            text: qsTrc("mixhouse", "SOUND COLOR FX")
                            color: "#9a9890"
                            font.pixelSize: 8
                            font.bold: true
                            font.letterSpacing: 1.4
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            y: 40
                            text: qsTrc("mixhouse", "LOW ◀  FILTER  ▶ HI")
                            color: "#6c6f73"
                            font.pixelSize: 8
                        }
                    }

                    ChannelColumn {
                        x: section.axis1 - width / 2
                        side: "out"
                        number: "1"
                        accent: root.outColor
                        mixer: root
                    }
                    ChannelColumn {
                        x: section.axis2 - width / 2
                        side: "in"
                        number: "2"
                        accent: root.inColor
                        mixer: root
                    }

                    // meters: CH 1 / MASTER / CH 2
                    Row {
                        x: section.centre - width / 2
                        y: 50
                        spacing: 6
                        opacity: 0.8
                        Meter { label: "1" }
                        Meter { label: "M" }
                        Meter { label: "2" }
                    }

                    // between the channel faders: EQ CURVE and CROSS F. CURVE
                    Column {
                        x: section.centre - width / 2
                        y: 372
                        spacing: 6
                        SectionTitle {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: qsTrc("mixhouse", "EQ CURVE")
                            font.pixelSize: 8
                        }
                        // Waits for the DJM EQ / isolator DSP: the lanes use the
                        // current transition EQ meanwhile.
                        Row {
                            anchors.horizontalCenter: parent.horizontalCenter
                            spacing: 3
                            DjLitButton { text: qsTrc("mixhouse", "ISO"); enabled: false }
                            DjLitButton { text: qsTrc("mixhouse", "EQ"); lit: true; enabled: false }
                        }
                        Item { width: 1; height: 8 }
                        SectionTitle {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: qsTrc("mixhouse", "CROSS F. CURVE")
                            font.pixelSize: 8
                        }
                        Repeater {
                            model: [qsTrc("mixhouse", "SMOOTH"), qsTrc("mixhouse", "CONSTANT"), qsTrc("mixhouse", "CUT")]
                            delegate: DjLitButton {
                                required property var modelData
                                required property int index
                                anchors.horizontalCenter: parent.horizontalCenter
                                implicitWidth: 74
                                text: modelData
                                lit: root.xfCurve === index
                                litColor: "#3fa9ff"
                                onClicked: root.xfCurve = index
                            }
                        }
                    }

                    // the crossfader, centred between the two channel axes
                    DjFader {
                        id: xfader
                        x: section.centre - width / 2
                        y: 556
                        implicitWidth: section.axis2 - section.axis1 + 28
                        horizontal: true
                        label: qsTrc("mixhouse", "CROSSFADER")
                        defaultValue: 0.5
                        value: root.xfPosition()
                        onGrabbed: function(down) {
                            if (down) {
                                root.beginMove([
                                    { lane: 0, side: "out", map: function(x) { return root.xfGain(x, "out") } },
                                    { lane: 0, side: "in", map: function(x) { return root.xfGain(x, "in") } }
                                ])
                            } else {
                                root.endMove()
                            }
                        }
                        onMoved: function(v) { root.moveTo(v) }
                    }
                    Text {
                        x: xfader.x - width - 4
                        y: xfader.y + 22 - height / 2
                        text: "A"
                        color: root.outColor
                        font.pixelSize: 12
                        font.bold: true
                    }
                    Text {
                        x: xfader.x + xfader.width + 4
                        y: xfader.y + 22 - height / 2
                        text: "B"
                        color: root.inColor
                        font.pixelSize: 12
                        font.bold: true
                    }
                }

                // ---- right: BEAT FX (compact, DJM-S11 style select buttons) ----
                ColumnLayout {
                    Layout.alignment: Qt.AlignTop
                    Layout.preferredWidth: 216
                    Layout.maximumWidth: 216
                    Layout.fillHeight: true
                    spacing: 12

                    SectionTitle {
                        text: qsTrc("mixhouse", "BEAT FX")
                    }

                    // ◀ display ▶ : the beat buttons sit on the display, as on the hardware.
                    // The beat length is set by each effect for now, so they are dim.
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        DjLitButton { implicitWidth: 22; implicitHeight: 34; text: "◀"; enabled: false }
                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: 34
                            radius: 3
                            color: "#0b1418"
                            border.color: "#2c3a40"
                            Column {
                                anchors.centerIn: parent
                                spacing: 1
                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: root.fxInfo.title
                                    color: "#7fd0ff"
                                    font.pixelSize: 11
                                    font.bold: true
                                    font.family: "Menlo"
                                }
                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: qsTrc("mixhouse", "--- BPM   %1").arg(root.fxInfo.beats !== undefined
                                        ? qsTrc("mixhouse", "BEAT %1").arg(root.fxInfo.beats) : "")
                                    color: "#4f8fb3"
                                    font.pixelSize: 8
                                    font.family: "Menlo"
                                }
                            }
                        }
                        DjLitButton { implicitWidth: 22; implicitHeight: 34; text: "▶"; enabled: false }
                    }

                    // the effect select buttons; dim = not in GlideMix yet
                    GridLayout {
                        Layout.fillWidth: true
                        columns: 4
                        columnSpacing: 3
                        rowSpacing: 3
                        Repeater {
                            model: root.beatFx
                            delegate: DjLitButton {
                                required property var modelData
                                Layout.fillWidth: true
                                implicitWidth: 50
                                implicitHeight: 20
                                fontSize: 7
                                text: modelData.title
                                enabled: modelData.enabled !== false
                                lit: root.fxChosen === modelData.id
                                litColor: "#3fa9ff"
                                onClicked: root.fxChosen = modelData.id
                            }
                        }
                    }

                    // channel (fixed per effect for now) | LEVEL/DEPTH
                    RowLayout {
                        Layout.alignment: Qt.AlignHCenter
                        spacing: 28
                        DjRotarySelector {
                            choices: [
                                { id: "1", title: "1" },
                                { id: "2", title: "2" },
                                { id: "M", title: qsTrc("mixhouse", "M") }
                            ]
                            current: root.fxInfo.side !== undefined ? root.fxInfo.side : "1"
                            knobSize: 28
                            labelRadiusX: 22
                            labelRadiusY: 20
                            labelPad: 8
                            fromAngle: -70
                            toAngle: 70
                            interactive: false
                        }
                        DjKnob {
                            id: fxLevel
                            size: 42
                            big: true
                            label: qsTrc("mixhouse", "LEVEL/DEPTH")
                            accent: "#3fa9ff"
                            defaultValue: 0.5
                            value: root.fxCurrent ? root.fxCurrent.amount : 0.5
                            enabled: root.fxCurrent !== null
                            opacity: enabled ? 1 : 0.4
                            onGrabbed: function(down) {
                                if (!down && root.fxCurrent) {
                                    root.zonesModel.setEffectAmount(root.row, root.fxChosen, fxLevel.local)
                                }
                            }
                        }
                    }

                    // FX FREQUENCY: not in GlideMix's effects yet.
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 3
                        Text {
                            text: qsTrc("mixhouse", "FX FREQ")
                            color: "#6c6f73"
                            font.pixelSize: 7
                            font.bold: true
                        }
                        DjLitButton { Layout.fillWidth: true; implicitHeight: 18; fontSize: 7; text: qsTrc("mixhouse", "LOW"); enabled: false }
                        DjLitButton { Layout.fillWidth: true; implicitHeight: 18; fontSize: 7; text: qsTrc("mixhouse", "MID"); enabled: false }
                        DjLitButton { Layout.fillWidth: true; implicitHeight: 18; fontSize: 7; text: qsTrc("mixhouse", "HI"); enabled: false }
                    }

                    // The effects on this transition: click one to pick it, × to take it off.
                    Repeater {
                        model: root.fxAdded
                        delegate: RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 3
                            Rectangle {
                                Layout.fillWidth: true
                                implicitHeight: 18
                                radius: 3
                                color: root.fxChosen === modelData.kind ? "#1c2b36" : "#202225"
                                border.color: "#3a3e43"
                                Text {
                                    anchors.fill: parent
                                    anchors.leftMargin: 6
                                    anchors.rightMargin: 6
                                    verticalAlignment: Text.AlignVCenter
                                    elide: Text.ElideRight
                                    font.pixelSize: 8
                                    color: modelData.enabled ? "#e7e4dc" : "#7a7870"
                                    text: modelData.name + "  " + Math.round(modelData.amount * 100) + "%"
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: root.fxChosen = modelData.kind
                                }
                            }
                            DjLitButton {
                                implicitWidth: 20
                                implicitHeight: 18
                                text: "×"
                                fontSize: 10
                                onClicked: root.zonesModel.removeEffect(root.row, modelData.kind)
                            }
                        }
                    }

                    // the big ON/OFF, on its own at the bottom, as on the hardware
                    Item { Layout.fillHeight: true }
                    DjLitButton {
                        Layout.alignment: Qt.AlignHCenter
                        implicitWidth: 96
                        implicitHeight: 54
                        fontSize: 13
                        round: true
                        text: qsTrc("mixhouse", "ON/OFF")
                        enabled: root.fxInfo.enabled !== false
                        lit: root.fxCurrent !== null && root.fxCurrent.enabled
                        litColor: "#3fa9ff"
                        onClicked: {
                            if (!root.fxCurrent) {
                                root.zonesModel.addEffect(root.row, root.fxChosen)
                            } else {
                                root.zonesModel.setEffectEnabled(root.row, root.fxChosen, !root.fxCurrent.enabled)
                            }
                        }
                    }
                    Item { implicitHeight: 70 }
                }
            }

            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: "#7a7870"
                font.pixelSize: 10
                text: qsTrc("mixhouse", "CH 1 is the song going out, CH 2 the song coming in. A move sets a point at the playhead; with REC on, moves made while playing are written as played. Dim controls are not in GlideMix yet.")
            }
        }
    }
}
