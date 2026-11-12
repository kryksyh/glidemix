/*
 * GlideMix Samples B8: Beat. A channel-rack step sequencer -- the workflow DJs
 * know from FL Studio, in GlideMix's own look: up to 8 rows of your own
 * one-shots (drag them in from the Samples tab), 16 steps a bar.
 * Click or drag to paint steps, right-click or drag to erase, Shift-click for
 * accent / soft. The row's name button has the tools (fill, shift, reverse,
 * randomize...). Play loops it at the tempo of the mix; "Put on sample line"
 * renders it to one loop on the song's sample line.
 */
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls 2.15

import Muse.Ui 1.0
import Muse.UiComponents 1.0
import Audacity.MixHouse
import Audacity.UiComponents

StyledDialogView {
    id: root

    property int sampleLine: -1
    //! Set by "Edit beat" on a placed beat: that clip is updated in place.
    property int clipId: -1

    objectId: "MixHouseBeat"
    title: beat.clipId >= 0 ? qsTrc("mixhouse", "Edit beat") : qsTrc("mixhouse", "Beat")

    contentWidth: 1080
    //! Fixed: room for 8 rows and the graph, so the window never jumps
    //! while rows are added (a moving window took clicks meant for it).
    contentHeight: 180 + 9 * (rowHeight + rowSpacing) + graphHeight + 50
    margins: 14
    modal: false
    resizable: false
    //! Stays above the app: one-shots are dragged in from the Samples tab
    //! behind it, and the timeline is clicked while it is open.
    alwaysAboveApp: true

    readonly property int rowHeight: 34
    readonly property int rowSpacing: 6
    readonly property int headWidth: 360
    readonly property int stepCount: beat.bars * beat.stepsPerBar
    readonly property int spb: beat.stepsPerBar
    //! Steps in a beat: 4 (1/16), 8 (x2), 6 (triplets) -- the shading goes by beats.
    readonly property int beatSteps: spb / 4
    //! Up to 4 bars show at once; 8 and 16 bars are paged (page = 4 bars).
    //! The steps keep a comfortable size: up to 2 bars show at once and a
    //! longer pattern scrolls sideways (the scroll bar under the rows, or the
    //! wheel over the steps). pageStart is the first step on screen.
    readonly property int viewSteps: Math.min(stepCount, 2 * spb)
    property int pageStart: 0
    //! Snaps to whole bars, so the view always starts on a bar.
    function scrollTo(step) { pageStart = Math.max(0, Math.min(stepCount - viewSteps, Math.round(step / spb) * spb)) }
    onStepCountChanged: scrollTo(pageStart)
    readonly property int graphHeight: 120
    //! The row the Graph editor shows (click a name to pick it).
    property int selectedRow: 0
    //! velocity | pitch | stepPan | shift | repeat
    property string graphWhat: "velocity"

    onClosed: beat.stop()

    Connections {
        target: beat
        function onPlayStepChanged() {
            // The view follows the running light, a bar at a time.
            if (beat.playing && beat.playStep >= 0 && root.stepCount > root.viewSteps
                    && (beat.playStep < root.pageStart || beat.playStep >= root.pageStart + root.viewSteps)) {
                root.scrollTo(Math.floor(beat.playStep / root.spb) * root.spb)
            }
        }
    }

    MixHouseBeatModel {
        id: beat
        sampleLine: root.sampleLine
        // After sampleLine: the pattern is found through the clip on that line.
        Component.onCompleted: clipId = root.clipId
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        // Transport and pattern settings.
        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            FlatButton {
                Layout.preferredWidth: 36
                Layout.preferredHeight: 30
                icon: beat.playing ? IconCode.STOP : IconCode.PLAY
                accentButton: beat.playing
                toolTipTitle: beat.playing ? qsTrc("mixhouse", "Stop") : qsTrc("mixhouse", "Play the beat (loops at the tempo of the mix)")
                onClicked: beat.togglePlay()
            }
            // Tempo: the mix under the playhead, or the beat's own (a genre sets it).
            FlatButton {
                Layout.preferredWidth: 44
                Layout.preferredHeight: 26
                text: qsTrc("mixhouse", "Mix")
                accentButton: beat.followMix
                toolTipTitle: beat.followMix ? qsTrc("mixhouse", "Following the tempo of the mix: click to set the beat's own")
                                             : qsTrc("mixhouse", "Follow the tempo of the mix")
                onClicked: beat.followMix = !beat.followMix
            }
            FlatButton {
                Layout.preferredWidth: 26
                Layout.preferredHeight: 26
                text: "−"
                onClicked: { if (beat.followMix) { beat.ownBpm = beat.bpm; beat.followMix = false } beat.ownBpm = beat.ownBpm - 1 }
            }
            // Type your own tempo (Enter): the beat then keeps it instead of the mix's.
            // The wheel over it: a BPM a click (Shift: 0.1).
            TextInputField {
                id: bpmField
                Layout.preferredWidth: 70
                Layout.preferredHeight: 26
                currentText: beat.bpm.toFixed(1)
                measureUnitsSymbol: qsTrc("mixhouse", "BPM")
                validator: DoubleValidator { bottom: 60; top: 200; decimals: 1 }
                textSidePadding: 6
                onTextEditingFinished: function(newTextValue) {
                    const v = parseFloat(String(newTextValue).replace(",", "."))
                    if (!isNaN(v) && v >= 60 && v <= 200) {
                        beat.followMix = false
                        beat.ownBpm = v
                    }
                    currentText = beat.bpm.toFixed(1)
                }
                // Typing replaced the binding: follow the tempo again (genre, -/+, mix).
                Connections {
                    target: beat
                    function onBpmChanged() { bpmField.currentText = beat.bpm.toFixed(1) }
                }
                WheelHandler {
                    onWheel: function(wheel) {
                        const d = (wheel.angleDelta.y > 0 ? 1 : -1) * ((wheel.modifiers & Qt.ShiftModifier) ? 0.1 : 1)
                        beat.followMix = false
                        beat.ownBpm = beat.ownBpm + d
                    }
                }
            }
            FlatButton {
                Layout.preferredWidth: 26
                Layout.preferredHeight: 26
                text: "+"
                onClicked: { if (beat.followMix) { beat.ownBpm = beat.bpm; beat.followMix = false } beat.ownBpm = beat.ownBpm + 1 }
            }

            StyledTextLabel { text: qsTrc("mixhouse", "Bars"); opacity: 0.7 }
            Repeater {
                model: [1, 2, 4, 8, 16]
                FlatButton {
                    required property int modelData
                    Layout.preferredWidth: 32
                    Layout.preferredHeight: 26
                    text: modelData
                    toolTipTitle: modelData === 1 ? qsTrc("mixhouse", "1 bar") : qsTrc("mixhouse", "%1 bars").arg(modelData)
                    accentButton: beat.bars === modelData
                    onClicked: beat.bars = modelData
                }
            }
            // The grid (Serato's Double and Triplet grid).
            Repeater {
                model: [
                    { spb: 16, title: "1/16", tip: qsTrc("mixhouse", "16 steps a bar") },
                    { spb: 32, title: "×2", tip: qsTrc("mixhouse", "Double grid: 32 steps a bar (1/32)") },
                    { spb: 24, title: "3", tip: qsTrc("mixhouse", "Triplet grid: 24 steps a bar") }
                ]
                FlatButton {
                    required property var modelData
                    Layout.preferredWidth: modelData.spb === 16 ? 38 : 28
                    Layout.preferredHeight: 26
                    text: modelData.title
                    toolTipTitle: modelData.tip
                    accentButton: beat.stepsPerBar === modelData.spb
                    onClicked: beat.stepsPerBar = modelData.spb
                }
            }

            Item { Layout.preferredWidth: 12 }
            // Swing the MPC way: 50 % is straight, ~54-58 % loosens it, ~66 % is a
            // triplet feel, 75 % the most. Every second 16th moves.
            StyledTextLabel { text: qsTrc("mixhouse", "Swing"); opacity: 0.7 }
            DjSlider {
                Layout.preferredWidth: 64
                from: 50
                to: 75
                stepSize: 1
                value: beat.swingPercent
                onMoved: beat.swingPercent = value
                ToolTip.visible: hovered || pressed
                ToolTip.text: qsTrc("mixhouse", "Swing %1% (50 % straight, 54-58 % loose, 66 % triplet)").arg(Math.round(value))
            }
            StyledTextLabel {
                Layout.preferredWidth: 30
                text: Math.round(beat.swingPercent) + "%"
                font: ui.theme.bodyBoldFont
            }
            Item { Layout.fillWidth: true }

            FlatButton {
                Layout.preferredWidth: 60
                Layout.preferredHeight: 30
                text: qsTrc("mixhouse", "Clear")
                onClicked: beat.clearAll()
            }
            FlatButton {
                Layout.preferredWidth: 140
                Layout.preferredHeight: 30
                text: beat.clipId >= 0 ? qsTrc("mixhouse", "Update beat") : qsTrc("mixhouse", "Put on sample line")
                accentButton: true
                enabled: beat.rows.length > 0 && root.sampleLine >= 0
                toolTipTitle: qsTrc("mixhouse", "One loop on the song's sample line, on the bar under the playhead")
                onClicked: beat.putOnSampleLine()
            }
        }

        // Beat starter (the Loop Starter idea, from your own Samples tab): a genre
        // fills the rack with one-shots of the right kinds and a groove.
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            StyledTextLabel { text: qsTrc("mixhouse", "Start a beat"); font: ui.theme.bodyBoldFont }
            StyledDropdown {
                id: genreBox
                Layout.preferredWidth: 220
                textRole: "label"
                valueRole: "id"
                // Every genre and your patterns in view, no scrolling.
                popupItemsCount: 40
                // The genres, then your own saved patterns (Serato's "User").
                model: beat.genres.map(g => ({ id: g.id, label: g.title + "  ·  " + g.bpm + " BPM" }))
                       .concat(beat.savedPatterns.length > 0 ? [{ id: "user", label: qsTrc("mixhouse", "Your patterns: any") }] : [])
                       .concat(beat.savedPatterns.map(n => ({ id: "pattern:" + n, label: qsTrc("mixhouse", "Your pattern: %1").arg(n) })))
                currentIndex: beat.genres.findIndex(g => g.id === beat.genre)
                displayText: currentIndex >= 0 ? beat.genres[currentIndex].title
                             : beat.genre === "user" ? qsTrc("mixhouse", "Your pattern") : qsTrc("mixhouse", "Choose a genre")
                onActivated: function(index, value) {
                    if (value === "user") {
                        beat.loadPattern("")
                    } else if (String(value).startsWith("pattern:")) {
                        beat.loadPattern(String(value).substring(8))
                    } else {
                        beat.startBeat(value)
                    }
                }
            }
            FlatButton {
                Layout.preferredWidth: 100
                Layout.preferredHeight: 26
                orientation: Qt.Horizontal
                icon: IconCode.UPDATE
                text: qsTrc("mixhouse", "Random")
                toolTipTitle: qsTrc("mixhouse", "Any genre's kit and tempo, with a groove of its own")
                onClicked: beat.startBeat("random")
            }
            FlatButton {
                Layout.preferredWidth: 150
                Layout.preferredHeight: 26
                orientation: Qt.Horizontal
                icon: IconCode.UPDATE
                text: qsTrc("mixhouse", "New sounds")
                enabled: beat.rows.length > 0
                toolTipTitle: qsTrc("mixhouse", "Another sample of the same kind on every row that isn't locked")
                onClicked: beat.diceAll()
            }

            Item { Layout.fillWidth: true }
            FlatButton {
                Layout.preferredWidth: 120
                Layout.preferredHeight: 26
                text: qsTrc("mixhouse", "Save pattern")
                enabled: beat.rows.length > 0
                toolTipTitle: qsTrc("mixhouse", "Keep this beat in the genre menu under Your patterns, to start from it again")
                onClicked: beat.savePattern("")
            }
        }

        StyledTextLabel {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignLeft
            opacity: 0.6
            font.pixelSize: 11
            text: qsTrc("mixhouse", "Drag one-shots from the Samples tab onto a row. Click or drag to set steps, right-click or drag to clear them, Shift-click for accent or soft. Click a name to hear it (and show its graph), right-click it for its tools.")
        }

        // The rows.
        Column {
            Layout.fillWidth: true
            spacing: root.rowSpacing

            // A count, not the list: the rows stay the same items while a
            // drag paints across them (a new list would rebuild them mid-drag).
            Repeater {
                model: beat.rows.length

                delegate: Row {
                    id: rowItem
                    required property int index
                    readonly property var modelData: beat.rows[index] !== undefined ? beat.rows[index] : ({ steps: [] })
                    spacing: 6

                    // The light, FL-style: click mutes, right-click or Ctrl-click solos.
                    Rectangle {
                        width: 16; height: 16; radius: 8
                        anchors.verticalCenter: parent.verticalCenter
                        color: rowItem.modelData.mute ? ui.theme.backgroundSecondaryColor : ui.theme.accentColor
                        border.width: rowItem.modelData.solo ? 3 : 1
                        border.color: rowItem.modelData.solo ? ui.theme.fontPrimaryColor : ui.theme.strokeColor
                        MouseArea {
                            id: ledMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            onClicked: function(mouse) {
                                if (mouse.button === Qt.RightButton || (mouse.modifiers & Qt.ControlModifier)) {
                                    beat.toggleSolo(rowItem.index)
                                } else {
                                    beat.toggleMute(rowItem.index)
                                }
                            }
                        }
                        ToolTip.visible: ledMouse.containsMouse
                        ToolTip.text: rowItem.modelData.solo ? qsTrc("mixhouse", "Solo: right-click to hear all rows")
                                      : rowItem.modelData.mute ? qsTrc("mixhouse", "Muted: click to hear it")
                                      : qsTrc("mixhouse", "Click to mute, right-click to solo")
                    }
                    DjSlider {
                        width: 46
                        anchors.verticalCenter: parent.verticalCenter
                        from: -1; to: 1; stepSize: 0.01
                        value: rowItem.modelData.pan
                        onMoved: beat.setPan(rowItem.index, value)
                        ToolTip.visible: hovered || pressed
                        ToolTip.text: Math.abs(value) < 0.01 ? qsTrc("mixhouse", "Pan: centre")
                                      : value < 0 ? qsTrc("mixhouse", "Pan: %1% left").arg(Math.round(-value * 100))
                                                  : qsTrc("mixhouse", "Pan: %1% right").arg(Math.round(value * 100))
                    }
                    DjSlider {
                        width: 70
                        anchors.verticalCenter: parent.verticalCenter
                        from: 0; to: 1; stepSize: 0.01
                        value: rowItem.modelData.gain
                        onMoved: beat.setGain(rowItem.index, value)
                        ToolTip.visible: hovered || pressed
                        ToolTip.text: qsTrc("mixhouse", "Volume: %1%").arg(Math.round(value * 100))
                    }

                    // The name button: drop a one-shot on it, right-click for the tools.
                    Rectangle {
                        width: root.headWidth - 16 - 46 - 70 - 22 - 22 - 30
                        height: root.rowHeight
                        radius: 4
                        color: nameDrop.containsDrag ? Qt.rgba(ui.theme.accentColor.r, ui.theme.accentColor.g, ui.theme.accentColor.b, 0.35)
                                                     : ui.theme.buttonColor
                        border.color: root.selectedRow === rowItem.index ? ui.theme.accentColor : ui.theme.strokeColor
                        border.width: root.selectedRow === rowItem.index ? 2 : 1
                        // The kind of sound at a glance: kick red (bass-heavy), snare and clap
                        // teal, hats blue, percussion green, anything else the accent.
                        Rectangle {
                            x: 3; y: 4
                            width: 4; height: parent.height - 8
                            radius: 2
                            color: ({ kick: "#E5484D", snare: "#26B5A6", clap: "#26B5A6", hat: "#4C8DFF",
                                      openhat: "#7C6BFF", perc: "#46B85C" })[rowItem.modelData.role] || ui.theme.accentColor
                        }
                        StyledTextLabel {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 8
                            horizontalAlignment: Text.AlignLeft
                            elide: Text.ElideRight
                            text: rowItem.modelData.name
                        }
                        DropArea {
                            id: nameDrop
                            anchors.fill: parent
                            onDropped: function(drop) {
                                if (drop.hasUrls && drop.urls.length > 0) {
                                    beat.setRowSample(rowItem.index, drop.urls[0].toString())
                                    drop.accept()
                                }
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            onClicked: function(mouse) {
                                if (mouse.button === Qt.LeftButton) {
                                    root.selectedRow = rowItem.index   // the Graph editor shows this row
                                    beat.previewRow(rowItem.index)   // hear it
                                    return
                                }
                                toolsMenu.row = rowItem.index
                                toolsMenu.show(Qt.point(mouse.x, mouse.y), [
                                    { id: "fill1", title: qsTrc("mixhouse", "Fill every step") },
                                    { id: "fill2", title: qsTrc("mixhouse", "Fill every 2 steps") },
                                    { id: "fill4", title: qsTrc("mixhouse", "Fill every 4 steps") },
                                    { id: "fill8", title: qsTrc("mixhouse", "Fill every 8 steps") },
                                    { },
                                    { id: "even3", title: qsTrc("mixhouse", "Even out: 3 hits a bar") },
                                    { id: "even5", title: qsTrc("mixhouse", "Even out: 5 hits a bar") },
                                    { id: "even7", title: qsTrc("mixhouse", "Even out: 7 hits a bar") },
                                    { },
                                    { id: "left", title: qsTrc("mixhouse", "Rotate left") },
                                    { id: "right", title: qsTrc("mixhouse", "Rotate right") },
                                    { id: "reverse", title: qsTrc("mixhouse", "Reverse") },
                                    { id: "invert", title: qsTrc("mixhouse", "Invert") },
                                    { id: "random", title: qsTrc("mixhouse", "Randomize") },
                                    { id: "groove", title: qsTrc("mixhouse", "New groove") },
                                    { id: "key", title: qsTrc("mixhouse", "Tune to the song's key") },
                                    { },
                                    { },
                                    { id: "cutself", title: qsTrc("mixhouse", "Cut itself"), checkable: true, checked: rowItem.modelData.cutSelf },
                                    { id: "swing100", title: qsTrc("mixhouse", "Swing mix 100 %"), checkable: true, checked: rowItem.modelData.swingMix > 0.99 },
                                    { id: "swing50", title: qsTrc("mixhouse", "Swing mix 50 %"), checkable: true, checked: Math.abs(rowItem.modelData.swingMix - 0.5) < 0.01 },
                                    { id: "swing0", title: qsTrc("mixhouse", "Swing mix 0 % (straight)"), checkable: true, checked: rowItem.modelData.swingMix < 0.01 },
                                    { id: "choke", title: qsTrc("mixhouse", "Choke group"), subitems: [
                                        { id: "choke0", title: qsTrc("mixhouse", "None"), checkable: true, checked: rowItem.modelData.choke === 0 },
                                        { id: "choke1", title: qsTrc("mixhouse", "Group 1 (hats)"), checkable: true, checked: rowItem.modelData.choke === 1 },
                                        { id: "choke2", title: qsTrc("mixhouse", "Group 2"), checkable: true, checked: rowItem.modelData.choke === 2 },
                                        { id: "choke3", title: qsTrc("mixhouse", "Group 3"), checkable: true, checked: rowItem.modelData.choke === 3 },
                                        { id: "choke4", title: qsTrc("mixhouse", "Group 4"), checkable: true, checked: rowItem.modelData.choke === 4 }
                                    ] },
                                    { },
                                    { id: "fillin", title: qsTrc("mixhouse", "Fill in"), subitems: [
                                        { id: "fill/1", title: "1/1" }, { id: "fill/2", title: "1/2" }, { id: "fill/4", title: "1/4" },
                                        { id: "fill/8", title: "1/8" }, { id: "fill/16", title: "1/16" }, { id: "fill/32", title: "1/32" } ] },
                                    { id: "copysteps", title: qsTrc("mixhouse", "Copy steps") },
                                    { id: "pastesteps", title: qsTrc("mixhouse", "Paste steps"), enabled: beat.hasCopiedSteps },
                                    { },
                                    { id: "attack", title: qsTrc("mixhouse", "Attack"), subitems: [0, 0.005, 0.02, 0.05, 0.1, 0.3].map(v => (
                                        { id: "attack/" + v, title: v === 0 ? qsTrc("mixhouse", "Off") : Math.round(v * 1000) + " ms",
                                          checkable: true, checked: Math.abs(rowItem.modelData.attack - v) < 0.0005 })) },
                                    { id: "release", title: qsTrc("mixhouse", "Release"), subitems: [0, 0.05, 0.15, 0.4, 1].map(v => (
                                        { id: "release/" + v, title: v === 0 ? qsTrc("mixhouse", "Off") : (v >= 1 ? "1 s" : Math.round(v * 1000) + " ms"),
                                          checkable: true, checked: Math.abs(rowItem.modelData.release - v) < 0.0005 })) },
                                    { id: "keyshift", title: qsTrc("mixhouse", "Key shift"), subitems: [-12, -7, -5, -2, -1, 0, 1, 2, 5, 7, 12].map(v => (
                                        { id: "key/" + v, title: v === 0 ? "0" : (v > 0 ? "+" + v : "" + v) + " st",
                                          checkable: true, checked: rowItem.modelData.keyShift === v })) },
                                    { id: "clone", title: qsTrc("mixhouse", "Clone row") },
                                    { id: "clear", title: qsTrc("mixhouse", "Clear") },
                                    { id: "remove", title: qsTrc("mixhouse", "Remove row") }
                                ])
                            }
                        }
                    }

                    // The dice (another sample of the same kind) and the lock.
                    FlatButton {
                        width: 22; height: 22
                        anchors.verticalCenter: parent.verticalCenter
                        transparent: true
                        icon: IconCode.UPDATE
                        enabled: !rowItem.modelData.locked
                        toolTipTitle: qsTrc("mixhouse", "Another sample of this kind")
                        onClicked: beat.diceRow(rowItem.index)
                    }
                    FlatButton {
                        width: 22; height: 22
                        anchors.verticalCenter: parent.verticalCenter
                        transparent: true
                        icon: rowItem.modelData.locked ? IconCode.LOCK_CLOSED : IconCode.LOCK_OPEN
                        accentButton: rowItem.modelData.locked === true
                        toolTipTitle: rowItem.modelData.locked ? qsTrc("mixhouse", "Locked: the dice and the starter leave this row alone")
                                                                : qsTrc("mixhouse", "Lock this row")
                        onClicked: beat.toggleLock(rowItem.index)
                    }

                    // The steps: one bar = 4 beats of 4, beats shaded in turn.
                    Item {
                        id: steps
                        width: root.contentWidth - root.headWidth - 6
                        height: root.rowHeight
                        readonly property real cell: width / root.viewSteps
                        property int paintLevel: -1

                        Repeater {
                            model: root.viewSteps
                            Rectangle {
                                required property int index
                                //! The step in the pattern (the page shows 4 bars of it).
                                readonly property int g: root.pageStart + index
                                // Past a short row's cycle: its repeats, faded and not editable here.
                                readonly property int cycle: rowItem.modelData.length !== undefined ? rowItem.modelData.length : root.stepCount
                                readonly property bool ghost: g >= cycle
                                readonly property int src: g % cycle
                                readonly property int level: rowItem.modelData.steps[src] !== undefined ? rowItem.modelData.steps[src] : 0
                                readonly property real vel: rowItem.modelData.velocity !== undefined ? rowItem.modelData.velocity[src] : 0.78
                                readonly property int reps: rowItem.modelData.repeat !== undefined ? rowItem.modelData.repeat[src] : 1
                                x: index * steps.cell + 1
                                width: steps.cell - 2
                                height: steps.height
                                radius: 3
                                color: level > 0 ? ui.theme.accentColor
                                                 : (Math.floor(g / root.beatSteps) % 2 === 0 ? ui.theme.buttonColor : ui.theme.backgroundSecondaryColor)
                                // Velocity shows as brightness, like the graph below.
                                opacity: (level > 0 ? 0.35 + 0.65 * vel : 1.0) * (ghost ? 0.35 : 1.0)
                                border.width: beat.playStep === g ? 2 : (g % root.spb === 0 ? 1 : 0)
                                border.color: beat.playStep === g ? ui.theme.fontPrimaryColor : ui.theme.strokeColor
                                // Accent: a bright bar along the top.
                                Rectangle {
                                    visible: parent.level > 0 && parent.vel >= 0.95
                                    anchors.top: parent.top
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    width: parent.width - 6
                                    height: 3
                                    radius: 1
                                    color: ui.theme.fontPrimaryColor
                                }
                                // Repeat: the step split into its hits.
                                Repeater {
                                    model: parent.level > 0 ? parent.reps - 1 : 0
                                    Rectangle {
                                        required property int index
                                        x: (index + 1) * parent.width / parent.reps - 1
                                        y: 4
                                        width: 2
                                        height: parent.height - 8
                                        color: ui.theme.backgroundPrimaryColor
                                    }
                                }
                            }
                        }

                        WheelHandler {
                            // Sideways (or Shift + wheel): through a long pattern.
                            onWheel: function(wheel) {
                                const d = wheel.angleDelta.x !== 0 ? -wheel.angleDelta.x : ((wheel.modifiers & Qt.ShiftModifier) ? -wheel.angleDelta.y : 0)
                                if (d !== 0) {
                                    root.scrollTo(root.pageStart + (d > 0 ? root.spb : -root.spb))
                                }
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            preventStealing: true
                            readonly property int cycle: rowItem.modelData.length !== undefined ? rowItem.modelData.length : root.stepCount
                            // A short row: a click on a repeat edits the step it repeats.
                            function stepAt(x) { return (root.pageStart + Math.max(0, Math.min(root.viewSteps - 1, Math.floor(x / steps.cell)))) % cycle }
                            onPressed: function(mouse) {
                                const s = stepAt(mouse.x)
                                const now = rowItem.modelData.steps[s]
                                if (mouse.button === Qt.RightButton) {
                                    steps.paintLevel = 0
                                } else if (mouse.modifiers & Qt.ShiftModifier) {
                                    steps.paintLevel = -1
                                    beat.cycleLevel(rowItem.index, s)
                                    return
                                } else {
                                    // FL: left-click sets a step (keeps its accent or soft), right-click clears it.
                                    steps.paintLevel = now > 0 ? now : 2
                                }
                                beat.setStep(rowItem.index, s, steps.paintLevel)
                                lastStep = s
                            }
                            property int lastStep: -1
                            onPositionChanged: function(mouse) {
                                if (steps.paintLevel >= 0) {
                                    // A fast stroke skips cells: paint every step between.
                                    const s = stepAt(mouse.x)
                                    const from = lastStep >= 0 ? lastStep : s
                                    for (let i = Math.min(from, s); i <= Math.max(from, s); ++i) {
                                        beat.setStep(rowItem.index, i, steps.paintLevel)
                                    }
                                    lastStep = s
                                }
                            }
                            onReleased: { steps.paintLevel = -1; lastStep = -1 }
                        }
                    }
                }
            }

            // Sideways through a long pattern (4, 8, 16 bars).
            Item {
                visible: root.stepCount > root.viewSteps
                width: root.contentWidth
                height: 14
                ScrollBar {
                    id: stepScroll
                    x: root.headWidth + 6
                    width: root.contentWidth - root.headWidth - 6
                    height: 12
                    orientation: Qt.Horizontal
                    policy: ScrollBar.AlwaysOn
                    size: root.viewSteps / root.stepCount
                    position: root.pageStart / root.stepCount
                    stepSize: 16 / root.stepCount
                    onPositionChanged: if (pressed || active) root.scrollTo(position * root.stepCount)
                }
                StyledTextLabel {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    opacity: 0.7
                    text: qsTrc("mixhouse", "Bars %1–%2 of %3").arg(Math.floor(root.pageStart / root.spb) + 1)
                          .arg(Math.ceil((root.pageStart + root.viewSteps) / root.spb)).arg(beat.bars)
                }
            }

            // Drop a one-shot here for a new row.
            Rectangle {
                visible: beat.rows.length < 8
                width: root.contentWidth
                height: root.rowHeight
                radius: 4
                color: addDrop.containsDrag ? Qt.rgba(ui.theme.accentColor.r, ui.theme.accentColor.g, ui.theme.accentColor.b, 0.35)
                                            : "transparent"
                border.color: ui.theme.strokeColor
                border.width: 1
                StyledTextLabel {
                    anchors.centerIn: parent
                    opacity: 0.6
                    text: beat.rows.length === 0 ? qsTrc("mixhouse", "Drag a kick, a clap, a hat... from the Samples tab here")
                                                 : qsTrc("mixhouse", "Drag another one-shot here for a new row")
                }
                DropArea {
                    id: addDrop
                    anchors.fill: parent
                    onDropped: function(drop) {
                        if (drop.hasUrls && drop.urls.length > 0) {
                            beat.setRowSample(beat.rows.length, drop.urls[0].toString())
                            drop.accept()
                        }
                    }
                }
            }
        }

        // The Graph editor (FL's graph, our look): one value per step of the
        // selected row. Draw with the mouse; right-drag draws a straight ramp;
        // Alt-click puts a step back to its default.
        ColumnLayout {
            Layout.fillWidth: true
            visible: beat.rows.length > 0
            spacing: 6

            RowLayout {
                spacing: 6
                StyledTextLabel {
                    Layout.preferredWidth: root.headWidth - 6
                    horizontalAlignment: Text.AlignLeft
                    elide: Text.ElideRight
                    font: ui.theme.bodyBoldFont
                    text: beat.rows[root.selectedRow] !== undefined
                          ? qsTrc("mixhouse", "Graph: %1").arg(beat.rows[root.selectedRow].name) : ""
                }
                // The lane to draw (like the step-lane menu of a sampler).
                StyledDropdown {
                    Layout.preferredWidth: 170
                    textRole: "title"
                    valueRole: "id"
                    model: [
                        { id: "velocity", title: qsTrc("mixhouse", "Velocity") },
                        { id: "pitch", title: qsTrc("mixhouse", "Pitch") },
                        { id: "gate", title: qsTrc("mixhouse", "Length") },
                        { id: "reverse", title: qsTrc("mixhouse", "Reverse") },
                        { id: "stepPan", title: qsTrc("mixhouse", "Pan") },
                        { id: "shift", title: qsTrc("mixhouse", "Shift") },
                        { id: "repeat", title: qsTrc("mixhouse", "Repeat") },
                        { id: "drive", title: qsTrc("mixhouse", "FX: Distortion") },
                        { id: "reverb", title: qsTrc("mixhouse", "FX: Reverb") },
                        { id: "filter", title: qsTrc("mixhouse", "FX: Filter") },
                        { id: "fill", title: qsTrc("mixhouse", "Advanced fill") }
                    ]
                    currentIndex: model.findIndex(m => m.id === root.graphWhat)
                    onActivated: function(index, value) { root.graphWhat = value }
                }
                // Quick fills for the selected row.
                Repeater {
                    model: [
                        { id: "q4", title: "1/4" }, { id: "q8", title: "1/8" }, { id: "q16", title: "1/16" },
                        { id: "offbeat", title: qsTrc("mixhouse", "offbeat") }, { id: "clear", title: qsTrc("mixhouse", "clear") },
                        { id: "rnd", title: qsTrc("mixhouse", "RND") }
                    ]
                    FlatButton {
                        required property var modelData
                        Layout.preferredWidth: modelData.title.length > 4 ? 64 : 44
                        Layout.preferredHeight: 24
                        text: modelData.title
                        onClicked: beat.quickFill(root.selectedRow, modelData.id)
                    }
                }
                Item { Layout.fillWidth: true }
                StyledTextLabel {
                    id: graphReadout
                    opacity: 0.7
                    text: ""
                }
            }

            // Advanced fill (FL's tool, our look): hits spread over a cycle of
            // steps, turned; a cycle shorter than the pattern repeats on its own
            // (polymeter: a 3-step perc over a 16-step kick).
            RowLayout {
                id: fillPanel
                visible: root.graphWhat === "fill"
                Layout.preferredHeight: root.graphHeight
                Layout.leftMargin: root.headWidth
                spacing: 10
                property int hits: 4
                property int length: root.stepCount
                property int rotate: 0
                property bool euclidean: true
                function apply() { beat.advancedFill(root.selectedRow, hits, length, rotate, euclidean) }
                onVisibleChanged: if (visible && beat.rows[root.selectedRow] !== undefined) {
                    const r = beat.rows[root.selectedRow]
                    length = r.length
                    hits = r.steps.slice(0, r.length).filter(v => v > 0).length
                    rotate = 0
                }

                Repeater {
                    model: [
                        { key: "hits", title: qsTrc("mixhouse", "Hits"), min: 0 },
                        { key: "length", title: qsTrc("mixhouse", "Steps in the cycle"), min: 1 },
                        { key: "rotate", title: qsTrc("mixhouse", "Rotate"), min: -256 }
                    ]
                    ColumnLayout {
                        required property var modelData
                        spacing: 4
                        StyledTextLabel { text: modelData.title; opacity: 0.7 }
                        RowLayout {
                            spacing: 4
                            FlatButton {
                                Layout.preferredWidth: 26; Layout.preferredHeight: 26
                                text: "−"
                                onClicked: {
                                    fillPanel[modelData.key] = Math.max(modelData.min, fillPanel[modelData.key] - 1)
                                    fillPanel.hits = Math.min(fillPanel.hits, fillPanel.length)
                                    fillPanel.apply()
                                }
                            }
                            StyledTextLabel {
                                Layout.preferredWidth: 34
                                text: fillPanel[modelData.key]
                                font: ui.theme.bodyBoldFont
                            }
                            FlatButton {
                                Layout.preferredWidth: 26; Layout.preferredHeight: 26
                                text: "+"
                                onClicked: {
                                    const max = modelData.key === "hits" ? fillPanel.length : modelData.key === "length" ? root.stepCount : root.stepCount
                                    fillPanel[modelData.key] = Math.min(max, fillPanel[modelData.key] + 1)
                                    fillPanel.apply()
                                }
                            }
                        }
                    }
                }
                ColumnLayout {
                    spacing: 4
                    StyledTextLabel { text: qsTrc("mixhouse", "Spread"); opacity: 0.7 }
                    RowLayout {
                        spacing: 4
                        FlatButton {
                            Layout.preferredWidth: 86; Layout.preferredHeight: 26
                            text: qsTrc("mixhouse", "Euclidean")
                            accentButton: fillPanel.euclidean
                            toolTipTitle: qsTrc("mixhouse", "The hits as evenly as they go: 3 in 8 is the tresillo, 5 in 8 the cinquillo")
                            onClicked: { fillPanel.euclidean = true; fillPanel.apply() }
                        }
                        FlatButton {
                            Layout.preferredWidth: 70; Layout.preferredHeight: 26
                            text: qsTrc("mixhouse", "Every")
                            accentButton: !fillPanel.euclidean
                            toolTipTitle: qsTrc("mixhouse", "A hit every (steps / hits) steps")
                            onClicked: { fillPanel.euclidean = false; fillPanel.apply() }
                        }
                    }
                }
                StyledTextLabel {
                    Layout.preferredWidth: 200
                    wrapMode: Text.WordWrap
                    opacity: 0.6
                    font.pixelSize: 11
                    text: qsTrc("mixhouse", "Fewer steps than the pattern and the row repeats on its own cycle: 3 steps against 16 rolls over the bar. The faded steps show the repeats.")
                }
            }

            Item {
                id: graph
                visible: root.graphWhat !== "fill"
                Layout.preferredWidth: root.contentWidth
                Layout.preferredHeight: root.graphHeight
                readonly property var rowData: beat.rows[root.selectedRow] !== undefined ? beat.rows[root.selectedRow] : null
                readonly property bool centred: root.graphWhat === "pitch" || root.graphWhat === "stepPan"
                readonly property real area: width - root.headWidth
                readonly property real cell: area / root.viewSteps

                function valueAt(step) {
                    if (!rowData || rowData[root.graphWhat] === undefined) {
                        return 0
                    }
                    return rowData[root.graphWhat][step]
                }
                //! A value as 0..1 of the height (centred graphs: 0.5 = zero).
                function norm(v) {
                    switch (root.graphWhat) {
                    case "pitch": return 0.5 + v / 48
                    case "stepPan": return 0.5 + v / 2
                    case "repeat": return v / 4
                    case "reverse": return v > 0.5 ? 1 : 0.04
                    default: return v
                    }
                }
                function fromY(y) {
                    const f = Math.max(0, Math.min(1, 1 - y / height))
                    switch (root.graphWhat) {
                    case "pitch": return Math.round((f - 0.5) * 48)
                    case "stepPan": return (f - 0.5) * 2
                    case "repeat": return Math.max(1, Math.min(4, Math.ceil(f * 4)))
                    case "reverse": return f >= 0.5 ? 1 : 0
                    case "gate": return Math.max(0.05, f)
                    default: return f
                    }
                }
                function label(v) {
                    switch (root.graphWhat) {
                    case "pitch": return (v > 0 ? "+" : "") + v + qsTrc("mixhouse", " st")
                    case "stepPan": return Math.abs(v) < 0.01 ? qsTrc("mixhouse", "centre")
                                         : Math.round(Math.abs(v) * 100) + (v < 0 ? qsTrc("mixhouse", "% left") : qsTrc("mixhouse", "% right"))
                    case "shift": return Math.round(v * 100) + qsTrc("mixhouse", "% of a step late")
                    case "repeat": return v === 1 ? qsTrc("mixhouse", "1 hit") : qsTrc("mixhouse", "%1 hits").arg(v)
                    case "reverse": return v > 0.5 ? qsTrc("mixhouse", "backwards") : qsTrc("mixhouse", "forwards")
                    case "gate": return v >= 0.999 ? qsTrc("mixhouse", "full length") : Math.round(v * 100) + qsTrc("mixhouse", "% of the sound")
                    case "filter": return v >= 0.999 ? qsTrc("mixhouse", "open") : Math.round(150 * Math.pow(20000 / 150, v)) + " Hz"
                    case "drive": return Math.round(v * 100) + qsTrc("mixhouse", "% distortion")
                    case "reverb": return Math.round(v * 100) + qsTrc("mixhouse", "% reverb")
                    default: return Math.round(v * 100) + "%"
                    }
                }

                Rectangle {
                    x: root.headWidth
                    width: graph.area
                    height: parent.height
                    radius: 4
                    color: ui.theme.backgroundSecondaryColor
                    border.color: ui.theme.strokeColor
                    // The zero line of pitch and pan.
                    Rectangle {
                        visible: graph.centred
                        y: parent.height / 2
                        width: parent.width
                        height: 1
                        color: ui.theme.strokeColor
                    }
                    Repeater {
                        model: root.viewSteps
                        Rectangle {
                            required property int index
                            readonly property int g: root.pageStart + index
                            readonly property real f: Math.max(0, Math.min(1, graph.norm(graph.valueAt(g))))
                            readonly property bool on: graph.rowData && graph.rowData.steps[g] > 0
                            x: index * graph.cell + 2
                            width: Math.max(2, graph.cell - 4)
                            y: graph.centred ? Math.min(f, 0.5) * 0 + (1 - Math.max(f, 0.5)) * parent.height : (1 - f) * parent.height
                            height: graph.centred ? Math.max(2, Math.abs(f - 0.5) * parent.height) : Math.max(2, f * parent.height)
                            radius: 2
                            color: ui.theme.accentColor
                            opacity: on ? 0.9 : 0.25
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        preventStealing: true
                        hoverEnabled: true
                        property int rampStep: -1
                        property real rampValue: 0
                        property int lastStep: -1
                        property real lastValue: 0
                        function stepAt(x) { return root.pageStart + Math.max(0, Math.min(root.viewSteps - 1, Math.floor(x / graph.cell))) }
                        function draw(mouse) {
                            const s = stepAt(mouse.x)
                            const v = graph.fromY(mouse.y)
                            if (rampStep >= 0) {
                                // A straight line from where the right button went down.
                                const a = Math.min(rampStep, s), b = Math.max(rampStep, s)
                                for (let i = a; i <= b; ++i) {
                                    const t = b === a ? 1 : (i - rampStep) / (s - rampStep)
                                    beat.setStepValue(root.selectedRow, i, root.graphWhat, rampValue + (v - rampValue) * t)
                                }
                            } else if (lastStep >= 0 && lastStep !== s) {
                                // A fast stroke skips cells: draw the line between the two points.
                                const dir = s > lastStep ? 1 : -1
                                for (let i = lastStep + dir; i !== s + dir; i += dir) {
                                    const t = (i - lastStep) / (s - lastStep)
                                    beat.setStepValue(root.selectedRow, i, root.graphWhat, lastValue + (v - lastValue) * t)
                                }
                            } else {
                                beat.setStepValue(root.selectedRow, s, root.graphWhat, v)
                            }
                            lastStep = s
                            lastValue = v
                            graphReadout.text = qsTrc("mixhouse", "Step %1: %2").arg(s + 1).arg(graph.label(graph.valueAt(s)))
                        }
                        onPressed: function(mouse) {
                            const s = stepAt(mouse.x)
                            if (mouse.modifiers & Qt.AltModifier) {
                                beat.resetStepValue(root.selectedRow, s, root.graphWhat)
                                return
                            }
                            if (mouse.button === Qt.RightButton) {
                                rampStep = s
                                rampValue = graph.fromY(mouse.y)
                            }
                            draw(mouse)
                        }
                        onPositionChanged: function(mouse) {
                            if (pressed) {
                                draw(mouse)
                            } else {
                                const s = stepAt(mouse.x)
                                graphReadout.text = qsTrc("mixhouse", "Step %1: %2").arg(s + 1).arg(graph.label(graph.valueAt(s)))
                            }
                        }
                        onReleased: { rampStep = -1; lastStep = -1 }
                        onExited: graphReadout.text = ""
                    }
                }
                StyledTextLabel {
                    width: root.headWidth - 12
                    anchors.verticalCenter: parent.verticalCenter
                    horizontalAlignment: Text.AlignLeft
                    wrapMode: Text.WordWrap
                    opacity: 0.6
                    font.pixelSize: 11
                    text: qsTrc("mixhouse", "Draw the values with the mouse. Right-drag draws a straight ramp, Alt-click resets a step. Click a row's name to show its graph.")
                }
            }
        }

        Item { Layout.fillHeight: true }
    }

    ContextMenuLoader {
        id: toolsMenu
        property int row: -1
        onHandleMenuItem: function(itemId) {
            const parts = String(itemId).split("/")
            if (parts.length === 2) {
                const v = parseFloat(parts[1])
                if (parts[0] === "fill") { beat.fillIn(row, v, false); return }
                if (parts[0] === "attack") { beat.setAttack(row, v); return }
                if (parts[0] === "release") { beat.setRelease(row, v); return }
                if (parts[0] === "key") { beat.setKeyShift(row, v); return }
            }
            switch (itemId) {
            case "fill1": beat.fill(row, 1); break
            case "fill2": beat.fill(row, 2); break
            case "fill4": beat.fill(row, 4); break
            case "fill8": beat.fill(row, 8); break
            case "even3": beat.evenOut(row, 3); break
            case "even5": beat.evenOut(row, 5); break
            case "even7": beat.evenOut(row, 7); break
            case "left": beat.shift(row, -1); break
            case "right": beat.shift(row, 1); break
            case "reverse": beat.reverse(row); break
            case "invert": beat.invert(row); break
            case "random": beat.randomize(row); break
            case "groove": beat.generateSteps(row); break
            case "key": beat.tuneToSong(row); break
            case "cutself": beat.toggleCutSelf(row); break
            case "swing100": beat.setSwingMix(row, 1.0); break
            case "swing50": beat.setSwingMix(row, 0.5); break
            case "swing0": beat.setSwingMix(row, 0.0); break
            case "clone": beat.cloneRow(row); break
            case "copysteps": beat.copySteps(row); break
            case "pastesteps": beat.pasteSteps(row); break
            case "choke0": case "choke1": case "choke2": case "choke3": case "choke4":
                beat.setChoke(row, parseInt(itemId.charAt(5))); break
            case "clear": beat.clearRow(row); break
            case "remove": beat.removeRow(row); break
            }
        }
    }
}
