/*
 * MixHouse DJ 2
 */
#pragma once

#include <QHash>
#include <QSqlDatabase>
#include <QThread>
#include <atomic>
#include <memory>
#include <vector>

#include "async/asyncable.h"
#include "modularity/ioc.h"

#include "mixhouse/imixhouselibrary.h"
#include "mixhouse/imixhouselibraryconfiguration.h"
#include "mixhouse/imixhouserekordboxphrases.h"
#include "mixhouse/imixhouselexicon.h"

namespace au::mixhouse {
class MixHouseLibraryScanner;
class MixHouseAnalyzerWorker;
struct MixHouseAnalysisResult;

class MixHouseLibrary : public IMixHouseLibrary, public muse::async::Asyncable
{
    // Applies the persisted fuzzy-BPM-search setting once at startup (see
    // init()) -- GlobalInject since IMixHouseLibraryConfiguration is
    // registered via globalIoc(), not this class's own context-scoped ioc().
    muse::GlobalInject<IMixHouseLibraryConfiguration> libraryConfiguration;

public:
    // GlideMix: analysis takes rekordbox's / Lexicon's beat grid when they
    // have one (set by the module: both are per-context exports).
    void setExternalAnalysis(std::shared_ptr<IMixHouseRekordboxPhrases> rekordbox, std::shared_ptr<IMixHouseLexicon> lexicon)
    {
        m_rekordboxPhrases = std::move(rekordbox);
        m_lexicon = std::move(lexicon);
    }

private:
    std::shared_ptr<IMixHouseRekordboxPhrases> m_rekordboxPhrases;
    std::shared_ptr<IMixHouseLexicon> m_lexicon;
    IMixHouseRekordboxPhrases* rekordboxPhrases() const { return m_rekordboxPhrases.get(); }
    IMixHouseLexicon* lexicon() const { return m_lexicon.get(); }

public:
    MixHouseLibrary() = default;
    ~MixHouseLibrary() override;

    void init();

    muse::async::Notification libraryChanged() const override;

    QString databasePath() const override;
    bool isAvailable() const override;
    QString lastError() const override;

