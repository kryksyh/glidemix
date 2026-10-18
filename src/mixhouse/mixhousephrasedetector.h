/*
 * GlideMix
 *
 * Phrases for tracks rekordbox has not analysed: the song's sections (Intro,
 * build-ups, drops, breaks, Outro) found from the analysis GlideMix already
 * keeps -- the overview's loudness and bass envelopes, the tempo and the
 * downbeat. Dance music is built from 8-bar blocks, so the song is cut into
 * 8-bar blocks on its bars, and each block is judged by how loud it is and
 * whether the bass is playing. Named with rekordbox's words so they take its
 * colours: Intro, Up (build-up), Chorus (drop), Down (break), Outro.
 */
#pragma once

#include <vector>

#include "mixhouse/imixhouserekordboxphrases.h"
#include "mixhouse/mixhousewaveform.h"

namespace au::mixhouse {
//! Empty when there is not enough to go on (no tempo, no overview, or a
//! track shorter than a few blocks).
std::vector<MixHousePhrase> detectPhrases(const MixHouseWaveformOverview& overview, double durationSec, double bpm,
                                          double downbeatSec);
}
