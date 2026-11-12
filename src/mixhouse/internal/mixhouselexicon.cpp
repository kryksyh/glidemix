/*
 * GlideMix
 */
#include "mixhouselexicon.h"
#include "mixhouse/glidemixpaths.h"
#include "mixhouse/mixhousesearchquery.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include "mixhouse/imixhousekeymatch.h"
#include "mixhouse/imixhouseexternallibrary.h"

#include "log.h"

using namespace au::mixhouse;

namespace {
const QString kConnectionName = QStringLiteral("mixtape_lexicon");

//! Lexicon's cue colour names.
QColor lexiconCueColor(const QString& name)
{
    static const QHash<QString, QColor> colors = {
        { QStringLiteral("red"), QColor(0xE6, 0x2E, 0x2E) },
        { QStringLiteral("orange"), QColor(0xFF, 0x8C, 0x1A) },
        { QStringLiteral("yellow"), QColor(0xF2, 0xD0, 0x1C) },
        { QStringLiteral("green"), QColor(0x3C, 0xC8, 0x4A) },
        { QStringLiteral("cyan"), QColor(0x1C, 0xC8, 0xE6) },
        { QStringLiteral("aqua"), QColor(0x1C, 0xC8, 0xE6) },
        { QStringLiteral("blue"), QColor(0x30, 0x6C, 0xF0) },
        { QStringLiteral("violet"), QColor(0x9A, 0x4C, 0xF0) },
        { QStringLiteral("purple"), QColor(0x9A, 0x4C, 0xF0) },
        { QStringLiteral("magenta"), QColor(0xE6, 0x3C, 0xC8) },
        { QStringLiteral("pink"), QColor(0xF0, 0x6C, 0xB4) },
        { QStringLiteral("white"), QColor(0xF0, 0xF0, 0xF0) },
    };
    return colors.value(name.toLower(), QColor(0xE6, 0x2E, 0x2E));
}

QString orderColumn(IMixHouseLibrary::SortKey key)
{
    using K = IMixHouseLibrary::SortKey;
    switch (key) {
    case K::Title: return QStringLiteral("t.title COLLATE NOCASE");
    case K::Artist: return QStringLiteral("t.artist COLLATE NOCASE");
    case K::Album: return QStringLiteral("t.albumTitle COLLATE NOCASE");
    case K::Genre: return QStringLiteral("t.genre COLLATE NOCASE");
    case K::Year: return QStringLiteral("t.year");
    case K::Bpm: return QStringLiteral("t.bpm");
    case K::Key: return QStringLiteral("t.key");
    case K::Duration: return QStringLiteral("t.duration");
    case K::Rating: return QStringLiteral("t.rating");
    case K::DateAdded: return QStringLiteral("t.dateAdded");
    case K::Composer: return QStringLiteral("t.composer COLLATE NOCASE");
    case K::Grouping: return QStringLiteral("t.grouping COLLATE NOCASE");
    case K::TrackNumber: return QStringLiteral("t.trackNumber");
    case K::Comment: return QStringLiteral("t.comment COLLATE NOCASE");
    case K::Bitrate: return QStringLiteral("t.bitrate");
    case K::TimesPlayed: return QStringLiteral("t.playCount");
    case K::LastPlayedAt: return QStringLiteral("t.lastPlayed");
    case K::SampleRate: return QStringLiteral("t.sampleRate");
    case K::Energy: return QStringLiteral("t.energy");
    default: return QString();
    }
}
}

MixHouseLexicon::MixHouseLexicon() = default;

MixHouseLexicon::~MixHouseLexicon()
{
    if (m_db.isValid()) {
        m_db.close();
    }
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(kConnectionName);
}

QString MixHouseLexicon::databasePath()
{
    // Lexicon is an Electron app: its data folder is the platform's per-user
    // app data folder + "lexicon" (%APPDATA%\lexicon on Windows).
    return paths::userAppDataRoot() + QStringLiteral("/lexicon/main.db");
}

