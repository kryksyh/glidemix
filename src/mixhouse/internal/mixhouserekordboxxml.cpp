/*
 * GlideMix
 */
#include "mixhouserekordboxxml.h"

#include <cmath>
#include <thread>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTimer>
#include <QUrl>
#include <QXmlStreamReader>

#include "global/settings.h"

#include "log.h"

using namespace au::mixhouse;

namespace {
const muse::Settings::Key REKORDBOX_XML_PATH("mixhouse", "mixhouse/rekordboxXml/path");

using Data = MixHouseRekordboxXml::Data;

//! GlideMix: none until the user picks the file (Import from Rekordbox XML):
//! looking for ~/Documents/rekordbox.xml at startup made macOS ask every new
//! user to let GlideMix into their Documents folder.
QString defaultPath()
{
    return QString();
}

//! rekordbox writes Location as a file URL: file://localhost/Volumes/...
QString localPath(const QString& location)
{
    QString url = location;
    if (url.startsWith(QStringLiteral("file://localhost/"))) {
        url = QStringLiteral("file:///") + url.mid(17);
    }
    return QUrl(url).toLocalFile();
}

double attrDouble(const QXmlStreamAttributes& a, const char* name)
{
    return a.value(QLatin1String(name)).toDouble();
}

QString attrText(const QXmlStreamAttributes& a, const char* name)
{
    return a.value(QLatin1String(name)).toString();
}

void readTrack(QXmlStreamReader& xml, Data& data, QHash<qint64, int>& indexById, QHash<QString, int>& indexByLocation)
{
    const QXmlStreamAttributes a = xml.attributes();
    Data::Track entry;
    MixHouseTrack& t = entry.track;
    const int index = static_cast<int>(data.tracks.size());
    t.id = IMixHouseRekordboxXml::kTrackIdBase - index;
    t.title = attrText(a, "Name");
    t.artist = attrText(a, "Artist");
    t.album = attrText(a, "Album");
    t.genre = attrText(a, "Genre");
    t.composer = attrText(a, "Composer");
    t.grouping = attrText(a, "Grouping");
    t.comment = attrText(a, "Comments");
    const int year = a.value(QLatin1String("Year")).toInt();
    t.year = year > 0 ? QString::number(year) : QString();
    const int trackNumber = a.value(QLatin1String("TrackNumber")).toInt();
    t.trackNumber = trackNumber > 0 ? QString::number(trackNumber) : QString();
    t.durationSec = attrDouble(a, "TotalTime");
    t.bpm = attrDouble(a, "AverageBpm");
    t.bitrate = a.value(QLatin1String("BitRate")).toInt();
    t.sampleRate = a.value(QLatin1String("SampleRate")).toInt();
    t.timesPlayed = a.value(QLatin1String("PlayCount")).toInt();
    t.dateAdded = attrText(a, "DateAdded");
    // rekordbox stores stars as 0, 51, 102, 153, 204, 255.
    t.rating = static_cast<int>(std::lround(a.value(QLatin1String("Rating")).toInt() / 51.0));
    t.key = attrText(a, "Tonality");
    t.keyId = keyIdForCamelot(t.key);
    const QString location = attrText(a, "Location");
    t.filePath = localPath(location);
    t.fileType = QFileInfo(t.filePath).suffix().toLower();

    bool tempoSeen = false;
    bool tempoVaries = false;
    std::vector<MixHouseCue> memoryCues;
    while (xml.readNextStartElement()) {
        const QXmlStreamAttributes m = xml.attributes();
        if (xml.name() == QLatin1String("TEMPO")) {
            const double bpm = attrDouble(m, "Bpm");
            if (!tempoSeen && bpm > 0.0) {
                tempoSeen = true;
                t.beats.valid = true;
                t.beats.bpm = bpm;
                t.beats.firstBeatSec = attrDouble(m, "Inizio");
                t.beats.beatLengthSec = 60.0 / bpm;
            } else if (tempoSeen && std::abs(bpm - t.beats.bpm) > 0.01) {
                tempoVaries = true;
            }
        } else if (xml.name() == QLatin1String("POSITION_MARK")) {
            // Type 0 cue, 1 fade-in, 2 fade-out, 3 load, 4 loop; Num -1 is a
            // memory cue, 0.. a hot cue (A..).
            MixHouseCue cue;
            cue.name = attrText(m, "Name");
            cue.loop = m.value(QLatin1String("Type")).toInt() == 4;
            cue.startSec = attrDouble(m, "Start");
            cue.endSec = m.hasAttribute(QLatin1String("End")) ? attrDouble(m, "End") : -1.0;
            cue.hotCueIndex = m.value(QLatin1String("Num")).toInt();
            if (m.hasAttribute(QLatin1String("Red"))) {
                cue.color = QColor(m.value(QLatin1String("Red")).toInt(), m.value(QLatin1String("Green")).toInt(),
                                   m.value(QLatin1String("Blue")).toInt());
            } else {
                cue.color = QColor(0xE6, 0x2E, 0x2E);   // rekordbox's memory cue red
            }
            (cue.hotCueIndex >= 0 ? entry.cues : memoryCues).push_back(cue);
        }
        xml.skipCurrentElement();
    }
    t.beats.constantTempo = !tempoVaries;
    t.analyzed = t.beats.valid;
    // rekordbox exports a hot cue again as a memory cue at the same spot.
    for (const MixHouseCue& memory : memoryCues) {
        const bool duplicate = std::any_of(entry.cues.begin(), entry.cues.end(), [&](const MixHouseCue& hot) {
            return std::abs(hot.startSec - memory.startSec) < 0.001 && hot.loop == memory.loop;
        });
        if (!duplicate) {
            entry.cues.push_back(memory);
        }
    }
    std::sort(entry.cues.begin(), entry.cues.end(), [](const MixHouseCue& x, const MixHouseCue& y) {
        return x.startSec < y.startSec;
    });

    indexById.insert(a.value(QLatin1String("TrackID")).toLongLong(), index);
    indexByLocation.insert(location, index);
    data.tracks.push_back(std::move(entry));
}

void readNode(QXmlStreamReader& xml, Data& data, int parentId, const QHash<qint64, int>& indexById,
              const QHash<QString, int>& indexByLocation)
{
    const QXmlStreamAttributes a = xml.attributes();
    IMixHouseExternalLibrary::Node node;
    node.id = static_cast<int>(data.nodes.size());
    node.name = attrText(a, "Name");
    node.parentId = parentId;
    const bool folder = a.value(QLatin1String("Type")).toInt() == 0;
    node.type = folder ? IMixHouseExternalLibrary::NodeType::Folder : IMixHouseExternalLibrary::NodeType::Playlist;
    const bool byLocation = a.value(QLatin1String("KeyType")).toInt() == 1;
    data.nodes.push_back(node);
    const int nodeIndex = node.id;

    std::vector<int> tracks;
    int children = 0;
    while (xml.readNextStartElement()) {
        if (xml.name() == QLatin1String("NODE")) {
            ++children;
            readNode(xml, data, nodeIndex, indexById, indexByLocation);
            continue;
        }
        if (xml.name() == QLatin1String("TRACK")) {
            const QStringView key = xml.attributes().value(QLatin1String("Key"));
            const int index = byLocation ? indexByLocation.value(key.toString(), -1)
                                         : indexById.value(key.toLongLong(), -1);
            if (index >= 0) {
                tracks.push_back(index);
            }
        }
        xml.skipCurrentElement();
    }
    data.nodes[static_cast<size_t>(nodeIndex)].trackCount = folder ? children : static_cast<int>(tracks.size());
    if (!folder) {
        data.playlistTracks.insert(nodeIndex, std::move(tracks));
    }
}

std::shared_ptr<Data> parse(const QString& path)
{
    auto data = std::make_shared<Data>();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        data->error = QStringLiteral("Could not open %1").arg(path);
        return data;
    }
    QXmlStreamReader xml(&file);
    if (!xml.readNextStartElement() || xml.name() != QLatin1String("DJ_PLAYLISTS")) {
        data->error = QStringLiteral("Not a rekordbox collection XML file.");
        return data;
    }
    QHash<qint64, int> indexById;
    QHash<QString, int> indexByLocation;
    while (xml.readNextStartElement()) {
        if (xml.name() == QLatin1String("COLLECTION")) {
            while (xml.readNextStartElement()) {
                if (xml.name() == QLatin1String("TRACK")) {
                    readTrack(xml, *data, indexById, indexByLocation);
                } else {
                    xml.skipCurrentElement();
                }
            }
        } else if (xml.name() == QLatin1String("PLAYLISTS")) {
            // One ROOT folder holds the tree; its children are the top level.
            while (xml.readNextStartElement()) {
                if (xml.name() != QLatin1String("NODE")) {
                    xml.skipCurrentElement();
                    continue;
                }
                while (xml.readNextStartElement()) {
                    if (xml.name() == QLatin1String("NODE")) {
                        readNode(xml, *data, -1, indexById, indexByLocation);
                    } else {
                        xml.skipCurrentElement();
                    }
                }
            }
        } else {
            xml.skipCurrentElement();
        }
    }
    if (xml.hasError()) {
        data->error = xml.errorString();
    }
    return data;
}
}

