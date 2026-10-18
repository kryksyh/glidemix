/*
 * GlideMix
 */
#pragma once

#include <QString>
#include <vector>

#include "async/notification.h"
#include "modularity/imoduleinterface.h"

#include "imixhouseexternallibrary.h"

namespace au::mixhouse {
//! Lexicon DJ's library, read straight from its own database
//! (~/Library/Application Support/lexicon/main.db; %APPDATA%\lexicon\main.db
//! on Windows), read-only. Nothing is copied into the GlideMix library;
//! Lexicon stays the source of truth.
class IMixHouseLexicon : MODULE_EXPORT_INTERFACE, public IMixHouseExternalLibrary
{
    INTERFACE_ID(IMixHouseLexicon)

public:
    //! MixHouseTrack::id of a Lexicon track: -(id + 1), never -1 ("none").
    static int trackIdForLexicon(int lexiconId) { return -(lexiconId + 1); }
    static int lexiconIdForTrack(int trackId) { return -trackId - 1; }
    static bool isLexiconTrack(int trackId) { return trackId < -1 && trackId > -1000000000; }
};
}
