/*
 * MixHouse DJ 2
 */
#pragma once

#include <QDir>

#include <QStringList>
#include <QHash>
#include <QVariantMap>
#include <utility>
#include <QByteArray>

#include "async/notification.h"
#include "async/channel.h"
#include "modularity/imoduleinterface.h"
#include "mixhousetypes.h"
#include "mixhousesmartplaylist.h"
#include "imixhouserekordboxphrases.h"

#include <optional>

namespace au::mixhouse {
//! GlideMix: the disk, /Users, /Volumes, a volume or the home folder -- far
//! more than a music folder; scanning one walks everything on it.
inline bool mixHouseFolderTooBroadToScan(const QString& folder)
{
    const QString path = QDir::cleanPath(folder);
#ifdef Q_OS_WIN
    // Windows: a drive (C:, C:/), a drive's Users folder, or the home folder.
    const bool driveRoot = (path.size() == 2 || path.size() == 3) && path.at(1) == QLatin1Char(':');
    const bool usersFolder = path.size() == 8 && path.at(1) == QLatin1Char(':')
                             && path.mid(2).compare(QStringLiteral("/Users"), Qt::CaseInsensitive) == 0;
    return driveRoot || usersFolder || path.compare(QDir::cleanPath(QDir::homePath()), Qt::CaseInsensitive) == 0;
#else
    const bool volumeRoot = path.startsWith(QStringLiteral("/Volumes/")) && path.count(QLatin1Char('/')) == 2;
    return path == QStringLiteral("/") || path == QDir::cleanPath(QDir::homePath()) || path == QStringLiteral("/Users")
           || path == QStringLiteral("/Volumes") || volumeRoot;
#endif
}
}

namespace au::mixhouse {
class IMixHouseLibrary : MODULE_EXPORT_INTERFACE
{
    INTERFACE_ID(IMixHouseLibrary)
public:
    virtual ~IMixHouseLibrary() = default;

    /// Fires after a write that a currently-displayed track list has no
    /// other way to learn about: Add Folder/Add Files to Library and the
    /// Rekordbox/iTunes XML importers all write straight through this
    /// interface from MixHouseActionsController (a File-menu-level
    /// controller with no reference to whatever MixHouseTrackListModel
    /// instance happens to be on screen), so none of them could ever tell an
    /// already-open Tracks view to reload -- the exact real bug behind
    /// "0 tracks" after a scan that its own log proved had genuinely added
    /// every file back. Every other write path (the track menu's own
    /// invokables) already calls load() on its own model directly and does
    /// NOT need this -- it's specifically for writes that happen with no
    /// model in hand at all.
    virtual muse::async::Notification libraryChanged() const = 0;

    /// Absolute path of the Mixxx library database this reads.
    virtual QString databasePath() const = 0;
    /// False when the database is missing or could not be opened.
    virtual bool isAvailable() const = 0;
    /// Human-readable reason when isAvailable() is false.
    virtual QString lastError() const = 0;

    /// How a track list is ordered. Maps onto real columns, so sorting happens in
    /// SQL rather than by shuffling a already-truncated page in memory. Mirrors
    /// mixxx's real library/columncache.cpp column list.
    enum class SortKey {
        Title, Artist, Album, Genre, Year, Bpm, Key, Duration, Rating, DateAdded,
        AlbumArtist, Composer, Grouping, TrackNumber, FileType, Comment, Bitrate,
        ReplayGain, TimesPlayed, LastPlayedAt,
        /// PlaylistTracks.position -- only meaningful when playlistId is set.
        /// Real mixxx playlists are ordered lists (PlaylistDAO::moveTrack)
        /// and this is their real default sort; a plain library-wide "Tracks"
        /// query has no ordering column to use this for.
        Position,
        /// Real mixxx's own column (library/columncache.cpp:94-102), hidden
        /// by default just like this port's own copy of that list. We
        /// already carry real samplerate data (MixHouseTrack::sampleRate,
        /// read for beatgrid decoding); Channels has no backing column
        /// anywhere in this schema, same as real mixxx's own
        /// basetracktablemodel.cpp:794-797 ("Not yet supported" -- the cell
        /// is always blank there too, so this isn't a regression).
        SampleRate, Channels,
        /// Whether the track has been analysed (GlideMix: the status column).
        Analyzed,
        /// GlideMix: the energy rating (1-10).
        Energy
    };

