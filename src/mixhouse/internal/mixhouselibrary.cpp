/*
 * MixHouse DJ 2
 *
 * Reads Mixxx's library database directly. This is the first piece of MixHouse
 * "brain" brought into Audacity, and it is deliberately the piece that needs no
 * engine port at all: the analysis output this project cares about -- BPM, key, the
 * serialised beatgrid, ratings, colours -- is already sitting in that SQLite
 * file, written there by the fork.
 *
 * Nothing here includes a Mixxx header. It is a schema dependency, not a code
 * dependency, so it cannot drag in ControlObject, EngineBuffer or QtWidgets.
 * The schema was read from the live database rather than assumed (`.schema
 * library` against the real mixxxdb.sqlite -- every column mixxx's own
 * library/columncache.cpp offers as a data-backed table column is here):
 *   library:         id, artist, title, album, album_artist, year, genre,
 *                    composer, grouping, tracknumber, filetype, comment,
 *                    duration, bitrate, bpm, replaygain, samplerate, key,
 *                    key_id, beats, beats_version, rating, color, location,
 *                    datetime_added, timesplayed, last_played_at, mixxx_deleted,
 *                    bpm_lock
 *   track_locations: id, location
 *   cues:            id, track_id, type, position, length, hotcue, label
 *   track_analysis:  id, track_id, type, description, version, created,
 *                    data_checksum
 *   Playlists:       id, name, position, hidden, date_created, date_modified,
 *                    locked
 *   PlaylistTracks:  playlist_id, track_id, position, pl_datetime_added
 *   crates:          id, name, count, show, locked, autodj_source
 *   crate_tracks:    crate_id, track_id
 */
#include "mixhouselibrary.h"
#include "mixhouse/mixhousenativepath.h"

#include <algorithm>
#include <cmath>
#include <thread>

#include "mixhouse/mixhouseaudiosource.h"
#include "mixhouse/mixhousefiletags.h"
#include "mixhouse/mixhousesearchquery.h"
#include "mixhouse/imixhousekeymatch.h"
#include "mixhouse/mixhouseanalyzer.h"
#include "mixhouse/mixhousewaveform.h"
#include "mixhouse/mixhousephrasedetector.h"
#include "mixhouselibraryscanner.h"
#include "mixhouseanalysiswriter.h"
#include "mixhouseanalyzerworker.h"
#include "mixhouselibraryschema.h"

#include <taglib/fileref.h>
#include <taglib/tfile.h>
#include <taglib/tpropertymap.h>
#include <taglib/tstring.h>
#include <taglib/tstringlist.h>

#include <QCollator>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QVariant>
#include <QXmlStreamReader>
#include <QUrl>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "log.h"

using namespace au::mixhouse;

namespace {
/// MixHouse's own side table: mixxx's schema has no downbeat concept (its
/// Beats are a tempo + first beat only). Kept in the same database so purge/
/// clear can remove rows with the track; real Mixxx ignores tables it
/// doesn't know. Same approach as the parked MixHouse DJ fork's
/// DownbeatCache, which used a separate file keyed by path.
constexpr const char* kCreateDownbeatTableSql =
    "CREATE TABLE IF NOT EXISTS mixhouse_downbeats ("
    " track_id INTEGER PRIMARY KEY,"
    " downbeat_sec REAL NOT NULL,"
    " confidence REAL NOT NULL,"
    " detector TEXT NOT NULL)";

//! GlideMix's energy rating (1-10) of a track, from its analysis.
constexpr const char* kCreateEnergyTableSql =
    "CREATE TABLE IF NOT EXISTS mixhouse_energy ("
    " track_id INTEGER PRIMARY KEY,"
    " energy REAL NOT NULL)";

//! MixMeister Intro/Outro anchors moved by hand, per file.
constexpr const char* kCreateAnchorTableSql =
    "CREATE TABLE IF NOT EXISTS mixhouse_anchors ("
    " location TEXT PRIMARY KEY,"
    " intro_sec REAL NOT NULL,"
    " outro_sec REAL NOT NULL)";

//! Phrases edited in the Beatgrid editor, as JSON [{"s","e","f","l"}].
constexpr const char* kCreatePhraseTableSql =
    "CREATE TABLE IF NOT EXISTS mixhouse_phrases ("
    " track_id INTEGER PRIMARY KEY,"
    " phrases TEXT NOT NULL)";

//! The phrase model's phrases (mixhousephrasemodel.h) and the grid they were
//! made for: used only while the track keeps that tempo and bar 1.
constexpr const char* kCreateModelPhraseTableSql =
    "CREATE TABLE IF NOT EXISTS mixhouse_model_phrases ("
    " track_id INTEGER PRIMARY KEY,"
    " bpm REAL NOT NULL,"
    " downbeat_sec REAL NOT NULL,"
    " phrases TEXT NOT NULL)";

//! Tags edited in GlideMix (Edit Tags) for songs shown from Lexicon or rekordbox:
//! their rows come from those apps, so these are laid over them. JSON {"title":...}.
constexpr const char* kCreateTagEditTableSql =
    "CREATE TABLE IF NOT EXISTS mixhouse_tag_edits ("
    " location TEXT PRIMARY KEY,"
    " fields TEXT NOT NULL)";

//! Smart playlists (GlideMix): name and rules as JSON (MixHouseSmartPlaylist).
constexpr const char* kCreateSmartPlaylistTableSql =
    "CREATE TABLE IF NOT EXISTS mixhouse_smart_playlists ("
    " id INTEGER PRIMARY KEY AUTOINCREMENT,"
    " name TEXT NOT NULL,"
    " definition TEXT NOT NULL,"
    " position INTEGER NOT NULL DEFAULT 0)";

//! Edit Tags field -> library column and file tag (TagLib property name).
struct TagField {
    const char* key;
    const char* column;
    const char* tag;
};
constexpr TagField kTagFields[] = {
    { "title", "title", "TITLE" },
    { "artist", "artist", "ARTIST" },
    { "album", "album", "ALBUM" },
    { "albumArtist", "album_artist", "ALBUMARTIST" },
    { "genre", "genre", "GENRE" },
    { "year", "year", "DATE" },
    { "trackNumber", "tracknumber", "TRACKNUMBER" },
    { "composer", "composer", "COMPOSER" },
    { "grouping", "grouping", "GROUPING" },
    { "comment", "comment", "COMMENT" },
};

QByteArray phrasesToJson(const std::vector<MixHousePhrase>& phrases)
{
    QJsonArray array;
    for (const MixHousePhrase& phrase : phrases) {
        QJsonObject o;
        o.insert(QStringLiteral("s"), phrase.startSec);
        o.insert(QStringLiteral("e"), phrase.endSec);
        o.insert(QStringLiteral("l"), phrase.label);
        array.append(o);
    }
    return QJsonDocument(array).toJson(QJsonDocument::Compact);
}

std::vector<MixHousePhrase> phrasesFromJson(const QByteArray& json)
{
    std::vector<MixHousePhrase> phrases;
    for (const QJsonValue& value : QJsonDocument::fromJson(json).array()) {
        const QJsonObject o = value.toObject();
        MixHousePhrase phrase;
        phrase.startSec = o.value(QStringLiteral("s")).toDouble();
        phrase.endSec = o.value(QStringLiteral("e")).toDouble();
        phrase.label = o.value(QStringLiteral("l")).toString();
        phrase.color = mixHousePhraseColor(phrase.label);
        if (phrase.endSec > phrase.startSec) {
            phrases.push_back(phrase);
        }
    }
    return phrases;
}

bool hasEnergyTable(QSqlDatabase& db)
{
    QSqlQuery q(db);
    return q.exec(QStringLiteral(
        "SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = 'mixhouse_energy'")) && q.next();
}

bool hasDownbeatTable(QSqlDatabase& db)
{
    QSqlQuery q(db);
    return q.exec(QStringLiteral(
        "SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = 'mixhouse_downbeats'")) && q.next();
}

// keyId -> Camelot text (the notation the reference library actually uses --
// confirmed empirically against his own real, Mixxx-populated mixxxdb.sqlite:
// `SELECT DISTINCT key, key_id FROM library ... ORDER BY key_id`, NOT derived
// from mixhousesearchquery.cpp's/mixhousekeywheel.cpp's own
// kOpenKeyNumberByChromaticKey formula, which turned out to disagree with
// this real data for several keyIds (e.g. it computes keyId 20 -> "11A", but
// every real Gm track in the reference library is actually stored as "6A") -- that
// table may have its own bug worth a separate look, but this one is verified
// ground truth, not a formula. This is the SAME notation ~99% of the project's
// library already used before analyzeTrack() started writing kKeyIdToText's
// Traditional spelling instead ("Em" for a track everything else calls "9A")
// -- exactly the "some tracks had musical key and some had Camelot key"
// inconsistency he reported. Both the write-back in analyzeTrack() and the
// tracks() read path below normalize to this table, so the display is
// self-consistent regardless of what raw text a track happens to have
// stored. Used before its declaration point in the file would otherwise
// need to move (tracks() comes first) -- placed here, right after the
// includes, so every function in this translation unit can see it.
const char* const kKeyIdToCamelotText[25] = {
    "",
    "8B", "3B", "10B", "5B", "12B", "7B", "2B", "9B", "4B", "11B", "6B", "1B",
    "5A", "12A", "7A", "2A", "9A", "4A", "11A", "6A", "1A", "8A", "3A", "10A"
};
}

static const char* kConnectionName = "mixhouse_library";
//! After the window is up: the fill reads every file missing a sample rate.
static constexpr int kSampleRateFillDelayMs = 5000;

MixHouseLibrary::~MixHouseLibrary()
{
    *m_alive = false;
    // A running QThread must be stopped before its destructor runs (Qt
    // itself asserts/aborts otherwise) -- a real scenario if the app quits
    // while a background scan is still going. Ask the worker to wrap up
    // first (see MixHouseLibraryScanner::cancel()'s own comment) rather
    // than just calling wait(), which would otherwise block app shutdown
    // until an arbitrarily long scan finished on its own.
    if (m_scanThread && m_scanThread->isRunning()) {
        if (m_scanWorker) {
            m_scanWorker->cancel();
        }
        m_scanThread->quit();
        m_scanThread->wait();
    }
    // Same reasoning as the scan thread above, generalized to N workers:
    // ask every worker's thread to wind down first (quit() just requests
    // the exit -- each worker's own in-flight analyzeTrackAndWrite() call,
    // if any, still has to return naturally first, same disclosed no-mid-
    // decode-cancellation limitation stopBackgroundAnalysis() has), THEN
    // wait() on all of them. Two passes rather than quit()+wait() per
    // worker in one loop, so workers still finishing their current track
    // do so concurrently during shutdown instead of one at a time.
    for (auto& handle : m_analyzeWorkers) {
        handle.thread->quit();
    }
    for (auto& handle : m_analyzeWorkers) {
        handle.thread->wait();
    }
    // Release only THIS instance's connections (see m_connectionSuffix).
    for (QSqlDatabase* db : { &m_db, &m_writeDb }) {
        const QString name = db->connectionName();
        if (db->isOpen()) {
            db->close();
        }
        *db = QSqlDatabase();
        if (!name.isEmpty()) {
            QSqlDatabase::removeDatabase(name);
        }
    }
}

QString MixHouseLibrary::defaultDatabasePath()
{
    // GlideMix's own library. Its analysis folder (waveform overviews)
    // sits next to it, the layout Mixxx uses. Kept out of Mixxx's sandbox
    // container: macOS asks for permission every time an app reads another
    // app's container.
    return QDir::homePath() + QStringLiteral("/Library/Application Support/Glidemix/Library/library.sqlite");
}

QString MixHouseLibrary::mixxxDatabasePath()
{
    // Where Mixxx (and the parked MixHouse DJ fork, same bundle id) keeps its
    // library on macOS -- only read once, to import it.
    return QDir::homePath() + QStringLiteral(
        "/Library/Containers/org.mixxx.mixxx/Data/Library/Application Support/Mixxx/mixxxdb.sqlite");
}

namespace {
bool copyDirectoryRecursively(const QString& from, const QString& to)
{
    QDir source(from);
    if (!source.exists()) {
        return true;
    }
    if (!QDir().mkpath(to)) {
        return false;
    }
    bool ok = true;
    QDirIterator it(from, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QString target = to + QLatin1Char('/') + source.relativeFilePath(path);
        if (it.fileInfo().isDir()) {
            ok = QDir().mkpath(target) && ok;
        } else if (!QFileInfo::exists(target)) {
            ok = QFile::copy(path, target) && ok;
        }
    }
    return ok;
}
}

void MixHouseLibrary::importMixxxSettingsOnce() const
{
    const auto config = libraryConfiguration();
    if (!config || config->mixxxSettingsImported()) {
        return;
    }

    // Mixxx's config format: "[Section]" lines, then "Key value" lines.
    QHash<QString, QString> values;   // "Section/Key" -> value
    QFile cfg(QFileInfo(m_path).absolutePath() + QStringLiteral("/mixxx.cfg"));
    if (cfg.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString section;
        while (!cfg.atEnd()) {
            const QString line = QString::fromUtf8(cfg.readLine()).trimmed();
            if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
                section = line.mid(1, line.size() - 2);
            } else if (!line.isEmpty()) {
                const int space = line.indexOf(QLatin1Char(' '));
                if (space > 0) {
                    values.insert(section + QLatin1Char('/') + line.left(space), line.mid(space + 1));
                }
            }
        }
    }
    auto has = [&](const char* key) { return values.contains(QString::fromUtf8(key)); };
    auto boolOf = [&](const char* key) { return values.value(QString::fromUtf8(key)).toInt() != 0; };
    auto intOf = [&](const char* key) { return values.value(QString::fromUtf8(key)).toInt(); };

    if (has("Library/RowHeight")) { config->setLibraryRowHeight(intOf("Library/RowHeight")); }
    if (has("Library/TrackLoadAction")) {
        config->setTrackDoubleClickAction(static_cast<MixHouseTrackDoubleClickAction>(std::clamp(intOf("Library/TrackLoadAction"), 0, 3)));
    }
    if (has("Library/ApplyPlayedTrackColor")) { config->setGreyOutPlayedTracks(boolOf("Library/ApplyPlayedTrackColor")); }
    if (has("Library/SearchDebouncingTimeoutMillis")) { config->setSearchDebounceMs(intOf("Library/SearchDebouncingTimeoutMillis")); }
    if (has("Library/search_bpm_fuzzy_range")) {
        // DlgPrefLibrary::slotBpmRangeSelected: percent * rate range (8) / 10000.
        const double range = intOf("Library/search_bpm_fuzzy_range") * 8.0 / 10000.0;
        config->setFuzzyBpmRelativeRange(range);
        setFuzzyBpmRelativeRange(range);
    }
    if (has("Library/RescanOnStartup")) { config->setRescanOnStartup(boolOf("Library/RescanOnStartup")); }
    if (has("Library/show_library_scan_summary")) { config->setShowScanSummaryDialog(boolOf("Library/show_library_scan_summary")); }
    if (has("Library/EditMetadataSelectedClick")) { config->setEditMetadataAfterClick(boolOf("Library/EditMetadataSelectedClick")); }
    if (has("Library/EnableSearchCompletions")) { config->setSearchCompletionsEnabled(boolOf("Library/EnableSearchCompletions")); }
    if (has("Library/EnableSearchHistoryShortcuts")) {
        config->setSearchHistoryShortcutsEnabled(boolOf("Library/EnableSearchHistoryShortcuts"));
    }
    if (has("Library/SyncTrackMetadataExport")) { config->setSyncMetadataToFileTags(boolOf("Library/SyncTrackMetadataExport")); }
    if (has("Library/BpmColumnPrecision")) { config->setBpmPrecision(intOf("Library/BpmColumnPrecision")); }
    if (has("Library/Font")) {
        QFont font;
        if (font.fromString(values.value(QStringLiteral("Library/Font")))) {
            config->setLibraryFont(font.family(), std::max(0, font.pointSize()));
        }
    }
    // Waveform overview type is not imported: GlideMix's White overview
    // (a deliberate choice) has no Mixxx equivalent.

    // Music directories live in the library database (Mixxx's directories table).
    if (config->musicDirectories().isEmpty()) {
        QSqlQuery q(m_db);
        if (q.exec(QStringLiteral("SELECT directory FROM directories"))) {
            while (q.next()) {
                config->addMusicDirectory(q.value(0).toString());
            }
        }
    }

    config->setMixxxSettingsImported(true);
    LOGI() << "GlideMix library: imported " << values.size() << " Mixxx preference values";
}

bool MixHouseLibrary::importFromMixxxOnce() const
{
    if (QFileInfo::exists(m_path)) {
        return true;
    }
    const QString source = mixxxDatabasePath();
    if (!QFileInfo::exists(source)) {
        return false;
    }
    const QString libraryDir = QFileInfo(m_path).absolutePath();
    if (!QDir().mkpath(libraryDir)) {
        LOGE() << "GlideMix library: could not create " << libraryDir;
        return false;
    }
    LOGI() << "GlideMix library: importing Mixxx library from " << source;

    // Mixxx's preferences file, applied by importMixxxSettingsOnce().
    const QString mixxxCfg = QFileInfo(source).absolutePath() + QStringLiteral("/mixxx.cfg");
    if (QFileInfo::exists(mixxxCfg)) {
        QFile::remove(libraryDir + QStringLiteral("/mixxx.cfg"));
        QFile::copy(mixxxCfg, libraryDir + QStringLiteral("/mixxx.cfg"));
    }

    // Waveform overviews first: the database is put in place last, so an
    // interrupted import starts again on the next launch.
    const QString mixxxAnalysis = QFileInfo(source).absolutePath() + QStringLiteral("/analysis");
    if (!copyDirectoryRecursively(mixxxAnalysis, libraryDir + QStringLiteral("/analysis"))) {
        LOGW() << "GlideMix library: some analysis files could not be copied";
    }

    // VACUUM INTO writes a consistent snapshot even if Mixxx has the library
    // open with a write-ahead log.
    const QString temp = m_path + QStringLiteral(".importing");
    QFile::remove(temp);
    bool copied = false;
    {
        QSqlDatabase src = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                     QStringLiteral("mixtapestudio_import_") + m_connectionSuffix);
        src.setDatabaseName(source);
        src.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        if (src.open()) {
            QSqlQuery q(src);
            QString escaped = temp;
            escaped.replace(QLatin1Char('\''), QStringLiteral("''"));
            copied = q.exec(QStringLiteral("VACUUM INTO '%1'").arg(escaped));
            if (!copied) {
                LOGW() << "GlideMix library: VACUUM INTO failed (" << q.lastError().text() << "), copying the file";
            }
            src.close();
        } else {
            LOGW() << "GlideMix library: cannot open the Mixxx library: " << src.lastError().text();
        }
    }
    QSqlDatabase::removeDatabase(QStringLiteral("mixtapestudio_import_") + m_connectionSuffix);
    if (!copied) {
        QFile::remove(temp);
        QFile file(source);
        copied = file.copy(temp);
        if (!copied) {
            LOGW() << "GlideMix library: cannot copy the Mixxx library: " << file.errorString();
        }
    }
    if (!copied || !QFile::rename(temp, m_path)) {
        QFile::remove(temp);
        LOGE() << "GlideMix library: import failed";
        return false;
    }
    LOGI() << "GlideMix library: imported into " << m_path;
    return true;
}

// A new user without Mixxx: an empty library with Mixxx's schema, the Auto
// DJ playlist Mixxx makes on its first start, and GlideMix's own tables. Made
// under another name and moved into place, so a half-made one never opens.
bool MixHouseLibrary::createEmptyLibrary() const
{
    const QString libraryDir = QFileInfo(m_path).absolutePath();
    if (!QDir().mkpath(libraryDir)) {
        LOGE() << "GlideMix library: could not create " << libraryDir;
        return false;
    }
    const QString temp = m_path + QStringLiteral(".creating");
    QFile::remove(temp);

    const QString connection = QStringLiteral("glidemix_create_") + m_connectionSuffix;
    bool ok = true;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        db.setDatabaseName(temp);
        if (!db.open()) {
            LOGE() << "GlideMix library: could not create " << temp << ": " << db.lastError().text();
            ok = false;
        } else {
            QStringList statements = QString::fromUtf8(kLibrarySchema).split(QLatin1Char(';'), Qt::SkipEmptyParts);
            statements << QStringLiteral("INSERT INTO settings (name, value) VALUES ('mixxx.schema.version', '%1')")
                          .arg(QString::fromLatin1(kLibrarySchemaVersion))
                       << QStringLiteral("INSERT INTO Playlists (name, position, hidden, date_created, date_modified) "
                                         "VALUES ('Auto DJ', 0, 1, CURRENT_TIMESTAMP, CURRENT_TIMESTAMP)");
            db.transaction();
            for (const QString& statement : statements) {
                if (statement.trimmed().isEmpty()) {
                    continue;
                }
                QSqlQuery q(db);
                if (!q.exec(statement)) {
                    LOGE() << "GlideMix library: " << q.lastError().text() << " in: " << statement;
                    ok = false;
                    break;
                }
            }
            ok = ok && db.commit();
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connection);

    if (!ok || !QFile::rename(temp, m_path)) {
        QFile::remove(temp);
        LOGE() << "GlideMix library: could not create a new library";
        return false;
    }
    LOGI() << "GlideMix library: created a new library at " << m_path;
    return true;
}

void MixHouseLibrary::init()
{
    m_libraryChanged.onNotify(this, [this]() { m_detectedPhrases.clear(); });
    m_path = defaultDatabasePath();
    LOGI() << "GlideMix library will open on first use: " << m_path;

    // Applies the persisted "fuzzy BPM search range" preference at
    // startup -- MixHouseLibraryPreferencesModel applies the same setter
    // live on every change after that, matching real mixxx's own
    // DlgPrefLibrary::slotBpmRangeSelected (see setFuzzyBpmRelativeRange's
    // own comment in mixhousesearchquery.h).
    setFuzzyBpmRelativeRange(libraryConfiguration()->fuzzyBpmRelativeRange());

    // GlideMix: Fast or Accurate beat analysis, now and on every change.
    setMixHouseAccurateBeatAnalysis(libraryConfiguration()->accurateBeatAnalysis());
    libraryConfiguration()->settingsChanged().onNotify(this, [this]() {
        setMixHouseAccurateBeatAnalysis(libraryConfiguration()->accurateBeatAnalysis());
    });
}

muse::async::Notification MixHouseLibrary::libraryChanged() const
{
    return m_libraryChanged;
}

// Opened on first real use. The first time ever, that imports Mixxx's library
// (reading Mixxx's container, which macOS asks permission for once).
bool MixHouseLibrary::ensureOpen() const
{
    if (m_open) {
        return true;
    }
    if (m_tried) {
        return false;
    }
    m_tried = true;

    // GlideMix: no import from Mixxx (rekordbox and Lexicon cover other DJ
    // apps' libraries): reading Mixxx's sandbox could bring up a macOS
    // "access data from other apps" prompt for people who never use Mixxx.
    if (!QFileInfo::exists(m_path)) {
        createEmptyLibrary();
    }
    if (!QFileInfo::exists(m_path)) {
        m_error = QStringLiteral("Could not create the library at %1").arg(m_path);
        LOGW() << m_error;
        return false;
    }

    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                     QString::fromLatin1(kConnectionName) + QLatin1Char('_') + m_connectionSuffix);
    m_db.setDatabaseName(m_path);
    // Read-only connection for queries; writes go through m_writeDb.
    m_db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));

    if (!m_db.open()) {
        m_error = m_db.lastError().text();
        LOGE() << "could not open Mixxx library: " << m_error;
        return false;
    }

    m_open = true;
    importMixxxSettingsOnce();
    LOGI() << "GlideMix library opened: " << m_path << ", " << trackCount() << " tracks";

    // Two self-checks on a bounded sample, run once when the library first opens.
    // Both exist because a silent failure here is invisible until much later:
    // a beatgrid that does not decode looks like "no beatgrid", and a decoder that
    // disagrees with the file would make every analyzer fed by it wrong.
    QElapsedTimer selfCheckTimer;
    selfCheckTimer.start();
    TrackQuery probe;
    probe.limit = 200;
    const MixHouseTrackList sample = tracks(probe);

    int withBlob = 0, decoded = 0;
    for (const MixHouseTrack& t : sample) {
        if (!t.beatsBlob.isEmpty()) { ++withBlob; }
        if (t.beats.valid) { ++decoded; }
    }
    if (decoded < withBlob) {
        LOGW() << "GlideMix beatgrids: only " << decoded << "/" << withBlob
               << " decoded in a " << sample.size() << "-track sample -- tracks will be placed out of phase";
    } else {
        LOGI() << "GlideMix beatgrids: " << decoded << "/" << withBlob
               << " decoded in a " << sample.size() << "-track sample";
    }

    // The decoder check opens files, which on a sleeping external drive took
    // 4 s of a frozen window (2026-11-01): it runs on its own thread.
    struct DecoderCheck {
        QString filePath;
        QString title;
        int sampleRate = 0;
        double durationSec = 0.0;
    };
    std::vector<DecoderCheck> checks;
    for (const MixHouseTrack& t : sample) {
        if (!t.filePath.isEmpty() && t.sampleRate > 0 && t.durationSec > 0.0) {
            checks.push_back({ t.filePath, t.title, t.sampleRate, t.durationSec });
        }
    }
    LOGI() << "GlideMix library self-check on open: " << selfCheckTimer.elapsed() << " ms";
    std::thread([checks = std::move(checks)]() {
        int probed = 0, agree = 0;
        for (const DecoderCheck& t : checks) {
            const AudioInfo ai = probeAudioFile(t.filePath);
            if (!ai.valid) {
                LOGW() << "decode failed: " << t.filePath << " -- " << ai.error;
                continue;
            }
            ++probed;
            // Duration tolerance of 1s: container headers and decoders disagree
            // slightly on trailing partial frames, which is not an error.
            if (ai.sampleRate == t.sampleRate && std::abs(ai.durationSec - t.durationSec) < 1.0) {
                ++agree;
            } else {
                LOGW() << "decode mismatch: " << t.title
                       << " db(sr=" << t.sampleRate << " dur=" << t.durationSec << ")"
                       << " decoded(sr=" << ai.sampleRate << " dur=" << ai.durationSec << ")";
            }
            if (probed >= 8) { break; }
        }
        LOGI() << "GlideMix decoder: " << agree << "/" << probed
               << " files agree with the database on sample rate and duration";
    }).detach();

    // Not from inside ensureOpen(): the fill opens the library for writing.
    QTimer::singleShot(kSampleRateFillDelayMs, qApp, [this, alive = m_alive]() {
        if (*alive) {
            const_cast<MixHouseLibrary*>(this)->fillMissingSampleRates();
        }
    });
    return true;
}

