/*
 * MixHouse DJ 2
 *
 * Decodes the library's "Waveform Overview" column data. That data isn't in
 * mixxxdb.sqlite (library.wavesummaryhex is null on every row) -- it lives in
 * a separate per-track analysis file, one zlib-compressed proto/waveform.proto
 * message per track_analysis row (see IMixHouseLibrary::overviewAnalysisBlob(),
 * which resolves the row and does the zlib qUncompress() step; this file only
 * decodes the protobuf bytes that come back from that).
 *
 * The relevant slice of the real message (proto/waveform.proto):
 *
 *     message Signal {
 *         repeated int32 value = 1;               // NOT [packed=true] -- unpacked wire encoding
 *         optional int32 channels = 2 [default = 2];
 *         ...
 *     }
 *     message FilteredSignal {
 *         optional Signal low = 1; optional Signal mid = 2; optional Signal high = 3;
 *         ...
 *     }
 *     message Waveform {
 *         optional Signal signal_all = 3;
 *         optional FilteredSignal signal_filtered = 4;
 *         ...
 *     }
 *
 * Hand-rolled for the same reason mixhousebeats.cpp's beats.proto reader is:
 * four fields don't justify a real protobuf dependency. `signal_all` drives
 * the waveform's height (the broadband envelope); `signal_filtered.low/mid/
 * high` drive its color -- mixxx's own real split, not something invented
 * here (see [[mixhouse-waveform-overview-research]]).
 */
#pragma once

#include <QByteArray>
#include <cstdint>
#include <vector>

namespace au::mixhouse {
struct MixHouseWaveformOverview {
    bool valid = false;
    std::vector<uint8_t> all;            // signal_all.value
    std::vector<uint8_t> low, mid, high; // signal_filtered.{low,mid,high}.value
    /// Per-column display colour, 0xRRGGBB, ONE entry per visual column (not
    /// interleaved like the vectors above). Only set when the overview comes
    /// from rekordbox, whose analysis stores each column's finished colour;
    /// empty for Mixxx-style analysis, which only has band levels.
    std::vector<uint32_t> rgb;
    /// True for rekordbox's PWV4 colour preview. Its low/mid/high vectors then
    /// hold the raw band bytes (0 to 127) doubled to fit 8 bits, and
    /// resampleOverview() derives each column's colour and height from the
    /// AVERAGE of the bands over the column's window rather than from any one
    /// stored column: neighbouring columns of a loud passage flip between very
    /// different colours, and rekordbox's own smooth preview is the average.
    bool rekordboxPreview = false;
};

/// One resampled column of the rendered waveform: the max value in each
/// channel within that column's window (mixxx's own real "max across the
/// visual samples in this pixel" rule), after also collapsing the two
/// interleaved audio channels (L/R) by taking their max first.
struct MixHouseOverviewBucket {
    uint8_t low = 0, mid = 0, high = 0, all = 0;
    /// The stored display colour of the tallest column in this bucket's
    /// window, 0xRRGGBB. Meaningful only when hasColor (rekordbox-sourced).
    uint32_t color = 0;
    bool hasColor = false;
};

/// Decodes an ALREADY-DECOMPRESSED Waveform protobuf message (the bytes
/// IMixHouseLibrary::overviewAnalysisBlob() returns after its own zlib
/// qUncompress() step). `valid` is false if the bytes are empty, don't parse,
/// or have no signal_all data at all.
MixHouseWaveformOverview decodeWaveformOverview(const QByteArray& protobufBytes);

/// Resamples a decoded overview down to exactly `bucketCount` columns for
/// rendering. Returns an empty vector if `overview` isn't valid or
/// `bucketCount` isn't positive.
std::vector<MixHouseOverviewBucket> resampleOverview(const MixHouseWaveformOverview& overview, int bucketCount);

/// The write-side counterpart to decodeWaveformOverview() -- encodes a
/// mono amplitude envelope plus the real mono 3-band split (see
/// MixHouseAnalysisResult::waveformOverview/Low/Mid/High and
/// WaveformBandCollector's own comment for how these are computed) into a
/// real Waveform protobuf message (signal_all AND signal_filtered), then
/// qCompress()es it into the same container format overviewAnalysisBlob()
/// already qUncompress()es on read. Every entry is written twice in a row
/// to satisfy resampleOverview()'s real, load-bearing "2 interleaved
/// channels per visual sample" assumption -- this data is genuinely mono
/// (a disclosed simplification, see WaveformBandCollector), but that reader
/// also serves real mixxx-written files and must not be changed to
/// accommodate it. `low`/`mid`/`high` may be empty (an older caller, or a
/// track analyzed before this existed) -- signal_filtered is then omitted
/// entirely, which the render code already renders as solid black.
QByteArray encodeWaveformOverview(const std::vector<uint8_t>& monoAmplitude,
                                   const std::vector<uint8_t>& low = {},
                                   const std::vector<uint8_t>& mid = {},
                                   const std::vector<uint8_t>& high = {});
}
