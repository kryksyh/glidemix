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
// regression fitted on 606 songs Lexicon had rated 1-10, about evenly per
// level: on a collection rated mostly 6-8 it lands within 1 of Lexicon's
// rating for 94% of songs (5-fold cross-validated, 2026-10-22).
constexpr int kFeatures = 12;
constexpr double kBias = 5.83333;
constexpr double kWeights[2 * kFeatures] = {
    2.45181, -0.303244, -2.87613, 1.44164, -0.138633, -0.329936, -0.703038, -2.37202, 0.732945, 0.554527, 1.33036,
    -0.988504, 0.697766, -0.133501, -1.61488, 0.715481, -0.32418, 0.233357, 1.05172, 1.60061, -0.810115, -0.066514,
    -1.20579, 0.163224,
};
constexpr double kMean[2 * kFeatures] = {
    -12.7623, -3.44239, -4.92837, -12.3196, 0.497685, 17.2311, 2.61498, 1.83762, 5.18184, 3.46758, 136.745, -8.17647,
    173.885, 25.291, 30.915, 164.94, 0.266903, 358.419, 7.17818, 3.94031, 31.4602, 17.4312, 19226.8, 77.5429,
};
constexpr double kScale[2 * kFeatures] = {
    3.31784, 3.66619, 2.57412, 3.62887, 0.13861, 7.84264, 0.583151, 0.750634, 2.14678, 2.32531, 22.9721, 3.26929,
    97.8229, 94.999, 26.9273, 118.942, 0.144475, 610.02, 3.99291, 4.03393, 31.5955, 24.4319, 6548.21, 66.4342,
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
