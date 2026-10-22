/*
 * GlideMix: the energy rating (1-10) of a song, like Mixed In Key's and
 * Lexicon's -- from whole-track features the analyser measures (absolute
 * loudness, band balance, kick and hi-hat activity, tempo). The weights were
 * fitted against Lexicon's ratings of real DJ collections.
 */
#pragma once

#include <vector>

namespace au::mixhouse {
/// 1.0-10.0, or 0 when the features are missing.
double rateEnergy(const std::vector<float>& features);
}
