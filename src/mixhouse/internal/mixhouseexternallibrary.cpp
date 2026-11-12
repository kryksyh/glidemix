/*
 * GlideMix
 */
#include "mixhouse/imixhouseexternallibrary.h"

#include <algorithm>

#include <QDateTime>
#include <QFileInfo>
#include <QHash>
#include <QMutex>

#include "mixhouse/imixhousekeymatch.h"

namespace au::mixhouse {
MixHouseFileInfo mixhouseFileInfo(const QString& path)
{
    static QMutex mutex;
    static QHash<QString, MixHouseFileInfo> cache;
    if (path.isEmpty()) {
        return {};
    }
    {
        QMutexLocker lock(&mutex);
        const auto it = cache.constFind(path);
        if (it != cache.constEnd()) {
            return *it;
        }
    }
    MixHouseFileInfo info;
    const QFileInfo fi(path);
    if (fi.exists()) {
        const QString format = QStringLiteral("yyyy-MM-dd HH:mm:ss");
        info.modified = fi.lastModified().toString(format);
        QDateTime born = fi.birthTime();
        if (!born.isValid()) {
            born = fi.metadataChangeTime();   // file systems without a creation date
        }
        info.created = born.toString(format);
        info.size = fi.size();
    }
    QMutexLocker lock(&mutex);
    cache.insert(path, info);
    return info;
}

int keyIdForCamelot(const QString& text)
{
    for (int keyId = 1; keyId <= 24 && !text.isEmpty(); ++keyId) {
        if (IMixHouseKeyMatch::camelot(keyId).compare(text, Qt::CaseInsensitive) == 0) {
            return keyId;
        }
    }
    return 0;
}

void sortAndLimitTracks(MixHouseTrackList& tracks, const IMixHouseLibrary::TrackQuery& query, bool playlistOrder)
{
    using K = IMixHouseLibrary::SortKey;
    // A playlist's own order needs no sort (tracks come in that order).
    if (!(query.sort == K::Position && playlistOrder)) {
        auto text = [](const QString& a, const QString& b) { return QString::compare(a, b, Qt::CaseInsensitive); };
        auto cmp = [&](const MixHouseTrack& a, const MixHouseTrack& b) -> int {
            switch (query.sort) {
            case K::Title: return text(a.title, b.title);
            case K::Artist: return text(a.artist, b.artist);
            case K::Album: return text(a.album, b.album);
            case K::Genre: return text(a.genre, b.genre);
            case K::Year: return a.year.toInt() - b.year.toInt();
            case K::Bpm: return a.bpm < b.bpm ? -1 : a.bpm > b.bpm ? 1 : 0;
            case K::Key: return text(a.key, b.key);
            case K::Duration: return a.durationSec < b.durationSec ? -1 : a.durationSec > b.durationSec ? 1 : 0;
            case K::Rating: return a.rating - b.rating;
            case K::Composer: return text(a.composer, b.composer);
            case K::Grouping: return text(a.grouping, b.grouping);
            case K::TrackNumber: return a.trackNumber.toInt() - b.trackNumber.toInt();
            case K::Comment: return text(a.comment, b.comment);
            case K::Bitrate: return a.bitrate - b.bitrate;
            case K::TimesPlayed: return a.timesPlayed - b.timesPlayed;
            case K::LastPlayedAt: return text(a.lastPlayedAt, b.lastPlayedAt);
            case K::SampleRate: return a.sampleRate - b.sampleRate;
            case K::Position: return a.playlistPosition - b.playlistPosition;
            case K::Remixer: return text(a.remixer, b.remixer);
            case K::Mix: return text(a.mix, b.mix);
            case K::Label: return text(a.label, b.label);
            case K::Producer: return text(a.producer, b.producer);
            case K::Lyricist: return text(a.lyricist, b.lyricist);
            case K::Extra1: return text(a.extra1, b.extra1);
            case K::Extra2: return text(a.extra2, b.extra2);
            case K::Danceability: return a.danceability - b.danceability;
            case K::Popularity: return a.popularity - b.popularity;
            case K::Happiness: return a.happiness - b.happiness;
            case K::Size: {
                const qint64 sa = a.sizeBytes > 0 ? a.sizeBytes : mixhouseFileInfo(a.filePath).size;
                const qint64 sb = b.sizeBytes > 0 ? b.sizeBytes : mixhouseFileInfo(b.filePath).size;
                return sa < sb ? -1 : sa > sb ? 1 : 0;
            }
            case K::DateModified:
                return text(a.dateModified.isEmpty() ? mixhouseFileInfo(a.filePath).modified : a.dateModified,
                            b.dateModified.isEmpty() ? mixhouseFileInfo(b.filePath).modified : b.dateModified);
            case K::FileModified: return text(mixhouseFileInfo(a.filePath).modified, mixhouseFileInfo(b.filePath).modified);
            case K::FileCreated: return text(mixhouseFileInfo(a.filePath).created, mixhouseFileInfo(b.filePath).created);
            default: return text(a.dateAdded, b.dateAdded);
            }
        };
        std::stable_sort(tracks.begin(), tracks.end(), [&](const MixHouseTrack& a, const MixHouseTrack& b) {
            const int c = cmp(a, b);
            return query.ascending ? c < 0 : c > 0;
        });
    }
    if (query.limit > 0 && tracks.size() > static_cast<size_t>(query.limit)) {
        tracks.resize(static_cast<size_t>(query.limit));
    }
}
}
