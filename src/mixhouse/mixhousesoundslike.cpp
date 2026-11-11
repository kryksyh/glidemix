/*
 * GlideMix: "Sounds like" fingerprint, see mixhousesoundslike.h.
 *
 * Fingerprint index list (kSoundsLikeVersion 1):
 *   0-12  MFCC c1..c13, mean over the main frames
 *   13-25 MFCC c1..c13, standard deviation
 *   26    brightness: spectral centroid, mean, log2(Hz)
 *   27    brightness spread: centroid standard deviation (log2)
 *   28-32 band shares in dB: sub <60 Hz, bass 60-250, low mid 250-2k,
 *         high mid 2k-6k, high >6k
 *   33    noisiness: spectral flatness 100 Hz-8 kHz, mean, dB
 *   34    share of the playing time that is "main" (loud) part
 *   35    loudness spread over the playing time, dB
 *   36-41 groove, from the energy features 4-9: time at full power, dynamic
 *         range, kick pumping, hat activity, kicks per second, hits per second
 */
#include "mixhousesoundslike.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "dsp/mfcc/MFCC.h"
#include "dsp/transforms/FFT.h"

namespace au::mixhouse {
namespace {
constexpr int kCeps = 13;
constexpr int kPerFrame = 1 + kCeps + 1 + 5 + 1;   // rms dB, mfcc, centroid, 5 bands, flatness
constexpr double kFramesPerSecond = 10.0;          // one analysis frame every 100 ms
constexpr double kSilenceDb = -60.0;
constexpr double kMainBelowTopDb = 8.0;            // main part: within 8 dB of the loud level
constexpr double kBandEdges[] = { 60.0, 250.0, 2000.0, 6000.0 };
constexpr double kPi = 3.14159265358979323846;

double dbPower(double p) { return 10.0 * std::log10(p + 1e-12); }
}

struct SoundsLikeCollector::Impl {
    int channels = 1;
    int sampleRate = 0;
    int frameSize = 2048;
    long long hop = 4410;
    long long pos = 0;             // samples into the current hop
    std::vector<double> frame;
    std::vector<double> window;
    std::vector<double> re, im;
    std::unique_ptr<FFTReal> fft;
    std::unique_ptr<MFCC> mfcc;
    std::vector<float> frames;     // kPerFrame per analysed frame