    struct TrackQuery {
        QString filter;          ///< matches title/artist/album, case-insensitive
        QStringList keyFilters;  ///< exact library.key values; empty = any key
        int playlistId = -1;     ///< -1 = whole library
        /// Real mixxx's CrateFeature::activateChild() swaps the track view to
        /// that crate's CrateTableModel -- this port's sidebar had a crate
        /// tree with no filtering wired to it at all (clicking a crate did
        /// nothing). Mutually exclusive with playlistId in practice (a view
        /// is either the flat library, one playlist, or one crate), but both
        /// fields exist independently since crate_tracks and PlaylistTracks
        /// are real, separate tables with no shared join key.
        int crateId = -1;        ///< -1 = not viewing a crate
        /// Real mixxx's MissingTableModel: a track whose underlying file no
        /// longer exists on disk (track_locations.fs_deleted = 1), see
        /// verifyTrackLocations() below. Mutually exclusive with playlistId/
        /// crateId in practice -- the sidebar's "Missing" is its own fixed
        /// node, not a playlist or crate.
        bool missingOnly = false;
        /// GlideMix: only tracks not analysed yet (the sidebar's Analyze).
        bool unanalyzedOnly = false;
        /// GlideMix: only tracks used in a project (the sidebar's History).
        bool playedOnly = false;
        /// Mixxx's Hidden Tracks feature (library.mixxx_deleted = 1).
        bool hiddenOnly = false;
        SortKey sort = SortKey::DateAdded;
        bool ascending = false;
        int limit = 0;
        /// >= 0: only this library.id (e.g. refreshing one row after it
        /// was analyzed) -- combined with every other filter as usual.
        int trackId = -1;
        /// Non-empty: only the track whose file is exactly this path
        /// (track_locations.location) -- e.g. a library track dropped on the
        /// timeline, which arrives as a file URL.
        QString location;
        /// Non-empty: only tracks whose file name without its extension is
        /// exactly this -- how a timeline clip (named after its file) finds
        /// its library track again.
        QString fileBaseName;
    };

    virtual MixHouseTrackList tracks(const TrackQuery& query) const = 0;
    virtual int trackCount() const = 0;

    /// Every real library id (and, when query.playlistId >= 0, the matching
    /// real PlaylistTracks.position) that matches this query -- the SAME
    /// filter/join logic tracks() uses, but with no `limit` applied and no
    /// row data fetched, just ids.
    ///
    /// Real bug this exists to fix, confirmed directly against the own
    /// library: tracks()'s own query.limit (2000, see its own comment) caps
    /// what actually gets loaded into the model backing the table view. A
    /// QML "Select All" that built its index range from the wrong, global
    /// library()->trackCount() (ignoring the current filter/playlist/crate
    /// entirely) then handed those indices to row-bounds-checked lookups
    /// silently dropped every index past whatever page was actually loaded
    /// -- so "Select All" + "Remove from Library" only ever removed the
    /// first ~2000 matches per click, repeatably, with the UI never
    /// indicating truncation. We hit this for real: repeated Select-
    /// All-then-Remove attempts silently walked his real 33,688-track
    /// library down to 309 tracks before anyone noticed. A bulk action that
    /// means "everything currently matching this view", however many rows
    /// that is, must resolve against this -- never against the loaded page.
    struct MatchingTracks {
        QList<int> ids;
        QList<int> playlistPositions;   ///< only meaningful when query.playlistId >= 0
    };
    virtual MatchingTracks matchingTracks(const TrackQuery& query) const = 0;

    /// Mixxx keeps three different things in the Playlists table, told apart by
    /// its `hidden` column (PlaylistDAO::HiddenType):
    ///   0 PLHT_NOT_HIDDEN -- real playlists
    ///   1 PLHT_AUTO_DJ    -- the Auto DJ queue
    ///   2 PLHT_SET_LOG    -- session history, one per date
    /// Listing them together is what turned the sidebar into 123 date rows.
    virtual MixHouseNamedList playlists() const = 0;
    /// GlideMix: the playlists `trackId` is in (the sidebar lights them up).
    virtual QList<int> playlistsContainingTrack(int trackId) const = 0;
    virtual MixHouseNamedList autoDjQueue() const = 0;
    virtual MixHouseNamedList historySessions() const = 0;
    virtual MixHouseNamedList crates() const = 0;

    /// Counts for the fixed nodes at the top of the tree.
    virtual int missingTrackCount() const = 0;
    virtual int hiddenTrackCount() const = 0;