bool MixHouseLexicon::open() const
{
    if (libraryConfiguration() && !libraryConfiguration()->useLexiconLibrary()) {
        return false;
    }
    if (m_db.isValid() && m_db.isOpen()) {
        return true;
    }
    if (!QFileInfo::exists(databasePath())) {
        return false;
    }
    // Read-only: Lexicon owns this database (and usually has it open).
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), kConnectionName);
    m_db.setDatabaseName(databasePath());
    m_db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=2000"));
    if (!m_db.open()) {
        LOGW() << "Lexicon database: " << m_db.lastError().text();
        return false;
    }
    return true;
}

bool MixHouseLexicon::available() const
{
    return open();
}

std::vector<IMixHouseExternalLibrary::Node> MixHouseLexicon::nodes() const
{
    std::vector<Node> result;
    if (!open()) {
        return result;
    }
    QSqlQuery q(m_db);
    // type 1 = folder, 2 = playlist, 3 = smart list; the tree hangs off ROOT.
    const bool ok = q.exec(QStringLiteral(
        "SELECT p.id, p.name, p.type, p.parentId, "
        "  (SELECT COUNT(*) FROM LinkTrackPlaylist l JOIN Track t ON t.id = l.trackId "
        "     WHERE l.playlistId = p.id AND t.archived = 0), "
        "  (SELECT COUNT(*) FROM Playlist c WHERE c.parentId = p.id), "
        "  (SELECT id FROM Playlist WHERE parentId IS NULL LIMIT 1) "
        "FROM Playlist p WHERE p.parentId IS NOT NULL ORDER BY p.parentId, p.position"));
    if (!ok) {
        LOGW() << "Lexicon playlists: " << q.lastError().text();
        return result;
    }
    while (q.next()) {
        Node n;
        n.id = q.value(0).toInt();
        n.name = q.value(1).toString();
        const int type = q.value(2).toInt();
        n.type = type == 1 ? NodeType::Folder : type == 3 ? NodeType::SmartList : NodeType::Playlist;
        const int rootId = q.value(6).toInt();
        n.parentId = q.value(3).toInt() == rootId ? -1 : q.value(3).toInt();
        n.trackCount = n.type == NodeType::Folder ? q.value(5).toInt() : q.value(4).toInt();
        result.push_back(n);
    }
    return result;
}

int MixHouseLexicon::trackCount() const
{
    if (!open()) {
        return 0;
    }
    QSqlQuery q(m_db);
    return q.exec(QStringLiteral("SELECT COUNT(*) FROM Track WHERE archived = 0")) && q.next() ? q.value(0).toInt() : 0;
}

MixHouseTrackList MixHouseLexicon::tracks(int playlistId, const IMixHouseLibrary::TrackQuery& query) const
{
    // One file lookups (a dropped file, a clip's name) go straight to SQL.
    if (!query.location.isEmpty() || !query.fileBaseName.isEmpty()) {
        return readTracks(playlistId, query);
    }
    // GlideMix (fast library): the whole list once (~0.5 s for 37k songs),
    // then every sort and search from memory. Lexicon writing its database
    // changes the file's time stamp, and the lists are read again.
    const QString db = databasePath();
    const QString stamp = QFileInfo(db).lastModified().toString(Qt::ISODateWithMs) + QLatin1Char('|')
                          + QFileInfo(db + QStringLiteral("-wal")).lastModified().toString(Qt::ISODateWithMs);
    if (stamp != m_listCacheStamp) {
        m_listCache.clear();
        m_listCacheStamp = stamp;
    }
    auto it = m_listCache.find(playlistId);
    if (it == m_listCache.end()) {
        IMixHouseLibrary::TrackQuery all;
        all.limit = 0;
        all.sort = IMixHouseLibrary::SortKey::Position;   // the SQL default order
        it = m_listCache.insert(playlistId, readTracks(playlistId, all));
    }
    MixHouseTrackList result;
    result.reserve(it->size());
    for (const MixHouseTrack& t : *it) {
        if (!query.filter.isEmpty() && !t.title.contains(query.filter, Qt::CaseInsensitive)
            && !t.artist.contains(query.filter, Qt::CaseInsensitive)
            && !t.album.contains(query.filter, Qt::CaseInsensitive)) {
            continue;
        }
        if (!query.keyFilters.isEmpty() && !query.keyFilters.contains(t.key)) {
            continue;
        }
        result.push_back(t);
    }
    sortAndLimitTracks(result, query, playlistId >= 0);
    return result;
}

