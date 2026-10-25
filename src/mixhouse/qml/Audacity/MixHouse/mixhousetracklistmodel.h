/*
 * MixHouse DJ 2
 */
#pragma once

#include <optional>

#include "mixhouse/mixhousehdwaveform.h"

#include <QAbstractListModel>
#include <QtQmlIntegration>
#include <QHash>
#include <QSet>
#include <QPixmap>
#include <QTimer>

#include <memory>

#include "modularity/ioc.h"
#include "importexport/export/iexportconfiguration.h"
#include "mixhouse/imixhouselibrary.h"
#include "mixhouse/imixhousegrideditor.h"
#include "mixhouse/imixhouselexicon.h"
#include "mixhouse/imixhouserekordboxxml.h"
#include "mixhouse/imixhouserekordboxphrases.h"
#include "mixhouse/imixhouselibraryconfiguration.h"
#include "mixhouse/imixhousetempomap.h"
#include "mixhouse/imixhousetrackaligner.h"
#include "mixhouse/mixhousewaveform.h"
#include "context/iglobalcontext.h"
#include "framework/interactive/iinteractive.h"
#include "framework/global/progress.h"
#include "async/asyncable.h"
#include "trackedit/iselectioncontroller.h"
#include "trackedit/itrackeditinteraction.h"
#include "trackedit/internal/itracknavigationcontroller.h"

namespace au::mixhouse {
class MixHousePreviewPlayer;

class MixHouseTrackListModel : public QAbstractListModel, public muse::Injectable, public muse::async::Asyncable
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(int trackCount READ trackCount NOTIFY loaded)
    Q_PROPERTY(bool available READ available NOTIFY loaded)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY loaded)
    Q_PROPERTY(int sortKey READ sortKey NOTIFY loaded)
    Q_PROPERTY(bool sortAscending READ sortAscending NOTIFY loaded)
    // TableView needs the model to report a real columnCount() (QAbstractListModel
    // defaults to 1) so it can lay out one column per entry in the QML `columns`
    // array via HorizontalHeaderView -- see mixxx's res/qml/Library/TrackList.qml.
    // QML sets this once, from root.columns.length, so the array stays the single
    // source of truth.
    Q_PROPERTY(int uiColumnCount READ uiColumnCount WRITE setUiColumnCount NOTIFY uiColumnCountChanged)

    // "Music library" Preferences page settings (LibraryPreferencesPage.qml,
    // MixHouseLibraryPreferencesModel) the track table itself needs to read
    // for rendering -- row height, the played-tracks grey-out, and search
    // debounce. Real NOTIFY signals, fired from IMixHouseLibraryConfiguration::
    // settingsChanged() (subscribed once in load(), same guarded-subscribe
    // pattern as IMixHouseLibrary::libraryChanged() just below) -- an
    // EARLIER version of this marked these Q_PROPERTY CONSTANT, meaning a
    // change made in Preferences while this panel was already open never
    // reached it at all (caught directly by testing Library Row
    // Height live and seeing nothing happen). CONSTANT was wrong: nothing
    // here is actually constant, this model just hadn't been told how to
    // hear about a change.
    Q_PROPERTY(int libraryRowHeight READ libraryRowHeight NOTIFY libraryRowHeightChanged)
    Q_PROPERTY(bool greyOutPlayedTracks READ greyOutPlayedTracks NOTIFY greyOutPlayedTracksChanged)
    Q_PROPERTY(int searchDebounceMs READ searchDebounceMs NOTIFY searchDebounceMsChanged)
    // GlideMix: Mixxx's "Library Font" and "BPM display precision".
    Q_PROPERTY(QString libraryFontFamily READ libraryFontFamily NOTIFY libraryFontChanged)
    Q_PROPERTY(int libraryFontPointSize READ libraryFontPointSize NOTIFY libraryFontChanged)
    Q_PROPERTY(int bpmPrecision READ bpmPrecision NOTIFY bpmPrecisionChanged)
    /// MixHouseKeyNotation, for the key wheel's labels.
    Q_PROPERTY(int keyNotation READ keyNotation NOTIFY keyNotationChanged)
    // DlgPrefWaveform's "Overview Waveforms" group -- see
    // IMixHouseLibraryConfiguration::waveformOverviewType/
    // showWaveformMinuteMarkers's own comments. Same guarded-subscribe/
    // real-NOTIFY pattern as the three settings just above -- this is
    // exactly the live-apply bug already fixed once for those, so these
    // start out right rather than repeating it.
    Q_PROPERTY(int waveformOverviewType READ waveformOverviewType NOTIFY waveformOverviewTypeChanged)
    //! GlideMix: goes up as HD waveforms finish, so the overview images reload.
    Q_PROPERTY(int hdWaveRevision READ hdWaveRevision NOTIFY hdWaveRevisionChanged)
    Q_PROPERTY(bool showWaveformMinuteMarkers READ showWaveformMinuteMarkers NOTIFY showWaveformMinuteMarkersChanged)

    muse::Inject<IMixHouseLibrary> library = { this };
    muse::GlobalInject<au::importexport::IExportConfiguration> exportConfiguration;
    muse::GlobalInject<IMixHouseHdWaveforms> hdWaveforms;
    muse::Inject<IMixHouseGridEditor> gridEditor = { this };
    muse::Inject<IMixHouseLexicon> lexicon = { this };
    muse::Inject<IMixHouseRekordboxXml> rekordboxXml = { this };
    muse::Inject<IMixHouseRekordboxPhrases> rekordboxAnalysis = { this };
    muse::Inject<context::IGlobalContext> globalContext = { this };
    muse::Inject<muse::IInteractive> interactive = { this };
    // Importing through IAudacityProject bypasses the normal track-view
    // interaction that selects/focuses a newly-created lane.  Keep the
    // realtime-effects target in sync explicitly after a library import.
    muse::Inject<trackedit::ISelectionController> selectionController = { this };
    muse::Inject<trackedit::ITrackNavigationController> trackNavigationController = { this };
    muse::Inject<trackedit::ITrackeditInteraction> trackeditInteraction = { this };
    muse::Inject<IMixHouseTempoMap> tempoMap = { this };
    muse::Inject<IMixHouseTrackAligner> trackAligner = { this };
    // Registered via globalIoc() (see IMixHouseLibraryConfiguration's own
    // registration comment in mixhousemodule.cpp), so GlobalInject here,
    // not the context-scoped muse::Inject the three lines above use.
    muse::GlobalInject<IMixHouseLibraryConfiguration> libraryConfiguration;

