/*
 * GlideMix
 */
#pragma once

#include <QSqlDatabase>

#include "../imixhouselexicon.h"
#include "../imixhouselibraryconfiguration.h"
#include "modularity/ioc.h"

namespace au::mixhouse {
class MixHouseLexicon : public IMixHouseLexicon
{
    muse::GlobalInject<IMixHouseLibraryConfiguration> libraryConfiguration;

public:
    MixHouseLexicon();
    ~MixHouseLexicon() override;

    static QString databasePath();

    bool available() const override;
    std::vector<Node> nodes() const override;
    int trackCount() const override;
    MixHouseTrackList tracks(int playlistId, const IMixHouseLibrary::TrackQuery& query) const override;
    bool ownsTrack(int trackId) const override { return isLexiconTrack(trackId); }
    std::vector<MixHouseCue> cues(int trackId) const override;

private:
    // GlideMix (fast library): each list read once, then searched and sorted
    // in memory; read again when Lexicon's database file changes.
    MixHouseTrackList readTracks(int playlistId, const IMixHouseLibrary::TrackQuery& query) const;
    mutable QHash<int, MixHouseTrackList> m_listCache;
    mutable QString m_listCacheStamp;

    bool open() const;

    mutable QSqlDatabase m_db;
};
}