    MixHouseTrackList tracks(const TrackQuery& query) const override;
    int trackCount() const override;
    MatchingTracks matchingTracks(const TrackQuery& query) const override;
    MixHouseNamedList playlists() const override;
    QList<int> playlistsContainingTrack(int trackId) const override;
    MixHouseNamedList autoDjQueue() const override;
    MixHouseNamedList historySessions() const override;
    MixHouseNamedList crates() const override;
    int missingTrackCount() const override;
    int unanalyzedTrackCount() const override;
    QList<int> takeChangedTrackIds() override;
    void installChangeTriggers();
    int hiddenTrackCount() const override;
    int verifyTrackLocations() override;
    ScanResult addTracksFromFolder(const QString& folder) override;
    ScanResult addTracksFromFiles(const QStringList& filePaths) override;
    void startBackgroundScan(const QStringList& directories, bool watch = false) override;
    bool isScanning() const override;
    muse::async::Channel<int, int> scanProgress() const override;
    muse::async::Channel<ScanResult> scanFinished() const override;
    MixHouseImportResult importFromItunesXml(const QString& xmlPath) override;
    MixHouseImportResult importFromRekordboxXml(const QString& xmlPath) override;
    QStringList keys() const override;
    QHash<QString, std::pair<int, int> > ratingsAndColorsByLocation() const override;
    bool setTrackTags(int trackId, const QVariantMap& fields, bool writeFile, bool keepAsEdit) override;
    QHash<QString, QVariantMap> tagEditsByLocation() const override;
    QHash<int, QString> trackLocations(const QList<int>& trackIds) const override;
    int applyFileTags(const std::vector<std::pair<int, MixHouseFileTags> >& tags) override;
    QStringList customTagNames() const override;
    QHash<int, QString> customTagValues(const QString& name) const override;
    //! Replaces a track's custom tags with these (m_writeDb).
    void storeCustomTags(int trackId, const QMap<QString, QString>& tags);
    void requestRevealTrack(int trackId, RevealMode mode) override;
    muse::async::Channel<int, RevealMode> revealTrackRequested() const override;
    std::vector<MixHouseSmartPlaylist> smartPlaylists() const override;
    int saveSmartPlaylist(const MixHouseSmartPlaylist& playlist) override;
    bool deleteSmartPlaylist(int id) override;
    bool addTrackToPlaylist(int trackId, int playlistId) override;
    bool addTrackToCrate(int trackId, int crateId) override;
    bool setTrackColor(int trackId, int color) override;
    bool setTrackHidden(int trackId, bool hidden) override;
    int createPlaylist(const QString& name, int parentId) override;
    int createCrate(const QString& name) override;
    int createPlaylistFolder(const QString& name, int parentId) override;
    bool movePlaylistToFolder(int playlistId, int newParentId) override;
    bool movePlaylistOrder(int playlistId, bool moveUp) override;
    bool movePlaylist(int playlistId, int parentId, int beforeId) override;
    bool markTrackPlayed(int trackId) override;
    bool renamePlaylist(int playlistId, const QString& name) override;
    bool renameCrate(int crateId, const QString& name) override;
    bool deletePlaylist(int playlistId) override;
    bool deleteCrate(int crateId) override;
    bool setPlaylistLocked(int playlistId, bool locked) override;
    bool setCrateLocked(int crateId, bool locked) override;
    bool addTrackToAutoDj(int trackId, MixHouseAutoDjLoc loc) override;
    bool setTrackRating(int trackId, int rating) override;
    bool setTrackComment(int trackId, const QString& text) override;
    bool setTrackGrouping(int trackId, const QString& text) override;
    bool setTrackComposer(int trackId, const QString& text) override;
    bool setTrackAlbumArtist(int trackId, const QString& text) override;
    bool setTrackNumber(int trackId, const QString& text) override;
    bool scaleTrackBpm(int trackId, double factor) override;
    bool setTrackBeatGrid(int trackId, double bpm, double firstBeatSec, double downbeatSec,
                          const QString& detector = QString()) override;
    int setTrackBeatGrids(const std::vector<BeatGridEdit>& edits) override;
    bool setTrackEnergy(int trackId, double energy) override;
    std::optional<std::vector<MixHousePhrase> > trackPhraseEdits(int trackId) const override;
    std::vector<MixHousePhrase> detectedPhrases(int trackId) const override;
    std::optional<std::pair<double, double> > trackAnchors(const QString& location) const override;
    bool setTrackAnchors(const QString& location, double introSec, double outroSec) override;
    bool setTrackPhraseEdits(int trackId, const std::vector<MixHousePhrase>* phrases) override;
    bool setTrackBpmLocked(int trackId, bool locked) override;
    bool clearTrackField(int trackId, MixHouseClearField field) override;
    bool purgeTrack(int trackId) override;
    bool removeTracksFromPlaylist(int playlistId, const QList<int>& positions) override;
    bool removeTracksFromCrate(int crateId, const QList<int>& trackIds) override;
    bool setTracksHidden(const QList<int>& trackIds, bool hidden) override;
    bool purgeTracks(const QList<int>& trackIds) override;
    QByteArray overviewAnalysisBlob(int trackId) const override;
    MixHouseAnalyzeResult analyzeTrack(int trackId) override;
    void startBackgroundAnalysis(const QList<int>& trackIds) override;
    void stopBackgroundAnalysis() override;
    bool isAnalyzing() const override;
    muse::async::Channel<int, int> analysisProgress() const override;
    muse::async::Channel<AnalysisSummary> analysisFinished() const override;
    muse::async::Channel<int, int> trackAnalysisProgress() const override;
    bool coverInfoForTrack(int trackId, CoverInfo& out) const override;

private:
    void appendToManualOrder(int playlistId, int parentId);
    static QString defaultDatabasePath();
    static QString mixxxDatabasePath();
    //! Copies Mixxx's library (database + analysis folder) into Mixtape
    //! Studio's own library folder, the first time only.
    bool importFromMixxxOnce() const;
    bool createEmptyLibrary() const;
    //! Applies Mixxx's own preference values (the imported mixxx.cfg copy and
    //! the library's music directories) once.
    void importMixxxSettingsOnce() const;
    bool ensureOpen() const;
    MixHouseNamedList playlistsOfType(int hidden, bool newestFirst) const;
    bool ensureWritable();
    /// Shared by setTrackComment/Grouping/Composer/AlbumArtist/Number.
    /// tagLibPropertyKey is a TagLib::PropertyMap key (e.g. "COMPOSER"),
    /// only used when the "Synchronize library track metadata from/to
    /// file tags" preference is on -- see writeFileTagProperty's own
    /// comment.
    bool setTrackTextField(int trackId, const QString& column, const QString& text,
                           const QString& tagLibPropertyKey);
    /// "Music library" Preferences page's own "Synchronize library track
    /// metadata from/to file tags" (real mixxx: kSyncTrackMetadataExport).
    /// TagLib::PropertyMap is a real, format-agnostic key/value tag API
    /// (COMMENT/COMPOSER/ALBUMARTIST/GROUPING/TRACKNUMBER map onto each
    /// format's own real frame -- ID3v2 TCOM/TPE2/TIT1 for MP3, Vorbis
    /// comment fields for FLAC/Ogg, MP4 atoms -- without this app needing
    /// its own per-format dispatch the way mixhousecoverart.cpp's read
    /// path does, since TagLib itself already provides that mapping).
    /// Logs and returns false on failure without throwing -- a file-tag
    /// write failing must never fail the library-row write it accompanies.
    bool writeFileTagProperty(const QString& filePath, const QString& propertyKey, const QString& value);
    bool backupOnce();
    /// Builds a comma-joined literal for a SQL "IN (...)" clause from real
    /// int ids (never user text, so no bind-parameter/injection concern) --
    /// shared by every batch method below instead of one DELETE/UPDATE per
    /// id. A plain class-scoped static (not a free function in an anonymous
    /// namespace) on purpose: this module's CMake build concatenates every
    /// .cpp in src/mixhouse/ into one translation unit, and anonymous-
    /// namespace helper names have collided across files here before.
    static QString sqlInClause(const QList<int>& ids);
    /// Shared by every mutating playlist/crate method below -- see
    /// MixHouseNamedItem::locked's own comment. Read via m_writeDb (called
    /// only from methods that already went through ensureWritable()).
    bool isPlaylistLocked(int playlistId) const;
    bool isCrateLocked(int crateId) const;
    /// The id of the one real Auto DJ playlist row (Playlists.hidden = 1),
    /// or -1 if somehow missing.
    int autoDjPlaylistId() const;
    /// Shared by addTracksFromFiles and the two XML importers (deliberately
    /// NOT by addTracksFromFolder -- that method already shipped and is
    /// working; duplicating its few lines here is safer than risking it by
    /// refactoring already-verified code). Finds the existing library.id for
    /// `path` by its track_locations row, or inserts both rows if genuinely
    /// new. False only on a real SQL failure.
    bool ensureTrackForFile(const QString& path, int& outLibraryId, bool& outWasNew);
    /// Read-only counterpart to ensureTrackForFile -- looks up an EXISTING
    /// library.id for `path`, never creates one. Used by
    /// importFromRekordboxXml()'s analysis-enrichment pass, which must only
    /// ever touch tracks the library already has in this library (matched by real
    /// file path), never silently create new rows for the thousands of
    /// other tracks a real rekordbox collection XML can reference that
    /// aren't part of this library at all. Returns -1 if not found.
    int findExistingTrackId(const QString& path) const;
    /// Fallback for findExistingTrackId() when the exact path doesn't match
    /// -- real-world DJ libraries routinely have the SAME file reachable at
    /// two different absolute paths (an external drive rekordbox scanned
    /// from, a local working copy this library was scanned from), so path
    /// equality alone misses genuine matches. Filename + real byte size
    /// together is a strong, low-false-positive heuristic without needing
    /// to hash file contents. Returns -1 if not found.
    int findExistingTrackByFilenameAndSize(const QString& filename, qint64 size) const;
    /// Rekordbox-playlist-import counterpart to ensureTrackForFile -- tries
    /// the SAME exact-path-then-filename+size fallback matching
    /// findExistingTrackByFilenameAndSize exists for, and only creates a new
    /// track if truly neither finds one. A real, confirmed bug this fixes:
    /// without the fallback here, a track already in this library under a
    /// different real absolute path than what's in the rekordbox XML (a
    /// real-world case: rekordbox's own "DJ USB" collection path vs. this
    /// library's own local working-copy path for the exact same file) got a
    /// brand-new DUPLICATE library row created for it, and the analysis-
    /// enrichment pass then wrote rekordbox's real bpm/beatgrid/key into
    /// that duplicate instead of the track the library actually has. `size` is
    /// the rekordbox XML's own Size attribute in bytes (<= 0 skips the
    /// fallback, matching findExistingTrackByFilenameAndSize's own rule).
    bool ensureTrackForRekordboxEntry(const QString& path, qint64 size, int& outLibraryId, bool& outWasNew);

