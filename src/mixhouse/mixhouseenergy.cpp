/*
 * GlideMix
 */
#include "mixhouseenergy.h"

#include <algorithm>
#include <cmath>

namespace au::mixhouse {
namespace {
// MixHouseAnalysisResult::energyFeatures, in order (see the analyser's
// PhraseEnergyCollector::energyFeatures), then their squares. A ridge
// regression fitted on 696 songs Lexicon had rated 1-10, about evenly per
// level with extra 8s and 9s: on a collection rated mostly 6-8 it lands
// within 1 of Lexicon's rating for 89% of songs, Lexicon's 9s for 77%
// (5-fold cross-validated, 2026-10-24). Stretching the top of the scale
// was tried and made the many 7s and 8s worse: these features can't tell
// an 8 from a 9 much better than this.
constexpr int kFeatures = 12;
constexpr double kBias = 6.22126;
constexpr double kWeights[2 * kFeatures] = {
    1.9152, -0.198077, -3.09271, 1.40631, 0.343263, -0.00549432, -1.10184, -2.03235, 0.714121, 0.424266, 1.66452,
    -0.479636, 1.26724, 0.0424537, -1.72841, 0.687388, -0.543037, -0.166407, 1.3565, 1.23378, -0.628182, 0.0367208,
    -1.51484, -0.339126,
};
constexpr double kMean[2 * kFeatures] = {
    -12.6596, -3.10234, -5.22625, -12.1624, 0.478487, 17.3484, 2.64702, 1.83701, 5.28795, 3.49926, 137.022,
    -7.96078, 171.515, 19.066, 33.6754, 160.695, 0.24636, 336.165, 7.31304, 3.95725, 32.1155, 17.6197,
    19237.6, 74.2522,
};
constexpr double kScale[2 * kFeatures] = {
    3.35385, 3.0727, 2.52223, 3.57364, 0.131948, 5.93276, 0.553463, 0.763318, 2.03791, 2.31838, 21.5079,
    3.29821, 103.237, 62.2591, 26.4211, 115.661, 0.132888, 297.763, 3.90087, 4.54044, 30.484, 25.139,
    6135.28, 70.257,
};
}

double rateEnergy(const std::vector<float>& features)
{
    if (static_cast<int>(features.size()) < kFeatures) {
        return 0.0;
    }
    double e = kBias;
    for (int i = 0; i < kFeatures; ++i) {
        const double x = features[i];
        e += kWeights[i] * (x - kMean[i]) / kScale[i];
        e += kWeights[kFeatures + i] * (x * x - kMean[kFeatures + i]) / kScale[kFeatures + i];
    }
    return std::clamp(e, 1.0, 10.0);
}
}
