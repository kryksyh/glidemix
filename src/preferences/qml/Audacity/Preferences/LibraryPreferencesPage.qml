/*
 * Audacity: A Digital Audio Editor
 */

/*
 * Music library preferences.
 *
 * Generated 1:1 from MixHouse DJ's own preference dialog (dlgpreflibrarydlg.ui) by walking that
 * .ui file in document order, so the control list here matches the real page
 * rather than a hand-picked subset. Combo options come from the harvested
 * addItem() calls in the matching .cpp where they are built in code.
 *
 * Every section here is wired to MixHouseLibraryPreferencesModel, which
 * persists through IMixHouseLibraryConfiguration. The sections that were not --
 * Serato tag sync, relative-path playlist export, Session History, External
 * Libraries, the Cover Art Fetcher -- mapped to Mixxx features this app does
 * not have, and were removed in the Preferences audit rather than left as
 * controls that do nothing. The chrome is Audacity's own -- do not restyle it.
 */
import QtQuick
import QtQuick.Controls 2.15

import Muse.Ui 1.0
import Muse.UiComponents

import Audacity.Preferences
import Audacity.UiComponents 1.0

PreferencesPage {
    id: root

    MixHouseLibraryPreferencesModel {
        id: mixHouseLibraryModel
    }

    Column {
        width: parent.width
        spacing: root.sectionsSpacing

        BaseSection {
            id: section0

            title: qsTrc("preferences", "Music Directories")

            navigation.section: root.navigationSection
            navigation.order: root.navigationOrderStart

            onFocusChanged: {
                if (activeFocus) {
                    root.ensureContentVisibleRequested(Qt.rect(x, y, width, height))
                }
            }

            // Real mixxx: watched directories, each rescanned for new files
            // on demand or at startup. GlideMix also watches them while it
            // runs (MixHouseFolderWatcher): new songs are added by themselves.
            // FilePickerModel is the exact same real native-folder-picker
            // component MixHouseLibraryPanel.qml's own "Add Folder to
            // Library" already uses, not a new mechanism.
            FilePickerModel {
                id: musicDirPicker
                title: qsTrc("preferences", "Choose a music folder to add")
                dir: mixHouseLibraryModel.musicFolder()
            }

            StyledListView {
                id: musicDirList
                width: parent.width
                height: Math.max(60, Math.min(200, count * 24))
                clip: true
                ScrollBar.vertical: StyledScrollBar {}

                model: mixHouseLibraryModel.musicDirectories

                delegate: ListItemBlank {
                    width: musicDirList.width
                    height: 24
                    isSelected: ListView.isCurrentItem

                    onClicked: musicDirList.currentIndex = index

                    StyledTextLabel {
                        anchors.fill: parent
                        anchors.leftMargin: 6
                        anchors.rightMargin: 6
                        horizontalAlignment: Qt.AlignLeft
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideMiddle
                        text: modelData
                    }
                }
            }

            StyledTextLabel {
                width: parent.width
                visible: mixHouseLibraryModel.musicDirectories.length === 0
                text: qsTrc("preferences", "No music directories added yet.")
                opacity: 0.6
                horizontalAlignment: Qt.AlignLeft
            }

            FlatButton {
                text: qsTrc("preferences", "Add")

                navigation.name: "LibraryButton0"
                navigation.panel: section0.navigation
                navigation.row: 0

                onClicked: {
                    var chosen = musicDirPicker.selectDirectory()
                    if (chosen) {
                        mixHouseLibraryModel.addMusicDirectory(chosen)
                        musicDirList.currentIndex = mixHouseLibraryModel.musicDirectories.length - 1
                    }
                }
            }

            FlatButton {
                // Real mixxx's Relink recovers tracks whose files MOVED
                // (matching by filename/size at a new path) -- a real,
                // separate feature from "remember this folder" above, not
                // built here yet. Shown correctly disabled rather than
                // silently doing nothing, same convention as the Analyze
                // submenu's own Reanalyze (constant/variable BPM) items.
                text: qsTrc("preferences", "Relink")
                enabled: false

                navigation.name: "LibraryButton1"
                navigation.panel: section0.navigation
                navigation.row: 1
            }

            FlatButton {
                text: qsTrc("preferences", "Remove")
                enabled: musicDirList.currentIndex >= 0
                          && musicDirList.currentIndex < mixHouseLibraryModel.musicDirectories.length

                navigation.name: "LibraryButton2"
                navigation.panel: section0.navigation
                navigation.row: 2

                onClicked: {
                    var dirs = mixHouseLibraryModel.musicDirectories
                    mixHouseLibraryModel.removeMusicDirectory(dirs[musicDirList.currentIndex])
                    musicDirList.currentIndex = -1
                }
            }

            CheckBox {
                width: parent.width

                text: qsTrc("preferences", "Rescan directories on start-up")
                checked: mixHouseLibraryModel.rescanOnStartup

                navigation.name: "LibraryCheck3"
                navigation.panel: section0.navigation
                navigation.row: 3

                onClicked: {
                    mixHouseLibraryModel.rescanOnStartup = !checked
                }
            }

            CheckBox {
                width: parent.width

                text: qsTrc("preferences", "Watch these folders: add new songs automatically")
                checked: mixHouseLibraryModel.watchMusicDirectories

                navigation.name: "LibraryCheckWatch"
                navigation.panel: section0.navigation
                navigation.row: 3

                onClicked: {
                    mixHouseLibraryModel.watchMusicDirectories = !checked
                }
            }

            CheckBox {
                width: parent.width

                text: qsTrc("preferences", "Analyze new songs automatically (BPM, key, beat grid, phrases)")
                checked: mixHouseLibraryModel.analyzeNewTracks

                navigation.name: "LibraryCheckAnalyzeNew"
                navigation.panel: section0.navigation
                navigation.row: 3

                onClicked: {
                    mixHouseLibraryModel.analyzeNewTracks = !checked
                }
            }

            // GlideMix: like rekordbox's Concurrent Process Mode.
            ComboBoxWithTitle {
                title: qsTrc("preferences", "Analysis uses:")
                columnWidth: 208
                currentIndex: mixHouseLibraryModel.analysisMode
                model: [
                    { text: qsTrc("preferences", "Performance (all cores, fastest)"), value: 0 },
                    { text: qsTrc("preferences", "Normal"), value: 1 },
                    { text: qsTrc("preferences", "Power saving (half the cores)"), value: 2 }
                ]
                textRole: "text"
                valueRole: "value"
                onValueEdited: function(newIndex, newValue) {
                    mixHouseLibraryModel.analysisMode = newValue
                }
                navigation.name: "LibraryAnalysisMode"
                navigation.panel: section0.navigation
                navigation.row: 5
            }

            CheckBox {
                width: parent.width
                // On the Mac the full model always runs on the GPU (Core ML).
                visible: Qt.platform.os !== "osx"

                // GlideMix: Fast (small model) by default; tracks rekordbox
                // or Lexicon analysed already use their grid either way.
                text: qsTrc("preferences", "Accurate beat analysis (about 1.7× slower)")
                checked: mixHouseLibraryModel.accurateBeatAnalysis

                navigation.name: "LibraryCheckAccurate"
                navigation.panel: section0.navigation
                navigation.row: 3

                onClicked: {
                    mixHouseLibraryModel.accurateBeatAnalysis = !checked
                }
            }

            CheckBox {
                width: parent.width

                text: qsTrc("preferences", "Show scan summary dialog")
                checked: mixHouseLibraryModel.showScanSummaryDialog

                navigation.name: "LibraryCheck4"
                navigation.panel: section0.navigation
                navigation.row: 4

                onClicked: {
                    mixHouseLibraryModel.showScanSummaryDialog = !checked
                }
            }
        }

        SeparatorLine {}

        BaseSection {
            id: section1

            title: qsTrc("preferences", "Track Metadata Synchronization")

            navigation.section: root.navigationSection
            navigation.order: section0.navigationOrderEnd + 1

            onFocusChanged: {
                if (activeFocus) {
                    root.ensureContentVisibleRequested(Qt.rect(x, y, width, height))
                }
            }

            CheckBox {
                width: parent.width

                // Real mixxx: kSyncTrackMetadataExport -- when on, this
                // port's own real Comment/Grouping/Composer/Album Artist/
                // Track # edits (already real -- see the track table's own
                // double-click-to-edit) also write straight to the actual
                // audio file's tags via TagLib, not just the database.
                text: qsTrc("preferences", "Synchronize library track metadata from/to file tags")
                checked: mixHouseLibraryModel.syncMetadataToFileTags

                navigation.name: "LibraryCheck0"
                navigation.panel: section1.navigation
                navigation.row: 0

                onClicked: {
                    mixHouseLibraryModel.syncMetadataToFileTags = !checked
                }
            }
        }

        SeparatorLine {}

        // GlideMix: analysis from the DJ apps a user already has, in addition
        // to GlideMix's own (which fills in wherever they have none).
        BaseSection {
            id: sectionOtherApps

            title: qsTrc("preferences", "Other DJ Apps")

            navigation.section: root.navigationSection
            navigation.order: section1.navigationOrderEnd + 1

            // Bumped when an app is switched on or off, so the boxes re-read.
            property int shownRevision: 0

            Connections {
                target: mixHouseLibraryModel
                function onDjAppsShownChanged() {
                    sectionOtherApps.shownRevision++
                }
            }

            CheckBox {
                width: parent.width
                text: qsTrc("preferences", "Use rekordbox's analysis when a track has it (phrases, beatgrids, waveforms)")
                checked: mixHouseLibraryModel.useRekordboxAnalysis

                navigation.name: "RekordboxAnalysisCheck"
                navigation.panel: sectionOtherApps.navigation
                navigation.row: 0

                onClicked: {
                    mixHouseLibraryModel.useRekordboxAnalysis = !checked
                }
            }

            CheckBox {
                width: parent.width
                text: qsTrc("preferences", "Show the Lexicon DJ library (tracks, playlists, artwork, cues)")
                checked: mixHouseLibraryModel.useLexiconLibrary

                navigation.name: "LexiconLibraryCheck"
                navigation.panel: sectionOtherApps.navigation
                navigation.row: 1

                onClicked: {
                    mixHouseLibraryModel.useLexiconLibrary = !checked
                }
            }

            // GlideMix: hide the apps this user doesn't have or use; all shown
            // after installing. A hidden app is not read at all.
            Repeater {
                model: [
                    { source: "rekordbox", name: "Rekordbox XML" },
                    { source: "engine", name: "Engine DJ" },
                    { source: "traktor", name: "Traktor" },
                    { source: "serato", name: "Serato DJ" },
                    { source: "vdj", name: "VirtualDJ" },
                    { source: "djay", name: "djay" }
                ]

                CheckBox {
                    required property var modelData
                    required property int index

                    width: sectionOtherApps.width
                    //: %1 is a DJ app, e.g. "Engine DJ"
                    text: qsTrc("preferences", "Show %1 in the library").arg(modelData.name)
                    checked: {
                        sectionOtherApps.shownRevision
                        return mixHouseLibraryModel.djAppShown(modelData.source)
                    }

                    navigation.name: "DjAppShown_" + modelData.source
                    navigation.panel: sectionOtherApps.navigation
                    navigation.row: 2 + index

                    onClicked: {
                        mixHouseLibraryModel.setDjAppShown(modelData.source, !checked)
                    }
                }
            }

            // GlideMix: libraries kept somewhere else. Every drive's root is
            // looked at by itself (Engine Library, _Serato_, VirtualDJ);
            // these are for a library folder moved anywhere else.
            StyledTextLabel {
                width: parent.width
                horizontalAlignment: Text.AlignLeft
                wrapMode: Text.WordWrap
                text: qsTrc("preferences", "DJ app library folders (every drive is searched by itself; add a folder if an app keeps its library somewhere else):")
            }

            FilePickerModel {
                id: djAppDirPicker
                title: qsTrc("preferences", "Choose a folder with a DJ app's library")
                dir: mixHouseLibraryModel.musicFolder()
            }

            StyledListView {
                id: djAppDirList
                width: parent.width
                height: Math.max(24, Math.min(120, count * 24))
                clip: true
                visible: count > 0
                ScrollBar.vertical: StyledScrollBar {}

                model: mixHouseLibraryModel.djAppFolders

                delegate: ListItemBlank {
                    width: djAppDirList.width
                    height: 24
                    isSelected: ListView.isCurrentItem

                    onClicked: djAppDirList.currentIndex = index

                    StyledTextLabel {
                        anchors.fill: parent
                        anchors.leftMargin: 6
                        anchors.rightMargin: 6
                        horizontalAlignment: Qt.AlignLeft
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideMiddle
                        text: modelData
                    }
                }
            }

            Row {
                spacing: 8

                FlatButton {
                    text: qsTrc("preferences", "Add folder…")

                    navigation.name: "DjAppFolderAdd"
                    navigation.panel: sectionOtherApps.navigation
                    navigation.row: 10

                    onClicked: {
                        var chosen = djAppDirPicker.selectDirectory()
                        if (chosen) {
                            mixHouseLibraryModel.addDjAppFolder(chosen)
                        }
                    }
                }

                FlatButton {
                    text: qsTrc("preferences", "Remove")
                    enabled: djAppDirList.currentIndex >= 0
                              && djAppDirList.currentIndex < mixHouseLibraryModel.djAppFolders.length

                    navigation.name: "DjAppFolderRemove"
                    navigation.panel: sectionOtherApps.navigation
                    navigation.row: 11

                    onClicked: {
                        var dirs = mixHouseLibraryModel.djAppFolders
                        mixHouseLibraryModel.removeDjAppFolder(dirs[djAppDirList.currentIndex])
                        djAppDirList.currentIndex = -1
                    }
                }
            }

            StyledTextLabel {
                width: parent.width
                horizontalAlignment: Text.AlignLeft
                wrapMode: Text.WordWrap
                opacity: 0.7
                text: qsTrc("preferences", "GlideMix's own analysis is used for every track they have none for. Phrases and beatgrids you edit always win.")
            }
        }

        SeparatorLine {}

        BaseSection {
            id: section2

            title: qsTrc("preferences", "Track Table View")

            navigation.section: root.navigationSection
            navigation.order: sectionOtherApps.navigationOrderEnd + 1

            onFocusChanged: {
                if (activeFocus) {
                    root.ensureContentVisibleRequested(Qt.rect(x, y, width, height))
                }
            }

            CheckBox {
                width: parent.width

                // Real mixxx: a SECOND click on an already-selected row
                // starts editing that cell (no double-click needed) when
                // this is on -- see the track table's own onClicked/
                // beginEdit wiring.
                text: qsTrc("preferences", "Edit metadata after clicking selected track")
                checked: mixHouseLibraryModel.editMetadataAfterClick

                navigation.name: "LibraryCheck0"
                navigation.panel: section2.navigation
                navigation.row: 0

                onClicked: {
                    mixHouseLibraryModel.editMetadataAfterClick = !checked
                }
            }

            ComboBoxWithTitle {
                title: qsTrc("preferences", "Track Double-Click Action:")

                columnWidth: 208
                currentIndex: mixHouseLibraryModel.trackDoubleClickAction
                model: [
                    { text: qsTrc("preferences", "Load the track into the project"), value: 0 },
                    { text: qsTrc("preferences", "Add track to Auto DJ queue (bottom)"), value: 1 },
                    { text: qsTrc("preferences", "Add track to Auto DJ queue (top)"), value: 2 },
                    { text: qsTrc("preferences", "Ignore"), value: 3 }
                ]
                textRole: "text"
                valueRole: "value"

                onValueEdited: function(newIndex, newValue) {
                    mixHouseLibraryModel.trackDoubleClickAction = newValue
                }

                navigation.name: "LibraryCombo1"
                navigation.panel: section2.navigation
                navigation.row: 1
            }

            IncrementalPropertyControlWithTitle {
                title: qsTrc("preferences", "Library Row Height:")

                currentValue: mixHouseLibraryModel.libraryRowHeight
                minValue: 5
                maxValue: 100
                step: 1
                decimals: 0
                measureUnitsSymbol: qsTrc("global", "px")

                onValueEdited: function(newValue) {
                    mixHouseLibraryModel.libraryRowHeight = newValue
                }

                navigation.name: "LibraryNum2"
                navigation.panel: section2.navigation
                navigation.row: 2
            }

            // Mixxx's Library Font: family and size ("Default" = the app theme's font).
            ComboBoxWithTitle {
                id: fontFamilyCombo

                title: qsTrc("preferences", "Library Font:")
                columnWidth: 208

                readonly property var families: [ { text: qsTrc("preferences", "Default"), value: "" } ].concat(
                    Qt.fontFamilies().map(function(f) { return { text: f, value: f } }))

                model: families
                textRole: "text"
                valueRole: "value"
                currentIndex: Math.max(0, indexOfValue(mixHouseLibraryModel.libraryFontFamily))

                onValueEdited: function(newIndex, newValue) {
                    mixHouseLibraryModel.libraryFontFamily = newValue
                }

                navigation.name: "LibraryCombo3"
                navigation.panel: section2.navigation
                navigation.row: 3
            }

            IncrementalPropertyControlWithTitle {
                title: qsTrc("preferences", "Library Font Size:")

                currentValue: mixHouseLibraryModel.libraryFontPointSize
                minValue: 0
                maxValue: 72
                step: 1
                decimals: 0
                measureUnitsSymbol: qsTrc("preferences", "pt (0 = default)")
                // Wider than the 96 other spin boxes use: the unit text is a
                // sentence, and at 96 the spinner arrows sit on top of it.
                controlWidth: 160

                onValueEdited: function(newValue) {
                    mixHouseLibraryModel.libraryFontPointSize = newValue
                }

                navigation.name: "LibraryNum5"
                navigation.panel: section2.navigation
                navigation.row: 5
            }

            IncrementalPropertyControlWithTitle {
                title: qsTrc("preferences", "BPM display precision:")

                currentValue: mixHouseLibraryModel.bpmPrecision
                minValue: 0
                maxValue: 4
                step: 1
                decimals: 0

                onValueEdited: function(newValue) {
                    mixHouseLibraryModel.bpmPrecision = newValue
                }

                navigation.name: "LibraryNum6"
                navigation.panel: section2.navigation
                navigation.row: 6
            }

            // Every place a key is shown: the Key column, the clip menu's
            // Match key and Transpose, the beat grid editor. Search takes
            // keys in any of these.
            ComboBoxWithTitle {
                title: qsTrc("preferences", "Key notation:")

                columnWidth: 208
                currentIndex: mixHouseLibraryModel.keyNotation
                model: [
                    { text: qsTrc("preferences", "Lancelot (8A)"), value: 0 },
                    { text: qsTrc("preferences", "Lancelot/Traditional (8A/Am)"), value: 1 },
                    { text: qsTrc("preferences", "OpenKey (1m)"), value: 2 },
                    { text: qsTrc("preferences", "OpenKey/Traditional (1m/Am)"), value: 3 },
                    { text: qsTrc("preferences", "Traditional (Am)"), value: 4 }
                ]
                textRole: "text"
                valueRole: "value"

                onValueEdited: function(newIndex, newValue) {
                    mixHouseLibraryModel.keyNotation = newValue
                }

                navigation.name: "LibraryKeyNotation"
                navigation.panel: section2.navigation
                navigation.row: 7
            }

            // GlideMix (Chris): the Key column as plain text, coloured
            // text or coloured badges.
            ComboBoxWithTitle {
                title: qsTrc("preferences", "Key column:")

                columnWidth: 208
                currentIndex: mixHouseLibraryModel.keyStyle
                model: [
                    { text: qsTrc("preferences", "Plain text"), value: 0 },
                    { text: qsTrc("preferences", "Coloured text"), value: 1 },
                    { text: qsTrc("preferences", "Coloured badges"), value: 2 }
                ]
                textRole: "text"
                valueRole: "value"

                onValueEdited: function(newIndex, newValue) {
                    mixHouseLibraryModel.keyStyle = newValue
                }

                navigation.name: "LibraryKeyBadges"
                navigation.panel: section2.navigation
                navigation.row: 8
            }

            CheckBox {
                width: parent.width

                text: qsTrc("preferences", "Grey out played tracks")
                checked: mixHouseLibraryModel.greyOutPlayedTracks

                navigation.name: "LibraryCheck4"
                navigation.panel: section2.navigation
                navigation.row: 4

                onClicked: {
                    mixHouseLibraryModel.greyOutPlayedTracks = !checked
                }
            }
        }

        SeparatorLine {}

        // Real mixxx keeps this on its OWN separate Preferences page
        // (DlgPrefWaveform, "Waveforms") -- this port folds its one real,
        // portable group ("Overview Waveforms") into the existing MixHouse
        // "Music library" page instead of adding a whole new top-level page
        // for two controls. Everything else on real mixxx's Waveforms page
        // (frame rate, Low/Mid/High visual gain, the deck-scrolling
        // waveform type, "Synchronize zoom", caching, OpenGL status) is
        // confirmed -- not guessed -- to be exclusively a live-deck-
        // scrolling-waveform concept with zero code path to any whole-track
        // overview widget (real mixxx's own WOverview/OverviewCache never
        // read those settings), and this port has no live deck for any of
        // it to mean anything for yet -- see MixHouseLibraryPanel.qml's
        // overview Canvas's own comment.
        BaseSection {
            id: sectionWaveform

            title: qsTrc("preferences", "Waveform Overview")

            navigation.section: root.navigationSection
            navigation.order: section2.navigationOrderEnd + 1

            onFocusChanged: {
                if (activeFocus) {
                    root.ensureContentVisibleRequested(Qt.rect(x, y, width, height))
                }
            }

            ComboBoxWithTitle {
                title: qsTrc("preferences", "Waveform overview type:")

                columnWidth: 208
                currentIndex: mixHouseLibraryModel.waveformOverviewType
                model: [
                    { text: qsTrc("preferences", "Filtered"), value: 0 },
                    { text: qsTrc("preferences", "HSV"), value: 1 },
                    { text: qsTrc("preferences", "RGB"), value: 2 },
                    { text: qsTrc("preferences", "White"), value: 3 }
                ]
                textRole: "text"
                valueRole: "value"

                onValueEdited: function(newIndex, newValue) {
                    mixHouseLibraryModel.waveformOverviewType = newValue
                }

                navigation.name: "LibraryCombo0"
                navigation.panel: sectionWaveform.navigation
                navigation.row: 0
            }

            CheckBox {
                width: parent.width

                text: qsTrc("preferences", "Show minute markers on waveform overview")
                checked: mixHouseLibraryModel.showWaveformMinuteMarkers

                navigation.name: "LibraryCheck1"
                navigation.panel: sectionWaveform.navigation
                navigation.row: 1

                onClicked: {
                    mixHouseLibraryModel.showWaveformMinuteMarkers = !checked
                }
            }
        }

        SeparatorLine {}

        BaseSection {
            id: section3

            title: qsTrc("preferences", "Track Search")

            navigation.section: root.navigationSection
            navigation.order: sectionWaveform.navigationOrderEnd + 1

            onFocusChanged: {
                if (activeFocus) {
                    root.ensureContentVisibleRequested(Qt.rect(x, y, width, height))
                }
            }

            IncrementalPropertyControlWithTitle {
                title: qsTrc("preferences", "Search-as-you-type timeout:")

                // Real mixxx: WSearchLineEdit::kMinDebouncingTimeoutMillis/
                // kMaxDebouncingTimeoutMillis (100/9999) -- a value below
                // 100 gets silently clamped up there anyway
                // (verifyDebouncingTimeoutMillis), so this port's own UI
                // just enforces that range directly instead of accepting
                // then clamping.
                currentValue: mixHouseLibraryModel.searchDebounceMs
                minValue: 100
                maxValue: 9999
                step: 50
                decimals: 0
                measureUnitsSymbol: qsTrc("global", "ms")

                onValueEdited: function(newValue) {
                    mixHouseLibraryModel.searchDebounceMs = newValue
                }

                navigation.name: "LibraryNum0"
                navigation.panel: section3.navigation
                navigation.row: 0
            }

            CheckBox {
                width: parent.width

                // Real mixxx: WSearchLineEdit's own completer, matching
                // this port's real search-history list (typed searches
                // remembered as you search) against what you're currently
                // typing -- see the track table's own search box, which
                // shows a real dropdown of matches when this is on.
                text: qsTrc("preferences", "Enable search completions")
                checked: mixHouseLibraryModel.searchCompletionsEnabled

                navigation.name: "LibraryCheck1"
                navigation.panel: section3.navigation
                navigation.row: 1

                onClicked: {
                    mixHouseLibraryModel.searchCompletionsEnabled = !checked
                }
            }

            CheckBox {
                width: parent.width

                // Real mixxx: Up/Down arrow keys in the search box cycle
                // through the SAME real search-history list, most recent
                // first -- see the track table's own search box.
                text: qsTrc("preferences", "Enable search history keyboard shortcuts")
                checked: mixHouseLibraryModel.searchHistoryShortcutsEnabled

                onClicked: {
                    mixHouseLibraryModel.searchHistoryShortcutsEnabled = !checked
                }

                navigation.name: "LibraryCheck2"
                navigation.panel: section3.navigation
                navigation.row: 2
            }

            ComboBoxWithTitle {
                id: fuzzyBpmRangeCombo

                title: qsTrc("preferences", "Percentage of pitch slider range for 'fuzzy' BPM search:")

                columnWidth: 208
                // Real mixxx's own four choices (dlgpreflibrary.cpp:96-99) --
                // see MixHouseLibraryPreferencesModel's own header comment
                // for the percent -> stored-relative-range conversion.
                currentIndex: fuzzyBpmRangeCombo.indexOfValue(mixHouseLibraryModel.fuzzyBpmRangePercent)
                model: [
                    { text: qsTrc("preferences", "25 %"), value: 25 },
                    { text: qsTrc("preferences", "50 %"), value: 50 },
                    { text: qsTrc("preferences", "75 %"), value: 75 },
                    { text: qsTrc("preferences", "100 %"), value: 100 }
                ]
                textRole: "text"
                valueRole: "value"

                onValueEdited: function(newIndex, newValue) {
                    mixHouseLibraryModel.fuzzyBpmRangePercent = newValue
                }

                navigation.name: "LibraryCombo3"
                navigation.panel: section3.navigation
                navigation.row: 3
            }

            StyledTextLabel {
                width: parent.width

                text: qsTrc("preferences", "This range will be used for the 'fuzzy' BPM search (~bpm:) via the search box, as well as for BPM search in Track context menu > Search related Tracks")

                horizontalAlignment: Qt.AlignLeft
                wrapMode: Text.WordWrap
                maximumLineCount: 4
            }
        }

        SeparatorLine {}

        BaseSection {
            id: section7

            title: qsTrc("preferences", "Settings Directory")

            navigation.section: root.navigationSection
            navigation.order: section3.navigationOrderEnd + 1

            onFocusChanged: {
                if (activeFocus) {
                    root.ensureContentVisibleRequested(Qt.rect(x, y, width, height))
                }
            }

            StyledTextLabel {
                width: parent.width

                text: qsTrc("preferences", "The GlideMix settings directory contains the library database, various configuration files, log files, and track analysis data.")

                horizontalAlignment: Qt.AlignLeft
                wrapMode: Text.WordWrap
                maximumLineCount: 4
            }

            StyledTextLabel {
                width: parent.width

                text: qsTrc("preferences", "Edit those files only if you know what you are doing and only while GlideMix is not running.")

                horizontalAlignment: Qt.AlignLeft
                wrapMode: Text.WordWrap
                maximumLineCount: 4
            }

            FlatButton {
                text: qsTrc("preferences", "Open Settings Folder")

                navigation.name: "LibraryButton0"
                navigation.panel: section7.navigation
                navigation.row: 0

                onClicked: Qt.openUrlExternally("file://" + mixHouseLibraryModel.settingsDirectoryPath)
            }
        }

        SeparatorLine {}

        //! Was the "Music" page. Where a track's tempo comes from is one
        //! question, so its two halves sit on one page: the library's own
        //! analysis above, and plain audio files here.
        ImportTempoSection {
            width: parent.width

            navigation.section: root.navigationSection
            navigation.order: section7.navigationOrderEnd + 1

            onFocusChanged: {
                if (activeFocus) {
                    root.ensureContentVisibleRequested(Qt.rect(x, y, width, height))
                }
            }
        }
    }
}
