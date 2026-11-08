/*
 * GlideMix: reading Engine DJ, Traktor, VirtualDJ, Serato and djay. See the
 * header. Formats, as found in the apps' own files:
 *
 * Engine DJ  Music/Engine Library/Database2/m.db. Track.path is relative to
 *            the Engine Library folder; PerformanceData.beatData is a 4-byte length
 *            then zlib: sample rate and sample count (big-endian doubles), a
 *            flag byte, a big-endian int64 marker count, then markers of
 *            (sample offset: double, beat index: int64, beats to the next:
 *            int32, unused: int32), little-endian. quickCues: zlib again, a
 *            big-endian int64 count, then per cue a name length byte, the
 *            name, a big-endian double sample position (-1 = empty) and an
 *            ARGB colour. Track.key is 0..23 around the circle of fifths
 *            (0 C, 1 Am, 2 G, ...). Playlist / PlaylistEntity hold the lists.
 * Traktor    Documents/Native Instruments/Traktor x/collection.nml (XML).
 *            CUE_V2 TYPE="4" START is the grid's first beat in ms; TYPE 0 with
 *            HOTCUE >= 0 a hot cue, TYPE 5 a loop. TEMPO BPM, MUSICAL_KEY 0..11
 *            C..B major, 12..23 C..B minor. PLAYLISTS NODE / PRIMARYKEY KEY is
 *            VOLUME + DIR + FILE.
 * VirtualDJ  database.xml: Poi Type="beatgrid" Pos (s) Bpm, Poi Type="cue",
 *            Tags Key ("Gm"), Scan Bpm is seconds per beat.
 * Serato     Music/_Serato_/database V2 and Subcrates/*.crate: chunks of a
 *            4-letter tag, a big-endian uint32 length and the data; strings are
 *            UTF-16 big-endian; "otrk" holds a track (pfil path from the drive's
 *            root, tsng title, tart artist, talb album, tgen genre, tbpm, tkey,
 *            tlen "mm:ss.xx", ttyr year), a crate's "otrk" holds "ptrk" paths.
 * djay       Music/djay/djay Media Library.djayMediaLibrary/MediaLibrary.db:
 *            secondaryIndex_mediaItemAnalyzedDataIndex (bpm, keySignatureIndex:
 *            even = major on C + i/2, odd = minor on A + (i-1)/2), joined by
 *            the record key to localMediaItemLocations (the file:// URL) and
 *            mediaItems (title, artist). No beat grid.
 */
#include "mixhousedjapps.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <map>
#include <mutex>
#include <thread>
#include <unordered_map>

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSettings>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStorageInfo>
#include <QUrl>
#include <QUuid>
#include <QXmlStreamReader>
#include <QtEndian>

#ifdef Q_OS_WIN
// winsock2 first: windows.h alone pulls in the old winsock.h, and Link's
// asio (same unity file) refuses to build after it.
#include <winsock2.h>
#include <windows.h>
#elif defined(Q_OS_MAC)
#include <sys/mount.h>
#include <sys/param.h>
#endif


#include "log.h"
#include "mixhouse/imixhousekeymatch.h"