    /// Real mixxx's LibraryScanner runs TrackDAO::verifyRemainingTracks as
    /// part of every library scan: for each track_locations row, checks
    /// QFile::exists() and sets fs_deleted accordingly (trackdao.cpp:2143) --
    /// that's what feeds missingTrackCount() above and the Missing view
    /// (TrackQuery::missingOnly). This port has no directory-hash-based
    /// incremental scanner to trigger that as a side effect (Phase 6's scan
    /// only ever inserts new tracks), so this exposes the same disk check
    /// directly, run on demand (the sidebar's Missing node, right-click
    /// "Check for missing tracks", or equivalent). Returns the number of
    /// tracks found missing after the check (== missingTrackCount()).
    virtual int verifyTrackLocations() = 0;

    struct ScanResult {
        int scanned = 0;    ///< audio files seen
        int added = 0;      ///< rows actually inserted
        int skipped = 0;    ///< already in the library
        int unreadable = 0; ///< not audio GlideMix can read (damaged, empty...): not added
        int pending = 0;    ///< watch scans: files still being written, left for the next scan
        QList<int> addedIds; ///< library ids of the rows inserted (background scans)
        bool watch = false; ///< started by the folder watcher: no summary dialog
        QString error;      ///< empty on success
    };

    /// Recursively add audio files under `folder` to the library.
    /// INSERT-only: existing rows are never modified or deleted, and the database
    /// is backed up before the first write of a session.
    virtual ScanResult addTracksFromFolder(const QString& folder) = 0;

    /// Real bug this exists to fix, confirmed against a real ~35k-track
    /// library: addTracksFromFolder() above runs entirely on the calling
    /// thread. Called synchronously from runStartupRescanIfNeeded() at
    /// panel load, a big-enough collection (especially right after a real
    /// rekordbox import adds thousands of files at once) can keep the UI
    /// thread busy long enough for macOS's own "Application Not Responding"
    /// watchdog to fire -- that is the exact freeze we hit, not a real
    /// crash. Real mixxx's own fix for the identical problem is
    /// library/scanner/libraryscanner.h: a dedicated QThread with its own
    /// DB connection, reporting back over signals instead of blocking the
    /// caller. This is a faithful-but-simplified port of that SAME core
    /// idea (own thread, own connection, async progress) -- NOT a port of
    /// mixxx's full scanner machinery (LibraryHashes directory-hash-based
    /// skip-unchanged-dirs optimization, the ScannerTask/
    /// RecursiveScanDirectoryTask class hierarchy, DbConnectionPool
    /// pooling): this app's own scan need (a flat file-extension walk,
    /// INSERT-only, no per-directory hash cache) doesn't need that
    /// machinery to stop freezing the UI, which is the one real bug this
    /// exists to fix.
    ///
    /// Starts scanning `directories` on a background thread and returns
    /// immediately. No-op (does not start a second scan) if one is already
    /// running -- same rule LibraryScanner::scan() documents for itself.
    /// scanProgress() fires periodically (scanned-so-far, added-so-far) on
    /// whichever thread connects to it -- muse::async::Channel marshals
    /// delivery across threads on its own (confirmed in kors_async's own
    /// implementation), so QML/model code can connect normally with no
    /// extra Qt::QueuedConnection plumbing. scanFinished() fires once, with
    /// the same ScanResult shape addTracksFromFolder() returns directly.
    /// `watch`: a watch-folder scan (MixHouseFolderWatcher) -- files changed
    /// in the last few seconds are still being copied or downloaded, so they
    /// are counted as `pending` and left for the next scan.
    virtual void startBackgroundScan(const QStringList& directories, bool watch = false) = 0;
    virtual bool isScanning() const = 0;
    virtual muse::async::Channel<int, int> scanProgress() const = 0;   ///< (scanned, added)
    virtual muse::async::Channel<ScanResult> scanFinished() const = 0;
    /// The File-menu's "Add File(s) to Library": the same INSERT-only logic
    /// as addTracksFromFolder, for an explicit file list instead of a whole
    /// directory tree (Audacity's own "Import..." puts a file straight onto
    /// the project timeline for editing; this instead routes it into the
    /// library, same DB path the sidebar's "Add Folder to Library" uses).
    virtual ScanResult addTracksFromFiles(const QStringList& filePaths) = 0;