    void analyseFrame()
    {
        double sumSq = 0.0;
        for (int i = 0; i < frameSize; ++i) {
            sumSq += frame[i] * frame[i];
        }
        const double rmsDb = dbPower(sumSq / frameSize);
        std::vector<double> windowed(frameSize);
        for (int i = 0; i < frameSize; ++i) {
            windowed[i] = frame[i] * window[i];
        }
        fft->forward(windowed.data(), re.data(), im.data());

        double ceps[kCeps + 1] = {};
        mfcc->process(re.data(), im.data(), ceps);

        const int bins = frameSize / 2;
        const double binHz = static_cast<double>(sampleRate) / frameSize;
        double total = 0.0, weighted = 0.0;
        double band[5] = {};
        double logSum = 0.0, linSum = 0.0;
        int flatBins = 0;
        for (int k = 1; k < bins; ++k) {
            const double p = re[k] * re[k] + im[k] * im[k];
            const double hz = k * binHz;
            total += p;
            weighted += p * hz;
            int b = 0;
            while (b < 4 && hz >= kBandEdges[b]) {
                ++b;
            }
            band[b] += p;
            if (hz >= 100.0 && hz <= 8000.0) {
                logSum += std::log(p + 1e-20);
                linSum += p;
                ++flatBins;
            }
        }
        frames.push_back(static_cast<float>(rmsDb));
        for (int c = 0; c < kCeps; ++c) {
            frames.push_back(static_cast<float>(ceps[c]));
        }
        const double centroid = total > 0.0 ? weighted / total : 0.0;
        frames.push_back(static_cast<float>(std::log2(std::max(20.0, centroid))));
        for (double v : band) {
            frames.push_back(static_cast<float>(total > 0.0 ? v / total : 0.0));
        }
        double flatness = 0.0;
        if (flatBins > 0 && linSum > 0.0) {
            flatness = std::exp(logSum / flatBins) / (linSum / flatBins);
        }
        frames.push_back(static_cast<float>(flatness));
    }
};

SoundsLikeCollector::SoundsLikeCollector()
    : m(std::make_unique<Impl>())
{
}

SoundsLikeCollector::~SoundsLikeCollector() = default;

void SoundsLikeCollector::initialize(int channels, int sampleRate)
{
    m->channels = std::max(1, channels);
    m->sampleRate = std::max(1, sampleRate);
    // About 46 ms per frame at any rate.
    m->frameSize = sampleRate > 60000 ? 4096 : 2048;
    m->hop = std::max<long long>(m->frameSize, std::llround(sampleRate / kFramesPerSecond));
    m->pos = 0;
    m->frame.assign(m->frameSize, 0.0);
    m->window.resize(m->frameSize);
    for (int i = 0; i < m->frameSize; ++i) {
        m->window[i] = 0.5 - 0.5 * std::cos(2.0 * kPi * i / (m->frameSize - 1));
    }
    m->re.assign(m->frameSize, 0.0);
    m->im.assign(m->frameSize, 0.0);
    m->fft = std::make_unique<FFTReal>(m->frameSize);
    MFCCConfig config(m->sampleRate);
    config.fftsize = m->frameSize;
    config.nceps = kCeps;
    config.want_c0 = false;   // c0 is loudness; the fingerprint is about sound, not level
    m->mfcc = std::make_unique<MFCC>(config);
    m->frames.clear();
}

void SoundsLikeCollector::processSamples(const float* samples, long long frames)
{
    if (!m->fft) {
        return;
    }
    const int ch = m->channels;
    for (long long i = 0; i < frames; ++i) {
        // Only the first frameSize samples of every hop are looked at.
        if (m->pos < m->frameSize) {
            double sum = 0.0;
            for (int c = 0; c < ch; ++c) {
                sum += samples[i * ch + c];
            }
            m->frame[m->pos] = sum / ch;
            if (m->pos == m->frameSize - 1) {
                m->analyseFrame();
            }
        }
        if (++m->pos >= m->hop) {
            m->pos = 0;
        }
    }
}

std::vector<float> SoundsLikeCollector::finalize(const std::vector<float>& energyFeatures) const
{
    const size_t count = m->frames.size() / kPerFrame;
    const auto at = [&](size_t f, int i) { return static_cast<double>(m->frames[f * kPerFrame + i]); };

    std::vector<size_t> playing;
    std::vector<double> playingDb;
    for (size_t f = 0; f < count; ++f) {
        if (at(f, 0) > kSilenceDb) {
            playing.push_back(f);
            playingDb.push_back(at(f, 0));
        }
    }
    if (playing.size() < 100) {   // under ~10 s of sound
        return {};
    }
    std::vector<double> sorted = playingDb;
    const size_t p90 = static_cast<size_t>(0.9 * (sorted.size() - 1));
    std::nth_element(sorted.begin(), sorted.begin() + p90, sorted.end());
    const double threshold = sorted[p90] - kMainBelowTopDb;

    std::vector<size_t> main;
    for (size_t f : playing) {
        if (at(f, 0) >= threshold) {
            main.push_back(f);
        }
    }
    if (main.size() < 20) {
        main = playing;
    }

    std::vector<float> out;
    out.reserve(kSoundsLikeSize);
    const auto meanStd = [&](int index, double* mean, double* sd) {
        double s = 0.0, s2 = 0.0;
        for (size_t f : main) {
            const double v = at(f, index);
            s += v;
            s2 += v * v;
        }
        *mean = s / main.size();
        *sd = std::sqrt(std::max(0.0, s2 / main.size() - *mean * *mean));
    };
    double means[kCeps], sds[kCeps];
    for (int c = 0; c < kCeps; ++c) {
        meanStd(1 + c, &means[c], &sds[c]);
    }
    for (double v : means) {
        out.push_back(static_cast<float>(v));
    }
    for (double v : sds) {
        out.push_back(static_cast<float>(v));
    }
    double centroidMean = 0.0, centroidSd = 0.0;
    meanStd(1 + kCeps, &centroidMean, &centroidSd);
    out.push_back(static_cast<float>(centroidMean));
    out.push_back(static_cast<float>(centroidSd));
    for (int b = 0; b < 5; ++b) {
        double mean = 0.0, sd = 0.0;
        meanStd(2 + kCeps + b, &mean, &sd);
        out.push_back(static_cast<float>(dbPower(mean)));
    }
    double flatMean = 0.0, flatSd = 0.0;
    meanStd(7 + kCeps, &flatMean, &flatSd);
    out.push_back(static_cast<float>(dbPower(flatMean)));
    out.push_back(static_cast<float>(static_cast<double>(main.size()) / playing.size()));
    double s = 0.0, s2 = 0.0;
    for (double v : playingDb) {
        s += v;
        s2 += v * v;
    }
    const double m0 = s / playingDb.size();
    out.push_back(static_cast<float>(std::sqrt(std::max(0.0, s2 / playingDb.size() - m0 * m0))));
    for (size_t i = 4; i <= 9; ++i) {
        out.push_back(i < energyFeatures.size() ? energyFeatures[i] : 0.0f);
    }
    return out;
}

void SoundsLikeIndex::clear()
{
    m_ids.clear();
    m_values.clear();
    m_rowOf.clear();
}

void SoundsLikeIndex::load(const std::vector<std::pair<int, std::vector<float> > >& fingerprints)
{
    clear();
    std::vector<const std::vector<float>*> rows;
    for (const auto& [id, values] : fingerprints) {
        if (id >= 0 && values.size() == static_cast<size_t>(kSoundsLikeSize)) {
            m_ids.push_back(id);
            rows.push_back(&values);
        }
    }
    const size_t n = rows.size();
    if (n == 0) {
        return;
    }
    // Weight per value: each group weighs the same in total.
    static const int kGroupEnds[] = { 13, 26, 28, 33, 36, kSoundsLikeSize };
    double weight[kSoundsLikeSize];
    int start = 0;
    for (int end : kGroupEnds) {
        for (int i = start; i < end; ++i) {
            weight[i] = 1.0 / (end - start);
        }
        start = end;
    }
    double mean[kSoundsLikeSize] = {}, sd[kSoundsLikeSize] = {};
    for (const auto* r : rows) {
        for (int i = 0; i < kSoundsLikeSize; ++i) {
            mean[i] += (*r)[i];
        }
    }
    for (double& m : mean) {
        m /= n;
    }
    for (const auto* r : rows) {
        for (int i = 0; i < kSoundsLikeSize; ++i) {
            const double d = (*r)[i] - mean[i];
            sd[i] += d * d;
        }
    }
    for (double& s : sd) {
        s = std::sqrt(s / n);
    }
    // Stored already scaled by sqrt(weight): the distance is then a plain sum of squares.
    m_values.resize(n * kSoundsLikeSize);
    for (size_t k = 0; k < n; ++k) {
        for (int i = 0; i < kSoundsLikeSize; ++i) {
            const double z = sd[i] > 1e-9 ? ((*rows[k])[i] - mean[i]) / sd[i] : 0.0;
            m_values[k * kSoundsLikeSize + i] = static_cast<float>(z * std::sqrt(weight[i]));
        }
    }
    const int maxId = *std::max_element(m_ids.begin(), m_ids.end());
    m_rowOf.assign(static_cast<size_t>(maxId) + 1, -1);
    for (size_t k = 0; k < n; ++k) {
        m_rowOf[static_cast<size_t>(m_ids[k])] = static_cast<int>(k);
    }
}

bool SoundsLikeIndex::contains(int trackId) const
{
    return trackId >= 0 && static_cast<size_t>(trackId) < m_rowOf.size() && m_rowOf[static_cast<size_t>(trackId)] >= 0;
}

double SoundsLikeIndex::distance(size_t a, size_t b) const
{
    const float* x = m_values.data() + a * kSoundsLikeSize;
    const float* y = m_values.data() + b * kSoundsLikeSize;
    double sum = 0.0;
    for (int i = 0; i < kSoundsLikeSize; ++i) {
        const double d = x[i] - y[i];
        sum += d * d;
    }
    return sum;
}

std::vector<double> SoundsLikeIndex::percentAlike(int refId, const std::vector<int>& trackIds) const
{
    if (!contains(refId) || m_ids.size() < 2) {
        return {};
    }
    const size_t ref = static_cast<size_t>(m_rowOf[static_cast<size_t>(refId)]);
    // Distance from the reference to every other song (a few ms for 40,000).
    std::vector<double> all;
    all.reserve(m_ids.size() - 1);
    for (size_t k = 0; k < m_ids.size(); ++k) {
        if (k != ref) {
            all.push_back(distance(ref, k));
        }
    }
    std::sort(all.begin(), all.end());
    std::vector<double> out;
    out.reserve(trackIds.size());
    for (int id : trackIds) {
        if (!contains(id) || id == refId) {
            out.push_back(-1.0);
            continue;
        }
        const double d = distance(ref, static_cast<size_t>(m_rowOf[static_cast<size_t>(id)]));
        // Songs at least as close as this one (itself included).
        const size_t closer = static_cast<size_t>(std::upper_bound(all.begin(), all.end(), d) - all.begin());
        out.push_back(100.0 * (1.0 - static_cast<double>(closer - 1) / all.size()));
    }
    return out;
}

std::vector<float> decodeSoundsLike(const void* data, int bytes)
{
    if (!data || bytes != kSoundsLikeSize * static_cast<int>(sizeof(float))) {
        return {};
    }
    std::vector<float> out(kSoundsLikeSize);
    std::memcpy(out.data(), data, bytes);
    return out;
}
}