namespace au::mixhouse {
namespace {
// ---- one app's library, as the sidebar shows it ---------------------------
struct AppData {
    struct Track {
        MixHouseTrack track;
        std::vector<MixHouseCue> cues;
    };
    std::vector<Track> tracks;
    std::vector<IMixHouseExternalLibrary::Node> nodes;
    QHash<int, QList<int> > playlistTracks;   // playlist node id -> track indexes
};

//! Ids below Lexicon's (which stays above -100,000,000) and above
//! rekordbox's (-1,000,000,000 and down): 100 million per app.
constexpr int kIdBlock = 100000000;

class AppLibrary : public IMixHouseExternalLibrary
{
public:
    AppLibrary(int idBase, AppData data)
        : m_idBase(idBase), m_data(std::move(data))
    {
        for (size_t i = 0; i < m_data.tracks.size(); ++i) {
            m_data.tracks[i].track.id = m_idBase - static_cast<int>(i);
        }
    }
    bool available() const override { return !m_data.tracks.empty(); }
    std::vector<Node> nodes() const override { return m_data.nodes; }
    int trackCount() const override { return static_cast<int>(m_data.tracks.size()); }
    bool ownsTrack(int trackId) const override
    {
        const long long index = static_cast<long long>(m_idBase) - trackId;
        return index >= 0 && index < static_cast<long long>(m_data.tracks.size());
    }
    std::vector<MixHouseCue> cues(int trackId) const override
    {
        return ownsTrack(trackId) ? m_data.tracks[static_cast<size_t>(m_idBase - trackId)].cues : std::vector<MixHouseCue> {};
    }
    MixHouseTrackList tracks(int playlistId, const IMixHouseLibrary::TrackQuery& query) const override
    {
        MixHouseTrackList result;
        auto matches = [&](const MixHouseTrack& t) {
            if (!query.filter.isEmpty() && !t.title.contains(query.filter, Qt::CaseInsensitive)
                && !t.artist.contains(query.filter, Qt::CaseInsensitive)
                && !t.album.contains(query.filter, Qt::CaseInsensitive)) {
                return false;
            }
            if (!query.keyFilters.isEmpty() && !query.keyFilters.contains(t.key)) {
                return false;
            }
            if (!query.location.isEmpty() && t.filePath != query.location) {
                return false;
            }
            if (!query.fileBaseName.isEmpty() && QFileInfo(t.filePath).completeBaseName() != query.fileBaseName) {
                return false;
            }
            return true;
        };
        if (playlistId >= 0) {
            int position = 0;
            for (int index : m_data.playlistTracks.value(playlistId)) {
                MixHouseTrack t = m_data.tracks[static_cast<size_t>(index)].track;
                t.playlistPosition = position++;
                if (matches(t)) {
                    result.push_back(std::move(t));
                }
            }
        } else {
            for (const AppData::Track& entry : m_data.tracks) {
                if (matches(entry.track)) {
                    result.push_back(entry.track);
                }
            }
        }
        sortAndLimitTracks(result, query, playlistId >= 0);
        return result;
    }

private:
    int m_idBase = 0;
    AppData m_data;
};

// ---- shared state ---------------------------------------------------------
using Table = std::unordered_map<std::string, MixHouseDjAppInfo>;

std::mutex g_mutex;
std::shared_ptr<const Table> g_table;
std::map<QString, std::shared_ptr<const AppLibrary> > g_libraries;
std::atomic<bool> g_loaded { false };
std::atomic<int> g_generation { 0 };   // bumps when a library arrives later (Traktor after its click)
bool g_started = false;
std::atomic<int> g_loadTicket { 0 };   // a newer load (after a preference change) wins

const QStringList kSources = { QStringLiteral("engine"), QStringLiteral("traktor"), QStringLiteral("serato"),
                               QStringLiteral("vdj"), QStringLiteral("djay") };

std::string keyOf(const QString& path)
{
    QString p = QDir::cleanPath(path);
#ifdef Q_OS_WIN
    p = p.toLower();
#endif
    return p.toStdString();
}

int keyIdFrom(int semitone, bool minor)
{
    return (minor ? 13 : 1) + ((semitone % 12) + 12) % 12;
}

QString home()
{
    return QDir::homePath();
}


//! Fills the track's fields every app shares, and the analysis table entry.
void finish(Table& table, AppData::Track& entry, const QString& source, double firstBeat, bool hasGrid)
{
    MixHouseTrack& t = entry.track;
    t.fileType = QFileInfo(t.filePath).suffix().toLower();
    t.key = IMixHouseKeyMatch::camelot(t.keyId);   // as rekordbox and Lexicon show keys
    if (hasGrid && t.bpm > 0.0) {
        t.beats.valid = true;
        t.beats.bpm = t.bpm;
        t.beats.firstBeatSec = firstBeat;
        t.beats.beatLengthSec = 60.0 / t.bpm;
    }
    if (t.filePath.isEmpty() || t.bpm <= 0.0) {
        return;
    }
    MixHouseDjAppInfo info { t.bpm, firstBeat, hasGrid, t.keyId, source };
    auto [it, added] = table.try_emplace(keyOf(t.filePath), info);
    if (!added && !it->second.hasGrid && hasGrid) {
        it->second = info;
    }
}

IMixHouseExternalLibrary::Node playlistNode(int id, const QString& name, int parentId, bool folder, int count)
{
    IMixHouseExternalLibrary::Node n;
    n.id = id;
    n.name = name;
    n.parentId = parentId;
    n.type = folder ? IMixHouseExternalLibrary::NodeType::Folder : IMixHouseExternalLibrary::NodeType::Playlist;
    n.trackCount = count;
    return n;
}

//! Where else an app's library can be: every other drive (Engine DJ, Serato
//! and VirtualDJ keep one at the root of each drive for the songs on it -- an
//! SD card, a USB stick) and the folders added in Preferences > Library
//! (a VirtualDJ or Traktor folder moved somewhere else). The home folder's
//! own libraries are read by each app's loader.
struct Place {
    QString folder;      ///< a drive's root or an added folder
    QString name;        ///< the sidebar folder its playlists go in
    QString driveRoot;   ///< root of the drive it is on ("" = the macOS startup disk)
};

QString driveRootOf(const QString& path)
{
    QString root = QDir::cleanPath(QStorageInfo(path).rootPath());
#ifndef Q_OS_WIN
    if (root == u"/" || QDir(root).canonicalPath() == u"/") {
        root.clear();
    }
#endif
    return root;
}

//! The roots of the local drives (internal, USB, SD card), listed without
//! asking any drive anything. Network shares are left out: a mapped drive
//! whose server is gone answers a volume query only after the network
//! timeout, and 1.1.29 froze the UI 25-40 s every ~40 s on such a PC
//! (Marc, 2026-11-08). A share is still read when added in Preferences.
QStringList localDriveRoots()
{
    QStringList roots;
#ifdef Q_OS_WIN
    const DWORD mask = GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if (!(mask & (DWORD(1) << i))) {
            continue;
        }
        const wchar_t root[] = { wchar_t(L'A' + i), L':', L'\\', 0 };
        const UINT type = GetDriveTypeW(root);   // no I/O, mapped drives say DRIVE_REMOTE
        if (type == DRIVE_FIXED || type == DRIVE_REMOVABLE) {
            roots << QString(QChar(u'A' + i)) + QStringLiteral(":/");
        }
    }
#elif defined(Q_OS_MAC)
    struct statfs* mounts = nullptr;
    const int count = getmntinfo(&mounts, MNT_NOWAIT);   // the kernel's table, no statfs per mount
    for (int i = 0; i < count; ++i) {
        if (mounts[i].f_flags & MNT_LOCAL) {
            roots << QString::fromLocal8Bit(mounts[i].f_mntonname);
        }
    }
#else
    for (const QStorageInfo& volume : QStorageInfo::mountedVolumes()) {
        static const QByteArrayList network = { "nfs", "nfs4", "cifs", "smb3", "smbfs", "sshfs", "fuse.sshfs", "davfs", "fuse.davfs2" };
        if (!network.contains(volume.fileSystemType())) {
            roots << volume.rootPath();
        }
    }
#endif
    return roots;
}

std::vector<Place> otherPlaces()
{
    std::vector<Place> out;
    const QString homeRoot = QStorageInfo(QDir::homePath()).rootPath();
    for (const QString& localRoot : localDriveRoots()) {
        const QStorageInfo volume(localRoot);
        const QString root = volume.rootPath();
        if (!volume.isValid() || !volume.isReady() || root == homeRoot) {
            continue;
        }
#ifndef Q_OS_WIN
        // System and snapshot mounts; the startup disk is also linked in /Volumes.
        if (!root.startsWith(u"/Volumes/") || QDir(root).canonicalPath() == u"/") {
            continue;
        }
#endif
        const QString clean = QDir::cleanPath(root);
        out.push_back({ clean, volume.displayName().isEmpty() ? clean : volume.displayName(), clean });
    }
    for (const QString& added : djAppFolders()) {
        const QString folder = QDir::cleanPath(added);
        const bool known = std::any_of(out.begin(), out.end(), [&](const Place& p) { return p.folder == folder; });
        if (!known && QFileInfo(folder).isDir()) {
            out.push_back({ folder, QDir(folder).dirName(), driveRootOf(folder) });
        }
    }
    return out;
}

//! The first of `names` (relative to `folder`, "" = the folder itself) that exists.
QString firstExisting(const QString& folder, const QStringList& names)
{
    for (const QString& name : names) {
        const QString path = name.isEmpty() ? folder : folder + QLatin1Char('/') + name;
        if (QFileInfo::exists(path)) {
            return path;
        }
    }
    return {};
}

//! Adds a drive's library to the app's: its songs (each song once), its
//! playlists in a folder named after the drive, as the apps show them.
void addDrive(AppData& into, AppData more, const QString& driveName)
{
    if (more.tracks.empty()) {
        return;
    }
    std::unordered_map<std::string, int> indexByPath;
    for (size_t i = 0; i < into.tracks.size(); ++i) {
        indexByPath.emplace(keyOf(into.tracks[i].track.filePath), static_cast<int>(i));
    }
    std::vector<int> newIndex(more.tracks.size());
    for (size_t i = 0; i < more.tracks.size(); ++i) {
        const auto [it, added] = indexByPath.emplace(keyOf(more.tracks[i].track.filePath),
                                                     static_cast<int>(into.tracks.size()));
        newIndex[i] = it->second;
        if (added) {
            into.tracks.push_back(std::move(more.tracks[i]));
        }
    }
    if (more.nodes.empty()) {
        return;
    }
    int nextId = 0;
    for (const auto& node : into.nodes) {
        nextId = std::max(nextId, node.id + 1);
    }
    const int folder = nextId++;
    into.nodes.push_back(playlistNode(folder, driveName, -1, true, 0));
    QHash<int, int> newId;
    for (const auto& node : more.nodes) {
        newId.insert(node.id, nextId++);
    }
    for (auto node : more.nodes) {
        node.parentId = node.parentId < 0 ? folder : newId.value(node.parentId, folder);
        node.id = newId.value(node.id);
        into.nodes.push_back(node);
    }
    for (auto it = more.playlistTracks.cbegin(); it != more.playlistTracks.cend(); ++it) {
        QList<int> indexes;
        indexes.reserve(it.value().size());
        for (int index : it.value()) {
            indexes << newIndex[static_cast<size_t>(index)];
        }
        into.playlistTracks.insert(newId.value(it.key()), indexes);
    }
}

// ---- Engine DJ ------------------------------------------------------------
QByteArray inflateEngine(const QByteArray& blob)
{
    if (blob.size() < 6) {
        return {};
    }
    const quint32 size = qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(blob.constData()));
    if (size == 0 || size > 16 * 1024 * 1024) {
        return {};
    }
    // The same layout as Qt's own qCompress (4-byte big-endian size, then
    // zlib), so Qt reads it -- no separate zlib to find on Windows.
    return qUncompress(blob);
}