// Tracks imported from rekordbox or added by this app's scanner have a
// beatgrid but no sample rate, and a beatgrid can't be read without it (frame
// positions). 34,695 of a 38,346-track library had none, and filling them in
// while listing tracks read every file on the UI thread, again each run. Here
// each file is read once, ever, on its own thread, and the rate is stored.
void MixHouseLibrary::fillMissingSampleRates()
{
    // One per run, not per instance (there is one library per project window).
    static std::atomic<bool> started { false };
    if (started.exchange(true) || !ensureWritable()) {
        return;
    }
    std::thread([this, path = m_path, alive = m_alive]() {
        QElapsedTimer timer;
        timer.start();
        const QString connection = QStringLiteral("mixhouse_samplerate_fill");
        int missing = 0, filled = 0, unreadable = 0;
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
            db.setDatabaseName(path);
            if (db.open()) {
                QSqlQuery(db).exec(QStringLiteral("PRAGMA busy_timeout=5000"));
                std::vector<std::pair<int, QString> > rows;
                {
                    QSqlQuery sel(db);
                    sel.setForwardOnly(true);
                    if (sel.exec(QStringLiteral(
                            "SELECT l.id, t.location FROM library l JOIN track_locations t ON t.id = l.location "
                            "WHERE (l.samplerate IS NULL OR l.samplerate <= 0) AND l.beats IS NOT NULL "
                            "AND length(l.beats) > 0 AND t.location IS NOT NULL"))) {
                        while (sel.next()) {
                            rows.emplace_back(sel.value(0).toInt(), sel.value(1).toString());
                        }
                    }
                }
                missing = static_cast<int>(rows.size());
                if (missing > 0) {
                    LOGI() << "GlideMix: reading the sample rate of " << missing << " tracks in the background";
                }
                // Short write transactions: the main thread writes to the same file.
                QSqlQuery update(db);
                update.prepare(QStringLiteral("UPDATE library SET samplerate = :sr WHERE id = :id"));
                std::vector<std::pair<int, int> > batch;
                const auto flush = [&]() {
                    if (batch.empty()) {
                        return;
                    }
                    db.transaction();
                    for (const auto& [id, rate] : batch) {
                        update.bindValue(QStringLiteral(":sr"), rate);
                        update.bindValue(QStringLiteral(":id"), id);
                        if (!update.exec()) {
                            LOGW() << "sample rate fill: update failed: " << update.lastError().text();
                        }
                    }
                    db.commit();
                    batch.clear();
                };
                for (const auto& [id, file] : rows) {
                    if (!*alive) {
                        break;
                    }
                    const AudioInfo info = probeAudioFile(file);
                    if (info.valid && info.sampleRate > 0) {
                        batch.emplace_back(id, info.sampleRate);
                        ++filled;
                    } else {
                        ++unreadable;   // missing or unplugged: tried again next run
                    }
                    if (batch.size() >= 200) {
                        flush();
                    }
                }
                flush();
                db.close();
            }
        }
        QSqlDatabase::removeDatabase(connection);
        if (missing == 0) {
            return;
        }
        LOGI() << "GlideMix: sample rates filled in for " << filled << " of " << missing << " tracks ("
               << unreadable << " unreadable) in " << timer.elapsed() << " ms";
        if (filled == 0) {
            return;
        }
        QTimer::singleShot(0, qApp, [this, alive]() {
            if (*alive) {
                m_probedSampleRates.clear();
                m_libraryChanged.notify();   // the open views get their beatgrids
            }
        });
    }).detach();
}

QString MixHouseLibrary::databasePath() const { return m_path; }
bool MixHouseLibrary::isAvailable() const { return ensureOpen(); }
QString MixHouseLibrary::lastError() const { return m_error; }

int MixHouseLibrary::trackCount() const
{
    if (!m_open) {   // no ensureOpen() here: called from inside it
        return 0;
    }
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("SELECT COUNT(*) FROM library WHERE mixxx_deleted = 0")) || !q.next()) {
        return 0;
    }
    return q.value(0).toInt();
}

MixHouseTrackList MixHouseLibrary::tracks(const TrackQuery& query) const
{
    MixHouseTrackList out;
    if (!ensureOpen()) {
        return out;
    }
    QElapsedTimer timer;
    timer.start();
    int headerReads = 0;

    // Sorting is done in SQL, not on an already-truncated page: sorting a 2000-row
    // slice of a 1372-track library would sort the wrong rows.
    const bool withEnergy = hasEnergyTable(m_db);
    const QString energyColumn = withEnergy
        ? QStringLiteral("(SELECT en.energy FROM mixhouse_energy en WHERE en.track_id = l.id)")
        : QStringLiteral("NULL");
    QString orderBy;
    switch (query.sort) {
    case SortKey::Energy:    orderBy = energyColumn; break;
    case SortKey::Title:     orderBy = QStringLiteral("l.title COLLATE NOCASE"); break;
    case SortKey::Artist:    orderBy = QStringLiteral("l.artist COLLATE NOCASE"); break;
    case SortKey::Album:     orderBy = QStringLiteral("l.album COLLATE NOCASE"); break;
    case SortKey::Genre:     orderBy = QStringLiteral("l.genre COLLATE NOCASE"); break;
    case SortKey::Year:      orderBy = QStringLiteral("l.year"); break;
    case SortKey::Bpm:       orderBy = QStringLiteral("l.bpm"); break;
    case SortKey::Key:
        // Real mixxx never sorts by raw key_id -- KeyUtils::
        // keyToCircleOfFifthsOrder() (track/keyutils.cpp:794-804) maps each
        // ChromaticKey to a circle-of-fifths position instead, specifically
        // so harmonically compatible keys land next to each other when
        // sorted. Two real tables exist there (plain vs Lancelot/Camelot
        // notation); this app only ever displays Camelot text
        // (kKeyIdToCamelotText), so this uses the exact real
        // s_sortKeysCircleOfFifthsLancelot values for that notation, keyed
        // by key_id (which already matches mixxx's own ChromaticKey
        // numbering 1-24 exactly, confirmed by that table's own real
        // comments matching this app's kKeyIdToCamelotText order).
        orderBy = QStringLiteral(
            "CASE l.key_id "
            "WHEN 1 THEN 16 WHEN 2 THEN 6 WHEN 3 THEN 20 WHEN 4 THEN 10 "
            "WHEN 5 THEN 24 WHEN 6 THEN 14 WHEN 7 THEN 4 WHEN 8 THEN 18 "
            "WHEN 9 THEN 8 WHEN 10 THEN 22 WHEN 11 THEN 12 WHEN 12 THEN 2 "
            "WHEN 13 THEN 9 WHEN 14 THEN 23 WHEN 15 THEN 13 WHEN 16 THEN 3 "
            "WHEN 17 THEN 17 WHEN 18 THEN 7 WHEN 19 THEN 21 WHEN 20 THEN 11 "
            "WHEN 21 THEN 1 WHEN 22 THEN 15 WHEN 23 THEN 5 WHEN 24 THEN 19 "
            "ELSE 0 END");
        break;
    case SortKey::Duration:  orderBy = QStringLiteral("l.duration"); break;
    case SortKey::Rating:    orderBy = QStringLiteral("l.rating"); break;
    case SortKey::AlbumArtist:  orderBy = QStringLiteral("l.album_artist COLLATE NOCASE"); break;
    case SortKey::Composer:     orderBy = QStringLiteral("l.composer COLLATE NOCASE"); break;
    case SortKey::Grouping:     orderBy = QStringLiteral("l.grouping COLLATE NOCASE"); break;
    case SortKey::TrackNumber:  orderBy = QStringLiteral("CAST(l.tracknumber AS INTEGER)"); break;
    case SortKey::FileType:     orderBy = QStringLiteral("l.filetype COLLATE NOCASE"); break;
    case SortKey::Comment:      orderBy = QStringLiteral("l.comment COLLATE NOCASE"); break;
    case SortKey::Bitrate:      orderBy = QStringLiteral("l.bitrate"); break;
    case SortKey::ReplayGain:   orderBy = QStringLiteral("l.replaygain"); break;
    case SortKey::TimesPlayed:  orderBy = QStringLiteral("l.timesplayed"); break;
    case SortKey::LastPlayedAt: orderBy = QStringLiteral("l.last_played_at"); break;
    case SortKey::Position:
        // Only meaningful with the PlaylistTracks join below (playlistId
        // >= 0); harmless (just ties on 0) for the flat library view, which
        // never asks for this sort key anyway (see the default-sort logic
        // below, mirroring real mixxx's own "playlists sort by position by
        // default, Tracks sorts by date added" split).
        orderBy = QStringLiteral("pt.position");
        break;
    case SortKey::SampleRate: orderBy = QStringLiteral("l.samplerate"); break;
    // No backing column for Channels anywhere in this schema (see this
    // enum value's own comment) -- ties on 0, same as real mixxx sorting
    // an always-NULL column.
    case SortKey::Channels: orderBy = QStringLiteral("0"); break;
    case SortKey::Analyzed:
        orderBy = QStringLiteral("EXISTS(SELECT 1 FROM track_analysis ta WHERE ta.track_id = l.id AND ta.type = 2)");
        break;
    // Real mixxx has no special-case sort clause for this column at all
    // (library/columncache.cpp:227-244) -- it sorts on the real
    // `datetime_added` value directly. Sorting by `l.id` instead only
    // happens to match as long as ids track chronological insertion order,
    // which breaks after any re-import, restore, or bulk merge.
    case SortKey::DateAdded: default: orderBy = QStringLiteral("l.datetime_added"); break;
    }

    // Real mixxx registers a custom SQLite collation backed by QCollator
    // (util/db/dbconnection.cpp, mixxx::StringCollator) so accented text
    // sorts where a human expects, not just by raw ASCII code point --
    // SQLite's own COLLATE NOCASE only folds ASCII case, so e.g. "Bjork"
    // with the real diacritic sorts after every plain-ASCII name instead of
    // near "Bj...". Registering a custom SQL collation function needs the
    // live sqlite3* connection handle Qt's QSQLITE driver privately owns
    // (its own SQLite is statically linked into the plugin with no exported
    // symbols this module can safely call into) -- so this ports the same
    // OUTCOME a different way: for a text column's sort, skip COLLATE
    // NOCASE and instead re-sort the already-fetched rows below with a real
    // QCollator, same as mixxx's own StringCollator (case-insensitive,
    // locale-aware). SQL still does the filtering/JOINs; only the final
    // ORDER BY for these columns moves to C++.
    bool sortIsLocaleAwareText = false;
    switch (query.sort) {
    case SortKey::Title: case SortKey::Artist: case SortKey::Album: case SortKey::Genre:
    case SortKey::AlbumArtist: case SortKey::Composer: case SortKey::Grouping:
    case SortKey::FileType: case SortKey::Comment:
        sortIsLocaleAwareText = true;
        // A neutral, deterministic pre-order; the real ORDER BY for these
        // columns happens in C++ below via QCollator.
        orderBy = QStringLiteral("l.id");
        break;
    default:
        break;
    }

    QString sql = QStringLiteral(
        "SELECT l.id, l.title, l.artist, l.album, l.genre, l.year, l.duration, l.bpm, "
        "       l.key, l.key_id, l.rating, l.color, l.beats, l.beats_version, t.location, "
        "       l.samplerate, l.album_artist, l.composer, l.grouping, l.tracknumber, "
        "       l.filetype, l.comment, l.bitrate, l.replaygain, l.datetime_added, "
        "       l.timesplayed, l.last_played_at, l.bpm_lock, "
        "       l.coverart_type, l.coverart_source, l.coverart_location, l.coverart_hash, "
        "       %1, "
        "       EXISTS(SELECT 1 FROM track_analysis ta WHERE ta.track_id = l.id AND ta.type = 2), "
        "       %2, %3 "
        "FROM library l "
        "LEFT JOIN track_locations t ON t.id = l.location ");
    sql = sql.arg(QStringLiteral("%1"), QStringLiteral("%2"), energyColumn);
    // The table only exists once something has opened the library for
    // writing (see ensureWritable); until then there are no downbeats to read.
    const bool withDownbeats = hasDownbeatTable(m_db);
    sql = sql.arg(QStringLiteral("%1"), withDownbeats
                  ? QStringLiteral("db.downbeat_sec, db.confidence, db.detector")
                  : QStringLiteral("-1.0, 0.0, NULL"));
    if (withDownbeats) {
        sql += QStringLiteral("LEFT JOIN mixhouse_downbeats db ON db.track_id = l.id ");
    }
    if (query.playlistId >= 0) {
        sql = sql.arg(QStringLiteral("pt.position"));
        sql += QStringLiteral("JOIN PlaylistTracks pt ON pt.track_id = l.id AND pt.playlist_id = :pl ");
    } else {
        sql = sql.arg(QStringLiteral("-1 AS pt_position"));
    }
    if (query.crateId >= 0) {
        // Real mixxx: CrateStorage's crate_tracks is an unordered SET (just
        // crate_id+track_id, no position) -- an INNER JOIN filter is the
        // whole story, unlike the ordered-list PlaylistTracks case above.
        sql += QStringLiteral("JOIN crate_tracks ct ON ct.track_id = l.id AND ct.crate_id = :crate ");
    }
    sql += query.hiddenOnly ? QStringLiteral("WHERE l.mixxx_deleted = 1 ")
                            : QStringLiteral("WHERE l.mixxx_deleted = 0 ");
    if (query.trackId >= 0) {
        sql += QStringLiteral("AND l.id = :trackid ");
    }
    if (!query.location.isEmpty()) {
        sql += QStringLiteral("AND t.location = :location ");
    }
    if (!query.fileBaseName.isEmpty()) {
        // Narrowed by prefix in SQL, matched exactly below.
        sql += QStringLiteral("AND substr(t.filename, 1, length(:fbn)) = :fbn ");
    }
    // Real mixxx field-scoped search syntax (searchqueryparser.cpp), ported
    // in mixhousesearchquery.cpp -- artist:/bpm:/key:/etc, not just a plain
    // substring; a bare word still matches title/artist/album exactly like
    // before.
    const MixHouseParsedSearch parsedSearch = parseSearchQuery(query.filter);
    if (!parsedSearch.whereSql.isEmpty()) {
        sql += QStringLiteral("AND (%1) ").arg(parsedSearch.whereSql);
    }
    if (query.missingOnly) {
        // Real mixxx's missing_songs view (missingtablemodel.cpp): every
        // track whose file has gone missing from disk, regardless of
        // whichever crate/playlist it might also belong to.
        sql += QStringLiteral("AND t.fs_deleted = 1 ");
    }
    if (query.unanalyzedOnly) {
        sql += QStringLiteral("AND NOT EXISTS(SELECT 1 FROM track_analysis ta WHERE ta.track_id = l.id AND ta.type = 2) ");
    }
    if (query.playedOnly) {
        sql += QStringLiteral("AND l.last_played_at IS NOT NULL ");
    }
    if (!query.keyFilters.isEmpty()) {
        // One placeholder per selected key: the wheel selects a SET, and an IN
        // list is what lets "show me 8A and 9A" be a single query.
        QStringList marks;
        for (int i = 0; i < query.keyFilters.size(); ++i) {
            marks << QStringLiteral(":k%1").arg(i);
        }
        // By key id, so a key written in any notation finds the same tracks
        // whatever text a track happens to have stored.
        sql += QStringLiteral("AND l.key_id IN (%1) ").arg(marks.join(QStringLiteral(", ")));
    }
    sql += QStringLiteral("ORDER BY %1 %2 ").arg(orderBy,
             query.ascending ? QStringLiteral("ASC") : QStringLiteral("DESC"));
    // A locale-aware text sort happens in C++ below, AFTER fetching every
    // matching row -- same "don't sort an already-truncated page" rule the
    // comment above this function states, just with the real sort moved
    // past the SQL boundary. Applying SQL's LIMIT here would truncate
    // BEFORE that real sort ever runs.
    if (query.limit > 0 && !sortIsLocaleAwareText) {
        sql += QStringLiteral("LIMIT :lim");
    }

    QSqlQuery q(m_db);
    q.setForwardOnly(true);
    if (!q.prepare(sql)) {
        LOGE() << "library query prepare failed: " << q.lastError().text();
        return out;
    }
    if (query.playlistId >= 0) { q.bindValue(QStringLiteral(":pl"), query.playlistId); }
    if (query.crateId >= 0) { q.bindValue(QStringLiteral(":crate"), query.crateId); }
    if (query.trackId >= 0) { q.bindValue(QStringLiteral(":trackid"), query.trackId); }
    if (!query.location.isEmpty()) { q.bindValue(QStringLiteral(":location"), query.location); }
    if (!query.fileBaseName.isEmpty()) { q.bindValue(QStringLiteral(":fbn"), query.fileBaseName); }
    for (const MixHouseSearchBind& bind : parsedSearch.binds) {
        q.bindValue(QStringLiteral(":") + bind.name, bind.value);
    }
    for (int i = 0; i < query.keyFilters.size(); ++i) {
        q.bindValue(QStringLiteral(":k%1").arg(i), keyIdFromText(query.keyFilters.at(i)));
    }
    if (query.limit > 0 && !sortIsLocaleAwareText) { q.bindValue(QStringLiteral(":lim"), query.limit); }

    if (!q.exec()) {
        LOGE() << "library query failed: " << q.lastError().text();
        return out;
    }

    while (q.next()) {
        MixHouseTrack t;
        t.id           = q.value(0).toInt();
        t.title        = q.value(1).toString();
        t.artist       = q.value(2).toString();
        t.album        = q.value(3).toString();
        t.genre        = q.value(4).toString();
        t.year         = q.value(5).toString();
        // Real mixxx returns an empty QVariant (blank cell) for a NULL
        // duration -- a track whose header hasn't been parsed yet -- rather
        // than reading it as 0 (basetracktablemodel.cpp:620-624). -1 is
        // this struct's existing "unknown" convention elsewhere (color,
        // crateId, ...); MixHouseLibraryPanel.qml's durationSec formatter
        // blanks on < 0 rather than showing a real 0:00.
        t.durationSec  = q.value(6).isNull() ? -1.0 : q.value(6).toDouble();
        t.bpm          = q.value(7).toDouble();
        t.keyId        = q.value(9).toInt();
        // Normalize the DISPLAYED key text from key_id (Camelot, see
        // kKeyIdToCamelotText's own comment) rather than trusting the raw
        // stored `key` column verbatim -- the reference library has a real mix of
        // notations from different tools/times, and this keeps every row
        // showing the same convention regardless of what text happens to be
        // stored, self-healing without a data migration. Falls back to the
        // raw text only for the (currently nonexistent, checked directly
        // against his real data) edge case of a key_id outside 1-24.
        // Written in the notation chosen in Preferences > Music library.
        t.key = (t.keyId >= 1 && t.keyId <= 24)
            ? IMixHouseKeyMatch::keyText(t.keyId, libraryConfiguration()->keyNotation())
            : q.value(8).toString();
        t.rating       = q.value(10).toInt();
        t.color        = q.value(11).isNull() ? -1 : q.value(11).toInt();
        t.beatsBlob    = q.value(12).toByteArray();
        t.beatsVersion = q.value(13).toString();
        t.filePath     = q.value(14).toString();
        t.sampleRate   = q.value(15).toInt();
        t.albumArtist  = q.value(16).toString();
        t.composer     = q.value(17).toString();
        t.grouping     = q.value(18).toString();
        t.trackNumber  = q.value(19).toString();
        t.fileType     = q.value(20).toString();
        t.comment      = q.value(21).toString();
        t.bitrate      = q.value(22).toInt();
        t.replaygain   = q.value(23).toDouble();
        t.dateAdded    = q.value(24).toString();
        t.timesPlayed  = q.value(25).toInt();
        t.lastPlayedAt = q.value(26).isNull() ? QString() : q.value(26).toString();
        t.bpmLocked    = q.value(27).toInt() != 0;
        t.coverArtType   = q.value(28).toInt();
        t.coverArtSource = q.value(29).toInt();
        t.coverArtLocation = q.value(30).toString();
        t.coverArtHash   = static_cast<quint16>(q.value(31).toUInt());
        t.playlistPosition = q.value(32).toInt();
        t.analyzed     = q.value(33).toInt() != 0;
        t.downbeatSec  = q.value(34).isNull() ? -1.0 : q.value(34).toDouble();
        t.downbeatConfidence = q.value(35).isNull() ? 0.0 : q.value(35).toDouble();
        t.downbeatDetector = q.value(36).toString();
        t.energy       = q.value(37).isNull() ? 0.0 : q.value(37).toDouble();
        if (!query.fileBaseName.isEmpty() && QFileInfo(t.filePath).completeBaseName() != query.fileBaseName) {
            continue;
        }
        out.push_back(std::move(t));
    }

    if (sortIsLocaleAwareText) {
        // Real mixxx: mixxx::StringCollator (util/string.h) -- a QCollator,
        // case-insensitive, defaulted to the system locale, so e.g. "Bjork"
        // with the real diacritic sorts near "Bj..." instead of after every
        // plain-ASCII name the way SQL's own COLLATE NOCASE (ASCII-only)
        // left it. See this function's own comment above for why this runs
        // here in C++ instead of as a registered SQL collation.
        QCollator collator;
        collator.setCaseSensitivity(Qt::CaseInsensitive);
        auto textOf = [&query](const MixHouseTrack& t) -> const QString& {
            switch (query.sort) {
            case SortKey::Title: return t.title;
            case SortKey::Artist: return t.artist;
            case SortKey::Album: return t.album;
            case SortKey::Genre: return t.genre;
            case SortKey::AlbumArtist: return t.albumArtist;
            case SortKey::Composer: return t.composer;
            case SortKey::Grouping: return t.grouping;
            case SortKey::FileType: return t.fileType;
            case SortKey::Comment: return t.comment;
            default: return t.title;
            }
        };
        std::stable_sort(out.begin(), out.end(), [&](const MixHouseTrack& a, const MixHouseTrack& b) {
            const int cmp = collator.compare(textOf(a), textOf(b));
            return query.ascending ? cmp < 0 : cmp > 0;
        });
        if (query.limit > 0 && static_cast<int>(out.size()) > query.limit) {
            out.resize(static_cast<size_t>(query.limit));
        }
    }

    // Beatgrids only for the rows kept: a text sort fetches the whole library.
    // A beatgrid stores its beats as frame positions, so it needs the sample
    // rate, and tracks added by this app's own scanner (or imported from
    // rekordbox) never had it filled in. Reading it from the file header is
    // only done for a one-track lookup (placing a song, the grid editor); a
    // list never opens files -- sorting 38k tracks by Artist read 34,674
    // headers off an external drive and froze the app for 125 s (2026-11-01).
    // fillMissingSampleRates() writes them into the library in the background
    // instead; until it gets to a row, that row has no grid.
    const bool oneTrack = query.trackId >= 0 || !query.location.isEmpty() || !query.fileBaseName.isEmpty();
    for (MixHouseTrack& t : out) {
        if (t.sampleRate <= 0 && !t.beatsBlob.isEmpty() && !t.filePath.isEmpty()) {
            auto cached = m_probedSampleRates.find(t.filePath);
            if (cached == m_probedSampleRates.end() && oneTrack) {
                const AudioInfo info = probeAudioFile(t.filePath);
                ++headerReads;
                cached = m_probedSampleRates.insert(t.filePath, info.valid ? info.sampleRate : 0);
            }
            if (cached != m_probedSampleRates.end()) {
                t.sampleRate = cached.value();
            }
        }
        t.beats = decodeBeats(t.beatsBlob, t.beatsVersion, t.sampleRate);
    }

    if (timer.elapsed() >= 50) {
        LOGI() << "GlideMix library query: " << out.size() << " rows in " << timer.elapsed() << " ms, "
               << headerReads << " file header reads";
    }
    return out;
}

IMixHouseLibrary::MatchingTracks MixHouseLibrary::matchingTracks(const TrackQuery& query) const
{
    // Same FROM/JOIN/WHERE this file's own tracks() builds -- deliberately
    // duplicated rather than shared, so a future edit to tracks()'s complex
    // sort/collation handling can't accidentally change what a bulk action
    // considers "matching" (this function has no sort and no limit at all).
    // See this function's own declaration in imixhouselibrary.h for why it
    // exists: tracks()'s query.limit must never be what a "Select All" bulk
    // action resolves against.
    MatchingTracks out;
    if (!ensureOpen()) {
        return out;
    }

    QString sql = QStringLiteral(
        "SELECT l.id, %1 FROM library l LEFT JOIN track_locations t ON t.id = l.location ");
    if (query.playlistId >= 0) {
        sql = sql.arg(QStringLiteral("pt.position"));
        sql += QStringLiteral("JOIN PlaylistTracks pt ON pt.track_id = l.id AND pt.playlist_id = :pl ");
    } else {
        sql = sql.arg(QStringLiteral("-1"));
    }
    if (query.crateId >= 0) {
        sql += QStringLiteral("JOIN crate_tracks ct ON ct.track_id = l.id AND ct.crate_id = :crate ");
    }
    sql += query.hiddenOnly ? QStringLiteral("WHERE l.mixxx_deleted = 1 ")
                            : QStringLiteral("WHERE l.mixxx_deleted = 0 ");
    const MixHouseParsedSearch parsedSearch = parseSearchQuery(query.filter);
    if (!parsedSearch.whereSql.isEmpty()) {
        sql += QStringLiteral("AND (%1) ").arg(parsedSearch.whereSql);
    }
    if (query.missingOnly) {
        sql += QStringLiteral("AND t.fs_deleted = 1 ");
    }
    if (query.unanalyzedOnly) {
        sql += QStringLiteral("AND NOT EXISTS(SELECT 1 FROM track_analysis ta WHERE ta.track_id = l.id AND ta.type = 2) ");
    }
    if (query.playedOnly) {
        sql += QStringLiteral("AND l.last_played_at IS NOT NULL ");
    }
    if (!query.keyFilters.isEmpty()) {
        QStringList marks;
        for (int i = 0; i < query.keyFilters.size(); ++i) {
            marks << QStringLiteral(":k%1").arg(i);
        }
        // By key id, so a key written in any notation finds the same tracks
        // whatever text a track happens to have stored.
        sql += QStringLiteral("AND l.key_id IN (%1) ").arg(marks.join(QStringLiteral(", ")));
    }

    QSqlQuery q(m_db);
    q.setForwardOnly(true);
    if (!q.prepare(sql)) {
        LOGE() << "matchingTracks query prepare failed: " << q.lastError().text();
        return out;
    }
    if (query.playlistId >= 0) { q.bindValue(QStringLiteral(":pl"), query.playlistId); }
    if (query.crateId >= 0) { q.bindValue(QStringLiteral(":crate"), query.crateId); }
    for (const MixHouseSearchBind& bind : parsedSearch.binds) {
        q.bindValue(QStringLiteral(":") + bind.name, bind.value);
    }
    for (int i = 0; i < query.keyFilters.size(); ++i) {
        q.bindValue(QStringLiteral(":k%1").arg(i), keyIdFromText(query.keyFilters.at(i)));
    }
    if (!q.exec()) {
        LOGE() << "matchingTracks query failed: " << q.lastError().text();
        return out;
    }
    while (q.next()) {
        out.ids.append(q.value(0).toInt());
        out.playlistPositions.append(q.value(1).toInt());
    }
    return out;
}

