/*
 * GlideMix
 */
#pragma once

#include <memory>

#include <QFileSystemWatcher>
#include <QHash>

#include "../imixhouserekordboxxml.h"

namespace au::mixhouse {
class MixHouseRekordboxXml : public IMixHouseRekordboxXml
{
public:
    MixHouseRekordboxXml();
    ~MixHouseRekordboxXml() override;

    void init();

    bool available() const override;
    std::vector<Node> nodes() const override;
    int trackCount() const override;
    MixHouseTrackList tracks(int playlistId, const IMixHouseLibrary::TrackQuery& query) const override;
    bool ownsTrack(int trackId) const override { return isRekordboxTrack(trackId); }
    std::vector<MixHouseCue> cues(int trackId) const override;

    QString path() const override;
    void setPath(const QString& path) override;
    bool loading() const override;
    muse::async::Notification changed() const override;

    struct Data {
        struct Track {
            MixHouseTrack track;
            std::vector<MixHouseCue> cues;
        };
        std::vector<Track> tracks;
        std::vector<Node> nodes;
        QHash<int, std::vector<int> > playlistTracks;   ///< node id -> track indices
        QString error;
    };

private:
    void reload();

    std::shared_ptr<const Data> m_data;
    std::unique_ptr<QFileSystemWatcher> m_watcher;
    bool m_loading = false;
    bool m_reloadPending = false;
    int m_generation = 0;
    muse::async::Notification m_changed;
};
}