    /// Real mixxx's own iTunes/Rekordbox features keep imported tracks in a
    /// separate, read-only external tree and only copy a track into the real
    /// library the moment the user touches it (library/itunes/itunesfeature.cpp,
    /// library/baseexternaltrackmodel.cpp's getOrAddTrack). This app has no
    /// such external-tree browsing UI (a real, separate, later feature), so
    /// these instead do the copy up front: every track referenced by a
    /// playlist is resolved into `library`/`track_locations` (matched by file
    /// path, inserted only if genuinely new -- the exact same rule
    /// addTracksFromFolder already follows), and each playlist becomes a
    /// real row in `Playlists`, named with a "Rekordbox: "/"iTunes: " prefix
    /// so its external origin stays visible. Real mixxx has no Rekordbox XML
    /// importer at all (only a binary USB-device *.PDB parser) -- this is
    /// implemented directly against Pioneer's publicly documented rekordbox
    /// XML collection-export schema instead of ported mixxx code.
    virtual MixHouseImportResult importFromItunesXml(const QString& xmlPath) = 0;
    virtual MixHouseImportResult importFromRekordboxXml(const QString& xmlPath) = 0;

    /// The start of the real WTrackMenu's write actions, matched column-for-
    /// column against mixxx's own DAOs. All back up the database on the first
    /// write of a session (backupOnce(), already proven by addTracksFromFolder).
    /// Appends to the end of the playlist/crate, mirroring
    /// PlaylistDAO::appendTracksToPlaylist / CrateStorage::onAddingCrateTracks.
    virtual bool addTrackToPlaylist(int trackId, int playlistId) = 0;
    /// CrateStorage::onAddingCrateTracks is INSERT OR IGNORE -- already-in-crate
    /// is a silent no-op there, not an error, and stays that way here.
    virtual bool addTrackToCrate(int trackId, int crateId) = 0;
    /// `color` is 0xRRGGBB, or -1 for "no color" (written as SQL NULL, matching
    /// how it's read back and matching mixxx's own RgbColor::toQVariant on an
    /// empty std::optional).
    virtual bool setTrackColor(int trackId, int color) = 0;
    /// TrackDAO::hideTracks / unhideTracks: library.mixxx_deleted = 1 / 0.
    virtual bool setTrackHidden(int trackId, bool hidden) = 0;
    /// GlideMix: the track was loaded into a project -- one more play, and
    /// now as its last played time (the sidebar's History).
    virtual bool markTrackPlayed(int trackId) = 0;

    /// PlaylistDAO::createPlaylist / CrateStorage::onInsertingCrate. Returns
    /// the new row's real id, or -1 on failure (an empty/duplicate name, a
    /// write error) -- there is no "no playlists yet" case in real mixxx
    /// either, since Add to Playlist always offers this at the bottom of its
    /// submenu, not only when the list is empty.
    virtual int createPlaylist(const QString& name, int parentId = -1) = 0;
    virtual int createCrate(const QString& name) = 0;
    /// Playlist folders/nesting -- see MixHouseNamedItem::parentId's own
    /// comment for why this is original work, not a port (real mixxx's own
    /// Playlists table has no folder concept). A folder is a real Playlists
    /// row (is_folder=1) that's never expected to hold tracks itself --
    /// enforced by the UI simply never offering "Add to Playlist" for one,
    /// the same app-level-only enforcement locked playlists already use,
    /// not a schema CHECK constraint. Returns the new row's real id, or -1
    /// on failure, same convention as createPlaylist() above.
    virtual int createPlaylistFolder(const QString& name, int parentId) = 0;
    /// Moves an existing playlist OR folder under a new parent folder (-1 =
    /// top level). Real rekordbox: drag a playlist/folder onto another
    /// folder in the tree (manual p.52). Refuses (returns false) if
    /// `newParentId` is `playlistId` itself or, transitively, one of its
    /// own descendants -- that would create a cycle a tree can't represent.
    virtual bool movePlaylistToFolder(int playlistId, int newParentId) = 0;
    /// Manual reordering among siblings (same real parent) -- default
    /// stays alphabetical, but "this ordering should be via right click."
    /// See MixHouseLibrary::movePlaylistOrder's own comment for the real
    /// mechanism (a dedicated sort_order column, deliberately not a reuse
    /// of real mixxx's own pre-existing, differently-purposed `position`
    /// column). moveUp true moves one slot earlier, false moves one slot
    /// later; false (no-op, not an error) if already at that end. Real
    /// drag-to-reorder is a genuine follow-up, not built yet.
    virtual bool movePlaylistOrder(int playlistId, bool moveUp) = 0;
    /// Drag and drop in the sidebar, as in rekordbox: the playlist or folder
    /// goes into `parentId` (-1 = top level), just before its sibling
    /// `beforeId` (-1 = at the end). Refuses a move into its own subtree.
    virtual bool movePlaylist(int playlistId, int parentId, int beforeId) = 0;
    /// PlaylistDAO::renamePlaylist / CrateStorage::onUpdatingCrate (name only).
    virtual bool renamePlaylist(int playlistId, const QString& name) = 0;
    virtual bool renameCrate(int crateId, const QString& name) = 0;
    /// PlaylistDAO::deletePlaylist / CrateStorage::onDeletingCrate: removes
    /// the row AND its membership rows (PlaylistTracks / crate_tracks).
    /// Real mixxx does not ask for confirmation in the DAO itself -- that's
    /// the caller's job, done here in the QML layer via IInteractive before
    /// this is ever called.
    virtual bool deletePlaylist(int playlistId) = 0;
    virtual bool deleteCrate(int crateId) = 0;

