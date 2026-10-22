/*
 * GlideMix: cue points made from a song's phrases -- a hot cue (A..H) where
 * each section starts (Intro, Up, Chorus, Down, Outro...), named and coloured
 * like the phrase, plus a memory cue at the same place, so the song arrives
 * on CDJs ready to play (the rekordbox XML export). Like Lexicon's Cue Point
 * Generator, from rekordbox's phrases or GlideMix's own.
 */
#pragma once

#include <vector>

#include "imixhouseexternallibrary.h"
#include "imixhouserekordboxphrases.h"

namespace au::mixhouse {
/// The section a phrase belongs to: its label without rekordbox's variant
/// number ("Intro 2" -> "Intro", "Verse 1" -> "Verse").
QString phraseSection(const QString& label);

/// Hot cues A.. at the start of each section (at most `maxHotCues`; when a
/// song has more sections the shortest ones in the middle are left out),
/// then a memory cue at each of them. Empty for no phrases.
/// With a beat grid (beatLengthSec > 0) every cue sits on its nearest beat,
/// and a section under two bars joins the next one.
std::vector<MixHouseCue> phraseCues(const std::vector<MixHousePhrase>& phrases, double firstBeatSec = 0.0,
                                    double beatLengthSec = 0.0, int maxHotCues = 8);
}