    /// One worker thread's slot in the background analyzer pool -- see
    /// startBackgroundAnalysis's own interface comment. thread/worker are
    /// non-owning: lifetime is deleteLater(), chained off QThread::finished
    /// (connected once, at creation, in startBackgroundAnalysis itself).
    /// The worker itself owns no QSqlDatabase at all -- see
    /// MixHouseAnalyzerWorker's own header comment for why (a real,
    /// measured contention bug the first version of this pool had).
    struct AnalyzerWorkerHandle {
        QThread* thread = nullptr;
        MixHouseAnalyzerWorker* worker = nullptr;
        bool busy = false;
    };
    /// Looks up and hands `handle` its next queued track (skipping, and
    /// immediately tallying as failed, any track id whose lookup itself
    /// fails -- see mixhouseanalysiswriter.h's own comment for why that
    /// lookup happens HERE, on the main thread, rather than in the worker).
    /// Leaves handle.busy false and asks its thread to wind down if the
    /// queue is empty or a stop was requested -- callers are responsible
    /// for calling finishBatchIfAllIdle() afterward (not from inside a loop
    /// over m_analyzeWorkers -- see startBackgroundAnalysis's own comment).
    void tryDispatchNextTrack(AnalyzerWorkerHandle& handle);
    /// If every worker handle is currently idle, tears the whole pool down
    /// and publishes analysisFinished() with the final tally. A no-op
    /// (returns immediately) if any handle is still busy.
    void finishBatchIfAllIdle();
    /// Runs on the MAIN thread (see startBackgroundAnalysis's own
    /// implementation comment for exactly why that's guaranteed) whenever a
    /// worker finishes computing one track's analysis. This is the ONLY
    /// place a background-pool result is ever written to the database
    /// (via m_writeDb) -- tallies the result, publishes progress, and
    /// either hands that worker the next queued track or -- once every
    /// worker has nothing left -- winds the whole pool down.
    void onAnalyzerWorkerFinished(QThread* thread, int trackId, bool bpmLocked,
                                  const MixHouseAnalysisResult& analysis);