    /// PlaylistDAO::setPlaylistLocked / CrateStorage's own locked column
    /// (see MixHouseNamedItem::locked's own comment for the real widgets
    /// this mirrors). A locked collection blocks rename/delete/add/remove --
    /// enforced directly in this layer's own methods below, not merely by a
    /// disabled menu item, so it holds regardless of which caller reaches it.
    virtual bool setPlaylistLocked(int playlistId, bool locked) = 0;
    virtual bool setCrateLocked(int crateId, bool locked) = 0;

    /// The distinct keys present in the collection, for key filtering.
    virtual QStringList keys() const = 0;
    /// Rating and color (0xRRGGBB or -1) the user gave a file in GlideMix, by its
    /// path; only files that have one. Lexicon / rekordbox rows show these: their
    /// own databases are read-only to GlideMix.
    virtual QHash<QString, std::pair<int, int> > ratingsAndColorsByLocation() const = 0;
    /// Edit Tags: `fields` keyed title, artist, album, albumArtist, genre, year,
    /// trackNumber, composer, grouping, comment. `writeFile` also saves them into
    /// the music file's tags (how rekordbox and Lexicon see them); `keepAsEdit`
    /// remembers them for Lexicon / rekordbox rows (tagEditsByLocation).
    virtual bool setTrackTags(int trackId, const QVariantMap& fields, bool writeFile, bool keepAsEdit) = 0;
    virtual QHash<QString, QVariantMap> tagEditsByLocation() const = 0;

    /// GlideMix: smart playlists (rules, see mixhousesmartplaylist.h), in
    /// sidebar order. Saving one with id < 0 creates it; returns its id
    /// (-1 on failure). Both notify libraryChanged.
    virtual std::vector<MixHouseSmartPlaylist> smartPlaylists() const = 0;
    virtual int saveSmartPlaylist(const MixHouseSmartPlaylist& playlist) = 0;
    virtual bool deleteSmartPlaylist(int id) = 0;

    /// The library table's "Waveform Overview" column data -- resolves the
    /// track's real `track_analysis` row (type 2, WAVESUMMARY -- the small,
    /// whole-track-spanning cache mixxx's own OverviewCache uses; NEVER the
    /// full-resolution type-1 WAVEFORM, which the library table never reads),
    /// reads its file from the real analysis storage directory (a sibling of
    /// this database file itself, matching mixxx's own
    /// AnalysisDao::getAnalysisStoragePath()), and reverses mixxx's own
    /// `qCompress()` container (a 4-byte length header + zlib deflate) via
    /// Qt's own `qUncompress()`. Returns the DECOMPRESSED protobuf bytes --
    /// decoding those into a Waveform message is mixhousewaveform.h's job,
    /// kept separate so this stays free of proto-decoding detail. Empty
    /// QByteArray if there's no such analysis row, no such file, or the file
    /// fails to decompress (corrupt) -- never throws or logs a hard error for
    /// what's an entirely expected "not analyzed yet" case for new tracks.
    virtual QByteArray overviewAnalysisBlob(int trackId) const = 0;