MixHouseTrackList MixHouseLexicon::readTracks(int playlistId, const IMixHouseLibrary::TrackQuery& query) const
{
    MixHouseTrackList result;
    if (!open()) {
        return result;
    }
    const bool inPlaylist = playlistId >= 0;
    QString sql = QStringLiteral(
        "SELECT t.id, t.title, t.artist, t.albumTitle, t.genre, t.year, t.location, t.duration, t.bpm, t.key, "
        "  t.rating, t.composer, t.grouping, t.trackNumber, t.comment, t.bitrate, t.dateAdded, t.playCount, "
        "  t.lastPlayed, t.sampleRate, %1, "
        "  (SELECT m.startTime FROM Tempomarker m WHERE m.trackId = t.id ORDER BY m.startTime LIMIT 1), "
        "  (SELECT m.bpm FROM Tempomarker m WHERE m.trackId = t.id ORDER BY m.startTime LIMIT 1), "
        "  (SELECT COUNT(*) FROM Tempomarker m WHERE m.trackId = t.id), t.energy, "
        "  t.remixer, t.mix, t.label, t.producer, t.lyricist, t.extra1, t.extra2, t.dateModified, t.sizeBytes, "
        "  t.danceability, t.popularity, t.happiness "
        "FROM %2 WHERE t.archived = 0")
                  .arg(inPlaylist ? QStringLiteral("l.position") : QStringLiteral("-1"),
                       inPlaylist ? QStringLiteral("LinkTrackPlaylist l JOIN Track t ON t.id = l.trackId")
                                  : QStringLiteral("Track t"));
    if (inPlaylist) {
        sql += QStringLiteral(" AND l.playlistId = :playlist");
    }
    if (!query.filter.isEmpty()) {
        sql += QStringLiteral(" AND (t.title LIKE :filter OR t.artist LIKE :filter OR t.albumTitle LIKE :filter)");
    }
    // A clip is named after its file; a dropped file arrives as a path.
    if (!query.location.isEmpty()) {
        sql += QStringLiteral(" AND t.location = :location");
    }
    if (!query.fileBaseName.isEmpty()) {
        sql += QStringLiteral(" AND t.location LIKE :baseName ESCAPE '\\'");
    }
    if (!query.keyFilters.isEmpty()) {
        QStringList placeholders;
        for (int i = 0; i < query.keyFilters.size(); ++i) {
            placeholders << QStringLiteral(":key%1").arg(i);
        }
        sql += QStringLiteral(" AND t.key IN (%1)").arg(placeholders.join(u','));
    }
    QString order = orderColumn(query.sort);
    if (query.sort == IMixHouseLibrary::SortKey::Position || order.isEmpty()) {
        order = inPlaylist ? QStringLiteral("l.position") : QStringLiteral("t.dateAdded");
    }
    sql += QStringLiteral(" ORDER BY %1 %2").arg(order, query.ascending ? QStringLiteral("ASC") : QStringLiteral("DESC"));
    if (query.limit > 0) {
        sql += QStringLiteral(" LIMIT %1").arg(query.limit);
    }

    QSqlQuery q(m_db);
    q.prepare(sql);
    if (inPlaylist) {
        q.bindValue(QStringLiteral(":playlist"), playlistId);
    }
    if (!query.filter.isEmpty()) {
        q.bindValue(QStringLiteral(":filter"), QStringLiteral("%") + query.filter + QStringLiteral("%"));
    }
    for (int i = 0; i < query.keyFilters.size(); ++i) {
        q.bindValue(QStringLiteral(":key%1").arg(i), query.keyFilters.at(i));
    }
    if (!query.location.isEmpty()) {
        q.bindValue(QStringLiteral(":location"), query.location);
    }
    if (!query.fileBaseName.isEmpty()) {
        QString escaped = query.fileBaseName;
        escaped.replace(u'\\', QStringLiteral("\\\\")).replace(u'%', QStringLiteral("\\%")).replace(u'_', QStringLiteral("\\_"));
        q.bindValue(QStringLiteral(":baseName"), QStringLiteral("%/") + escaped + QStringLiteral(".%"));
    }
    if (!q.exec()) {
        LOGW() << "Lexicon tracks: " << q.lastError().text();
        return result;
    }
    while (q.next()) {
        MixHouseTrack t;
        t.id = trackIdForLexicon(q.value(0).toInt());
        t.title = q.value(1).toString();
        t.artist = q.value(2).toString();
        t.album = q.value(3).toString();
        t.genre = q.value(4).toString();
        t.year = q.value(5).toInt() > 0 ? q.value(5).toString() : QString();
        t.filePath = q.value(6).toString();
        t.durationSec = q.value(7).toDouble();
        t.bpm = q.value(8).toDouble();
        t.key = q.value(9).toString();
        t.keyId = keyIdForCamelot(t.key);
        if (t.keyId <= 0) {
            // Lexicon set to another notation: Open Key in capitals ("8M",
            // "11D"), traditional ("Am")...
            t.keyId = keyIdFromText(t.key);
            if (t.keyId <= 0) {
                t.keyId = keyIdFromText(t.key.toLower());
            }
        }
        t.rating = q.value(10).toInt();
        t.composer = q.value(11).toString();
        t.grouping = q.value(12).toString();
        t.trackNumber = q.value(13).toInt() > 0 ? q.value(13).toString() : QString();
        t.comment = q.value(14).toString();
        t.bitrate = q.value(15).toInt();
        t.dateAdded = q.value(16).toString().left(19).replace(u'T', u' ');
        t.timesPlayed = q.value(17).toInt();
        t.lastPlayedAt = q.value(18).toString();
        t.sampleRate = q.value(19).toInt();
        t.playlistPosition = q.value(20).toInt();
        t.fileType = QFileInfo(t.filePath).suffix().toLower();
        // Lexicon's beat grid: tempo markers; the first one anchors the grid.
        if (!q.value(21).isNull() && q.value(22).toDouble() > 0.0) {
            t.beats.valid = true;
            t.beats.bpm = q.value(22).toDouble();
            t.beats.firstBeatSec = q.value(21).toDouble();
            t.beats.beatLengthSec = 60.0 / t.beats.bpm;
            t.beats.constantTempo = q.value(23).toInt() <= 1;
        }
        t.analyzed = t.beats.valid;
        t.energy = q.value(24).toDouble();   // Lexicon's own 1-10, 0 = not rated
        t.remixer = q.value(25).toString();
        t.mix = q.value(26).toString();
        t.label = q.value(27).toString();
        t.producer = q.value(28).toString();
        t.lyricist = q.value(29).toString();
        t.extra1 = q.value(30).toString();
        t.extra2 = q.value(31).toString();
        t.dateModified = q.value(32).toString().left(19).replace(u'T', u' ');
        t.sizeBytes = q.value(33).toLongLong();
        t.danceability = q.value(34).toInt();
        t.popularity = q.value(35).toInt();
        t.happiness = q.value(36).toInt();
        result.push_back(std::move(t));
    }
    return result;
}

std::vector<MixHouseCue> MixHouseLexicon::cues(int trackId) const
{
    std::vector<MixHouseCue> result;
    if (!isLexiconTrack(trackId) || !open()) {
        return result;
    }
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT name, type, startTime, endTime, position, color FROM Cuepoint "
                             "WHERE trackId = :track ORDER BY startTime"));
    q.bindValue(QStringLiteral(":track"), lexiconIdForTrack(trackId));
    if (!q.exec()) {
        return result;
    }
    while (q.next()) {
        MixHouseCue c;
        c.name = q.value(0).toString();
        c.loop = q.value(1).toInt() == 5;   // 1 = cue, 5 = loop
        c.hotCueIndex = q.value(4).isNull() ? -1 : q.value(4).toInt();
        c.startSec = q.value(2).toDouble();
        c.endSec = q.value(3).isNull() ? -1.0 : q.value(3).toDouble();
        c.color = lexiconCueColor(q.value(5).toString());
        result.push_back(c);
    }
    return result;
}