    /// Unique per instance: Audacity 4 creates a MixHouseLibrary per project
    /// context, and QSqlDatabase connection names are process-global -- with
    /// a fixed name, File > New's second instance silently replaced the
    /// first one's connection ("duplicate connection name ... old connection
    /// removed") and the open library panel showed no tracks at all
    /// (2026-09-13 15:38).
    const QString m_connectionSuffix = QString::number(reinterpret_cast<quintptr>(this), 16);
    mutable QSqlDatabase m_db;
    QString m_path;
    mutable QString m_error;
    mutable bool m_open = false;
    mutable bool m_tried = false;
    mutable QHash<QString, int> m_probedSampleRates;   ///< file -> header sample rate, for rows missing samplerate
    //! Writes the sample rate of every track missing one into the library,
    //! on its own thread, once per run (see tracks()).
    void fillMissingSampleRates();
    //! False once this instance is gone: background threads check it before
    //! calling back.
    const std::shared_ptr<std::atomic<bool> > m_alive = std::make_shared<std::atomic<bool> >(true);
    // Writing needs its own connection: the read one is opened READONLY on
    // purpose and must stay that way.
    QSqlDatabase m_writeDb;
    muse::async::Notification m_libraryChanged;
    //! detectedPhrases() per track, until the library changes (analysis, grid edits).
    mutable QHash<int, std::vector<MixHousePhrase> > m_detectedPhrases;
    bool m_writeOpen = false;
    bool m_backedUp = false;