    /// Phase 8 cover art: the bare facts mixhousecoverart.h's TagLib extractor
    /// needs to resolve one track's cover, read straight off `library.coverart_*`
    /// (CoverInfoRelative -- see coverart.h). `filePath` is the track's own real
    /// audio file (track_locations.location); `type` is CoverInfoRelative::Type
    /// (0=NONE, 1=METADATA -- embedded in the audio file's own tags, 2=FILE -- a
    /// standalone sidecar image); `location` is only meaningful when type==FILE,
    /// a path relative to the track file's own directory. Returns false only if
    /// the track id itself doesn't resolve to a real row.
    struct CoverInfo {
        QString filePath;
        int type = 0;
        QString location;
    };
    virtual bool coverInfoForTrack(int trackId, CoverInfo& out) const = 0;

    /// Roadmap Phase 7. Real mixxx's own default analysis engine
    /// (Queen Mary DSP -- analyzer/plugins/analyzerqueenmarybeats.cpp +
    /// analyzerqueenmarykey.cpp, ported to mixhouseanalyzer.h) run against
    /// this track's real audio file: detects BPM/beatgrid and musical key,
    /// then writes `library.bpm`, `library.beats`/`beats_version`,
    /// `library.key`/`key_id` for real, same columns
    /// `TrackDAO::bindTrackLibraryValues()` writes in real mixxx. For
    /// tracks that already have a beatgrid/key (e.g. from real mixxx
    /// itself), this OVERWRITES them -- matching "Reanalyze", not "Analyze
    /// only if missing"; the caller (the track menu) is expected to ask for
    /// confirmation before invoking this on an already-analyzed track, the
    /// same way Purge/Move-to-Trash already do for their own destructive
    /// actions.
    virtual MixHouseAnalyzeResult analyzeTrack(int trackId) = 0;

    struct AnalysisSummary {
        int succeeded = 0;
        int failed = 0;
        int total = 0;
        bool stopped = false;   ///< ended by Stop Analysis
    };

    /// Real, genuine background analysis -- a pool of worker threads, each
    /// running analyzeTrack()'s own real Queen Mary DSP pipeline for one
    /// track at a time on ITS OWN dedicated QSqlDatabase connection (same
    /// rule startBackgroundScan() above established for the library scanner,
    /// after this app's own real cover-art crash from cross-thread SQL --
    /// see MixHouseLibraryScanner's own header comment for exactly why).
    /// Ported from real mixxx's own TrackAnalysisScheduler + AnalyzerThread
    /// (analyzer/trackanalysisscheduler.{h,cpp}, analyzer/analyzerthread.
    /// {h,cpp}), read directly from mixxx's real source before writing this
    /// -- NOT a port of its full machinery (no SPSC lock-free queue, no
    /// live sub-track decode-progress: this app's own analyzeTrackFile() is
    /// a single atomic call with no partial-progress hook, same disclosed
    /// simplification MixHouseTrackListModel's own startBatchAnalysis
    /// comment already made for the single-threaded queue this replaces --
    /// just genuinely PARALLEL now instead of one-at-a-time). numWorkerThreads
    /// is real mixxx's own exact formula for batch analysis, confirmed
    /// directly from library/analysis/analysisfeature.cpp ("Utilize all
    /// available cores for batch analysis of tracks"): max(1,
    /// QThread::idealThreadCount()), no cores reserved -- matching the reference machine
    ///  explicit call on this: "if mixxx says is ok 1/cpu core than do
    /// as mixxx says".
    ///
    /// Replaces the real freeze we hit running a 1372-track batch
    /// through the OLD analyzeTracksWithConfirm() for-loop (one track at a
    /// time on the main thread, no yield point at all -- worse than even
    /// the old single-timer-driven startBatchAnalysis queue, which at least
    /// yielded between tracks). No-op if a background analysis is already
    /// running. analysisProgress() fires periodically (tracks completed so
    /// far, total scheduled) on whichever thread connects -- same
    /// muse::async::Channel usage as scanProgress() above (see that
    /// comment); the ACTUAL cross-thread hop happens via a plain Qt queued
    /// signal from each worker first, same proven pattern as the scanner,
    /// so .send() itself is always called from the main thread.
    /// analysisFinished() fires once, with the real succeeded/failed tally.
    virtual void startBackgroundAnalysis(const QList<int>& trackIds) = 0;
    /// Stops dispatching new queued tracks to idle workers; any track
    /// already in flight on a worker finishes naturally (analyzeTrackFile()
    /// has no mid-decode cancellation hook, same disclosed limitation as
    /// stop() has always had) before that worker's thread winds down.
    virtual void stopBackgroundAnalysis() = 0;
    virtual bool isAnalyzing() const = 0;
    virtual muse::async::Channel<int, int> analysisProgress() const = 0;   ///< (processed, total)
    virtual muse::async::Channel<AnalysisSummary> analysisFinished() const = 0;
    /// Per-track progress for the library table's rekordbox-style progress
    /// column: (trackId, percent) with percent 0-95 while that track is on a
    /// worker (see MixHouseAnalyzerWorker::progress), then exactly one
    /// (trackId, -1) once its result has been written, or (trackId, -2) if
    /// it failed -- the "this track is no longer in flight" signal. Queued-but-not-started
    /// tracks send nothing; the table model seeds those itself. (trackId, -3):
    /// setTrackEnergy() rated a song outside any analysis batch.
    virtual muse::async::Channel<int, int> trackAnalysisProgress() const = 0;

