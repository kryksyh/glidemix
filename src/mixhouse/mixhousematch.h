/*
 * GlideMix
 *
 * Match and Related: which songs mix with a reference song (the one under the
 * playhead). General DJ rules (harmonic keys on the key wheel, BPM within a
 * few percent, half and double tempo counting), written for GlideMix; the
 * idea of lighting keys in the list and a related-songs panel was learnt from
 * other DJ apps, nothing copied.
 *
 * Keys are Mixxx key ids: 1..12 major C..B, 13..24 minor C..B; 0 = unknown.
 */
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>

namespace au::mixhouse::match {
//! How far a key match reaches around the reference key (8A as example).
enum class KeyWidth {
    Same = 0,        ///< 8A
    Relative = 1,    ///< 8A, 8B
    OneStep = 2,     ///< 8A, 8B, 7A, 9A (default)
    Diagonal = 3,    ///< 8A, 8B, 7A, 9A, 7B, 9B
};

//! A key's place on the wheel: number 1..12 and minor (A) or major (B).
struct WheelPos {
    int number = 0;
    bool minor = false;
    bool valid() const { return number >= 1 && number <= 12; }
};

inline WheelPos wheelPos(int keyId)
{
    if (keyId < 1 || keyId > 24) {
        return {};
    }
    const bool major = keyId <= 12;
    const int tonic = keyId - (major ? 1 : 13);   // 0 = C .. 11 = B
    // Open Key number: C major = 1d, A minor = 1m; one fifth = one step.
    // Relative minor of a major key is 3 semitones lower.
    const int fromC = major ? tonic : (tonic + 3) % 12;    // the major key it shares notes with
    const int fifths = (fromC * 7) % 12;                  // circle of fifths position, C = 0
    // Wheel number: C major = 8B, A minor = 8A.
    int number = (fifths + 7) % 12 + 1;
    return { number, !major };
}

//! Steps between two wheel numbers, 0..6 (the wheel wraps 12 -> 1).
inline int wheelDistance(int a, int b)
{
    const int d = std::abs(a - b) % 12;
    return d > 6 ? 12 - d : d;
}

//! Whether a song in `keyId` mixes with a reference in `refKeyId`.
inline bool keysMix(int refKeyId, int keyId, KeyWidth width)
{
    const WheelPos ref = wheelPos(refKeyId);
    const WheelPos key = wheelPos(keyId);
    if (!ref.valid() || !key.valid()) {
        return false;
    }
    const int steps = wheelDistance(ref.number, key.number);
    const bool sameLetter = ref.minor == key.minor;
    switch (width) {
    case KeyWidth::Same:
        return steps == 0 && sameLetter;
    case KeyWidth::Relative:
        return steps == 0;
    case KeyWidth::OneStep:
        return steps == 0 || (steps == 1 && sameLetter);
    case KeyWidth::Diagonal:
        return steps <= 1;
    }
    return false;
}

//! Whether `bpm` is within `percent` of `refBpm` (and, with `halfDouble`,
//! of half or double it). An unknown BPM (<= 0) matches nothing.
inline bool bpmWithin(double refBpm, double bpm, double percent, bool halfDouble)
{
    if (refBpm <= 0.0 || bpm <= 0.0) {
        return false;
    }
    const double frac = percent / 100.0;
    auto near = [&](double centre) { return bpm >= centre * (1.0 - frac) && bpm <= centre * (1.0 + frac); };
    return near(refBpm) || (halfDouble && (near(refBpm * 0.5) || near(refBpm * 2.0)));
}

//! How far a BPM is from the reference, as a fraction (half/double folded in);
//! for sorting the closest first.
inline double bpmDistance(double refBpm, double bpm)
{
    if (refBpm <= 0.0 || bpm <= 0.0) {
        return 1e9;
    }
    double best = std::abs(bpm - refBpm) / refBpm;
    best = std::min(best, std::abs(bpm - refBpm * 0.5) / (refBpm * 0.5));
    best = std::min(best, std::abs(bpm - refBpm * 2.0) / (refBpm * 2.0));
    return best;
}
}
