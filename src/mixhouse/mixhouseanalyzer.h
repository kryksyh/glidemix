/*
 * MixHouse DJ 2
 *
 * Roadmap Phase 7: BPM/beatgrid and musical-key detection for tracks that
 * show up after today (anything already in the reference library was already
 * analyzed by real Mixxx -- this is for new tracks, or an explicit
 * (re)analyze request).
 *
 * BPM/beat detection is a real port of real mixxx's own DEFAULT analysis
 * engine -- analyzer/plugins/analyzerqueenmarybeats.cpp, a thin wrapper
 * around the Queen Mary DSP library (lib/qm-dsp, vendored verbatim at
 * src/mixhouse/thirdparty/qm-dsp/, GPL-2.0) -- NOT the optional neural
 * "Beat This!" ONNX model, which is OFF by default even in real mixxx and
 * needs ~150-250MB of extra fetched dependencies (ONNX Runtime, a whole
 * separate C++ library, three more helper libs). Queen Mary IS what
 * actually runs for every real mixxx user who hasn't opted into that.
 * Full research trail in the [[mixhouse-analysis-engine-research]] memory.
 *
 * Musical-key detection does NOT use mixxx's own default (Queen Mary's
 * GetKeyMode, also qm-dsp) -- we found it producing wrong keys on his
 * real library and asked for OpenKeyScan (github.com/rekordcloud/
 * openkeyscan-analyzer, MIT), a convolutional neural network, in its place.
 * See OpenKeyScanClient's own header comment for how that integration
 * works (a persistent Python subprocess, not an in-process port) and why.
 *
 * Decodes via this app's own already-proven mixhouseaudiosource.h (not
 * mixxx's SoundSourceProxy), then feeds the SAME decoded audio to every
 * IN-PROCESS sub-analyzer in one pass -- matching real mixxx's
 * AnalyzerThread, where one shared decode feeds every active sub-analyzer's
 * processSamples(). As of this file, that's three: BPM/beat, a whole-track
 * amplitude envelope, and the real 3-band (low/mid/high) frequency split
 * that drives the library's RGB Waveform Overview column -- real mixxx's
 * own analyzer/analyzerwaveform.cpp, ported with two disclosed
 * simplifications (see WaveformBandCollector's own comment). Key detection
 * runs separately, by file path, over the OpenKeyScan subprocess.
 */
#pragma once

#include <QMetaType>
#include <QString>

#include "mixhouse/imixhouserekordboxphrases.h"
#include <cstdint>
#include <functional>
#include <vector>

namespace au::mixhouse {
/// Real mixxx's own analyzer/analyzerprogress.h scale, copied verbatim:
/// decoding maps linearly onto [None, Finalizing), the post-decode work
/// (beat finalize + key detection) sits at Finalizing, Done is only ever
/// reported by the caller once the result is actually written.
constexpr double kMixHouseAnalyzerProgressNone = 0.0;
constexpr double kMixHouseAnalyzerProgressFinalizing = 0.95;
constexpr double kMixHouseAnalyzerProgressDone = 1.0;

/// Called from whichever thread runs analyzeTrackFile(), once per decoded
/// chunk -- throttling is the receiver's job (see MixHouseAnalyzerWorker).
using MixHouseAnalyzerProgressFn = std::function<void (double progress)>;

struct MixHouseAnalysisResult {
    bool valid = false;
    double bpm = 0.0;
    double firstBeatSec = 0.0;   ///< anchor for a constant-tempo grid
    int keyId = 0;               ///< ChromaticKey enum value; 0 = none detected
    int sampleRate = 0;
    double durationSec = 0.0;    ///< Header duration; available before DSP analysis
    QString error;

    /// A whole-track amplitude envelope from the SAME decode pass beats/key
    /// already ran -- 1920 entries (0-255), real mixxx's own fixed Overview-
    /// widget resolution regardless of track length (signal_all).
    std::vector<uint8_t> waveformOverview;

