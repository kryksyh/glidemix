/*
 * MixHouse DJ 2 -- the music library, docked at the bottom.
 *
 * Source tree on the left (Tracks / Playlists / Crates), track table on the right
 * with sortable, show/hide-able columns -- the capabilities the Mixxx fork's
 * library had, not just a list.
 *
 * Built from Audacity's own components so it sits inside their design.
 *
 * NOT here yet, and deliberately not faked:
 *   - Adding and analysing tracks. Analysis is Mixxx's 3,985-line analyzer stack
 *     plus libkeyfinder and the Beat This! model; it is its own port. Until a
 *     track has a real analysis file, its Waveform Overview column is
 *     correctly blank -- never a fabricated flat line.
 *
 * The Overview waveform column IS here: that data isn't in mixxxdb.sqlite
 * (wavesummaryhex is null on every row) -- it's decoded on demand from
 * Mixxx's own real per-track analysis cache (see mixhousewaveform.h and
 * IMixHouseLibrary::overviewAnalysisBlob()).
 */
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls 2.15
import Qt.labs.settings 1.1

import Muse.Ui 1.0
import Muse.UiComponents 1.0
import Muse.GraphicalEffects

import Audacity.MixHouse

import "MixHouseOverviewPaint.js" as OverviewPaint
import Audacity.UiComponents