//! One Engine Library folder: Music/Engine Library, or one at a drive's root.
AppData loadEngineAt(Table& table, QString base)
{
    AppData data;
    // Track paths ("../../Desktop/x.mp3") are relative to the real Engine Library
    // folder (a link to it must not move them).
    if (const QString real = QFileInfo(base).canonicalFilePath(); !real.isEmpty()) {
        base = real;
    }
    const QString dbPath = base + QStringLiteral("/Database2/m.db");
    if (!QFile::exists(dbPath)) {
        return data;
    }
    static const int kKey[24] = { 0, 9, 7, 4, 2, 11, 9, 6, 4, 1, 11, 8, 6, 3, 1, 10, 8, 5, 3, 0, 10, 7, 5, 2 };
    const QString connection = QStringLiteral("glidemix-engine-") + QUuid::createUuid().toString();
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        db.setDatabaseName(dbPath);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        if (db.open()) {
            QHash<int, int> indexById;
            QSqlQuery q(db);
            q.setForwardOnly(true);
            if (q.exec(QStringLiteral("SELECT t.id, t.path, t.title, t.artist, t.album, t.genre, t.year, t.length, "
                                      "t.bpmAnalyzed, t.key, t.rating, p.beatData, p.quickCues, t.comment FROM Track t "
                                      "LEFT JOIN PerformanceData p ON p.trackId = t.id"))) {
                while (q.next()) {
                    AppData::Track entry;
                    MixHouseTrack& t = entry.track;
                    QString path = q.value(1).toString();
                    if (path.isEmpty()) {
                        continue;
                    }
                    t.filePath = QDir::cleanPath(QDir::isRelativePath(path) ? QDir(base).absoluteFilePath(path) : path);
                    t.title = q.value(2).toString();
                    t.artist = q.value(3).toString();
                    t.album = q.value(4).toString();
                    t.genre = q.value(5).toString();
                    t.year = q.value(6).toInt() > 0 ? q.value(6).toString() : QString();
                    t.durationSec = q.value(7).toDouble();
                    t.bpm = q.value(8).toDouble();
                    if (!q.value(9).isNull() && q.value(9).toInt() >= 0 && q.value(9).toInt() < 24) {
                        const int k = q.value(9).toInt();
                        t.keyId = keyIdFrom(kKey[k], k % 2 == 1);
                    }
                    t.rating = q.value(10).toInt() / 20;
                    t.comment = q.value(13).toString();
                    double firstBeat = 0.0, rate = 44100.0;
                    bool hasGrid = false;
                    const QByteArray beats = inflateEngine(q.value(11).toByteArray());
                    if (beats.size() >= 25 + 48) {
                        const auto* b = reinterpret_cast<const uchar*>(beats.constData());
                        rate = qFromBigEndian<double>(b);
                        const qint64 markers = qFromBigEndian<qint64>(b + 17);
                        if (rate > 0.0 && markers >= 2 && beats.size() >= 25 + markers * 24) {
                            const double off0 = qFromLittleEndian<double>(b + 25);
                            const qint64 beat0 = qFromLittleEndian<qint64>(b + 25 + 8);
                            const double off1 = qFromLittleEndian<double>(b + 49);
                            const qint64 beat1 = qFromLittleEndian<qint64>(b + 49 + 8);
                            if (beat1 > beat0 && off1 > off0) {
                                const double samplesPerBeat = (off1 - off0) / double(beat1 - beat0);
                                t.bpm = 60.0 * rate / samplesPerBeat;
                                firstBeat = (off0 - double(beat0) * samplesPerBeat) / rate;
                                hasGrid = true;
                            }
                        }
                    }
                    const QByteArray cues = inflateEngine(q.value(12).toByteArray());
                    if (cues.size() >= 8 && rate > 0.0) {
                        const auto* c = reinterpret_cast<const uchar*>(cues.constData());
                        const qint64 count = qFromBigEndian<qint64>(c);
                        int at = 8;
                        for (qint64 i = 0; i < count && i < 16 && at < cues.size(); ++i) {
                            const int nameLen = c[at];
                            if (at + 1 + nameLen + 12 > cues.size()) {
                                break;
                            }
                            const QString name = QString::fromUtf8(cues.constData() + at + 1, nameLen);
                            const double pos = qFromBigEndian<double>(c + at + 1 + nameLen);
                            const quint32 argb = qFromBigEndian<quint32>(c + at + 1 + nameLen + 8);
                            at += 1 + nameLen + 12;
                            if (pos >= 0.0) {
                                MixHouseCue cue;
                                cue.name = name;
                                cue.hotCueIndex = static_cast<int>(i);
                                cue.startSec = pos / rate;
                                cue.color = QColor::fromRgb(argb);
                                entry.cues.push_back(cue);
                            }
                        }
                    }
                    finish(table, entry, QStringLiteral("Engine DJ"), firstBeat, hasGrid);
                    indexById.insert(q.value(0).toInt(), static_cast<int>(data.tracks.size()));
                    data.tracks.push_back(std::move(entry));
                }
            }
            // Playlists (Engine 2+): parentListId 0 = top level.
            QSqlQuery lists(db);
            if (lists.exec(QStringLiteral("SELECT id, title, parentListId FROM Playlist ORDER BY id"))) {
                while (lists.next()) {
                    const int id = lists.value(0).toInt();
                    const int parent = lists.value(2).toInt();
                    QSqlQuery entries(db);
                    entries.prepare(QStringLiteral("SELECT trackId FROM PlaylistEntity WHERE listId = ? ORDER BY id"));
                    entries.addBindValue(id);
                    QList<int> indexes;
                    if (entries.exec()) {
                        while (entries.next()) {
                            const auto it = indexById.constFind(entries.value(0).toInt());
                            if (it != indexById.constEnd()) {
                                indexes << it.value();
                            }
                        }
                    }
                    data.nodes.push_back(playlistNode(id, lists.value(1).toString(), parent > 0 ? parent : -1, false,
                                                      static_cast<int>(indexes.size())));
                    data.playlistTracks.insert(id, indexes);
                }
            }
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connection);
    LOGI() << "GlideMix DJ apps: Engine DJ (" << base << "), " << data.tracks.size() << " songs, " << data.nodes.size()
           << " playlists";
    return data;
}

