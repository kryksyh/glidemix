/*
 * GlideMix: a tracklist written next to an exported mix -- a timestamped
 * text list (Mixcloud's tracklist, show notes) and a .cue sheet. Times are
 * those of the file: they follow the tempo line, like the export itself.
 */
#pragma once

#include <string>

#include "modularity/ioc.h"
#include "context/iglobalcontext.h"
#include "mixhouse/imixhousebeatgrids.h"
#include "mixhouse/imixhouselibrary.h"

namespace au::mixhouse {
class MixHouseTracklist : public muse::Contextable
{
    muse::ContextInject<context::IGlobalContext> globalContext { this };
    muse::ContextInject<IMixHouseBeatGrids> beatGrids { this };
    muse::ContextInject<IMixHouseLibrary> library { this };

public:
    explicit MixHouseTracklist(const muse::modularity::ContextPtr& ctx)
        : muse::Contextable(ctx) {}

    //! Writes `<audio file>.txt` and `.cue` for timeline t0..t1 of the current
    //! project; returns the .txt path, or empty when there are no songs.
    std::string write(const std::string& audioPath, double t0, double t1) const;
};
}
