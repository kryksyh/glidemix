/*
 * GlideMix: writes a rekordbox collection XML (rekordbox: Preferences >
 * Advanced > rekordbox xml; Lexicon imports it too) -- tracks with their
 * tags, rating, color, beat grid and cues, and a tree of playlists. Neither
 * app's database is touched: the DJ imports what they want.
 */
#pragma once

#include <QString>
#include <functional>
#include <vector>

#include "imixhouseexternallibrary.h"
#include "mixhousetypes.h"

namespace au::mixhouse {
struct MixHouseXmlPlaylist {
    QString name;
    bool folder = false;
    std::vector<MixHouseXmlPlaylist> children;   ///< folders only
    QStringList files;                           ///< playlists only: track file paths, in order
};

struct MixHouseXmlExport {
    QString productVersion;
    MixHouseTrackList tracks;                    ///< the collection (unique file paths)
    std::vector<MixHouseXmlPlaylist> playlists;  ///< top level
    /// Cue points of a track (Lexicon / rekordbox rows); may be empty.
    std::function<std::vector<MixHouseCue>(const MixHouseTrack&)> cues;
};

/// Returns an error message, or an empty string when written.
QString writeRekordboxXml(const QString& path, const MixHouseXmlExport& data);

/// rekordbox's Colour value for one of the 8 track colors GlideMix offers
/// (rekordbox's own, see the library panel), or "" for none.
QString rekordboxColourFor(int rgb);
}