    /// PlaylistDAO::addTracksToAutoDJQueue for a single track: Top inserts at
    /// the head via insertTracksIntoPlaylist, Bottom appends via
    /// appendTracksToPlaylist, Replace clears the queue first (from position
    /// 1 -- there is no "track already loaded" concept here) then appends.
    /// False if there is no Auto DJ playlist row at all (shouldn't happen:
    /// every real mixxx library has exactly one, hidden=1).
    virtual bool addTrackToAutoDj(int trackId, MixHouseAutoDjLoc loc) = 0;
    /// TrackDAO's rating setter, reached here the same way setTrackColor is:
    /// a plain column write. 0-5, matching mixxx's own star range.
    virtual bool setTrackRating(int trackId, int rating) = 0;
    /// Real mixxx's BaseSqlTableModel::setTrackValueForColumn()
    /// (basesqltablemodel.cpp:706-733): these five library columns are
    /// editable straight from the table cell, a plain Track::setXxx() call
    /// with no validation beyond that. This port has no in-memory Track
    /// object to route through -- these are the same kind of direct column
    /// write setTrackColor/setTrackRating above already are. Comment can
    /// already be blanked via Clear > Comment; these are the only way to
    /// set any of the five to a NEW value at all.
    virtual bool setTrackComment(int trackId, const QString& text) = 0;
    virtual bool setTrackGrouping(int trackId, const QString& text) = 0;
    virtual bool setTrackComposer(int trackId, const QString& text) = 0;
    virtual bool setTrackAlbumArtist(int trackId, const QString& text) = 0;
    virtual bool setTrackNumber(int trackId, const QString& text) = 0;
    /// Adjust BPM's Halve/Double/2:3/3:4/4:3/3:2: real mixxx recalls
    /// Beats::scale() and rewrites the beatgrid; the full beatgrid ENCODER
    /// needed to do that here is a later analysis-engine phase (see the
    /// roadmap), so this rewrites library.bpm by the same factor for now --
    /// the number visibly changes and re-sorts correctly, same column mixxx
    /// itself shows, just without touching the grid blob yet. Refuses (false)
    /// when the track's bpm_lock is set, same as real mixxx graying these
    /// actions out entirely while locked.
    virtual bool scaleTrackBpm(int trackId, double factor) = 0;
    /// The timeline's grid tools (GlideMix): rewrite a track's constant
    /// beatgrid to `bpm` with its first beat at `firstBeatSec`, and move its
    /// detected downbeat to `downbeatSec` (< 0 leaves the downbeat alone).
    /// Refuses (false) when the track's bpm_lock is set, like real mixxx's
    /// grid controls.
    /// `detector` names who set the grid in mixhouse_downbeats ("manual" for
    /// the grid tools, "rekordbox" when taken from rekordbox); empty keeps it.
    virtual bool setTrackBeatGrid(int trackId, double bpm, double firstBeatSec, double downbeatSec,
                                  const QString& detector = QString()) = 0;
    struct BeatGridEdit {
        int trackId = -1;
        double bpm = 0.0;
        double firstBeatSec = 0.0;
        double downbeatSec = -1.0;
        QString detector;
    };
    /// setTrackBeatGrid for many tracks in one transaction and one
    /// libraryChanged(); returns how many were written.
    virtual int setTrackBeatGrids(const std::vector<BeatGridEdit>& edits) = 0;
    /// Stores a song's energy (1-10) without touching its grid or key: for
    /// songs whose analysis came from rekordbox. No libraryChanged(); sends
    /// trackAnalysisProgress (trackId, -3).
    virtual bool setTrackEnergy(int trackId, double energy) = 0;