AppData loadEngine(Table& table)
{
    AppData data = loadEngineAt(table, home() + QStringLiteral("/Music/Engine Library"));
    for (const Place& place : otherPlaces()) {
        // A drive's root holds "Engine Library"; an added folder may be it.
        const QString lib = firstExisting(place.folder, { QStringLiteral("Engine Library/Database2/m.db"),
                                                          QStringLiteral("Database2/m.db") });
        if (!lib.isEmpty()) {
            addDrive(data, loadEngineAt(table, QFileInfo(QFileInfo(lib).absolutePath()).absolutePath()), place.name);
        }
    }
    return data;
}

// ---- Traktor --------------------------------------------------------------
QString traktorCollection()
{
    QDir ni(home() + QStringLiteral("/Documents/Native Instruments"));
    QString best;
    QDateTime newest;
    for (const QFileInfo& dir : ni.entryInfoList({ QStringLiteral("Traktor*") }, QDir::Dirs)) {
        const QFileInfo nml(dir.absoluteFilePath() + QStringLiteral("/collection.nml"));
        if (nml.exists() && (best.isEmpty() || nml.lastModified() > newest)) {
            best = nml.absoluteFilePath();
            newest = nml.lastModified();
        }
    }
    return best;
}

AppData loadTraktorAt(Table& table, const QString& path)
{
    AppData data;
    QFile file(path);
    if (path.isEmpty() || !file.open(QIODevice::ReadOnly)) {
        return data;
    }
    QXmlStreamReader xml(&file);
    QHash<QString, int> indexByKey;   // VOLUME + DIR + FILE, as playlists name songs
    enum class Part { None, Collection, Playlists } part = Part::None;
    AppData::Track entry;
    double firstBeat = 0.0;
    bool hasGrid = false;
    QString primaryKey;
    std::vector<int> nodeStack;   // playlist node ids, for folders
    int nextNodeId = 0;
    int currentList = -1;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            const auto name = xml.name();
            const auto a = xml.attributes();
            if (name == u"COLLECTION") {
                part = Part::Collection;
            } else if (name == u"PLAYLISTS") {
                part = Part::Playlists;
            } else if (part == Part::Collection) {
                if (name == u"ENTRY") {
                    entry = {};
                    entry.track.title = a.value(u"TITLE").toString();
                    entry.track.artist = a.value(u"ARTIST").toString();
                    firstBeat = 0.0;
                    hasGrid = false;
                } else if (name == u"LOCATION") {
                    const QString rawDir = a.value(u"DIR").toString();
                    const QString volume = a.value(u"VOLUME").toString();
                    const QString fileName = a.value(u"FILE").toString();
                    primaryKey = volume + rawDir + fileName;
                    const QString dir = QString(rawDir).replace(QStringLiteral("/:"), QStringLiteral("/"));
#ifdef Q_OS_WIN
                    entry.track.filePath = volume + dir + fileName;
#else
                    const bool startup = volume.isEmpty()
                                         || QDir(QStringLiteral("/Volumes/") + volume).canonicalPath() == u"/";
                    entry.track.filePath = (startup ? QString() : QStringLiteral("/Volumes/") + volume) + dir + fileName;
#endif
                } else if (name == u"ALBUM") {
                    entry.track.album = a.value(u"TITLE").toString();
                } else if (name == u"INFO") {
                    entry.track.genre = a.value(u"GENRE").toString();
                    entry.track.comment = a.value(u"COMMENT").toString();
                    entry.track.durationSec = a.value(u"PLAYTIME_FLOAT").toDouble();
                    entry.track.year = a.value(u"RELEASE_DATE").toString().section(QLatin1Char('/'), 0, 0);
                    entry.track.rating = a.value(u"RANKING").toInt() / 51;
                    entry.track.dateAdded = a.value(u"IMPORT_DATE").toString().replace(QLatin1Char('/'), QLatin1Char('-'));
                } else if (name == u"TEMPO") {
                    entry.track.bpm = a.value(u"BPM").toDouble();
                } else if (name == u"MUSICAL_KEY") {
                    bool ok = false;
                    const int k = a.value(u"VALUE").toInt(&ok);
                    if (ok && k >= 0 && k < 24) {
                        entry.track.keyId = keyIdFrom(k % 12, k >= 12);
                    }
                } else if (name == u"CUE_V2") {
                    const int type = a.value(u"TYPE").toInt();
                    const double start = a.value(u"START").toDouble() / 1000.0;
                    if (type == 4 && !hasGrid) {
                        firstBeat = start;
                        hasGrid = true;
                    } else if ((type == 0 || type == 5) && a.value(u"HOTCUE").toInt() >= 0) {
                        MixHouseCue cue;
                        cue.name = a.value(u"NAME").toString();
                        cue.hotCueIndex = a.value(u"HOTCUE").toInt();
                        cue.startSec = start;
                        cue.loop = type == 5;
                        if (cue.loop) {
                            cue.endSec = start + a.value(u"LEN").toDouble() / 1000.0;
                        }
                        entry.cues.push_back(cue);
                    }
                }
            } else if (part == Part::Playlists) {
                if (name == u"NODE") {
                    const bool folder = a.value(u"TYPE") == u"FOLDER";
                    const QString nodeName = a.value(u"NAME").toString();
                    if (folder && nodeName == u"$ROOT") {
                        nodeStack.push_back(-1);
                        continue;
                    }
                    const int id = nextNodeId++;
                    data.nodes.push_back(playlistNode(id, nodeName, nodeStack.empty() ? -1 : nodeStack.back(), folder, 0));
                    nodeStack.push_back(id);
                    if (!folder) {
                        currentList = id;
                        data.playlistTracks.insert(id, {});
                    }
                } else if (name == u"PRIMARYKEY" && currentList >= 0) {
                    const auto it = indexByKey.constFind(a.value(u"KEY").toString());
                    if (it != indexByKey.constEnd()) {
                        data.playlistTracks[currentList] << it.value();
                    }
                }
            }
        } else if (xml.isEndElement()) {
            const auto name = xml.name();
            if (part == Part::Collection && name == u"ENTRY") {
                finish(table, entry, QStringLiteral("Traktor"), firstBeat, hasGrid);
                indexByKey.insert(primaryKey, static_cast<int>(data.tracks.size()));
                data.tracks.push_back(std::move(entry));
            } else if (name == u"COLLECTION" || name == u"PLAYLISTS") {
                part = Part::None;
            } else if (part == Part::Playlists && name == u"NODE" && !nodeStack.empty()) {
                if (nodeStack.back() == currentList) {
                    currentList = -1;
                }
                nodeStack.pop_back();
            }
        }
    }
    for (auto& node : data.nodes) {
        node.trackCount = node.type == IMixHouseExternalLibrary::NodeType::Playlist
                          ? static_cast<int>(data.playlistTracks.value(node.id).size()) : 0;
    }
    LOGI() << "GlideMix DJ apps: Traktor (" << path << "), " << data.tracks.size() << " songs, " << data.nodes.size()
           << " playlists";
    return data;
}

