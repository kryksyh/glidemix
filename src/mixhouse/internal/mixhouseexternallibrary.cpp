/*
 * GlideMix
 */
#include "mixhouse/imixhouseexternallibrary.h"

#include <algorithm>

#include "mixhouse/imixhousekeymatch.h"

namespace au::mixhouse {
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