    // Background scan (see startBackgroundScan()'s own interface comment
    // for why this exists and what it deliberately doesn't port from real
    // mixxx's own, larger LibraryScanner). Owned here, not by the worker
    // object itself, so isScanning() can answer synchronously from the
    // calling (usually main) thread without reaching across threads.
    std::unique_ptr<QThread> m_scanThread;
    // Non-owning -- lifetime is deleteLater() from the finished handler.
    // Kept only so the destructor can ask a still-running scan to wrap up
    // instead of blocking on QThread::wait() for however long it would
    // otherwise take to finish naturally (see MixHouseLibrary's own
    // destructor and MixHouseLibraryScanner::cancel()'s own comment).
    MixHouseLibraryScanner* m_scanWorker = nullptr;
    bool m_scanning = false;
    muse::async::Channel<int, int> m_scanProgress;
    muse::async::Channel<int, RevealMode> m_revealTrackRequested;
    muse::async::Channel<ScanResult> m_scanFinished;

    // Background analysis pool (see startBackgroundAnalysis's own interface
    // comment). One AnalyzerWorkerHandle per worker thread, all created
    // together at the start of a batch and all torn down together once the
    // batch (or a stopBackgroundAnalysis() request) finishes -- unlike the
    // single long-lived scan thread above, a fresh pool per batch keeps
    // lifetime simple and matches the scanner's own per-call create/
    // destroy rule.
    std::vector<AnalyzerWorkerHandle> m_analyzeWorkers;
    QList<int> m_analyzeQueue;
    //! GlideMix crash guard: the songs on an analysis thread right now, kept in
    //! a file beside the library. Left over at the next batch = they were being
    //! analysed when GlideMix stopped. A song left over twice is skipped.
    QSet<int> m_analysisInFlight;
    QHash<int, int> m_analysisSuspects;   // track id -> crashes it was part of
    QHash<int, int> m_mikEnergy;          // track id -> Mixed In Key energy from the comment
    bool m_analysisGuardLoaded = false;
    void loadAnalysisGuard();
    void saveAnalysisGuard() const;
    //! Songs a batch had not finished when GlideMix last stopped (crash or quit):
    //! analysed again a few seconds after the next start.
    QList<int> m_analysisResumeIds;
    void resumeAnalysisAfterRestart();
    int m_analyzeTotal = 0;
    int m_analyzeSucceeded = 0;
    int m_analyzeFailed = 0;
    bool m_analyzing = false;
    bool m_analyzeStopRequested = false;
    muse::async::Channel<int, int> m_analysisProgress;
    muse::async::Channel<AnalysisSummary> m_analysisFinished;
    muse::async::Channel<int, int> m_trackAnalysisProgress;
};
}