AppData loadTraktor(Table& table)
{
    AppData data;
    if (const QString path = traktorCollection(); !path.isEmpty()) {
        data = loadTraktorAt(table, path);
    }
    // A Traktor root folder moved elsewhere (Traktor > Preferences > File Management).
    for (const Place& place : otherPlaces()) {
        const QString nml = firstExisting(place.folder, { QStringLiteral("collection.nml") });
        if (!nml.isEmpty()) {
            addDrive(data, loadTraktorAt(table, nml), place.name);
        }
    }
    return data;
}

// ---- VirtualDJ ------------------------------------------------------------
//! The first of `candidates` that exists. A drive's own database (driveRoot
//! set) names its songs with a drive letter ("D:/Music/x.mp3") whatever the
//! drive is called now: they are on that drive.
AppData loadVirtualDjAt(Table& table, const QStringList& candidates, const QString& driveRoot)
{
    AppData data;
    static const QRegularExpression driveLetter(QStringLiteral("^[A-Za-z]:[/\\\\]"));
    QString opened;
    for (const QString& path : candidates) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }
        opened = path;
        QXmlStreamReader xml(&file);
        AppData::Track entry;
        double firstBeat = 0.0;
        bool hasGrid = false;
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isStartElement()) {
                const auto a = xml.attributes();
                if (xml.name() == u"Song") {
                    entry = {};
                    entry.track.filePath = a.value(u"FilePath").toString();
                    if (!driveRoot.isEmpty() && driveLetter.match(entry.track.filePath).hasMatch()) {
                        entry.track.filePath = QDir::cleanPath(driveRoot + QLatin1Char('/') + entry.track.filePath.mid(3));
                    }
                    firstBeat = 0.0;
                    hasGrid = false;
                } else if (xml.name() == u"Tags") {
                    entry.track.title = a.value(u"Title").toString();
                    entry.track.artist = a.value(u"Author").toString();
                    entry.track.album = a.value(u"Album").toString();
                    entry.track.genre = a.value(u"Genre").toString();
                    entry.track.year = a.value(u"Year").toString();
                    if (!a.value(u"Key").isEmpty()) {
                        entry.track.keyId = djKeyIdFromText(a.value(u"Key").toString());
                    }
                } else if (xml.name() == u"Comment") {
                    entry.track.comment = xml.readElementText();
                } else if (xml.name() == u"Infos") {
                    entry.track.durationSec = a.value(u"SongLength").toDouble();
                } else if (xml.name() == u"Scan") {
                    const double spb = a.value(u"Bpm").toDouble();   // seconds per beat
                    if (spb > 0.1 && spb < 3.0 && entry.track.bpm <= 0.0) {
                        entry.track.bpm = 60.0 / spb;
                    }
                    if (!a.value(u"Key").isEmpty() && entry.track.keyId == 0) {
                        entry.track.keyId = djKeyIdFromText(a.value(u"Key").toString());
                    }
                } else if (xml.name() == u"Poi") {
                    const QStringView type = a.value(u"Type");
                    if (type == u"beatgrid" && !hasGrid) {
                        firstBeat = a.value(u"Pos").toDouble();
                        if (a.value(u"Bpm").toDouble() > 0.0) {
                            entry.track.bpm = a.value(u"Bpm").toDouble();
                        }
                        hasGrid = true;
                    } else if (type == u"cue") {
                        MixHouseCue cue;
                        cue.name = a.value(u"Name").toString();
                        cue.hotCueIndex = a.value(u"Num").toInt() - 1;
                        cue.startSec = a.value(u"Pos").toDouble();
                        entry.cues.push_back(cue);
                    }
                }
            } else if (xml.isEndElement() && xml.name() == u"Song") {
                finish(table, entry, QStringLiteral("VirtualDJ"), firstBeat, hasGrid);
                data.tracks.push_back(std::move(entry));
            }
        }
        break;
    }
    LOGI() << "GlideMix DJ apps: VirtualDJ (" << opened << "), " << data.tracks.size() << " songs";
    return data;
}

