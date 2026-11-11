/*
 * GlideMix: "Sounds like" -- a small sound fingerprint of a song, so Related
 * (and later the Samples tab) can find songs that simply sound alike, next to
 * key and tempo.
 *
 * Measured on the song's main parts (its loud sections, not the intro,
 * breakdowns or outro): timbre (MFCC c1..c13, mean and spread), brightness
 * (spectral centroid), bass weight (share of five bands), noisiness
 * (spectral flatness), how much of the song is "main", and groove (kick and
 * hat activity from the energy features). Compared with a distance over
 * library-normalised values, see soundsLikeDistance().
 */
#pragma once

#include <memory>
#include <vector>

namespace au::mixhouse {
/// Stored with each fingerprint; a higher number means older ones are
/// measured again in the background.
constexpr int kSoundsLikeVersion = 1;

/// Number of values in a fingerprint (see the index list in the .cpp).
constexpr int kSoundsLikeSize = 42;

/// Fed the same interleaved float chunks as the other analysis collectors.
class SoundsLikeCollector
{
public:
    SoundsLikeCollector();
    ~SoundsLikeCollector();

    void initialize(int channels, int sampleRate);
    void processSamples(const float* samples, long long frames);

    /// kSoundsLikeSize values, or empty for silence / under ~10 s of sound.
    /// `energyFeatures` (MixHouseAnalysisResult::energyFeatures) gives the
    /// groove part; missing ones count as 0.
    std::vector<float> finalize(const std::vector<float>& energyFeatures) const;

private:
    struct Impl;
    std::unique_ptr<Impl> m;
};

/// Packs / unpacks a fingerprint for the library DB (little-endian float32).
std::vector<float> decodeSoundsLike(const void* data, int bytes);

/// All fingerprints of a library, ready to compare. Each value is scaled by
/// its spread over the whole library (z-score), and each group of values
/// (timbre, timbre spread, brightness, bands, dynamics, groove) weighs the
/// same in the distance, as measured in the halves test (HANDOFF A1).
class SoundsLikeIndex
{
public:
    void clear();
    /// Replaces the set; ids without a valid fingerprint are left out.
    void load(const std::vector<std::pair<int, std::vector<float> > >& fingerprints);
    bool contains(int trackId) const;
    int size() const { return static_cast<int>(m_ids.size()); }

    /// How alike each of `trackIds` sounds to `refId`, 0-100: the share of
    /// the whole library that sounds LESS alike (100 = the closest song of
    /// all). -1 for an id without a fingerprint. Empty when `refId` has none.
    std::vector<double> percentAlike(int refId, const std::vector<int>& trackIds) const;

private:
    double distance(size_t a, size_t b) const;
    std::vector<int> m_ids;
    std::vector<float> m_values;   // normalised, kSoundsLikeSize per song
    std::vector<int> m_rowOf;      // track id -> row, -1 = none (dense: ids are small)
};
}