// Playlists and crates share a shape; Mixxx stores them in differently-named
// tables with the same idea, so one helper query serves both.
static MixHouseNamedList namedListQuery(QSqlDatabase& db, const QString& sql)
{
    MixHouseNamedList out;
    QSqlQuery q(db);
    q.setForwardOnly(true);
    if (!q.exec(sql)) {
        return out;
    }
    while (q.next()) {
        MixHouseNamedItem it;
        it.id = q.value(0).toInt();
        it.name = q.value(1).toString();
        it.count = q.value(2).toInt();
        it.locked = q.value(3).toInt() != 0;
        out.push_back(std::move(it));
    }
    return out;
}

// Shared shape for the three kinds of row in Mixxx's Playlists table.
// `hidden` is PlaylistDAO::HiddenType: 0 real playlist, 1 Auto DJ, 2 history.
MixHouseNamedList MixHouseLibrary::playlistsOfType(int hidden, bool newestFirst) const
{
    if (!ensureOpen()) {
        return {};
    }
    const QString order = newestFirst
        ? QStringLiteral("p.id DESC")
        : QStringLiteral("p.name COLLATE NOCASE");
    return namedListQuery(m_db, QStringLiteral(
        "SELECT p.id, p.name, COUNT(pt.track_id), p.locked "
        "FROM Playlists p LEFT JOIN PlaylistTracks pt ON pt.playlist_id = p.id "
        "WHERE p.hidden = %1 "
        "GROUP BY p.id, p.name, p.locked ORDER BY %2").arg(hidden).arg(order));
}

// Real playlists (hidden=0) get their own query, not playlistsOfType() above
// -- folders/nesting (parent_id, is_folder) is a real-Playlists-only
// concept, never meaningful for Auto DJ or History rows, so this stays
// separate rather than growing namedListQuery()'s shared 4-column shape (and
// every one of its callers) for two columns only one of them needs. See
// MixHouseNamedItem::parentId's own comment for why this is original work,
// not a port.
QList<int> MixHouseLibrary::playlistsContainingTrack(int trackId) const
{
    QList<int> ids;
    if (trackId < 0 || !ensureOpen()) {
        return ids;
    }
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT DISTINCT playlist_id FROM PlaylistTracks WHERE track_id = :id"));
    q.bindValue(QStringLiteral(":id"), trackId);
    if (q.exec()) {
        while (q.next()) {
            ids << q.value(0).toInt();
        }
    }
    return ids;
}

MixHouseNamedList MixHouseLibrary::playlists() const
{
    // ensureWritable(), not just ensureOpen() -- m_db (this method's own
    // read connection, used just below) is opened QSQLITE_OPEN_READONLY on
    // purpose and genuinely cannot run the parent_id/is_folder/sort_order
    // ALTER TABLEs those columns below need (see ensureWritable()'s own
    // migration comments), so this has to guarantee that's already run
    // before the SELECT below can rely on them existing at all -- otherwise
    // the very first time this is ever called in a session, before any
    // other real write has happened, this query would fail outright on a
    // column that doesn't exist yet. Free after the first call in a
    // session (ensureWritable() itself early-returns once m_writeOpen is
    // already true), so no real cost from calling it here on every sidebar
    // refresh.
    if (!const_cast<MixHouseLibrary*>(this)->ensureWritable()) {
        return {};
    }
    QSqlQuery q(m_db);
    q.setForwardOnly(true);
    if (!q.exec(QStringLiteral(
            "SELECT p.id, p.name, COUNT(pt.track_id), p.locked, p.parent_id, p.is_folder "
            "FROM Playlists p LEFT JOIN PlaylistTracks pt ON pt.playlist_id = p.id "
            "WHERE p.hidden = 0 "
            "GROUP BY p.id, p.name, p.locked, p.parent_id, p.is_folder "
            "ORDER BY p.sort_order, p.name COLLATE NOCASE"))) {
        return {};
    }
    MixHouseNamedList out;
    while (q.next()) {
        MixHouseNamedItem it;
        it.id = q.value(0).toInt();
        it.name = q.value(1).toString();
        it.count = q.value(2).toInt();
        it.locked = q.value(3).toInt() != 0;
        it.parentId = q.value(4).toInt();
        it.isFolder = q.value(5).toInt() != 0;
        out.push_back(std::move(it));
    }
    return out;
}
MixHouseNamedList MixHouseLibrary::autoDjQueue() const { return playlistsOfType(1, false); }
// Newest session first -- a DJ wants last night, not a date from 2024.
MixHouseNamedList MixHouseLibrary::historySessions() const { return playlistsOfType(2, true); }

int MixHouseLibrary::missingTrackCount() const
{
    if (!ensureOpen()) {
        return 0;
    }
    QSqlQuery q(m_db);
    // "Missing" is Mixxx's own definition: the file is gone from disk.
    if (!q.exec(QStringLiteral(
            "SELECT COUNT(*) FROM library l JOIN track_locations t ON t.id = l.location "
            "WHERE l.mixxx_deleted = 0 AND t.fs_deleted = 1")) || !q.next()) {
        return 0;
    }
    return q.value(0).toInt();
}

int MixHouseLibrary::verifyTrackLocations()
{
    if (!ensureWritable()) {
        return missingTrackCount();
    }

    // Real mixxx's TrackDAO::verifyRemainingTracks, ported directly: for
    // every track_locations row, ask the filesystem whether it's still
    // there and set fs_deleted accordingly (trackdao.cpp:2143). Real mixxx
    // only runs this over the subset a directory-hash scan decided still
    // needs checking (needs_verification); this port has no such
    // incremental scanner, so it just checks every row every time --
    // correct, just not the same performance optimization.
    QSqlQuery sel(m_writeDb);
    if (!sel.exec(QStringLiteral("SELECT id, location, fs_deleted FROM track_locations"))) {
        LOGE() << "verifyTrackLocations: select failed: " << sel.lastError().text();
        return missingTrackCount();
    }
    QList<QPair<int, int>> updates;   // (id, newFsDeleted), only where changed
    while (sel.next()) {
        const int id = sel.value(0).toInt();
        const QString location = sel.value(1).toString();
        const int oldFsDeleted = sel.value(2).toInt();
        const int newFsDeleted = QFileInfo::exists(location) ? 0 : 1;
        if (newFsDeleted != oldFsDeleted) {
            updates.append({ id, newFsDeleted });
        }
    }

    if (!updates.isEmpty()) {
        QSqlQuery upd(m_writeDb);
        upd.prepare(QStringLiteral(
            "UPDATE track_locations SET fs_deleted = :fs, needs_verification = 0 WHERE id = :id"));
        m_writeDb.transaction();
        for (const auto& [id, fsDeleted] : updates) {
            upd.bindValue(QStringLiteral(":fs"), fsDeleted);
            upd.bindValue(QStringLiteral(":id"), id);
            if (!upd.exec()) {
                LOGW() << "verifyTrackLocations: update failed for track_locations id "
                       << id << ": " << upd.lastError().text();
            }
        }
        if (!m_writeDb.commit()) {
            m_writeDb.rollback();
            LOGE() << "verifyTrackLocations: commit failed: " << m_writeDb.lastError().text();
        } else {
            LOGI() << "verifyTrackLocations: " << updates.size() << " track_locations row(s) changed";
            m_libraryChanged.notify();
        }
    }

    return missingTrackCount();
}

int MixHouseLibrary::hiddenTrackCount() const
{
    if (!ensureOpen()) {
        return 0;
    }
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("SELECT COUNT(*) FROM library WHERE mixxx_deleted = 1")) || !q.next()) {
        return 0;
    }
    return q.value(0).toInt();
}

MixHouseNamedList MixHouseLibrary::crates() const
{
    if (!ensureOpen()) {
        return {};
    }
    return namedListQuery(m_db, QStringLiteral(
        "SELECT c.id, c.name, COUNT(ct.track_id), c.locked "
        "FROM crates c LEFT JOIN crate_tracks ct ON ct.crate_id = c.id "
        "GROUP BY c.id, c.name, c.locked ORDER BY c.name COLLATE NOCASE"));
}

QHash<QString, std::pair<int, int> > MixHouseLibrary::ratingsAndColorsByLocation() const
{
    QHash<QString, std::pair<int, int> > out;
    if (!ensureOpen()) {
        return out;
    }
    // The connection that wrote them: right after a rating change the read
    // connection still answered with the old value.
    QSqlQuery q(m_writeDb.isValid() && m_writeDb.isOpen() ? m_writeDb : m_db);
    q.setForwardOnly(true);
    if (!q.exec(QStringLiteral(
            "SELECT t.location, l.rating, l.color FROM library l "
            "JOIN track_locations t ON t.id = l.location "
            "WHERE l.mixxx_deleted = 0 AND (l.rating > 0 OR l.color IS NOT NULL)"))) {
        return out;
    }
    while (q.next()) {
        const QVariant color = q.value(2);
        out.insert(q.value(0).toString(),
                   { q.value(1).toInt(), color.isNull() ? -1 : color.toInt() });
    }
    return out;
}

QStringList MixHouseLibrary::keys() const
{
    QStringList out;
    if (!ensureOpen()) {
        return out;
    }
    QSqlQuery q(m_db);
    q.setForwardOnly(true);
    if (!q.exec(QStringLiteral(
            "SELECT DISTINCT key FROM library WHERE mixxx_deleted = 0 "
            "AND key IS NOT NULL AND key != '' ORDER BY key_id"))) {
        return out;
    }
    while (q.next()) {
        out << q.value(0).toString();
    }
    return out;
}

// ---------------------------------------------------------------------------
// Adding tracks.
//
// Mixxx's own two inserts, matched column for column from
// mixxx/src/library/dao/trackdao.cpp:
//   track_locations(location, directory, filename, filesize, fs_deleted,
//                   needs_verification)
//   library(title, artist, filetype, location, header_parsed, mixxx_deleted,
//           datetime_added, ...)
//
// Mixxx's scanner adds a file first with header_parsed = 0 and lets the analyzer
// fill in duration/bpm/key afterwards; this does the same, so a scanned track is
// immediately visible and gets its analysis later.

static const QStringList kAudioExtensions = {
    QStringLiteral("mp3"), QStringLiteral("m4a"), QStringLiteral("aac"),
    QStringLiteral("flac"), QStringLiteral("ogg"), QStringLiteral("opus"),
    QStringLiteral("wav"), QStringLiteral("aiff"), QStringLiteral("aif"),
    QStringLiteral("wv")
};

bool MixHouseLibrary::backupOnce()
{
    if (m_backedUp) {
        return true;
    }
    // Before the FIRST write of a session, copy the database. This is the project's
    // live collection and a bad write would be unrecoverable otherwise.
    QElapsedTimer timer;
    timer.start();
    const QString backup = m_path + QStringLiteral(".mixhouse-backup");
    QFile::remove(backup);
    if (!QFile::copy(m_path, backup)) {
        LOGE() << "could not back up the library before writing: " << backup;
        return false;
    }
    LOGI() << "library backed up to " << backup << " in " << timer.elapsed() << " ms";
    m_backedUp = true;
    return true;
}

bool MixHouseLibrary::ensureWritable()
{
    if (m_writeOpen) {
        return true;
    }
    if (!ensureOpen() || !backupOnce()) {
        return false;
    }
    m_writeDb = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                          QStringLiteral("mixhouse_library_rw_") + m_connectionSuffix);
    m_writeDb.setDatabaseName(m_path);
    if (!m_writeDb.open()) {
        LOGE() << "could not open the library for writing: " << m_writeDb.lastError().text();
        return false;
    }
    m_writeOpen = true;

    // Real bug, confirmed against the reference library: track_locations.location
    // is UNIQUE (so findExistingTrackId's lookup is already indexed for free --
    // see sqlite_autoindex_track_locations_1), but (filename, filesize) --
    // findExistingTrackByFilenameAndSize's own lookup, the rekordbox-path-
    // mismatch fallback used by every track ensureTrackForRekordboxEntry()
    // processes -- had NO index at all. Every fallback lookup was a full
    // table scan of the whole library, and a real 70k-track rekordbox
    // collection triggers that fallback for the vast majority of its
    // entries (their exact stored path essentially never matches this
    // library's own scanned path) -- confirmed processing barely 300 tracks
    // in 3 hours before this was caught. CREATE INDEX IF NOT EXISTS is cheap
    // and safe to run on every write-open, not just the first ever one, so
    // a library whose write connection was already opened before this fix
    // shipped still gets it the next time it writes anything.
    QSqlQuery downbeatTable(m_writeDb);
    if (!downbeatTable.exec(QString::fromLatin1(kCreateDownbeatTableSql))) {
        LOGW() << "could not create mixhouse_downbeats: " << downbeatTable.lastError().text();
    }
    QSqlQuery energyTable(m_writeDb);
    if (!energyTable.exec(QString::fromLatin1(kCreateEnergyTableSql))) {
        LOGW() << "could not create mixhouse_energy: " << energyTable.lastError().text();
    }
    QSqlQuery anchorTable(m_writeDb);
    if (!anchorTable.exec(QString::fromLatin1(kCreateAnchorTableSql))) {
        LOGW() << "could not create mixhouse_anchors: " << anchorTable.lastError().text();
    }
    QSqlQuery phraseTable(m_writeDb);
    if (!phraseTable.exec(QString::fromLatin1(kCreatePhraseTableSql))) {
        LOGW() << "could not create mixhouse_phrases: " << phraseTable.lastError().text();
    }
    QSqlQuery modelPhraseTable(m_writeDb);
    if (!modelPhraseTable.exec(QString::fromLatin1(kCreateModelPhraseTableSql))) {
        LOGW() << "could not create mixhouse_model_phrases: " << modelPhraseTable.lastError().text();
    }
    QSqlQuery tagEditTable(m_writeDb);
    if (!tagEditTable.exec(QString::fromLatin1(kCreateTagEditTableSql))) {
        LOGW() << "could not create mixhouse_tag_edits: " << tagEditTable.lastError().text();
    }
    QSqlQuery smartTable(m_writeDb);
    if (!smartTable.exec(QString::fromLatin1(kCreateSmartPlaylistTableSql))) {
        LOGW() << "could not create mixhouse_smart_playlists: " << smartTable.lastError().text();
    }

    QSqlQuery indexQuery(m_writeDb);
    if (!indexQuery.exec(QStringLiteral(
            "CREATE INDEX IF NOT EXISTS mixhouse_idx_track_locations_filename_size "
            "ON track_locations (filename, filesize)"))) {
        LOGW() << "could not create filename/filesize index: " << indexQuery.lastError().text();
    }

    // Playlist folders/nesting -- see MixHouseNamedItem::parentId's own
    // comment for why this is original schema work, not a port (real
    // mixxx's own Playlists table has no folder concept at all). `ALTER
    // TABLE ... ADD COLUMN IF NOT EXISTS` needs SQLite 3.35+ -- rather than
    // assume the Qt SQLite driver this app links is new enough, this checks
    // PRAGMA table_info() first, the same way real mixxx's own
    // SchemaManager checks a real schema_version table before running any
    // migration, just against the live columns directly since this port
    // owns no schema_version row of its own to key off. Safe to run on
    // every write-open, same reasoning as the index above.
    QSqlQuery columnCheck(m_writeDb);
    if (columnCheck.exec(QStringLiteral("PRAGMA table_info(Playlists)"))) {
        bool hasParentId = false;
        bool hasIsFolder = false;
        bool hasSortOrder = false;
        while (columnCheck.next()) {
            const QString colName = columnCheck.value(1).toString();
            if (colName == QStringLiteral("parent_id")) { hasParentId = true; }
            if (colName == QStringLiteral("is_folder")) { hasIsFolder = true; }
            if (colName == QStringLiteral("sort_order")) { hasSortOrder = true; }
        }
        if (!hasParentId) {
            QSqlQuery alter(m_writeDb);
            if (!alter.exec(QStringLiteral("ALTER TABLE Playlists ADD COLUMN parent_id INTEGER DEFAULT -1 NOT NULL"))) {
                LOGW() << "could not add Playlists.parent_id: " << alter.lastError().text();
            }
        }
        if (!hasIsFolder) {
            QSqlQuery alter(m_writeDb);
            if (!alter.exec(QStringLiteral("ALTER TABLE Playlists ADD COLUMN is_folder INTEGER DEFAULT 0 NOT NULL"))) {
                LOGW() << "could not add Playlists.is_folder: " << alter.lastError().text();
            }
        }
        if (!hasSortOrder) {
            // Manual playlist/folder reordering -- "default should be
            // alphabetical order... but this ordering should be via right
            // click" (drag-to-reorder noted as a real follow-up, not built
            // yet). Deliberately its OWN column, not a reuse of the
            // PRE-EXISTING real-mixxx `position` column: every row already
            // has a real, distinct `position` value (this port's own
            // createPlaylist() sets it to MAX(position)+1 on insert, a
            // creation-order artifact, not a curated display order) --
            // sorting by that today would silently replace the current
            // alphabetical default with creation order for every playlist
            // that's never been manually reordered. -1 (the default, for
            // every existing AND future row until it's actually touched)
            // means "no manual order set for this row" -- see
            // movePlaylistOrder()'s own comment for how a real order gets
            // assigned the first time it's used.
            QSqlQuery alter(m_writeDb);
            if (!alter.exec(QStringLiteral("ALTER TABLE Playlists ADD COLUMN sort_order INTEGER DEFAULT -1 NOT NULL"))) {
                LOGW() << "could not add Playlists.sort_order: " << alter.lastError().text();
            }
        }
    } else {
        LOGW() << "could not read Playlists schema: " << columnCheck.lastError().text();
    }
    return true;
}

au::mixhouse::IMixHouseLibrary::ScanResult MixHouseLibrary::addTracksFromFolder(const QString& folder)
{
    ScanResult res;
    if (!ensureWritable()) {
        res.error = QStringLiteral("Could not open the library for writing.");
        return res;
    }

    QDirIterator it(folder,
                    QDir::Files | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);

    QSqlQuery findLoc(m_writeDb);
    findLoc.prepare(QStringLiteral(
        "SELECT l.id, l.duration FROM library l JOIN track_locations t "
        "ON t.id = l.location WHERE t.location = :loc"));
    QSqlQuery repairDuration(m_writeDb);
    repairDuration.prepare(QStringLiteral(
        "UPDATE library SET duration = :duration WHERE id = :id"));

    QSqlQuery insLoc(m_writeDb);
    insLoc.prepare(QStringLiteral(
        "INSERT INTO track_locations (location, directory, filename, filesize, "
        "fs_deleted, needs_verification) "
        "VALUES (:location, :directory, :filename, :filesize, 0, 0)"));

    QSqlQuery insLib(m_writeDb);
    insLib.prepare(QStringLiteral(
        "INSERT INTO library (title, artist, album, genre, year, replaygain, replaygain_peak, filetype, location, header_parsed, "
        "mixxx_deleted, played, timesplayed, rating, bpm, duration, datetime_added) "
        "VALUES (:title, :artist, :album, :genre, :year, :replaygain, :replaygainPeak, :filetype, :location, :parsed, 0, 0, 0, 0, 0, :duration, CURRENT_TIMESTAMP)"));

    m_writeDb.transaction();
    while (it.hasNext()) {
        const QString path = it.next();
        const QFileInfo fi(path);
        if (!kAudioExtensions.contains(fi.suffix().toLower())) {
            continue;
        }
        ++res.scanned;

        // Real bug, confirmed against the reference library: this whole
        // function runs on the calling thread with no yielding at all --
        // fine for a small folder, but runStartupRescanIfNeeded() calls
        // this synchronously from QML at panel load, and a real ~35k-track
        // collection (worse right after a big rekordbox import adds
        // thousands more files at once) can keep this loop busy long enough
        // for macOS's own "Application Not Responding" watchdog to fire --
        // confirmed: that's the exact freeze we hit on reopen, not a
        // crash in the ordinary sense. A real fix would move this whole
        // scan to a worker thread (real mixxx's own LibraryScanner does
        // exactly that); short of that larger rewrite, periodically handing
        // control back to the event loop keeps the UI thread pumping
        // (repainting, accepting input, and letting a progress dialog
        // actually animate) without changing what this loop does at all.
        if ((res.scanned % 200) == 0) {
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        }

        findLoc.bindValue(QStringLiteral(":loc"), path);
        if (findLoc.exec() && findLoc.next()) {
            const QVariant oldDuration = findLoc.value(1);
            if (oldDuration.isNull() || oldDuration.toDouble() <= 0.0) {
                const AudioInfo info = probeAudioFile(path);
                if (info.valid && info.durationSec > 0.0) {
                    repairDuration.bindValue(QStringLiteral(":duration"), info.durationSec);
                    repairDuration.bindValue(QStringLiteral(":id"), findLoc.value(0));
                    if (!repairDuration.exec()) {
                        LOGW() << "scan: could not repair duration for " << path << ": "
                               << repairDuration.lastError().text();
                    }
                }
            }
            ++res.skipped;          // already known; never re-add or modify
            continue;
        }

        // GlideMix: a file that is not readable audio (damaged, empty, not
        // what its name says) is not added -- it would only fail later.
        const AudioInfo info = probeAudioFile(path);
        if (!info.valid) {
            ++res.unreadable;
            LOGW() << "scan: not readable audio, not added: " << path << ": " << info.error;
            continue;
        }
        insLoc.bindValue(QStringLiteral(":location"), path);
        insLoc.bindValue(QStringLiteral(":directory"), fi.absolutePath());
        insLoc.bindValue(QStringLiteral(":filename"), fi.fileName());
        insLoc.bindValue(QStringLiteral(":filesize"), fi.size());
        if (!insLoc.exec()) {
            LOGW() << "track_locations insert failed for " << path << ": "
                   << insLoc.lastError().text();
            continue;
        }
        const int locId = insLoc.lastInsertId().toInt();

        // Title from the filename, like Mixxx does before the tags are parsed.
        {
            // GlideMix: the file's tags (Artist, Title...), the file name only
            // when it has none -- the stream and the library showed file names.
            const MixHouseFileTags tags = readFileTags(path);
            insLib.bindValue(QStringLiteral(":title"), tags.title);
            insLib.bindValue(QStringLiteral(":artist"), tags.artist);
            insLib.bindValue(QStringLiteral(":album"), tags.album);
            insLib.bindValue(QStringLiteral(":genre"), tags.genre);
            insLib.bindValue(QStringLiteral(":year"), tags.year);
            insLib.bindValue(QStringLiteral(":replaygain"), tags.replayGain);
            insLib.bindValue(QStringLiteral(":replaygainPeak"), tags.replayGainPeak);
            insLib.bindValue(QStringLiteral(":parsed"), tags.fromTags ? 1 : 0);
        }
        insLib.bindValue(QStringLiteral(":filetype"), fi.suffix().toLower());
        insLib.bindValue(QStringLiteral(":location"), locId);
        insLib.bindValue(QStringLiteral(":duration"),
                         info.valid && info.durationSec > 0.0 ? QVariant(info.durationSec) : QVariant());
        if (!insLib.exec()) {
            LOGW() << "library insert failed for " << path << ": " << insLib.lastError().text();
            continue;
        }
        ++res.added;
    }

    if (!m_writeDb.commit()) {
        m_writeDb.rollback();
        res.error = QStringLiteral("Could not commit: %1").arg(m_writeDb.lastError().text());
        return res;
    }

    LOGI() << "GlideMix scan of " << folder << ": " << res.scanned << " audio files, "
           << res.added << " added, " << res.skipped << " already known";
    // Tell whatever Tracks view happens to be on screen to reload -- this
    // write goes through MixHouseActionsController (a File-menu action),
    // which has no reference to that model at all, so without this the
    // scan can genuinely add every row back and the visible list would
    // still show 0 tracks until something else happened to reload it.
    if (res.added > 0) {
        m_libraryChanged.notify();
    }
    return res;
}