    /// The real 3-band frequency split (signal_filtered.low/mid/high) --
    /// real mixxx's own analyzer/analyzerwaveform.cpp: independent 600Hz
    /// lowpass / 600-4000Hz bandpass / 4000Hz highpass filters, each 4th
    /// order, RMS per bucket (not real mixxx's own peak rule -- see
    /// WaveformBandCollector's own comment for why) exactly like
    /// waveformOverview above. See analyzeTrackFile()'s own
    /// WaveformBandCollector for the two
    /// disclosed simplifications (a textbook Butterworth cascade instead of
    /// real mixxx's Bessel pole table; mono instead of real per-channel
    /// stereo) -- same crossover frequencies and filter order either way.
    /// Empty under the exact same condition as waveformOverview (no frame
    /// count up front), and always the same length as it when non-empty.
    std::vector<uint8_t> waveformLow;
    std::vector<uint8_t> waveformMid;
    std::vector<uint8_t> waveformHigh;

    /// GlideMix phrase model input: per beat of the grid (bpm, firstBeatSec),
    /// the loudness in dB of three bands -- sub/kick (<150 Hz), low-mid
    /// (150-2000 Hz), high (>2 kHz) -- relative to the track's own 95th
    /// percentile, 3 values per beat. Empty with no grid.
    std::vector<float> beatBandEnergy;

    /// GlideMix energy (1-10): absolute loudness, band balance, kick and
    /// hi-hat activity of the whole track (mixhouseenergy.h), and the energy
    /// rated from them; 0 = not measured.
    std::vector<float> energyFeatures;
    double energy = 0.0;

    /// The phrase model's phrases for this grid (mixhousephrasemodel.h);
    /// empty without the model.
    std::vector<MixHousePhrase> modelPhrases;

    /// Real downbeat (beat 1 of a bar), seconds from the start of the file,
    /// or -1 if none was detected. From the "Beat This!" neural tracker
    /// (thirdparty/beat_this_cpp): every downbeat it finds votes for which
    /// beat of the Queen Mary grid above starts a bar, and this is the first
    /// grid beat with the winning phase -- so it always sits exactly on the
    /// grid when bpm > 0. Mixxx's own grid has no downbeat at all; its first
    /// beat is only a guess (wrong on any track with a pickup).
    double downbeatSec = -1.0;
    /// Share of detected downbeats that agreed with the winning bar phase
    /// (0..1); 0 when there was no grid to vote against.
    double downbeatConfidence = 0.0;
    /// Which method set downbeatSec, stored with it in mixhouse_downbeats.
    QString downbeatDetector;
};

/// Runs real mixxx's own default BPM/beat + key detection algorithms
/// against the audio file at `filePath`. `valid` is false (with `error` set)
/// if the file can't be decoded; a successfully-analyzed track can still
/// have `bpm == 0`/`keyId == 0` if the detectors genuinely found nothing
/// (near-silent audio, spoken word, etc.) -- that's a real result, not a
/// failure. `onProgress` (optional) receives mixxx's AnalyzerThread-style
/// per-track progress -- see kMixHouseAnalyzerProgress* above.
/// A model file shipped with the app (beat_this.onnx, the phrase model), or
/// empty if missing. Developer builds fall back to `devPath`.
QString mixHouseModelPath(const QString& fileName, const QString& devPath = QString());

/// GlideMix: what another app (rekordbox, Lexicon) already knows about a
/// file. A grid (bpm > 0) skips the beat network; a key skips key detection.
struct MixHouseKnownAnalysis {
    double bpm = 0.0;
    double firstBeatSec = 0.0;
    double downbeatSec = -1.0;
    int keyId = 0;
    QString source;   ///< stored as the downbeat detector, e.g. "rekordbox"
};

MixHouseAnalysisResult analyzeTrackFile(const QString& filePath,
                                        const MixHouseAnalyzerProgressFn& onProgress = {},
                                        const MixHouseKnownAnalysis& known = {});

/// On by default. Developer tools that need no key (the overnight trainer's
/// collection pass) switch it off to save the OpenKeyScan round trip.
void setMixHouseKeyDetectionEnabled(bool enabled);
/// GlideMix: false (default) runs Beat This!'s small model -- 1.7x faster,
/// tempo the same, beats about 2% less exact; true the full model.
void setMixHouseAccurateBeatAnalysis(bool accurate);

}
// Registered so this can cross threads as a QueuedConnection signal argument
// -- MixHouseAnalyzerWorker::analyzed() carries one of these from a worker
// thread back to the main thread (see that class's own header comment for
// why the worker computes this and nothing else touches SQL at all).
Q_DECLARE_METATYPE(au::mixhouse::MixHouseAnalysisResult)
