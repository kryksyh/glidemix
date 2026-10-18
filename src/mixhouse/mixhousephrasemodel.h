/*
 * GlideMix
 *
 * The GlideMix Phrase Detector: a small network that names each bar of a
 * track (Intro, Up/Verse, Down/Bridge, Chorus, Outro), marks where phrases
 * start, and picks the name set for the track's style. Trained on ~2,000
 * DJ-labelled tracks of every genre and tested on held-out folders: phrase
 * starts within 0.5 s 59% (the loudness detector: 17%), names 70% (60%).
 * Its inputs come from the analysis: the beat grid, each beat's band loudness
 * (MixHouseAnalysisResult::beatBandEnergy) and the loudness detector's own
 * phrases. Model file: glidemix_phrases.onnx (models/).
 */
#pragma once

#include <vector>

#include "mixhouse/imixhouserekordboxphrases.h"

namespace au::mixhouse {
//! Number of inputs per bar; must match the trainer (p2_app.py FEATURES).
constexpr int kPhraseModelFeatures = 12 + 3 + 5 + 2 + 1;

//! One row of kPhraseModelFeatures per bar, and each bar's start time.
//! `beatBandEnergy` is 3 values per beat of the grid (bpm, firstBeatSec);
//! `builtIn` is the loudness detector's phrases for the same grid.
struct PhraseModelInput {
    std::vector<float> rows;
    std::vector<double> barStarts;
    double barSec = 0.0;
};
PhraseModelInput phraseModelInput(const std::vector<float>& beatBandEnergy, double bpm, double firstBeatSec,
                                  double downbeatSec, const std::vector<MixHousePhrase>& builtIn);

//! The model's phrases, or empty when the model file or ONNX Runtime is
//! missing or the track is too short. Thread-safe.
std::vector<MixHousePhrase> modelPhrases(const std::vector<float>& beatBandEnergy, double bpm, double firstBeatSec,
                                         double downbeatSec, double durationSec,
                                         const std::vector<MixHousePhrase>& builtIn);
}