bool MixHouseLibrary::ensureTrackForFile(const QString& path, int& outLibraryId, bool& outWasNew)
{
    // Shared by addTracksFromFiles and the two XML importers below --
    // addTracksFromFolder above is left untouched (already shipped and
    // verified working) rather than refactored onto this helper.
    QSqlQuery find(m_writeDb);
    find.prepare(QStringLiteral(
        "SELECT l.id FROM library l JOIN track_locations t ON t.id = l.location WHERE t.location = :loc"));
    find.bindValue(QStringLiteral(":loc"), path);
    if (!find.exec()) {
        LOGW() << "ensureTrackForFile: lookup failed for " << path << ": " << find.lastError().text();
        return false;
    }
    if (find.next()) {
        outLibraryId = find.value(0).toInt();
        outWasNew = false;
        return true;
    }

    const QFileInfo fi(path);
    // GlideMix: not readable audio -- not added (see addTracksFromFolder).
    const AudioInfo probed = probeAudioFile(path);
    if (!probed.valid) {
        LOGW() << "ensureTrackForFile: not readable audio, not added: " << path << ": " << probed.error;
        return false;
    }
    QSqlQuery insLoc(m_writeDb);
    insLoc.prepare(QStringLiteral(
        "INSERT INTO track_locations (location, directory, filename, filesize, "
        "fs_deleted, needs_verification) VALUES (:location, :directory, :filename, :filesize, 0, 0)"));
    insLoc.bindValue(QStringLiteral(":location"), path);
    insLoc.bindValue(QStringLiteral(":directory"), fi.absolutePath());
    insLoc.bindValue(QStringLiteral(":filename"), fi.fileName());
    insLoc.bindValue(QStringLiteral(":filesize"), fi.size());
    if (!insLoc.exec()) {
        LOGW() << "ensureTrackForFile: track_locations insert failed for " << path << ": " << insLoc.lastError().text();
        return false;
    }
    const int locId = insLoc.lastInsertId().toInt();

    QSqlQuery insLib(m_writeDb);
    insLib.prepare(QStringLiteral(
        "INSERT INTO library (title, artist, album, genre, year, replaygain, replaygain_peak, filetype, location, header_parsed, "
        "mixxx_deleted, played, timesplayed, rating, bpm, duration, datetime_added) "
        "VALUES (:title, :artist, :album, :genre, :year, :replaygain, :replaygainPeak, :filetype, :location, :parsed, 0, 0, 0, 0, 0, :duration, CURRENT_TIMESTAMP)"));
    {
            // GlideMix: the file's tags (Artist, Title...), the file name only
            // when it has none -- the stream and the library showed file names.
            const MixHouseFileTags tags = readFileTags(path);
            insLib.bindValue(QStringLiteral(":title"), tags.title);
            insLib.bindValue(QStringLiteral(":artist"), tags.artist);
            insLib.bindValue(QStringLiteral(":album"), tags.album);
            insLib.bindValue(QStringLiteral(":genre"), tags.genre);
            insLib.bindValue(QStringLiteral(":year"), tags.year);
            insLib.bindValue(QStringLiteral(":replaygain"), tags.replayGain);
            insLib.bindValue(QStringLiteral(":replaygainPeak"), tags.replayGainPeak);
            insLib.bindValue(QStringLiteral(":parsed"), tags.fromTags ? 1 : 0);
        }
    insLib.bindValue(QStringLiteral(":filetype"), fi.suffix().toLower());
    insLib.bindValue(QStringLiteral(":location"), locId);
    const AudioInfo info = probeAudioFile(path);
    insLib.bindValue(QStringLiteral(":duration"),
                     info.valid && info.durationSec > 0.0 ? QVariant(info.durationSec) : QVariant());
    if (!insLib.exec()) {
        LOGW() << "ensureTrackForFile: library insert failed for " << path << ": " << insLib.lastError().text();
        return false;
    }
    outLibraryId = insLib.lastInsertId().toInt();
    outWasNew = true;
    return true;
}

int MixHouseLibrary::findExistingTrackId(const QString& path) const
{
    QSqlQuery find(m_writeDb);
    find.prepare(QStringLiteral(
        "SELECT l.id FROM library l JOIN track_locations t ON t.id = l.location WHERE t.location = :loc"));
    find.bindValue(QStringLiteral(":loc"), path);
    if (!find.exec() || !find.next()) {
        return -1;
    }
    return find.value(0).toInt();
}

int MixHouseLibrary::findExistingTrackByFilenameAndSize(const QString& filename, qint64 size) const
{
    QSqlQuery find(m_writeDb);
    find.prepare(QStringLiteral(
        "SELECT l.id FROM library l JOIN track_locations t ON t.id = l.location "
        "WHERE t.filename = :fn AND t.filesize = :size"));
    find.bindValue(QStringLiteral(":fn"), filename);
    find.bindValue(QStringLiteral(":size"), size);
    if (!find.exec() || !find.next()) {
        return -1;
    }
    return find.value(0).toInt();
}

bool MixHouseLibrary::ensureTrackForRekordboxEntry(const QString& path, qint64 size, int& outLibraryId, bool& outWasNew)
{
    int existing = findExistingTrackId(path);
    if (existing < 0 && size > 0) {
        existing = findExistingTrackByFilenameAndSize(QFileInfo(path).fileName(), size);
    }
    if (existing >= 0) {
        outLibraryId = existing;
        outWasNew = false;
        return true;
    }
    return ensureTrackForFile(path, outLibraryId, outWasNew);
}

au::mixhouse::IMixHouseLibrary::ScanResult MixHouseLibrary::addTracksFromFiles(const QStringList& filePaths)
{
    // The File-menu's "Add File(s) to Library" -- same INSERT-only rule as
    // addTracksFromFolder, for an explicit file list instead of a directory
    // walk.
    ScanResult res;
    if (!ensureWritable()) {
        res.error = QStringLiteral("Could not open the library for writing.");
        return res;
    }
    m_writeDb.transaction();
    for (const QString& path : filePaths) {
        const QFileInfo fi(path);
        if (!fi.isFile() || !kAudioExtensions.contains(fi.suffix().toLower())) {
            continue;
        }
        ++res.scanned;
        int libId = -1;
        bool wasNew = false;
        if (findExistingTrackId(path) < 0 && !probeAudioFile(path).valid) {
            ++res.unreadable;
            continue;
        }
        if (!ensureTrackForFile(path, libId, wasNew)) {
            continue;
        }
        if (wasNew) {
            ++res.added;
        } else {
            ++res.skipped;
        }
    }
    if (!m_writeDb.commit()) {
        m_writeDb.rollback();
        res.error = QStringLiteral("Could not commit: %1").arg(m_writeDb.lastError().text());
        return res;
    }
    LOGI() << "GlideMix addTracksFromFiles: " << res.scanned << " files, "
           << res.added << " added, " << res.skipped << " already known";
    // See addTracksFromFolder's own comment above -- same disconnected-from-
    // any-visible-model write path, same need to say so out loud.
    if (res.added > 0) {
        m_libraryChanged.notify();
    }
    return res;
}

// ---------------------------------------------------------------------------
// Background scan -- see IMixHouseLibrary::startBackgroundScan's own
// interface comment for the real freeze this exists to fix and what it
// deliberately does and doesn't port from real mixxx's own LibraryScanner.

void MixHouseLibrary::startBackgroundScan(const QStringList& directories, bool watch)
{
    if (m_scanning || directories.isEmpty()) {
        return;
    }
    // The safety backup must exist before ANY write happens on ANY
    // connection, including the scanner thread's own -- calling
    // ensureWritable() here, on the calling (normally main) thread, first
    // guarantees backupOnce() has already run before that thread is even
    // started, without needing to duplicate backup logic there too.
    if (!ensureWritable()) {
        ScanResult result;
        result.error = QStringLiteral("Could not open the library for writing.");
        result.watch = watch;
        m_scanFinished.send(result);
        return;
    }

    m_scanning = true;
    // Each background scan needs a connection name Qt has never seen before
    // -- QSqlDatabase::addDatabase() on an existing name replaces that
    // connection out from under anyone still holding it, and a static
    // instance counter (this whole module runs single-process, so this is
    // never contended by more than one thread at a time -- startBackgroundScan
    // itself already refuses to start a second scan while m_scanning is true)
    // is the simplest way to guarantee that.
    static int scannerInstanceCounter = 0;
    const QString connectionName = QStringLiteral("mixhouse_scanner_%1").arg(++scannerInstanceCounter);

    m_scanThread = std::make_unique<QThread>();
    // A file written in the last few seconds may still be arriving.
    auto* worker = new MixHouseLibraryScanner(m_path, directories, connectionName, watch ? 5 : 0);
    m_scanWorker = worker;
    worker->moveToThread(m_scanThread.get());

    // Plain Qt signal/slot connections, deliberately -- see
    // MixHouseLibraryScanner's own header comment for why muse::async::
    // Channel was rejected for this exact cross-thread hop. m_scanThread
    // itself is the connection's context/receiver-thread anchor: a QThread
    // object's OWN thread affinity is whichever thread CREATED it (here,
    // this method's caller, normally the main thread) -- NOT the thread it
    // manages -- so Qt's default AutoConnection correctly resolves to a
    // real queued connection here (worker's signal thread vs. m_scanThread's
    // own thread differ), the same automatic-and-safe delivery real mixxx's
    // own LibraryScanner relies on for this identical job.
    QObject::connect(worker, &MixHouseLibraryScanner::scanProgress, m_scanThread.get(),
                      [this](int scanned, int added) {
        m_scanProgress.send(scanned, added);
    });
    QObject::connect(worker, &MixHouseLibraryScanner::scanFinished, m_scanThread.get(),
                      [this, worker, watch](int scanned, int added, int skipped, int unreadable, int pending,
                                            const QList<int>& addedIds, const QString& error) {
        ScanResult result;
        result.scanned = scanned;
        result.added = added;
        result.skipped = skipped;
        result.unreadable = unreadable;
        result.pending = pending;
        result.addedIds = addedIds;
        result.watch = watch;
        result.error = error;
        m_scanning = false;
        m_scanWorker = nullptr;
        m_scanThread->quit();
        m_scanThread->wait();
        worker->deleteLater();
        LOGI() << "GlideMix background scan: " << result.scanned << " files, "
               << result.added << " added, " << result.skipped << " already known, " << result.pending
               << " still being written";
        if (result.added > 0) {
            m_libraryChanged.notify();
        }
        m_scanFinished.send(result);
    });
    QObject::connect(m_scanThread.get(), &QThread::started, worker, &MixHouseLibraryScanner::run);
    m_scanThread->start();
}

bool MixHouseLibrary::isScanning() const { return m_scanning; }
muse::async::Channel<int, int> MixHouseLibrary::scanProgress() const { return m_scanProgress; }
muse::async::Channel<IMixHouseLibrary::ScanResult> MixHouseLibrary::scanFinished() const { return m_scanFinished; }

// ---------------------------------------------------------------------------
// Background analysis (see IMixHouseLibrary::startBackgroundAnalysis's own
// comment for the real mixxx precedent this ports and why).

namespace {
int mixhouseAnalyzerThreadCount(int mode)
{
    // Real mixxx: library/analysis/analysisfeature.cpp's own
    // kNumberOfAnalyzerThreads -- "Utilize all available cores for batch
    // analysis of tracks", math_max(1, QThread::idealThreadCount()), no
    // cores reserved. Read directly from real mixxx source before writing
    // this, not guessed -- matches the explicit call on this: "if
    // mixxx says is ok 1/cpu core than do as mixxx says".
    // GlideMix: on small machines (4 threads or fewer -- a 2-core laptop)
    // keep half for audio and the interface; four analysis threads there made
    // playback crackle and every track slower.
    const int threads = QThread::idealThreadCount();
    // GlideMix: Power saving (rekordbox's Concurrent Process Mode) keeps
    // half the cores free; Normal and Performance use them all.
    if (mode == 2) {
        return std::max(1, threads / 2);
    }
    return threads <= 4 && mode != 0 ? std::max(1, threads / 2) : threads;
}
}

void MixHouseLibrary::startBackgroundAnalysis(const QList<int>& trackIds)
{
    if (m_analyzing || trackIds.isEmpty()) {
        return;   // already running -- same no-op-on-double-start rule as startBackgroundScan()
    }
    if (!ensureWritable()) {
        return;   // matches startBackgroundScan's own silent-fail rule when the library can't be opened for writing at all
    }

    m_analyzeQueue = trackIds;
    m_analyzeTotal = trackIds.size();
    m_analyzeSucceeded = 0;
    m_analyzeFailed = 0;
    m_analyzeStopRequested = false;
    m_analyzing = true;

    // Capped at m_analyzeTotal: no point starting more workers than there
    // are tracks queued -- each extra idle worker would just immediately
    // find nothing to do and tear itself right back down again.
    const int mode = libraryConfiguration() ? libraryConfiguration()->analysisMode() : 1;
    const int numThreads = std::min(mixhouseAnalyzerThreadCount(mode), m_analyzeTotal);

    m_analyzeWorkers.reserve(numThreads);
    for (int i = 0; i < numThreads; ++i) {
        // No connection name needed -- unlike MixHouseLibraryScanner's own
        // per-instance QSqlDatabase, a worker here touches no SQL at all
        // (see this class's own header comment for why).
        auto* worker = new MixHouseAnalyzerWorker();
        auto* thread = new QThread();
        worker->moveToThread(thread);

        QObject::connect(thread, &QThread::finished, worker, &QObject::deleteLater);
        QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);

        // thread (not worker) is the connection's context/receiver-thread
        // anchor -- see startBackgroundScan's own comment on exactly why
        // this makes Qt's default AutoConnection resolve to a real queued
        // connection here (worker's signal thread vs. thread's own thread,
        // which is whichever thread CREATED it -- this one, the main
        // thread -- differ), guaranteeing onAnalyzerWorkerFinished always
        // actually runs on the main thread even though it's triggered by a
        // signal emitted from a worker thread. This is also the ONLY place
        // that ever touches m_writeDb for a background-pool track (see
        // onAnalyzerWorkerFinished's own comment) -- the worker itself
        // never does, by design.
        QObject::connect(worker, &MixHouseAnalyzerWorker::analyzed, thread,
                          [this, thread](int trackId, bool bpmLocked, const MixHouseAnalysisResult& analysis) {
            onAnalyzerWorkerFinished(thread, trackId, bpmLocked, analysis);
        });
        // Same thread-anchored queued hop as analyzed() above, so .send()
        // always runs on the main thread.
        QObject::connect(worker, &MixHouseAnalyzerWorker::progress, thread, [this](int trackId, int percent) {
            if (m_analyzing) {
                m_trackAnalysisProgress.send(trackId, percent);
            }
        });

        // GlideMix: below the interface, so the app stays responsive while a
        // library is analysed (each thread now also runs Beat This!); macOS
        // gives these threads every core the interface and audio don't need.
        // Performance: normal priority, as fast as it goes. Normal: below the
        // interface and audio. Power saving: only when nothing else wants it.
#ifdef Q_OS_WIN
        // Windows' LowPriority still competes with audio; idle doesn't.
        thread->start(mode == 0 ? QThread::NormalPriority : QThread::IdlePriority);
#else
        thread->start(mode == 0 ? QThread::NormalPriority : (mode == 2 ? QThread::IdlePriority : QThread::LowPriority));
#endif
        m_analyzeWorkers.push_back(AnalyzerWorkerHandle{ thread, worker, false });
    }
    for (auto& handle : m_analyzeWorkers) {
        tryDispatchNextTrack(handle);
    }
    // Single check AFTER the loop above, not inside it: tryDispatchNextTrack
    // can decide a handle is idle before every handle has even been visited
    // (e.g. several bad track ids in a row draining the queue early), and
    // finishBatchIfAllIdle() may clear m_analyzeWorkers -- doing that from
    // inside a loop still iterating over m_analyzeWorkers would be exactly
    // the kind of container-mutated-while-iterating bug this comment exists
    // to avoid introducing.
    finishBatchIfAllIdle();
}

void MixHouseLibrary::tryDispatchNextTrack(AnalyzerWorkerHandle& handle)
{
    while (!m_analyzeStopRequested && !m_analyzeQueue.isEmpty()) {
        const int trackId = m_analyzeQueue.takeFirst();
        // Looked up HERE, on the main thread, using m_writeDb -- not by the
        // worker. See mixhouseanalysiswriter.h's own comment for why: a
        // worker touching SQL at all (even a read) reintroduces the exact
        // multi-connection contention this rearchitecture exists to remove.
        const MixHouseTrackFileInfo lookup = lookupTrackFileForAnalysis(m_writeDb, trackId);
        if (!lookup.valid) {
            // Never reaches a worker at all -- tally right here, same as
            // any other per-track failure, and try the next queued track
            // for this same handle instead of leaving it idle over one bad
            // row.
            LOGW() << "startBackgroundAnalysis: failed for track " << trackId << ": " << lookup.error;
            ++m_analyzeFailed;
            m_trackAnalysisProgress.send(trackId, -2);
            m_analysisProgress.send(m_analyzeSucceeded + m_analyzeFailed, m_analyzeTotal);
            continue;
        }
        // GlideMix: rekordbox or Lexicon analysed this file already -- take
        // its grid (and Lexicon's key) instead of running the beat network.
        double knownBpm = 0.0, knownFirstBeat = 0.0, knownDownbeat = -1.0;
        int knownKey = 0;
        QString knownSource;
        if (rekordboxPhrases()) {
            const MixHouseRekordboxGrid grid = rekordboxPhrases()->beatGrid(lookup.filePath, 0.0);
            if (grid.valid && grid.bpm > 0.0) {
                knownBpm = grid.bpm;
                knownFirstBeat = grid.firstBeatSec;
                knownDownbeat = grid.firstDownbeatSec;
                knownSource = QStringLiteral("rekordbox");
            }
        }
        if (lexicon() && lexicon()->available()) {
            IMixHouseLibrary::TrackQuery query;
            query.location = lookup.filePath;
            const MixHouseTrackList found = lexicon()->tracks(-1, query);
            if (!found.empty()) {
                const MixHouseTrack& t = found.front();
                knownKey = t.keyId;
                if (knownBpm <= 0.0 && t.bpm > 0.0) {
                    knownBpm = t.bpm;
                    knownFirstBeat = t.beats.firstBeatSec;
                    knownDownbeat = t.downbeatSec;
                    knownSource = QStringLiteral("Lexicon");
                }
            }
        }
        handle.busy = true;
        QMetaObject::invokeMethod(handle.worker, "analyzeTrack", Qt::QueuedConnection,
                                   Q_ARG(int, trackId), Q_ARG(QString, lookup.filePath), Q_ARG(bool, lookup.bpmLocked),
                                   Q_ARG(double, knownBpm), Q_ARG(double, knownFirstBeat), Q_ARG(double, knownDownbeat),
                                   Q_ARG(int, knownKey), Q_ARG(QString, knownSource));
        return;
    }
    // Nothing left for this worker (queue drained, or stop requested) --
    // ask its thread to wind down exactly once, right at this idle
    // transition. quit() just requests the exit; the finished() connections
    // made at creation (the two deleteLater()s) handle the rest once this
    // worker's own event loop actually returns.
    handle.busy = false;
    handle.thread->quit();
}

void MixHouseLibrary::finishBatchIfAllIdle()
{
    const bool anyBusy = std::any_of(m_analyzeWorkers.begin(), m_analyzeWorkers.end(),
                                      [](const AnalyzerWorkerHandle& h) { return h.busy; });
    if (anyBusy) {
        return;
    }
    const IMixHouseLibrary::AnalysisSummary summary{ m_analyzeSucceeded, m_analyzeFailed, m_analyzeTotal,
                                                     m_analyzeStopRequested };
    m_analyzeWorkers.clear();   // handles now dangle by design -- both thread and worker self-delete via the deleteLater()s connected above
    m_analyzing = false;
    m_analyzeStopRequested = false;
    LOGI() << "GlideMix background analysis: " << summary.succeeded << " succeeded, "
           << summary.failed << " failed, " << summary.total << " total";
    m_analysisFinished.send(summary);
}

void MixHouseLibrary::onAnalyzerWorkerFinished(QThread* thread, int trackId, bool bpmLocked,
                                               const MixHouseAnalysisResult& analysis)
{
    // Guaranteed to run on the main thread -- see startBackgroundAnalysis's
    // own comment on the connect() that invokes this. This is the ONLY
    // place a background-pool track's result is ever written -- through
    // m_writeDb, this object's own single write connection, exactly like
    // every other write in this class (see mixhouseanalysiswriter.h's own
    // comment for the real, measured contention bug this replaced).
    bool success = false;
    if (analysis.valid) {
        const MixHouseAnalyzeResult result = writeAnalysisResult(m_path, m_writeDb, trackId, bpmLocked, analysis);
        success = result.valid;
        if (!success) {
            LOGW() << "startBackgroundAnalysis: failed for track " << trackId << ": " << result.error;
        }
    } else {
        LOGW() << "startBackgroundAnalysis: failed for track " << trackId << ": " << analysis.error;
    }
    if (success) {
        ++m_analyzeSucceeded;
    } else {
        ++m_analyzeFailed;
    }
    // Its phrases were just written: an answer cached before the analysis
    // (none yet) must not outlive it -- the library's waveform asks for the
    // song's cue points the moment this arrives.
    m_detectedPhrases.remove(trackId);
    m_trackAnalysisProgress.send(trackId, success ? -1 : -2);
    m_analysisProgress.send(m_analyzeSucceeded + m_analyzeFailed, m_analyzeTotal);

    const auto it = std::find_if(m_analyzeWorkers.begin(), m_analyzeWorkers.end(),
                                  [thread](const AnalyzerWorkerHandle& h) { return h.thread == thread; });
    if (it == m_analyzeWorkers.end()) {
        return;   // shouldn't happen -- every handle's thread is only ever removed as part of tearing the whole pool down together
    }
    tryDispatchNextTrack(*it);
    finishBatchIfAllIdle();
}

void MixHouseLibrary::stopBackgroundAnalysis()
{
    if (!m_analyzing) {
        return;
    }
    m_analyzeStopRequested = true;
    m_analyzeQueue.clear();
    // Every worker currently busy will hit tryDispatchNextTrack's own
    // "stop requested" branch the moment its CURRENT track's
    // analyzeTrackFile() call returns -- there is no earlier mid-decode
    // cancellation point (a single atomic call), the same disclosed
    // limitation the old single-timer queue always had. onAnalyzerWorkerFinished
    // is what actually tears the pool down and publishes analysisFinished()
    // once every worker has hit that branch; nothing more to do here.
}

bool MixHouseLibrary::isAnalyzing() const { return m_analyzing; }
muse::async::Channel<int, int> MixHouseLibrary::analysisProgress() const { return m_analysisProgress; }
muse::async::Channel<IMixHouseLibrary::AnalysisSummary> MixHouseLibrary::analysisFinished() const { return m_analysisFinished; }
muse::async::Channel<int, int> MixHouseLibrary::trackAnalysisProgress() const { return m_trackAnalysisProgress; }

// ---------------------------------------------------------------------------
// External library XML import (Rekordbox / iTunes). Real mixxx keeps these
// in a separate, read-only external tree (library/itunes/itunesfeature.cpp)
// and only copies a track into the real library the moment the user touches
// it. This app has no such browsing UI yet, so both importers instead copy
// up front: every track an imported playlist references is resolved via
// ensureTrackForFile() above (matched by file path, inserted only if
// genuinely new), and each playlist becomes a real row in `Playlists`,
// named with a "Rekordbox: "/"iTunes: " prefix so its external origin stays
// visible. Real mixxx has NO Rekordbox XML importer at all -- only a binary
// USB-device *.PDB parser (a completely different format) -- so the
// Rekordbox side below is implemented directly against Pioneer's publicly
// documented rekordbox XML collection-export schema, not ported mixxx code;
// the iTunes side ports the real field mapping from mixxx's own
// library/itunes/itunesxmlimporter.cpp.

namespace {
struct ItunesTrackInfo {
    QString location;   // resolved local path; empty if unresolvable or "Remote" (iTunes Match)
    bool remote = false;
};

/// One <key>NAME</key><VALUE .../> pair's value, wherever it appears in the
/// plist. `tag` is the value element's own name ("string"/"integer"/"true"/
/// "false"/...), which is how a boolean is told apart from other scalars.
struct PlistScalar { QString tag; QString text; };

PlistScalar readPlistScalar(QXmlStreamReader& xml)
{
    if (!xml.readNextStartElement()) {
        return {};
    }
    PlistScalar v;
    v.tag = xml.name().toString();
    v.text = xml.readElementText(QXmlStreamReader::SkipChildElements);
    return v;
}

/// Real iTunes plist "Location" is a percent-encoded file:// URL, e.g.
/// "file://localhost/Users/chris/Music/Track.mp3" -- itunesxmlimporter.cpp's
/// own real path resolution (dbITunesRoot/mixxxITunesRoot remapping) is
/// mixxx-specific to ITS OWN settings layout; this just decodes the URL to a
/// local path, matching the common case of the same machine.
QString itunesLocalPath(const QString& location)
{
    const QUrl url(location);
    return url.isLocalFile() ? url.toLocalFile() : QUrl::fromPercentEncoding(location.toUtf8());
}

/// Real field names read by mixxx's own ITunesXMLImporter::parseTrack()
/// (library/itunes/itunesxmlimporter.cpp) -- only Location and Track Type
/// matter here since this materializes tracks up front rather than lazily.
void parseItunesTrack(QXmlStreamReader& xml, ItunesTrackInfo& info)
{
    while (xml.readNextStartElement()) {
        if (xml.name() != QLatin1String("key")) {
            xml.skipCurrentElement();
            continue;
        }
        const QString key = xml.readElementText();
        const PlistScalar v = readPlistScalar(xml);
        if (key == QLatin1String("Location")) {
            info.location = itunesLocalPath(v.text);
        } else if (key == QLatin1String("Track Type")) {
            // Real mixxx: an iTunes Match cloud-only track, discarded outright.
            info.remote = (v.text == QLatin1String("Remote"));
        }
    }
}

void parseItunesTracksDict(QXmlStreamReader& xml, QHash<int, ItunesTrackInfo>& tracksById)
{
    while (xml.readNextStartElement()) {
        if (xml.name() != QLatin1String("key")) {
            xml.skipCurrentElement();
            continue;
        }
        bool ok = false;
        const int trackId = xml.readElementText().toInt(&ok);
        if (!xml.readNextStartElement()) {
            continue;
        }
        if (xml.name() != QLatin1String("dict")) {
            xml.skipCurrentElement();
            continue;
        }
        ItunesTrackInfo info;
        parseItunesTrack(xml, info);
        if (ok && !info.remote && !info.location.isEmpty()) {
            tracksById.insert(trackId, info);
        }
    }
}

struct ItunesPlaylistInfo {
    QString name;
    bool isSystem = false;   // Distinguished Kind present, or Visible=false (real mixxx's own rule)
    QList<int> trackIds;
};

void parseItunesPlaylist(QXmlStreamReader& xml, ItunesPlaylistInfo& pl)
{
    while (xml.readNextStartElement()) {
        if (xml.name() != QLatin1String("key")) {
            xml.skipCurrentElement();
            continue;
        }
        const QString key = xml.readElementText();
        if (key == QLatin1String("Playlist Items")) {
            if (xml.readNextStartElement()) {   // the per-track <array>
                while (xml.readNextStartElement()) {
                    if (xml.name() != QLatin1String("dict")) {
                        xml.skipCurrentElement();
                        continue;
                    }
                    int trackId = -1;
                    while (xml.readNextStartElement()) {
                        if (xml.name() != QLatin1String("key")) {
                            xml.skipCurrentElement();
                            continue;
                        }
                        const QString k2 = xml.readElementText();
                        const PlistScalar v2 = readPlistScalar(xml);
                        if (k2 == QLatin1String("Track ID")) {
                            trackId = v2.text.toInt();
                        }
                    }
                    if (trackId >= 0) {
                        pl.trackIds.push_back(trackId);
                    }
                }
            }
            continue;
        }
        const PlistScalar v = readPlistScalar(xml);
        if (key == QLatin1String("Name")) {
            pl.name = v.text;
        } else if (key == QLatin1String("Distinguished Kind")) {
            pl.isSystem = true;
        } else if (key == QLatin1String("Visible") && v.tag == QLatin1String("false")) {
            pl.isSystem = true;
        }
    }
}

void parseItunesPlaylistsArray(QXmlStreamReader& xml, QList<ItunesPlaylistInfo>& out)
{
    while (xml.readNextStartElement()) {
        if (xml.name() != QLatin1String("dict")) {
            xml.skipCurrentElement();
            continue;
        }
        ItunesPlaylistInfo pl;
        parseItunesPlaylist(xml, pl);
        out.push_back(pl);
    }
}
} // namespace

