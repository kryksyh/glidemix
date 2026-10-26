/*
 * GlideMix
 *
 * Grid jumps: places where a song's beats move off its (constant) beat grid
 * and stay off -- a demo stitched from loops, an edit, a live recording that
 * was cut. One grid fits only one side of the jump, so from there on the
 * metronome and the other songs flam against the kicks.
 */
#pragma once

#include <cstddef>
#include <vector>

namespace au::mixhouse {
struct MixHouseGridShift {
    double startSec = 0.0;   ///< file time of the grid bar line where the new section starts
    double shiftSec = 0.0;   ///< from there on the beats are this much late (negative: early), against the grid
};

/// Kick onsets (below 150 Hz) of `mono` compared with the grid `bpm` /
/// `barOneSec` (any bar 1, file seconds), one median per bar, then fitted
/// with as few constant pieces as explain them. A piece of at least
/// kMinShiftBars bars that sits steadily (within a few ms) at least 20 ms
/// away from the section before it is a jump. The offsets are relative to
/// the first steady section, which is where the grid itself fits.
std::vector<MixHouseGridShift> detectGridShifts(const float* mono, size_t frames, int sampleRate,
                                                double bpm, double barOneSec, int beatsPerBar = 4);
}