MixHouseRekordboxXml::MixHouseRekordboxXml()
    : m_data(std::make_shared<Data>())
{
}

MixHouseRekordboxXml::~MixHouseRekordboxXml()
{
    ++m_generation;   // a read still running is dropped
}

void MixHouseRekordboxXml::init()
{
    muse::settings()->setDefaultValue(REKORDBOX_XML_PATH, muse::Val(defaultPath().toStdString()));
    m_watcher = std::make_unique<QFileSystemWatcher>();
    QObject::connect(m_watcher.get(), &QFileSystemWatcher::fileChanged, m_watcher.get(), [this](const QString&) {
        // rekordbox rewrites the file; wait for it to finish.
        QTimer::singleShot(2000, m_watcher.get(), [this]() { reload(); });
    });
    reload();
}

QString MixHouseRekordboxXml::path() const
{
    return QString::fromStdString(muse::settings()->value(REKORDBOX_XML_PATH).toString());
}

void MixHouseRekordboxXml::setPath(const QString& path)
{
    if (path == this->path()) {
        reload();
        return;
    }
    muse::settings()->setSharedValue(REKORDBOX_XML_PATH, muse::Val(path.toStdString()));
    reload();
}

void MixHouseRekordboxXml::reload()
{
    const QString file = path();
    if (m_watcher) {
        if (!m_watcher->files().isEmpty()) {
            m_watcher->removePaths(m_watcher->files());
        }
        if (QFileInfo::exists(file)) {
            m_watcher->addPath(file);
        }
    }
    if (m_loading) {
        m_reloadPending = true;
        return;
    }
    if (!QFileInfo::exists(file)) {
        m_data = std::make_shared<Data>();
        m_changed.notify();
        return;
    }
    m_loading = true;
    const int generation = ++m_generation;
    std::thread([this, file, generation]() {
        QElapsedTimer timer;
        timer.start();
        std::shared_ptr<const Data> data = parse(file);
        const qint64 ms = timer.elapsed();
        QTimer::singleShot(0, qApp, [this, data, generation, ms, file]() {
            if (generation != m_generation) {
                return;
            }
            m_loading = false;
            m_data = data;
            if (!data->error.isEmpty()) {
                LOGW() << "rekordbox XML " << file << ": " << data->error;
            }
            LOGI() << "GlideMix: rekordbox XML read in " << ms << " ms: " << data->tracks.size() << " tracks, "
                   << data->nodes.size() << " folders/playlists";
            m_changed.notify();
            if (m_reloadPending) {
                m_reloadPending = false;
                reload();
            }
        });
    }).detach();
}