MixHouseImportResult MixHouseLibrary::importFromItunesXml(const QString& xmlPath)
{
    MixHouseImportResult res;
    QFile file(xmlPath);
    if (!file.open(QIODevice::ReadOnly)) {
        res.error = QStringLiteral("Could not open %1").arg(xmlPath);
        return res;
    }
    if (!ensureWritable()) {
        res.error = QStringLiteral("Could not open the library for writing.");
        return res;
    }

    QXmlStreamReader xml(&file);
    QHash<int, ItunesTrackInfo> tracksById;
    QList<ItunesPlaylistInfo> playlists;

    if (!xml.readNextStartElement() || xml.name() != QLatin1String("plist")) {
        res.error = QStringLiteral("Not a plist file.");
        return res;
    }
    if (!xml.readNextStartElement() || xml.name() != QLatin1String("dict")) {
        res.error = QStringLiteral("Malformed iTunes XML (no root dict).");
        return res;
    }
    while (xml.readNextStartElement()) {
        if (xml.name() != QLatin1String("key")) {
            xml.skipCurrentElement();
            continue;
        }
        const QString key = xml.readElementText();
        if (key == QLatin1String("Tracks")) {
            if (xml.readNextStartElement()) {
                parseItunesTracksDict(xml, tracksById);
            }
        } else if (key == QLatin1String("Playlists")) {
            if (xml.readNextStartElement()) {
                parseItunesPlaylistsArray(xml, playlists);
            }
        } else if (xml.readNextStartElement()) {
            xml.skipCurrentElement();
        }
    }
    if (xml.hasError()) {
        res.error = QStringLiteral("XML parse error: %1").arg(xml.errorString());
        return res;
    }

    m_writeDb.transaction();
    for (const ItunesPlaylistInfo& pl : playlists) {
        if (pl.isSystem || pl.trackIds.isEmpty()) {
            continue;
        }
        const int playlistId = createPlaylist(QStringLiteral("iTunes: %1").arg(pl.name), -1);
        if (playlistId < 0) {
            continue;
        }
        ++res.playlistsCreated;
        for (int tid : pl.trackIds) {
            const auto it = tracksById.constFind(tid);
            if (it == tracksById.constEnd()) {
                continue;
            }
            ++res.tracksFound;
            int libId = -1;
            bool wasNew = false;
            if (!ensureTrackForFile(it->location, libId, wasNew)) {
                continue;
            }
            if (wasNew) {
                ++res.tracksAdded;
            } else {
                ++res.tracksMatched;
            }
            addTrackToPlaylist(libId, playlistId);
        }
    }
    if (!m_writeDb.commit()) {
        m_writeDb.rollback();
        res.error = QStringLiteral("Could not commit: %1").arg(m_writeDb.lastError().text());
        return res;
    }

    LOGI() << "GlideMix iTunes import from " << xmlPath << ": " << res.playlistsCreated
           << " playlists, " << res.tracksFound << " track refs, " << res.tracksAdded << " new";
    // See addTracksFromFolder's own comment above -- same disconnected-from-
    // any-visible-model write path.
    if (res.tracksAdded > 0 || res.playlistsCreated > 0) {
        m_libraryChanged.notify();
    }
    return res;
}

namespace {
struct RekordboxTrackInfo {
    QString location;
    /// From the track's own first real <TEMPO> marker -- 0.0/-1.0 if the
    /// track has none (untagged/unanalyzed in rekordbox).
    double bpm = 0.0;
    double firstBeatSec = -1.0;
    /// Real rekordbox XML's own Tonality attribute -- already Camelot text
    /// (e.g. "4A"), the same notation this app's own kKeyIdToCamelotText
    /// produces. Empty if rekordbox has no key for this track.
    QString tonality;
    /// The TRACK element's own real SampleRate attribute -- encodeBeatGrid()
    /// needs it, and reading it straight from the XML avoids re-decoding
    /// the actual audio file just to ask it its own sample rate again.
    /// 44100 (the overwhelming real-world default) if genuinely missing.
    int sampleRate = 44100;
    /// The TRACK element's own real Size attribute (bytes) -- used for the
    /// filename+size fallback match when the exact path doesn't line up
    /// (see findExistingTrackByFilenameAndSize's own comment). -1 if
    /// genuinely missing/unparseable.
    qint64 size = -1;
};

/// Real rekordbox XML "Location" is "file://localhost/..." (percent-encoded)
/// -- but real files confirm rekordbox does NOT reliably percent-encode a
/// literal "#" the way it does space/&/[ (measured against real
/// collection: a track literally named "#Selfie NRG (Dyro Mashup)..." comes
/// through with a bare "#" in its Location). QUrl's own parser treats an
/// unescaped "#" as the URL FRAGMENT delimiter, so trying QUrl::isLocalFile()/
/// toLocalFile() first (an earlier version of this function did) silently
/// truncates the path at "#", dropping the filename entirely -- a real bug
/// this cost: the truncated path matched no existing track, so
/// importFromRekordboxXml() created a brand-new stub row instead of finding
/// the real one. Plain prefix-stripping + percent-decoding has no fragment
/// concept at all and handles this correctly, so it's tried first now; QUrl
/// is only the fallback for a Location that isn't the expected "file://"
/// form to begin with.
QString rekordboxLocalPath(const QString& location)
{
    QString s = location;
    if (s.startsWith(QLatin1String("file://localhost"))) {
        return QUrl::fromPercentEncoding(s.mid(QStringLiteral("file://localhost").size()).toUtf8());
    }
    if (s.startsWith(QLatin1String("file://"))) {
        return QUrl::fromPercentEncoding(s.mid(QStringLiteral("file://").size()).toUtf8());
    }
    const QUrl url(location);
    return url.isLocalFile() ? url.toLocalFile() : location;
}

/// From Pioneer's publicly documented rekordbox XML collection-export
/// schema (File > Export Collection in xml format) -- NOT ported from
/// mixxx, which has no XML importer at all (see the comment above
/// importFromRekordboxXml). Most <TRACK> data is attributes, but its real
/// beatgrid lives in nested <TEMPO> children (one per detected tempo
/// region: Inizio="<anchor seconds>" Bpm=".." Metro="4/4" Battito="1..4"),
/// which is why this now actually descends into TRACK instead of skipping
/// its whole subtree in one call.
void parseRekordboxCollection(QXmlStreamReader& xml, QHash<int, RekordboxTrackInfo>& tracksById)
{
    while (xml.readNextStartElement()) {
        if (xml.name() != QLatin1String("TRACK")) {
            xml.skipCurrentElement();
            continue;
        }
        const QXmlStreamAttributes a = xml.attributes();
        bool ok = false;
        const int trackId = a.value(QLatin1String("TrackID")).toString().toInt(&ok);

        RekordboxTrackInfo info;
        info.location = rekordboxLocalPath(a.value(QLatin1String("Location")).toString());
        info.tonality = a.value(QLatin1String("Tonality")).toString();
        bool srOk = false;
        const int sr = a.value(QLatin1String("SampleRate")).toString().toInt(&srOk);
        if (srOk && sr > 0) {
            info.sampleRate = sr;
        }
        bool sizeOk = false;
        const qint64 sz = a.value(QLatin1String("Size")).toString().toLongLong(&sizeOk);
        if (sizeOk && sz > 0) {
            info.size = sz;
        }

        // A track can carry more than one <TEMPO> marker (a real, genuine
        // tempo change partway through, e.g. a mashup that speeds up) --
        // this app's own beatgrid storage (encodeBeatGrid, "BeatGrid-2.0",
        // same as mixhouseanalyzer.h's own analyzer) is a single constant-
        // tempo grid, so only the FIRST marker is kept: its Inizio is the
        // anchor beat position, its Bpm the constant tempo used throughout.
        // Not a regression this import step introduces -- this app's own
        // analyzer has the identical single-tempo limitation already.
        bool tempoTaken = false;
        while (xml.readNextStartElement()) {
            if (xml.name() == QLatin1String("TEMPO") && !tempoTaken) {
                const QXmlStreamAttributes ta = xml.attributes();
                bool bpmOk = false, posOk = false;
                const double bpm = ta.value(QLatin1String("Bpm")).toString().toDouble(&bpmOk);
                const double inizio = ta.value(QLatin1String("Inizio")).toString().toDouble(&posOk);
                if (bpmOk && bpm > 0.0 && posOk) {
                    info.bpm = bpm;
                    info.firstBeatSec = inizio;
                    tempoTaken = true;
                }
            }
            xml.skipCurrentElement();   // this child only (TEMPO once taken, or POSITION_MARK -- unused here)
        }

        if (ok && !info.location.isEmpty()) {
            tracksById.insert(trackId, info);
        }
    }
}

/// Reverse of kKeyIdToCamelotText -- real rekordbox XML's own Tonality
/// attribute is already Camelot text (e.g. "4A"), so importing it needs the
/// inverse lookup. Case-insensitive defensively; real rekordbox always
/// exports it uppercase like this app's own table already is.
int camelotTextToKeyId(const QString& text)
{
    if (text.isEmpty()) {
        return 0;
    }
    for (int id = 1; id <= 24; ++id) {
        if (text.compare(QLatin1String(kKeyIdToCamelotText[id]), Qt::CaseInsensitive) == 0) {
            return id;
        }
    }
    return 0;
}

struct RekordboxPlaylistNode {
    QString name;
    QList<int> trackIds;
};

/// <NODE Type="0"> is a folder (recurse); Type="1" is a real playlist (leaf,
/// its children are <TRACK Key="TrackID"/> references). Folders are
/// flattened -- our Playlists table has no folder concept, same
/// simplification as the iTunes side -- so each leaf keeps only its own
/// name, not the full folder path.
void parseRekordboxNode(QXmlStreamReader& xml, QList<RekordboxPlaylistNode>& out)
{
    const QXmlStreamAttributes a = xml.attributes();
    const QString type = a.value(QLatin1String("Type")).toString();
    const QString name = a.value(QLatin1String("Name")).toString();

    if (type == QLatin1String("1")) {
        RekordboxPlaylistNode node;
        node.name = name;
        while (xml.readNextStartElement()) {
            if (xml.name() == QLatin1String("TRACK")) {
                bool ok = false;
                const int tid = xml.attributes().value(QLatin1String("Key")).toString().toInt(&ok);
                if (ok) {
                    node.trackIds.push_back(tid);
                }
            }
            xml.skipCurrentElement();
        }
        out.push_back(node);
    } else {
        while (xml.readNextStartElement()) {
            if (xml.name() == QLatin1String("NODE")) {
                parseRekordboxNode(xml, out);
            } else {
                xml.skipCurrentElement();
            }
        }
    }
}
} // namespace

MixHouseImportResult MixHouseLibrary::importFromRekordboxXml(const QString& xmlPath)
{
    MixHouseImportResult res;
    QFile file(xmlPath);
    if (!file.open(QIODevice::ReadOnly)) {
        res.error = QStringLiteral("Could not open %1").arg(xmlPath);
        return res;
    }
    if (!ensureWritable()) {
        res.error = QStringLiteral("Could not open the library for writing.");
        return res;
    }

    QXmlStreamReader xml(&file);
    QHash<int, RekordboxTrackInfo> tracksById;
    QList<RekordboxPlaylistNode> playlists;

    if (!xml.readNextStartElement() || xml.name() != QLatin1String("DJ_PLAYLISTS")) {
        res.error = QStringLiteral("Not a rekordbox collection XML file.");
        return res;
    }
    while (xml.readNextStartElement()) {
        if (xml.name() == QLatin1String("COLLECTION")) {
            parseRekordboxCollection(xml, tracksById);
        } else if (xml.name() == QLatin1String("PLAYLISTS")) {
            // The real schema wraps everything in one root <NODE Type="0"
            // Name="ROOT">; recurse from there so ROOT's own name never
            // itself becomes a playlist.
            while (xml.readNextStartElement()) {
                if (xml.name() == QLatin1String("NODE")) {
                    parseRekordboxNode(xml, playlists);
                } else {
                    xml.skipCurrentElement();
                }
            }
        } else {
            xml.skipCurrentElement();
        }
    }
    if (xml.hasError()) {
        res.error = QStringLiteral("XML parse error: %1").arg(xml.errorString());
        return res;
    }

    m_writeDb.transaction();
    for (const RekordboxPlaylistNode& pl : playlists) {
        if (pl.trackIds.isEmpty()) {
            continue;
        }
        const int playlistId = createPlaylist(QStringLiteral("Rekordbox: %1").arg(pl.name), -1);
        if (playlistId < 0) {
            continue;
        }
        ++res.playlistsCreated;
        for (int tid : pl.trackIds) {
            const auto it = tracksById.constFind(tid);
            if (it == tracksById.constEnd()) {
                continue;
            }
            ++res.tracksFound;
            // Same real freeze risk addTracksFromFolder()'s own comment
            // describes, for the same reason -- a real 70k-track rekordbox
            // collection means this inner loop alone can run tens of
            // thousands of times with nothing handed back to the event
            // loop, which is also why the ProgressDialog wrapping this call
            // (MixHouseTrackListModel::importFromRekordboxXml) couldn't
            // actually animate before this fix -- Qt has no chance to
            // repaint it mid-call.
            if ((res.tracksFound % 200) == 0) {
                QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
            }
            int libId = -1;
            bool wasNew = false;
            if (!ensureTrackForRekordboxEntry(it->location, it->size, libId, wasNew)) {
                continue;
            }
            if (wasNew) {
                ++res.tracksAdded;
            } else {
                ++res.tracksMatched;
            }
            addTrackToPlaylist(libId, playlistId);
        }
    }

    // Real rekordbox analysis is genuinely expensive to produce (a single
    // real collection scan can take rekordbox itself many hours), and this
    // app's own analyzer can only ever approximate it -- so once it exists
    // in the XML, use it. Walks EVERY <COLLECTION> track (not just ones
    // referenced by a playlist above), but only ever WRITES to a track this
    // library already has by real file path (findExistingTrackId never
    // creates one) -- a real rekordbox collection routinely references tens
    // of thousands of files this specific library doesn't have at all, and
    // this must never silently create stub rows for those.
    int enrichmentWalked = 0;
    for (auto it = tracksById.constBegin(); it != tracksById.constEnd(); ++it) {
        // Same real freeze-risk fix as the playlist loop above -- this walks
        // EVERY <COLLECTION> track (its own comment above already explains
        // why), which for a real 70k-track collection is again tens of
        // thousands of iterations with no yielding.
        if ((++enrichmentWalked % 200) == 0) {
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        }
        const RekordboxTrackInfo& info = it.value();
        if (info.bpm <= 0.0 && info.tonality.isEmpty()) {
            continue;   // rekordbox itself has nothing analyzed for this track
        }
        int trackId = findExistingTrackId(info.location);
        if (trackId < 0 && info.size > 0) {
            // Real-world case, confirmed against the actual files:
            // rekordbox's own collection path (e.g. an external "DJ USB"
            // volume) and this library's own scanned path (e.g. a local
            // working-copy folder) can genuinely differ for the exact same
            // track -- see findExistingTrackByFilenameAndSize's own comment.
            trackId = findExistingTrackByFilenameAndSize(QFileInfo(info.location).fileName(), info.size);
        }
        if (trackId < 0) {
            continue;   // not a track this library actually has
        }

        bool wroteAnything = false;

        if (info.bpm > 0.0 && info.firstBeatSec >= 0.0) {
            // Same bpm_lock rule as analyzeTrack() -- a locked track's own
            // bpm/beats are never overwritten by a new analysis, rekordbox's
            // or this app's own.
            QSqlQuery lockCheck(m_writeDb);
            lockCheck.prepare(QStringLiteral("SELECT bpm_lock FROM library WHERE id = :id"));
            lockCheck.bindValue(QStringLiteral(":id"), trackId);
            const bool locked = lockCheck.exec() && lockCheck.next() && lockCheck.value(0).toInt() != 0;
            if (!locked) {
                QSqlQuery updBpm(m_writeDb);
                updBpm.prepare(QStringLiteral(
                    "UPDATE library SET bpm = :bpm, beats = :beats, beats_version = :version WHERE id = :id"));
                updBpm.bindValue(QStringLiteral(":bpm"), info.bpm);
                updBpm.bindValue(QStringLiteral(":beats"),
                                  encodeBeatGrid(info.bpm, info.firstBeatSec, info.sampleRate));
                updBpm.bindValue(QStringLiteral(":version"), QStringLiteral("BeatGrid-2.0"));
                updBpm.bindValue(QStringLiteral(":id"), trackId);
                if (updBpm.exec()) {
                    wroteAnything = true;
                } else {
                    LOGW() << "importFromRekordboxXml: bpm/beats update failed for track " << trackId
                           << ": " << updBpm.lastError().text();
                }
            }
        }

        const int keyId = camelotTextToKeyId(info.tonality);
        if (keyId > 0) {
            QSqlQuery updKey(m_writeDb);
            updKey.prepare(QStringLiteral("UPDATE library SET key = :key, key_id = :key_id WHERE id = :id"));
            updKey.bindValue(QStringLiteral(":key"), QString::fromLatin1(kKeyIdToCamelotText[keyId]));
            updKey.bindValue(QStringLiteral(":key_id"), keyId);
            updKey.bindValue(QStringLiteral(":id"), trackId);
            if (updKey.exec()) {
                wroteAnything = true;
            } else {
                LOGW() << "importFromRekordboxXml: key update failed for track " << trackId
                       << ": " << updKey.lastError().text();
            }
        }

        if (wroteAnything) {
            ++res.analysisImported;
        }
    }

    if (!m_writeDb.commit()) {
        m_writeDb.rollback();
        res.error = QStringLiteral("Could not commit: %1").arg(m_writeDb.lastError().text());
        return res;
    }

    LOGI() << "GlideMix Rekordbox import from " << xmlPath << ": " << res.playlistsCreated
           << " playlists, " << res.tracksFound << " track refs, " << res.tracksAdded << " new, "
           << res.analysisImported << " got real bpm/beatgrid/key from rekordbox's own analysis";
    // See addTracksFromFolder's own comment above -- same disconnected-from-
    // any-visible-model write path.
    if (res.tracksAdded > 0 || res.playlistsCreated > 0 || res.analysisImported > 0) {
        m_libraryChanged.notify();
    }
    return res;
}

// ---------------------------------------------------------------------------
// The real WTrackMenu's write actions. Each mirrors the exact SQL Mixxx's own
// DAOs run for the same action (library/dao/playlistdao.cpp,
// library/trackset/crate/cratestorage.cpp, library/dao/trackdao.cpp).

bool MixHouseLibrary::addTrackToPlaylist(int trackId, int playlistId)
{
    if (!ensureWritable() || isPlaylistLocked(playlistId)) {
        return false;
    }
    // PlaylistDAO::appendTracksToPlaylist: append after the last position in
    // the playlist (0 if empty, so the track becomes position 1).
    QSqlQuery maxPos(m_writeDb);
    maxPos.prepare(QStringLiteral(
        "SELECT MAX(position) FROM PlaylistTracks WHERE playlist_id = :pl"));
    maxPos.bindValue(QStringLiteral(":pl"), playlistId);
    if (!maxPos.exec() || !maxPos.next()) {
        LOGE() << "addTrackToPlaylist: position query failed: " << maxPos.lastError().text();
        return false;
    }
    const int position = maxPos.value(0).toInt() + 1;

    QSqlQuery ins(m_writeDb);
    ins.prepare(QStringLiteral(
        "INSERT INTO PlaylistTracks (playlist_id, track_id, position, pl_datetime_added) "
        "VALUES (:playlist_id, :track_id, :position, CURRENT_TIMESTAMP)"));
    ins.bindValue(QStringLiteral(":playlist_id"), playlistId);
    ins.bindValue(QStringLiteral(":track_id"), trackId);
    ins.bindValue(QStringLiteral(":position"), position);
    if (!ins.exec()) {
        LOGE() << "addTrackToPlaylist failed: " << ins.lastError().text();
        return false;
    }
    return true;
}

bool MixHouseLibrary::addTrackToCrate(int trackId, int crateId)
{
    if (!ensureWritable() || isCrateLocked(crateId)) {
        return false;
    }
    // CrateStorage::onAddingCrateTracks: no position concept, and already-in-
    // crate is a silent no-op (INSERT OR IGNORE), not an error.
    QSqlQuery ins(m_writeDb);
    ins.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO crate_tracks (crate_id, track_id) VALUES (:crate_id, :track_id)"));
    ins.bindValue(QStringLiteral(":crate_id"), crateId);
    ins.bindValue(QStringLiteral(":track_id"), trackId);
    if (!ins.exec()) {
        LOGE() << "addTrackToCrate failed: " << ins.lastError().text();
        return false;
    }
    return true;
}

bool MixHouseLibrary::setTrackColor(int trackId, int color)
{
    if (!ensureWritable()) {
        return false;
    }
    // RgbColor::toQVariant on an empty std::optional binds NULL; -1 here means
    // the same "no color" mixxx's own WColorPicker's AllowNoColor option sets.
    QSqlQuery upd(m_writeDb);
    upd.prepare(QStringLiteral("UPDATE library SET color = :color WHERE id = :id"));
    upd.bindValue(QStringLiteral(":color"), color >= 0 ? QVariant(color) : QVariant());
    upd.bindValue(QStringLiteral(":id"), trackId);
    if (!upd.exec()) {
        LOGE() << "setTrackColor failed: " << upd.lastError().text();
        return false;
    }
    return true;
}

bool MixHouseLibrary::isPlaylistLocked(int playlistId) const
{
    QSqlQuery q(m_writeDb);
    q.prepare(QStringLiteral("SELECT locked FROM Playlists WHERE id = :id"));
    q.bindValue(QStringLiteral(":id"), playlistId);
    return q.exec() && q.next() && q.value(0).toInt() != 0;
}

bool MixHouseLibrary::isCrateLocked(int crateId) const
{
    QSqlQuery q(m_writeDb);
    q.prepare(QStringLiteral("SELECT locked FROM crates WHERE id = :id"));
    q.bindValue(QStringLiteral(":id"), crateId);
    return q.exec() && q.next() && q.value(0).toInt() != 0;
}

bool MixHouseLibrary::setPlaylistLocked(int playlistId, bool locked)
{
    if (!ensureWritable()) {
        return false;
    }
    QSqlQuery upd(m_writeDb);
    upd.prepare(QStringLiteral("UPDATE Playlists SET locked = :locked WHERE id = :id"));
    upd.bindValue(QStringLiteral(":locked"), locked ? 1 : 0);
    upd.bindValue(QStringLiteral(":id"), playlistId);
    if (!upd.exec()) {
        LOGE() << "setPlaylistLocked failed: " << upd.lastError().text();
        return false;
    }
    m_libraryChanged.notify();
    return true;
}

bool MixHouseLibrary::setCrateLocked(int crateId, bool locked)
{
    if (!ensureWritable()) {
        return false;
    }
    QSqlQuery upd(m_writeDb);
    upd.prepare(QStringLiteral("UPDATE crates SET locked = :locked WHERE id = :id"));
    upd.bindValue(QStringLiteral(":locked"), locked ? 1 : 0);
    upd.bindValue(QStringLiteral(":id"), crateId);
    if (!upd.exec()) {
        LOGE() << "setCrateLocked failed: " << upd.lastError().text();
        return false;
    }
    m_libraryChanged.notify();
    return true;
}

int MixHouseLibrary::createPlaylist(const QString& name, int parentId)
{
    if (name.trimmed().isEmpty() || !ensureWritable()) {
        return -1;
    }
    // Real mixxx pre-checks PlaylistDAO::getPlaylistIdFromName() (exact,
    // case-sensitive match -- ported verbatim, no COLLATE NOCASE) before
    // both create and rename, and shows "A playlist by that name already
    // exists" (widget/wtrackmenu.cpp:1586-1620; library/trackset/
    // baseplaylistfeature.cpp:366-405). Our own Playlists table has no
    // UNIQUE constraint on name (unlike crates below), so without this
    // check two playlists could silently end up with the identical name.
    // Real rekordbox's own equivalent check is scoped to one folder (two
    // DIFFERENT folders may each hold a playlist named "Warmup") -- kept
    // global here instead, matching this port's own pre-existing
    // (folder-less) uniqueness rule rather than narrowing it now.
    QSqlQuery findName(m_writeDb);
    // GlideMix: as in rekordbox, the same name can be used in another folder.
    findName.prepare(QStringLiteral("SELECT id FROM Playlists WHERE name = :name AND parent_id = :parent AND hidden = 0"));
    findName.bindValue(QStringLiteral(":name"), name.trimmed());
    findName.bindValue(QStringLiteral(":parent"), parentId);
    if (findName.exec() && findName.next()) {
        return -1;
    }
    // PlaylistDAO::createPlaylist: append after the last playlist position;
    // hidden=0 (PLHT_NOT_HIDDEN) -- a real, ordinary playlist, not Auto DJ or
    // a history log.
    QSqlQuery maxPos(m_writeDb);
    maxPos.prepare(QStringLiteral("SELECT MAX(position) FROM Playlists"));
    if (!maxPos.exec() || !maxPos.next()) {
        LOGE() << "createPlaylist: position query failed: " << maxPos.lastError().text();
        return -1;
    }
    const int position = maxPos.value(0).toInt() + 1;

    QSqlQuery ins(m_writeDb);
    ins.prepare(QStringLiteral(
        "INSERT INTO Playlists (name, position, hidden, date_created, date_modified, parent_id) "
        "VALUES (:name, :position, 0, CURRENT_TIMESTAMP, CURRENT_TIMESTAMP, :parent)"));
    ins.bindValue(QStringLiteral(":name"), name.trimmed());
    ins.bindValue(QStringLiteral(":position"), position);
    ins.bindValue(QStringLiteral(":parent"), parentId);
    if (!ins.exec()) {
        LOGE() << "createPlaylist failed: " << ins.lastError().text();
        return -1;
    }
    const int id = ins.lastInsertId().toInt();
    appendToManualOrder(id, parentId);
    return id;
}

