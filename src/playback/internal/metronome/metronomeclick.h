/*
 * GlideMix
 */
#pragma once

#include <vector>

namespace au::playback {
/// The metronome click, SYNTHESISED rather than sampled -- copied from the
/// parked MixHouse DJ app (src/mixxxmeister/metronomeclick.cpp), where we
/// tuned it: an Ableton-style tick, loud enough for a club mix, with Logic's
/// Klopfgeist controls (tuning, tonality, damp). Generating it keeps us clear
/// of shipping anyone else's click sample.
///
/// `voice` is IMetronome::Voice; `accent` selects the downbeat click (an octave
/// or a fifth above, depending on the voice).
std::vector<float> renderMetronomeClick(int sampleRate, bool accent, int voice, int semis, int tonalityPct, int dampPct);
}