    /// MixMeister's Intro and Outro anchors of a file (seconds into it), where
    /// it mixes in and out. Keyed by the file's location so tracks from
    /// Lexicon DJ and rekordbox keep theirs too. nullopt = never moved.
    virtual std::optional<std::pair<double, double> > trackAnchors(const QString& location) const = 0;
    /// Saves them; intro < 0 forgets them (back to the defaults).
    virtual bool setTrackAnchors(const QString& location, double introSec, double outroSec) = 0;

    /// Phrases edited in the Beatgrid editor (seconds into the file, our
    /// decoder's clock). They replace rekordbox's phrases for the track;
    /// nullopt = never edited.
    virtual std::optional<std::vector<MixHousePhrase> > trackPhraseEdits(int trackId) const = 0;
    /// GlideMix's own phrases for the track (mixhousephrasedetector.h), from
    /// its analysis; for tracks rekordbox has none for. Empty before analysis.
    virtual std::vector<MixHousePhrase> detectedPhrases(int trackId) const = 0;
    /// Saves the track's edited phrases; nullptr goes back to rekordbox's.
    virtual bool setTrackPhraseEdits(int trackId, const std::vector<MixHousePhrase>* phrases) = 0;
    /// library.bpm_lock, real mixxx's own Lock/Unlock BPM column.
    virtual bool setTrackBpmLocked(int trackId, bool locked) = 0;
    /// The real "Clear" submenu's per-field resets: Beats/Key/ReplayGain/
    /// Rating/PlayCount/Comment are plain `library` column writes; MainCue/
    /// HotCues/IntroCue/OutroCue/Loops are DELETEs from `cues` scoped by
    /// its `type` column (mixxx::CueType); Waveform deletes the cached
    /// analysis row(s) from `track_analysis`. All is every one of those
    /// EXCEPT Comment, matching ClearAllPerformanceMetadataTrackPointerOperation
    /// exactly (mixxx treats Comment as library metadata, not performance data).
    virtual bool clearTrackField(int trackId, MixHouseClearField field) = 0;
    /// TrackDAO::onPurgingTracks: removes the track_locations row, the
    /// library row, and (explicitly here, since we can't rely on this
    /// database's foreign keys cascading under our own write connection)
    /// its membership in every playlist/crate plus its cues and cached
    /// analysis. Irreversible -- the caller (QML, via IInteractive) confirms
    /// first, same as hide/delete-playlist already do.
    virtual bool purgeTrack(int trackId) = 0;

    // -----------------------------------------------------------------------
    // Batch versions for the track menu's "Collection Actions" submenu --
    // parallel to, not replacing, the single-id methods above (those still
    // back every other still-single-row menu item: Rating, Adjust BPM,
    // Color, Add to Playlist/Crate, etc.). Every id list passed in here is
    // real library track ids, already resolved by the caller BEFORE any
    // confirmation dialog was shown -- never row indices, which a reload
    // mid-dialog could renumber out from under a stale reference.

    /// PlaylistDAO::removeTracksFromPlaylistInner (playlistdao.cpp), by real
    /// PlaylistTracks.POSITION, not track id: a real mixxx playlist can
    /// contain the same track more than once (it's an ORDERED LIST, not a
    /// set, unlike a crate), so track_id alone can't tell two occurrences of
    /// the same track apart -- removing one used to delete every occurrence.
    /// The track stays in the library, the file stays on disk, and
    /// membership in every other playlist/crate is untouched.
    virtual bool removeTracksFromPlaylist(int playlistId, const QList<int>& positions) = 0;
    /// CrateStorage's own removal counterpart (CrateTableModel::removeTracks
    /// in real mixxx) -- not reachable from the UI yet, since crates aren't
    /// wired into TrackQuery filtering above (no crateId field, and the
    /// sidebar's crate rows don't filter the track view), but added now so
    /// this DAO-equivalent layer is complete and ready once that wiring
    /// exists, the same spirit as purgeTrack being kept even after its own
    /// menu item was retired.
    virtual bool removeTracksFromCrate(int crateId, const QList<int>& trackIds) = 0;
    /// TrackDAO::hideTracks/unhideTracks, generalized to a list -- the exact
    /// same per-id UPDATE setTrackHidden() runs, batched into one statement.
    virtual bool setTracksHidden(const QList<int>& trackIds, bool hidden) = 0;
    /// TrackDAO::onPurgingTracks, generalized to a list -- the same explicit
    /// cleanup purgeTrack() runs per id, batched into one statement per
    /// table instead of one DELETE per track.
    virtual bool purgeTracks(const QList<int>& trackIds) = 0;
};
}