int MixHouseLibrary::createPlaylistFolder(const QString& name, int parentId)
{
    // Same name-collision/position bookkeeping as createPlaylist() above --
    // a folder is a real Playlists row (is_folder=1), just one the UI never
    // offers "Add to Playlist" for. See MixHouseNamedItem::parentId's own
    // comment for why this whole feature is original work, not a port.
    if (name.trimmed().isEmpty() || !ensureWritable()) {
        return -1;
    }
    QSqlQuery findName(m_writeDb);
    // GlideMix: as in rekordbox, the same name can be used in another folder.
    findName.prepare(QStringLiteral("SELECT id FROM Playlists WHERE name = :name AND parent_id = :parent AND hidden = 0"));
    findName.bindValue(QStringLiteral(":name"), name.trimmed());
    findName.bindValue(QStringLiteral(":parent"), parentId);
    if (findName.exec() && findName.next()) {
        return -1;
    }
    QSqlQuery maxPos(m_writeDb);
    maxPos.prepare(QStringLiteral("SELECT MAX(position) FROM Playlists"));
    if (!maxPos.exec() || !maxPos.next()) {
        LOGE() << "createPlaylistFolder: position query failed: " << maxPos.lastError().text();
        return -1;
    }
    const int position = maxPos.value(0).toInt() + 1;

    QSqlQuery ins(m_writeDb);
    ins.prepare(QStringLiteral(
        "INSERT INTO Playlists (name, position, hidden, date_created, date_modified, parent_id, is_folder) "
        "VALUES (:name, :position, 0, CURRENT_TIMESTAMP, CURRENT_TIMESTAMP, :parent, 1)"));
    ins.bindValue(QStringLiteral(":name"), name.trimmed());
    ins.bindValue(QStringLiteral(":position"), position);
    ins.bindValue(QStringLiteral(":parent"), parentId);
    if (!ins.exec()) {
        LOGE() << "createPlaylistFolder failed: " << ins.lastError().text();
        return -1;
    }
    const int id = ins.lastInsertId().toInt();
    appendToManualOrder(id, parentId);
    return id;
}

//! A folder the user has arranged by hand gets new playlists at its end, as
//! in rekordbox; one never arranged stays in alphabetical order.
void MixHouseLibrary::appendToManualOrder(int playlistId, int parentId)
{
    QSqlQuery q(m_writeDb);
    q.prepare(QStringLiteral(
        "UPDATE Playlists SET sort_order = (SELECT MAX(sort_order) + 1 FROM Playlists "
        "WHERE parent_id = ? AND hidden = 0 AND id != ?) "
        "WHERE id = ? AND EXISTS (SELECT 1 FROM Playlists WHERE parent_id = ? AND hidden = 0 AND id != ? "
        "AND sort_order >= 0)"));
    q.addBindValue(parentId);
    q.addBindValue(playlistId);
    q.addBindValue(playlistId);
    q.addBindValue(parentId);
    q.addBindValue(playlistId);
    if (!q.exec()) {
        LOGW() << "appendToManualOrder failed: " << q.lastError().text();
    }
}

bool MixHouseLibrary::movePlaylistToFolder(int playlistId, int newParentId)
{
    if (!ensureWritable() || isPlaylistLocked(playlistId)) {
        return false;
    }
    if (playlistId == newParentId) {
        return false;   // a node can't be its own parent
    }
    // Walk newParentId's own ancestor chain looking for playlistId -- moving
    // a folder INTO one of its own descendants would create a cycle a tree
    // can't represent (real rekordbox simply refuses this drag too). Capped
    // at a generous depth rather than an unbounded loop, purely as a safety
    // net against a real data bug (a stray cycle already in the table)
    // turning into an infinite loop here.
    int walk = newParentId;
    for (int depth = 0; walk >= 0 && depth < 1000; ++depth) {
        if (walk == playlistId) {
            return false;
        }
        QSqlQuery parentOf(m_writeDb);
        parentOf.prepare(QStringLiteral("SELECT parent_id FROM Playlists WHERE id = :id"));
        parentOf.bindValue(QStringLiteral(":id"), walk);
        if (!parentOf.exec() || !parentOf.next()) {
            break;
        }
        walk = parentOf.value(0).toInt();
    }
    QSqlQuery upd(m_writeDb);
    upd.prepare(QStringLiteral("UPDATE Playlists SET parent_id = :parent WHERE id = :id"));
    upd.bindValue(QStringLiteral(":parent"), newParentId);
    upd.bindValue(QStringLiteral(":id"), playlistId);
    if (!upd.exec()) {
        LOGE() << "movePlaylistToFolder failed: " << upd.lastError().text();
        return false;
    }
    m_libraryChanged.notify();
    return true;
}

bool MixHouseLibrary::movePlaylistOrder(int playlistId, bool moveUp)
{
    if (!ensureWritable() || isPlaylistLocked(playlistId)) {
        return false;
    }
    QSqlQuery parentQ(m_writeDb);
    parentQ.prepare(QStringLiteral("SELECT parent_id FROM Playlists WHERE id = :id"));
    parentQ.bindValue(QStringLiteral(":id"), playlistId);
    if (!parentQ.exec() || !parentQ.next()) {
        return false;
    }
    const int parentId = parentQ.value(0).toInt();

    // Real siblings, in their CURRENT effective display order -- same
    // ORDER BY playlists() itself uses, so "current order" here means
    // exactly what's on screen right now, whether that's still the
    // alphabetical default (every sort_order still -1, tied on name) or an
    // already-customized order from an earlier move.
    QSqlQuery siblingsQ(m_writeDb);
    siblingsQ.prepare(QStringLiteral(
        "SELECT id FROM Playlists WHERE parent_id = :parent AND hidden = 0 "
        "ORDER BY sort_order, name COLLATE NOCASE"));
    siblingsQ.bindValue(QStringLiteral(":parent"), parentId);
    if (!siblingsQ.exec()) {
        LOGE() << "movePlaylistOrder: siblings query failed: " << siblingsQ.lastError().text();
        return false;
    }
    QList<int> siblings;
    while (siblingsQ.next()) {
        siblings.append(siblingsQ.value(0).toInt());
    }
    const int index = siblings.indexOf(playlistId);
    const int swapIndex = moveUp ? index - 1 : index + 1;
    if (index < 0 || swapIndex < 0 || swapIndex >= siblings.size()) {
        return false;   // already at that end -- a real no-op, not an error
    }
    const int tmp = siblings[index];
    siblings[index] = siblings[swapIndex];
    siblings[swapIndex] = tmp;

    // Renumber the WHOLE sibling group sequentially from its new order --
    // this is also what "activates" a real manual order the first time any
    // one of these siblings is ever moved (every untouched row starts at
    // sort_order -1, tied alphabetically): renumbering from the group's own
    // CURRENT effective order means every row except the two that just
    // swapped keeps its exact same relative position, so the very first
    // move only visibly changes the two rows the user actually asked to
    // swap.
    for (int i = 0; i < siblings.size(); ++i) {
        QSqlQuery upd(m_writeDb);
        upd.prepare(QStringLiteral("UPDATE Playlists SET sort_order = :order WHERE id = :id"));
        upd.bindValue(QStringLiteral(":order"), i);
        upd.bindValue(QStringLiteral(":id"), siblings[i]);
        if (!upd.exec()) {
            LOGE() << "movePlaylistOrder failed: " << upd.lastError().text();
            return false;
        }
    }
    m_libraryChanged.notify();
    return true;
}

bool MixHouseLibrary::markTrackPlayed(int trackId)
{
    if (trackId < 0 || !ensureWritable()) {
        return false;
    }
    QSqlQuery q(m_writeDb);
    q.prepare(QStringLiteral(
        "UPDATE library SET timesplayed = timesplayed + 1, last_played_at = CURRENT_TIMESTAMP WHERE id = :id"));
    q.bindValue(QStringLiteral(":id"), trackId);
    if (!q.exec()) {
        LOGW() << "markTrackPlayed failed: " << q.lastError().text();
        return false;
    }
    return true;
}

bool MixHouseLibrary::movePlaylist(int playlistId, int parentId, int beforeId)
{
    if (!ensureWritable() || isPlaylistLocked(playlistId) || beforeId == playlistId) {
        return false;
    }
    QSqlQuery parentQ(m_writeDb);
    parentQ.prepare(QStringLiteral("SELECT parent_id FROM Playlists WHERE id = :id"));
    parentQ.bindValue(QStringLiteral(":id"), playlistId);
    if (!parentQ.exec() || !parentQ.next()) {
        return false;
    }
    if (parentQ.value(0).toInt() != parentId && !movePlaylistToFolder(playlistId, parentId)) {
        return false;
    }
    QSqlQuery siblingsQ(m_writeDb);
    siblingsQ.prepare(QStringLiteral(
        "SELECT id FROM Playlists WHERE parent_id = :parent AND hidden = 0 "
        "ORDER BY sort_order, name COLLATE NOCASE"));
    siblingsQ.bindValue(QStringLiteral(":parent"), parentId);
    if (!siblingsQ.exec()) {
        return false;
    }
    QList<int> siblings;
    while (siblingsQ.next()) {
        const int id = siblingsQ.value(0).toInt();
        if (id != playlistId) {
            siblings.append(id);
        }
    }
    const int at = siblings.indexOf(beforeId);
    siblings.insert(at >= 0 ? at : siblings.size(), playlistId);
    m_writeDb.transaction();
    for (int i = 0; i < siblings.size(); ++i) {
        QSqlQuery upd(m_writeDb);
        upd.prepare(QStringLiteral("UPDATE Playlists SET sort_order = :order WHERE id = :id"));
        upd.bindValue(QStringLiteral(":order"), i);
        upd.bindValue(QStringLiteral(":id"), siblings[i]);
        if (!upd.exec()) {
            LOGE() << "movePlaylist failed: " << upd.lastError().text();
            m_writeDb.rollback();
            return false;
        }
    }
    m_writeDb.commit();
    m_libraryChanged.notify();
    return true;
}

int MixHouseLibrary::createCrate(const QString& name)
{
    if (name.trimmed().isEmpty() || !ensureWritable()) {
        return -1;
    }
    // CrateStorage::onInsertingCrate: name (UNIQUE NOT NULL in the real
    // schema -- a duplicate name fails the INSERT, same as real mixxx),
    // locked=0, autodj_source=0.
    QSqlQuery ins(m_writeDb);
    ins.prepare(QStringLiteral(
        "INSERT INTO crates (name, locked, autodj_source) VALUES (:name, 0, 0)"));
    ins.bindValue(QStringLiteral(":name"), name.trimmed());
    if (!ins.exec()) {
        LOGE() << "createCrate failed: " << ins.lastError().text();
        return -1;
    }
    return ins.lastInsertId().toInt();
}

bool MixHouseLibrary::renamePlaylist(int playlistId, const QString& name)
{
    if (name.trimmed().isEmpty() || !ensureWritable() || isPlaylistLocked(playlistId)) {
        return false;
    }
    // Same real-mixxx collision check as createPlaylist() above -- a
    // different EXISTING playlist already using this exact name is a
    // failure, but the playlist keeping its own current name (a no-op
    // rename) is not.
    QSqlQuery findName(m_writeDb);
    findName.prepare(QStringLiteral("SELECT id FROM Playlists WHERE name = :name AND id != :id"));
    findName.bindValue(QStringLiteral(":name"), name.trimmed());
    findName.bindValue(QStringLiteral(":id"), playlistId);
    if (findName.exec() && findName.next()) {
        return false;
    }
    QSqlQuery upd(m_writeDb);
    upd.prepare(QStringLiteral("UPDATE Playlists SET name = :name WHERE id = :id"));
    upd.bindValue(QStringLiteral(":name"), name.trimmed());
    upd.bindValue(QStringLiteral(":id"), playlistId);
    if (!upd.exec()) {
        LOGE() << "renamePlaylist failed: " << upd.lastError().text();
        return false;
    }
    return true;
}

bool MixHouseLibrary::renameCrate(int crateId, const QString& name)
{
    if (name.trimmed().isEmpty() || !ensureWritable() || isCrateLocked(crateId)) {
        return false;
    }
    // Real mixxx's UPDATE also rewrites locked/autodj_source in the same
    // statement (CrateStorage::onUpdatingCrate), but it's a straight
    // pass-through of the crate's own existing values there -- since we're
    // not touching either, updating just the name has the identical effect.
    QSqlQuery upd(m_writeDb);
    upd.prepare(QStringLiteral("UPDATE crates SET name = :name WHERE id = :id"));
    upd.bindValue(QStringLiteral(":name"), name.trimmed());
    upd.bindValue(QStringLiteral(":id"), crateId);
    if (!upd.exec()) {
        LOGE() << "renameCrate failed: " << upd.lastError().text();
        return false;
    }
    return true;
}

bool MixHouseLibrary::deletePlaylist(int playlistId)
{
    if (!ensureWritable()) {
        return false;
    }
    // Real rekordbox: deleting a folder takes every playlist/sub-folder
    // inside it with it (manual pp.52, 248) -- real mixxx has no folder
    // concept to model this after at all (see MixHouseNamedItem::
    // parentId's own comment), so this walks the whole real subtree here,
    // breadth-first, rather than porting anything. Refuses the WHOLE
    // operation if anything anywhere in that subtree is locked, rather than
    // doing a partial delete that would leave the tree in a confusing
    // half-deleted state -- for a plain (non-folder) playlist this is
    // exactly the single-node case it always was.
    QList<int> toDelete;
    QList<int> frontier;
    frontier.append(playlistId);
    while (!frontier.isEmpty()) {
        const int id = frontier.takeFirst();
        if (isPlaylistLocked(id)) {
            return false;
        }
        toDelete.append(id);
        QSqlQuery children(m_writeDb);
        children.prepare(QStringLiteral("SELECT id FROM Playlists WHERE parent_id = :id"));
        children.bindValue(QStringLiteral(":id"), id);
        if (children.exec()) {
            while (children.next()) {
                frontier.append(children.value(0).toInt());
            }
        }
    }
    const QString idList = sqlInClause(toDelete);
    QSqlQuery delTracks(m_writeDb);
    if (!delTracks.exec(QStringLiteral("DELETE FROM PlaylistTracks WHERE playlist_id IN (%1)").arg(idList))) {
        LOGE() << "deletePlaylist (membership rows) failed: " << delTracks.lastError().text();
        return false;
    }
    QSqlQuery delPl(m_writeDb);
    if (!delPl.exec(QStringLiteral("DELETE FROM Playlists WHERE id IN (%1)").arg(idList))) {
        LOGE() << "deletePlaylist failed: " << delPl.lastError().text();
        return false;
    }
    return true;
}

bool MixHouseLibrary::deleteCrate(int crateId)
{
    if (!ensureWritable() || isCrateLocked(crateId)) {
        return false;
    }
    QSqlQuery delTracks(m_writeDb);
    delTracks.prepare(QStringLiteral("DELETE FROM crate_tracks WHERE crate_id = :id"));
    delTracks.bindValue(QStringLiteral(":id"), crateId);
    if (!delTracks.exec()) {
        LOGE() << "deleteCrate (membership rows) failed: " << delTracks.lastError().text();
        return false;
    }
    QSqlQuery delCr(m_writeDb);
    delCr.prepare(QStringLiteral("DELETE FROM crates WHERE id = :id"));
    delCr.bindValue(QStringLiteral(":id"), crateId);
    if (!delCr.exec()) {
        LOGE() << "deleteCrate failed: " << delCr.lastError().text();
        return false;
    }
    return true;
}