public:
    explicit MixHouseTrackListModel(QObject* parent = nullptr);
    ~MixHouseTrackListModel() override;

    enum Roles {
        TitleRole = Qt::UserRole + 1,
        ArtistRole, AlbumRole, GenreRole, YearRole,
        BpmRole, KeyRole, KeyIdRole, DurationRole, RatingRole,
        HasBeatgridRole, FilePathRole, ColorRole,
        FirstBeatSecRole, BeatLengthSecRole, ConstantTempoRole,
        AlbumArtistRole, ComposerRole, GroupingRole, TrackNumberRole,
        FileTypeRole, CommentRole, BitrateRole, ReplayGainRole,
        DateAddedRole, TimesPlayedRole, LastPlayedAtRole, BpmLockedRole,
        TrackIdRole, CoverArtTypeRole, SampleRateRole,
        AnalysisProgressRole, AnalyzedRole, EnergyRole
    };

    /// PlaylistDAO::AutoDJSendLoc, named for QML as MixHouseTrackListModel.AutoDjTop etc.
    enum AutoDjLoc { AutoDjTop, AutoDjBottom, AutoDjReplace };
    Q_ENUM(AutoDjLoc)

    /// The real "Clear" submenu's fields, named for QML the same way Sort is.
    enum ClearField {
        ClearBeats, ClearPlayCount, ClearRatingField, ClearComment,
        ClearMainCue, ClearHotCues, ClearIntroCue, ClearOutroCue, ClearLoops,
        ClearKey, ClearReplayGain, ClearWaveform, ClearAll
    };
    Q_ENUM(ClearField)

    /// Sort keys, mirrored from IMixHouseLibrary::SortKey so QML can name them.
    // sortBy() below does a raw static_cast<IMixHouseLibrary::SortKey>, so
    // this enum's values must stay numerically aligned with SortKey's own
    // order in imixhouselibrary.h -- including SortPosition, which no
    // column header ever actually sorts by (setPlaylist() sets
    // SortKey::Position directly in C++, never via this enum) but still
    // has to occupy the same numeric slot SortKey::Position does.
    enum Sort { SortTitle, SortArtist, SortAlbum, SortGenre, SortYear,
                SortBpm, SortKey, SortDuration, SortRating, SortDateAdded,
                SortAlbumArtist, SortComposer, SortGrouping, SortTrackNumber,
                SortFileType, SortComment, SortBitrate, SortReplayGain,
                SortTimesPlayed, SortLastPlayedAt, SortPosition,
                SortSampleRate, SortChannels, SortAnalyzed, SortEnergy };
    Q_ENUM(Sort)

    Q_INVOKABLE void load();
    /// Clicking a column header: same column flips direction, new column resets.
    Q_INVOKABLE void sortBy(int sortKey);
    /// Sets the sort directly (a list coming back to how it was left).
    Q_INVOKABLE void setSort(int sortKey, bool ascending);
    Q_INVOKABLE void setSearch(const QString& text);
    Q_INVOKABLE void setPlaylist(int playlistId);
    /// Mirrors setPlaylist() for a crate -- see TrackQuery::crateId in
    /// imixhouselibrary.h for why the two fields are mutually exclusive.
    /// Unlike a playlist, a crate is an unordered set (no PlaylistTracks-style
    /// position column), so entering/leaving one never touches sort order.
    Q_INVOKABLE void setCrate(int crateId);
    /// The sidebar's "Missing Tracks" node -- a fixed view (not one-of-many
    /// like a playlist/crate id), so there's nothing to pass in.
    Q_INVOKABLE void setMissing();
    /// The sidebar's Hidden Tracks node -- rows explicitly hidden in Mixxx's
    /// library (library.mixxx_deleted = 1).
    Q_INVOKABLE void setHidden();
    /// Runs the real disk-existence check (IMixHouseLibrary::
    /// verifyTrackLocations()) and returns the resulting missing-track
    /// count. The sidebar's Missing node calls this itself since this port
    /// has no background scanner to keep fs_deleted up to date on its own.
    Q_INVOKABLE int verifyMissingTracks();
    /// The sidebar's dead-end placeholder leaves -- Computer, Recordings,
    /// Analyze, Rekordbox -- none of which are wired to a real data source
    /// in this port yet. Real mixxx still calls switchToView/showTrackModel
    /// on activation even for a placeholder (browsefeature.cpp:286-290,
    /// recordingfeature.cpp:68-73, analysisfeature.cpp:134-140), so the
    /// track table shows genuinely empty here rather than silently leaving
    /// whatever view was active before this click.
    Q_INVOKABLE void setEmptyPlaceholder();
    //! GlideMix: show another DJ app's tracks -- `source` "lexicon" or
    //! "rekordbox", `playlistId` -1 = all -- read-only, see
    //! IMixHouseExternalLibrary.
    Q_INVOKABLE void setExternalPlaylist(const QString& source, int playlistId);

    //! GlideMix: smart playlists (rekordbox's Intelligent Playlists,
    //! Lexicon's Smartlists). Definitions travel as QVariantMaps:
    //! {id, name, match: "all"|"any", source, groups: [[{field, op, value,
    //! value2}]], sort, ascending, limit}.
    Q_INVOKABLE void setSmartPlaylist(int id);
    /// GlideMix: the sidebar's Duplicates -- songs in the Music Collection
    /// more than once (same artist and title, lengths within 3 s), each
    /// group together with its best copy first (lossless, then bitrate).
    Q_INVOKABLE void setDuplicates();
    /// Rows of every copy but the best one in each group, to select and remove.
    Q_INVOKABLE QVariantList duplicateExtraRows() const;
    /// GlideMix: Lexicon's Smartlist Generator -- one smart playlist per
    /// genre ("genre"), Camelot key ("key") or energy level ("energy") of
    /// `source` ("collection", "lexicon", "rekordbox", "all"). Skips names
    /// that exist already; reports how many were made.
    Q_INVOKABLE void generateSmartPlaylists(const QString& field, const QString& source);
    /// Deletes every smart playlist the generator made (after asking).
    Q_INVOKABLE void deleteGeneratedSmartPlaylistsWithConfirm();
    /// GlideMix (Lexicon): the sidebar nodes ("pl-<id>", "sp-<id>") of the
    /// playlists and smart playlists the song in `row` is in.
    Q_INVOKABLE QStringList sidebarNodesContaining(int row) const;
    Q_INVOKABLE QVariantList smartFieldList() const;
    Q_INVOKABLE QVariantList smartOperatorList(const QString& fieldType) const;
    Q_INVOKABLE QVariantMap smartPlaylistDefinition(int id) const;
    Q_INVOKABLE int saveSmartPlaylist(const QVariantMap& definition);
    Q_INVOKABLE void deleteSmartPlaylistWithConfirm(int id, const QString& name);
    //! How many tracks a definition picks right now (the editor's live count).
    Q_INVOKABLE int smartPlaylistPreviewCount(const QVariantMap& definition) const;
    //! Playlists a rule can name, for a source: [{value: "collection:5", text}].
    Q_INVOKABLE QVariantList smartPlaylistChoices(const QString& source) const;
    //! A plain playlist with the smart playlist's current tracks.
    Q_INVOKABLE int duplicateSmartPlaylistAsPlaylist(int id);

    //! GlideMix: copies a playlist's songs, in order, into a new folder under
    //! `destination` (named after the playlist), with a tracklist .txt and an
    //! .m3u8. `kind`: "playlist", "smart-playlist", "lexicon-playlist",
    //! "rekordbox-playlist". Runs in the background; the status line reports.
    Q_INVOKABLE void exportPlaylistToFolder(const QString& kind, int id, const QString& name, const QString& destination);

    //! GlideMix: the Music Collection, its playlists, the smart playlists and
    //! the Lexicon / rekordbox songs changed in GlideMix, as a rekordbox XML
    //! (asks where to save it).
    Q_INVOKABLE void exportRekordboxXml();
    /// GlideMix: Computer > Quick Links / Devices -- the audio files in one
    /// folder, without importing them (tracks already in the library show as
    /// themselves). Dragging them to a playlist or the collection imports them.
    Q_INVOKABLE void setBrowseFolder(const QString& path);
    /// Sidebar views: Analyze (tracks not analysed yet), History (tracks
    /// used in a project, newest first), Recordings (the export folder).
    Q_INVOKABLE void setAnalyzeView();
    Q_INVOKABLE void setHistoryView();
    Q_INVOKABLE void setRecordingsView();
    Q_INVOKABLE void addQuickLink(const QString& path);
    Q_INVOKABLE void removeQuickLink(const QString& path);
    /// Imports the rows' files that are not in the library yet (browsed
    /// folders, Lexicon, rekordbox XML) and returns true if any was added.
    Q_INVOKABLE bool importRows(const QVariantList& rows);
    //! The rekordbox collection XML shown under Rekordbox XML.
    Q_INVOKABLE void setRekordboxXmlPath(const QString& path);
    Q_INVOKABLE void setKeyFilters(const QStringList& keys);
    /// The sidebar, as Mixxx builds it: a fixed feature hierarchy, NOT a flat
    /// list of playlist rows. Returned as a flat list carrying `level` so QML can
    /// indent it, with expandable parents collapsed by default.
    Q_INVOKABLE QVariantList sidebarNodes(const QStringList& expandedIds) const;
    Q_INVOKABLE QStringList availableKeys() const;

    /// "Load to Deck": there is no live DJ deck in Audacity's world, so this is
    /// its real equivalent here -- import the track's actual audio file as a new
    /// track in the current project, via Audacity's OWN real import pipeline
    /// (the same one File > Import Audio uses). Not a stub: this creates a real,
    /// editable waveform track. Returns false if there's no open project or the
    /// row is out of range.
    Q_INVOKABLE bool loadTrackToProject(int row) const;
    //! GlideMix: opens the row's track in the Beatgrid editor (Beatgrid workspace).
    Q_INVOKABLE bool openInGridEditor(int row) const;

    /// Drag a track (or the whole current multi-selection) out of the
    /// library table -- real mixxx: DragAndDropHelper::dragTrackLocations()
    /// -> dragUrls() (src/util/dnd.cpp), triggered from
    /// WTrackTableView::mouseMoveEvent() once the press has moved past
    /// QApplication::startDragDistance(). Real mixxx has no separate
    /// "internal" MIME type for this: QMimeData::setUrls() with each
    /// track's real file:// location, handed to QDrag::exec(Qt::CopyAction),
    /// is the SAME data whether the drop lands on a Finder window, another
    /// application, or back inside this app's own table/sidebar -- so this
    /// one call is genuinely "drag to anywhere", not just an in-app gesture.
    /// `rows` are indices into the current row list (root.selectedRows if
    /// the pressed row is part of a multi-selection, otherwise just that
    /// one row -- decided in QML, mirroring WTrackTableView's own
    /// getSelectedRows()). Rows with no known file path are silently
    /// skipped (never drag out a placeholder/missing-file row).
    Q_INVOKABLE void startExternalDrag(const QVariantList& rows);
    //! GlideMix: where folder pickers start -- the user's Music folder.
    Q_INVOKABLE QString musicFolder() const;
    /// Where the rekordbox XML chooser opens: the current XML's folder, else Documents.
    Q_INVOKABLE QString rekordboxXmlFolder() const;

    int libraryRowHeight() const;
    bool greyOutPlayedTracks() const;
    int searchDebounceMs() const;
    QString libraryFontFamily() const;
    int libraryFontPointSize() const;
    int bpmPrecision() const;
    int keyNotation() const;
    int waveformOverviewType() const;
    int hdWaveRevision() const { return m_hdWaveRevision; }
    bool showWaveformMinuteMarkers() const;

    /// "Music Directories" -- see IMixHouseLibraryConfiguration's own
    /// per-method comments.
    Q_INVOKABLE QStringList musicDirectories() const;
    Q_INVOKABLE void addMusicDirectory(const QString& path);
    Q_INVOKABLE void removeMusicDirectory(const QString& path);
    /// Called once from QML's Component.onCompleted -- real mixxx's own
    /// startup rescan, run here since this model (not a pure C++ class)
    /// already has the real IInteractive access the scan-summary dialog
    /// needs. Guarded so it only actually runs once no matter how many
    /// times QML happens to call it.
    Q_INVOKABLE void runStartupRescanIfNeeded();

    /// Real mixxx: WSearchLineEdit's own remembered-searches list, backing
    /// the search box's completions dropdown and Up/Down history cycling
    /// -- see IMixHouseLibraryConfiguration::searchHistory's own comment.
    /// Real mixxx: a SECOND click on an already-selected row starts
    /// editing that cell instead of needing a double-click -- see
    /// IMixHouseLibraryConfiguration::editMetadataAfterClick's own comment.
    Q_INVOKABLE bool editMetadataAfterClick() const { return libraryConfiguration()->editMetadataAfterClick(); }
    Q_INVOKABLE bool searchCompletionsEnabled() const { return libraryConfiguration()->searchCompletionsEnabled(); }
    Q_INVOKABLE bool searchHistoryShortcutsEnabled() const { return libraryConfiguration()->searchHistoryShortcutsEnabled(); }
    Q_INVOKABLE QStringList searchHistory() const { return libraryConfiguration()->searchHistory(); }
    Q_INVOKABLE void addSearchHistoryEntry(const QString& text) { libraryConfiguration()->addSearchHistoryEntry(text); }

    /// Real mixxx: WTrackTableView's own double-click dispatch, driven by
    /// DlgPrefLibrary's "Track Double-Click Action" (see
    /// IMixHouseLibraryConfiguration::trackDoubleClickAction's own
    /// comment) -- what double-clicking a track cell in
    /// MixHouseLibraryPanel.qml actually does, replacing the previously
    /// hardcoded-to-loadTrackToProject behavior.
    Q_INVOKABLE void handleTrackDoubleClicked(int row);

    /// The start of mixxx's real WTrackMenu -- Add to Playlist/Crate, Select
    /// Color, Hide/Unhide. Real writes into the same live SQLite database
    /// tracks() already reads (via IMixHouseLibrary::ensureWritable(), the
    /// same write path addTracksFromFolder() already proved out), matched
    /// column-for-column against mixxx's own trackdao.cpp/playlistdao.cpp/
    /// cratestorage.cpp. All return false without touching anything if the
    /// row is out of range; all reload the current query on success so the
    /// view reflects the write immediately (a hide, for instance, needs the
    /// row gone from the Tracks view right away).
    Q_INVOKABLE QVariantList playlistsForMenu() const;
    Q_INVOKABLE QVariantList cratesForMenu() const;
    Q_INVOKABLE bool addTrackToPlaylist(int row, int playlistId);
    Q_INVOKABLE bool addTrackToCrate(int row, int crateId);
    Q_INVOKABLE bool setTrackColor(int row, int color);
    Q_INVOKABLE bool setTrackHidden(int row, bool hidden);
    /// Creates a real playlist/crate (PlaylistDAO::createPlaylist /
    /// CrateStorage::onInsertingCrate) and, matching how any DJ app's
    /// "New Playlist..." from a track's own context menu behaves, adds this
    /// row to it in the same step. Returns the new id, or -1 on failure (an
    /// empty or duplicate name).
    Q_INVOKABLE int createPlaylistWithTrack(const QString& name, int row, int parentId = -1);
    /// Playlist folders/nesting -- see MixHouseNamedItem::parentId's own
    /// comment for why this is original work, not a port.
    Q_INVOKABLE int createPlaylistFolder(const QString& name, int parentId);
    Q_INVOKABLE bool movePlaylistToFolder(int playlistId, int newParentId);
    /// Manual reordering -- see IMixHouseLibrary::movePlaylistOrder's own
    /// comment. moveUp true = one slot earlier, false = one slot later.
    Q_INVOKABLE bool movePlaylistOrder(int playlistId, bool moveUp);
    Q_INVOKABLE bool movePlaylist(int playlistId, int parentId, int beforeId);
    Q_INVOKABLE int createCrateWithTrack(const QString& name, int row);
    Q_INVOKABLE bool renamePlaylist(int playlistId, const QString& name);
    Q_INVOKABLE bool renameCrate(int crateId, const QString& name);
    Q_INVOKABLE bool setPlaylistLocked(int playlistId, bool locked);
    Q_INVOKABLE bool setCrateLocked(int crateId, bool locked);
    /// Both ask for confirmation first (IInteractive::question) -- real
    /// mixxx's own DAOs don't guard this themselves, the UI layer does, so
    /// that's where it belongs here too. Fire-and-forget from QML: the
    /// actual delete (and the reload after it) happens asynchronously once
    /// the user answers.
    Q_INVOKABLE void deletePlaylistWithConfirm(int playlistId, const QString& name, bool isFolder = false);
    Q_INVOKABLE void deleteCrateWithConfirm(int crateId, const QString& name);
    /// Batch versions -- the sidebar previously had no way to select more
    /// than one playlist/crate at all, so deleting several (e.g. the many
    /// "Rekordbox: ..." playlists a single XML import can create) meant one
    /// at a time. ONE confirm dialog naming the real count, same pattern as
    /// removeTracksFromLibraryWithConfirm's batch version; a locked item is
    /// silently skipped (isPlaylistLocked/isCrateLocked, same rule as the
    /// single-item version), not counted as a failure for the rest.
    Q_INVOKABLE void deletePlaylistsWithConfirm(const QVariantList& playlistIds);
    Q_INVOKABLE void deleteCratesWithConfirm(const QVariantList& crateIds);

    /// "Open in File Browser" -- reveals and selects the actual file in
    /// Finder (`open -R`), not Qt.openUrlExternally on the file itself, which
    /// launches the file's default PLAYER instead of showing where it lives.
    Q_INVOKABLE void revealInFileBrowser(const QString& filePath) const;

    /// Round 2 of the real WTrackMenu: Auto DJ queueing, rating, BPM adjust/
    /// lock, the Clear submenu, and the two destructive actions (Purge, Move
    /// to Trash), each real SQL against IMixHouseLibrary -- see mixhouselibrary.cpp.
    Q_INVOKABLE bool addTrackToAutoDj(int row, int loc);
    Q_INVOKABLE bool setTrackRating(int row, int rating);
    /// Real mixxx's editable library cells (basesqltablemodel.cpp:706-733)
    /// -- see IMixHouseLibrary::setTrackComment's own comment.
    Q_INVOKABLE bool setTrackComment(int row, const QString& text);
    Q_INVOKABLE bool setTrackGrouping(int row, const QString& text);
    Q_INVOKABLE bool setTrackComposer(int row, const QString& text);
    Q_INVOKABLE bool setTrackAlbumArtist(int row, const QString& text);
    Q_INVOKABLE bool setTrackNumber(int row, const QString& text);
    /// False (and no write at all) when the track's BPM is locked -- mirrors
    /// real mixxx graying these actions out rather than silently ignoring
    /// the click.
    Q_INVOKABLE bool scaleTrackBpm(int row, double factor);
    Q_INVOKABLE bool setTrackBpmLocked(int row, bool locked);
    Q_INVOKABLE bool clearTrackField(int row, int field);

    /// Whole-selection versions of the eight actions above. Real mixxx's
    /// WTrackMenu runs every one of these over the ENTIRE current selection
    /// (applyTrackPointerOperation/newTrackPointerIterator,
    /// wtrackmenu.cpp:1368-1384) -- this port's menu wiring always passed
    /// just the single right-clicked row instead, so selecting several
    /// tracks and picking Rating/Color/BPM-lock/BPM-scale/Clear/Add-to-
    /// Playlist/Add-to-Crate/Add-to-Auto-DJ silently changed only one of
    /// them. Each of these loops the resolved track ids through the exact
    /// same real per-track IMixHouseLibrary call the single-row version
    /// above already uses (matching mixxx's own per-track iteration, not a
    /// single batched SQL statement), then reloads once at the end. Returns
    /// true if at least one track in the selection was actually affected
    /// (a locked track being skipped by scaleTracksBpm/clearTracksField is
    /// expected, not a failure).
    Q_INVOKABLE bool addTracksToPlaylist(const QVariantList& rows, int playlistId);
    Q_INVOKABLE bool addTracksToCrate(const QVariantList& rows, int crateId);
    Q_INVOKABLE bool setTracksColor(const QVariantList& rows, int color);
    Q_INVOKABLE bool addTracksToAutoDj(const QVariantList& rows, int loc);
    /// "Add to Auto DJ Queue (replace)" specifically: real mixxx pops
    /// "Replace current Auto DJ queue?" first (wtrackmenu.cpp:2825-2853) --
    /// this port ran the replace immediately and unconditionally, so one
    /// misclick could silently wipe a real queue with no way to cancel.
    Q_INVOKABLE void addTracksToAutoDjReplaceWithConfirm(const QVariantList& rows);
    Q_INVOKABLE bool setTracksRating(const QVariantList& rows, int rating);
    //! Edit Tags: only the keys in `fields` change (see IMixHouseLibrary::setTrackTags).
    Q_INVOKABLE bool editTags(const QVariantList& rows, const QVariantMap& fields, bool writeFile);
    //! The Edit Tags fields of one row.
    Q_INVOKABLE QVariantMap tagsForRow(int row) const;
    Q_INVOKABLE bool scaleTracksBpm(const QVariantList& rows, double factor);
    Q_INVOKABLE bool setTracksBpmLocked(const QVariantList& rows, bool locked);
    Q_INVOKABLE bool clearTracksField(const QVariantList& rows, int field);
    /// Backs the Clear submenu's real mixxx behavior of graying out
    /// "BPM and Beatgrid" (and, by extension, "All") whenever ANY track in
    /// the selection is BPM-locked (wtrackmenu.cpp:1137-1153) -- checked
    /// once at menu-open time over the whole selection, not just the
    /// clicked row.
    Q_INVOKABLE bool isAnyTrackBpmLocked(const QVariantList& rows) const;
    /// Both ask for confirmation first (IInteractive::question), same
    /// pattern as deletePlaylistWithConfirm. Move-to-trash uses Qt's own
    /// QFile::moveToTrash() -- the file goes to the OS Trash, not unlinked
    /// outright -- then purges the row, matching real mixxx's
    /// slotRemoveFromDisk() exactly (delete-to-trash, then purge).
    Q_INVOKABLE void purgeTrackWithConfirm(int row, const QString& title);
    Q_INVOKABLE void moveTrackFileToTrashWithConfirm(int row, const QString& title);

    /// Backs "Remove from Collection"'s visibility in the track menu's
    /// "Collection Actions" submenu: true only when the active view is a
    /// specific Playlist or Crate -- not the flat Tracks view, Hidden,
    /// Missing, or a Computer folder.
    Q_INVOKABLE bool isViewingCollection() const { return m_query.playlistId >= 0 || m_query.crateId >= 0; }

    /// The "Collection Actions" submenu's four batch actions -- each resolves
    /// every given row to its real library track id BEFORE doing anything
    /// else (rows can be stale by the time a confirm dialog resolves; ids
    /// never are), matched against real mixxx's own WTrackMenu/DAO behavior
    /// the same way every single-row method above already is.
    ///
    /// "Remove from Collection": PlaylistDAO::removeTracksFromPlaylist,
    /// scoped to whichever single playlist is currently active
    /// (isViewingCollection() must be true -- a no-op returning false
    /// otherwise). No confirmation, matching real mixxx's own un-confirmed
    /// WTrackMenu::slotRemove. Track stays in the library; file stays on
    /// disk; every other playlist/crate membership is untouched.
    Q_INVOKABLE bool removeTracksFromCurrentCollection(const QVariantList& rows);
    /// Same as removeTracksFromCurrentCollection(), except it resolves
    /// against EVERY track/position matching the current view (search text,
    /// key filters, the active playlist/crate) via library()->
    /// matchingTracks(), never against the loaded/rendered page -- the fix
    /// for "Select All" inside a playlist/crate. See
    /// removeAllMatchingFromLibraryWithConfirm()'s own comment for why this
    /// distinction exists at all.
    Q_INVOKABLE bool removeAllMatchingFromCurrentCollection();
    /// "Remove from Library": a real removal (TrackDAO::onPurgingTracks,
    /// i.e. IMixHouseLibrary::purgeTracks) -- the library row and every
    /// playlist/crate/cue/analysis reference to it are gone for good, though
    /// the audio file itself is untouched on disk. This used to soft-hide
    /// (mixxx_deleted=1, mirroring mixxx's own "Hide") instead, WITHOUT a
    /// confirmation dialog -- which is exactly what turned one no-confirm
    /// click on a large shift-click selection into an entire real library
    /// going invisible with no way back (this app never wired a way to
    /// browse into the Hidden state, and re-adding the same file by path
    /// silently no-ops against the already-known hidden row). "Remove" now
    /// means removed, and -- being real and less reversible than a soft hide
    /// -- it gets the same ONE-confirmation-naming-the-count treatment as
    /// "Delete from Disk" below.
    Q_INVOKABLE void removeTracksFromLibraryWithConfirm(const QVariantList& rows);
    /// Same as removeTracksFromLibraryWithConfirm(), except the track list it
    /// confirms and purges is EVERY track matching the current view (search
    /// text, key filters, the active playlist/crate, or the whole flat
    /// library) via library()->matchingTracks(query) -- never the rows the
    /// QML "Select All" happened to have actually loaded into m_tracks.
    ///
    /// Real bug this fixes, confirmed against the reference library: rows()'s
    /// own query.limit (2000) truncates m_tracks, but QML's selectAllRows()
    /// built its selection from library()->trackCount() -- the GLOBAL,
    /// unfiltered row count -- then resolveTrackIds() silently dropped every
    /// index past whatever page was actually loaded. "Select All" + "Remove
    /// from Library" looked like it selected and removed everything (the
    /// confirm dialog even named a plausible count) but only ever purged the
    /// first ~2000 real matches per click; repeating it kept eating the next
    /// page. That is exactly how a real 33,688-track library ended up
    /// at 309 tracks. QML must call this (never the row-based version above)
    /// whenever the user's selection means "everything in this view", however
    /// many rows that actually is.
    Q_INVOKABLE void removeAllMatchingFromLibraryWithConfirm();
    /// "Remove from Collection and Library": both of the above, as ONE
    /// action behind ONE confirmation dialog naming the selection's track
    /// count. If no playlist is currently active there's nothing to remove
    /// from, so this behaves exactly like removeTracksFromLibraryWithConfirm()
    /// alone -- still behind the same one confirmation.
    Q_INVOKABLE void removeFromCollectionAndLibraryWithConfirm(const QVariantList& rows);
    /// "Delete from Disk": generalizes moveTrackFileToTrashWithConfirm to a
    /// whole selection -- ONE confirmation dialog naming the track count,
    /// then each file is moved to the real OS Trash and, only for files
    /// that actually moved, its row is purged (TrackDAO::onPurgingTracks) --
    /// exactly the single-track version's "only purge what's confirmed
    /// gone" rule, just batched.
    Q_INVOKABLE void moveTracksToTrashWithConfirm(const QVariantList& rows);

    /// The Computer sidebar's real "Add Folder to Library..." -- mixxx's own
    /// equivalent is the Computer tree's right-click "Add to Library"
    /// (library/browse/browsefeature.cpp) plus Preferences > Library's "Add"
    /// button, both of which funnel into Library::requestAddDir() and then a
    /// separate library scan. This app's own addTracksFromFolder() already
    /// does the directory scan AND the insert in one synchronous step, so
    /// this is the one-call equivalent of that whole real flow -- reports
    /// the real ScanResult back via IInteractive, in the spirit of mixxx's
    /// own "Library scan finished" summary dialog. `folder` empty means the
    /// picker was cancelled; a no-op, not an error.
    Q_INVOKABLE void addFolderToLibrary(const QString& folder);
    //! GlideMix: files and folders dropped from Finder. Into the library, or
    //! with a playlist/crate id (kDropCurrentList: the list being shown) also
    //! into that list. Folders are taken with every subfolder.
    Q_INVOKABLE void addDroppedUrls(const QVariantList& urls, int playlistId = -1, int crateId = -1);
    static constexpr int kDropCurrentList = -2;
    //! GlideMix: dropped onto Playlists (or Crates): each folder becomes a new
    //! playlist (crate) named after it, loose files one named after their
    //! folder -- as in rekordbox. Tracks already in the collection are listed,
    //! not added twice.
    Q_INVOKABLE void addDroppedUrlsAsNewLists(const QVariantList& urls, bool crates, int parentId = -1);

    /// The real "reason we added Rekordbox XML import" : rekordbox's
    /// own beatgrid/BPM/key analysis is genuinely expensive to produce (a
    /// full real collection re-analysis can take rekordbox itself many
    /// hours) -- reading it back out of an already-exported collection XML
    /// is pure text parsing, seconds not hours, and reuses analysis the library
    /// already trusts instead of re-running this app's own analyzer. Only
    /// ever writes to tracks ALREADY in this library by real file path --
    /// see IMixHouseLibrary::importFromRekordboxXml's own comment for why a
    /// real rekordbox collection XML referencing tens of thousands of other
    /// files must never silently create library rows for them. `path` empty
    /// means the picker was cancelled -- a no-op, not an error.
    Q_INVOKABLE void importFromRekordboxXml(const QString& path);

    /// Roadmap Phase 5, the library table's "Waveform Overview" column.
    /// Resolves + decodes the track's real analysis file on first request
    /// (cached by track id after that -- file IO + zlib + a hand-rolled
    /// protobuf decode once per track is real work, not something to redo on
    /// every repaint/resize), then resamples it down to `bucketCount` columns
    /// (mixxx's own "max within this pixel's window" rule). Each returned
    /// entry is {low,mid,high,all}, 0-255 -- `all` (the broadband envelope)
    /// drives the drawn bar's height, low/mid/high drive its color, exactly
    /// mixxx's own real split (see mixhousewaveform.h). Empty list if the
    /// row is out of range or the track has no cached analysis yet (an
    /// ordinary, expected state for anything not yet analyzed -- never
    /// fabricated as a flat line).
    Q_INVOKABLE QVariantList overviewBuckets(int row, int bucketCount);
    /// GlideMix: the cue points over a row's waveform overview -- the song's
    /// own (Lexicon / rekordbox), else the phrase cues the rekordbox XML
    /// export gives it. [{ pos (0-1), color, letter, hot }]
    Q_INVOKABLE QVariantList overviewCues(int row);
    //! A song's cue points: its own, else hot cues at its phrases.
    std::vector<MixHouseCue> cuesFor(const MixHouseTrack& track, bool* generated = nullptr) const;

    /// Roadmap Phase 7 -- the real analysis engine (mixhouseanalyzer.h, a
    /// port of real mixxx's own default Queen Mary BPM/key detectors).
    /// Asks for confirmation first (this OVERWRITES any existing bpm/key/
    /// beatgrid, same "Reanalyze" semantics as real mixxx's own menu item),
    /// then reports the real result via IInteractive, same pattern as every
    /// other write action here.
    Q_INVOKABLE void analyzeTrackWithConfirm(int row, const QString& title);
    /// The whole-selection version -- the single-row one above predates the
    /// shift-click multiselect work and was never updated for it, which is
    /// exactly why selecting several tracks and choosing Analyze only ever
    /// analyzed one. ONE confirmation naming the count, same pattern as the
    /// Collection Actions batch methods, then delegates to the SAME real
    /// background analyzer pool the toolbar's own Analyze button uses (see
    /// startBatchAnalysis's own comment) -- this used to run every track
    /// one at a time in a blocking for-loop with no yield point at all,
    /// which is the real freeze we hit analyzing a 1372-track selection
    /// from this exact menu item; not true anymore.
    Q_INVOKABLE void analyzeTracksWithConfirm(const QVariantList& rows);

    /// The "Analysis" stats bar above the header (real mixxx: DlgAnalysis/
    /// AnalysisFeature, library/analysis/dlganalysis.cpp) -- New/All radio
    /// + Select All + a real, live-progress, stoppable batch run, now
    /// backed by IMixHouseLibrary::startBackgroundAnalysis's own genuine
    /// multi-threaded worker pool (ported from real mixxx's own
    /// TrackAnalysisScheduler + AnalyzerThread, one QSqlDatabase connection
    /// per worker thread -- ping-ponged in from analyzeTracksWithConfirm's
    /// own real freeze report). This model layer just mirrors
    /// isAnalyzing()/analysisProgress() into these Q_PROPERTYs; MixHouseLibrary
    /// owns the actual threads. One real, deliberate simplification remains,
    /// disclosed rather than silently diverging: real mixxx's own progress
    /// percentage is a genuine sub-track decode-progress average
    /// (AnalyzerProgress); this app's own analyzeTrackFile() is a single
    /// atomic call with no partial-progress hook, so analyzeProcessed/
    /// analyzeTotal below is just tracks-completed/tracks-total, same
    /// numbers, coarser resolution -- and real mixxx's Analysis view is its
    /// own separate, separately-searched track list
    /// (AnalysisLibraryTableModel), while this instead operates directly on
    /// the already-visible Tracks table via the SAME row-selection
    /// mechanism Cmd+A/Collection Actions already use, so what's being
    /// analyzed is visibly selected/highlighted in place rather than hidden
    /// in a separate list.
    /// "New" mirrors real mixxx's own recentFilter() exactly: added within
    /// the last 7 days (library/analysis/analysislibrarytablemodel.cpp).
    Q_PROPERTY(bool analyzing READ analyzing NOTIFY analyzingChanged)
    //! GlideMix: how far the rekordbox XML has been read, 0-100 (-1 = not reading).
    Q_PROPERTY(int rekordboxLoadProgress READ rekordboxLoadProgress NOTIFY rekordboxLoadProgressChanged)
    Q_PROPERTY(bool duplicatesView READ duplicatesView NOTIFY loaded)
    Q_PROPERTY(int duplicateGroups READ duplicateGroups NOTIFY loaded)
    /// A few seconds of "Analysis finished/stopped" text after a batch.
    Q_PROPERTY(QString analysisMessage READ analysisMessage NOTIFY analysisMessageChanged)
    Q_PROPERTY(int analyzeProcessed READ analyzeProcessed NOTIFY analyzeProgressChanged)
    Q_PROPERTY(int analyzeTotal READ analyzeTotal NOTIFY analyzeProgressChanged)
    /// Library click-to-preview (MixHouse DJ's OverviewDelegate, ported --
    /// see previewTrackAt). previewTrackId is -1 until something is loaded;
    /// it stays set after stopping so the playhead remains visible, the same
    /// as the fork's delegate kept drawing it while the track sat in the
    /// preview deck.
    Q_PROPERTY(int previewTrackId READ previewTrackId NOTIFY previewStateChanged)
    Q_PROPERTY(bool previewPlaying READ previewPlaying NOTIFY previewStateChanged)
    Q_PROPERTY(double previewPosition READ previewPosition NOTIFY previewPositionChanged)

    /// Backs the status line's real-time "Scanning..." state -- see
    /// IMixHouseLibrary::startBackgroundScan's own comment for the real
    /// freeze this whole background-scan mechanism exists to fix, and
    /// MixHouseLibraryPanel.qml's own status-line binding for where this is
    /// actually shown. scanStatusText is only meaningful while scanning is
    /// true; the panel falls back to the ordinary statusMessage() the rest
    /// of the time.
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    Q_PROPERTY(QString scanStatusText READ scanStatusText NOTIFY scanProgressChanged)

    /// Row indices (into the CURRENTLY LOADED page, same rows Cmd+A
    /// selects) matching the "New" (added within the real 7-day
    /// recentFilter() window) or "All" (every loaded row) scope.
    Q_INVOKABLE QVariantList rowsForAnalysisScope(bool newOnly) const;
    bool analyzing() const;
    int analyzeProcessed() const;
    int analyzeTotal() const;
    int previewTrackId() const { return m_previewTrackId; }
    bool previewPlaying() const { return m_previewPlaying; }
    double previewPosition() const { return m_previewPosition; }
    /// Click on a row's Waveform Overview at `fraction` (0..1): starts
    /// previewing that track from there, or -- if it's already the preview
    /// track and playing -- jumps there via the fork's coalesced 90ms seek.
    Q_INVOKABLE void previewTrackAt(int row, double fraction);
    Q_INVOKABLE void stopPreview();
    /// Preview from the next phrase start of this row's track: after the
    /// current position while it is previewing, otherwise from the start.
    Q_INVOKABLE void previewNextPhrase(int row);
    Q_INVOKABLE void startBatchAnalysis(const QVariantList& rows);
    Q_INVOKABLE void stopBatchAnalysis();
    bool scanning() const;
    QString scanStatusText() const;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int trackCount() const;
    /// The TRUE number of tracks matching the current view (filter/key
    /// filters/playlist/crate), with no page cap -- unlike trackCount()
    /// above, which is the whole library's global total regardless of what's
    /// currently being viewed. This is what a QML "Select All" must size its
    /// selection against; see removeAllMatchingFromLibraryWithConfirm()'s own
    /// comment for what went wrong using trackCount() for that instead.
    Q_INVOKABLE int matchingCount() const;
    bool available() const;
    QString statusMessage() const;
    int sortKey() const { return static_cast<int>(m_query.sort); }
    bool sortAscending() const { return m_query.ascending; }
    int uiColumnCount() const { return m_uiColumnCount; }
    void setUiColumnCount(int n);

