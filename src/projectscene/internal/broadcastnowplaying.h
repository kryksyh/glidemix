/*
 * GlideMix
 *
 * Tells the broadcaster which song is playing, for the listeners' players
 * ("Send the playing track's name to the server"). The song is the clip under
 * the playhead of the output being streamed: the red Perform playhead while
 * performing, else the ordinary one. During a transition the incoming song
 * takes over halfway through the overlap.
 */
#pragma once

#include <QTimer>

#include "async/asyncable.h"
#include "modularity/ioc.h"

#include "broadcast/ibroadcaster.h"
#include "broadcast/ibroadcastconfiguration.h"
#include "context/iglobalcontext.h"
#include "playback/iplayback.h"
#include "playback/iperformance.h"
#include "mixhouse/imixhousebeatgrids.h"
#include "mixhouse/imixhouselibrary.h"

namespace au::projectscene {
class BroadcastNowPlaying : public muse::async::Asyncable, public muse::Contextable
{
    muse::GlobalInject<broadcast::IBroadcaster> broadcaster;
    muse::GlobalInject<broadcast::IBroadcastConfiguration> broadcastConfiguration;
    muse::ContextInject<context::IGlobalContext> globalContext { this };
    muse::ContextInject<playback::IPlayback> playback { this };
    muse::ContextInject<playback::IPerformance> performance { this };
    muse::ContextInject<mixhouse::IMixHouseBeatGrids> beatGrids { this };
    muse::ContextInject<mixhouse::IMixHouseLibrary> library { this };

public:
    explicit BroadcastNowPlaying(const muse::modularity::ContextPtr& ctx)
        : muse::Contextable(ctx) {}

    void init();

private:
    void update();
    //! "Artist - Title" as radio apps expect it (they look artwork up by it).
    std::string streamTitle(const trackedit::ClipKey& clip, const muse::String& clipTitle) const;

    QTimer m_timer;
};
}