bool MixHouseLibrary::setTrackHidden(int trackId, bool hidden)
{
    if (!ensureWritable()) {
        return false;
    }
    // TrackDAO::hideTracks / unhideTracks, for a single id.
    QSqlQuery upd(m_writeDb);
    if (hidden) {
        upd.prepare(QStringLiteral("UPDATE library SET mixxx_deleted = 1 WHERE id = :id"));
    } else {
        upd.prepare(QStringLiteral(
            "UPDATE library SET mixxx_deleted = 0 WHERE mixxx_deleted != 0 AND id = :id"));
    }
    upd.bindValue(QStringLiteral(":id"), trackId);
    if (!upd.exec()) {
        LOGE() << "setTrackHidden failed: " << upd.lastError().text();
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Round 2 of the real WTrackMenu port: Auto DJ queueing, rating, BPM
// adjust/lock, the Clear submenu, and Purge. Same style as above -- every
// statement matched against the exact real DAO method it mirrors.

int MixHouseLibrary::autoDjPlaylistId() const
{
    if (!ensureOpen()) {
        return -1;
    }
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT id FROM Playlists WHERE hidden = 1 LIMIT 1"));
    if (!q.exec() || !q.next()) {
        return -1;
    }
    return q.value(0).toInt();
}

bool MixHouseLibrary::addTrackToAutoDj(int trackId, MixHouseAutoDjLoc loc)
{
    if (!ensureWritable()) {
        return false;
    }
    const int autodjId = autoDjPlaylistId();
    if (autodjId < 0) {
        LOGE() << "addTrackToAutoDj: no Auto DJ playlist row";
        return false;
    }

    // PlaylistDAO::addTracksToAutoDJQueue: no live deck here, so "next track
    // already loaded" never applies -- Top always means position 1.
    switch (loc) {
    case MixHouseAutoDjLoc::Bottom: {
        // appendTracksToPlaylist, same statement as addTrackToPlaylist.
        QSqlQuery maxPos(m_writeDb);
        maxPos.prepare(QStringLiteral(
            "SELECT MAX(position) FROM PlaylistTracks WHERE playlist_id = :pl"));
        maxPos.bindValue(QStringLiteral(":pl"), autodjId);
        if (!maxPos.exec() || !maxPos.next()) {
            LOGE() << "addTrackToAutoDj (bottom): position query failed: " << maxPos.lastError().text();
            return false;
        }
        const int position = maxPos.value(0).toInt() + 1;
        QSqlQuery ins(m_writeDb);
        ins.prepare(QStringLiteral(
            "INSERT INTO PlaylistTracks (playlist_id, track_id, position, pl_datetime_added) "
            "VALUES (:playlist_id, :track_id, :position, CURRENT_TIMESTAMP)"));
        ins.bindValue(QStringLiteral(":playlist_id"), autodjId);
        ins.bindValue(QStringLiteral(":track_id"), trackId);
        ins.bindValue(QStringLiteral(":position"), position);
        if (!ins.exec()) {
            LOGE() << "addTrackToAutoDj (bottom) failed: " << ins.lastError().text();
            return false;
        }
        return true;
    }
    case MixHouseAutoDjLoc::Top: {
        // insertTracksIntoPlaylist(trackIds, autodjId, 1): shift everything
        // already in the queue down by one position, then insert at 1.
        QSqlQuery shift(m_writeDb);
        shift.prepare(QStringLiteral(
            "UPDATE PlaylistTracks SET position = position + 1 "
            "WHERE playlist_id = :pl AND position >= 1"));
        shift.bindValue(QStringLiteral(":pl"), autodjId);
        if (!shift.exec()) {
            LOGE() << "addTrackToAutoDj (top) shift failed: " << shift.lastError().text();
            return false;
        }
        QSqlQuery ins(m_writeDb);
        ins.prepare(QStringLiteral(
            "INSERT INTO PlaylistTracks (playlist_id, track_id, position) "
            "VALUES (:playlist_id, :track_id, 1)"));
        ins.bindValue(QStringLiteral(":playlist_id"), autodjId);
        ins.bindValue(QStringLiteral(":track_id"), trackId);
        if (!ins.exec()) {
            LOGE() << "addTrackToAutoDj (top) insert failed: " << ins.lastError().text();
            return false;
        }
        return true;
    }
    case MixHouseAutoDjLoc::Replace: {
        // removeTracksFromPlaylist(autodjId, 1) then append.
        QSqlQuery clear(m_writeDb);
        clear.prepare(QStringLiteral(
            "DELETE FROM PlaylistTracks WHERE playlist_id = :pl AND position >= 1"));
        clear.bindValue(QStringLiteral(":pl"), autodjId);
        if (!clear.exec()) {
            LOGE() << "addTrackToAutoDj (replace) clear failed: " << clear.lastError().text();
            return false;
        }
        QSqlQuery ins(m_writeDb);
        ins.prepare(QStringLiteral(
            "INSERT INTO PlaylistTracks (playlist_id, track_id, position) "
            "VALUES (:playlist_id, :track_id, 1)"));
        ins.bindValue(QStringLiteral(":playlist_id"), autodjId);
        ins.bindValue(QStringLiteral(":track_id"), trackId);
        if (!ins.exec()) {
            LOGE() << "addTrackToAutoDj (replace) insert failed: " << ins.lastError().text();
            return false;
        }
        return true;
    }
    }
    return false;
}

bool MixHouseLibrary::setTrackRating(int trackId, int rating)
{
    if (!ensureWritable()) {
        return false;
    }
    QSqlQuery upd(m_writeDb);
    upd.prepare(QStringLiteral("UPDATE library SET rating = :rating WHERE id = :id"));
    upd.bindValue(QStringLiteral(":rating"), qBound(0, rating, 5));
    upd.bindValue(QStringLiteral(":id"), trackId);
    if (!upd.exec()) {
        LOGE() << "setTrackRating failed: " << upd.lastError().text();
        return false;
    }
    return true;
}

// Shared by the five plain-text column setters below -- see
// IMixHouseLibrary::setTrackComment's own comment for what these port.
bool MixHouseLibrary::writeFileTagProperty(const QString& filePath, const QString& propertyKey, const QString& value)
{
    TagLib::FileRef f = au::mixhouse::openTagFile(filePath);
    if (f.isNull() || !f.file()) {
        LOGW() << "writeFileTagProperty: TagLib couldn't open " << filePath;
        return false;
    }
    TagLib::PropertyMap props = f.file()->properties();
    props.replace(propertyKey.toStdString(),
                  TagLib::StringList(TagLib::String(value.toUtf8().constData(), TagLib::String::UTF8)));
    f.file()->setProperties(props);
    if (!f.save()) {
        LOGW() << "writeFileTagProperty: TagLib save failed for " << filePath;
        return false;
    }
    return true;
}

bool MixHouseLibrary::setTrackTags(int trackId, const QVariantMap& fields, bool writeFile, bool keepAsEdit)
{
    if (fields.isEmpty() || !ensureWritable()) {
        return false;
    }
    QSqlQuery findPath(m_writeDb);
    findPath.prepare(QStringLiteral(
        "SELECT t.location FROM library l JOIN track_locations t ON t.id = l.location WHERE l.id = :id"));
    findPath.bindValue(QStringLiteral(":id"), trackId);
    const QString location = findPath.exec() && findPath.next() ? findPath.value(0).toString() : QString();

    QStringList sets;
    for (const TagField& f : kTagFields) {
        if (fields.contains(QString::fromLatin1(f.key))) {
            sets << QStringLiteral("%1 = :%2").arg(QString::fromLatin1(f.column), QString::fromLatin1(f.key));
        }
    }
    if (sets.isEmpty()) {
        return false;
    }
    QSqlQuery upd(m_writeDb);
    upd.prepare(QStringLiteral("UPDATE library SET %1 WHERE id = :id").arg(sets.join(QStringLiteral(", "))));
    for (const TagField& f : kTagFields) {
        const QString key = QString::fromLatin1(f.key);
        if (fields.contains(key)) {
            upd.bindValue(QStringLiteral(":") + key, fields.value(key).toString());
        }
    }
    upd.bindValue(QStringLiteral(":id"), trackId);
    if (!upd.exec()) {
        LOGE() << "setTrackTags failed: " << upd.lastError().text();
        return false;
    }

    if (keepAsEdit && !location.isEmpty()) {
        QJsonObject edits;
        QSqlQuery old(m_writeDb);
        old.prepare(QStringLiteral("SELECT fields FROM mixhouse_tag_edits WHERE location = :loc"));
        old.bindValue(QStringLiteral(":loc"), location);
        if (old.exec() && old.next()) {
            edits = QJsonDocument::fromJson(old.value(0).toString().toUtf8()).object();
        }
        for (auto it = fields.constBegin(); it != fields.constEnd(); ++it) {
            edits.insert(it.key(), it.value().toString());
        }
        QSqlQuery save(m_writeDb);
        save.prepare(QStringLiteral("INSERT OR REPLACE INTO mixhouse_tag_edits (location, fields) VALUES (:loc, :fields)"));
        save.bindValue(QStringLiteral(":loc"), location);
        save.bindValue(QStringLiteral(":fields"), QString::fromUtf8(QJsonDocument(edits).toJson(QJsonDocument::Compact)));
        save.exec();
    }

    if (writeFile && !location.isEmpty()) {
        TagLib::FileRef file = au::mixhouse::openTagFile(location);
        if (file.isNull() || !file.file()) {
            LOGW() << "setTrackTags: TagLib couldn't open " << location;
            return true;   // the library has them; the file stays as it was
        }
        TagLib::PropertyMap props = file.file()->properties();
        for (const TagField& f : kTagFields) {
            const QString key = QString::fromLatin1(f.key);
            if (!fields.contains(key)) {
                continue;
            }
            const QString value = fields.value(key).toString();
            if (value.isEmpty()) {
                props.erase(f.tag);
            } else {
                props.replace(f.tag, TagLib::StringList(TagLib::String(value.toUtf8().constData(), TagLib::String::UTF8)));
            }
        }
        file.file()->setProperties(props);
        if (!file.save()) {
            LOGW() << "setTrackTags: TagLib save failed for " << location;
        }
    }
    return true;
}

QHash<QString, QVariantMap> MixHouseLibrary::tagEditsByLocation() const
{
    QHash<QString, QVariantMap> out;
    if (!ensureOpen()) {
        return out;
    }
    QSqlQuery q(m_writeDb.isValid() && m_writeDb.isOpen() ? m_writeDb : m_db);
    q.setForwardOnly(true);
    if (!q.exec(QStringLiteral("SELECT location, fields FROM mixhouse_tag_edits"))) {
        return out;   // no table yet: nothing edited
    }
    while (q.next()) {
        out.insert(q.value(0).toString(), QJsonDocument::fromJson(q.value(1).toString().toUtf8()).object().toVariantMap());
    }
    return out;
}

QHash<int, QString> MixHouseLibrary::trackLocations(const QList<int>& trackIds) const
{
    QHash<int, QString> out;
    if (!ensureOpen()) {
        return out;
    }
    const QSet<int> wanted(trackIds.begin(), trackIds.end());
    QSqlQuery q(m_writeDb.isValid() && m_writeDb.isOpen() ? m_writeDb : m_db);
    q.setForwardOnly(true);
    if (!q.exec(QStringLiteral("SELECT l.id, t.location FROM library l JOIN track_locations t ON t.id = l.location"))) {
        LOGE() << "trackLocations failed: " << q.lastError().text();
        return out;
    }
    while (q.next()) {
        const int id = q.value(0).toInt();
        if (wanted.contains(id)) {
            out.insert(id, q.value(1).toString());
        }
    }
    return out;
}

int MixHouseLibrary::applyFileTags(const std::vector<std::pair<int, MixHouseFileTags> >& tags)
{
    if (!ensureWritable()) {
        return -1;
    }
    QSqlQuery old(m_writeDb);
    old.prepare(QStringLiteral(
        "SELECT title, artist, album, genre, year, replaygain, replaygain_peak FROM library WHERE id = :id"));
    int changed = 0;
    QSqlQuery upd(m_writeDb);
    upd.prepare(QStringLiteral(
        "UPDATE library SET title = COALESCE(NULLIF(:title, ''), title), artist = COALESCE(NULLIF(:artist, ''), artist), "
        "album = COALESCE(NULLIF(:album, ''), album), genre = COALESCE(NULLIF(:genre, ''), genre), "
        "year = COALESCE(NULLIF(:year, ''), year), "
        "replaygain = CASE WHEN :gainSet > 0 THEN :gain ELSE replaygain END, "
        "replaygain_peak = CASE WHEN :peakSet >= 0 THEN :peak ELSE replaygain_peak END "
        "WHERE id = :id"));
    m_writeDb.transaction();
    for (const auto& [trackId, t] : tags) {
        old.bindValue(QStringLiteral(":id"), trackId);
        if (!old.exec() || !old.next()) {
            continue;
        }
        const auto differs = [&old](int column, const QString& value) {
            return !value.isEmpty() && value != old.value(column).toString();
        };
        const auto differsNumber = [&old](int column, double value) {
            return std::abs(value - old.value(column).toDouble()) > 1e-6;
        };
        if (!(differs(0, t.fromTags ? t.title : QString()) || differs(1, t.artist) || differs(2, t.album)
              || differs(3, t.genre) || differs(4, t.year)
              || (t.replayGain > 0.0 && differsNumber(5, t.replayGain))
              || (t.replayGainPeak >= 0.0 && differsNumber(6, t.replayGainPeak)))) {
            continue;
        }
        ++changed;
        upd.bindValue(QStringLiteral(":title"), t.fromTags ? t.title : QString());
        upd.bindValue(QStringLiteral(":artist"), t.artist);
        upd.bindValue(QStringLiteral(":album"), t.album);
        upd.bindValue(QStringLiteral(":genre"), t.genre);
        upd.bindValue(QStringLiteral(":year"), t.year);
        upd.bindValue(QStringLiteral(":gainSet"), t.replayGain);
        upd.bindValue(QStringLiteral(":gain"), t.replayGain);
        upd.bindValue(QStringLiteral(":peakSet"), t.replayGainPeak);
        upd.bindValue(QStringLiteral(":peak"), t.replayGainPeak);
        upd.bindValue(QStringLiteral(":id"), trackId);
        if (!upd.exec()) {
            LOGE() << "applyFileTags failed: " << upd.lastError().text();
            m_writeDb.rollback();
            return -1;
        }
    }
    if (!m_writeDb.commit()) {
        m_writeDb.rollback();
        return -1;
    }
    return changed;
}

void MixHouseLibrary::requestRevealTrack(int trackId, RevealMode mode)
{
    m_revealTrackRequested.send(trackId, mode);
}

muse::async::Channel<int, IMixHouseLibrary::RevealMode> MixHouseLibrary::revealTrackRequested() const
{
    return m_revealTrackRequested;
}

std::vector<MixHouseSmartPlaylist> MixHouseLibrary::smartPlaylists() const
{
    std::vector<MixHouseSmartPlaylist> out;
    if (!ensureOpen()) {
        return out;
    }
    QSqlQuery q(m_writeDb.isValid() && m_writeDb.isOpen() ? m_writeDb : m_db);
    q.setForwardOnly(true);
    if (!q.exec(QStringLiteral("SELECT id, name, definition FROM mixhouse_smart_playlists ORDER BY position, id"))) {
        return out;   // no table yet
    }
    while (q.next()) {
        MixHouseSmartPlaylist p = MixHouseSmartPlaylist::fromJson(q.value(2).toString());
        p.id = q.value(0).toInt();
        p.name = q.value(1).toString();
        out.push_back(std::move(p));
    }
    return out;
}

int MixHouseLibrary::saveSmartPlaylist(const MixHouseSmartPlaylist& playlist)
{
    if (!ensureWritable()) {
        return -1;
    }
    QSqlQuery q(m_writeDb);
    if (playlist.id < 0) {
        q.prepare(QStringLiteral("INSERT INTO mixhouse_smart_playlists (name, definition, position) "
                                 "VALUES (:name, :def, (SELECT COALESCE(MAX(position), 0) + 1 FROM mixhouse_smart_playlists))"));
    } else {
        q.prepare(QStringLiteral("UPDATE mixhouse_smart_playlists SET name = :name, definition = :def WHERE id = :id"));
        q.bindValue(QStringLiteral(":id"), playlist.id);
    }
    q.bindValue(QStringLiteral(":name"), playlist.name);
    q.bindValue(QStringLiteral(":def"), playlist.toJson());
    if (!q.exec()) {
        LOGW() << "could not save smart playlist: " << q.lastError().text();
        return -1;
    }
    const int id = playlist.id < 0 ? q.lastInsertId().toInt() : playlist.id;
    m_libraryChanged.notify();
    return id;
}

bool MixHouseLibrary::deleteSmartPlaylist(int id)
{
    if (!ensureWritable()) {
        return false;
    }
    QSqlQuery q(m_writeDb);
    q.prepare(QStringLiteral("DELETE FROM mixhouse_smart_playlists WHERE id = :id"));
    q.bindValue(QStringLiteral(":id"), id);
    if (!q.exec()) {
        return false;
    }
    m_libraryChanged.notify();
    return true;
}

bool MixHouseLibrary::setTrackTextField(int trackId, const QString& column, const QString& text,
                                        const QString& tagLibPropertyKey)
{
    if (!ensureWritable()) {
        return false;
    }
    QSqlQuery upd(m_writeDb);
    upd.prepare(QStringLiteral("UPDATE library SET %1 = :value WHERE id = :id").arg(column));
    upd.bindValue(QStringLiteral(":value"), text);
    upd.bindValue(QStringLiteral(":id"), trackId);
    if (!upd.exec()) {
        LOGE() << "setTrackTextField (" << column << ") failed: " << upd.lastError().text();
        return false;
    }
    // "Music library" Preferences page's own "Synchronize library track
    // metadata from/to file tags" -- see IMixHouseLibraryConfiguration::
    // syncMetadataToFileTags's own comment. A failed file-tag write is
    // logged but doesn't fail this call: the library row (the source of
    // truth this whole app reads from) is already correctly updated either
    // way, matching real mixxx's own "the library wins" precedence.
    if (libraryConfiguration()->syncMetadataToFileTags()) {
        QSqlQuery findPath(m_writeDb);
        findPath.prepare(QStringLiteral(
            "SELECT t.location FROM library l JOIN track_locations t ON t.id = l.location WHERE l.id = :id"));
        findPath.bindValue(QStringLiteral(":id"), trackId);
        if (findPath.exec() && findPath.next()) {
            writeFileTagProperty(findPath.value(0).toString(), tagLibPropertyKey, text);
        }
    }
    return true;
}

bool MixHouseLibrary::setTrackComment(int trackId, const QString& text)
{
    return setTrackTextField(trackId, QStringLiteral("comment"), text, QStringLiteral("COMMENT"));
}

bool MixHouseLibrary::setTrackGrouping(int trackId, const QString& text)
{
    return setTrackTextField(trackId, QStringLiteral("grouping"), text, QStringLiteral("GROUPING"));
}

bool MixHouseLibrary::setTrackComposer(int trackId, const QString& text)
{
    return setTrackTextField(trackId, QStringLiteral("composer"), text, QStringLiteral("COMPOSER"));
}

bool MixHouseLibrary::setTrackAlbumArtist(int trackId, const QString& text)
{
    return setTrackTextField(trackId, QStringLiteral("album_artist"), text, QStringLiteral("ALBUMARTIST"));
}

bool MixHouseLibrary::setTrackNumber(int trackId, const QString& text)
{
    return setTrackTextField(trackId, QStringLiteral("tracknumber"), text, QStringLiteral("TRACKNUMBER"));
}

bool MixHouseLibrary::scaleTrackBpm(int trackId, double factor)
{
    if (!ensureWritable()) {
        return false;
    }
    // Real mixxx's Beats::tryScale() (track/beats.cpp:656-701) regenerates
    // the real beat grid, not just the cached bpm number -- for a constant-
    // tempo grid that means: same first-beat phase anchor, bpm multiplied by
    // the same factor (the beat LENGTH implicit in that bpm changes as a
    // consequence, nothing else to touch). This app's earlier port only
    // updated the `bpm` column, leaving the stored `beats` blob describing
    // the OLD tempo -- the waveform beatgrid and the BPM column would
    // visibly disagree after scaling. Read the current grid first so the
    // real anchor survives the rewrite.
    QSqlQuery find(m_writeDb);
    find.prepare(QStringLiteral(
        "SELECT bpm, beats, beats_version, samplerate, bpm_lock FROM library WHERE id = :id"));
    find.bindValue(QStringLiteral(":id"), trackId);
    if (!find.exec() || !find.next()) {
        LOGE() << "scaleTrackBpm: track not found: " << trackId;
        return false;
    }
    // Real mixxx grays these actions out entirely while bpm_lock is set;
    // mirrored here as a hard refusal rather than trusting QML not to ask.
    if (find.value(4).toInt() != 0) {
        return false;
    }
    const double oldBpm = find.value(0).toDouble();
    const double newBpm = oldBpm * factor;
    const int sampleRate = find.value(3).toInt();
    const MixHouseBeatGrid grid = decodeBeats(
        find.value(1).toByteArray(), find.value(2).toString(), sampleRate);

    QSqlQuery upd(m_writeDb);
    if (grid.valid && grid.constantTempo && sampleRate > 0) {
        upd.prepare(QStringLiteral(
            "UPDATE library SET bpm = :bpm, beats = :beats "
            "WHERE id = :id AND (bpm_lock IS NULL OR bpm_lock = 0)"));
        upd.bindValue(QStringLiteral(":beats"), encodeBeatGrid(newBpm, grid.firstBeatSec, sampleRate));
    } else {
        // No decodable constant-tempo grid to rescale (e.g. a real mixxx
        // BeatMap, or no beatgrid at all yet) -- fall back to the old
        // bpm-only behavior rather than fabricate a grid from nothing.
        upd.prepare(QStringLiteral(
            "UPDATE library SET bpm = :bpm "
            "WHERE id = :id AND (bpm_lock IS NULL OR bpm_lock = 0)"));
    }
    upd.bindValue(QStringLiteral(":bpm"), newBpm);
    upd.bindValue(QStringLiteral(":id"), trackId);
    if (!upd.exec()) {
        LOGE() << "scaleTrackBpm failed: " << upd.lastError().text();
        return false;
    }
    return upd.numRowsAffected() > 0;
}

bool MixHouseLibrary::setTrackBeatGrid(int trackId, double bpm, double firstBeatSec, double downbeatSec,
                                       const QString& detector)
{
    BeatGridEdit edit;
    edit.trackId = trackId;
    edit.bpm = bpm;
    edit.firstBeatSec = firstBeatSec;
    edit.downbeatSec = downbeatSec;
    edit.detector = detector;
    return setTrackBeatGrids({ edit }) == 1;
}

bool MixHouseLibrary::setTrackEnergy(int trackId, double energy)
{
    if (trackId < 0 || energy <= 0.0 || !ensureWritable()) {
        return false;
    }
    QSqlQuery createTable(m_writeDb);
    createTable.exec(QString::fromLatin1(kCreateEnergyTableSql));
    QSqlQuery upd(m_writeDb);
    upd.prepare(QStringLiteral("INSERT OR REPLACE INTO mixhouse_energy (track_id, energy) VALUES (:id, :energy)"));
    upd.bindValue(QStringLiteral(":id"), trackId);
    upd.bindValue(QStringLiteral(":energy"), energy);
    if (!upd.exec()) {
        LOGW() << "setTrackEnergy: track " << trackId << ": " << upd.lastError().text();
        return false;
    }
    m_trackAnalysisProgress.send(trackId, -3);
    return true;
}

int MixHouseLibrary::setTrackBeatGrids(const std::vector<BeatGridEdit>& edits)
{
    if (edits.empty() || !ensureWritable()) {
        return 0;
    }
    const bool transaction = edits.size() > 1 && m_writeDb.transaction();
    int written = 0;
    for (const BeatGridEdit& edit : edits) {
        if (edit.bpm <= 0.0) {
            continue;
        }
        QSqlQuery find(m_writeDb);
        find.prepare(QStringLiteral(
            "SELECT l.samplerate, l.bpm_lock, t.location FROM library l "
            "LEFT JOIN track_locations t ON t.id = l.location WHERE l.id = :id"));
        find.bindValue(QStringLiteral(":id"), edit.trackId);
        if (!find.exec() || !find.next()) {
            LOGE() << "setTrackBeatGrid: track not found: " << edit.trackId;
            continue;
        }
        if (find.value(1).toInt() != 0) {
            continue;
        }
        int sampleRate = find.value(0).toInt();
        if (sampleRate <= 0) {
            const AudioInfo info = probeAudioFile(find.value(2).toString());
            sampleRate = info.valid ? info.sampleRate : 0;
        }
        if (sampleRate <= 0) {
            LOGE() << "setTrackBeatGrid: no sample rate for track " << edit.trackId;
            continue;
        }

        QSqlQuery upd(m_writeDb);
        upd.prepare(QStringLiteral(
            "UPDATE library SET bpm = :bpm, beats = :beats, beats_version = 'BeatGrid-2.0', "
            "samplerate = CASE WHEN samplerate IS NULL OR samplerate <= 0 THEN :samplerate ELSE samplerate END "
            "WHERE id = :id AND (bpm_lock IS NULL OR bpm_lock = 0)"));
        upd.bindValue(QStringLiteral(":bpm"), edit.bpm);
        upd.bindValue(QStringLiteral(":beats"), encodeBeatGrid(edit.bpm, edit.firstBeatSec, sampleRate));
        upd.bindValue(QStringLiteral(":samplerate"), sampleRate);
        upd.bindValue(QStringLiteral(":id"), edit.trackId);
        if (!upd.exec()) {
            LOGE() << "setTrackBeatGrid failed: " << upd.lastError().text();
            continue;
        }
        ++written;

        QSqlQuery db(m_writeDb);
        if (!edit.detector.isEmpty()) {
            // Who set it, so a later rekordbox pass leaves hand-made grids alone.
            // Without a downbeat the bar anchor is the first beat, same as before.
            db.prepare(QStringLiteral(
                "INSERT OR REPLACE INTO mixhouse_downbeats (track_id, downbeat_sec, confidence, detector) "
                "VALUES (:id, :sec, 1.0, :detector)"));
            db.bindValue(QStringLiteral(":sec"), edit.downbeatSec >= 0.0 ? edit.downbeatSec : edit.firstBeatSec);
            db.bindValue(QStringLiteral(":detector"), edit.detector);
        } else if (edit.downbeatSec >= 0.0) {
            db.prepare(QStringLiteral("UPDATE mixhouse_downbeats SET downbeat_sec = :sec WHERE track_id = :id"));
            db.bindValue(QStringLiteral(":sec"), edit.downbeatSec);
        } else {
            continue;
        }
        db.bindValue(QStringLiteral(":id"), edit.trackId);
        if (!db.exec()) {
            LOGW() << "setTrackBeatGrid: could not move downbeat: " << db.lastError().text();
        }
    }
    if (transaction) {
        m_writeDb.commit();
    }
    if (written > 0) {
        m_libraryChanged.notify();
    }
    return written;
}

std::optional<std::pair<double, double> > MixHouseLibrary::trackAnchors(const QString& location) const
{
    QSqlDatabase db = m_db;
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT intro_sec, outro_sec FROM mixhouse_anchors WHERE location = :location"));
    q.bindValue(QStringLiteral(":location"), location);
    if (!q.exec() || !q.next()) {
        return std::nullopt;
    }
    return std::make_pair(q.value(0).toDouble(), q.value(1).toDouble());
}

bool MixHouseLibrary::setTrackAnchors(const QString& location, double introSec, double outroSec)
{
    if (location.isEmpty() || !ensureWritable()) {
        return false;
    }
    QSqlQuery q(m_writeDb);
    if (introSec < 0.0) {
        q.prepare(QStringLiteral("DELETE FROM mixhouse_anchors WHERE location = :location"));
    } else {
        q.prepare(QStringLiteral(
            "INSERT OR REPLACE INTO mixhouse_anchors (location, intro_sec, outro_sec) VALUES (:location, :intro, :outro)"));
        q.bindValue(QStringLiteral(":intro"), introSec);
        q.bindValue(QStringLiteral(":outro"), outroSec);
    }
    q.bindValue(QStringLiteral(":location"), location);
    if (!q.exec()) {
        LOGW() << "setTrackAnchors: " << q.lastError().text();
        return false;
    }
    return true;
}

std::optional<std::vector<MixHousePhrase> > MixHouseLibrary::trackPhraseEdits(int trackId) const
{
    QSqlDatabase db = m_db;
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT phrases FROM mixhouse_phrases WHERE track_id = :id"));
    q.bindValue(QStringLiteral(":id"), trackId);
    if (!q.exec() || !q.next()) {
        return std::nullopt;   // no row, or the table doesn't exist yet
    }
    std::vector<MixHousePhrase> phrases;
    const QJsonArray array = QJsonDocument::fromJson(q.value(0).toByteArray()).array();
    for (const QJsonValue& value : array) {
        const QJsonObject o = value.toObject();
        MixHousePhrase phrase;
        phrase.startSec = o.value(QStringLiteral("s")).toDouble();
        phrase.endSec = o.value(QStringLiteral("e")).toDouble();
        phrase.fillStartSec = o.value(QStringLiteral("f")).toDouble(-1.0);
        phrase.label = o.value(QStringLiteral("l")).toString();
        const QString color = o.value(QStringLiteral("c")).toString();
        phrase.color = QColor::isValidColorName(color) ? QColor(color) : mixHousePhraseColor(phrase.label);
        if (phrase.endSec > phrase.startSec) {
            phrases.push_back(phrase);
        }
    }
    return phrases;
}

std::vector<MixHousePhrase> MixHouseLibrary::detectedPhrases(int trackId) const
{
    if (trackId < 0) {
        return {};
    }
    const auto cached = m_detectedPhrases.constFind(trackId);
    if (cached != m_detectedPhrases.constEnd()) {
        return *cached;
    }
    std::vector<MixHousePhrase> phrases;
    TrackQuery query;
    query.trackId = trackId;
    query.limit = 1;
    const MixHouseTrackList found = tracks(query);
    if (!found.empty()) {
        const MixHouseTrack& t = found.front();
        const double bpm = t.beats.valid && t.beats.bpm > 0.0 ? t.beats.bpm : t.bpm;
        const double downbeat = t.downbeatSec >= 0.0 ? t.downbeatSec : (t.beats.valid ? t.beats.firstBeatSec : 0.0);
        // The phrase model's, while the track keeps the grid they were made for.
        QSqlQuery model(m_db);
        model.prepare(QStringLiteral("SELECT bpm, downbeat_sec, phrases FROM mixhouse_model_phrases WHERE track_id = :id"));
        model.bindValue(QStringLiteral(":id"), trackId);
        if (model.exec() && model.next() && std::abs(model.value(0).toDouble() - bpm) < 1e-6
            && std::abs(model.value(1).toDouble() - downbeat) < 1e-4) {
            phrases = phrasesFromJson(model.value(2).toByteArray());
        }
        const QByteArray blob = phrases.empty() ? overviewAnalysisBlob(trackId) : QByteArray();
        if (phrases.empty() && bpm > 0.0 && t.durationSec > 0.0 && !blob.isEmpty()) {
            phrases = detectPhrases(decodeWaveformOverview(blob), t.durationSec, bpm, downbeat);
        }
    }
    m_detectedPhrases.insert(trackId, phrases);
    return phrases;
}

bool MixHouseLibrary::setTrackPhraseEdits(int trackId, const std::vector<MixHousePhrase>* phrases)
{
    if (trackId < 0 || !ensureWritable()) {
        return false;
    }
    QSqlQuery q(m_writeDb);
    if (!phrases) {
        q.prepare(QStringLiteral("DELETE FROM mixhouse_phrases WHERE track_id = :id"));
    } else {
        QJsonArray array;
        for (const MixHousePhrase& phrase : *phrases) {
            QJsonObject o;
            o.insert(QStringLiteral("s"), phrase.startSec);
            o.insert(QStringLiteral("e"), phrase.endSec);
            if (phrase.fillStartSec >= 0.0) {
                o.insert(QStringLiteral("f"), phrase.fillStartSec);
            }
            o.insert(QStringLiteral("l"), phrase.label);
            o.insert(QStringLiteral("c"), phrase.color.name());
            array.append(o);
        }
        q.prepare(QStringLiteral("INSERT OR REPLACE INTO mixhouse_phrases (track_id, phrases) VALUES (:id, :phrases)"));
        q.bindValue(QStringLiteral(":phrases"), QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact)));
    }
    q.bindValue(QStringLiteral(":id"), trackId);
    if (!q.exec()) {
        LOGW() << "setTrackPhraseEdits: " << q.lastError().text();
        return false;
    }
    m_libraryChanged.notify();
    return true;
}

bool MixHouseLibrary::setTrackBpmLocked(int trackId, bool locked)
{
    if (!ensureWritable()) {
        return false;
    }
    QSqlQuery upd(m_writeDb);
    upd.prepare(QStringLiteral("UPDATE library SET bpm_lock = :locked WHERE id = :id"));
    upd.bindValue(QStringLiteral(":locked"), locked ? 1 : 0);
    upd.bindValue(QStringLiteral(":id"), trackId);
    if (!upd.exec()) {
        LOGE() << "setTrackBpmLocked failed: " << upd.lastError().text();
        return false;
    }
    return true;
}

namespace {
/// mixxx::CueType (track/cueinfo.h), the `cues.type` column's real values.
enum class MixxxCueType { Invalid = 0, HotCue = 1, MainCue = 2, Loop = 4, Intro = 6, Outro = 7 };

bool deleteCuesOfType(QSqlDatabase& db, int trackId, MixxxCueType type)
{
    QSqlQuery del(db);
    del.prepare(QStringLiteral("DELETE FROM cues WHERE track_id = :id AND type = :type"));
    del.bindValue(QStringLiteral(":id"), trackId);
    del.bindValue(QStringLiteral(":type"), static_cast<int>(type));
    if (!del.exec()) {
        LOGE() << "clearTrackField (cues) failed: " << del.lastError().text();
        return false;
    }
    return true;
}
} // namespace