signals:
    void analysisMessageChanged();
    void loaded();
    void rekordboxLoadProgressChanged();
    void uiColumnCountChanged();
    void analyzingChanged();
    void analyzeProgressChanged();
    void previewStateChanged();
    void previewPositionChanged();
    void scanningChanged();
    void scanProgressChanged();
    void libraryRowHeightChanged();
    void greyOutPlayedTracksChanged();
    void searchDebounceMsChanged();
    void libraryFontChanged();
    void bpmPrecisionChanged();
    void keyNotationChanged();
    void waveformOverviewTypeChanged();
    void showWaveformMinuteMarkersChanged();
    //! The waveform overview data behind the rows changed (e.g. the rekordbox
    //! index finished loading): the overview column should fetch its buckets again.
    void overviewDataChanged();
    void hdWaveRevisionChanged();

public:
    QString analysisMessage() const { return m_analysisMessage; }
    void setAnalysisMessage(const QString& text)
    {
        if (m_analysisMessage != text) {
            m_analysisMessage = text;
            emit analysisMessageChanged();
        }
    }

private:
    void ensurePreviewPlayer();
    void updatePreviewPosition();

    std::unique_ptr<MixHousePreviewPlayer> m_previewPlayer;
    QTimer* m_previewSeekTimer = nullptr;
    QTimer* m_previewPollTimer = nullptr;
    double m_pendingPreviewSeek = -1.0;
    int m_previewTrackId = -1;
    bool m_previewPlaying = false;
    double m_previewPosition = 0.0;

    /// Resolves a QML-selection's row indices to real library track ids,
    /// silently skipping any row that's out of range (e.g. a stale index
    /// from before a reload) rather than failing the whole batch over it.
    QList<int> resolveTrackIds(const QVariantList& rows) const;
    QList<int> resolvePlaylistPositions(const QVariantList& rows) const;
    /// Shared by startBatchAnalysis (resolves QML row indices first) and
    /// analyzeTracksWithConfirm (already has real track ids resolved from
    /// BEFORE the confirm dialog was shown, same stale-index-avoidance rule
    /// every other confirm-dialog batch action in this file follows) --
    /// clears the overview cache for the whole batch up front, then hands
    /// off to the real background analyzer pool. No-op if already analyzing
    /// or trackIds is empty.
    void startBatchAnalysisForIds(const QList<int>& trackIds);
    /// The small "N tracks" / track-title label QDrag shows under the
    /// cursor while startExternalDrag()'s drag is in flight -- real mixxx
    /// instead always shows one static icon (res/images/library/
    /// ic_library_drag_and_drop.svg) regardless of what's being dragged;
    /// this port doesn't have that resource, so it draws a small themed
    /// label instead. Purely cosmetic -- QDrag works with no pixmap at all.
    static QPixmap dragPixmap(const QStringList& titles);

    // Mirrors of the real background analyzer pool's own state (see
    // IMixHouseLibrary::startBackgroundAnalysis), updated from the
    // analysisProgress/analysisFinished subscriptions set up once in
    // load() -- analyzing() itself just delegates straight to
    // library()->isAnalyzing(), same convention as scanning()/
    // library()->isScanning().
    bool m_subscribedToAnalysis = false;
    bool m_subscribedToRekordbox = false;
    int m_analyzeTotal = 0;
    int m_analyzeProcessed = 0;
    /// Rekordbox-style per-row progress column: real library.id -> percent
    /// (0-95) for every track in the running batch that hasn't finished
    /// yet. Queued tracks sit at 0, exactly like rekordbox's own queued
    /// rows; a track leaves this map the moment its result is written
    /// (IMixHouseLibrary::trackAnalysisProgress's -1), so its cell goes
    /// blank and its waveform appears. Keyed by id, not row, for the same
    /// reason m_overviewCache is.
    QHash<int, int> m_analysisProgressByTrack;
    void emitAnalysisProgressChanged(int trackId, bool allRoles = false);

    MixHouseTrackList m_tracks;
    IMixHouseLibrary::TrackQuery m_query;
    int m_uiColumnCount = 1;
    /// Decoded Waveform Overview data, cached by real library.id (NOT row --
    /// a reload can renumber rows out from under a stale row-keyed cache,
    /// but a track's own id never changes). The expensive part (file IO +
    /// zlib + protobuf decode) happens once per track for the process's
    /// lifetime; resampling to a given width stays cheap enough to redo
    /// every call.
    QHash<int, MixHouseWaveformOverview> m_overviewCache;
    QHash<int, QVariantList> m_overviewCueCache;
    /// Guards the one-time library()->libraryChanged().onNotify() subscribe
    /// in load() -- see that function's own comment for why this can't
    /// happen in the constructor.
    bool m_subscribedToLibraryChanged = false;
    bool m_subscribedToSettingsChanged = false;
    int m_keyNotation = 0;   ///< MixHouseKeyNotation the tracks were last read in
    bool m_subscribedToScan = false;
    int m_scanScanned = 0;
    int m_scanAdded = 0;
    bool m_ranStartupRescan = false;
    /// See setEmptyPlaceholder()'s own comment -- load() short-circuits to
    /// an empty result without touching the database while this is set.
    bool m_emptyPlaceholder = false;
    //! The external library the table shows ("" = this library) and which
    //! of its playlists (-1 = all).
    QString m_externalSource;
    int m_externalPlaylist = -1;
    bool m_subscribedToExternal = false;
    void leaveExternal();
    const IMixHouseExternalLibrary* externalLibrary(const QString& source) const;
    void applyOwnOverrides(MixHouseTrackList& tracks) const;
    //! GlideMix: the tracks a smart playlist picks, filtered by the search box.
    MixHouseTrackList smartPlaylistTracks(int id) const;
    MixHouseTrackList duplicateTracks();
    bool duplicatesView() const { return m_externalSource == u"duplicates"; }
    int rekordboxLoadProgress() const;
    QTimer m_rekordboxProgressTimer;
    int m_rekordboxProgress = -1;
    //! The rekordbox XML whose read result was last looked at: a file that
    //! isn't a rekordbox collection is reported once per choice.
    QString m_rekordboxCheckedPath;
    int duplicateGroups() const { return duplicatesView() ? m_duplicateGroups : 0; }
    QSet<int> m_duplicateExtras;   ///< ids of the copies that aren't the best
    int m_duplicateGroups = 0;
    MixHouseTrackList smartCandidates(const QString& source, const IMixHouseLibrary::TrackQuery& query) const;
    QSet<QString> smartPlaylistFiles(const QString& playlistRef) const;
    MixHouseTrackList playlistTracksForExport(const QString& kind, int id) const;
    //! A track from another DJ app's library: not in this library, so
    //! nothing writes to it.
    static bool isExternalTrack(const MixHouseTrack& t) { return isExternalLibraryTrack(t.id); }
    static void applyTagFields(MixHouseTrack& t, const QVariantMap& fields);
    //! The library id of the same file as an external track, or -1.
    //! The song's phrases for its cue points (rekordbox XML export).
    std::vector<MixHousePhrase> phrasesForExport(const MixHouseTrack& track) const;
    int libraryIdForFile(const QString& filePath) const;
    //! Library ids and external files of `rows`, for analysis.
    void collectAnalysisTargets(const QVariantList& rows, QList<int>& ids, QStringList& externalFiles) const;
    //! Adds external files not yet in the Music Collection (analysis results
    //! live there) and returns every id to analyze.
    QList<int> analysisTrackIds(QList<int> ids, const QStringList& externalFiles);
    bool m_subscribedToHdWaves = false;
    int m_hdWaveRevision = 0;

    //! While a dropped folder tree is being made into lists: the totals for
    //! its one summary.
    struct DropTotals {
        int scanned = 0, added = 0, skipped = 0, unreadable = 0, listed = 0, lists = 0;
    };
    std::optional<DropTotals> m_dropTotals;
    QString m_analysisMessage;
    QString m_browseFolder;
    MixHouseTrackList browseFolderTracks() const;
    QList<int> resolveOrImportTrackIds(const QVariantList& rows);
};
}
