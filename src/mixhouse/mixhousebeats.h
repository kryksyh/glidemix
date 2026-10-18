/*
 * MixHouse DJ 2
 *
 * Decodes the beatgrid stored in Mixxx's library.beats column.
 *
 * That column is a serialised protobuf (mixxx/src/proto/beats.proto):
 *
 *     Beat     { int32 frame_position = 1; bool enabled = 2; Source source = 3; }
 *     Bpm      { double bpm = 1; Source source = 2; }
 *     BeatMap  { repeated Beat beat = 1; }            // variable tempo
 *     BeatGrid { Bpm bpm = 1; Beat first_beat = 2; }  // constant tempo
 *
 * The wire format is walked by hand rather than by linking protobuf: the schema is
 * four tiny messages, and adding a protobuf dependency to Audacity to read 16 bytes
 * would not be a fair trade.
 *
 * Units confirmed against Mixxx's own reader (track/beats.cpp,
 * fromBeatGridByteArray): frame_position is an audio::FramePos, so seconds =
 * frames / sampleRate. It CAN BE NEGATIVE -- a grid may extrapolate its first beat
 * to before the start of the file, and protobuf sign-extends a negative int32 into
 * a ten-byte varint. Real values seen in the reference library include -88 and -145.
 */
#pragma once

#include <QByteArray>
#include <QString>
#include <vector>

namespace au::mixhouse {
struct MixHouseBeatGrid {
    bool valid = false;
    bool constantTempo = true;   // false = decoded from a BeatMap
    double bpm = 0.0;
    double firstBeatSec = 0.0;   // may be negative
    double beatLengthSec = 0.0;

    /// Explicit beat positions, only for a variable-tempo BeatMap. Empty for the
    /// constant-tempo case, where beatSec(n) is exact and cheaper.
    std::vector<double> beatSecs;

    double beatSec(int n) const { return firstBeatSec + n * beatLengthSec; }

    /// Index of the beat at or immediately before `sec` (may be negative).
    int beatIndexAt(double sec) const;
};

/// `beatsVersion` is library.beats_version, e.g. "BeatGrid-2.0" or "BeatMap-2.0".
MixHouseBeatGrid decodeBeats(const QByteArray& blob, const QString& beatsVersion, int sampleRate);

/// The write-side counterpart, for the analysis engine (mixhouseanalyzer.h):
/// encodes a real `BeatGrid{ Bpm{bpm}, Beat{first_beat} }` message (see the
/// real proto/beats.proto quoted above) for library.beats. `source`/
/// `enabled` fields are deliberately left unset -- their real proto2
/// defaults (ANALYZER, true) are exactly what a fresh analysis should record,
/// and no reader here (or, as far as this port goes, in real mixxx) treats
/// an explicit default differently from an absent field. Pair with
/// `library.beats_version = "BeatGrid-2.0"` when writing the row (the real
/// version string decodeBeats() itself checks for "BeatMap"/anything else).
QByteArray encodeBeatGrid(double bpm, double firstBeatSec, int sampleRate);
}