AppData loadVirtualDj(Table& table)
{
    AppData data = loadVirtualDjAt(table, {
        home() + QStringLiteral("/Library/Application Support/VirtualDJ/database.xml"),
        home() + QStringLiteral("/Documents/VirtualDJ/database.xml"),
        home() + QStringLiteral("/AppData/Local/VirtualDJ/database.xml"),
    }, QString());
    for (const Place& place : otherPlaces()) {
        const QString db = firstExisting(place.folder, { QStringLiteral("VirtualDJ/database.xml"),
                                                         QStringLiteral("database.xml") });
        if (!db.isEmpty()) {
            addDrive(data, loadVirtualDjAt(table, { db }, place.driveRoot), place.name);
        }
    }
    return data;
}

// ---- Serato ---------------------------------------------------------------
struct Chunk {
    QByteArray tag;
    QByteArray data;
};

std::vector<Chunk> chunks(const QByteArray& in)
{
    std::vector<Chunk> out;
    int at = 0;
    while (at + 8 <= in.size()) {
        const quint32 len = qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(in.constData() + at + 4));
        if (at + 8 + static_cast<qint64>(len) > in.size()) {
            break;
        }
        out.push_back({ in.mid(at, 4), in.mid(at + 8, static_cast<int>(len)) });
        at += 8 + static_cast<int>(len);
    }
    return out;
}

QString utf16be(const QByteArray& d)
{
    QString s(d.size() / 2, Qt::Uninitialized);
    for (int i = 0; i + 1 < d.size(); i += 2) {
        s[i / 2] = QChar((uchar(d[i]) << 8) | uchar(d[i + 1]));
    }
    return s.remove(QChar(0));
}

//! Serato keeps paths from the root of the drive its _Serato_ folder is on.
QString seratoPath(const QString& stored, const QString& driveRoot)
{
    return QDir::cleanPath(driveRoot + QLatin1Char('/') + stored);
}

//! One _Serato_ folder: Music/_Serato_ for the home drive, or one at a drive's root.
AppData loadSeratoAt(Table& table, const QString& folder, const QString& driveRoot)
{
    AppData data;
    QFile file(folder + QStringLiteral("/database V2"));
    if (!file.open(QIODevice::ReadOnly)) {
        return data;
    }
    QHash<QString, int> indexByStored;
    for (const Chunk& c : chunks(file.readAll())) {
        if (c.tag != "otrk") {
            continue;
        }
        AppData::Track entry;
        MixHouseTrack& t = entry.track;
        QString stored;
        for (const Chunk& f : chunks(c.data)) {
            const QString v = f.tag.startsWith('t') || f.tag.startsWith('p') ? utf16be(f.data) : QString();
            if (f.tag == "pfil") {
                stored = v;
            } else if (f.tag == "tsng") {
                t.title = v;
            } else if (f.tag == "tart") {
                t.artist = v;
            } else if (f.tag == "talb") {
                t.album = v;
            } else if (f.tag == "tgen") {
                t.genre = v;
            } else if (f.tag == "tbpm") {
                t.bpm = v.toDouble();
            } else if (f.tag == "tkey") {
                t.keyId = djKeyIdFromText(v);
            } else if (f.tag == "tcom") {
                t.comment = v;
            } else if (f.tag == "ttyr") {
                t.year = v;
            } else if (f.tag == "tlen") {
                const QStringList mmss = v.split(QLatin1Char(':'));
                if (mmss.size() == 2) {
                    t.durationSec = mmss[0].toDouble() * 60.0 + mmss[1].toDouble();
                }
            }
        }
        if (stored.isEmpty()) {
            continue;
        }
        t.filePath = seratoPath(stored, driveRoot);
        // The grid lives in the file (read when the song is analysed).
        finish(table, entry, QStringLiteral("Serato"), 0.0, false);
        indexByStored.insert(stored, static_cast<int>(data.tracks.size()));
        data.tracks.push_back(std::move(entry));
    }
    // Crates: "Parent%%Child.crate".
    QDir crates(folder + QStringLiteral("/Subcrates"));
    QHash<QString, int> nodeByPath;
    int nextId = 0;
    QStringList names = crates.entryList({ QStringLiteral("*.crate") }, QDir::Files, QDir::Name);
    for (const QString& fileName : names) {
        const QStringList parts = QFileInfo(fileName).completeBaseName().split(QStringLiteral("%%"));
        int parent = -1;
        QString pathSoFar;
        for (int i = 0; i + 1 < parts.size(); ++i) {
            pathSoFar += parts[i] + QStringLiteral("%%");
            if (!nodeByPath.contains(pathSoFar)) {
                nodeByPath.insert(pathSoFar, nextId);
                data.nodes.push_back(playlistNode(nextId++, parts[i], parent, true, 0));
            }
            parent = nodeByPath.value(pathSoFar);
        }
        QFile crate(crates.absoluteFilePath(fileName));
        if (!crate.open(QIODevice::ReadOnly)) {
            continue;
        }
        QList<int> indexes;
        for (const Chunk& c : chunks(crate.readAll())) {
            if (c.tag != "otrk") {
                continue;
            }
            for (const Chunk& f : chunks(c.data)) {
                if (f.tag == "ptrk") {
                    const auto it = indexByStored.constFind(utf16be(f.data));
                    if (it != indexByStored.constEnd()) {
                        indexes << it.value();
                    }
                }
            }
        }
        const int id = nextId++;
        data.nodes.push_back(playlistNode(id, parts.last(), parent, false, static_cast<int>(indexes.size())));
        data.playlistTracks.insert(id, indexes);
    }
    LOGI() << "GlideMix DJ apps: Serato (" << folder << "), " << data.tracks.size() << " songs, " << data.nodes.size()
           << " crates";
    return data;
}