bool MixHouseLibrary::clearTrackField(int trackId, MixHouseClearField field)
{
    if (!ensureWritable()) {
        return false;
    }

    // Beats/Rating/Comment/Key/ReplayGain/PlayCount are plain library column
    // writes -- one UPDATE, matched against the real ResetXxxTrackPointerOperation
    // each field mirrors. Cue-shaped fields are DELETEs scoped by cues.type.
    // Waveform deletes the cached analysis mixxx's AnalysisDao owns.
    auto runUpdate = [this](const QString& sql) {
        QSqlQuery upd(m_writeDb);
        upd.prepare(sql);
        return upd;
    };

    switch (field) {
    case MixHouseClearField::Beats: {
        // Real mixxx greys out "Clear > BPM and Beatgrid" entirely while
        // bpm_lock is set (widget/wtrackmenu.cpp:1137-1153) -- the same
        // guard scaleTrackBpm() already has, missing here until now.
        QSqlQuery upd = runUpdate(QStringLiteral(
            "UPDATE library SET beats = NULL, beats_version = NULL, bpm = 0 "
            "WHERE id = :id AND (bpm_lock IS NULL OR bpm_lock = 0)"));
        upd.bindValue(QStringLiteral(":id"), trackId);
        if (!upd.exec()) { LOGE() << "clearTrackField (Beats) failed: " << upd.lastError().text(); return false; }
        // The downbeat is bar phase ON that grid -- goes with it (and stays,
        // like the grid, while bpm_lock is set).
        QSqlQuery delDownbeat = runUpdate(QStringLiteral(
            "DELETE FROM mixhouse_downbeats WHERE track_id = :id AND NOT EXISTS "
            "(SELECT 1 FROM library WHERE id = :id2 AND bpm_lock = 1)"));
        delDownbeat.bindValue(QStringLiteral(":id"), trackId);
        delDownbeat.bindValue(QStringLiteral(":id2"), trackId);
        if (!delDownbeat.exec()) { LOGW() << "clearTrackField (Beats) downbeat: " << delDownbeat.lastError().text(); }
        return true;
    }
    case MixHouseClearField::PlayCount: {
        QSqlQuery upd = runUpdate(QStringLiteral(
            "UPDATE library SET timesplayed = 0 WHERE id = :id"));
        upd.bindValue(QStringLiteral(":id"), trackId);
        if (!upd.exec()) { LOGE() << "clearTrackField (PlayCount) failed: " << upd.lastError().text(); return false; }
        return true;
    }
    case MixHouseClearField::Rating: {
        QSqlQuery upd = runUpdate(QStringLiteral(
            "UPDATE library SET rating = 0 WHERE id = :id"));
        upd.bindValue(QStringLiteral(":id"), trackId);
        if (!upd.exec()) { LOGE() << "clearTrackField (Rating) failed: " << upd.lastError().text(); return false; }
        return true;
    }
    case MixHouseClearField::Comment: {
        QSqlQuery upd = runUpdate(QStringLiteral(
            "UPDATE library SET comment = '' WHERE id = :id"));
        upd.bindValue(QStringLiteral(":id"), trackId);
        if (!upd.exec()) { LOGE() << "clearTrackField (Comment) failed: " << upd.lastError().text(); return false; }
        return true;
    }
    case MixHouseClearField::Key: {
        QSqlQuery upd = runUpdate(QStringLiteral(
            "UPDATE library SET key = '', key_id = 0 WHERE id = :id"));
        upd.bindValue(QStringLiteral(":id"), trackId);
        if (!upd.exec()) { LOGE() << "clearTrackField (Key) failed: " << upd.lastError().text(); return false; }
        return true;
    }
    case MixHouseClearField::ReplayGain: {
        QSqlQuery upd = runUpdate(QStringLiteral(
            "UPDATE library SET replaygain = 0 WHERE id = :id"));
        upd.bindValue(QStringLiteral(":id"), trackId);
        if (!upd.exec()) { LOGE() << "clearTrackField (ReplayGain) failed: " << upd.lastError().text(); return false; }
        return true;
    }
    case MixHouseClearField::Waveform: {
        // Real mixxx AnalysisDao::deleteAnalysesForTrack -> deleteAnalysis:
        // each analysis row's data file (analysis/<id>) is removed with it.
        // Deleting only the rows used to leave every file orphaned on disk.
        const QString analysisDir = QFileInfo(m_path).absolutePath() + QStringLiteral("/analysis");
        QSqlQuery ids(m_writeDb);
        ids.prepare(QStringLiteral("SELECT id FROM track_analysis WHERE track_id = :id"));
        ids.bindValue(QStringLiteral(":id"), trackId);
        if (!ids.exec()) { LOGE() << "clearTrackField (Waveform) failed: " << ids.lastError().text(); return false; }
        while (ids.next()) {
            QFile::remove(analysisDir + QStringLiteral("/") + ids.value(0).toString());
        }
        QSqlQuery del(m_writeDb);
        del.prepare(QStringLiteral("DELETE FROM track_analysis WHERE track_id = :id"));
        del.bindValue(QStringLiteral(":id"), trackId);
        if (!del.exec()) { LOGE() << "clearTrackField (Waveform) failed: " << del.lastError().text(); return false; }
        return true;
    }
    case MixHouseClearField::MainCue:
        return deleteCuesOfType(m_writeDb, trackId, MixxxCueType::MainCue);
    case MixHouseClearField::HotCues:
        return deleteCuesOfType(m_writeDb, trackId, MixxxCueType::HotCue);
    case MixHouseClearField::IntroCue:
        return deleteCuesOfType(m_writeDb, trackId, MixxxCueType::Intro);
    case MixHouseClearField::OutroCue:
        return deleteCuesOfType(m_writeDb, trackId, MixxxCueType::Outro);
    case MixHouseClearField::Loops:
        return deleteCuesOfType(m_writeDb, trackId, MixxxCueType::Loop);
    case MixHouseClearField::All: {
        // ClearAllPerformanceMetadataTrackPointerOperation, exactly: every
        // field above EXCEPT Comment.
        const bool ok =
            clearTrackField(trackId, MixHouseClearField::Beats) &&
            clearTrackField(trackId, MixHouseClearField::PlayCount) &&
            clearTrackField(trackId, MixHouseClearField::Rating) &&
            clearTrackField(trackId, MixHouseClearField::MainCue) &&
            clearTrackField(trackId, MixHouseClearField::HotCues) &&
            clearTrackField(trackId, MixHouseClearField::IntroCue) &&
            clearTrackField(trackId, MixHouseClearField::OutroCue) &&
            clearTrackField(trackId, MixHouseClearField::Loops) &&
            clearTrackField(trackId, MixHouseClearField::Key) &&
            clearTrackField(trackId, MixHouseClearField::ReplayGain) &&
            clearTrackField(trackId, MixHouseClearField::Waveform);
        return ok;
    }
    }
    return false;
}

bool MixHouseLibrary::purgeTrack(int trackId)
{
    if (!ensureWritable()) {
        return false;
    }
    // TrackDAO::onPurgingTracks. Real mixxx relies on the schema's own
    // foreign-key cascades for the membership/cues/analysis rows; done
    // explicitly here instead since this write connection doesn't turn
    // foreign_keys on, and a purge should never leave orphaned rows behind
    // regardless.
    const QStringList cleanup = {
        QStringLiteral("DELETE FROM PlaylistTracks WHERE track_id = :id"),
        QStringLiteral("DELETE FROM crate_tracks WHERE track_id = :id"),
        QStringLiteral("DELETE FROM cues WHERE track_id = :id"),
        QStringLiteral("DELETE FROM track_analysis WHERE track_id = :id"),
        QStringLiteral("DELETE FROM mixhouse_downbeats WHERE track_id = :id"),
        QStringLiteral("DELETE FROM mixhouse_model_phrases WHERE track_id = :id"),
    };
    for (const QString& sql : cleanup) {
        QSqlQuery del(m_writeDb);
        del.prepare(sql);
        del.bindValue(QStringLiteral(":id"), trackId);
        if (!del.exec()) {
            LOGE() << "purgeTrack (cleanup) failed: " << del.lastError().text();
            return false;
        }
    }
    QSqlQuery delLoc(m_writeDb);
    delLoc.prepare(QStringLiteral(
        "DELETE FROM track_locations WHERE id = "
        "(SELECT location FROM library WHERE id = :id)"));
    delLoc.bindValue(QStringLiteral(":id"), trackId);
    if (!delLoc.exec()) {
        LOGE() << "purgeTrack (track_locations) failed: " << delLoc.lastError().text();
        return false;
    }
    QSqlQuery delLib(m_writeDb);
    delLib.prepare(QStringLiteral("DELETE FROM library WHERE id = :id"));
    delLib.bindValue(QStringLiteral(":id"), trackId);
    if (!delLib.exec()) {
        LOGE() << "purgeTrack (library) failed: " << delLib.lastError().text();
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Batch versions of the above, for the track menu's "Collection Actions"
// submenu -- one call covering a whole multi-row selection instead of one
// call per row. Every id here is a real library track id the caller already
// resolved from its rows before showing any confirmation dialog.

QString MixHouseLibrary::sqlInClause(const QList<int>& ids)
{
    QStringList parts;
    parts.reserve(ids.size());
    for (int id : ids) {
        parts << QString::number(id);
    }
    return parts.join(QStringLiteral(","));
}

bool MixHouseLibrary::removeTracksFromPlaylist(int playlistId, const QList<int>& positions)
{
    if (positions.isEmpty() || !ensureWritable() || isPlaylistLocked(playlistId)) {
        return false;
    }
    // PlaylistDAO::removeTracksFromPlaylistInner: delete by POSITION, not
    // track_id -- a real mixxx playlist is an ordered LIST, the same track
    // can legitimately appear in it more than once, and deleting by
    // track_id would remove every occurrence at once instead of just the
    // one the user actually selected. Positions are left with gaps -- real
    // mixxx's own removal doesn't renumber them either, and nothing in this
    // app reads `position` as anything but an ORDER BY key.
    QSqlQuery del(m_writeDb);
    del.prepare(QStringLiteral(
        "DELETE FROM PlaylistTracks WHERE playlist_id = :pl AND position IN (%1)")
        .arg(sqlInClause(positions)));
    del.bindValue(QStringLiteral(":pl"), playlistId);
    if (!del.exec()) {
        LOGE() << "removeTracksFromPlaylist failed: " << del.lastError().text();
        return false;
    }
    return true;
}

bool MixHouseLibrary::removeTracksFromCrate(int crateId, const QList<int>& trackIds)
{
    if (trackIds.isEmpty() || !ensureWritable() || isCrateLocked(crateId)) {
        return false;
    }
    // CrateStorage's removal counterpart to onAddingCrateTracks/addTrackToCrate.
    QSqlQuery del(m_writeDb);
    del.prepare(QStringLiteral(
        "DELETE FROM crate_tracks WHERE crate_id = :cr AND track_id IN (%1)")
        .arg(sqlInClause(trackIds)));
    del.bindValue(QStringLiteral(":cr"), crateId);
    if (!del.exec()) {
        LOGE() << "removeTracksFromCrate failed: " << del.lastError().text();
        return false;
    }
    return true;
}

bool MixHouseLibrary::setTracksHidden(const QList<int>& trackIds, bool hidden)
{
    if (trackIds.isEmpty() || !ensureWritable()) {
        return false;
    }
    // TrackDAO::hideTracks/unhideTracks, batched: one UPDATE ... IN (...)
    // instead of one statement per id.
    QSqlQuery upd(m_writeDb);
    if (hidden) {
        upd.prepare(QStringLiteral("UPDATE library SET mixxx_deleted = 1 WHERE id IN (%1)")
            .arg(sqlInClause(trackIds)));
    } else {
        upd.prepare(QStringLiteral(
            "UPDATE library SET mixxx_deleted = 0 WHERE mixxx_deleted != 0 AND id IN (%1)")
            .arg(sqlInClause(trackIds)));
    }
    if (!upd.exec()) {
        LOGE() << "setTracksHidden failed: " << upd.lastError().text();
        return false;
    }
    return true;
}

bool MixHouseLibrary::purgeTracks(const QList<int>& trackIds)
{
    if (trackIds.isEmpty() || !ensureWritable()) {
        return false;
    }
    // TrackDAO::onPurgingTracks, batched -- the same explicit cleanup
    // purgeTrack() runs per id (see its own comment above), one IN (...)
    // statement per table instead of one DELETE per track.
    const QString idList = sqlInClause(trackIds);
    const QStringList cleanup = {
        QStringLiteral("DELETE FROM PlaylistTracks WHERE track_id IN (%1)").arg(idList),
        QStringLiteral("DELETE FROM crate_tracks WHERE track_id IN (%1)").arg(idList),
        QStringLiteral("DELETE FROM cues WHERE track_id IN (%1)").arg(idList),
        QStringLiteral("DELETE FROM track_analysis WHERE track_id IN (%1)").arg(idList),
        QStringLiteral("DELETE FROM mixhouse_downbeats WHERE track_id IN (%1)").arg(idList),
        QStringLiteral("DELETE FROM mixhouse_model_phrases WHERE track_id IN (%1)").arg(idList),
    };
    for (const QString& sql : cleanup) {
        QSqlQuery del(m_writeDb);
        if (!del.exec(sql)) {
            LOGE() << "purgeTracks (cleanup) failed: " << del.lastError().text();
            return false;
        }
    }
    QSqlQuery delLoc(m_writeDb);
    if (!delLoc.exec(QStringLiteral(
            "DELETE FROM track_locations WHERE id IN "
            "(SELECT location FROM library WHERE id IN (%1))").arg(idList))) {
        LOGE() << "purgeTracks (track_locations) failed: " << delLoc.lastError().text();
        return false;
    }
    QSqlQuery delLib(m_writeDb);
    if (!delLib.exec(QStringLiteral("DELETE FROM library WHERE id IN (%1)").arg(idList))) {
        LOGE() << "purgeTracks (library) failed: " << delLib.lastError().text();
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Roadmap Phase 5: the library table's "Waveform Overview" column. Real
// mixxx keeps this data outside mixxxdb.sqlite entirely (AnalysisDao,
// library/dao/analysisdao.cpp) -- one file per track_analysis row, named by
// that row's own numeric id, stored as a `qCompress()`-wrapped blob (Qt's own
// 4-byte length header + zlib deflate) under an "analysis" directory sitting
// beside the database itself. mixhousewaveform.h then decodes the protobuf
// bytes this returns into the actual signal_all/signal_filtered samples.

QByteArray MixHouseLibrary::overviewAnalysisBlob(int trackId) const
{
    if (!ensureOpen()) {
        return QByteArray();
    }

    // AnalysisDao::AnalysisType::TYPE_WAVESUMMARY = 2 -- the small,
    // whole-track-spanning cache the library table's Overview column reads
    // (library/overviewcache.cpp). TYPE_WAVEFORM = 1 is the full-resolution
    // per-track waveform the deck's own player uses; never read here. A
    // track can in principle have more than one WAVESUMMARY row (re-analysis
    // over time) -- take the newest, matching how a fresh analysis is meant
    // to supersede an older one.
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT id FROM track_analysis WHERE track_id = :id AND type = 2 ORDER BY id DESC LIMIT 1"));
    q.bindValue(QStringLiteral(":id"), trackId);
    if (!q.exec() || !q.next()) {
        return QByteArray();   // not analyzed (yet) -- the ordinary, expected case
    }
    const int analysisId = q.value(0).toInt();

    // getAnalysisStoragePath(): "<settings path>/analysis/" -- a sibling of
    // mixxxdb.sqlite itself, so this is derived from the already-open
    // database's own path rather than hardcoded, and works on any machine.
    const QString analysisDir = QFileInfo(m_path).absolutePath() + QStringLiteral("/analysis");
    QFile file(analysisDir + QStringLiteral("/") + QString::number(analysisId));
    if (!file.open(QIODevice::ReadOnly)) {
        return QByteArray();   // row exists but the file is missing -- treat as "no data"
    }
    const QByteArray compressed = file.readAll();
    file.close();

    // Real mixxx also verifies `data_checksum` before trusting this file
    // (analysisdao.cpp); skipped here since its exact on-disk value format
    // wasn't independently confirmed this session (see
    // [[mixhouse-waveform-overview-research]]) and qUncompress() already
    // provides its own real corruption check -- it returns an empty
    // QByteArray on malformed zlib data rather than garbage, so a bad file
    // fails the exact same safe way either way.
    return qUncompress(compressed);
}

bool MixHouseLibrary::coverInfoForTrack(int trackId, CoverInfo& out) const
{
    if (!ensureOpen()) {
        return false;
    }

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT t.location, l.coverart_type, l.coverart_location "
        "FROM library l JOIN track_locations t ON t.id = l.location WHERE l.id = :id"));
    q.bindValue(QStringLiteral(":id"), trackId);
    if (!q.exec() || !q.next()) {
        return false;
    }
    out.filePath = q.value(0).toString();
    out.type = q.value(1).toInt();
    out.location = q.value(2).toString();
    return true;
}

// ---------------------------------------------------------------------------
// Roadmap Phase 7: the real analysis engine. mixhouseanalyzer.h does the
// actual DSP (a port of real mixxx's own AnalyzerQueenMaryBeats/Key); this
// just resolves the file, calls it, and writes the result back using the
// exact real `library` column set TrackDAO::bindTrackLibraryValues() writes.

namespace {
// The real proto/keys.proto ChromaticKey enum's own symbolic names, just
// reformatted to display text -- not a separate guess at spelling. Index 0
// (INVALID) is unused; 1-12 major, 13-24 minor, matching the enum exactly.
// Currently unused (analyzeTrack() below writes kKeyIdToCamelotText instead,
// see that table's own comment near the top of this file) -- left in place
// rather than deleted in case a Traditional-notation display preference is
// ever added; some DJs do prefer it over Camelot.
const char* const kKeyIdToText[25] = {
    "",
    "C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B",
    "Cm", "C#m", "Dm", "Ebm", "Em", "Fm", "F#m", "Gm", "G#m", "Am", "Bbm", "Bm"
};
}

// Shared by MixHouseLibrary::analyzeTrack() (main thread, m_writeDb) and
// MixHouseAnalyzerWorker::analyzeTrack() (each background worker's own
// dedicated connection) -- see mixhouseanalysiswriter.h's own comment for
// why this is a real shared free function rather than a duplicated one.
// Defined here (not inside the anonymous namespace above, which would give
// it internal linkage and break the OTHER file's caller at link time) but
// still able to see kKeyIdToCamelotText: an anonymous namespace's names are
// injected into this enclosing (au::mixhouse) scope for the rest of the
// translation unit, exactly like a `using namespace`.
MixHouseTrackFileInfo au::mixhouse::lookupTrackFileForAnalysis(QSqlDatabase& db, int trackId)
{
    MixHouseTrackFileInfo out;
    QSqlQuery find(db);
    find.prepare(QStringLiteral(
        "SELECT t.location, l.bpm_lock FROM library l JOIN track_locations t "
        "ON t.id = l.location WHERE l.id = :id"));
    find.bindValue(QStringLiteral(":id"), trackId);
    if (!find.exec() || !find.next()) {
        out.error = QStringLiteral("Track not found.");
        return out;
    }
    out.filePath = find.value(0).toString();
    out.bpmLocked = find.value(1).toInt() != 0;
    if (out.filePath.isEmpty()) {
        out.error = QStringLiteral("Track has no file location.");
        return out;
    }
    out.valid = true;
    return out;
}

MixHouseAnalyzeResult au::mixhouse::writeAnalysisResult(const QString& libraryPath, QSqlDatabase& db, int trackId,
                                                        bool bpmLocked, const MixHouseAnalysisResult& analysis)
{
    MixHouseAnalyzeResult result;
    result.valid = true;
    result.bpmSkippedLocked = bpmLocked;
    result.keyId = analysis.keyId;
    result.key = (analysis.keyId >= 1 && analysis.keyId <= 24)
        ? QString::fromLatin1(kKeyIdToCamelotText[analysis.keyId])
        : QString();

    QSqlQuery upd(db);
    // Real mixxx's AnalyzerBeats::shouldAnalyze()/isAnalyzerFeatureEnabled()
    // (analyzer/analyzerbeats.cpp:65-69,142-147) refuse to run the beat/BPM
    // analyzer at all on a BPM-locked track -- this app's own analyzer
    // engine doesn't know about the lock at the DSP level, so it's enforced
    // here at the write-back instead, with the exact same effect: a locked
    // track's bpm/beats are left completely alone. Key detection is a
    // separate real analyzer with no such lock (real mixxx's AnalyzerKey),
    // so it's still written either way.
    if (!bpmLocked && analysis.bpm > 0.0) {
        result.bpm = analysis.bpm;
        upd.prepare(QStringLiteral(
            "UPDATE library SET bpm = :bpm, beats = :beats, beats_version = :version, "
            "key = :key, key_id = :key_id, duration = :duration, "
            "samplerate = CASE WHEN samplerate IS NULL OR samplerate <= 0 THEN :samplerate ELSE samplerate END "
            "WHERE id = :id"));
        // The beatgrid's frame positions are at this rate; readers need it.
        upd.bindValue(QStringLiteral(":samplerate"), analysis.sampleRate);
        upd.bindValue(QStringLiteral(":bpm"), analysis.bpm);
        upd.bindValue(QStringLiteral(":beats"),
                      encodeBeatGrid(analysis.bpm, analysis.firstBeatSec, analysis.sampleRate));
        upd.bindValue(QStringLiteral(":version"), QStringLiteral("BeatGrid-2.0"));
    } else {
        // Either BPM-locked, or no beat detected (near-silent/spoken audio,
        // etc.) -- leave bpm/beats alone rather than overwrite a real value;
        // still record the key if one was found.
        upd.prepare(QStringLiteral(
            "UPDATE library SET key = :key, key_id = :key_id, duration = :duration WHERE id = :id"));
    }
    const bool writeDownbeat = !bpmLocked;   // a locked grid keeps its bar phase too
    upd.bindValue(QStringLiteral(":key"), result.key);
    upd.bindValue(QStringLiteral(":key_id"), analysis.keyId);
    upd.bindValue(QStringLiteral(":duration"), analysis.durationSec);
    upd.bindValue(QStringLiteral(":id"), trackId);
    if (!upd.exec()) {
        result.valid = false;
        result.error = QStringLiteral("Could not save analysis: %1").arg(upd.lastError().text());
        return result;
    }

    // Waveform Overview -- signal_all plus the real 3-band split (see
    // MixHouseAnalysisResult::waveformOverview/Low/Mid/High and
    // WaveformBandCollector's own comment). Not fatal if this part fails --
    // the track's real bpm/key are already saved above regardless.
    if (writeDownbeat) {
        QSqlQuery createTable(db);
        createTable.exec(QString::fromLatin1(kCreateDownbeatTableSql));
        QSqlQuery downbeat(db);
        if (analysis.downbeatSec >= 0.0) {
            downbeat.prepare(QStringLiteral(
                "INSERT OR REPLACE INTO mixhouse_downbeats (track_id, downbeat_sec, confidence, detector) "
                "VALUES (:id, :sec, :confidence, :detector)"));
            downbeat.bindValue(QStringLiteral(":detector"), analysis.downbeatDetector);
            downbeat.bindValue(QStringLiteral(":sec"), analysis.downbeatSec);
            downbeat.bindValue(QStringLiteral(":confidence"), analysis.downbeatConfidence);
        } else {
            // Re-analysis found none: don't leave an older downbeat pointing
            // at a grid that was just rewritten.
            downbeat.prepare(QStringLiteral("DELETE FROM mixhouse_downbeats WHERE track_id = :id"));
        }
        downbeat.bindValue(QStringLiteral(":id"), trackId);
        if (!downbeat.exec()) {
            LOGW() << "writeAnalysisResult: could not save downbeat for track " << trackId
                   << ": " << downbeat.lastError().text();
        }
    }

    if (analysis.energy > 0.0) {
        QSqlQuery createTable(db);
        createTable.exec(QString::fromLatin1(kCreateEnergyTableSql));
        QSqlQuery energy(db);
        energy.prepare(QStringLiteral("INSERT OR REPLACE INTO mixhouse_energy (track_id, energy) VALUES (:id, :energy)"));
        energy.bindValue(QStringLiteral(":id"), trackId);
        energy.bindValue(QStringLiteral(":energy"), analysis.energy);
        if (!energy.exec()) {
            LOGW() << "writeAnalysisResult: could not save energy for track " << trackId << ": "
                   << energy.lastError().text();
        }
    }

    {
        // The phrase model's phrases, with the grid they belong to.
        QSqlQuery createTable(db);
        createTable.exec(QString::fromLatin1(kCreateModelPhraseTableSql));
        QSqlQuery model(db);
        if (!analysis.modelPhrases.empty()) {
            model.prepare(QStringLiteral(
                "INSERT OR REPLACE INTO mixhouse_model_phrases (track_id, bpm, downbeat_sec, phrases) "
                "VALUES (:id, :bpm, :downbeat, :phrases)"));
            model.bindValue(QStringLiteral(":bpm"), analysis.bpm);
            model.bindValue(QStringLiteral(":downbeat"),
                            analysis.downbeatSec >= 0.0 ? analysis.downbeatSec : analysis.firstBeatSec);
            model.bindValue(QStringLiteral(":phrases"), phrasesToJson(analysis.modelPhrases));
        } else {
            model.prepare(QStringLiteral("DELETE FROM mixhouse_model_phrases WHERE track_id = :id"));
        }
        model.bindValue(QStringLiteral(":id"), trackId);
        if (!model.exec()) {
            LOGW() << "writeAnalysisResult: could not save model phrases for track " << trackId
                   << ": " << model.lastError().text();
        }
    }

    if (!analysis.waveformOverview.empty()) {
        // Real mixxx's own "a fresh analysis supersedes an older one" rule
        // (overviewAnalysisBlob()'s comment) means a stale row would just
        // never be read again -- but its file would sit around forever on
        // every re-analyze, so clean up any prior TYPE_WAVESUMMARY row (and
        // file) for this track first.
        const QString analysisDir = QFileInfo(libraryPath).absolutePath() + QStringLiteral("/analysis");
        QSqlQuery findOld(db);
        findOld.prepare(QStringLiteral(
            "SELECT id FROM track_analysis WHERE track_id = :id AND type = 2"));
        findOld.bindValue(QStringLiteral(":id"), trackId);
        if (findOld.exec()) {
            while (findOld.next()) {
                QFile::remove(analysisDir + QStringLiteral("/") + findOld.value(0).toString());
            }
        }
        QSqlQuery delOld(db);
        delOld.prepare(QStringLiteral("DELETE FROM track_analysis WHERE track_id = :id AND type = 2"));
        delOld.bindValue(QStringLiteral(":id"), trackId);
        delOld.exec();

        QSqlQuery insAnalysis(db);
        insAnalysis.prepare(QStringLiteral(
            "INSERT INTO track_analysis (track_id, type, description, version, data_checksum) "
            "VALUES (:track_id, 2, :description, :version, '')"));
        insAnalysis.bindValue(QStringLiteral(":track_id"), trackId);
        insAnalysis.bindValue(QStringLiteral(":description"), QStringLiteral("waveform-summary"));
        insAnalysis.bindValue(QStringLiteral(":version"), QStringLiteral("MixHouseRGB-1.0"));
        if (insAnalysis.exec()) {
            const int analysisId = insAnalysis.lastInsertId().toInt();
            QDir().mkpath(analysisDir);
            QFile file(analysisDir + QStringLiteral("/") + QString::number(analysisId));
            if (file.open(QIODevice::WriteOnly)) {
                file.write(encodeWaveformOverview(analysis.waveformOverview,
                                                   analysis.waveformLow, analysis.waveformMid,
                                                   analysis.waveformHigh));
                file.close();
            } else {
                LOGW() << "writeAnalysisResult: could not write waveform overview file for track " << trackId;
            }
        } else {
            LOGW() << "writeAnalysisResult: could not insert track_analysis row for track " << trackId
                   << ": " << insAnalysis.lastError().text();
        }
    }

    return result;
}

MixHouseAnalyzeResult au::mixhouse::analyzeTrackAndWrite(const QString& libraryPath, QSqlDatabase& db, int trackId)
{
    MixHouseAnalyzeResult result;
    const MixHouseTrackFileInfo lookup = lookupTrackFileForAnalysis(db, trackId);
    if (!lookup.valid) {
        result.error = lookup.error;
        return result;
    }
    const MixHouseAnalysisResult analysis = analyzeTrackFile(lookup.filePath);
    if (!analysis.valid) {
        result.error = analysis.error.isEmpty()
            ? QStringLiteral("Analysis failed.")
            : analysis.error;
        return result;
    }
    return writeAnalysisResult(libraryPath, db, trackId, lookup.bpmLocked, analysis);
}

MixHouseAnalyzeResult MixHouseLibrary::analyzeTrack(int trackId)
{
    if (!ensureWritable()) {
        MixHouseAnalyzeResult result;
        result.error = QStringLiteral("Could not open the library for writing.");
        return result;
    }
    return analyzeTrackAndWrite(m_path, m_writeDb, trackId);
}
