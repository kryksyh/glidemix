/*
 * GlideMix -- the Beatgrid editor (Beatgrid workspace).
 *
 * One track: the overview across the top, the big waveform with its grid and
 * phrase ribbon, and below it rekordbox's GRID EDIT and PHRASE EDIT tools
 * laid out the way Lexicon DJ's Edit Grid view lays out its deck.
 */
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC

import Muse.Ui 1.0
import Muse.UiComponents 1.0

import Audacity.MixHouse

Rectangle {
    id: root

    property NavigationSection navigationSection: null

    color: "#12151b"

    MixHouseGridEditorModel {
        id: editor
    }

    function timeText(seconds) {
        if (!(seconds >= 0)) {
            return "0:00.0"
        }
        var m = Math.floor(seconds / 60)
        var s = seconds - m * 60
        return m + ":" + (s < 10 ? "0" : "") + s.toFixed(1)
    }

    component ToolLabel: StyledTextLabel {
        font.pixelSize: 10
        font.bold: true
        color: "#8b93a3"
    }

    component DeckButton: FlatButton {
        property bool checked: false
        buttonType: FlatButton.Horizontal
        isNarrow: true
        minWidth: 34
        height: 28
        accentButton: checked
        textFont.pixelSize: 11
        textFont.bold: true
    }

    component Group: Rectangle {
        default property alias content: row.data
        property string title: ""
        color: "#191d25"
        radius: 4
        implicitWidth: row.implicitWidth + 16
        implicitHeight: 62
        ToolLabel {
            x: 8
            y: 4
            text: parent.title
        }
        Row {
            id: row
            x: 8
            y: 24
            spacing: 4
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6

        // ---- header: track, BPM, position
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            spacing: 16

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                StyledTextLabel {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignLeft
                    text: editor.loaded || editor.loading ? editor.title : qsTrc("mixhouse", "Beatgrid editor")
                    font.pixelSize: 15
                    font.bold: true
                    color: "#eef0f4"
                }
                StyledTextLabel {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignLeft
                    text: editor.artist
                    color: "#9aa3b5"
                }
            }
            StyledTextLabel {
                text: editor.keyText
                font.pixelSize: 15
                font.bold: true
                color: "#c58cff"
                visible: text.length > 0
            }
            ColumnLayout {
                spacing: 0
                StyledTextLabel {
                    text: editor.bpm.toFixed(2)
                    font.pixelSize: 20
                    font.bold: true
                    color: editor.gridLocked ? "#ff8c00" : "#eef0f4"
                }
                ToolLabel {
                    Layout.alignment: Qt.AlignHCenter
                    text: editor.gridLocked ? qsTrc("mixhouse", "BPM · LOCKED") : qsTrc("mixhouse", "BPM")
                }
            }
            ColumnLayout {
                spacing: 0
                StyledTextLabel {
                    text: editor.barBeatText
                    font.pixelSize: 20
                    font.bold: true
                    color: "#4fa3ff"
                }
                ToolLabel {
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTrc("mixhouse", "BAR.BEAT")
                }
            }
            ColumnLayout {
                spacing: 0
                StyledTextLabel {
                    text: root.timeText(editor.positionSec)
                    font.pixelSize: 15
                    font.bold: true
                    color: "#eef0f4"
                }
                StyledTextLabel {
                    text: "-" + root.timeText(Math.max(0, editor.durationSec - editor.positionSec))
                    color: "#9aa3b5"
                }
            }
            Rectangle {
                radius: 3
                color: "#262b35"
                implicitWidth: sourceText.implicitWidth + 12
                implicitHeight: 20
                visible: editor.loaded
                StyledTextLabel {
                    id: sourceText
                    anchors.centerIn: parent
                    text: editor.gridSource
                    font.pixelSize: 10
                    color: "#c7ccd6"
                }
            }
        }

        MixHouseGridEditorView {
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            model: editor
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 120

            MixHouseGridEditorWave {
                anchors.fill: parent
                model: editor
                ribbonHeight: 20
            }

            StyledTextLabel {
                anchors.centerIn: parent
                width: parent.width - 40
                visible: !editor.loaded
                wrapMode: Text.WordWrap
                color: "#9aa3b5"
                font.pixelSize: 13
                text: editor.loading ? qsTrc("mixhouse", "Loading…")
                      : editor.errorText.length > 0 ? editor.errorText
                      : qsTrc("mixhouse", "Open a track here: right-click it in the Music library and choose Edit Beat Grid, or right-click a clip › Beat grid › Open in Beatgrid editor.")
            }
        }

        // ---- tools
        Flow {
            Layout.fillWidth: true
            spacing: 6
            enabled: editor.loaded

            Group {
                title: qsTrc("mixhouse", "DECK")
                DeckButton {
                    text: editor.playing ? qsTrc("mixhouse", "PAUSE") : qsTrc("mixhouse", "PLAY")
                    minWidth: 58
                    checked: editor.playing
                    onClicked: editor.playPause()
                }
                DeckButton {
                    id: cueButton
                    text: qsTrc("mixhouse", "CUE")
                    minWidth: 46
                    accentColor: "#ff8c00"
                    checked: cueButton.mouseArea.pressed
                    mouseArea.onPressed: editor.cuePressed()
                    mouseArea.onReleased: editor.cueReleased()
                    mouseArea.onCanceled: editor.cueReleased()
                    toolTipTitle: qsTrc("mixhouse", "Cue")
                    toolTipDescription: qsTrc("mixhouse", "Stopped: sets the cue here (on the beat with Quantize), or plays while held when on the cue. Playing: back to the cue")
                }
                DeckButton {
                    text: "Q"
                    checked: editor.quantize
                    onClicked: editor.quantize = !editor.quantize
                    toolTipTitle: qsTrc("mixhouse", "Quantize")
                    toolTipDescription: qsTrc("mixhouse", "Cues, loops, seeks and phrase cuts snap to the nearest beat (phrase cuts to the nearest bar)")
                }
                DeckButton {
                    text: qsTrc("mixhouse", "SLIP")
                    checked: editor.slip
                    onClicked: editor.slip = !editor.slip
                    toolTipTitle: qsTrc("mixhouse", "Slip mode")
                    toolTipDescription: qsTrc("mixhouse", "The track keeps playing silently under a loop or a waveform drag; when it ends, playback comes back where the track would have been")
                }
                DeckButton {
                    text: qsTrc("mixhouse", "CLICK")
                    checked: editor.metronome
                    onClicked: editor.metronome = !editor.metronome
                    toolTipTitle: qsTrc("mixhouse", "Metronome")
                    toolTipDescription: qsTrc("mixhouse", "Clicks on the grid's beats, higher on each bar's first beat, to hear whether the grid sits on the kicks. Right-click for level, sound and tuning")
                    // Right-click: the toolbar metronome's menu, as in DJ mode.
                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.RightButton
                        onClicked: function(mouse) {
                            metronomeMenu.show(Qt.point(mouse.x, mouse.y), editor.metronomeMenu())
                        }
                    }
                }
                StyledSlider {
                    width: 80
                    height: 28
                    // The toolbar metronome's level menu runs 25..100%.
                    from: 0.25
                    to: 1
                    value: editor.metronomeLevel
                    onMoved: editor.metronomeLevel = value
                }
                DeckButton {
                    id: metronomeMenuButton
                    text: "▾"
                    minWidth: 24
                    toolTipTitle: qsTrc("mixhouse", "Metronome settings")
                    toolTipDescription: qsTrc("mixhouse", "Level, sound and tuning, shared with the toolbar metronome")
                    onClicked: metronomeMenu.show(Qt.point(0, height), editor.metronomeMenu())
                    ContextMenuLoader {
                        id: metronomeMenu
                        onHandleMenuItem: function(itemId) {
                            editor.handleMetronomeMenu(itemId)
                        }
                    }
                }
            }

            Group {
                title: qsTrc("mixhouse", "LOOP · JUMP")
                DeckButton {
                    text: "‹"
                    onClicked: editor.halveLoop()
                    toolTipTitle: qsTrc("mixhouse", "Halve loop and jump size")
                }
                Rectangle {
                    width: 30
                    height: 28
                    radius: 3
                    color: "#0d0f13"
                    StyledTextLabel {
                        anchors.centerIn: parent
                        text: editor.loopBeats
                        font.bold: true
                        color: "#eef0f4"
                    }
                }
                DeckButton {
                    text: "›"
                    onClicked: editor.doubleLoop()
                    toolTipTitle: qsTrc("mixhouse", "Double loop and jump size")
                }
                DeckButton {
                    text: qsTrc("mixhouse", "LOOP")
                    checked: editor.loopActive
                    accentColor: "#3fb950"
                    onClicked: editor.toggleLoop()
                    toolTipTitle: qsTrc("mixhouse", "Loop")
                    toolTipDescription: qsTrc("mixhouse", "Loops this many beats from the playhead (from the beat with Quantize)")
                }
                DeckButton {
                    text: "|◀"
                    onClicked: editor.jump(-1)
                    toolTipTitle: qsTrc("mixhouse", "Beat jump back")
                }
                DeckButton {
                    text: "▶|"
                    onClicked: editor.jump(1)
                    toolTipTitle: qsTrc("mixhouse", "Beat jump forward")
                }
                DeckButton {
                    text: "◀◀"
                    onClicked: editor.jumpToPhrase(-1)
                    toolTipTitle: qsTrc("mixhouse", "Previous phrase")
                }
                DeckButton {
                    text: "▶▶"
                    onClicked: editor.jumpToPhrase(1)
                    toolTipTitle: qsTrc("mixhouse", "Next phrase")
                }
            }

            Group {
                title: qsTrc("mixhouse", "GRID EDIT")
                enabled: !editor.gridLocked
                DeckButton {
                    text: "◀◀"
                    onClicked: editor.shiftGrid(-10)
                    toolTipTitle: qsTrc("mixhouse", "Move the grid 10 ms earlier")
                }
                DeckButton {
                    text: "◀"
                    onClicked: editor.shiftGrid(-1)
                    toolTipTitle: qsTrc("mixhouse", "Move the grid 1 ms earlier")
                }
                DeckButton {
                    text: "▶"
                    onClicked: editor.shiftGrid(1)
                    toolTipTitle: qsTrc("mixhouse", "Move the grid 1 ms later")
                }
                DeckButton {
                    text: "▶▶"
                    onClicked: editor.shiftGrid(10)
                    toolTipTitle: qsTrc("mixhouse", "Move the grid 10 ms later")
                }
                DeckButton {
                    text: qsTrc("mixhouse", "BEAT")
                    onClicked: editor.setBeatHere()
                    toolTipTitle: qsTrc("mixhouse", "Beat here")
                    toolTipDescription: qsTrc("mixhouse", "Moves the grid so the nearest beat is on the playhead")
                }
                DeckButton {
                    text: "1.1"
                    accentColor: "#e53935"
                    onClicked: editor.setBarOneHere()
                    toolTipTitle: qsTrc("mixhouse", "Bar start here")
                    toolTipDescription: qsTrc("mixhouse", "The playhead becomes beat 1 of a bar")
                }
                TextInputField {
                    width: 70
                    height: 28
                    currentText: editor.bpm.toFixed(2)
                    textHorizontalAlignment: Text.AlignHCenter
                    onTextEditingFinished: function(newTextValue) {
                        var value = parseFloat(newTextValue)
                        if (!isNaN(value)) {
                            editor.setBpmValue(value)
                        }
                    }
                }
                DeckButton {
                    text: "−"
                    onClicked: editor.adjustBpm(-0.01)
                    toolTipTitle: qsTrc("mixhouse", "Widen the grid (0.01 BPM slower)")
                }
                DeckButton {
                    text: "+"
                    onClicked: editor.adjustBpm(0.01)
                    toolTipTitle: qsTrc("mixhouse", "Narrow the grid (0.01 BPM faster)")
                }
                DeckButton {
                    text: "×½"
                    onClicked: editor.scaleBpm(0.5)
                    toolTipTitle: qsTrc("mixhouse", "Half the BPM")
                }
                DeckButton {
                    text: "×2"
                    onClicked: editor.scaleBpm(2.0)
                    toolTipTitle: qsTrc("mixhouse", "Double the BPM")
                }
                DeckButton {
                    text: qsTrc("mixhouse", "TAP")
                    onClicked: editor.tap()
                    toolTipTitle: qsTrc("mixhouse", "Tap tempo")
                    toolTipDescription: qsTrc("mixhouse", "Tap four or more times on the beat")
                }
                DeckButton {
                    text: qsTrc("mixhouse", "RB")
                    onClicked: editor.resetGridToRekordbox()
                    toolTipTitle: qsTrc("mixhouse", "Imported grid")
                    toolTipDescription: qsTrc("mixhouse", "Puts the imported beat grid back")
                }
            }

            Group {
                title: qsTrc("mixhouse", "LOCK")
                DeckButton {
                    text: editor.gridLocked ? qsTrc("mixhouse", "LOCKED") : qsTrc("mixhouse", "LOCK")
                    checked: editor.gridLocked
                    accentColor: "#ff8c00"
                    onClicked: editor.toggleGridLock()
                    toolTipTitle: qsTrc("mixhouse", "Lock the grid")
                    toolTipDescription: qsTrc("mixhouse", "A locked grid can't be edited or re-analysed")
                }
            }

            Group {
                title: qsTrc("mixhouse", "PHRASE EDIT")
                DeckButton {
                    text: qsTrc("mixhouse", "CUT")
                    onClicked: editor.cutPhrase()
                    toolTipTitle: qsTrc("mixhouse", "Cut the phrase at the playhead")
                    toolTipDescription: qsTrc("mixhouse", "With Quantize, on the nearest bar")
                }
                StyledDropdown {
                    width: 110
                    height: 28
                    enabled: editor.selectedPhrase >= 0
                    model: editor.phraseLabels.map(function(label) { return { text: label, value: label } })
                    currentIndex: editor.selectedPhrase >= 0
                                  ? editor.phraseLabels.indexOf(editor.phrases[editor.selectedPhrase].label) : -1
                    indeterminateText: qsTrc("mixhouse", "Pick a phrase")
                    onActivated: function(index, value) {
                        editor.setPhraseLabel(value)
                    }
                }
                TextInputField {
                    width: 120
                    height: 28
                    enabled: editor.selectedPhrase >= 0
                    hint: qsTrc("mixhouse", "Name")
                    currentText: editor.selectedPhrase >= 0 ? editor.phrases[editor.selectedPhrase].label : ""
                    onTextEditingFinished: function(newTextValue) {
                        editor.setPhraseName(newTextValue)
                    }
                }
                DeckButton {
                    id: colorButton
                    text: ""
                    minWidth: 34
                    enabled: editor.selectedPhrase >= 0
                    onClicked: colorPicker.active ? colorPicker.item.open() : (colorPicker.active = true)
                    toolTipTitle: qsTrc("mixhouse", "Phrase colour")
                    Rectangle {
                        anchors.centerIn: parent
                        width: 16
                        height: 16
                        radius: 3
                        color: editor.selectedPhrase >= 0 ? editor.phrases[editor.selectedPhrase].color : "#555"
                    }
                    Loader {
                        id: colorPicker
                        active: false
                        source: "qrc:/qml/Audacity/ProjectScene/trackspanel/TrackColorPalettePopup.qml"
                        onLoaded: {
                            item.parent = ui.rootItem
                            item.colors = editor.colorPalette()
                            item.currentColorIndex = 0
                            item.colorSelected.connect(function(colorIndex) { editor.setPhraseColorIndex(colorIndex) })
                            item.open()
                        }
                    }
                }
                DeckButton {
                    text: qsTrc("mixhouse", "FILL")
                    enabled: editor.selectedPhrase >= 0
                    checked: editor.selectedPhrase >= 0 && editor.phrases[editor.selectedPhrase].fill >= 0
                    onClicked: editor.toggleFill()
                    toolTipTitle: qsTrc("mixhouse", "Fill-in")
                    toolTipDescription: qsTrc("mixhouse", "Marks the phrase's last bar as a fill-in (the dotted line)")
                }
                DeckButton {
                    text: qsTrc("mixhouse", "DELETE")
                    enabled: editor.selectedPhrase >= 0
                    onClicked: editor.deletePhrase()
                    toolTipTitle: qsTrc("mixhouse", "Delete the phrase")
                    toolTipDescription: qsTrc("mixhouse", "Merges the selected phrase into the one before it")
                }
                DeckButton {
                    text: qsTrc("mixhouse", "RB")
                    enabled: editor.phrasesEdited
                    onClicked: editor.resetPhrases()
                    toolTipTitle: qsTrc("mixhouse", "Imported phrases")
                    toolTipDescription: qsTrc("mixhouse", "Throws away the edits and shows the imported phrases again")
                }
            }

            Group {
                title: qsTrc("mixhouse", "EDIT · ZOOM")
                DeckButton {
                    text: qsTrc("mixhouse", "UNDO")
                    enabled: editor.canUndo
                    onClicked: editor.undo()
                }
                DeckButton {
                    text: qsTrc("mixhouse", "REDO")
                    enabled: editor.canRedo
                    onClicked: editor.redo()
                }
                DeckButton {
                    text: "−"
                    onClicked: editor.zoomBy(1 / 1.5)
                    toolTipTitle: qsTrc("mixhouse", "Zoom out")
                }
                DeckButton {
                    text: "+"
                    onClicked: editor.zoomBy(1.5)
                    toolTipTitle: qsTrc("mixhouse", "Zoom in")
                }
            }
        }

        StyledTextLabel {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignLeft
            text: editor.statusText
            color: "#9aa3b5"
            font.pixelSize: 11
        }
    }
}