Item {
    id: root

    // GlideMix: files and folders dragged in from Finder, dropped anywhere on
    // the library that is not a sidebar list -- added to the library, and to
    // the playlist or crate being shown. Underneath everything, so the
    // sidebar's own playlist/crate drop areas take drops on them first; the
    // library's own drags (text "library") are not taken here.
    DropArea {
        id: finderDropArea
        anchors.fill: parent
        z: -1
        onEntered: function(drag) {
            if (!drag.hasUrls || (drag.hasText && drag.text === "library")) {
                drag.accepted = false
            }
        }
        onDropped: function(drop) {
            tracksModel.addDroppedUrls(drop.urls, -2, -2)
            drop.accept()
        }
    }

    Rectangle {
        anchors.fill: parent
        z: 1000
        visible: finderDropArea.containsDrag
        color: "transparent"
        border.color: ui.theme.accentColor
        border.width: 2
    }

    // Persist the user's header order, visibility, and widths just like
    // Mixxx's ColumnCache. The defaults above are used only on first launch;
    // once a user customises the header, that layout survives app restarts.
    Settings {
        id: columnSettings
        category: "MixHouseLibrary"
        property string layoutJson: ""
    }

    property alias navigationSection: navPanel.section

    NavigationPanel {
        id: navPanel
        name: "MixHouseLibrary"
        direction: NavigationPanel.Both
        accessible.name: qsTrc("mixhouse", "Music library")
    }

    // REVERTED, 2026-09-06: a border overlay + HoverHandler here (meant to
    // highlight the panel boundary) broke the dock's own resize-drag on the
    // splitter between this panel and the tracks/timeline above it. Even a
    // passive HoverHandler spanning the full panel apparently contends with
    // that splitter's own hit-testing at the shared edge. Confirmed in testing: the
    // drag loss traced to this. Not attempting a repaired version of the same
    // idea in this pass -- see the plain, input-free grip glyph below instead,
    // which cannot have this problem because it has no input handling at all.
    MixHouseTrackListModel {
        id: tracksModel
    }
    // For children with a property of the same name (SmartPlaylistEditor).
    readonly property var tracksModelRef: tracksModel

    // The FULL real Mixxx column list -- every entry its own right-click column
    // picker offers (library/columncache.cpp's kColumnPropertiesByEnum), in the
    // same order a real Mixxx shows them, with the same titles and default
    // widths. Not a curated subset: "Grid" (an earlier invention of mine, not a
    // real Mixxx column) is gone; everything below has a real counterpart.
    // `visible` defaults mirror the real Mixxx column setup exactly
    // (Artist/Title/Year/Rating/Genre/Key/BPM/Duration/Color checked, rest off).
    // Cover Art is real (Phase 8 -- TagLib embedded-picture decode, see
    // mixhousecoverart.h) and, matching real mixxx's own default
    // (basetracktablemodel.cpp's isColumnHiddenByDefault), ships visible.
    // Preview is still listed (matching the real menu) but renders empty for
    // now -- it needs the engine bridge, a separate later phase; not faked
    // with a fixed icon.
    // `width` is live state a drag-handle can change; array ORDER is display
    // order, so reordering a column means splicing this array.  Widths below
    // are Mixxx's real ColumnCache defaults (kDefaultColumnWidth = 50): the
    // three icon columns are 25px, Overview is 400px, and text columns use the
    // same 50/100/150/200/300px values as the native QHeaderView.
    // Default visibility matches real mixxx's own BaseTrackTableModel::
    // isColumnHiddenByDefault() exactly (basetracktablemodel.cpp:314-330):
    // hidden = AlbumArtist/Bitrate/Channels/Composer/FileType/Grouping/
    // Location/Played/ReplayGain/SampleRate/TimesPlayed/TrackNumber/Year --
    // everything else (Album/Artist/Title/Genre/Rating/Key/BPM/Duration/
    // Comment/DateAdded/LastPlayedAt/Preview) is visible. This port had
    // FIVE of those backwards (Album/Comment/DateAdded/LastPlayedAt/Preview
    // hidden, Year shown) -- fixed to match the real default exactly.
    // Library click-to-preview (see the overview cell's own click handling):
    // MixHouse DJ's kStopBoxWidth and preview red, copied from the fork's
    // OverviewDelegate.
    // The stop button gets its own gutter LEFT of the waveform (rekordbox
    // layout) instead of sitting on top of the track's intro --     // 2026-09-13. Reserved on every row, so waveforms don't shift sideways
    // when a preview starts. The gutter IS the stop zone (the fork's 16px
    // kStopBoxWidth, +2px breathing room before the waveform).

    property var columns: [
        // Match the requested Mixxx-style default order exactly. Additional
        // optional fields stay available through the header menu and are
        // appended after the default visible set.
        { key: "trackNumber",  label: qsTrc("mixhouse", "Track #"),     sort: MixHouseTrackListModel.SortTrackNumber,      width: 70,  align: Qt.AlignRight,   visible: true },
        // Rekordbox-style per-track analysis progress ("96%" over a thin
        // bar), in the slot real mixxx's never-wired Preview column used
        // to hold. headerLabel "" -- rekordbox's own column has no title;
        // `label` is still what the right-click column picker shows.
        { key: "analysisProgress", label: qsTrc("mixhouse", "Analysis Progress"), headerLabel: qsTrc("mixhouse", "Analysis"), sort: MixHouseTrackListModel.SortAnalyzed,                width: 70,  align: Qt.AlignHCenter, visible: true },
        { key: "coverArt",     label: qsTrc("mixhouse", "Cover Art"),   sort: -1,                                          width: 90,  align: Qt.AlignHCenter, visible: true },
        { key: "overview",     label: qsTrc("mixhouse", "Waveform Overview"), sort: -1,                                    width: 330, align: Qt.AlignLeft,    visible: true },
        { key: "artist",       label: qsTrc("mixhouse", "Artist"),      sort: MixHouseTrackListModel.SortArtist,           width: 180, align: Qt.AlignLeft,    visible: true },
        { key: "title",        label: qsTrc("mixhouse", "Title"),       sort: MixHouseTrackListModel.SortTitle,            width: 220, align: Qt.AlignLeft,    visible: true },
        { key: "year",         label: qsTrc("mixhouse", "Year"),        sort: MixHouseTrackListModel.SortYear,             width: 70,  align: Qt.AlignRight,   visible: true },
        { key: "genre",        label: qsTrc("mixhouse", "Genre"),       sort: MixHouseTrackListModel.SortGenre,            width: 150, align: Qt.AlignLeft,    visible: true },
        { key: "musicalKey",   label: qsTrc("mixhouse", "Key"),         sort: MixHouseTrackListModel.SortKey,              width: 90,  align: Qt.AlignRight,   visible: true },
        { key: "bpm",          label: qsTrc("mixhouse", "BPM"),         sort: MixHouseTrackListModel.SortBpm,              width: 80,  align: Qt.AlignRight,   visible: true },
        // GlideMix: 1-10, from the analysis (Lexicon's own for its songs).
        { key: "energy",       label: qsTrc("mixhouse", "Energy"),      sort: MixHouseTrackListModel.SortEnergy,           width: 100,  align: Qt.AlignLeft,    visible: true },
        { key: "durationSec",  label: qsTrc("mixhouse", "Duration"),    sort: MixHouseTrackListModel.SortDuration,         width: 90,  align: Qt.AlignRight,   visible: true },
        { key: "rating",       label: qsTrc("mixhouse", "Rating"),      sort: MixHouseTrackListModel.SortRating,           width: 120, align: Qt.AlignRight,   visible: true },
        { key: "comment",      label: qsTrc("mixhouse", "Comment"),     sort: MixHouseTrackListModel.SortComment,          width: 220, align: Qt.AlignLeft,    visible: true },
        { key: "dateAdded",    label: qsTrc("mixhouse", "Date Added"),  sort: MixHouseTrackListModel.SortDateAdded,        width: 150, align: Qt.AlignLeft,    visible: true },
        { key: "lastPlayedAt", label: qsTrc("mixhouse", "Last Played"), sort: MixHouseTrackListModel.SortLastPlayedAt,     width: 150, align: Qt.AlignLeft,    visible: true },
        { key: "color",        label: qsTrc("mixhouse", "Color"),       sort: -1,                                          width: 70,  align: Qt.AlignHCenter, visible: true },
        { key: "album",        label: qsTrc("mixhouse", "Album"),       sort: MixHouseTrackListModel.SortAlbum,            width: 200, align: Qt.AlignLeft,    visible: false },
        { key: "timesPlayed",  label: qsTrc("mixhouse", "Played"),      sort: MixHouseTrackListModel.SortTimesPlayed,      width: 100, align: Qt.AlignRight,   visible: false },
        { key: "albumArtist",  label: qsTrc("mixhouse", "Album Artist"),sort: MixHouseTrackListModel.SortAlbumArtist,      width: 200, align: Qt.AlignLeft,    visible: false },
        { key: "composer",     label: qsTrc("mixhouse", "Composer"),    sort: MixHouseTrackListModel.SortComposer,         width: 200, align: Qt.AlignLeft,    visible: false },
        { key: "grouping",     label: qsTrc("mixhouse", "Grouping"),    sort: MixHouseTrackListModel.SortGrouping,         width: 200, align: Qt.AlignLeft,    visible: false },
        { key: "bitrate",      label: qsTrc("mixhouse", "Bitrate"),     sort: MixHouseTrackListModel.SortBitrate,          width: 80,  align: Qt.AlignRight,   visible: false },
        { key: "replaygain",   label: qsTrc("mixhouse", "ReplayGain"),  sort: MixHouseTrackListModel.SortReplayGain,       width: 100, align: Qt.AlignRight,   visible: false },
        { key: "fileType",     label: qsTrc("mixhouse", "Type"),        sort: MixHouseTrackListModel.SortFileType,         width: 80,  align: Qt.AlignLeft,    visible: false },
        { key: "filePath",     label: qsTrc("mixhouse", "Location"),    sort: -1,                                          width: 300, align: Qt.AlignLeft,    visible: false },
        // Real mixxx columns (library/columncache.cpp:94-102), hidden by
        // default there too. Samplerate has real backing data in this port
        // (already read for beatgrid decoding); Channels has none anywhere
        // in this schema, same as real mixxx's own "Not yet supported" cell
        // (basetracktablemodel.cpp:794-797) -- always blank, not a regression.
        { key: "sampleRate",   label: qsTrc("mixhouse", "Samplerate"),  sort: MixHouseTrackListModel.SortSampleRate,       width: 90,  align: Qt.AlignRight,   visible: false },
        { key: "channels",     label: qsTrc("mixhouse", "Channels"),    sort: MixHouseTrackListModel.SortChannels,         width: 70,  align: Qt.AlignRight,   visible: false }
    ]

    // The 8 track colors of rekordbox (and Lexicon), the ones DJs know. The
    // 19-color mixxx palette made the Select Color submenu too tall to open.
    readonly property var trackColorPalette: [
        { hex: 0xF870F8, name: qsTrc("mixhouse", "Pink") },
        { hex: 0xF84C4C, name: qsTrc("mixhouse", "Red") },
        { hex: 0xF8A030, name: qsTrc("mixhouse", "Orange") },
        { hex: 0xF8E331, name: qsTrc("mixhouse", "Yellow") },
        { hex: 0x1EE000, name: qsTrc("mixhouse", "Green") },
        { hex: 0x10B1F8, name: qsTrc("mixhouse", "Aqua") },
        { hex: 0x3264F8, name: qsTrc("mixhouse", "Blue") },
        { hex: 0x9050F8, name: qsTrc("mixhouse", "Purple") }
    ]

    readonly property int minColumnWidth: 28

    // A fixed, deliberately non-theme red for "Delete from Disk" -- the project's
    // own direct request for that one item to render in an actual visible
    // red, not a muted theme tint. A one-off exception to this file's
    // otherwise-universal ui.theme.* convention, same spirit as the Key
    // column's fixed Mixed-In-Key palette elsewhere in this file.
    readonly property string deleteFromDiskColor: "#ff3b30"

    // TableView has no built-in "current row"/selection model of its own the
    // way a real QTableView does -- Mixxx's own table gets ctrl/shift-click
    // multi-select for free from Qt's stock QAbstractItemView::
    // ExtendedSelection (see the Mixxx research notes: no custom logic at
    // all, just setSelectionMode(ExtendedSelection)); ours is a plain list
    // model driving a TableView, so both pieces of that are tracked
    // directly here instead.
    //   currentRow    -- the last plain- or cmd-clicked row: the shift-click
    //                    range anchor, the same role Qt's own
    //                    selectionModel()->currentIndex() plays.
    //   selectedRows  -- every currently selected row index (order doesn't
    //                    matter, only membership) -- what the row highlight
    //                    and the track menu's batch "Collection Actions"
    //                    both read.
    property int currentRow: -1
    // GlideMix (Lexicon): the playlists and smart playlists the selected song
    // is in, marked in the sidebar ("pl-<id>", "sp-<id>").
    property var containingNodes: []

    // GlideMix (Lexicon): each list remembers its sort, scroll position and
    // selected song while the app runs, so going back to a playlist puts you
    // where you were.
    property string currentViewKey: "all:-1"
    property var viewMemory: ({})
    property var pendingViewRestore: null
    // GlideMix: the Rekordbox XML row opened, so its playlists show the
    // moment the XML has been read.
    function openRekordboxNode() {
        if (sidebarColumn.expandedIds.indexOf("rekordbox") < 0) {
            sidebarColumn.expandedIds = sidebarColumn.expandedIds.concat(["rekordbox"])
        }
    }

    function rememberViewAndSwitch(kind, refId) {
        if (kind + ":" + refId === root.currentViewKey) {
            return   // the same list clicked again: nothing changes
        }
        var memory = root.viewMemory
        memory[root.currentViewKey] = { sort: tracksModel.sortKey, ascending: tracksModel.sortAscending,
                                        y: trackTableView.contentY, row: root.currentRow }
        root.viewMemory = memory
        root.currentViewKey = kind + ":" + refId
        root.pendingViewRestore = memory[root.currentViewKey] || null
    }
    //! Show in Library from another tab: this panel's tab has to come forward.
    signal raiseRequested()

    // Show in Library: All Tracks, nothing filtering it, the track selected --
    // or, for mode 1 and 2, only its album's or artist's songs (in the search
    // box, so clearing it brings the rest back).
    // Writes the box the way typing would: text shown, searchText following it.
    // Through "" first: what was typed is not in currentText, so the same value
    // would change nothing.
    function setSearchBox(text) {
        search.currentText = ""
        search.currentText = text
        search.searchText = text
    }

    function revealTrack(trackId, mode) {
        root.rememberViewAndSwitch("all", -1)
        root.pendingViewRestore = null
        root.selectedSidebarRefIds = []
        var nodes = sourceList.model
        for (var i = 0; i < nodes.length; ++i) {
            if (nodes[i].kind === "all") {
                sourceList.currentIndex = i
                break
            }
        }
        keyWheel.clearSelection()
        tracksModel.setPlaylist(-1)
        tracksModel.setSearch("")
        // An older song is past the 2000 rows All Tracks shows: its title finds it.
        var info = tracksModel.trackInfo(trackId)
        var text = mode === 1 ? (info.album || "") : mode === 2 ? (info.artist || "")
                 : tracksModel.rowOfTrack(trackId) < 0 ? (info.title || "") : ""
        root.setSearchBox(text)
        tracksModel.setSearch(text)
        var row = tracksModel.rowOfTrack(trackId)
        if (row < 0) {
            return
        }
        root.selectRow(row)
        Qt.callLater(function() { trackTableView.positionViewAtRow(row, TableView.Contain) })
    }

    function applyViewRestore() {
        var restore = root.pendingViewRestore
        if (!restore) {
            return
        }
        if (tracksModel.sortKey !== restore.sort || tracksModel.sortAscending !== restore.ascending) {
            tracksModel.setSort(restore.sort, restore.ascending)   // loads again, which runs this once more
            if (!root.pendingViewRestore) {
                return
            }
        }
        root.pendingViewRestore = null
        Qt.callLater(function() {
            trackTableView.contentY = Math.max(0, Math.min(restore.y, trackTableView.contentHeight - trackTableView.height))
            if (restore.row >= 0 && restore.row < tracksModel.rowCount()) {
                root.selectedRows = [restore.row]
                root.currentRow = restore.row
            }
        })
    }
    onCurrentRowChanged: containingNodesTimer.restart()
    Timer {
        id: containingNodesTimer
        interval: 150
        onTriggered: root.containingNodes = root.currentRow >= 0 ? tracksModel.sidebarNodesContaining(root.currentRow) : []
    }
    Connections {
        target: tracksModel
        function onLoaded() {
            containingNodesTimer.restart()
            root.applyViewRestore()
        }
        function onRevealTrackRequested(trackId, mode) {
            root.raiseRequested()
            root.revealTrack(trackId, mode)
        }
    }
    property var selectedRows: []
    // Room left of each waveform for the preview's stop button (rekordbox).
    readonly property int previewStopZoneWidth: 18
    // True only right after selectAllRows() (Cmd/Ctrl+A or its "Select All"
    // button), cleared by any manual selection change below. selectedRows
    // itself only ever holds indices into whatever page tracksModel actually
    // has loaded (m_tracks, capped -- see mixhousetracklistmodel.cpp's own
    // load() comment) -- real bug, confirmed against the reference 33,688-
    // track library: a bulk action that resolved "Select All" through THOSE
    // row indices silently only ever covered the first ~2000 real matches,
    // however large the real selection was. This flag is what tells a bulk
    // action ("Remove from Library", "Remove from Collection") to resolve
    // against the database's real matching set instead (tracksModel's own
    // removeAllMatchingFrom*() methods), not against selectedRows/m_tracks.
    property bool allMatchingSelected: false

    // Sidebar multi-select, for batch playlist/crate deletion. Real bug
    // we hit: the sidebar had no multi-select at all -- only ever one
    // playlist/crate at a time (StyledListView's own single currentIndex) --
    // so cleaning up e.g. the many "Rekordbox: ..." playlists one real XML
    // import can create meant deleting them one at a time. Cmd/Ctrl+click
    // toggles a row into/out of this set; restricted to ONE kind at once
    // (all playlists or all crates) -- clicking a different kind, or a
    // non-deletable node, resets it, same "mixed selection collapses" rule
    // real file browsers use. Shift-range IS supported (selectSidebarRange()
    // below, anchored on sidebarAnchorIndex) -- despite the sidebar looking
    // like a tree, sidebarNodes() already hands QML one FLAT array (level/
    // expandable/kind fields carry the nesting, not real parent/child
    // structure), so a contiguous slice of it is well-defined; the range is
    // filtered down to rows matching the ANCHOR's own kind, so shift-
    // clicking across a section header or into Crates silently skips those
    // rows rather than corrupting a same-kind batch selection.
    property var selectedSidebarRefIds: []

    // After the sidebar click that asked for it: switching the list rebuilds
    // the sidebar, which must not happen inside the click (see its handler).
    function deferredView(view) {
        Qt.callLater(function() {
            if (view === "recordings") {
                tracksModel.setRecordingsView()
            } else if (view === "history") {
                tracksModel.setHistoryView()
            } else if (view === "analyze") {
                tracksModel.setAnalyzeView()
            }
        })
    }

    function browseFolderLater(path) {
        Qt.callLater(function() { tracksModel.setBrowseFolder(path) })
    }
    property bool selectedSidebarIsCrate: false
    // The last plain- or Cmd-clicked sidebar row's flat index -- the shift-
    // click range anchor, same role root.currentRow plays for the track
    // table (see its own comment).
    property int sidebarAnchorIndex: -1

    // we hit this for real: 421 playlists in a flat expanded list with
    // no way to shift-click a contiguous run of them, only Cmd+click one at
    // a time.
    function selectSidebarRange(anchor, target) {
        var m = sourceList.model
        if (!m || anchor < 0 || anchor >= m.length || target < 0 || target >= m.length) {
            return
        }
        var anchorNode = m[anchor]
        var isCrateRange = anchorNode.nodeId.indexOf("cr-") === 0
        var isPlaylistRange = anchorNode.nodeId.indexOf("pl-") === 0
        if (!isCrateRange && !isPlaylistRange) {
            return
        }
        var lo = Math.min(anchor, target)
        var hi = Math.max(anchor, target)
        var ids = []
        for (var i = lo; i <= hi; ++i) {
            var n = m[i]
            var matches = isCrateRange ? (n.nodeId.indexOf("cr-") === 0) : (n.nodeId.indexOf("pl-") === 0)
            if (matches) {
                ids.push(n.refId)
            }
        }
        root.selectedSidebarRefIds = ids
        root.selectedSidebarIsCrate = isCrateRange
    }

    // The row indices being dragged in the CURRENT internal drag gesture
    // (see the track table cell's own mouseArea.onPositionChanged comment
    // for why this has to be set before, not after, startExternalDrag()).
    // Real mixxx: WLibrarySidebar::dropEvent -> SidebarModel::dropAccept ->
    // PlaylistFeature::dropAcceptChild, which resolves the dropped QUrls
    // back to track ids via the library (trackCollectionManager::
    // resolveTrackIds) since mixxx's sidebar has to accept a drop from ANY
    // source (Finder, another app). This app's own sidebar only ever
    // receives an internal drag (the row cell's own drag source, marked
    // via QMimeData::setText("library") in startExternalDrag()) -- an
    // external file/Finder drop was never wired to accept here, matching
    // this app's own file-add flow going through addFolderToLibrary/
    // addTracksToPlaylist elsewhere -- so the drop handler below can skip
    // the URL round-trip and use these already-known row indices directly.
    property var draggingRows: []

    // Real mixxx's editable library cells (basesqltablemodel.cpp:706-733) --
    // Comment/Grouping/Track #/Composer/Album Artist, the only columns this
    // port has a real write path for (see IMixHouseLibrary::setTrackComment's
    // own comment; every other real mixxx-editable column -- Title/Artist/
    // Album/Year/BPM -- still has no write path here and stays read-only).
    // -1/"" means "not currently editing any cell".
    property int editingRow: -1
    property string editingColumnKey: ""
    readonly property var editableColumnKeys: ["comment", "grouping", "trackNumber", "composer", "albumArtist"]
    function isEditableColumn(key) {
        return root.editableColumnKeys.indexOf(key) >= 0
    }
    function beginEdit(row, key) {
        root.editingRow = row
        root.editingColumnKey = key
    }
    function commitEdit(row, key, text) {
        root.editingRow = -1
        root.editingColumnKey = ""
        switch (key) {
        case "comment":      tracksModel.setTrackComment(row, text); break
        case "grouping":     tracksModel.setTrackGrouping(row, text); break
        case "trackNumber":  tracksModel.setTrackNumber(row, text); break
        case "composer":     tracksModel.setTrackComposer(row, text); break
        case "albumArtist":  tracksModel.setTrackAlbumArtist(row, text); break
        }
    }
    function cancelEdit() {
        root.editingRow = -1
        root.editingColumnKey = ""
    }

    function isRowSelected(row) {
        return root.selectedRows.indexOf(row) !== -1
    }

    // Plain click: replace the whole selection with just this row, and make
    // it the new shift-click anchor.
    function selectRow(row) {
        root.selectedRows = [row]
        root.currentRow = row
        root.allMatchingSelected = false
    }

    // Shift+click: the contiguous range between the anchor and this row,
    // inclusive, REPLACING whatever was selected before -- not unioned with
    // it, since only shift (not shift+cmd) was asked for. Standard Finder/
    // Explorer/DAW behavior. The anchor itself is left unchanged so a
    // second shift-click still extends from the same original row rather
    // than from wherever the last shift-click landed.
    function selectRange(anchor, row) {
        var lo = Math.min(anchor, row)
        var hi = Math.max(anchor, row)
        var out = []
        for (var i = lo; i <= hi; ++i) {
            out.push(i)
        }
        root.selectedRows = out
        root.allMatchingSelected = false
    }

    // Cmd+click: toggle just this row's membership, keeping the rest of the
    // selection intact -- the discontiguous-multi-select companion every
    // reference app (Mixxx, Rekordbox, Finder) offers alongside shift-click.
    // Becomes the new shift-click anchor.
    function toggleRowSelection(row) {
        var sel = root.selectedRows.slice()
        var i = sel.indexOf(row)
        if (i >= 0) {
            sel.splice(i, 1)
        } else {
            sel.push(row)
        }
        root.selectedRows = sel
        root.currentRow = row
        root.allMatchingSelected = false
    }

    // Cmd+A (Ctrl+A on other platforms -- StandardKey.SelectAll resolves the
    // platform's own convention): select every row currently in view, the
    // same real DAW/file-browser convention Mixxx/Rekordbox/Finder all share.
    // Anchoring the new shift-click range at row 0 matches what every one of
    // those apps does after a select-all -- shift-clicking after it shrinks
    // the selection from the top, not from wherever it happened to end.
    // Nothing selected: what Analyze leaves behind (see the Analyze button).
    function clearRowSelection() {
        root.selectedRows = []
        root.allMatchingSelected = false
    }

    function selectAllRows() {
        // tracksModel.trackCount is the GLOBAL library total, ignoring the
        // current filter/playlist/crate -- rowCount() (Q_INVOKABLE on every
        // QAbstractItemModel) is the real count of rows actually loaded for
        // THIS view. Using trackCount() here was the root cause of a real
        // bug that ate most of a real library: it built far more
        // indices than tracksModel actually has rows for, every index past
        // the loaded page silently resolved to nothing downstream, and
        // repeating Select-All-then-Remove kept eating the next page each
        // time with no indication anything was being truncated.
        var loaded = tracksModel.rowCount()
        if (loaded <= 0) {
            return
        }
        var all = []
        for (var i = 0; i < loaded; ++i) {
            all.push(i)
        }
        root.selectedRows = all
        root.currentRow = 0
        // Select All always means "every track matching this view", however
        // many that really is -- not just the page above happens to cover.
        // Bulk actions must check this and, when true, resolve against
        // tracksModel's own removeAllMatchingFrom*() methods instead of
        // selectedRows.
        root.allMatchingSelected = true
    }

    // Reassigning root.columns changes the DATA columnWidthProvider will return
    // next time it's asked, but TableView only actually re-asks it when told to --
    // it doesn't watch this array. Every mutator below must call forceLayout(),
    // or the change is real underneath but invisible until something unrelated
    // happens to trigger a relayout. This was the show/hide-columns bug: the menu
    // toggled `visible` correctly, but nothing ever asked the table to redraw.
    //
    // Every mutator runs via Qt.callLater(), deferred to the next event-loop
    // tick, for a sharper reason than tidiness: each of these is called from
    // inside a mouse handler that lives on a header CELL delegate, and since
    // reuseItems is off (see trackTableView below), forceLayout() destroys and
    // recreates every header cell -- including, possibly, the very one whose
    // handler is still running. Finishing that handler's remaining lines
    // against an object mid-destruction threw "Value is undefined and could
    // not be converted to an object". Deferring lets the current handler (and
    // the mouse-release event) finish first, on the still-alive delegate,
    // before anything gets torn down.
    function toggleColumn(i) {
        Qt.callLater(function() {
            var c = root.columns
            c[i].visible = !c[i].visible
            root.columns = c
            saveColumnLayout()
            trackTableView.forceLayout()
        })
    }

    function setColumnWidth(i, w) {
        Qt.callLater(function() {
            var c = root.columns
            c[i].width = Math.max(root.minColumnWidth, w)
            root.columns = c
            saveColumnLayout()
            trackTableView.forceLayout()
        })
    }

    function startLibraryDrag(rows) {
        tracksModel.startExternalDrag(rows)
    }

    function saveColumnLayout() {
        var snapshot = []
        for (var i = 0; i < root.columns.length; ++i) {
            var c = root.columns[i]
            snapshot.push({ key: c.key, visible: !!c.visible, width: c.width })
        }
        columnSettings.layoutJson = JSON.stringify(snapshot)
    }

    function restoreColumnLayout() {
        if (!columnSettings.layoutJson) return
        try {
            var saved = JSON.parse(columnSettings.layoutJson)
            if (!Array.isArray(saved)) return
            var byKey = {}
            for (var i = 0; i < root.columns.length; ++i) byKey[root.columns[i].key] = root.columns[i]
            var restored = []
            for (var j = 0; j < saved.length; ++j) {
                var item = saved[j]
                if (item.key === "preview") item.key = "analysisProgress"
                var col = byKey[item.key]
                if (!col) continue
                col.visible = item.visible !== false
                if (Number(item.width) > 0) col.width = Number(item.width)
                restored.push(col)
                delete byKey[item.key]
            }
            // A column added in a newer version goes next to the column it
            // follows by default (Energy after BPM), not to the far right.
            for (var k = 0; k < root.columns.length; ++k) {
                var extra = byKey[root.columns[k].key]
                if (!extra) continue
                var at = restored.length
                if (k > 0) {
                    var before = restored.indexOf(root.columns[k - 1])
                    if (before >= 0) at = before + 1
                }
                restored.splice(at, 0, extra)
            }
            if (restored.length > 0) root.columns = restored
        } catch (e) {
            columnSettings.layoutJson = ""
        }
    }

    // Move column `from` to sit at column `to`'s position -- a real reorder
    // (splice out, splice in), not a simple two-item swap, so dragging a column
    // three places over takes one gesture, matching real Mixxx's native
    // QHeaderView::setSectionsMovable(true) (src/widget/wtracktableviewheader.cpp).
    function moveColumn(from, to) {
        if (from === to) return
        Qt.callLater(function() {
            // A `var` property does not emit columnsChanged when the same
            // JavaScript array is mutated and assigned back to itself. That
            // left GlideMix's header delegates showing the old order
            // even though the reordered layout had already been persisted.
            // Splice a new array so every header and table-cell binding is
            // invalidated immediately for every column type.
            var c = root.columns.slice()
            var moved = c.splice(from, 1)[0]
            c.splice(to, 0, moved)
            root.columns = c
            saveColumnLayout()
            trackTableView.forceLayout()
        })
    }

    // Width of column `i` as TableView's columnWidthProvider must report it: the
    // single source both the header (via HorizontalHeaderView's syncView) and the
    // track rows read, so they can never drift out of alignment the way the old
    // hand-rolled per-delegate x/width math did.
    //
    // Deliberately NOT auto-stretching the last visible column to fill leftover
    // space (an earlier version did, copying Qt's stretchLastSection convention
    // mixxx's own header uses) -- "user can choose to resize". That
    // convention actively fought manual resizing: Math.max(width, leftover) meant
    // whichever column ended up last could never be dragged narrower than the
    // leftover space, no matter how far you pulled the handle. Every column now
    // just uses its own set width, full stop; any space past the last visible
    // column stays blank rather than being force-fed into one of them.
    function columnWidthFor(i) {
        var c = root.columns[i]
        return (c && c.visible) ? c.width : 0
    }

    // GlideMix: Preferences > Library Font. The theme's own font until
    // a family or size is chosen.
    readonly property font libraryFont: {
        const family = tracksModel.libraryFontFamily
        const size = tracksModel.libraryFontPointSize
        if (family === "" && size <= 0) {
            return ui.theme.bodyFont
        }
        const spec = { family: family !== "" ? family : ui.theme.bodyFont.family }
        if (size > 0) {
            // The size as macOS draws it: one point is one pixel there,
            // and 1.33 on Windows, which made the rows a third bigger.
            spec.pixelSize = size
        } else {
            spec.pixelSize = ui.theme.bodyFont.pixelSize
        }
        return Qt.font(spec)
    }

    function cellText(model, key) {
        switch (key) {
        case "bpm":         return model.bpm > 0 ? model.bpm.toFixed(tracksModel.bpmPrecision) : "—"
        case "energy":      return model.energy > 0 ? String(Math.round(model.energy)) : "—"
        case "musicalKey":  return model.musicalKey.length > 0 ? model.musicalKey : "—"
        // Five stars, empty ones outlined, like rekordbox.
        case "rating":      return "★".repeat(Math.max(0, model.rating)) + "☆".repeat(5 - Math.max(0, Math.min(5, model.rating)))
        case "durationSec":
            // -1 = NULL in the DB (header not parsed yet) -- real mixxx
            // shows a blank cell here, not a real 0:00 (basetracktablemodel.
            // cpp:620-624); see MixHouseTrack::durationSec's own comment.
            if (model.durationSec < 0) {
                return "—"
            }
            var s = Math.max(0, Math.round(model.durationSec))
            var m = Math.floor(s / 60), r = s % 60
            return m + ":" + (r < 10 ? "0" : "") + r
        case "bitrate":      return model.bitrate > 0 ? model.bitrate + " kbps" : "—"
        case "sampleRate":   return model.sampleRate > 0 ? model.sampleRate + " Hz" : "—"
        // No backing column anywhere in this schema -- see this column's
        // own entry in `columns` above for why that matches real mixxx.
        case "channels":     return "—"
        case "replaygain":
            // library.replaygain stores a linear RATIO (1.0 = unity), not
            // dB -- real mixxx converts via ratio2db() (util/math.h:
            // log10(ratio)*20) before ever displaying it
            // (basetracktablemodel.cpp's COLUMN_LIBRARYTABLE_REPLAYGAIN
            // case -> ReplayGain::ratioToString, 2 decimal places). This
            // used to print the raw ratio with a "dB" suffix stuck on --
            // not a unit conversion bug real mixxx has, one this port
            // introduced.
            if (model.replaygain <= 0) { return "—" }
            var db = 20 * Math.log10(model.replaygain)
            return (db > 0 ? "+" : "") + db.toFixed(2) + " dB"
        case "dateAdded":    return model.dateAdded ? model.dateAdded.split(" ")[0] : "—"
        case "lastPlayedAt": return model.lastPlayedAt ? model.lastPlayedAt.split(" ")[0] : "—"
        case "timesPlayed":  return String(model.timesPlayed)
        case "trackNumber":  return model.trackNumber ? model.trackNumber : ""
        // Rendered as their own item in the row delegate, not as text.
        case "analysisProgress": case "coverArt": case "color": case "overview": return ""
        default: return model[key] !== undefined ? model[key] : ""
        }
    }

    // Mixxx's sidebar uses its own 32px SVG artwork for each library feature
    // (res/images/library in the Mixxx source). The same source assets live in
    // this module and are color-overlaid at render time, so the silhouettes
    // stay identical while the active MixHouse theme supplies the color.
    // GlideMix: Export to Folder -- the songs in order, a tracklist and an .m3u8.
    function exportToFolder(kind, id, name) {
        var dest = exportFolderPicker.selectDirectory()
        if (dest) {
            tracksModel.exportPlaylistToFolder(kind, id, name, dest)
        }
    }

    function sidebarFeatureIcon(nodeId, isFolder) {
        if ((isFolder && nodeId.indexOf("pl-") === 0) || nodeId.indexOf("dir:") === 0) return "ic_library_folder.svg"
        if (nodeId === "tracks") return "ic_library_tracks.svg"
        if (nodeId === "autodj") return "ic_library_autodj.svg"
        if (nodeId === "playlists") return "ic_library_playlist.svg"
        if (nodeId === "smart" || nodeId.indexOf("sp-") === 0) return "ic_library_smart.svg"
        if (nodeId === "crates" || nodeId.indexOf("cr-") === 0) return "ic_library_crates.svg"
        if (nodeId === "computer") return "ic_library_computer.svg"
        if (nodeId === "recordings") return "ic_library_recordings.svg"
        if (nodeId === "history") return "ic_library_history.svg"
        if (nodeId === "analyze") return "ic_library_prepare.svg"
        if (nodeId === "rekordbox") return "ic_library_rekordbox.svg"
        if (nodeId === "lexicon") return "ic_library_lexicon.png"
        return ""
    }

    Component.onCompleted: {
        restoreColumnLayout()
        tracksModel.uiColumnCount = root.columns.length
        tracksModel.load()
        // "Music library" Preferences page's own "Rescan directories on
        // start-up" -- see MixHouseTrackListModel::runStartupRescanIfNeeded's
        // own comment.
        tracksModel.runStartupRescanIfNeeded()
    }

    // Cmd+A / Ctrl+A: select every track in the current view. Scoped off
    // while the search box has focus so its own native "select all THIS
    // TEXT" behavior still works normally -- we were asked for select-all on
    // the track table, not for stealing the shortcut out of the search field.
    Shortcut {
        // NOT StandardKey.SelectAll (Cmd+A on macOS): confirmed directly by
        // opening Audacity's own Select menu -- "Select all" (the audio
        // timeline's own selection) is ALREADY a real, natively-registered
        // shortcut on Cmd+A app-wide (and "Select all tracks" already owns
        // Shift+Cmd+A too), through the same Muse global-shortcut system,
        // which always wins that keystroke over an ad-hoc QML-level
        // Shortcut -- that's the actual reason it silently never fired, not
        // a focus/context scoping issue.
        //
        // NOT the string "Ctrl+A" either, even though that sounds like the
        // physical Control key -- this app never sets
        // Qt::AA_MacDontSwapCtrlAndMeta (confirmed when the Cmd+click
        // multi-select toggle was built earlier), so on macOS Qt's "Ctrl"
        // token in a key-sequence STRING resolves to the physical COMMAND
        // key, same as StandardKey.SelectAll -- it was still secretly bound
        // to the exact same already-taken Cmd+A the whole time, which is
        // exactly why changing it to "Ctrl+A" appeared to do nothing. "Meta"
        // is Qt's token for the physical Control key under that same
        // swapped convention -- this is the real, different binding.
        sequence: "Meta+A"
        context: Qt.ApplicationShortcut
        enabled: root.visible && !search.activeFocus
        onActivated: root.selectAllRows()
    }

    Shortcut {
        // Real mixxx binds Delete/Backspace (QKeySequence::Delete or
        // Qt::Key_Backspace) to remove/hide the selection (widget/
        // wtracktableview.cpp:1293-1298,1386-1478) -- there was previously
        // no way to do this from the keyboard at all here, only via the
        // track menu. Remove from whichever collection is active (playlist/
        // crate) when viewing one -- real mixxx doesn't confirm that either,
        // since it doesn't touch the library itself -- else "Remove from
        // Library", which already has its own real confirm dialog (this
        // port's old Hidden/mixxx_deleted flag was retired in favor of a
        // real purge -- see the Collection Actions submenu's own comment --
        // so there's no separate silent "Hide" path to fall back to here).
        sequences: ["Delete", "Backspace"]
        context: Qt.ApplicationShortcut
        enabled: root.visible && !search.activeFocus && root.selectedRows.length > 0
        onActivated: {
            // allMatchingSelected (set only by selectAllRows()) means the
            // real target is every track matching this view, however many
            // that is -- not just root.selectedRows, which only ever holds
            // indices into whatever page is actually loaded. See
            // removeAllMatchingFromLibraryWithConfirm()'s own comment for
            // the real bug this distinction exists to prevent a repeat of.
            if (tracksModel.isViewingCollection()) {
                if (root.allMatchingSelected) {
                    tracksModel.removeAllMatchingFromCurrentCollection()
                } else {
                    tracksModel.removeTracksFromCurrentCollection(root.selectedRows)
                }
            } else {
                if (root.allMatchingSelected) {
                    tracksModel.removeAllMatchingFromLibraryWithConfirm()
                } else {
                    tracksModel.removeTracksFromLibraryWithConfirm(root.selectedRows)
                }
            }
        }
    }

    // A real SplitView, not a fixed-width RowLayout -- "we need to be
    // able to drag sidebar to make wider or smaller... for long names"
    // (playlist/crate names truncating in a fixed 200px sidebar). Qt's own
    // SplitView, not a hand-rolled MouseArea-based handle: this session has
    // twice shipped a subtly-wrong hand-rolled drag/resize (the column resize
    // fighting HorizontalHeaderView's own positioning; the panel-boundary
    // highlight contending with the dock's own splitter) -- a real,
    // already-correct Qt component for exactly this job is the safer choice
    // here, not a third attempt at writing the same kind of code by hand.
    SplitView {
        anchors.fill: parent
        anchors.margins: 8
        orientation: Qt.Horizontal

        // Styled to match this panel's own SeparatorLine (ui.theme.strokeColor,
        // 1px) rather than SplitView's OS-default handle, with the same
        // wider-than-visible hit target and hover highlight the column resize
        // handle uses -- a real, working pattern, just reused here instead of
        // handles for two different mechanisms.
        handle: Rectangle {
            implicitWidth: 9
            color: "transparent"

            Rectangle {
                anchors.centerIn: parent
                width: 1
                height: parent.height
                color: (parent.SplitHandle.hovered || parent.SplitHandle.pressed)
                       ? ui.theme.accentColor : ui.theme.strokeColor

                Behavior on width { NumberAnimation { duration: 80 } }
                Behavior on color { ColorAnimation { duration: 80 } }
            }
        }

        // ---- Lancelot wheel + source tree -----------------------------------
        ColumnLayout {
            id: sidebarColumn
            SplitView.preferredWidth: 200
            SplitView.minimumWidth: 190
            Layout.fillHeight: true
            spacing: 4

            // Collapsible header, as in the fork: says what is filtered even when
            // collapsed, so a hidden filter never makes the library look broken.
            FlatButton {
                id: keyWheelButton
                Layout.fillWidth: true

                // Keep the accessible name free of the disclosure glyph; the
                // custom content below draws the same compact filled wedge used
                // by every sidebar row.
                text: qsTrc("mixhouse", "Key Wheel")
                      + (keyWheel.selectedKeys.length === 1
                         ? "  \u00B7  " + keyWheel.selectedLabels[0]
                         : keyWheel.selectedKeys.length > 1
                           ? "  \u00B7  " + keyWheel.selectedKeys.length + " keys" : "")

                contentItem: Component {
                    RowLayout {
                        spacing: 5

                        StyledTextLabel {
                            Layout.preferredWidth: 16
                            Layout.preferredHeight: 16
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            text: keyWheelButton.wheelExpanded ? "\u25BC" : "\u25B6"
                            font.pixelSize: 14
                            color: ui.theme.accentColor
                        }

                        StyledTextLabel {
                            text: qsTrc("mixhouse", "Key Wheel")
                                  + (keyWheel.selectedKeys.length === 1
                                     ? "  \u00B7  " + keyWheel.selectedLabels[0]
                                     : keyWheel.selectedKeys.length > 1
                                       ? "  \u00B7  " + keyWheel.selectedKeys.length + " keys" : "")
                            font: ui.theme.bodyFont
                            color: ui.theme.fontPrimaryColor
                        }
                    }
                }

                property bool wheelExpanded: true
                onClicked: wheelExpanded = !wheelExpanded
            }

            MixHouseKeyWheel {
                id: keyWheel

                notation: tracksModel.keyNotation

                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 182
                Layout.preferredHeight: 182
                visible: parent.children[0].wheelExpanded

                onSelectionChanged: function(lancelotKeys) {
                    tracksModel.setKeyFilters(lancelotKeys)
                }
            }

            FlatButton {
                Layout.alignment: Qt.AlignRight
                text: qsTrc("mixhouse", "Reset")
                visible: keyWheel.hasSelection
                onClicked: keyWheel.clearSelection()
            }

            // The sidebar is Mixxx's fixed feature tree, not the Playlists table:
            // those 123 date rows are session logs (PLHT_SET_LOG) and live under
            // History. Expandable parents collapse by default.
            property var expandedIds: ["tracks"]

            // sidebarNodes() has no way to be a tracked QML dependency -- it's
            // a plain function call, so a normal `model:` binding re-runs when
            // expandedIds changes but misses every count/playlist update from
            // an actual write. Bumped from the Connections below purely to
            // give the `model:` binding a second real dependency to react to.
            //
            // A real, confirmed bug lived in the PREVIOUS fix for this same
            // problem: it reassigned `sourceList.model` IMPERATIVELY from
            // onLoaded (`sourceList.model = tracksModel.sidebarNodes(...)`).
            // In QML, assigning a value to a property that already has a
            // declarative binding permanently SEVERS that binding -- so the
            // very first onLoaded() after startup replaced the live
            // `model: tracksModel.sidebarNodes(parent.expandedIds)` binding
            // with a frozen snapshot, and every later click toggling
            // expandedIds (Playlists/Crates/History) did nothing at all from
            // that point on, forever, until the NEXT onLoaded() happened to
            // fire and re-capture whatever expandedIds was current right
            // then. With only 1-2 playlists this was easy to miss; we hit
            // it directly after a real Rekordbox import created 423 --
            // clicking to expand Playlists highlighted the row and flipped
            // its arrow, but no child rows ever appeared, because expandedIds
            // toggling had been a dead end since the first load() years
            // earlier in the session's own object lifetime.
            property int sidebarRefreshToken: 0

            StyledListView {
                id: sourceList

                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                // Referencing sidebarRefreshToken here (a real property read,
                // not a comma-expression -- that was tried and reverted: it
                // broke `modelData` injection entirely, apparently confusing
                // QML's model-type detection for the `model:` property
                // specifically) gives this ONE declarative binding a second
                // real dependency, so it re-evaluates on either trigger
                // without ever being imperatively reassigned (see
                // sidebarRefreshToken's own comment for why that broke
                // expandedIds toggling permanently).
                model: {
                    parent.sidebarRefreshToken;
                    return tracksModel.sidebarNodes(parent.expandedIds);
                }

                // GlideMix: a refresh rebuilds this list's model, which put
                // the sidebar back at the top after every click; this keeps
                // where it was.
                function keepScroll() {
                    if (!scrollRestore.running) {
                        scrollRestore.y = contentY
                    }
                    scrollRestore.restart()
                }

                Timer {
                    id: scrollRestore
                    property real y: 0
                    interval: 1
                    onTriggered: {
                        sourceList.forceLayout()
                        sourceList.contentY = Math.max(0, Math.min(y, sourceList.contentHeight - sourceList.height))
                    }
                }

                // GlideMix: moving playlists and folders by drag and drop.
                property int plDragId: -1
                property int plDropIndex: -1
                property string plDropMode: ""   // "before", "after" or "into"

                function updatePlaylistDrop(p) {
                    plDropIndex = -1
                    plDropMode = ""
                    const i = indexAt(p.x, p.y)
                    if (i < 0) {
                        return
                    }
                    const n = model[i]
                    if (n.nodeId === "playlists") {
                        plDropMode = "into"
                    } else if (n.nodeId.indexOf("pl-") !== 0 || n.refId === plDragId) {
                        return
                    } else {
                        const row = itemAtIndex(i)
                        const frac = row ? (p.y - row.y) / row.height : 0.5
                        if (n.expandable) {   // a folder: its edges place it, the middle goes in
                            const open = parent.expandedIds.indexOf(n.nodeId) >= 0
                            plDropMode = frac < 0.25 ? "before" : (frac > 0.75 && !open ? "after" : "into")
                        } else {
                            plDropMode = frac < 0.5 ? "before" : "after"
                        }
                    }
                    plDropIndex = i
                }

                function finishPlaylistDrag() {
                    const id = plDragId
                    const i = plDropIndex
                    const mode = plDropMode
                    plDragId = -1
                    plDropIndex = -1
                    plDropMode = ""
                    if (i < 0) {
                        return
                    }
                    const n = model[i]
                    if (mode === "into") {
                        tracksModel.movePlaylist(id, n.nodeId === "playlists" ? -1 : n.refId, -1)
                        return
                    }
                    let before = n.refId
                    if (mode === "after") {   // before the next one at its level, or last
                        before = -1
                        for (let j = i + 1; j < model.length; ++j) {
                            const m = model[j]
                            if (m.level < n.level) {
                                break
                            }
                            if (m.level === n.level && m.refId !== id) {
                                before = m.refId
                                break
                            }
                        }
                    }
                    tracksModel.movePlaylist(id, n.parentId, before)
                }

                Connections {
                    target: tracksModel
                    function onLoaded() {
                        sourceList.keepScroll()
                        sourceList.parent.sidebarRefreshToken++
                    }
                }

                delegate: ListItemBlank {
                    id: sidebarRow
                    height: 24
                    width: ListView.view.width
                    // A sidebar multi-selection (root.selectedSidebarRefIds,
                    // Cmd/Ctrl+click below) overrides the plain single-
                    // current-item highlight for whichever kind it holds;
                    // every other row (and every row once the multi-
                    // selection is empty again) falls back to the ordinary
                    // ListView.isCurrentItem it always used.
                    isSelected: root.selectedSidebarRefIds.length > 0
                        ? (root.selectedSidebarIsCrate === (modelData.nodeId.indexOf("cr-") === 0)
                           && (modelData.nodeId.indexOf("pl-") === 0 || modelData.nodeId.indexOf("cr-") === 0)
                           && root.selectedSidebarRefIds.indexOf(modelData.refId) >= 0)
                        : ListView.isCurrentItem

                    // Real mixxx (and every other real DJ app -- rekordbox,
                    // Apple Music) manages playlists/crates from the sidebar,
                    // not from a track's own context menu: right-click
                    // Playlists/Crates itself for "Create New...", and right-
                    // click an actual playlist/crate row for Rename/Delete.
                    // History rows share the SAME "playlist" kind marker
                    // sidebarNodes() gives real playlists (both are session
                    // lists in mixxx's own Playlists table), so this goes by
                    // nodeId prefix ("pl-"/"cr-") instead, which is unique to
                    // real user-created playlists/crates.
                    mouseArea.acceptedButtons: Qt.LeftButton | Qt.RightButton

                    onClicked: function(mouse) {
                        // Real bug, root-caused from a real session
                        // log (first "TypeError: Cannot read property 'kind'
                        // of undefined", later a plain "ReferenceError:
                        // modelData is not defined" at the exact same line):
                        // this whole function reads modelData AGAIN further
                        // down, AFTER the plain-click branch below writes
                        // sourceList.parent.expandedIds -- and expandedIds is
                        // exactly what the sidebar's own `model: { ...
                        // sidebarNodes(parent.expandedIds) }` binding above
                        // depends on. Writing it fires that binding's
                        // re-evaluation SYNCHRONOUSLY, inside this very call
                        // stack, rebuilding sourceList's whole model and
                        // recycling delegates -- including, sometimes, THIS
                        // one, while its own onClicked is still running.
                        // Once that happens modelData no longer resolves at
                        // all (a real ReferenceError, not just a null
                        // property), which is why clicking Playlists/Music
                        // Collection (both expandable) to leave a
                        // playlist/crate view silently never reached
                        // setPlaylist(-1)/setCrate() below -- the crash cut
                        // this function off first, every real time. Fixed by
                        // reading every field this function needs OFF
                        // modelData right here, before any state change, and
                        // using only these local copies from this point on --
                        // never modelData itself again below.
                        if (!modelData) {
                            return
                        }
                        var nodeId = modelData.nodeId
                        var kind = modelData.kind
                        var refId = modelData.refId
                        var name = modelData.name
                        var locked = modelData.locked
                        var expandable = modelData.expandable
                        var folderPath = modelData.path || ""
                        var folderRemovable = !!modelData.removable
                        var isPl = nodeId.indexOf("pl-") === 0
                        var isCr = nodeId.indexOf("cr-") === 0
                        if (mouse && mouse.button === Qt.LeftButton
                                && (mouse.modifiers & Qt.ControlModifier) && (isPl || isCr)) {
                            // Cmd on macOS (this app never sets
                            // Qt.AA_MacDontSwapCtrlAndMeta -- see the track
                            // table's own Meta+A comment for the confirmed
                            // reasoning), same modifier the track table's
                            // toggleRowSelection() uses for its own
                            // discontiguous multi-select. Pure selection
                            // toggle: no expand/collapse, no view switch.
                            if (root.selectedSidebarRefIds.length > 0 && root.selectedSidebarIsCrate !== isCr) {
                                root.selectedSidebarRefIds = [refId]   // switched kind -- start fresh
                            } else {
                                var sel = root.selectedSidebarRefIds.slice()
                                var si = sel.indexOf(refId)
                                if (si >= 0) { sel.splice(si, 1) } else { sel.push(refId) }
                                root.selectedSidebarRefIds = sel
                            }
                            root.selectedSidebarIsCrate = isCr
                            root.sidebarAnchorIndex = index
                            return
                        }
                        if (mouse && mouse.button === Qt.LeftButton
                                && (mouse.modifiers & Qt.ShiftModifier) && (isPl || isCr)
                                && root.sidebarAnchorIndex >= 0) {
                            // Extends/replaces the selection from the anchor,
                            // same "replace, not union" rule the track
                            // table's own selectRange() follows for Shift
                            // there -- the anchor itself is left unchanged so
                            // a second shift-click still extends from the
                            // same original row.
                            root.selectSidebarRange(root.sidebarAnchorIndex, index)
                            return
                        }
                        sourceList.currentIndex = index
                        if (mouse && mouse.button === Qt.RightButton) {
                            var pos = sidebarRow.mapToItem(sidebarColumn, mouse.x, mouse.y)
                            if (nodeId === "playlists") {
                                // Real rekordbox's own root-level menu offers
                                // both (manual pp.52, 248) -- see
                                // MixHouseNamedItem::parentId's own comment
                                // for why folders are original work here.
                                newPlaylistRootMenu.openFor(pos, -1)
                            } else if (nodeId === "crates") {
                                newListPopup.openStandalone(true)
                            } else if (isPl || isCr) {
                                // Right-clicking a row that's already part of
                                // an active same-kind multi-selection (2+
                                // items) opens the batch menu for the WHOLE
                                // selection; otherwise this collapses the
                                // selection to just this one row first, same
                                // "right-click an unselected row replaces the
                                // selection" rule the track table's own
                                // trackMenu.open() follows. A folder never
                                // joins a multi-selection batch delete this
                                // way in practice (Cmd/Shift-click still
                                // allow mixing one in, so this only guards
                                // openFor's own single-item path below).
                                if (root.selectedSidebarIsCrate === isCr && root.selectedSidebarRefIds.length > 1
                                        && root.selectedSidebarRefIds.indexOf(refId) >= 0) {
                                    sidebarItemMenu.openForBatch(pos, isCr, root.selectedSidebarRefIds.slice())
                                } else {
                                    root.selectedSidebarRefIds = [refId]
                                    root.selectedSidebarIsCrate = isCr
                                    sidebarItemMenu.openFor(pos, isCr, refId, name, locked, expandable)
                                }
                            } else if (kind === "folder-browse") {
                                folderNodeMenu.folderPath = folderPath
                                folderNodeMenu.show(pos, folderRemovable ? [
                                    { id: "import-folder", title: qsTrc("mixhouse", "Add to Music Collection") },
                                    {},
                                    { id: "remove-link", title: qsTrc("mixhouse", "Remove from Quick Links") }
                                ] : [
                                    { id: "import-folder", title: qsTrc("mixhouse", "Add to Music Collection") }
                                ])
                            } else if (nodeId === "computer" || nodeId === "tracks") {
                                // Music Collection: right-click offers what its + does.
                                computerMenu.show(pos, nodeId === "tracks" ? [
                                    { id: "add-folder", title: qsTrc("mixhouse", "Add Folder to Library…") },
                                    {},
                                    { id: "export-rekordbox-xml", title: qsTrc("mixhouse", "Export to rekordbox XML…") }
                                ] : [
                                    { id: "add-folder", title: qsTrc("mixhouse", "Add Folder to Library…") }
                                ])
                            } else if (nodeId === "smart") {
                                // GlideMix: Lexicon's Smartlist Generator.
                                var sources = function(field) {
                                    return [
                                        { id: "gen:" + field + ":collection", title: qsTrc("mixhouse", "From Music Collection") },
                                        { id: "gen:" + field + ":lexicon", title: qsTrc("mixhouse", "From Lexicon DJ") },
                                        { id: "gen:" + field + ":rekordbox", title: qsTrc("mixhouse", "From Rekordbox XML") },
                                        { id: "gen:" + field + ":all", title: qsTrc("mixhouse", "From all libraries") }
                                    ]
                                }
                                smartRootMenu.show(pos, [
                                    { id: "new-smart", title: qsTrc("mixhouse", "New Smart Playlist…") },
                                    {},
                                    { id: "gen-menu", title: qsTrc("mixhouse", "Generate Smart Playlists"), subitems: [
                                        { id: "gen-genre", title: qsTrc("mixhouse", "One per Genre"), subitems: sources("genre") },
                                        { id: "gen-key", title: qsTrc("mixhouse", "One per Key"), subitems: sources("key") },
                                        { id: "gen-energy", title: qsTrc("mixhouse", "One per Energy level"), subitems: sources("energy") }
                                    ] },
                                    { id: "delete-generated", title: qsTrc("mixhouse", "Delete Generated Smart Playlists…") }
                                ])
                            } else if ((kind === "lexicon-playlist" || kind === "rekordbox-playlist") && refId >= 0) {
                                externalPlaylistMenu.kind = kind
                                externalPlaylistMenu.playlistId = refId
                                externalPlaylistMenu.playlistName = name
                                externalPlaylistMenu.show(pos, [
                                    { id: "export-folder", title: qsTrc("mixhouse", "Export to Folder…") }
                                ])
                            } else if (kind === "smart-playlist") {
                                smartItemMenu.playlistId = refId
                                smartItemMenu.playlistName = name
                                smartItemMenu.show(pos, [
                                    { id: "edit-smart", title: qsTrc("mixhouse", "Edit Smart Playlist…") },
                                    { id: "duplicate-smart", title: qsTrc("mixhouse", "Duplicate as Playlist") },
                                    { id: "export-folder", title: qsTrc("mixhouse", "Export to Folder…") },
                                    {},
                                    { id: "delete-smart", title: qsTrc("mixhouse", "Delete") }
                                ])
                            } else if (nodeId === "rekordbox") {
                                // GlideMix: Import shows the XML here, read-only and
                                // live, like Lexicon DJ; Copy puts its songs and
                                // playlists into your own Music Collection.
                                rekordboxMenu.show(pos, [
                                    { id: "import-rekordbox", title: qsTrc("mixhouse", "Import from Rekordbox XML…") },
                                    { id: "copy-rekordbox", title: qsTrc("mixhouse", "Copy Rekordbox XML into Music Collection…") },
                                    {},
                                    { id: "export-rekordbox-xml", title: qsTrc("mixhouse", "Export GlideMix to rekordbox XML…") }
                                ])
                            }
                            return
                        }
                        // A plain click (no Ctrl) always abandons any
                        // multi-selection, same convention every other app
                        // uses -- the row highlight then falls back to the
                        // ordinary single ListView.isCurrentItem above. Still
                        // becomes the new shift-click anchor regardless of
                        // kind, matching the track table's own selectRow().
                        root.selectedSidebarRefIds = []
                        root.sidebarAnchorIndex = index
                        // Real mixxx restores/clears each feature's OWN
                        // remembered search on activation (library.cpp:
                        // 318-330); this port shares one search box across
                        // every view, so without this a search typed while
                        // viewing Tracks would silently keep filtering
                        // whatever gets clicked into next, with the box's
                        // own text no longer matching what's actually
                        // active. Clearing it on every real view switch
                        // keeps the two in sync. (tracksModel itself also
                        // clears its OWN copy of the filter in each of
                        // these calls -- this just keeps the visible box in
                        // sync with that.)
                        if (["all", "playlist", "crate", "missing", "duplicates", "lexicon-all", "lexicon-playlist",
                             "rekordbox-all", "rekordbox-playlist", "smart-playlist", "history-view", "analyze-view"].indexOf(kind) >= 0) {
                            root.rememberViewAndSwitch(kind, refId)
                        }
                        if (["all", "playlist", "crate", "missing", "duplicates", "hidden", "placeholder", "lexicon-all", "lexicon-playlist", "rekordbox-all", "rekordbox-playlist", "folder-browse", "recordings-view", "history-view", "analyze-view", "smart-playlist"].indexOf(kind) >= 0) {
                            search.clear()
                        }
                        if (kind === "recordings-view") {
                            root.deferredView("recordings")
                        } else if (kind === "history-view") {
                            root.deferredView("history")
                        } else if (kind === "analyze-view") {
                            root.deferredView("analyze")
                        } else if (kind === "folder-browse" && folderPath) {
                            // After this click: switching the list rebuilds the
                            // sidebar at once, which would drop this row before
                            // it opens (see the comment at the end of this handler).
                            root.browseFolderLater(folderPath)
                        } else if (kind === "smart-playlist" && refId >= 0) {
                            tracksModel.setSmartPlaylist(refId)
                        } else if (kind === "lexicon-all" || kind === "rekordbox-all") {
                            // GlideMix: another DJ app's library (read-only).
                            tracksModel.setExternalPlaylist(kind.split("-")[0], -1)
                        } else if ((kind === "lexicon-playlist" || kind === "rekordbox-playlist") && refId >= 0) {
                            tracksModel.setExternalPlaylist(kind.split("-")[0], refId)
                        } else if (kind === "all") {
                            tracksModel.setPlaylist(-1)
                        } else if (kind === "playlist" && refId >= 0) {
                            tracksModel.setPlaylist(refId)
                        } else if (kind === "crate" && refId >= 0) {
                            tracksModel.setCrate(refId)
                        } else if (kind === "missing") {
                            // No background scanner keeps fs_deleted current
                            // in this port (see IMixHouseLibrary::
                            // verifyTrackLocations()'s own comment) -- run
                            // the real disk check right when the user asks
                            // to see this view, not just whenever it was
                            // last true.
                            tracksModel.setMissing()
                            tracksModel.verifyMissingTracks()
                        } else if (kind === "duplicates") {
                            tracksModel.setDuplicates()
                        } else if (kind === "rekordbox-choose") {
                            // No rekordbox XML chosen yet: ask for it.
                            Qt.callLater(function() {
                                var xmlFile = rekordboxXmlPicker.selectFile()
                                if (xmlFile) {
                                    root.openRekordboxNode()
                                    tracksModel.setRekordboxXmlPath(xmlFile)
                                }
                            })
                        } else if (kind === "hidden") {
                            tracksModel.setHidden()
                        } else if (kind === "placeholder") {
                            // Computer/Recordings/Analyze/Rekordbox -- none
                            // wired to a real data source in this port yet.
                            // Real mixxx still switches the track view to a
                            // genuinely empty one on activation rather than
                            // leaving whatever was showing before
                            // (browsefeature.cpp/recordingfeature.cpp/
                            // analysisfeature.cpp); see setEmptyPlaceholder()'s
                            // own comment.
                            tracksModel.setEmptyPlaceholder()
                        }
                        // Real root cause, confirmed from a real
                        // session log across two different symptoms (first a
                        // modelData TypeError, then -- even after every
                        // modelData read above was replaced with the local
                        // copies captured at the top of this function -- a
                        // SECOND, different crash: "Cannot call method
                        // 'clear' of undefined" at what was then this
                        // function's own search.clear() line). Writing
                        // sourceList.parent.expandedIds is what the
                        // sidebar's own `model: { ... sidebarNodes(parent.
                        // expandedIds) }` binding above depends on, and doing
                        // so can rebuild sourceList's model AND DESTROY this
                        // delegate synchronously, inside this very call
                        // stack. A destroyed QML Item's context chain is
                        // gone, so ANY further statement here -- not just
                        // modelData, but any other id lookup too (search,
                        // tracksModel, root, ...) -- becomes a live risk the
                        // instant that write happens. The real fix isn't
                        // "read modelData first" (already done above, and
                        // still correct/needed on its own), it's this: never
                        // run anything meaningful in this handler AFTER that
                        // write. So it now runs LAST, after every real
                        // view-switch above has already fully completed.
                        if (expandable) {
                            var e = sourceList.parent.expandedIds.slice()
                            var i = e.indexOf(nodeId)
                            if (i >= 0) { e.splice(i, 1) } else { e.push(nodeId) }
                            sourceList.keepScroll()
                            sourceList.parent.expandedIds = e
                        }
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 6 + modelData.level * 16
                        anchors.rightMargin: 6
                        spacing: 3

                        StyledTextLabel {
                            // Match the Key Wheel disclosure at a compact size.
                            Layout.preferredWidth: 16
                            Layout.preferredHeight: 16
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            text: modelData.expandable
                                  ? (sourceList.parent.expandedIds.indexOf(modelData.nodeId) >= 0
                                     ? "\u25BC" : "\u25B6")
                                  : ""
                            font.pixelSize: 14
                            color: ui.theme.accentColor
                            opacity: 1.0
                        }
                        Item {
                            Layout.preferredWidth: 16
                            Layout.preferredHeight: 16
                            Layout.minimumWidth: 16
                            Layout.maximumWidth: 16
                            Layout.alignment: Qt.AlignVCenter

                            // Keep the glyph geometry in its original 22px
                            // coordinate space and render it in a 16px slot.
                            Item {
                                anchors.centerIn: parent
                                width: 22
                                height: 22
                                scale: 16 / 22

                            Image {
                                id: featureIconSource
                                anchors.fill: parent
                                anchors.margins: 1
                                source: root.sidebarFeatureIcon(modelData.nodeId, modelData.expandable).length > 0
                                        ? "qrc:/qt/qml/Audacity/MixHouse/icons/"
                                          + root.sidebarFeatureIcon(modelData.nodeId, modelData.expandable) : ""
                                fillMode: Image.PreserveAspectFit
                                // These are the real Mixxx 1.12+ sidebar icons
                                // (title metadata inside each file), byte-
                                // identical to res/images/library/ in the
                                // parked Mixxx fork. Their own fixed orange
                                // gradient is only an INPUT here -- recolored
                                // below to this app's own theme accent, per
                                // the explicit call ("follow the app's
                                // theme accent", accepting flat single-color
                                // icons over Mixxx's real multi-tone art).
                                // layer.enabled is required for EffectColorOverlay
                                // (a ShaderEffectSource-based recolor) to be able
                                // to sample this Image as its source at all.
                                layer.enabled: true
                                // The Lexicon DJ app icon keeps its own colours.
                                visible: modelData.nodeId === "lexicon"
                                onStatusChanged: {
                                    if (status === Image.Error) {
                                        console.warn("GlideMix sidebar icon failed:", source)
                                    }
                                }
                            }
                            EffectColorOverlay {
                                anchors.fill: featureIconSource
                                source: featureIconSource
                                color: ui.theme.accentColor
                                visible: featureIconSource.status === Image.Ready && modelData.nodeId !== "lexicon"
                            }
                            // Real fallback now (not a permanent bypass): the
                            // hand-drawn silhouettes below only show for a
                            // node with no real Mixxx asset mapped in
                            // sidebarFeatureIcon() (e.g. Quick Links/Devices,
                            // individual playlists -- Mixxx has no per-item
                            // icon for those either), or if a real icon
                            // somehow fails to load.
                            readonly property bool fallbackVisible: featureIconSource.status !== Image.Ready
                            readonly property color glyphColor: ui.theme.accentColor
                            // Collection/music-note pictogram.
                            Rectangle {
                                visible: parent.fallbackVisible && modelData.nodeId === "tracks"
                                x: 12; y: 3; width: 2; height: 11; color: parent.glyphColor
                            }
                            Rectangle {
                                visible: parent.fallbackVisible && modelData.nodeId === "tracks"
                                x: 12; y: 3; width: 7; height: 2; color: parent.glyphColor
                            }
                            Rectangle {
                                visible: parent.fallbackVisible && modelData.nodeId === "tracks"
                                x: 8; y: 12; width: 6; height: 6; radius: 3; color: parent.glyphColor
                            }
                            // Auto DJ robot.
                            Rectangle {
                                visible: parent.fallbackVisible && modelData.nodeId === "autodj"
                                x: 3; y: 7; width: 17; height: 13; radius: 2; color: parent.glyphColor
                            }
                            Rectangle {
                                visible: parent.fallbackVisible && modelData.nodeId === "autodj"
                                x: 5; y: 3; width: 13; height: 2; color: parent.glyphColor
                            }
                            Rectangle {
                                visible: parent.fallbackVisible && modelData.nodeId === "autodj"
                                x: 10; y: 1; width: 2; height: 3; color: parent.glyphColor
                            }
                            Rectangle {
                                visible: parent.fallbackVisible && modelData.nodeId === "autodj"
                                x: 7; y: 11; width: 3; height: 3; color: ui.theme.backgroundPrimaryColor
                            }
                            Rectangle {
                                visible: parent.fallbackVisible && modelData.nodeId === "autodj"
                                x: 13; y: 11; width: 3; height: 3; color: ui.theme.backgroundPrimaryColor
                            }
                            // Playlist/play icon.
                            Rectangle {
                                visible: parent.fallbackVisible && modelData.nodeId === "playlists"
                                x: 2; y: 3; width: 19; height: 16; color: "transparent"
                                border.width: 2; border.color: parent.glyphColor
                            }
                            Text {
                                visible: parent.fallbackVisible && modelData.nodeId === "playlists"
                                x: 7; y: 1; width: 12; height: 20
                                text: "▶"; color: parent.glyphColor
                                font.pixelSize: 13; horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            // Crate/box diamond.
                            Rectangle {
                                visible: parent.fallbackVisible && modelData.nodeId === "crates"
                                x: 4; y: 5; width: 14; height: 14; color: "transparent"
                                border.width: 2; border.color: parent.glyphColor; rotation: 45
                            }
                            // Computer monitor.
                            Rectangle {
                                visible: parent.fallbackVisible && modelData.nodeId === "computer"
                                x: 2; y: 3; width: 19; height: 14; radius: 1
                                color: "transparent"; border.width: 2; border.color: parent.glyphColor
                            }
                            Rectangle {
                                visible: parent.fallbackVisible && modelData.nodeId === "computer"
                                x: 8; y: 18; width: 7; height: 2; color: parent.glyphColor
                            }
                            Rectangle {
                                visible: parent.fallbackVisible && modelData.nodeId === "computer"
                                x: 5; y: 20; width: 13; height: 2; color: parent.glyphColor
                            }
                            // Recording/cassette icon.
                            Rectangle {
                                visible: parent.fallbackVisible && modelData.nodeId === "recordings"
                                x: 2; y: 4; width: 19; height: 16; radius: 3; color: parent.glyphColor
                                Rectangle { x: 4; y: 6; width: 11; height: 2; color: ui.theme.backgroundPrimaryColor }
                                Rectangle { x: 5; y: 13; width: 3; height: 3; radius: 1.5; color: ui.theme.backgroundPrimaryColor }
                                Rectangle { x: 15; y: 13; width: 3; height: 3; radius: 1.5; color: ui.theme.backgroundPrimaryColor }
                            }
                            // History uses a compact curved-arrow glyph.
                            Text {
                                visible: parent.fallbackVisible && modelData.nodeId === "history"
                                anchors.centerIn: parent; text: "↶"; color: parent.glyphColor
                                font.pixelSize: 21; font.bold: true
                            }
                            // Analyze (crossed tools) and Rekordbox (file) retain
                            // the same compact visual weight as Mixxx.
                            Rectangle {
                                visible: parent.fallbackVisible && modelData.nodeId === "analyze"
                                x: 3; y: 10; width: 19; height: 2; color: parent.glyphColor; rotation: 45
                            }
                            Rectangle {
                                visible: parent.fallbackVisible && modelData.nodeId === "analyze"
                                x: 3; y: 10; width: 19; height: 2; color: parent.glyphColor; rotation: -45
                            }
                            Rectangle {
                                visible: parent.fallbackVisible && modelData.nodeId === "rekordbox"
                                x: 5; y: 2; width: 15; height: 19; color: "transparent"
                                border.width: 2; border.color: parent.glyphColor
                                Rectangle { x: 9; y: -2; width: 7; height: 5; color: ui.theme.backgroundPrimaryColor; rotation: 45 }
                            }
                            Rectangle {
                                visible: parent.fallbackVisible && ["computer-quick-links", "computer-devices"].indexOf(modelData.nodeId) >= 0
                                x: 2; y: 7; width: 20; height: 14; radius: 1
                                color: parent.glyphColor
                            }
                            Rectangle {
                                visible: parent.fallbackVisible && ["computer-quick-links", "computer-devices"].indexOf(modelData.nodeId) >= 0
                                x: 3; y: 4; width: 10; height: 5; radius: 1
                                color: parent.glyphColor
                            }
                            // Individual playlist icon: a small track-list
                            // (rows of lines, the last one shorter) beside a
                            // musical note -- the reference reference image,
                            // not invented. The note reuses "tracks"'s exact
                            // stem/flag/notehead proportions above, shifted to
                            // share the box with the lines. The Quick Links/
                            // Devices shortcut children still get no icon --
                            // real Mixxx's own sidebar shows those as plain
                            // indented text, and the reference was
                            // specifically for playlists.
                            Rectangle {
                                visible: parent.fallbackVisible && modelData.nodeId.indexOf("pl-") === 0
                                x: 1; y: 3; width: 10; height: 1.6; radius: 0.8; color: parent.glyphColor
                            }
                            Rectangle {
                                visible: parent.fallbackVisible && modelData.nodeId.indexOf("pl-") === 0
                                x: 1; y: 7; width: 10; height: 1.6; radius: 0.8; color: parent.glyphColor
                            }
                            Rectangle {
                                visible: parent.fallbackVisible && modelData.nodeId.indexOf("pl-") === 0
                                x: 1; y: 11; width: 10; height: 1.6; radius: 0.8; color: parent.glyphColor
                            }
                            Rectangle {
                                visible: parent.fallbackVisible && modelData.nodeId.indexOf("pl-") === 0
                                x: 1; y: 15; width: 10; height: 1.6; radius: 0.8; color: parent.glyphColor
                            }
                            Rectangle {
                                visible: parent.fallbackVisible && modelData.nodeId.indexOf("pl-") === 0
                                x: 1; y: 19; width: 6; height: 1.6; radius: 0.8; color: parent.glyphColor
                            }
                            Rectangle {
                                visible: parent.fallbackVisible && modelData.nodeId.indexOf("pl-") === 0
                                x: 17; y: 2; width: 1.6; height: 9; color: parent.glyphColor
                            }
                            Rectangle {
                                visible: parent.fallbackVisible && modelData.nodeId.indexOf("pl-") === 0
                                x: 17; y: 2; width: 5; height: 1.6; color: parent.glyphColor
                            }
                            Rectangle {
                                visible: parent.fallbackVisible && modelData.nodeId.indexOf("pl-") === 0
                                x: 13; y: 9; width: 5; height: 5; radius: 2.5; color: parent.glyphColor
                            }
                            }
                        }
                        StyledTextLabel {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            horizontalAlignment: Qt.AlignLeft
                            text: modelData.name
                            font.bold: modelData.level === 0
                        }
                        // The selected song is in this playlist.
                        Rectangle {
                            visible: root.containingNodes.indexOf(modelData.nodeId) >= 0
                            Layout.preferredWidth: 7
                            Layout.preferredHeight: 7
                            radius: 3.5
                            color: ui.theme.accentColor
                        }
                        // GlideMix: the rekordbox XML being read -- its
                        // progress instead of the count.
                        readonly property bool rekordboxReading: modelData.nodeId === "rekordbox"
                                                                 && tracksModel.rekordboxLoadProgress >= 0
                        Rectangle {
                            visible: parent.rekordboxReading
                            Layout.preferredWidth: 46
                            Layout.preferredHeight: 4
                            radius: 2
                            color: ui.theme.strokeColor
                            Rectangle {
                                width: parent.width * Math.max(0, tracksModel.rekordboxLoadProgress) / 100
                                height: parent.height
                                radius: 2
                                color: ui.theme.accentColor
                            }
                        }
                        StyledTextLabel {
                            horizontalAlignment: Qt.AlignRight
                            text: parent.rekordboxReading ? qsTrc("mixhouse", "Loading %1%").arg(tracksModel.rekordboxLoadProgress)
                                  : modelData.count > 0 ? modelData.count : ""
                            opacity: parent.rekordboxReading ? 0.85 : 0.5
                        }
                        FlatButton {
                            id: sidebarAddButton
                            readonly property bool isPlaylistFolder: modelData.nodeId.indexOf("pl-") === 0
                                && modelData.expandable
                            visible: ["tracks", "playlists", "smart", "crates", "computer", "computer-quick-links", "rekordbox"].indexOf(modelData.nodeId) >= 0
                                || isPlaylistFolder
                            enabled: !modelData.locked
                            Layout.preferredWidth: 22
                            Layout.preferredHeight: 22
                            Layout.minimumWidth: 22
                            minWidth: 22
                            margins: 0
                            transparent: true
                            icon: IconCode.PLUS
                            // No custom iconFont override -- every OTHER real
                            // "+" button in this app (TracksTitleBar's "Add
                            // track", ProjectGridItem, ProjectsListView) just
                            // uses FlatButton's own default iconFont
                            // (ui.theme.iconsFont) at its own natural size.
                            // Forcing this one specifically to 12px is what
                            // we saw render as an unreadable stack of bars
                            // instead of a plus sign -- icon fonts are only
                            // reliably crisp at the size they were designed
                            // for, and this app's own theme default is that
                            // size everywhere else.
                            // 2026-09-13: a bit smaller and white,
                            // matching the white waveforms. 13px (theme
                            // default 16) -- re-measured on screen, see the
                            // 12px note above.
                            iconFont: Qt.font({ family: ui.theme.iconsFont.family, pixelSize: 13 })
                            iconColor: "#FFFFFF"
                            toolTipTitle: modelData.nodeId === "crates" ? qsTrc("mixhouse", "Create New Crate…")
                                : modelData.nodeId === "computer-quick-links" ? qsTrc("mixhouse", "Add a folder to Quick Links…")
                                : modelData.nodeId === "rekordbox" ? qsTrc("mixhouse", "Import from Rekordbox XML…")
                                : modelData.nodeId === "smart" ? qsTrc("mixhouse", "New Smart Playlist…")
                                : modelData.nodeId === "playlists" || isPlaylistFolder ? qsTrc("mixhouse", "Create playlist or folder…")
                                : qsTrc("mixhouse", "Add Folder to Library…")
                            navigation.panel: navPanel
                            navigation.name: "SidebarAdd-" + modelData.nodeId
                            navigation.row: index

                            onClicked: {
                                // The button handles the gesture itself: opening
                                // an add menu must not collapse/select its row.
                                var nodeId = modelData.nodeId
                                var parentId = isPlaylistFolder ? modelData.refId : -1
                                var pos = sidebarAddButton.mapToItem(sidebarColumn, 0, height)
                                if (nodeId === "computer-quick-links") {
                                    var chosenLink = folderPicker.selectDirectory()
                                    if (chosenLink) {
                                        tracksModel.addQuickLink(chosenLink)
                                    }
                                } else if (nodeId === "smart") {
                                    smartPlaylistEditor.openFor(-1)
                                } else if (nodeId === "playlists" || isPlaylistFolder) {
                                    newPlaylistRootMenu.openFor(pos, parentId)
                                } else if (nodeId === "crates") {
                                    newListPopup.openStandalone(true)
                                } else if (nodeId === "rekordbox") {
                                    rekordboxMenu.show(pos, [
                                        { id: "import-rekordbox", title: qsTrc("mixhouse", "Import from Rekordbox XML…") },
                                        { id: "copy-rekordbox", title: qsTrc("mixhouse", "Copy Rekordbox XML into Music Collection…") }
                                    ])
                                } else {
                                    computerMenu.show(pos, [
                                        { id: "add-folder", title: qsTrc("mixhouse", "Add Folder to Library…") }
                                    ])
                                }
                            }
                        }
                    }

                    // Drag a track (or the current selection) from the table
                    // onto a playlist/crate row to add it -- rekordbox's own
                    // #1 documented playlist workflow (rekordbox manual,
                    // "Using a Playlist": drag & drop from the track list
                    // onto the playlist). Real mixxx's own equivalent chain
                    // is WLibrarySidebar::dropEvent -> SidebarModel::
                    // dropAccept -> PlaylistFeature::dropAcceptChild /
                    // CrateFeature::dropAcceptChild, which resolves the
                    // dropped QUrls back to track ids via the library
                    // (trackCollectionManager::resolveTrackIds) because
                    // mixxx's sidebar has to accept drops from ANY source
                    // (Finder, another app). This sidebar only ever expects
                    // an INTERNAL drag -- the row cell's own drag source,
                    // marked via QMimeData::setText("library") in
                    // startExternalDrag() -- so this skips that URL
                    // round-trip and uses root.draggingRows (the exact rows
                    // that drag started from) directly against the already-
                    // working addTracksToPlaylist/addTracksToCrate. An
                    // external file/Finder drop was deliberately never
                    // wired here, matching every other file-add path in
                    // this app going through addFolderToLibrary instead.
                    DropArea {
                        id: playlistDropArea
                        anchors.fill: parent
                        readonly property bool isCrateTarget: modelData.nodeId.indexOf("cr-") === 0
                        readonly property bool isPlaylistTarget: modelData.nodeId.indexOf("pl-") === 0
                        // Same isPlaylistLocked/isCrateLocked rule the right-
                        // click Rename/Delete menu already enforces (see
                        // sidebarItemMenu's own comment) -- a locked list
                        // refuses new tracks the same way it refuses being
                        // renamed or deleted.
                        readonly property bool validTarget: (isPlaylistTarget || isCrateTarget) && !modelData.locked
                                                            && !isFolderTarget
                        // A playlist folder: Finder folders dropped on it become playlists inside it.
                        readonly property bool isFolderTarget: isPlaylistTarget && modelData.expandable
                        // GlideMix: files and folders from Finder too -- onto a
                        // playlist or crate (added and listed), or onto Music
                        // Collection (added).
                        readonly property bool isCollectionTarget: modelData.nodeId === "tracks"
                        // Onto the Playlists / Crates headings: new lists, one per folder.
                        readonly property bool isNewListTarget: modelData.nodeId === "playlists" || modelData.nodeId === "crates"
                                                                || (isFolderTarget && !modelData.locked)

                        function fromLibrary(d) { return d.hasText && d.text === "library" }

                        onEntered: function(drag) {
                            const external = drag.hasUrls && !fromLibrary(drag)
                            if (external ? !(validTarget || isCollectionTarget || isNewListTarget)
                                         : (!(validTarget || isCollectionTarget) || !fromLibrary(drag))) {
                                drag.accepted = false
                            }
                        }
                        onDropped: function(drop) {
                            if (drop.hasUrls && !fromLibrary(drop)) {
                                if (isNewListTarget) {
                                    tracksModel.addDroppedUrlsAsNewLists(drop.urls, modelData.nodeId === "crates",
                                                                         isFolderTarget ? modelData.refId : -1)
                                } else if (isCollectionTarget) {
                                    tracksModel.addDroppedUrls(drop.urls, -1, -1)
                                } else if (validTarget) {
                                    tracksModel.addDroppedUrls(drop.urls, isPlaylistTarget ? modelData.refId : -1,
                                                               isCrateTarget ? modelData.refId : -1)
                                }
                                drop.accept()
                                return
                            }
                            if (isCollectionTarget && fromLibrary(drop) && root.draggingRows.length > 0) {
                                // Browsed or other-library tracks into the collection.
                                tracksModel.importRows(root.draggingRows)
                                return
                            }
                            if (!validTarget || !fromLibrary(drop) || root.draggingRows.length === 0) {
                                return
                            }
                            if (isCrateTarget) {
                                tracksModel.addTracksToCrate(root.draggingRows, modelData.refId)
                            } else {
                                tracksModel.addTracksToPlaylist(root.draggingRows, modelData.refId)
                            }
                        }
                    }

                    // Same accent-highlight convention as ListItemBlank's own
                    // SELECTED state (isSelected above) and real mixxx's
                    // WLibrarySidebar[dragHover] style -- a visibly distinct
                    // "drop here" ring while a valid drag hovers this row.
                    Rectangle {
                        anchors.fill: parent
                        visible: playlistDropArea.containsDrag && (playlistDropArea.validTarget || playlistDropArea.isCollectionTarget
                                                                    || playlistDropArea.isNewListTarget)
                        color: "transparent"
                        border.width: 2
                        border.color: ui.theme.accentColor
                        radius: 2
                    }

                    // GlideMix: drag a playlist or folder to move it, as in
                    // rekordbox -- before or after another, or into a folder.
                    readonly property bool isMovable: modelData.nodeId.indexOf("pl-") === 0 && !modelData.locked
                    readonly property bool isDropRow: sourceList.plDragId >= 0 && sourceList.plDropIndex === index
                    mouseArea.preventStealing: isMovable
                    opacity: sourceList.plDragId >= 0 && isMovable && modelData.refId === sourceList.plDragId ? 0.5 : 1.0

                    Connections {
                        target: sidebarRow.mouseArea
                        enabled: sidebarRow.isMovable

                        property point pressPos

                        function onPressed(mouse) {
                            pressPos = Qt.point(mouse.x, mouse.y)
                        }
                        function onPositionChanged(mouse) {
                            if (!sidebarRow.mouseArea.pressed) {
                                return
                            }
                            if (sourceList.plDragId < 0) {
                                if (Math.abs(mouse.x - pressPos.x) < 6 && Math.abs(mouse.y - pressPos.y) < 6) {
                                    return
                                }
                                sourceList.plDragId = modelData.refId
                            }
                            sourceList.updatePlaylistDrop(sidebarRow.mapToItem(sourceList.contentItem, mouse.x, mouse.y))
                        }
                        function onReleased(mouse) {
                            if (sourceList.plDragId >= 0) {
                                sourceList.finishPlaylistDrag()
                            }
                        }
                        function onCanceled() {
                            sourceList.plDragId = -1
                            sourceList.plDropIndex = -1
                        }
                    }

                    Rectangle {
                        visible: sidebarRow.isDropRow && sourceList.plDropMode !== "into"
                        x: 8 + modelData.level * 12
                        width: parent.width - x - 4
                        height: 2
                        y: sourceList.plDropMode === "before" ? 0 : parent.height - height
                        color: ui.theme.accentColor
                    }

                    Rectangle {
                        anchors.fill: parent
                        visible: sidebarRow.isDropRow && sourceList.plDropMode === "into"
                        color: "transparent"
                        border.width: 2
                        border.color: ui.theme.accentColor
                        radius: 2
                    }
                }
            }

            // "Add Folder to Library..." -- the real equivalent of mixxx's
            // Computer-tree right-click "Add to Library" (library/browse/
            // browsefeature.cpp) and its Preferences > Library "Add" button,
            // both of which end up at a native OS folder picker. Real mixxx
            // shows that picker via plain QFileDialog::getExistingDirectory;
            // this app already has the SAME real native picker behind
            // Muse's own IInteractive::selectDirectory(), wrapped for QML as
            // FilePickerModel -- the exact component FoldersSection.qml (the
            // real Preferences page) and ExportDialog.qml already use, not a
            // new mechanism invented for this. selectDirectory() is
            // synchronous and returns "" on cancel, so no promise/signal
            // plumbing is needed here.
            FilePickerModel {
                id: folderPicker
                title: qsTrc("mixhouse", "Choose a music folder to add")
                // GlideMix: start in the user's Music folder, not wherever the
                // app happens to run from (GlideMix's own bin folder on Windows).
                dir: tracksModel.musicFolder()
            }

            // Right-clicking the "Playlists" root -- see
            // MixHouseNamedItem::parentId's own comment for why folders
            // are original work here, not a port.
            ContextMenuLoader {
                id: newPlaylistRootMenu

                property int targetParentId: -1

                function openFor(pos, parentId) {
                    targetParentId = parentId
                    show(pos, [
                        { id: "new-playlist", title: qsTrc("mixhouse", "Create New Playlist…") },
                        { id: "new-folder", title: qsTrc("mixhouse", "Create New Folder…") }
                    ])
                }

                onHandleMenuItem: function(itemId) {
                    if (itemId === "new-playlist") {
                        newListPopup.openStandalone(false, targetParentId)
                    } else if (itemId === "new-folder") {
                        newListPopup.openStandaloneFolder(targetParentId)
                    }
                }
            }

            // GlideMix: right-click on a Quick Links / Devices folder.
            ContextMenuLoader {
                id: folderNodeMenu
                property string folderPath: ""

                onHandleMenuItem: function(itemId) {
                    if (itemId === "import-folder") {
                        tracksModel.addFolderToLibrary(folderNodeMenu.folderPath)
                    } else if (itemId === "remove-link") {
                        tracksModel.removeQuickLink(folderNodeMenu.folderPath)
                    }
                }
            }

            ContextMenuLoader {
                id: computerMenu

                onHandleMenuItem: function(itemId) {
                    if (itemId === "add-folder") {
                        var chosen = folderPicker.selectDirectory()
                        if (chosen) {
                            tracksModel.addFolderToLibrary(chosen)
                        }
                    } else if (itemId === "export-rekordbox-xml") {
                        tracksModel.exportRekordboxXml()
                    }
                }
            }

            // Same real native picker, this time for a single file -- the
            // real rekordbox collection XML (File > Export Collection in
            // xml format, in real rekordbox) -- see
            // MixHouseTrackListModel::importFromRekordboxXml's own comment
            // for why reading it back is worth doing at all.
            FilePickerModel {
                id: rekordboxXmlPicker
                title: qsTrc("mixhouse", "Choose a Rekordbox collection XML file")
                // Real filter string format (see ExportPreferencesModel::
                // fileFilter() for the same "Description (*.ext)" convention
                // this app's own native file dialog expects -- a bare
                // "*.xml" glob isn't it).
                filter: [qsTrc("mixhouse", "Rekordbox XML") + " (*.xml)"]
                dir: tracksModel.rekordboxXmlFolder()
            }

            ContextMenuLoader {
                id: externalPlaylistMenu
                property string kind: ""
                property int playlistId: -1
                property string playlistName: ""
                onHandleMenuItem: function(itemId) {
                    if (itemId === "export-folder") {
                        root.exportToFolder(kind, playlistId, playlistName)
                    }
                }
            }
            FilePickerModel {
                id: exportFolderPicker
                title: qsTrc("mixhouse", "Export the playlist into…")
                dir: tracksModel.musicFolder()
            }

            // GlideMix: smart playlists.
            ContextMenuLoader {
                id: smartRootMenu
                onHandleMenuItem: function(itemId) {
                    if (itemId === "new-smart") {
                        smartPlaylistEditor.openFor(-1)
                    } else if (itemId.indexOf("gen:") === 0) {
                        var parts = itemId.split(":")
                        tracksModel.generateSmartPlaylists(parts[1], parts[2])
                    } else if (itemId === "delete-generated") {
                        tracksModel.deleteGeneratedSmartPlaylistsWithConfirm()
                    }
                }
            }
            ContextMenuLoader {
                id: smartItemMenu
                property int playlistId: -1
                property string playlistName: ""
                onHandleMenuItem: function(itemId) {
                    if (itemId === "edit-smart") {
                        smartPlaylistEditor.openFor(playlistId)
                    } else if (itemId === "duplicate-smart") {
                        tracksModel.duplicateSmartPlaylistAsPlaylist(playlistId)
                    } else if (itemId === "export-folder") {
                        root.exportToFolder("smart-playlist", playlistId, playlistName)
                    } else if (itemId === "delete-smart") {
                        tracksModel.deleteSmartPlaylistWithConfirm(playlistId, playlistName)
                    }
                }
            }
            SmartPlaylistEditor {
                id: smartPlaylistEditor
                tracksModel: root.tracksModelRef
                colorPalette: root.trackColorPalette
                onSaved: function(playlistId) {
                    if (sidebarColumn.expandedIds.indexOf("smart") < 0) {
                        sidebarColumn.expandedIds = sidebarColumn.expandedIds.concat(["smart"])
                    }
                    sidebarColumn.sidebarRefreshToken++
                    tracksModel.setSmartPlaylist(playlistId)
                }
            }

            ContextMenuLoader {
                id: rekordboxMenu

                onHandleMenuItem: function(itemId) {
                    if (itemId === "import-rekordbox") {
                        // GlideMix: the XML shown live under Rekordbox XML,
                        // like Lexicon DJ (its row shows the reading progress).
                        var xmlFile = rekordboxXmlPicker.selectFile()
                        if (xmlFile) {
                            root.openRekordboxNode()
                            tracksModel.setRekordboxXmlPath(xmlFile)
                        }
                    } else if (itemId === "export-rekordbox-xml") {
                        tracksModel.exportRekordboxXml()
                    } else if (itemId === "copy-rekordbox") {
                        var chosen = rekordboxXmlPicker.selectFile()
                        if (chosen) {
                            tracksModel.importFromRekordboxXml(chosen)
                        }
                    }
                }
            }

            // Rename/Delete for one real playlist/crate row -- real
            // PlaylistDAO::renamePlaylist/deletePlaylist and
            // CrateStorage::onUpdatingCrate/onDeletingCrate SQL underneath,
            // via the model. Delete asks for confirmation first
            // (IInteractive::question, in C++ -- not a third custom popup).
            ContextMenuLoader {
                id: sidebarItemMenu

                property bool isCrate: false
                property int itemId: -1
                property string itemName: ""
                property bool itemLocked: false
                // Playlist folders/nesting -- see MixHouseNamedItem::
                // parentId's own comment for why this is original work, not
                // a port. Never true for a crate (crates don't nest).
                property bool itemIsFolder: false
                // Non-empty only for a batch delete opened via
                // root.selectedSidebarRefIds (see the delegate's own
                // onClicked) -- Rename/Lock stay single-item-only (a batch
                // rename/lock has no well-defined meaning here), so the menu
                // this shows for a batch is Delete-only.
                property var batchIds: []

                // Real mixxx disables Rename/Delete entirely on a locked
                // playlist/crate rather than just failing silently after
                // the fact (widget/wtrackmenu.cpp:1558-1559,1653;
                // library/trackset/baseplaylistfeature.cpp's own
                // isPlaylistLocked() guards) -- the actual enforcement lives
                // in MixHouseLibrary's own rename/delete/add/remove methods
                // (see IMixHouseLibrary::setPlaylistLocked's comment), this
                // just keeps the menu itself from offering an action that
                // would only fail.
                function openFor(pos, crate, id, name, locked, isFolder) {
                    sidebarItemMenu.isCrate = crate
                    sidebarItemMenu.itemId = id
                    sidebarItemMenu.itemName = name
                    sidebarItemMenu.itemLocked = !!locked
                    sidebarItemMenu.itemIsFolder = !!isFolder
                    sidebarItemMenu.batchIds = []
                    // Real rekordbox: right-clicking a folder ALSO offers
                    // creating a playlist/folder nested directly inside it
                    // (manual pp.52, 248), on top of the usual Rename/
                    // Delete/Lock every playlist/crate row already had.
                    var items = sidebarItemMenu.itemIsFolder ? [
                        { id: "new-playlist-in", title: qsTrc("mixhouse", "Create New Playlist…") },
                        { id: "new-folder-in", title: qsTrc("mixhouse", "Create New Folder…") },
                        {}
                    ] : []
                    if (!crate && !sidebarItemMenu.itemIsFolder) {
                        items = items.concat([
                            { id: "export-folder", title: qsTrc("mixhouse", "Export to Folder…") },
                            {}
                        ])
                    }
                    items = items.concat([
                        { id: "rename", title: qsTrc("mixhouse", "Rename"), enabled: !sidebarItemMenu.itemLocked },
                        { id: "delete", title: qsTrc("mixhouse", "Delete"), enabled: !sidebarItemMenu.itemLocked },
                        { id: "lock", title: sidebarItemMenu.itemLocked ? qsTrc("mixhouse", "Unlock")
                                                                         : qsTrc("mixhouse", "Lock") }
                    ])
                    // Manual reordering -- "default should be
                    // alphabetical order... but this ordering should be via
                    // right click" (playlists and folders only, not crates
                    // -- crates don't nest, and he asked for this
                    // specifically for "folders and playlists"; drag-to-
                    // reorder is a real, separate follow-up, not built
                    // yet). See IMixHouseLibrary::movePlaylistOrder's own
                    // comment for the real mechanism.
                    if (!crate) {
                        items = items.concat([
                            {},
                            { id: "move-up", title: qsTrc("mixhouse", "Move Up") },
                            { id: "move-down", title: qsTrc("mixhouse", "Move Down") }
                        ])
                    }
                    sidebarItemMenu.show(pos, items)
                }

                // See this loader's own batchIds comment. `ids` is the real
                // multi-selection (root.selectedSidebarRefIds), always 2+ --
                // the delegate's own onClicked never calls this for 0 or 1.
                function openForBatch(pos, crate, ids) {
                    sidebarItemMenu.isCrate = crate
                    sidebarItemMenu.batchIds = ids
                    sidebarItemMenu.show(pos, [
                        { id: "delete", title: qsTrc("mixhouse", "Delete %1 %2")
                              .arg(ids.length).arg(crate ? qsTrc("mixhouse", "Crates") : qsTrc("mixhouse", "Playlists")) }
                    ])
                }

                onHandleMenuItem: function(itemId) {
                    if (itemId === "new-playlist-in") {
                        newListPopup.openStandalone(false, sidebarItemMenu.itemId)
                    } else if (itemId === "new-folder-in") {
                        newListPopup.openStandaloneFolder(sidebarItemMenu.itemId)
                    } else if (itemId === "rename") {
                        newListPopup.openRename(sidebarItemMenu.isCrate, sidebarItemMenu.itemId,
                                                 sidebarItemMenu.itemName)
                    } else if (itemId === "delete") {
                        if (sidebarItemMenu.batchIds.length > 0) {
                            if (sidebarItemMenu.isCrate) {
                                tracksModel.deleteCratesWithConfirm(sidebarItemMenu.batchIds)
                            } else {
                                tracksModel.deletePlaylistsWithConfirm(sidebarItemMenu.batchIds)
                            }
                            root.selectedSidebarRefIds = []
                        } else if (sidebarItemMenu.isCrate) {
                            tracksModel.deleteCrateWithConfirm(sidebarItemMenu.itemId, sidebarItemMenu.itemName)
                        } else {
                            tracksModel.deletePlaylistWithConfirm(sidebarItemMenu.itemId, sidebarItemMenu.itemName,
                                                                   sidebarItemMenu.itemIsFolder)
                        }
                    } else if (itemId === "lock") {
                        var newLocked = !sidebarItemMenu.itemLocked
                        if (sidebarItemMenu.isCrate) {
                            tracksModel.setCrateLocked(sidebarItemMenu.itemId, newLocked)
                        } else {
                            tracksModel.setPlaylistLocked(sidebarItemMenu.itemId, newLocked)
                        }
                    } else if (itemId === "export-folder") {
                        root.exportToFolder("playlist", sidebarItemMenu.itemId, sidebarItemMenu.itemName)
                    } else if (itemId === "move-up") {
                        tracksModel.movePlaylistOrder(sidebarItemMenu.itemId, true)
                    } else if (itemId === "move-down") {
                        tracksModel.movePlaylistOrder(sidebarItemMenu.itemId, false)
                    }
                }
            }
        }

        // ---- tracks ----------------------------------------------------------
        ColumnLayout {
            id: tracksColumn
            SplitView.fillWidth: true
            Layout.fillHeight: true
            spacing: 6

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Item {
                    Layout.preferredWidth: 220
                    Layout.preferredHeight: search.implicitHeight

                    SearchField {
                        id: search
                        anchors.left: parent.left
                        anchors.right: parent.right
                        navigation.name: "MixHouseLibrarySearch"
                        navigation.panel: navPanel
                        navigation.row: 0
                        // Real mixxx: WSearchLineEdit's own debounce timer
                        // (widget/wsearchlineedit.cpp), driven by "Music
                        // library" Preferences page's "Search-as-you-type
                        // timeout" -- every keystroke used to re-run the full
                        // SQL query and reload the model synchronously, which
                        // can visibly flicker on a full LIKE-scan over a large
                        // library.
                        onSearchTextChanged: searchDebounceTimer.restart()

                        // Real mixxx: pressing Enter/Return commits the
                        // search into WSearchLineEdit's own remembered-
                        // searches list (real mixxx does this on focus-out
                        // too; Enter is this port's own equivalent trigger).
                        onAccepted: {
                            if (search.searchText.length > 0) {
                                tracksModel.addSearchHistoryEntry(search.searchText)
                            }
                            searchHistoryPopup.visible = false
                        }

                        // Real mixxx: Up/Down arrow keys in the search box
                        // cycle through the real search-history list, most
                        // recent first -- only when "Enable search history
                        // keyboard shortcuts" is on.
                        Keys.onUpPressed: function(event) {
                            if (!tracksModel.searchHistoryShortcutsEnabled()) return
                            searchHistoryNav.cycle(-1)
                            event.accepted = true
                        }
                        Keys.onDownPressed: function(event) {
                            if (!tracksModel.searchHistoryShortcutsEnabled()) return
                            searchHistoryNav.cycle(1)
                            event.accepted = true
                        }
                    }

                    QtObject {
                        id: searchHistoryNav
                        property int index: -1

                        function cycle(delta) {
                            var history = tracksModel.searchHistory()
                            if (history.length === 0) return
                            index = Math.max(-1, Math.min(history.length - 1, index + delta))
                            root.setSearchBox(index < 0 ? "" : history[index])
                        }
                    }

                    // Real mixxx: WSearchLineEdit's own QCompleter dropdown
                    // -- remembered searches matching what's currently
                    // typed, shown below the search box. Only when "Enable
                    // search completions" is on.
                    Popup {
                        id: searchHistoryPopup
                        y: search.height
                        width: search.width
                        padding: 0
                        visible: false
                        closePolicy: Popup.CloseOnPressOutside

                        readonly property var matches: {
                            if (!tracksModel.searchCompletionsEnabled() || search.searchText.length === 0) {
                                return []
                            }
                            var needle = search.searchText.toLowerCase()
                            return tracksModel.searchHistory().filter(function(s) {
                                return s.toLowerCase().indexOf(needle) === 0 && s !== search.searchText
                            }).slice(0, 8)
                        }

                        background: Rectangle {
                            color: ui.theme.backgroundPrimaryColor
                            border.width: 1
                            border.color: ui.theme.strokeColor
                        }

                        contentItem: Column {
                            Repeater {
                                model: searchHistoryPopup.matches
                                delegate: ListItemBlank {
                                    width: searchHistoryPopup.width
                                    height: 22
                                    onClicked: {
                                        root.setSearchBox(modelData)
                                        searchHistoryPopup.visible = false
                                    }
                                    StyledTextLabel {
                                        anchors.fill: parent
                                        anchors.leftMargin: 6
                                        verticalAlignment: Text.AlignVCenter
                                        horizontalAlignment: Qt.AlignLeft
                                        elide: Text.ElideRight
                                        text: modelData
                                    }
                                }
                            }
                        }
                    }
                }

                Timer {
                    id: searchDebounceTimer
                    interval: tracksModel.searchDebounceMs
                    repeat: false
                    onTriggered: {
                        tracksModel.setSearch(search.searchText)
                        searchHistoryPopup.visible = searchHistoryPopup.matches.length > 0
                    }
                }

                StyledTextLabel {
                    Layout.fillWidth: true
                    horizontalAlignment: Qt.AlignLeft
                    // we hit this for real: a folder scan (startup
                    // rescan, or Computer > Add Folder) used to run with
                    // zero visible feedback beyond "the app looks frozen
                    // for a while, then new tracks appear" -- see
                    // IMixHouseLibrary::startBackgroundScan's own comment.
                    // scanStatusText live-updates as tracksModel's own
                    // scanProgress channel fires (see load()'s own
                    // subscription), so this line genuinely counts up
                    // while a scan is running, not just a static "Scanning…".
                    //
                    // The idle-state "N tracks — /path/to/mixxxdb.sqlite"
                    // part of statusMessage was dropped from here -- the brief
                    // pointed out the track count duplicates the sidebar's
                    // own "Music Collection  1372" count. statusMessage()
                    // itself is kept (not deleted) because it does one OTHER
                    // real job: when the library fails to open at all,
                    // available is false and it returns library()->
                    // lastError() -- the only place that error ever reached
                    // the UI. So this row now shows: live scan progress,
                    // else that real error if the library isn't available,
                    // else nothing (the redundant case).
                    visible: tracksModel.scanning || !tracksModel.available
                    text: tracksModel.scanning ? tracksModel.scanStatusText : tracksModel.statusMessage
                    opacity: 0.6
                }

                Item {
                    Layout.fillWidth: true
                    visible: !(tracksModel.scanning || !tracksModel.available)
                }

                // GlideMix: zoom for the list, like rekordbox's Font Size and
                // Line Space (also in Preferences > Music library).
                StyledTextLabel {
                    text: "A"
                    font.pixelSize: 10
                    opacity: 0.7
                }
                DjSlider {
                    id: fontSizeSlider
                    Layout.preferredWidth: 110
                    from: 9
                    to: 20
                    stepSize: 1
                    value: tracksModel.libraryFontPointSize > 0 ? tracksModel.libraryFontPointSize : tracksModel.themeFontPointSize()
                    onMoved: tracksModel.setLibraryFontPointSize(Math.round(value))
                    ToolTip.visible: hovered
                    ToolTip.text: qsTrc("mixhouse", "Text size")
                }
                StyledTextLabel {
                    text: "A"
                    font.pixelSize: 16
                    opacity: 0.7
                }

                StyledTextLabel {
                    Layout.leftMargin: 14
                    text: "☰"
                    font.pixelSize: 10
                    opacity: 0.7
                }
                DjSlider {
                    id: rowHeightSlider
                    Layout.preferredWidth: 110
                    Layout.rightMargin: 8
                    from: 18
                    to: 60
                    stepSize: 1
                    value: tracksModel.libraryRowHeight
                    onMoved: tracksModel.setLibraryRowHeight(Math.round(value))
                    ToolTip.visible: hovered
                    ToolTip.text: qsTrc("mixhouse", "Row height")
                }
            }

            // ---- Analysis stats bar -- real mixxx's own DlgAnalysis
            // (library/analysis/dlganalysis.cpp/.ui): New/All scope,
            // Select All, and a real (not just a single blocking dialog)
            // stoppable batch run with live progress, previously entirely
            // absent here -- the brief pointed directly at real mixxx's own
            // screenshots of this exact bar and the empty strip above this
            // panel's own header where it belongs. See
            // MixHouseTrackListModel::startBatchAnalysis's own comment for
            // the two deliberate simplifications from the literal upstream
            // design (a timer-driven queue instead of a worker-thread pool;
            // operates on the visible Tracks table's own selection instead
            // of a separate hidden analysis-only list).
            RowLayout {
                id: analysisRow
                Layout.fillWidth: true
                spacing: 8

                // Real mixxx: radioButtonRecentlyAdded/radioButtonAllSongs,
                // a QButtonGroup (dlganalysis.cpp) -- reimplemented here as
                // two accent-highlighted FlatButtons (a plain QtQuick
                // RadioButton's own bullet-and-ring look doesn't match this
                // panel's flat Audacity styling anywhere else) rather than
                // real mixxx's own native radio widgets.
                // GlideMix: All by default. With New, a library already analysed
                // gave Select All nothing to select, and it looked broken.
                property bool analysisScopeIsNew: false

                FlatButton {
                    text: qsTrc("mixhouse", "New")
                    accentButton: analysisRow.analysisScopeIsNew
                    enabled: !tracksModel.analyzing
                    onClicked: analysisRow.analysisScopeIsNew = true
                }
                FlatButton {
                    text: qsTrc("mixhouse", "All")
                    accentButton: !analysisRow.analysisScopeIsNew
                    enabled: !tracksModel.analyzing
                    onClicked: analysisRow.analysisScopeIsNew = false
                }

                FlatButton {
                    text: qsTrc("mixhouse", "Select All")
                    enabled: !tracksModel.analyzing
                    onClicked: {
                        var rows = tracksModel.rowsForAnalysisScope(analysisRow.analysisScopeIsNew)
                        if (rows.length === 0 && analysisRow.analysisScopeIsNew) {
                            // No new tracks: select them all instead of nothing.
                            analysisRow.analysisScopeIsNew = false
                            rows = tracksModel.rowsForAnalysisScope(false)
                        }
                        root.selectedRows = rows
                        root.currentRow = rows.length > 0 ? rows[0] : -1
                    }
                }

                // GlideMix: Duplicates -- select every copy but the best of
                // each song, ready for Remove from Library / Delete from Disk.
                FlatButton {
                    visible: tracksModel.duplicatesView
                    text: qsTrc("mixhouse", "Select extra copies")
                    toolTipTitle: qsTrc("mixhouse", "Select every copy except the best one of each song")
                    toolTipDescription: qsTrc("mixhouse", "The best copy is listed first: lossless before MP3, then the higher bitrate. Then right-click > Remove from Library or Delete from Disk.")
                    enabled: tracksModel.duplicateGroups > 0
                    onClicked: {
                        var rows = tracksModel.duplicateExtraRows()
                        root.selectedRows = rows
                        root.currentRow = rows.length > 0 ? rows[0] : -1
                    }
                }

                StyledTextLabel {
                    visible: tracksModel.duplicatesView
                    text: tracksModel.duplicateGroups > 0
                          ? (tracksModel.duplicateGroups === 1 ? qsTrc("mixhouse", "1 song is in your collection more than once")
                             : qsTrc("mixhouse", "%1 songs are in your collection more than once").arg(tracksModel.duplicateGroups))
                          : qsTrc("mixhouse", "No duplicates found")
                }

                FlatButton {
                    // Real mixxx: pushButtonAnalyze, toggling text/role
                    // between "Analyze" and "Stop Analysis"
                    // (dlganalysis.cpp's slotAnalysisActive) -- enabled only
                    // with a selection or while already running, same as
                    // pushButtonAnalyze->setEnabled(tracksSelected ||
                    // m_bAnalysisActive) there.
                    text: tracksModel.analyzing ? qsTrc("mixhouse", "Stop Analysis") : qsTrc("mixhouse", "Analyze")
                    accentButton: tracksModel.analyzing
                    enabled: tracksModel.analyzing || root.selectedRows.length > 0
                    onClicked: {
                        if (tracksModel.analyzing) {
                            tracksModel.stopBatchAnalysis()
                        } else {
                            tracksModel.startBatchAnalysis(root.selectedRows)
                            // GlideMix: drop the selection once analysis starts,
                            // so a later drag can't carry every track onto
                            // the timeline by mistake.
                            root.clearRowSelection()
                        }
                    }
                }

                StyledTextLabel {
                    Layout.fillWidth: true
                    horizontalAlignment: Qt.AlignLeft
                    visible: tracksModel.analyzing || tracksModel.analysisMessage.length > 0
                    // Real mixxx: DlgAnalysis::onTrackAnalysisSchedulerProgress's
                    // own "Analyzing %1% %2/%3" wording -- see
                    // MixHouseTrackListModel::startBatchAnalysis's own
                    // comment for why the percentage here is coarser
                    // (whole-track, not real mixxx's own sub-track decode
                    // progress).
                    text: !tracksModel.analyzing ? tracksModel.analysisMessage
                          : tracksModel.analyzeTotal > 0
                          ? qsTrc("mixhouse", "Analyzing %1% %2/%3")
                                .arg(Math.round(100 * tracksModel.analyzeProcessed / tracksModel.analyzeTotal))
                                .arg(tracksModel.analyzeProcessed).arg(tracksModel.analyzeTotal)
                          : ""
                }
            }

            // ---- header: click to sort, drag the right edge to resize, right-click
            // to show/hide columns -- ported from mixxx's OWN upstream QML library
            // port (res/qml/Library/TrackList.qml's HorizontalHeaderView+TableView
            // pair, fetched from github.com/mixxxdj/mixxx main), not hand-rolled.
            // HorizontalHeaderView's `syncView` makes it mirror trackTableView's own
            // columnWidthProvider exactly, so header and rows share ONE width
            // authority (root.columnWidthFor) instead of two independent x/width
            // calculations that could -- and did -- drift apart.
            // Wrapped in a plain Item so a full-width right-click target can sit
            // BEHIND HorizontalHeaderView's own delegates: those only exist where
            // an actual column is, so right-clicking the empty header space past
            // the last visible column (e.g. after "Color") previously hit nothing
            // -- the brief pointed at exactly that empty strip. A column's own
            // MouseArea (headerMouse below) still gets first say over its own
            // bounds -- this background only ever sees clicks nothing else claimed.
            Item {
                id: headerRowContainer
                Layout.fillWidth: true
                // Mixxx's QHeaderView is one compact text row (24px with the
                // default 11px font and its native vertical padding).
                Layout.preferredHeight: 24

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.RightButton
                    onClicked: function(mouse) {
                        columnMenu.open(headerRowContainer.mapToItem(tracksColumn, mouse.x, mouse.y))
                    }
                }

                HorizontalHeaderView {
                    id: headerRow
                    anchors.fill: parent
                    syncView: trackTableView
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    interactive: false
                    reuseItems: false // same staleness reasoning as trackTableView below; 23 header cells is nothing to recycle for
    
                    // Reorder state, read-only from each delegate (only this Item's own
                    // plain properties change -- nothing here ever assigns a delegate's
                    // x/width directly, which is what broke resize: HorizontalHeaderView
                    // owns that. The drop indicator is purely a dimmed-opacity read).
                    property int dragIndex: -1
                    property int dropTarget: -1
                    property real dragWidth: 0
                    property real dragCursorX: 0
                    property real dragGrabOffset: 0
                    // Mixxx paints the grabbed section as a floating image
                    // under the pointer while the neighbouring sections slide
                    // around its empty slot. Keep that state on the header so
                    // it survives HorizontalHeaderView layout updates.
                    property bool dragProxyVisible: false
                    property string dragProxyLabel: ""
    
                    // Real mixxx's own native QHeaderView::setSectionsMovable(true)
                    // drag shows the OS's own moving section image plus a drop-
                    // target indicator line as you drag -- our own dimmed-opacity
                    // dragIndex cell (below) had no equivalent for WHERE it would
                    // land, the one piece of real feedback missing (the brief pointed
                    // directly at real mixxx's own header-drag screenshots: no
                    // hint here that a column is being reordered at all). This
                    // accent line at dropTarget's left edge is that missing cue.
                    function dropIndicatorX() {
                        if (headerRow.dropTarget < 0) {
                            return -1
                        }
                        var acc = 0
                        for (var i = 0; i < headerRow.dropTarget; ++i) {
                            if (!root.columns[i].visible) continue
                            acc += root.columnWidthFor(i)
                        }
                        // `acc` is a content coordinate, while the indicator
                        // is parented to the fixed header viewport. Mixtape
                        // Studio must remove the shared table/header scroll
                        // offset or the line points at an earlier column as
                        // soon as the library is scrolled horizontally.
                        return acc - headerRow.contentX
                    }

                    // Mouse positions mapped to headerRow are viewport
                    // coordinates. Column bounds are content coordinates.
                    // Keeping this conversion in one shared hit-test makes
                    // reordering work identically for every library column,
                    // regardless of the current horizontal scroll position.
                    function columnAtViewportX(viewportX) {
                        var contentX = viewportX + headerRow.contentX
                        var acc = 0
                        for (var i = 0; i < root.columns.length; ++i) {
                            if (!root.columns[i].visible) continue
                            var width = root.columnWidthFor(i)
                            if (contentX >= acc && contentX < acc + width) {
                                return i
                            }
                            acc += width
                        }
                        return -1
                    }

                    delegate: Item {
                        id: headerCell
                        required property int index
                        readonly property var col: root.columns[index]
                        implicitHeight: headerRow.height
                        // The original section becomes an empty placeholder;
                        // the grabbed section is painted by dragProxy below.
                        opacity: headerRow.dragIndex === index ? 0.0 : 1.0

                        // QHeaderView moves the dragged section with the
                        // pointer and smoothly slides the intervening sections
                        // out of its way. HorizontalHeaderView has no movable
                        // section API, so reproduce that visual contract with
                        // a translated delegate: the dragged header follows the
                        // cursor while the columns between source and target
                        // shift by exactly the dragged section's width.
                        property real reorderOffset: {
                            if (headerRow.dragIndex < 0) {
                                return 0
                            }
                            if (headerCell.index === headerRow.dragIndex) {
                                return headerRow.dragCursorX
                                       - headerRow.dragGrabOffset
                                       - headerCell.x
                            }
                            if (headerRow.dropTarget < 0) {
                                return 0
                            }
                            if (headerRow.dragIndex < headerRow.dropTarget
                                && headerCell.index > headerRow.dragIndex
                                && headerCell.index <= headerRow.dropTarget) {
                                return -headerRow.dragWidth
                            }
                            if (headerRow.dragIndex > headerRow.dropTarget
                                && headerCell.index >= headerRow.dropTarget
                                && headerCell.index < headerRow.dragIndex) {
                                return headerRow.dragWidth
                            }
                            return 0
                        }

                        transform: Translate { x: headerCell.reorderOffset }
                        Behavior on reorderOffset {
                            NumberAnimation {
                                duration: 120
                                easing.type: Easing.OutCubic
                            }
                        }

                        StyledTextLabel {
                            anchors.fill: parent
                            anchors.leftMargin: 15
                            anchors.rightMargin: 8
                            // Header labels are always left-aligned regardless of
                            // the column's own data alignment (numeric columns
                            // like BPM/Duration right-align their VALUES, per
                            // spreadsheet convention, but the column NAME itself
                            // reads better flush left across the whole row --
                            // "all text should be aligned to left").
                            horizontalAlignment: Qt.AlignLeft
                            text: headerCell.col
                                  ? (headerCell.col.headerLabel !== undefined ? headerCell.col.headerLabel : headerCell.col.label)
                                    + (tracksModel.sortKey === headerCell.col.sort && headerCell.col.sort >= 0
                                       ? (tracksModel.sortAscending ? " ▲" : " ▼") : "")
                                  : ""
                            font.bold: headerCell.col && tracksModel.sortKey === headerCell.col.sort
                                       && headerCell.col.sort >= 0
                        }

                        // Compact six-dot grip, matching the draggable widget
                        // affordance used throughout Audacity while following
                        // the active MixHouse accent color. It is visual only;
                        // the MouseArea below still owns the whole header cell
                        // so a drag can start anywhere in the section.
                        Grid {
                            id: headerGrip
                            anchors.left: parent.left
                            anchors.leftMargin: 4
                            anchors.verticalCenter: parent.verticalCenter
                            columns: 2
                            rows: 3
                            columnSpacing: 2
                            rowSpacing: 2
                            width: 6
                            height: 10
                            Repeater {
                                model: 6
                                delegate: Rectangle {
                                    width: 2
                                    height: 2
                                    radius: 1
                                    color: ui.theme.accentColor
                                    opacity: 0.9
                                }
                            }
                        }
    
                        // Click to sort, right-click to show/hide, drag to reorder --
                        // mixxx's real columnMouseHandler plus our own right-click menu,
                        // plus a QML stand-in for mixxx's native
                        // QHeaderView::setSectionsMovable(true) (Qt's own movable-section
                        // dragging is C++-internal, not portable source, so this
                        // reproduces the same user-facing behaviour). Deliberately NOT
                        // using MouseArea.drag.target here -- that's what broke resize:
                        // it fights HorizontalHeaderView's own authoritative positioning
                        // of headerCell. This only READS positions and writes to
                        // root.columns (the order), never to headerCell.x/width.
                        MouseArea {
                            id: headerMouse
                            anchors.fill: parent
                            anchors.rightMargin: 6 // leave room for the resize handle
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            // HorizontalHeaderView is a Flickable subclass --
                            // without this, Flickable's own internal pan/flick
                            // gesture recognizer grabs the mouse away from this
                            // MouseArea partway through a real drag (interactive:
                            // false above stops the user from actually panning
                            // the header, but doesn't stop that internal grab-
                            // steal), so onPositionChanged stops updating just
                            // past the press point and `moved` never reliably
                            // reaches true -- no dimming, no drop-indicator line,
                            // no reorder on release. This is the exact fix
                            // columnResizeHandler below already needed for its
                            // own drag to work; this MouseArea just never got it.
                            preventStealing: true

                            property real pressX: 0
                            property bool moved: false
    
                            onPressed: function(mouse) {
                                pressX = mapToItem(headerRow, mouse.x, 0).x
                                moved = false
                            }
                            onPositionChanged: function(mouse) {
                                if (!(mouse.buttons & Qt.LeftButton)) return
                                var curX = mapToItem(headerRow, mouse.x, 0).x
                                if (!moved && Math.abs(curX - pressX) > 6) {
                                    moved = true
                                    headerRow.dragWidth = root.columnWidthFor(headerCell.index)
                                    headerRow.dragGrabOffset = pressX
                                        - headerCell.mapToItem(headerRow, 0, 0).x
                                    headerRow.dragCursorX = curX
                                    headerRow.dragIndex = headerCell.index
                                    headerRow.dragProxyLabel = headerCell.col
                                        ? (headerCell.col.headerLabel !== undefined ? headerCell.col.headerLabel : headerCell.col.label)
                                          + (tracksModel.sortKey === headerCell.col.sort && headerCell.col.sort >= 0
                                             ? (tracksModel.sortAscending ? " ▲" : " ▼") : "")
                                        : ""
                                    headerRow.dragProxyVisible = true
                                }
                                if (!moved) return
                                headerRow.dragCursorX = curX
                                headerRow.dropTarget = headerRow.columnAtViewportX(curX)
                            }
                            onReleased: function(mouse) {
                                if (mouse.button === Qt.RightButton) {
                                    // Position is relative to ContextMenuLoader's OWN
                                    // parent (tracksColumn), not this cell -- passing the
                                    // raw (mouse.x, mouse.y) here was the bug: it opened
                                    // the menu at that offset from tracksColumn's top-left
                                    // every time, nowhere near the actual click.
                                    columnMenu.open(headerCell.mapToItem(tracksColumn, mouse.x, mouse.y))
                                } else if (moved) {
                                    if (headerRow.dropTarget >= 0 && headerRow.dropTarget !== headerCell.index) {
                                        root.moveColumn(headerCell.index, headerRow.dropTarget)
                                    }
                                    headerRow.dragProxyVisible = false
                                } else if (headerCell.col && headerCell.col.sort >= 0) {
                                    tracksModel.sortBy(headerCell.col.sort)
                                }
                                headerRow.dragIndex = -1
                                headerRow.dropTarget = -1
                                headerRow.dragWidth = 0
                                headerRow.dragCursorX = 0
                                headerRow.dragGrabOffset = 0
                                headerRow.dragProxyLabel = ""
                                moved = false
                            }
                        }
    
                        // Resize handle. Mixxx's own version relies on a real OS cursor
                        // landing on an exact 1px QHeaderView boundary -- fine with a
                        // physical mouse, needlessly hard to grab as a QML MouseArea, so
                        // the hit target here is much wider than the visible line
                        // ("the area must be bigger...magnetic"). Every pixel of
                        // drag writes straight into the column model via
                        // root.setColumnWidth (which itself calls forceLayout()), so the
                        // WHOLE table resizes live with the cursor instead of jumping
                        // once at release -- still driven by headerCell.width, still
                        // never touching headerCell.x, so it doesn't reintroduce the
                        // drag.target fight that broke resize the first time.
                        Rectangle {
                            id: columnResizer
                            readonly property bool active: columnResizeHandler.containsMouse || columnResizeHandler.drag.active
                            width: active ? 2 : 1
                            color: active ? ui.theme.accentColor : ui.theme.strokeColor
                            // Was 0.6 at rest -- "barely visible". Full
                            // strength now; the hover/drag brighten to accentColor
                            // above still does the job of showing which one is
                            // interactive.
                            opacity: 1.0
    
                            anchors {
                                bottom: parent.bottom
                                right: parent.right
                                top: parent.top
                            }
                            Behavior on width { NumberAnimation { duration: 80 } }
                            Behavior on color { ColorAnimation { duration: 80 } }
                            Behavior on opacity { NumberAnimation { duration: 80 } }
    
                            MouseArea {
                                id: columnResizeHandler
                                property int sizeOffset: 0
    
                                anchors.fill: parent
                                anchors.margins: -7 // much wider hit target than the visible line
                                cursorShape: Qt.SizeHorCursor
                                hoverEnabled: true
                                preventStealing: true
    
                                // Reverted from a "live write on every tick" version:
                                // once drag.target detaches columnResizer's anchor (see
                                // below), forceLayout()-ing the model on every single
                                // tick reassigns headerCell.width independently of where
                                // the now-untethered columnResizer actually is -- two
                                // systems fighting over position with nothing keeping
                                // them in sync, which compounded into wildly wrong
                                // widths (one column ballooning, others starved) over a
                                // real drag. Mixxx's own version only ever accumulates
                                // locally during the drag and commits ONCE at release --
                                // matching that exactly is what's actually stable.
                                onMouseXChanged: {
                                    if (drag.active) {
                                        headerCell.width += mouseX
                                        sizeOffset += mouseX
                                    }
                                }
    
                                // drag.target is the thin resizer RECTANGLE (mixxx's own
                                // `parent`), not headerCell -- headerCell's x/width are
                                // authoritatively driven every layout pass by
                                // HorizontalHeaderView itself (via columnWidthProvider),
                                // so making the drag system ALSO assign headerCell.x
                                // directly fights that and the drag has no visible
                                // effect. Dragging the small anchored rectangle instead
                                // avoids that fight entirely.
                                drag {
                                    axis: Drag.XAxis
                                    target: columnResizer
                                    threshold: 2
    
                                    onActiveChanged: {
                                        if (!drag.active && columnResizeHandler.sizeOffset !== 0) {
                                            // setColumnWidth() defers its own forceLayout() --
                                            // no need (and unsafe, see root.setColumnWidth's own
                                            // comment) to also force one synchronously here.
                                            root.setColumnWidth(headerCell.index, headerCell.width)
                                            columnResizeHandler.sizeOffset = 0
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // Mixxx's native moving-section pixmap: a solid, slightly
                // elevated header that follows the pointer at the exact grab
                // offset. The source delegate is hidden while this is visible,
                // and the other delegates keep their animated reorderOffset.
                Item {
                    id: dragProxy
                    z: 20
                    visible: headerRow.dragProxyVisible
                    x: headerRow.dragCursorX - headerRow.dragGrabOffset
                    y: 0
                    width: headerRow.dragWidth
                    height: headerRow.height

                    Rectangle {
                        anchors.fill: parent
                        color: ui.theme.backgroundPrimaryColor
                        opacity: 0.96
                        border.width: 2
                        border.color: ui.theme.accentColor
                    }
                    Rectangle {
                        anchors.fill: parent
                        color: ui.theme.accentColor
                        opacity: 0.10
                        border.width: 0
                    }
                    StyledTextLabel {
                        anchors.fill: parent
                        anchors.leftMargin: 15
                        anchors.rightMargin: 8
                        horizontalAlignment: Qt.AlignLeft
                        text: headerRow.dragProxyLabel
                        font.bold: false
                    }
                    Grid {
                        anchors.left: parent.left
                        anchors.leftMargin: 4
                        anchors.verticalCenter: parent.verticalCenter
                        columns: 2
                        rows: 3
                        columnSpacing: 2
                        rowSpacing: 2
                        width: 6
                        height: 10
                        Repeater {
                            model: 6
                            delegate: Rectangle {
                                width: 2
                                height: 2
                                radius: 1
                                color: ui.theme.accentColor
                                opacity: 0.9
                            }
                        }
                    }
                }

                // The drop-target insertion line -- see headerRow's own
                // dropIndicatorX()/dragIndex/dropTarget comments above.
                // Sits ABOVE HorizontalHeaderView's own delegates (declared
                // after it, so later in z-order) so it's never hidden
                // behind a header cell.
                Rectangle {
                    width: 2
                    height: headerRowContainer.height
                    color: ui.theme.accentColor
                    visible: headerRow.dragIndex >= 0 && headerRow.dropTarget >= 0
                    x: headerRow.dropIndicatorX()
                }
            }

            ContextMenuLoader {
                id: columnMenu

                function open(pos) {
                    var items = []
                    for (var i = 0; i < root.columns.length; ++i) {
                        items.push({ id: String(i), title: root.columns[i].label,
                                     checkable: true, checked: root.columns[i].visible })
                    }
                    columnMenu.show(pos, items)
                }

                onHandleMenuItem: function(itemId) {
                    root.toggleColumn(parseInt(itemId))
                }
            }

            SeparatorLine {}

            // Real TableView, not a ListView + hand-rolled per-cell x/width Repeater --
            // ported alongside the header from mixxx's res/qml/Library/TrackList.qml.
            // columnWidthProvider is the SAME root.columnWidthFor() the header syncs
            // to, so a cell can never land under the wrong header the way the old
            // duplicated positioning math did.
            TableView {
                id: trackTableView

                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                // OFF: with the column set now this large (23 columns, most
                // hidden and toggled on/off live), reuseItems showed a real
                // staleness bug -- a recycled delegate briefly kept a PREVIOUS
                // cell's text (e.g. an Artist value rendering under the Played
                // header on a couple of rows) after a column toggle/reorder.
                // 1372 rows, ~20 visible at once either way -- the correctness
                // is worth more than the recycling saved.
                reuseItems: false

                model: tracksModel
                columnWidthProvider: function(column) { return root.columnWidthFor(column) }
                // "Music library" Preferences page's own "Library Row
                // Height" (LibraryPreferencesPage.qml). Live, not just on
                // next launch: rowHeightProvider is a plain JS function, not
                // a declarative binding, so TableView never re-invokes it on
                // its own just because tracksModel.libraryRowHeight changed
                // elsewhere -- the forceLayout() below (on
                // libraryRowHeightChanged) is what actually makes it live.
                rowHeightProvider: function(row) { return tracksModel.libraryRowHeight }

                Connections {
                    target: tracksModel
                    function onLibraryRowHeightChanged() {
                        trackTableView.forceLayout()
                    }
                }

                // Audacity's own themed scrollbar (StyledScrollBar -- the same
                // component StyledTableView.qml uses for both axes), not the
                // plain Qt Quick Controls default. Horizontal matters now that
                // the real column set can genuinely outgrow the panel width.
                ScrollBar.vertical: StyledScrollBar {}
                ScrollBar.horizontal: StyledScrollBar {}

                delegate: ListItemBlank {
                    id: cell

                    required property int row
                    required property int column
                    required property var model
                    readonly property var col: root.columns[column]

                    implicitHeight: 24
                    // Selected rows get ListItemBlank's own accent highlight
                    // (its SELECTED/SELECTED_HOVERED states, ui.theme.accentColor)
                    // -- the same accent convention this file's hover accents
                    // (column resizer, SplitView handle) already use, reused here
                    // rather than invented fresh. That highlight is already
                    // visually distinct from a plain hover: ListItemBlank renders
                    // HOVERED (hoverHitColor, no accent) and SELECTED (accentColor)
                    // as two different states, so "selected" and "just hovered"
                    // never look the same.
                    isSelected: root.isRowSelected(row)
                    // No per-cell hover highlight: real mixxx's track table
                    // (a plain QTableView) has none. ListItemBlank's own
                    // HOVERED state lit ONE cell at a time and stuck on
                    // whenever a menu, dialog or drag took the mouse without
                    // a leave event -- the stray gray box kept appearing
                    // (measured 2026-09-13: exactly one 70x26pt cell,
                    // buttonColor at hover opacity).
                    hoverHitColor: "transparent"

                    mouseArea.acceptedButtons: Qt.LeftButton | Qt.RightButton

                    // Drag a track (or the current multi-selection) out of the
                    // library -- real mixxx: WTrackTableView::mouseMoveEvent()
                    // -> DragAndDropHelper::dragTrackLocations() (see
                    // MixHouseTrackListModel::startExternalDrag's own
                    // comment), triggered once the press has moved past the
                    // platform's normal drag-start distance. trackTableView
                    // is a TableView (a Flickable) -- exactly the same
                    // grab-stealing risk headerMouse above hit and was fixed
                    // for (see its own comment): without preventStealing,
                    // the Flickable's pan/flick recognizer steals the mouse
                    // grab a few pixels into a real drag, onPositionChanged
                    // stops updating, and the gesture below never reaches
                    // drag distance.
                    mouseArea.preventStealing: true

                    // Guards against onPositionChanged re-firing while
                    // tracksModel.startExternalDrag() is still running --
                    // that call is a blocking QDrag::exec() (same as real
                    // mixxx's own dragUrls()), so by the time it returns the
                    // drag has already been dropped or cancelled and this
                    // just needs resetting for the next gesture.
                    property bool dragStarted: false

                    mouseArea.onPositionChanged: function(mouse) {
                        if (cell.dragStarted || !(mouse.buttons & Qt.LeftButton)) {
                            return
                        }
                        var dx = mouseArea.mouseX - mouseArea.pressX
                        var dy = mouseArea.mouseY - mouseArea.pressY
                        var threshold = Qt.styleHints.startDragDistance
                        if (dx * dx + dy * dy < threshold * threshold) {
                            return
                        }
                        cell.dragStarted = true
                        // Drag the whole current selection only if the row
                        // this gesture started on is already part of it
                        // (mirrors real Qt item views: pressing an
                        // already-selected row defers collapsing the
                        // selection until release, specifically so a drag
                        // can still grab the whole block) -- otherwise just
                        // this one row, whatever else was selected before.
                        var rows = (root.isRowSelected(cell.row) && root.selectedRows.length > 1)
                                   ? root.selectedRows : [cell.row]
                        // Snapshot for the sidebar's own DropArea (see its
                        // own comment): startExternalDrag() below is a
                        // blocking QDrag::exec() that pumps its own nested
                        // event loop while dragging, so a DropArea's
                        // onDropped can and does fire DURING this call --
                        // root.draggingRows has to already be set before
                        // that happens, not after this call returns.
                        root.draggingRows = rows
                        // NOT called directly from this handler: the drag's
                        // nested event loop delivers the drop (e.g. onto the
                        // timeline, which imports and re-lays-out the panels)
                        // and the TableView can destroy THIS cell meanwhile --
                        // Qt aborts when an object dies inside its own running
                        // signal handler (crash 2026-09-13 15:30:01, this line).
                        // callLater runs it from the panel on the next event
                        // loop turn, with no cell handler on the stack.
                        Qt.callLater(root.startLibraryDrag, rows)
                    }
                    // Re-arm for the next gesture; the OS drag swallows the
                    // release, so this can't wait for onReleased.
                    mouseArea.onPressed: cell.dragStarted = false

                    // Plain click: select just this row (new shift-anchor).
                    // Shift+click: extend the CONTIGUOUS range from the last
                    // plain/cmd-clicked anchor, replacing the prior selection.
                    // Cmd+click (Qt.ControlModifier -- Qt's documented Mac
                    // Ctrl/Cmd swap maps the physical Cmd key to this bit, the
                    // same modifier every cross-platform Qt app checks for "the
                    // platform's discontiguous multi-select key"; no
                    // AA_MacDontSwapCtrlAndMeta override exists anywhere in this
                    // app, so the swap is in full effect): toggle just this row,
                    // becomes the new anchor. Right-click matches real mixxx's
                    // own contextMenuEvent/showTrackMenu (see the Mixxx research
                    // notes): it trusts whatever selection already exists,
                    // collapsing to just the clicked row ONLY when that row isn't
                    // already part of it, so the menu's batch "Collection
                    // Actions" always act on "the current selection" as a whole.
                    // Double-click is the quick gesture mixxx itself uses to load
                    // a track without the menu at all.
                    onClicked: function(mouse) {
                        // GlideMix: a plain click on the artwork is the preview
                        // button, as in rekordbox (manual: "Click the artwork
                        // ... Move the mouse cursor away from the artwork to
                        // stop the preview"): play, then each click skips
                        // ahead -- the next phrase, or 16 bars (rekordbox skips
                        // 30 s). It leaves the selection alone, because a
                        // selected track keeps previewing after the mouse
                        // leaves; its stop button sits left of the waveform.
                        if (mouse && mouse.button === Qt.LeftButton
                                && !(mouse.modifiers & (Qt.ShiftModifier | Qt.ControlModifier))
                                && cell.col && cell.col.key === "coverArt" && cell.model) {
                            if (coverPreviewButton.playingThis) {
                                tracksModel.previewNextPhrase(cell.row)
                            } else {
                                tracksModel.previewTrackAt(cell.row, 0)
                            }
                            return
                        }
                        if (mouse && mouse.button === Qt.RightButton) {
                            if (!root.isRowSelected(cell.row)) {
                                root.selectRow(cell.row)
                            }
                            trackMenu.open(cell.mapToItem(tracksColumn, mouse.x, mouse.y),
                                           cell.row, cell.model)
                            return
                        }
                        if (mouse && (mouse.modifiers & Qt.ShiftModifier)) {
                            if (root.currentRow < 0) {
                                root.selectRow(cell.row)
                            } else {
                                root.selectRange(root.currentRow, cell.row)
                            }
                        } else if (mouse && (mouse.modifiers & Qt.ControlModifier)) {
                            root.toggleRowSelection(cell.row)
                        } else {
                            // "Music library" Preferences page's own "Edit
                            // metadata after clicking selected track" --
                            // real mixxx starts editing on this SECOND
                            // click (the row was already the selection
                            // before this click) instead of requiring a
                            // double-click, for an editable column only.
                            if (tracksModel.editMetadataAfterClick() && root.isRowSelected(cell.row)
                                && cell.col && root.isEditableColumn(cell.col.key)) {
                                root.beginEdit(cell.row, cell.col.key)
                            } else {
                                root.selectRow(cell.row)
                            }
                            // While this track previews, clicking its waveform
                            // moves the preview there (no buttons on the
                            // waveform: the controls live on the artwork).
                            if (cell.col && cell.col.key === "overview" && mouse && cell.model
                                    && tracksModel.previewPlaying && tracksModel.previewTrackId === cell.model.trackId) {
                                var p = cell.mapToItem(overview, mouse.x, mouse.y)
                                if (p.x < 0) {
                                    tracksModel.stopPreview()   // the stop button in the gutter
                                } else {
                                    tracksModel.previewTrackAt(cell.row, Math.min(1, p.x / Math.max(1, overview.width)))
                                }
                            }
                        }
                    }
                    onDoubleClicked: function(mouse) {
                        // GlideMix: the rows take the right button too (their
                        // menu); a quick double right-click must not load the
                        // song into the mix as a new track.
                        if (mouse && mouse.button !== Qt.LeftButton) {
                            return
                        }
                        // Real mixxx: double-clicking an editable cell starts
                        // editing it in place; double-clicking anywhere else
                        // runs whatever "Music library" Preferences page's
                        // own "Track Double-Click Action" is set to
                        // (WTrackTableView's own double-click dispatch --
                        // see MixHouseTrackListModel::handleTrackDoubleClicked's
                        // own comment), not always load-to-project.
                        if (cell.col && root.isEditableColumn(cell.col.key)) {
                            root.beginEdit(cell.row, cell.col.key)
                        } else {
                            tracksModel.handleTrackDoubleClicked(cell.row)
                        }
                    }

                    // Cover Art -- Phase 8. Real embedded-picture decode (TagLib), via
                    // image://mixhousecovers/<trackId> (mixhousecoverart.h) which
                    // caches the decoded QImage per track id; cache: true additionally
                    // puts it through Qt's own pixmap cache, mirroring real mixxx's
                    // two-layer CoverArtCache/QPixmapCache split. Blank cell -- no
                    // source set, no broken-image icon -- for CoverInfoRelative::
                    // Type::NONE (0) tracks.
                    //
                    // asynchronous MUST stay false (Qt's own default -- do not set it
                    // true): with it true, Qt Quick services requestImage() from a
                    // background thread pool, and MixHouseCoverArtImageProvider calls
                    // straight into MixHouseLibrary's QSqlQuery/QSqlDatabase, which are
                    // only safe on the thread that opened them. That cross-thread SQL
                    // access is exactly what crashed the app four times in the field
                    // (every real crash report showed requestImage -> coverInfoForTrack/
                    // extractEmbeddedCoverArt on a non-main thread, EXC_BAD_ACCESS from
                    // corrupted SQLite state) -- root-caused from the actual .ips crash
                    // reports, not guessed. Loading synchronously on the GUI thread, the
                    // same thread every other DB access in this app already uses, is the
                    // fix; a real background-safe redesign (a dedicated QSqlDatabase
                    // connection for the image-loading thread, or pre-resolving
                    // CoverInfo synchronously before ever touching a worker thread)
                    // would be needed before this could safely go async again.
                    // Every track is asked: the provider looks in the file (and its
                    // folder) itself, since the scanner never records artwork.
                    Image {
                        visible: cell.col && cell.col.key === "coverArt" && status === Image.Ready
                        anchors.fill: parent
                        anchors.margins: 1
                        // Fills the cell (cropped), not a small square in the middle.
                        fillMode: Image.PreserveAspectCrop
                        clip: true
                        asynchronous: false
                        cache: true
                        source: !(cell.col && cell.col.key === "coverArt" && cell.model) ? ""
                                : cell.model.trackId > 0 ? ("image://mixhousecovers/" + cell.model.trackId)
                                : cell.model.filePath ? ("image://mixhousecovers/file/" + encodeURIComponent(cell.model.filePath))
                                : ""
                    }

                    // GlideMix: the preview button, white, on the artwork (rekordbox's
                    // look): play on hover; while this track plays, skip ahead.
                    // Moving the mouse off the artwork stops the preview unless
                    // the track is selected.
                    Rectangle {
                        id: coverPreviewButton
                        readonly property bool playingThis: !!cell.model && tracksModel.previewPlaying
                                                            && tracksModel.previewTrackId === cell.model.trackId
                        visible: !!cell.col && cell.col.key === "coverArt" && !!cell.model
                                 && cell.mouseArea.containsMouse
                        anchors.centerIn: parent
                        width: Math.min(18, cell.height - 4)
                        height: width
                        radius: width / 2
                        color: Qt.rgba(0, 0, 0, 0.6)
                        border.color: "white"
                        border.width: 1

                        StyledIconLabel {
                            anchors.centerIn: parent
                            iconCode: coverPreviewButton.playingThis ? IconCode.REWIND_END_FILL : IconCode.PLAY_FILL
                            color: "white"
                            font.pixelSize: 9
                        }
                    }

                    Connections {
                        target: cell.col && cell.col.key === "coverArt" ? cell.mouseArea : null
                        function onContainsMouseChanged() {
                            if (!cell.mouseArea.containsMouse && coverPreviewButton.playingThis
                                    && !root.isRowSelected(cell.row)) {
                                tracksModel.stopPreview()
                            }
                        }
                    }

                    // Color is a real swatch (mixxx's own colored dot), not text --
                    // library.color is 0xRRGGBB, or -1 for "no color set".
                    Rectangle {
                        visible: cell.col && cell.col.key === "color"
                        anchors.centerIn: parent
                        width: 12
                        height: 12
                        radius: 6
                        color: cell.model && cell.model.color >= 0
                               ? Qt.rgba(((cell.model.color >> 16) & 0xff) / 255,
                                         ((cell.model.color >> 8) & 0xff) / 255,
                                         (cell.model.color & 0xff) / 255, 1)
                               : "transparent"
                        border.width: 1
                        border.color: ui.theme.strokeColor
                    }

                    // Waveform Overview -- real mixxx's own three overview
                    // types (waveform/overviewtype.h's OverviewType enum:
                    // Filtered/HSV/RGB, DlgPrefWaveform's "Overview
                    // Waveforms" combo, default RGB), selectable from the
                    // "Music library" Preferences page -- see
                    // IMixHouseLibraryConfiguration::waveformOverviewType's
                    // own comment. An earlier version of this hardcoded RGB
                    // and tinted the bands with Audacity's own accent color
                    // instead of real mixxx's red/green/blue, reasoning that
                    // the "colors and font we keep as audacity have"
                    // rule covered this too -- wrong: that rule is about UI
                    // CHROME (buttons, borders, highlights), not audio-data
                    // color-coding, which is informational the same way the
                    // Key column's real Mixed In Key palette already is.
                    // testing caught this directly and asked for the real RGB
                    // waveform, "in library and everywhere" (this column IS
                    // the only place a MixHouse waveform renders anywhere in
                    // this app right now) -- explicitly NOT in Arrangement
                    // mode, which is Audacity's own native track waveform
                    // display (src/projectscene/, off-limits regardless) and
                    // stays its own default black.
                    //
                    // Real mixxx pre-renders and caches a QImage per track
                    // (OverviewCache); this instead resamples straight into
                    // {low,mid,high,all} buckets in C++
                    // (mixhousetracklistmodel::overviewBuckets, itself caching
                    // the expensive file+zlib+protobuf decode per track id)
                    // and draws them here with a Canvas every time -- cheap
                    // enough at one column's width per row, and (unlike a
                    // cached pixmap) never goes stale when the type/marker
                    // Preference changes underneath an already-open panel.
                    // A dedicated near-black background behind this column
                    // was tried and then explicitly reverted same day --
                    // with the colors now properly saturated (see
                    // onPaint below), the extra boxed-in dark background
                    // looked worse, not better; the row's own normal
                    // background reads fine now that the waveform itself
                    // carries the contrast.
                    // Rekordbox's own per-track analysis progress cell --
                    // matched against the rekordbox screenshots: a small
                    // centered "NN%" over a short 2px bar, dim while queued
                    // at 0%, blank once the track is done (or not in a
                    // running batch at all). Percent is mixxx's own
                    // AnalyzerThread scale (decode 0-95, 95 while finalizing),
                    // see MixHouseAnalyzerWorker::progress.
                    Item {
                        id: analysisProgressCell
                        readonly property int percent: cell.model ? cell.model.analysisProgress : -1
                        visible: cell.col && cell.col.key === "analysisProgress"
                        anchors.fill: parent

                        // Not in a running batch: rekordbox marks every
                        // analyzed track in this column (with its own icon);
                        // we were asked for the word instead.
                        StyledTextLabel {
                            visible: analysisProgressCell.percent < 0 && !!cell.model && cell.model.analyzed
                            anchors.centerIn: parent
                            text: qsTrc("mixhouse", "Analyzed")
                            font.pixelSize: 10
                            color: ui.theme.fontPrimaryColor
                            opacity: 0.6
                        }

                        Column {
                            visible: analysisProgressCell.percent >= 0
                            anchors.centerIn: parent
                            spacing: 2

                            StyledTextLabel {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: analysisProgressCell.percent + "%"
                                font.pixelSize: 10
                                color: ui.theme.fontPrimaryColor
                                opacity: analysisProgressCell.percent > 0 ? 0.85 : 0.5
                            }

                            Rectangle {
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: Math.min(44, analysisProgressCell.width - 12)
                                height: 2
                                color: ui.theme.strokeColor

                                Rectangle {
                                    width: parent.width * analysisProgressCell.percent / 100
                                    height: parent.height
                                    color: ui.theme.fontPrimaryColor
                                }
                            }
                        }
                    }

                    Canvas {
                        id: overview
                        visible: cell.col && cell.col.key === "overview"
                        anchors.fill: parent
                        anchors.margins: 2
                        anchors.leftMargin: root.previewStopZoneWidth

                        property var buckets: null
                        property var cues: []
                        // One bucket per PHYSICAL pixel (measured 2026-09-13:
                        // one per logical point drew every column 2px wide on
                        // Retina); remembered so onPaint uses the ratio the
                        // buckets were built for.
                        property real bucketDpr: 1

                        // Rekordbox fills in each track's waveform the moment
                        // that track finishes, not when the whole batch does:
                        // the model drops a finished track back to -1 (and
                        // clears its cached overview) right after writing it.
                        readonly property int analysisProgress: cell.model ? cell.model.analysisProgress : -1
                        onAnalysisProgressChanged: if (analysisProgress < 0) overview.refresh()

                        onWidthChanged: overview.refresh()
                        onVisibleChanged: if (visible) overview.refresh()

                        // Same guarded-subscribe reasoning as
                        // trackTableView's own onLibraryRowHeightChanged-
                        // >forceLayout() below: a Canvas only repaints when
                        // told to, so a type/marker change made in
                        // Preferences needs an explicit requestPaint() to
                        // reach an already-open panel instead of silently
                        // doing nothing until the next unrelated repaint.
                        Connections {
                            target: tracksModel
                            function onWaveformOverviewTypeChanged() { overview.requestPaint() }
                            function onShowWaveformMinuteMarkersChanged() { overview.requestPaint() }
                            function onOverviewDataChanged() { overview.refresh() }
                        }

                        function refresh() {
                            if (!overview.visible || overview.width <= 1) {
                                return
                            }
                            overview.bucketDpr = Math.max(1, Screen.devicePixelRatio)
                            overview.buckets = tracksModel.overviewBuckets(cell.row, Math.floor(overview.width * overview.bucketDpr))
                            overview.cues = tracksModel.overviewCues(cell.row)
                            overview.requestPaint()
                        }

                        onPaint: {
                            var ctx = getContext("2d")
                            ctx.clearRect(0, 0, width, height)
                            if (!overview.buckets || overview.buckets.length === 0) {
                                return
                            }

                            // Rekordbox's own real "RGB" waveform mode --
                            // confirmed by the brief directly against a real
                            // rekordbox screenshot of these SAME tracks: red=
                            // bass, green=mids, blue=treble, blended per
                            // column (not layered traces -- that's rekordbox's
                            // separate "3Band" mode, tried first and rejected
                            // once we compared the same tracks side by
                            // side). Real, sourced hex baseline: #FF0000 low,
                            // #00FF00 mid, #0000FF high -- identical to real
                            // mixxx's own literal Qt::red/green/blue defaults,
                            // so this is NOT the same "rainbow" the brief
                            // originally rejected -- that one came from real
                            // mixxx's own drawWaveformPartRGB() quirk (see the
                            // RGB branch below), which hardcoded low/mid/high
                            // straight into output R/G/B and never actually
                            // blended these configured colors at all. Used by
                            // all three types below (HSV takes its hue from
                            // lowColor.hsvHue, matching real mixxx's own
                            // drawWaveformPartHSV() using the low band's
                            // actual color rather than a hardcoded red).
                            OverviewPaint.drawBuckets(ctx, width, height, overview.buckets, overview.bucketDpr, tracksModel.waveformOverviewType)

                            // DlgPrefWaveform's "Show minute markers on
                            // waveform overview" -- a real, small tick every
                            // 60 seconds of track duration, not an invented
                            // control: this row's own real durationSec is
                            // already available, so each mark lands at its
                            // genuine proportional position.
                            if (tracksModel.showWaveformMinuteMarkers && cell.model && cell.model.durationSec > 0) {
                                ctx.fillStyle = Qt.rgba(1, 1, 1, 0.35)
                                var pxPerSec = width / cell.model.durationSec
                                for (var m = 60; m < cell.model.durationSec; m += 60) {
                                    ctx.fillRect(Math.round(m * pxPerSec), 0, 1, height)
                                }
                            }

                            // GlideMix: cue points, as rekordbox's overview
                            // shows them -- memory cues a red mark along the
                            // bottom, hot cues a line in their colour with
                            // the letter on top.
                            var cues = overview.cues || []
                            for (var c = 0; c < cues.length; ++c) {
                                var cx = Math.round(cues[c].pos * (width - 1))
                                if (!cues[c].hot) {
                                    ctx.fillStyle = cues[c].color
                                    ctx.beginPath()
                                    ctx.moveTo(cx - 3, height)
                                    ctx.lineTo(cx + 3, height)
                                    ctx.lineTo(cx, height - 4)
                                    ctx.closePath()
                                    ctx.fill()
                                }
                            }
                            var tag = Math.max(8, Math.min(11, Math.round(height * 0.4)))
                            ctx.font = "bold " + (tag - 2) + "px sans-serif"
                            ctx.textAlign = "center"
                            ctx.textBaseline = "middle"
                            for (var h = 0; h < cues.length; ++h) {
                                if (!cues[h].hot) {
                                    continue
                                }
                                var hx = Math.round(cues[h].pos * (width - 1))
                                ctx.fillStyle = cues[h].color
                                ctx.fillRect(hx, 0, 1, height)
                                var left = Math.min(Math.max(0, hx), width - tag)
                                ctx.fillRect(left, 0, tag, tag)
                                ctx.fillStyle = "black"
                                ctx.fillText(cues[h].letter, left + tag / 2, tag / 2 + 0.5)
                            }
                        }

                        Component.onCompleted: overview.refresh()
                    }

                    // Preview playhead, over the waveform of the
                    // track that's in the preview player only. Plain items,
                    // not Canvas painting, so the 10Hz position update moves
                    // one Rectangle instead of repainting the waveform (the
                    // fork's own lesson: repainting too much while previewing
                    // made the audio stutter).
                    Item {
                        id: previewOverlay
                        visible: !!cell.col && cell.col.key === "overview" && !!cell.model
                                 && tracksModel.previewTrackId !== -1   // Lexicon / rekordbox XML tracks have ids below -1
                                 && tracksModel.previewTrackId === cell.model.trackId
                                 && tracksModel.previewPlaying
                        anchors.fill: overview

                        Rectangle {
                            // 1px white playhead, as rekordbox draws it.
                            x: previewOverlay.visible
                               ? Math.round(Math.max(0, Math.min(1, tracksModel.previewPosition)) * (previewOverlay.width - 1))
                               : 0
                            width: 1
                            height: previewOverlay.height
                            color: "white"
                        }

                        // Stop, in the gutter left of the waveform, while a
                        // selected track previews (rekordbox's square in a circle).
                        Rectangle {
                            visible: root.isRowSelected(cell.row)
                            x: -root.previewStopZoneWidth + Math.round((root.previewStopZoneWidth - width) / 2)
                            anchors.verticalCenter: parent.verticalCenter
                            width: Math.min(14, previewOverlay.height)
                            height: width
                            radius: width / 2
                            color: "transparent"
                            border.color: "white"
                            border.width: 1

                            Rectangle {
                                anchors.centerIn: parent
                                width: Math.round(parent.width * 0.36)
                                height: width
                                color: "white"
                            }
                        }
                    }

                    // GlideMix: the key as a coloured badge with dark text, like
                    // Lexicon (Preferences > Music library > Show keys as badges).
                    Rectangle {
                        id: keyBadge
                        readonly property string keyText: cell.col && cell.col.key === "musicalKey"
                                                          ? root.cellText(cell.model, "musicalKey") : ""
                        readonly property color keyColor: cell.model ? (keyWheel.colorForKeyId(cell.model.keyId) || "transparent")
                                                                      : "transparent"
                        visible: tracksModel.keyBadges && keyText !== "" && keyText !== "—"
                                 && keyColor !== Qt.rgba(0, 0, 0, 0)
                                 && !(cell.row === root.editingRow && cell.col && cell.col.key === root.editingColumnKey)
                        anchors.left: parent.left
                        anchors.leftMargin: 13
                        anchors.verticalCenter: parent.verticalCenter
                        height: Math.min(parent.height - 6, badgeText.implicitHeight + 6)
                        width: Math.max(38, badgeText.implicitWidth + 16)
                        radius: 4
                        color: keyColor

                        Text {
                            id: badgeText
                            anchors.centerIn: parent
                            text: keyBadge.keyText
                            color: "#1E1E24"
                            font.family: root.libraryFont.family
                            font.pixelSize: root.libraryFont.pixelSize > 0 ? root.libraryFont.pixelSize : ui.theme.bodyFont.pixelSize
                            font.bold: true
                        }
                    }

                    // GlideMix: energy as LED blocks like a level meter (one per
                    // point: green, then yellow from 5, red from 8), then the number.
                    Row {
                        id: energyBlocks
                        readonly property int level: cell.model && cell.col && cell.col.key === "energy"
                                                     ? Math.round(cell.model.energy) : 0
                        visible: level > 0
                        anchors.left: parent.left
                        anchors.leftMargin: 15
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 6

                        Row {
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 1
                            Repeater {
                                model: energyBlocks.level
                                Rectangle {
                                    width: 4
                                    height: 12
                                    radius: 1
                                    color: index >= 7 ? "#EF476F" : (index >= 4 ? "#FFE100" : "#50DF46")
                                }
                            }
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: energyBlocks.level
                            color: ui.theme.fontPrimaryColor
                            font: root.libraryFont
                        }
                    }

                    StyledTextLabel {
                        visible: (!cell.col || (cell.col.key !== "color" && cell.col.key !== "overview"
                                               && cell.col.key !== "coverArt" && cell.col.key !== "analysisProgress"))
                                 && !(cell.row === root.editingRow && cell.col && cell.col.key === root.editingColumnKey)
                                 && !keyBadge.visible && !energyBlocks.visible
                        anchors.fill: parent
                        // a cell's text starts where its header's label
                        // starts -- just past the six-dot grip (the header
                        // label's own left margin) -- and every column reads
                        // flush left, values under names. Right-aligned numbers
                        // sat at the far edge of their column, a long way from
                        // the name they belong to.
                        anchors.leftMargin: 15
                        anchors.rightMargin: 8
                        verticalAlignment: Text.AlignVCenter
                        horizontalAlignment: Qt.AlignLeft
                        text: cell.col ? root.cellText(cell.model, cell.col.key) : ""
                        // GlideMix: Preferences > Library Font (unset = theme font).
                        font: root.libraryFont
                        // Real mixxx: BaseTrackTableModel's own
                        // s_bApplyPlayedTrackColor, driven by "Music
                        // library" Preferences page's "Grey out played
                        // tracks" (basetracktablemodel.cpp:423-450) -- a
                        // whole-row custom text color when timesplayed > 0.
                        // This port reuses the theme's own font color at
                        // reduced opacity instead of a second invented
                        // color (the reference "colors and font we keep as
                        // audacity have" rule, same one the Key column's
                        // color already follows).
                        opacity: (tracksModel.greyOutPlayedTracks && cell.model && cell.model.timesPlayed > 0) ? 0.5
                                 : (cell.col && cell.col.key === "artist" ? 0.8 : 1.0)
                        // Key text only, never a background swatch -- the real
                        // Mixed In Key palette (kMikKeyColors), reused from the
                        // Lancelot wheel itself rather than a second copy of the
                        // same colors. By key_id, not the "musicalKey" text:
                        // library.key is TEXT in whatever notation was active
                        // when the track was analysed -- a real library
                        // has it in Traditional ("Bb", "Ebm"), not Camelot, so
                        // parsing that string as Camelot silently matched
                        // nothing on every row. key_id (mixxx's ChromaticKey
                        // enum) means the same key regardless of notation, so
                        // it's the one to color from. Falls back to the normal
                        // text color for every other column.
                        color: (cell.col && cell.col.key === "musicalKey"
                                && keyWheel.colorForKeyId(cell.model.keyId))
                               || ui.theme.fontPrimaryColor
                    }

                    // Inline edit for the 5 columns this port has a real
                    // write path for (see root.editableColumnKeys' own
                    // comment) -- started by double-clicking the cell
                    // (onDoubleClicked above), committed on Enter or on
                    // losing focus (clicking elsewhere), cancelled on Escape.
                    TextField {
                        id: editField
                        visible: cell.row === root.editingRow && cell.col && cell.col.key === root.editingColumnKey
                        anchors.fill: parent
                        anchors.margins: 1
                        color: ui.theme.fontPrimaryColor
                        selectionColor: ui.theme.accentColor
                        background: Rectangle {
                            color: ui.theme.textFieldColor
                            border.width: 1
                            border.color: ui.theme.accentColor
                        }
                        onVisibleChanged: {
                            if (visible) {
                                text = cell.col ? root.cellText(cell.model, cell.col.key) : ""
                                selectAll()
                                forceActiveFocus()
                            }
                        }
                        Keys.onEscapePressed: root.cancelEdit()
                        onAccepted: root.commitEdit(cell.row, cell.col.key, text)
                        onActiveFocusChanged: {
                            if (!activeFocus && visible) {
                                root.commitEdit(cell.row, cell.col.key, text)
                            }
                        }
                    }

                    // Real mixxx's StarEditor/StarRating::starAtPosition
                    // (library/starrating.cpp:63-84, library/tabledelegates/
                    // stareditor.cpp) -- click a star directly in the cell
                    // instead of only through the right-click submenu. This
                    // cell renders right-aligned text stars rather than
                    // mixxx's own centered star polygons, so the click
                    // geometry is this cell's own (5 equal segments across
                    // the full cell width, a small dead zone at the very
                    // left for "0 stars"), not a literal port of that pixel
                    // math -- same real behavior (click position -> star
                    // count), simplified geometry for this port's own
                    // text-based rendering.
                    TextMetrics {
                        id: starsMetrics
                        font: root.libraryFont
                        text: "★★★★★"
                    }
                    MouseArea {
                        visible: cell.col && cell.col.key === "rating"
                        anchors.fill: parent
                        onClicked: {
                            root.selectRow(cell.row)   // same as any other plain click on the row
                            // The stars start at the text's left margin (15); clicking the
                            // star the song already has clears the rating.
                            var starWidth = Math.max(1, starsMetrics.advanceWidth / 5)
                            var star = mouse.x < 15 ? 0 : Math.min(5, Math.ceil((mouse.x - 15) / starWidth))
                            if (cell.model && star === cell.model.rating) {
                                star = 0
                            }
                            tracksModel.setTrackRating(cell.row, Math.max(0, star))
                        }
                    }
                }
            }

            // The real track menu, ported item-for-item against mixxx's own
            // WTrackMenu (widget/wtrackmenu.cpp) -- same order, same wording,
            // same submenu nesting. the brief pointed at Mixxx's and Rekordbox's
            // right-click menus and asked for the full count of options, not
            // just the handful this had before. Every item below either does
            // real work against the live database (most of them: Search
            // related Tracks, Auto DJ queueing, Adjust BPM, Rating, the whole
            // Clear submenu, Find on Web, Purge, Move to Trash, Properties)
            // or is shown correctly DISABLED (never a silent no-op) for the
            // pieces that genuinely need an engine this app doesn't have yet
            // -- file-tag read/write, MusicBrainz, cover art, hotcues, the
            // analysis engine. Those are real, separate, already-tracked
            // roadmap phases, not faked here.
            ContextMenuLoader {
                id: trackMenu

                property int targetRow: -1
                // The batch "Collection Actions" submenu's real target -- a
                // snapshot of root.selectedRows taken now, at menu-open time
                // (the row delegate's own onClicked has already collapsed the
                // selection to just the clicked row first, if it wasn't
                // already part of it -- see there for the real-mixxx-matched
                // right-click rule). Every other, still-single-row item below
                // keeps using targetRow, unchanged.
                property var targetRows: []
                // Same snapshot rule as targetRows, for the same reason:
                // root.allMatchingSelected (set only by selectAllRows())
                // means the real target of a batch action is every track
                // matching the current view, not just targetRows -- see
                // removeAllMatchingFromLibraryWithConfirm()'s own comment.
                property bool targetAllMatching: false
                property string targetFilePath: ""
                property string targetTitleFull: ""
                property var targetData: null

                function open(pos, row, model) {
                    trackMenu.targetRow = row
                    trackMenu.targetRows = root.selectedRows.slice()
                    trackMenu.targetAllMatching = root.allMatchingSelected
                    trackMenu.targetFilePath = model.filePath
                    trackMenu.targetTitleFull = (model.artist ? model.artist + " – " : "") + (model.title || "")
                    // A plain-object snapshot -- root.cellText() formats every
                    // field here the same way the table itself does. Snapshot,
                    // not a live model reference: safer across a reload that
                    // might happen before the user picks a submenu item.
                    trackMenu.targetData = {
                        title: model.title, artist: model.artist, album: model.album,
                        albumArtist: model.albumArtist, genre: model.genre, composer: model.composer,
                        grouping: model.grouping, year: model.year, trackNumber: model.trackNumber,
                        comment: model.comment, bpm: model.bpm, musicalKey: model.musicalKey,
                        durationSec: model.durationSec, bitrate: model.bitrate, replaygain: model.replaygain,
                        fileType: model.fileType, filePath: model.filePath, dateAdded: model.dateAdded,
                        timesPlayed: model.timesPlayed, lastPlayedAt: model.lastPlayedAt, rating: model.rating,
                        trackId: model.trackId, coverArtType: model.coverArtType
                    }

                    var artist = model.artist || ""
                    var ttl = model.title || ""
                    var album = model.album || ""
                    var keyText = model.musicalKey || ""

                    // ---- Search related Tracks: real, via the existing text/
                    // key filters -- a plain-text match (title/artist/album),
                    // same LIKE mixxx's own quick search would do, not yet the
                    // field-scoped syntax that's its own later roadmap item.
                    var searchItems = []
                    if (artist) searchItems.push({ id: "search:artist", title: qsTrc("mixhouse", "Artist") + ": " + artist })
                    if (album)  searchItems.push({ id: "search:album",  title: qsTrc("mixhouse", "Album") + ": " + album })
                    if (ttl)    searchItems.push({ id: "search:title",  title: qsTrc("mixhouse", "Title") + ": " + ttl })
                    if (keyText) searchItems.push({ id: "search:key",  title: qsTrc("mixhouse", "Key") + ": " + keyText })
                    if (searchItems.length === 0) {
                        searchItems.push({ id: "", title: qsTrc("mixhouse", "No related tracks"), enabled: false })
                    }

                    // "Create New Playlist"/"Create New Crate" sit at the bottom
                    // of these submenus in mixxx's real WTrackMenu ALWAYS, not
                    // only when the list is empty -- same here.
                    var playlists = tracksModel.playlistsForMenu()
                    var playlistItems = []
                    for (var i = 0; i < playlists.length; ++i) {
                        playlistItems.push({ id: "pl:" + playlists[i].id, title: playlists[i].name })
                    }
                    playlistItems.push({}, { id: "pl-new", title: qsTrc("mixhouse", "Create New Playlist…") })

                    var crates = tracksModel.cratesForMenu()
                    var crateItems = []
                    for (var j = 0; j < crates.length; ++j) {
                        crateItems.push({ id: "crate:" + crates[j].id, title: crates[j].name })
                    }
                    crateItems.push({}, { id: "crate-new", title: qsTrc("mixhouse", "Create New Crate…") })

                    var colorItems = [{ id: "color:-1", title: qsTrc("mixhouse", "No Color") }]
                    for (var k = 0; k < root.trackColorPalette.length; ++k) {
                        var hex = root.trackColorPalette[k].hex
                        colorItems.push({ id: "color:" + hex,
                                          title: root.trackColorPalette[k].name,
                                          icon: IconCode.FRETBOARD_MARKER_CIRCLE_FILLED,
                                          iconColor: "#" + ("000000" + hex.toString(16)).slice(-6) })
                    }

                    // ---- Adjust BPM: real column scaling (library.bpm), real
                    // bpm_lock. Graying the scale actions out entirely while
                    // locked matches mixxx exactly -- no silent no-op click.
                    var bpmItems
                    if (model.bpmLocked) {
                        bpmItems = [{ id: "bpm-unlock", title: qsTrc("mixhouse", "Unlock BPM") }]
                    } else {
                        bpmItems = [
                            { id: "bpm:0.5",   title: qsTrc("mixhouse", "Halve BPM") },
                            { id: "bpm:0.667", title: qsTrc("mixhouse", "2/3 BPM") },
                            { id: "bpm:0.75",  title: qsTrc("mixhouse", "3/4 BPM") },
                            { id: "bpm:1.333", title: qsTrc("mixhouse", "4/3 BPM") },
                            { id: "bpm:1.5",   title: qsTrc("mixhouse", "3/2 BPM") },
                            { id: "bpm:2",     title: qsTrc("mixhouse", "Double BPM") },
                            {},
                            { id: "bpm-lock",  title: qsTrc("mixhouse", "Lock BPM") }
                        ]
                    }

                    // ---- Rating: real, a small submenu standing in for
                    // mixxx's inline star widget (this generic menu can't
                    // host a custom hover-star control).
                    var ratingItems = [{ id: "rating:0", title: qsTrc("mixhouse", "No Rating") }]
                    for (var r = 1; r <= 5; ++r) {
                        ratingItems.push({ id: "rating:" + r, title: "★".repeat(r) })
                    }

                    // ---- Find on Web: real, opens the actual search URLs
                    // mixxx's own WFindOnWebMenu builds for Soundcloud/
                    // Discogs/Last.fm. Flattened to one "best available query"
                    // item per service (Artist + Title, else Title, else
                    // Artist, else Album) rather than mixxx's fuller per-field
                    // breakdown, to keep this a 1-click action here.
                    var webQuery = artist && ttl ? (artist + " " + ttl) : (ttl || artist || album)
                    var webItems = []
                    if (webQuery) {
                        webItems.push({ id: "web:https://soundcloud.com/search/sounds?q=" + encodeURIComponent(webQuery),
                                        title: qsTrc("mixhouse", "Soundcloud") + ": " + webQuery })
                        webItems.push({ id: "web:https://www.discogs.com/search/?type=release&q=" + encodeURIComponent(webQuery),
                                        title: qsTrc("mixhouse", "Discogs") + ": " + webQuery })
                        webItems.push({ id: "web:https://www.last.fm/search/tracks?q=" + encodeURIComponent(webQuery),
                                        title: qsTrc("mixhouse", "Last.fm") + ": " + webQuery })
                    } else {
                        webItems.push({ id: "", title: qsTrc("mixhouse", "No text to search"), enabled: false })
                    }

                    // ---- Clear: real, exact order and wording of mixxx's own
                    // Clear submenu. "All" mirrors
                    // ClearAllPerformanceMetadataTrackPointerOperation exactly
                    // -- every field below it EXCEPT Comment.
                    var M = MixHouseTrackListModel
                    // Real mixxx greys out "BPM and Beatgrid" (and, by the
                    // same logic, "All") whenever ANY track in the current
                    // selection is BPM-locked (wtrackmenu.cpp:1137-1153) --
                    // checked over the whole selection, not just the row
                    // that was actually right-clicked.
                    var anyBpmLocked = tracksModel.isAnyTrackBpmLocked(root.selectedRows)
                    var clearItems = [
                        { id: "clear:" + M.ClearBeats,       title: qsTrc("mixhouse", "BPM and Beatgrid"), enabled: !anyBpmLocked },
                        { id: "clear:" + M.ClearPlayCount,   title: qsTrc("mixhouse", "Play Count") },
                        { id: "clear:" + M.ClearRatingField, title: qsTrc("mixhouse", "Rating") },
                        { id: "clear:" + M.ClearComment,     title: qsTrc("mixhouse", "Comment") },
                        { id: "clear:" + M.ClearMainCue,     title: qsTrc("mixhouse", "Cue Point") },
                        { id: "clear:" + M.ClearHotCues,     title: qsTrc("mixhouse", "Hotcues") },
                        { id: "clear:" + M.ClearIntroCue,    title: qsTrc("mixhouse", "Intro") },
                        { id: "clear:" + M.ClearOutroCue,    title: qsTrc("mixhouse", "Outro") },
                        { id: "clear:" + M.ClearLoops,       title: qsTrc("mixhouse", "Loops") },
                        { id: "clear:" + M.ClearKey,         title: qsTrc("mixhouse", "Key") },
                        { id: "clear:" + M.ClearReplayGain,  title: qsTrc("mixhouse", "ReplayGain") },
                        { id: "clear:" + M.ClearWaveform,    title: qsTrc("mixhouse", "Waveform") },
                        {},
                        { id: "clear:" + M.ClearAll,         title: qsTrc("mixhouse", "All"), enabled: !anyBpmLocked }
                    ]

                    // ---- Collection Actions: real WTrackMenu::slotRemove /
                    // slotPurge / slotRemoveFromDisk, generalized to the whole
                    // current selection (trackMenu.targetRows) instead of the
                    // old flat, single-row Hide/Purge/Move-to-Trash items.
                    // "Remove from Collection" and "Remove from Collection and
                    // Library" only make sense -- and only appear -- while a
                    // specific playlist/crate is the active view; showing
                    // "...and Library" right next to a plain "Remove from
                    // Library" when there's no collection to remove FROM would
                    // just be the same action listed twice, so it's omitted
                    // rather than degraded to a duplicate in that context.
                    //
                    // "Remove from Library" REALLY removes the library row now
                    // (a purge) -- it originally soft-hid instead
                    // (mixxx_deleted=1, matching mixxx's own "Hide"), which is
                    // exactly what turned a routine cleanup into the whole
                    // library going invisible in one no-confirm click (shift-
                    // click a big range, "Remove from Library", done -- no
                    // warning, and hidden tracks had no way back in this app:
                    // no UI ever browsed into the Hidden node, and re-adding
                    // the same file by path silently no-ops against the
                    // already-known row instead of surfacing it again). Purge
                    // needs a real confirmation because of that, unlike the
                    // old Hide it replaces.
                    var viewingCollection = tracksModel.isViewingCollection()
                    var collectionItems = []
                    if (viewingCollection) {
                        collectionItems.push({ id: "collection-remove", title: qsTrc("mixhouse", "Remove from Collection") })
                    }
                    collectionItems.push({ id: "collection-hide", title: qsTrc("mixhouse", "Remove from Library") })
                    if (viewingCollection) {
                        collectionItems.push({ id: "collection-remove-hide", title: qsTrc("mixhouse", "Remove from Collection and Library") })
                    }
                    // Rich text: the ONE explicit visible-red exception, per
                    // the reference direct request -- StyledMenuItem's title
                    // label already renders textFormat: Text.RichText, so this
                    // needs no change to the shared menu component itself.
                    collectionItems.push({ id: "collection-trash",
                        title: "<font color=\"" + root.deleteFromDiskColor + "\">"
                               + qsTrc("mixhouse", "Delete from Disk") + "</font>" })

                    trackMenu.show(pos, [
                        { id: "search-menu", title: qsTrc("mixhouse", "Search related Tracks"), subitems: searchItems },
                        { id: "show-in-library", title: qsTrc("mixhouse", "Show in Library"),
                          enabled: trackMenu.targetData.trackId >= 0 && root.currentViewKey !== "all:-1" },
                        {},
                        // "Load to Deck" is genuinely functional: it hands the
                        // track's actual file to Audacity's own import
                        // pipeline (loadTrackToProject), the same one File >
                        // Import Audio uses -- not a stub. There's no live-deck
                        // concept in this app to offer Deck 2/3/4 alongside it.
                        { id: "load-menu", title: qsTrc("mixhouse", "Load to"),
                          subitems: [{ id: "load", title: trackMenu.targetRows.length > 1
                                       ? qsTrc("mixhouse", "Project (%1 songs, in this order)").arg(trackMenu.targetRows.length)
                                       : qsTrc("mixhouse", "Project") }] },
                        { id: "edit-beatgrid", title: qsTrc("mixhouse", "Edit Beat Grid"),
                          enabled: trackMenu.targetData.trackId >= 0 },
                        {},
                        { id: "pl-menu", title: qsTrc("mixhouse", "Add to Playlist"), subitems: playlistItems },
                        {},
                        { id: "bpm-menu", title: qsTrc("mixhouse", "Adjust BPM"), subitems: bpmItems },
                        { id: "rating-menu", title: qsTrc("mixhouse", "Rating"), subitems: ratingItems },
                        { id: "color-menu", title: qsTrc("mixhouse", "Select Color"), subitems: colorItems },
                        { id: "metadata-menu", title: qsTrc("mixhouse", "Metadata"), subitems: [
                            { id: "import-file-tags", title: qsTrc("mixhouse", "Import From File Tags"),
                              enabled: trackMenu.targetData.trackId >= 0 },
                            { id: "", title: qsTrc("mixhouse", "Import From MusicBrainz"), enabled: false },
                            { id: "", title: qsTrc("mixhouse", "Export To File Tags"), enabled: false },
                            {},
                            // Real mixxx's own "Cover Art" submenu is Choose new
                            // cover/Clear cover/Reload from file/folder (all
                            // WRITES to library.coverart_*) -- not built here yet.
                            // What this ships instead: the read-only "show it
                            // bigger" behavior real mixxx keeps on the deck's own
                            // cover-art widget (DlgCoverArtFullSize), reached from
                            // here since this app has no deck cover-art widget of
                            // its own to click. Enabled only when the track
                            // actually has embedded art (CoverInfoRelative::Type
                            // NONE == 0) -- there's nothing to show bigger otherwise.
                            { id: "cover-art", title: qsTrc("mixhouse", "Cover Art"),
                              enabled: model.coverArtType !== 0 },
                            { id: "web-menu", title: qsTrc("mixhouse", "Find on Web"), subitems: webItems }
                        ] },
                        { id: "", title: qsTrc("mixhouse", "Hotcues"), enabled: false },
                        { id: "clear-menu", title: qsTrc("mixhouse", "Clear"), subitems: clearItems },
                        // "Analyze" is real now -- real mixxx's own default
                        // Queen Mary DSP engine (ported to
                        // mixhouseanalyzer.h), detecting BPM/beatgrid/key
                        // from the actual audio. The other three stay
                        // disabled: this always overwrites (real "Analyze
                        // only if missing" needs a check this doesn't have
                        // yet) and always produces a constant-tempo grid
                        // (the constant/variable BPM distinction isn't built
                        // yet either) -- shown correctly unclickable rather
                        // than silently behaving like plain Analyze.
                        { id: "analyze-menu", title: qsTrc("mixhouse", "Analyze"), subitems: [
                            { id: "analyze", title: qsTrc("mixhouse", "Analyze") },
                            { id: "", title: qsTrc("mixhouse", "Reanalyze"), enabled: false },
                            { id: "", title: qsTrc("mixhouse", "Reanalyze (constant BPM)"), enabled: false },
                            { id: "", title: qsTrc("mixhouse", "Reanalyze (variable BPM)"), enabled: false }
                        ] },
                        {},
                        // "Purge from Library" (standalone) retired by the project's
                        // own explicit request -- redundant now that "Delete
                        // from Disk" and "Remove from Library" below cover the
                        // real needs. The underlying purgeTrackWithConfirm C++
                        // method is deliberately left in place, just unused
                        // from this menu, in case it's wanted again later.
                        //
                        // "Unhide from Library" retired too, on the own
                        // explicit correction: it was never actually reachable
                        // (a hidden track can't show up in this very view to
                        // right-click in the first place, since the Tracks
                        // query filters mixxx_deleted=0) -- dead UI. "Remove
                        // from Library" below no longer hides at all, so
                        // there's nothing left to unhide.
                        { id: "collection-actions-menu", title: qsTrc("mixhouse", "Collection Actions"), subitems: collectionItems },
                        { id: "reveal", title: qsTrc("mixhouse", "Open in File Browser") },
                        {},
                        { id: "edit-tags", title: qsTrc("mixhouse", "Edit Tags…") },
                        { id: "properties", title: qsTrc("mixhouse", "Properties") }
                    ])
                }

                onHandleMenuItem: function(itemId) {
                    if (itemId === "") {
                        return
                    } else if (itemId === "load") {
                        tracksModel.loadTracksToProject(trackMenu.targetRows)
                    } else if (itemId === "edit-beatgrid") {
                        tracksModel.openInGridEditor(trackMenu.targetRow)
                    } else if (itemId === "reveal") {
                        tracksModel.revealInFileBrowser(trackMenu.targetFilePath)
                    } else if (itemId === "analyze") {
                        // The whole current selection (trackMenu.targetRows is
                        // always at least [targetRow] -- see the Collection
                        // Actions comment above on how right-click resolves
                        // the active selection), not just the clicked row --
                        // this is exactly the "shift-click several, only one
                        // got analyzed" bug we hit.
                        tracksModel.analyzeTracksWithConfirm(trackMenu.targetRows)
                        root.clearRowSelection()
                    } else if (itemId === "collection-remove") {
                        if (trackMenu.targetAllMatching) {
                            tracksModel.removeAllMatchingFromCurrentCollection()
                        } else {
                            tracksModel.removeTracksFromCurrentCollection(trackMenu.targetRows)
                        }
                    } else if (itemId === "collection-hide") {
                        if (trackMenu.targetAllMatching) {
                            tracksModel.removeAllMatchingFromLibraryWithConfirm()
                        } else {
                            tracksModel.removeTracksFromLibraryWithConfirm(trackMenu.targetRows)
                        }
                    } else if (itemId === "collection-remove-hide") {
                        tracksModel.removeFromCollectionAndLibraryWithConfirm(trackMenu.targetRows)
                    } else if (itemId === "collection-trash") {
                        tracksModel.moveTracksToTrashWithConfirm(trackMenu.targetRows)
                    } else if (itemId === "import-file-tags") {
                        tracksModel.importTagsFromFiles(trackMenu.targetRows, trackMenu.targetAllMatching)
                    } else if (itemId === "show-in-library") {
                        root.revealTrack(trackMenu.targetData.trackId, 0)
                    } else if (itemId === "edit-tags") {
                        editTagsPopup.openFor(trackMenu.targetRows)
                    } else if (itemId === "properties") {
                        trackPropertiesPopup.openFor(trackMenu.targetData)
                    } else if (itemId === "cover-art") {
                        coverArtPopup.openFor(trackMenu.targetData)
                    } else if (itemId === "pl-new") {
                        newListPopup.openFor(false, trackMenu.targetRow)
                    } else if (itemId === "crate-new") {
                        newListPopup.openFor(true, trackMenu.targetRow)
                    } else if (itemId === "bpm-lock") {
                        // Whole selection, not just the clicked row -- real
                        // mixxx runs every one of these nine actions over the
                        // entire current selection (WTrackMenu's
                        // applyTrackPointerOperation), which this port's menu
                        // wiring never did until now.
                        tracksModel.setTracksBpmLocked(trackMenu.targetRows, true)
                    } else if (itemId === "bpm-unlock") {
                        tracksModel.setTracksBpmLocked(trackMenu.targetRows, false)
                    } else if (itemId.indexOf("pl:") === 0) {
                        tracksModel.addTracksToPlaylist(trackMenu.targetRows, parseInt(itemId.slice(3)))
                    } else if (itemId.indexOf("crate:") === 0) {
                        tracksModel.addTracksToCrate(trackMenu.targetRows, parseInt(itemId.slice(6)))
                    } else if (itemId.indexOf("color:") === 0) {
                        tracksModel.setTracksColor(trackMenu.targetRows, parseInt(itemId.slice(6)))
                    } else if (itemId.indexOf("autodj:") === 0) {
                        var autoDjLoc = parseInt(itemId.slice(7))
                        if (autoDjLoc === MixHouseTrackListModel.AutoDjReplace) {
                            // Real mixxx confirms before wiping the existing
                            // queue -- this used to run instantly with no
                            // way to cancel.
                            tracksModel.addTracksToAutoDjReplaceWithConfirm(trackMenu.targetRows)
                        } else {
                            tracksModel.addTracksToAutoDj(trackMenu.targetRows, autoDjLoc)
                        }
                    } else if (itemId.indexOf("bpm:") === 0) {
                        tracksModel.scaleTracksBpm(trackMenu.targetRows, parseFloat(itemId.slice(4)))
                    } else if (itemId.indexOf("rating:") === 0) {
                        tracksModel.setTracksRating(trackMenu.targetRows, parseInt(itemId.slice(7)))
                    } else if (itemId.indexOf("clear:") === 0) {
                        tracksModel.clearTracksField(trackMenu.targetRows, parseInt(itemId.slice(6)))
                    } else if (itemId.indexOf("search:") === 0) {
                        var field = itemId.slice(7)
                        if (field === "key") {
                            // Through the wheel, so it shows the key and offers Reset.
                            keyWheel.selectKeys([tracksModel.lancelotKey(trackMenu.targetData.musicalKey)])
                        } else {
                            // In the box, so the filter shows and can be cleared.
                            root.setSearchBox(trackMenu.targetData[field])
                            tracksModel.setSearch(trackMenu.targetData[field])
                        }
                    } else if (itemId.indexOf("web:") === 0) {
                        Qt.openUrlExternally(itemId.slice(4))
                    }
                }
            }

            // Mixxx's real DlgTrackInfo is a full metadata EDITOR; this is the
            // read-only first step -- the same fields, laid out the same way
            // ("Properties"), backed by the exact snapshot the track menu
            // itself just read. Editing lands with the Metadata roadmap phase
            // (file-tag read/write), not faked here as editable fields that
            // don't actually save.
            Popup {
                id: trackPropertiesPopup

                property var data: null

                function openFor(trackData) {
                    trackPropertiesPopup.data = trackData
                    trackPropertiesPopup.open()
                }

                anchors.centerIn: Overlay.overlay
                width: 420
                height: 480
                modal: true
                focus: true
                closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

                background: Rectangle {
                    color: ui.theme.backgroundPrimaryColor
                    border.width: 1
                    border.color: ui.theme.strokeColor
                    radius: 4
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 8

                    StyledTextLabel {
                        Layout.fillWidth: true
                        font.bold: true
                        text: trackPropertiesPopup.data
                              ? (trackPropertiesPopup.data.artist ? trackPropertiesPopup.data.artist + " – " : "")
                                + (trackPropertiesPopup.data.title || "")
                              : ""
                        elide: Text.ElideRight
                    }

                    SeparatorLine {}

                    StyledListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        ScrollBar.vertical: StyledScrollBar {}
                        model: trackPropertiesPopup.data ? [
                            { label: qsTrc("mixhouse", "Album"), key: "album" },
                            { label: qsTrc("mixhouse", "Album Artist"), key: "albumArtist" },
                            { label: qsTrc("mixhouse", "Genre"), key: "genre" },
                            { label: qsTrc("mixhouse", "Composer"), key: "composer" },
                            { label: qsTrc("mixhouse", "Grouping"), key: "grouping" },
                            { label: qsTrc("mixhouse", "Year"), key: "year" },
                            { label: qsTrc("mixhouse", "Track #"), key: "trackNumber" },
                            { label: qsTrc("mixhouse", "BPM"), key: "bpm" },
                            { label: qsTrc("mixhouse", "Energy"), key: "energy" },
                            { label: qsTrc("mixhouse", "Key"), key: "musicalKey" },
                            { label: qsTrc("mixhouse", "Duration"), key: "durationSec" },
                            { label: qsTrc("mixhouse", "Bitrate"), key: "bitrate" },
                            { label: qsTrc("mixhouse", "ReplayGain"), key: "replaygain" },
                            { label: qsTrc("mixhouse", "Type"), key: "fileType" },
                            { label: qsTrc("mixhouse", "Rating"), key: "rating" },
                            { label: qsTrc("mixhouse", "Comment"), key: "comment" },
                            { label: qsTrc("mixhouse", "Date Added"), key: "dateAdded" },
                            { label: qsTrc("mixhouse", "Times Played"), key: "timesPlayed" },
                            { label: qsTrc("mixhouse", "Last Played"), key: "lastPlayedAt" },
                            { label: qsTrc("mixhouse", "Location"), key: "filePath" }
                        ] : []

                        delegate: RowLayout {
                            width: ListView.view.width
                            spacing: 8

                            StyledTextLabel {
                                Layout.preferredWidth: 110
                                text: modelData.label
                                opacity: 0.7
                            }
                            StyledTextLabel {
                                Layout.fillWidth: true
                                text: modelData.key === "filePath" ? trackPropertiesPopup.data.filePath
                                                                    : root.cellText(trackPropertiesPopup.data, modelData.key)
                                elide: Text.ElideRight
                                wrapMode: Text.NoWrap
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignRight

                        FlatButton {
                            text: qsTrc("mixhouse", "Close")
                            onClicked: trackPropertiesPopup.close()
                        }
                    }
                }
            }

            // Edit Tags, as in rekordbox / Lexicon: one song or many. A field the
            // songs disagree on shows "Mixed" and is left alone unless typed in.
            // Saved into the music files too, so rekordbox and Lexicon see the
            // change after re-reading the tags; their databases stay untouched.
            Popup {
                id: editTagsPopup

                property var rows: []
                property var fieldList: [
                    { key: "title", label: qsTrc("mixhouse", "Title") },
                    { key: "artist", label: qsTrc("mixhouse", "Artist") },
                    { key: "album", label: qsTrc("mixhouse", "Album") },
                    { key: "albumArtist", label: qsTrc("mixhouse", "Album Artist") },
                    { key: "genre", label: qsTrc("mixhouse", "Genre") },
                    { key: "year", label: qsTrc("mixhouse", "Year") },
                    { key: "trackNumber", label: qsTrc("mixhouse", "Track #") },
                    { key: "composer", label: qsTrc("mixhouse", "Composer") },
                    { key: "grouping", label: qsTrc("mixhouse", "Grouping") },
                    { key: "comment", label: qsTrc("mixhouse", "Comment") }
                ]
                property var original: ({})   // key -> common value, or null when mixed
                property var edited: ({})

                function openFor(targetRows) {
                    rows = targetRows.slice()
                    var common = {}
                    for (var i = 0; i < rows.length; ++i) {
                        var tags = tracksModel.tagsForRow(rows[i])
                        for (var j = 0; j < fieldList.length; ++j) {
                            var k = fieldList[j].key
                            var v = tags[k] || ""
                            if (i === 0) {
                                common[k] = v
                            } else if (common[k] !== v) {
                                common[k] = null
                            }
                        }
                    }
                    original = common
                    edited = {}
                    fieldRepeater.model = []
                    fieldRepeater.model = fieldList
                    open()
                }

                function save() {
                    var changes = {}
                    var any = false
                    for (var k in edited) {
                        if (original[k] === null ? edited[k] !== "" : edited[k] !== original[k]) {
                            changes[k] = edited[k]
                            any = true
                        }
                    }
                    if (any) {
                        tracksModel.editTags(rows, changes, writeFileCheck.checked)
                    }
                    close()
                }

                anchors.centerIn: Overlay.overlay
                width: 480
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
                    spacing: 8

                    StyledTextLabel {
                        Layout.fillWidth: true
                        font.bold: true
                        horizontalAlignment: Text.AlignLeft
                        text: editTagsPopup.rows.length > 1
                              ? qsTrc("mixhouse", "Edit Tags – %1 songs").arg(editTagsPopup.rows.length)
                              : qsTrc("mixhouse", "Edit Tags")
                    }

                    SeparatorLine {}

                    Flickable {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        contentHeight: fieldColumn.height
                        ScrollBar.vertical: StyledScrollBar {}

                        Column {
                            id: fieldColumn
                            width: parent.width
                            spacing: 6

                            Repeater {
                                id: fieldRepeater
                                delegate: RowLayout {
                                    width: fieldColumn.width
                                    spacing: 8

                                    StyledTextLabel {
                                        Layout.preferredWidth: 100
                                        horizontalAlignment: Text.AlignLeft
                                        text: modelData.label
                                        opacity: 0.7
                                    }
                                    TextInputField {
                                        Layout.fillWidth: true
                                        currentText: editTagsPopup.original[modelData.key] === null
                                                     ? "" : (editTagsPopup.original[modelData.key] || "")
                                        hint: editTagsPopup.original[modelData.key] === null ? qsTrc("mixhouse", "Mixed") : ""
                                        onTextEdited: function(newTextValue) {
                                            var e = editTagsPopup.edited
                                            e[modelData.key] = newTextValue
                                            editTagsPopup.edited = e
                                        }
                                        onAccepted: editTagsPopup.save()
                                    }
                                }
                            }
                        }
                    }

                    CheckBox {
                        id: writeFileCheck
                        Layout.fillWidth: true
                        checked: true
                        text: qsTrc("mixhouse", "Also save into the music files (rekordbox and Lexicon show it after re-reading the tags)")
                        onClicked: checked = !checked
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignRight
                        spacing: 8

                        Item { Layout.fillWidth: true }
                        FlatButton {
                            text: qsTrc("mixhouse", "Cancel")
                            onClicked: editTagsPopup.close()
                        }
                        FlatButton {
                            text: qsTrc("mixhouse", "Save")
                            accentButton: true
                            onClicked: editTagsPopup.save()
                        }
                    }
                }
            }

            // Full-size cover art. Real mixxx's DlgCoverArtFullSize is only
            // reachable from a loaded deck's own cover-art widget
            // (wcoverart.cpp/wcoverartlabel.cpp/wspinnybase.cpp) -- there is no
            // such deck widget in this app, so this is reached from the track
            // menu's "Cover Art" item instead (see onHandleMenuItem above).
            // Minimal port of the "show it bigger" behavior only: a plain Popup
            // with the same image:// source the table cell already decoded and
            // cached (mixhousecoverart.h), at a larger size -- not a file-for-
            // file port of the real dialog (no drag-and-drop, no window chrome).
            Popup {
                id: coverArtPopup

                property var data: null

                function openFor(trackData) {
                    coverArtPopup.data = trackData
                    coverArtPopup.open()
                }

                anchors.centerIn: Overlay.overlay
                width: 360
                height: 420
                modal: true
                focus: true
                closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

                background: Rectangle {
                    color: ui.theme.backgroundPrimaryColor
                    border.width: 1
                    border.color: ui.theme.strokeColor
                    radius: 4
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 8

                    StyledTextLabel {
                        Layout.fillWidth: true
                        font.bold: true
                        text: coverArtPopup.data
                              ? (coverArtPopup.data.artist ? coverArtPopup.data.artist + " – " : "")
                                + (coverArtPopup.data.title || "")
                              : ""
                        elide: Text.ElideRight
                    }

                    SeparatorLine {}

                    // asynchronous must stay false here too -- see the row delegate's
                    // Cover Art Image above for why (cross-thread QSqlQuery access via
                    // MixHouseCoverArtImageProvider is the confirmed cause of four real
                    // crashes today).
                    Image {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        fillMode: Image.PreserveAspectFit
                        asynchronous: false
                        cache: true
                        source: (coverArtPopup.data && coverArtPopup.data.coverArtType !== 0)
                                ? ("image://mixhousecovers/" + coverArtPopup.data.trackId) : ""
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignRight

                        FlatButton {
                            text: qsTrc("mixhouse", "Close")
                            onClicked: coverArtPopup.close()
                        }
                    }
                }
            }

            // "Create New Playlist/Crate" -- there's no stock "type a name"
            // dialog in this framework (checked: IInteractive has question/
            // info/warning/error, none take free text), so this is a small,
            // self-contained popup, themed with Audacity's own colors rather
            // than a raw, unstyled QtQuick.Controls.Dialog that would look
            // foreign against its chrome. Two entry points, both real mixxx
            // behaviour: the sidebar's Playlists/Crates right-click (every
            // real DJ app's primary path -- rekordbox, mixxx itself, even
            // Apple Music, all manage lists from a sidebar, not a track's own
            // menu) via openStandalone(), and a track's own "Create New..."
            // shortcut via openFor(), which also adds that track to the new
            // list in the same step.
            //
            // The name field is a plain QtQuick.Controls TextField, not
            // Muse's own TextInputField -- that component's `currentText` is
            // display-only (sets the field's initial text; nothing writes
            // back to it as the user types, checked in TextInputField.qml),
            // so reading it back here always returned "", and every create
            // silently no-op'd. TextField's `text` is a real two-way bound
            // property; explicit colors below keep it from looking like a
            // bare, unstyled Qt Quick Controls default against this panel's
            // Audacity theme.
            Popup {
                id: newListPopup

                property bool isCrate: false
                property int targetRow: -1
                // -1 while creating; >= 0 means "rename this existing id" --
                // confirm() branches on this instead of on a separate mode flag.
                property int editId: -1
                // Playlist folders/nesting -- see MixHouseNamedItem::
                // parentId's own comment for why this is original work, not
                // a port. isFolder is only ever true alongside !isCrate (a
                // crate can't be a folder); parentId is the real Playlists
                // id this new playlist/folder should nest under, -1 for top
                // level -- crates ignore it entirely (crates don't nest).
                property bool isFolder: false
                property int parentId: -1

                modal: true
                focus: true
                x: (tracksColumn.width - width) / 2
                y: (tracksColumn.height - height) / 2
                width: 380
                padding: 16

                background: Rectangle {
                    color: ui.theme.backgroundPrimaryColor
                    radius: 4
                    border.width: 1
                    border.color: ui.theme.strokeColor
                }

                function openFor(crate, row) {
                    newListPopup.isCrate = crate
                    newListPopup.targetRow = row
                    newListPopup.editId = -1
                    newListPopup.isFolder = false
                    newListPopup.parentId = -1
                    newListPopup.errorText = ""
                    nameField.text = ""
                    newListPopup.open()
                    nameField.forceActiveFocus()
                }

                function openStandalone(crate, parentId) {
                    openFor(crate, -1)
                    newListPopup.parentId = parentId === undefined ? -1 : parentId
                }

                // Real rekordbox: right-click a folder (or the Playlists
                // root itself) offers "Create New Folder" as its own item,
                // nesting the new one under whichever folder was
                // right-clicked (manual pp.52, 248) -- see
                // MixHouseNamedItem::parentId's own comment for why this
                // whole feature is original work, not a port.
                function openStandaloneFolder(parentId) {
                    openFor(false, -1)
                    newListPopup.isFolder = true
                    newListPopup.parentId = parentId === undefined ? -1 : parentId
                }

                function openRename(crate, id, currentName) {
                    newListPopup.isCrate = crate
                    newListPopup.targetRow = -1
                    newListPopup.editId = id
                    newListPopup.isFolder = false
                    newListPopup.parentId = -1
                    newListPopup.errorText = ""
                    nameField.text = currentName
                    newListPopup.open()
                    nameField.selectAll()
                    nameField.forceActiveFocus()
                }

                property string errorText: ""

                // Real mixxx: a name collision on create/rename shows "A
                // playlist by that name already exists" and reopens the
                // SAME dialog to retry (widget/wtrackmenu.cpp:1586-1620;
                // library/trackset/baseplaylistfeature.cpp:366-405 create,
                // :260-305 rename; crates get the identical treatment via
                // cratefeaturehelper.cpp:61-67's readCrateByName()). This
                // port's own create/rename calls already return a real
                // failure value on collision (playlists: an explicit
                // pre-check added alongside this fix; crates: the schema's
                // own UNIQUE(name) constraint) -- the popup just never
                // looked at it and always closed regardless. Now it stays
                // open with an inline error instead, so the user can fix
                // the name and retry without retyping everything.
                function confirm() {
                    var name = nameField.text.trim()
                    if (name.length === 0) {
                        return
                    }
                    var ok = true
                    if (newListPopup.editId >= 0) {
                        ok = newListPopup.isCrate
                            ? tracksModel.renameCrate(newListPopup.editId, name)
                            : tracksModel.renamePlaylist(newListPopup.editId, name)
                    } else if (newListPopup.isCrate) {
                        ok = tracksModel.createCrateWithTrack(name, newListPopup.targetRow) >= 0
                    } else if (newListPopup.isFolder) {
                        ok = tracksModel.createPlaylistFolder(name, newListPopup.parentId) >= 0
                    } else {
                        ok = tracksModel.createPlaylistWithTrack(name, newListPopup.targetRow, newListPopup.parentId) >= 0
                    }
                    if (!ok) {
                        newListPopup.errorText = newListPopup.isCrate
                            ? qsTrc("mixhouse", "A crate by that name already exists.")
                            : (newListPopup.isFolder
                               ? qsTrc("mixhouse", "A playlist or folder by that name already exists.")
                               : qsTrc("mixhouse", "A playlist by that name already exists."))
                        nameField.selectAll()
                        nameField.forceActiveFocus()
                        return
                    }
                    newListPopup.errorText = ""
                    newListPopup.close()
                }

                contentItem: ColumnLayout {
                    spacing: 12

                    StyledTextLabel {
                        Layout.fillWidth: true
                        horizontalAlignment: Qt.AlignLeft
                        font.bold: true
                        text: newListPopup.editId >= 0
                              ? (newListPopup.isCrate ? qsTrc("mixhouse", "Rename crate")
                                                       : qsTrc("mixhouse", "Rename playlist"))
                              : (newListPopup.isCrate ? qsTrc("mixhouse", "New crate")
                                 : newListPopup.isFolder ? qsTrc("mixhouse", "New folder")
                                                          : qsTrc("mixhouse", "New playlist"))
                    }

                    TextField {
                        id: nameField
                        Layout.fillWidth: true
                        placeholderText: qsTrc("mixhouse", "Name")
                        color: ui.theme.fontPrimaryColor
                        placeholderTextColor: ui.theme.fontPrimaryColor
                        selectionColor: ui.theme.accentColor
                        background: Rectangle {
                            color: ui.theme.textFieldColor
                            radius: 3
                            border.width: 1
                            border.color: nameField.activeFocus ? ui.theme.accentColor : ui.theme.strokeColor
                        }
                        onAccepted: newListPopup.confirm()
                    }

                    StyledTextLabel {
                        Layout.fillWidth: true
                        visible: newListPopup.errorText.length > 0
                        horizontalAlignment: Qt.AlignLeft
                        wrapMode: Text.WordWrap
                        color: "#e05555"
                        text: newListPopup.errorText
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignRight
                        spacing: 8

                        Item { Layout.fillWidth: true }

                        FlatButton {
                            text: qsTrc("mixhouse", "Cancel")
                            onClicked: newListPopup.close()
                        }
                        FlatButton {
                            text: newListPopup.editId >= 0 ? qsTrc("mixhouse", "Rename")
                                                            : qsTrc("mixhouse", "Create")
                            accentButton: true
                            onClicked: newListPopup.confirm()
                        }
                    }
                }
            }
        }
    }
}