AppData loadSerato(Table& table)
{
#ifdef Q_OS_WIN
    const QString homeRoot = QDir::homePath().left(2);
#else
    const QString homeRoot;
#endif
    AppData data = loadSeratoAt(table, home() + QStringLiteral("/Music/_Serato_"), homeRoot);
    for (const Place& place : otherPlaces()) {
        const QString db = firstExisting(place.folder, { QStringLiteral("_Serato_/database V2"),
                                                         QStringLiteral("database V2") });
        if (!db.isEmpty()) {
            // Serato's paths start at the root of the drive the folder is on.
            addDrive(data, loadSeratoAt(table, QFileInfo(db).absolutePath(), place.driveRoot), place.name);
        }
    }
    return data;
}

// ---- djay -----------------------------------------------------------------
//! djay's records: each value is written just before its name, as
//! 0x08 value 0x00 0x08 name 0x00 (strings).
QString djayString(const QByteArray& data, const char* field)
{
    const QByteArray marker = QByteArray("\x08") + field + QByteArray(1, '\0');
    const int at = data.indexOf(marker);
    if (at <= 1 || data[at - 1] != '\0') {
        return {};
    }
    const int start = data.lastIndexOf('\x08', at - 2);
    return start < 0 ? QString() : QString::fromUtf8(data.mid(start + 1, at - 2 - start));
}

AppData loadDjay(Table& table)
{
    AppData data;
    const QString dbPath = home() + QStringLiteral("/Music/djay/djay Media Library.djayMediaLibrary/MediaLibrary.db");
    if (!QFile::exists(dbPath)) {
        return data;
    }
    const QString connection = QStringLiteral("glidemix-djay-") + QUuid::createUuid().toString();
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        db.setDatabaseName(dbPath);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        if (db.open()) {
            QHash<QString, QString> locations;
            QSqlQuery loc(db);
            loc.setForwardOnly(true);
            if (loc.exec(QStringLiteral("SELECT key, data FROM database2 WHERE collection = 'localMediaItemLocations'"))) {
                while (loc.next()) {
                    const QByteArray d = loc.value(1).toByteArray();
                    const int at = d.indexOf("file://");
                    if (at < 0) {
                        continue;
                    }
                    const int end = d.indexOf('\0', at);
                    const QUrl u(QString::fromUtf8(d.mid(at, end < 0 ? -1 : end - at)));
                    if (u.isLocalFile()) {
                        locations.insert(loc.value(0).toString(), u.toLocalFile());
                    }
                }
            }
            QSqlQuery q(db);
            q.setForwardOnly(true);
            if (q.exec(QStringLiteral("SELECT d.key, s.bpm, s.keySignatureIndex, d.data FROM database2 d "
                                      "JOIN secondaryIndex_mediaItemAnalyzedDataIndex s ON s.rowid = d.rowid "
                                      "WHERE d.collection = 'mediaItemAnalyzedData'"))) {
                while (q.next()) {
                    const auto it = locations.constFind(q.value(0).toString());
                    if (it == locations.constEnd()) {
                        continue;
                    }
                    AppData::Track entry;
                    MixHouseTrack& t = entry.track;
                    t.filePath = it.value();
                    t.bpm = q.value(1).toDouble();
                    if (!q.value(2).isNull()) {
                        const int i = q.value(2).toInt();
                        if (i >= 0 && i < 24) {
                            t.keyId = i % 2 == 0 ? keyIdFrom(i / 2, false) : keyIdFrom(9 + (i - 1) / 2, true);
                        }
                    }
                    const QByteArray d = q.value(3).toByteArray();
                    t.title = djayString(d, "title");
                    t.artist = djayString(d, "artist");
                    if (t.title.isEmpty()) {
                        t.title = QFileInfo(t.filePath).completeBaseName();
                    }
                    finish(table, entry, QStringLiteral("djay"), 0.0, false);
                    data.tracks.push_back(std::move(entry));
                }
            }
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connection);
    LOGI() << "GlideMix DJ apps: djay, " << data.tracks.size() << " songs";
    return data;
}
}

int djKeyIdFromText(const QString& text)
{
    const QString t = text.trimmed();
    static const QRegularExpression camelot(QStringLiteral("^(1[0-2]|[1-9])\\s*([AaBb])$"));
    if (const auto m = camelot.match(t); m.hasMatch()) {
        // 8B is C major, 8A A minor; each step up is a fifth up.
        const int n = m.captured(1).toInt();
        const bool minor = m.captured(2).toUpper() == u"A";
        return keyIdFrom((minor ? 9 : 0) + 7 * (n - 8), minor);
    }
    static const QRegularExpression openKey(QStringLiteral("^(1[0-2]|[1-9])\\s*([mMdD])$"));
    if (const auto m = openKey.match(t); m.hasMatch()) {
        // Open Key 1d is C major, 1m A minor.
        const int n = m.captured(1).toInt();
        const bool minor = m.captured(2).toLower() == u"m";
        return keyIdFrom((minor ? 9 : 0) + 7 * (n - 1), minor);
    }
    static const QRegularExpression note(QStringLiteral("^([A-Ga-g])([#b\\x{266F}\\x{266D}]?)\\s*(m|min|minor|maj|major)?$"));
    const auto m = note.match(t);
    if (!m.hasMatch()) {
        return 0;
    }
    static const int kBase[7] = { 9, 11, 0, 2, 4, 5, 7 };   // A B C D E F G
    int semitone = kBase[m.captured(1).toUpper().at(0).unicode() - 'A'];
    const QString accidental = m.captured(2);
    if (accidental == u"#" || accidental == QStringLiteral("♯")) {
        ++semitone;
    } else if (accidental == u"b" || accidental == QStringLiteral("♭")) {
        --semitone;
    }
    const QString mode = m.captured(3).toLower();
    return keyIdFrom(semitone, mode == u"m" || mode == u"min" || mode == u"minor");
}

