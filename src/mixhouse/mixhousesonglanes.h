/*
 * GlideMix Samples: the lanes that hold songs -- every lane but the sample
 * lines. Songs, transitions, the tempo line, the song counter and row picking
 * look at these only: a sample is never taken for a song.
 */
#pragma once

#include <vector>

#include "trackedit/itrackeditproject.h"

namespace au::mixhouse {
inline std::vector<trackedit::TrackId> songTrackIds(const trackedit::ITrackeditProject& project)
{
    std::vector<trackedit::TrackId> out;
    for (const trackedit::Track& track : project.trackList()) {
        if (!trackedit::isSampleLine(track)) {
            out.push_back(track.id);
        }
    }
    return out;
}

/// A stem lane made by Separate stems ("Drums — <song>"): part of its song,
/// not a song of its own (the SONG counter skips it).
inline bool isStemLane(const trackedit::Track& track)
{
    static const char16_t* kPrefixes[] = { u"Drums \u2014 ", u"Bass \u2014 ", u"Vocals \u2014 ", u"Other \u2014 " };
    for (const char16_t* prefix : kPrefixes) {
        if (track.title.startsWith(muse::String(prefix))) {
            return true;
        }
    }
    return false;
}

/// The lanes the SONG counter counts: songs only, no sample lines, no stems.
inline std::vector<trackedit::TrackId> countedSongTrackIds(const trackedit::ITrackeditProject& project)
{
    std::vector<trackedit::TrackId> out;
    for (const trackedit::Track& track : project.trackList()) {
        if (!trackedit::isSampleLine(track) && !isStemLane(track)) {
            out.push_back(track.id);
        }
    }
    return out;
}
}