bool MixHouseRekordboxXml::loading() const
{
    return m_loading;
}

muse::async::Notification MixHouseRekordboxXml::changed() const
{
    return m_changed;
}

bool MixHouseRekordboxXml::available() const
{
    return !m_data->tracks.empty();
}

std::vector<IMixHouseExternalLibrary::Node> MixHouseRekordboxXml::nodes() const
{
    return m_data->nodes;
}

int MixHouseRekordboxXml::trackCount() const
{
    return static_cast<int>(m_data->tracks.size());
}

MixHouseTrackList MixHouseRekordboxXml::tracks(int playlistId, const IMixHouseLibrary::TrackQuery& query) const
{
    const std::shared_ptr<const Data> data = m_data;
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
        const auto it = data->playlistTracks.constFind(playlistId);
        if (it == data->playlistTracks.constEnd()) {
            return result;
        }
        int position = 0;
        for (int index : it.value()) {
            MixHouseTrack t = data->tracks[static_cast<size_t>(index)].track;
            t.playlistPosition = position++;
            if (matches(t)) {
                result.push_back(std::move(t));
            }
        }
    } else {
        for (const Data::Track& entry : data->tracks) {
            if (matches(entry.track)) {
                result.push_back(entry.track);
            }
        }
    }
    sortAndLimitTracks(result, query, playlistId >= 0);
    return result;
}

std::vector<MixHouseCue> MixHouseRekordboxXml::cues(int trackId) const
{
    const std::shared_ptr<const Data> data = m_data;
    const long long index = static_cast<long long>(kTrackIdBase) - trackId;
    if (!isRekordboxTrack(trackId) || index < 0 || index >= static_cast<long long>(data->tracks.size())) {
        return {};
    }
    return data->tracks[static_cast<size_t>(index)].cues;
}