QStringList djAppFolders()
{
    return QSettings().value(QStringLiteral("GlideMix/djAppFolders")).toStringList();
}

QString djAppPlacesSignature()
{
    QStringList parts = djAppFolders();
    for (const QString& root : localDriveRoots()) {
        const QStorageInfo volume(root);   // local only: an empty card reader answers at once
        if (volume.isValid() && volume.isReady()) {
            parts << volume.rootPath();
        }
    }
    return parts.join(QLatin1Char('\n'));
}

QStringList hiddenDjApps()
{
    return QSettings().value(QStringLiteral("GlideMix/hiddenDjApps")).toStringList();
}

void reloadDjApps()
{
    {
        std::lock_guard lock(g_mutex);
        g_started = false;
    }
    startLoadingDjApps();
}

void startLoadingDjApps()
{
    {
        std::lock_guard lock(g_mutex);
        if (g_started) {
            return;
        }
        g_started = true;
    }
    // Apps switched off in Preferences > Library are not read at all.
    const QStringList hidden = hiddenDjApps();
    const int ticket = ++g_loadTicket;
    // macOS asks for the Documents folder the first time it is read: Traktor's
    // collection is read there only after the user clicks Traktor once.
#ifdef Q_OS_MAC
    const bool readTraktor = QSettings().value(QStringLiteral("GlideMix/readTraktor"), false).toBool();
#else
    const bool readTraktor = true;
#endif
    std::thread([readTraktor, hidden, ticket]() {
        auto table = std::make_shared<Table>();
        std::map<QString, std::shared_ptr<const AppLibrary> > libraries;
        // Grid sources first, so analysis takes theirs; then BPM and key only.
        const std::pair<QString, AppData (*)(Table&)> loaders[] = {
            { QStringLiteral("engine"), &loadEngine },
            { QStringLiteral("traktor"), &loadTraktor },
            { QStringLiteral("vdj"), &loadVirtualDj },
            { QStringLiteral("serato"), &loadSerato },
            { QStringLiteral("djay"), &loadDjay },
        };
        for (const auto& [source, load] : loaders) {
            if ((source == u"traktor" && !readTraktor) || hidden.contains(source)) {
                continue;
            }
            AppData data = load(*table);
            if (!data.tracks.empty()) {
                const int idBase = -2 * kIdBlock - static_cast<int>(kSources.indexOf(source)) * kIdBlock;
                libraries.emplace(source, std::make_shared<const AppLibrary>(idBase, std::move(data)));
            }
        }
        LOGI() << "GlideMix DJ apps: " << table->size() << " songs known";
        {
            std::lock_guard lock(g_mutex);
            if (ticket != g_loadTicket) {
                return;   // the preferences changed while this one read
            }
            g_table = std::move(table);
            g_libraries = std::move(libraries);
        }
        g_loaded = true;
        g_generation++;
    }).detach();
}

bool djAppsLoaded()
{
    return g_loaded;
}

bool djAppWaitsForClick(const QString& source)
{
#ifdef Q_OS_MAC
    if (source != u"traktor" || djAppLibrary(source) || hiddenDjApps().contains(source)) {
        return false;
    }
    if (QSettings().value(QStringLiteral("GlideMix/readTraktor"), false).toBool()) {
        return false;   // reading now (or nothing there)
    }
    // Installed? /Applications is not a protected folder.
    for (const QString& dir : { QStringLiteral("/Applications"), QStringLiteral("/Applications/Native Instruments") }) {
        if (!QDir(dir).entryList({ QStringLiteral("Traktor*") }, QDir::Dirs).isEmpty()) {
            return true;
        }
    }
#else
    Q_UNUSED(source)
#endif
    return false;
}

void readDjAppNow(const QString& source)
{
    if (source != u"traktor") {
        return;
    }
    QSettings().setValue(QStringLiteral("GlideMix/readTraktor"), true);
    std::thread([]() {
        std::shared_ptr<const Table> current;
        {
            std::lock_guard lock(g_mutex);
            current = g_table;
        }
        auto table = std::make_shared<Table>(current ? *current : Table {});
        AppData data = loadTraktor(*table);
        std::lock_guard lock(g_mutex);
        g_table = std::move(table);
        if (!data.tracks.empty()) {
            g_libraries[QStringLiteral("traktor")] = std::make_shared<const AppLibrary>(
                -2 * kIdBlock - static_cast<int>(kSources.indexOf(QStringLiteral("traktor"))) * kIdBlock, std::move(data));
        }
        g_generation++;
    }).detach();
}

int djAppsGeneration()
{
    return g_generation;
}

MixHouseDjAppInfo lookupDjApps(const QString& filePath)
{
    std::shared_ptr<const Table> table;
    {
        std::lock_guard lock(g_mutex);
        table = g_table;
    }
    if (!table) {
        return {};
    }
    const auto it = table->find(keyOf(filePath));
    return it == table->end() ? MixHouseDjAppInfo {} : it->second;
}

QStringList djAppSources()
{
    return kSources;
}

QString djAppName(const QString& source)
{
    if (source == u"engine") {
        return QStringLiteral("Engine DJ");
    }
    if (source == u"traktor") {
        return QStringLiteral("Traktor");
    }
    if (source == u"serato") {
        return QStringLiteral("Serato DJ");
    }
    if (source == u"vdj") {
        return QStringLiteral("VirtualDJ");
    }
    if (source == u"djay") {
        return QStringLiteral("djay");
    }
    return source;
}

const IMixHouseExternalLibrary* djAppLibrary(const QString& source)
{
    std::lock_guard lock(g_mutex);
    const auto it = g_libraries.find(source);
    return it == g_libraries.end() ? nullptr : it->second.get();
}
}
