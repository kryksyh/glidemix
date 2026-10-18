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
    bool open() const;

    mutable QSqlDatabase m_db;
};
}
