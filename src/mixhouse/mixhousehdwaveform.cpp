/*
 * GlideMix
 */
#include "mixhousehdwaveform.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include "mixhouseaudiosource.h"

namespace au::mixhouse {
namespace {
constexpr double kColumnsPerSecond = 150.0;
constexpr double kLowSplitHz = 200.0;     // bass | mids
constexpr double kHighSplitHz = 2500.0;   // mids | highs
constexpr double kRangeDb = 32.0;         // quieter than this below the loud parts is empty
constexpr double kShape = 1.6;            // opens the loud end up, as djay's scale does

//! RBJ biquad, transposed direct form II.
struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;

    static Biquad make(bool highPass, double hz, double q, double rate)
    {
        const double w = 2.0 * M_PI * hz / rate;
        const double alpha = std::sin(w) / (2.0 * q);
        const double c = std::cos(w);
        const double a0 = 1.0 + alpha;
        Biquad f;
        if (highPass) {
            f.b0 = (1.0 + c) / 2.0 / a0;
            f.b1 = -(1.0 + c) / a0;
        } else {
            f.b0 = (1.0 - c) / 2.0 / a0;
            f.b1 = (1.0 - c) / a0;
        }
        f.b2 = f.b0;
        f.a1 = -2.0 * c / a0;
        f.a2 = (1.0 - alpha) / a0;
        return f;
    }

    double run(double x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

//! Fourth-order Butterworth: two biquads.
struct Butterworth4 {
    std::array<Biquad, 2> stages;
    Butterworth4(bool highPass, double hz, double rate)
        : stages { Biquad::make(highPass, hz, 0.54119610, rate), Biquad::make(highPass, hz, 1.30656296, rate) } {}
    double run(double x) { return stages[1].run(stages[0].run(x)); }
};

//! Column energies (mean square) to 0..255 on the drawing scale.
double levelOf(double energy, double ref)
{
    const double db = 10.0 * std::log10(std::max(energy, 1e-14) / ref);   // energy, so 10 log
    return std::pow(std::clamp((db + kRangeDb) / kRangeDb, 0.0, 1.0), kShape);
}

std::vector<uint8_t> toLevels(const std::vector<double>& energy, double& refOut)
{
    std::vector<uint8_t> out(energy.size(), 0);
    if (energy.empty()) {
        return out;
    }
    // The song's own loud level: 99.5th percentile, so one spike can't
    // flatten everything else.
    std::vector<double> sorted = energy;
    const size_t k = std::min(sorted.size() - 1, static_cast<size_t>(sorted.size() * 0.995));
    std::nth_element(sorted.begin(), sorted.begin() + k, sorted.end());
    const double ref = std::max(sorted[k], 1e-12);
    refOut = ref;
    for (size_t i = 0; i < energy.size(); ++i) {
        out[i] = static_cast<uint8_t>(std::lround(levelOf(energy[i], ref) * 255.0));
    }
    return out;
}

//! Grain that belongs to the audio, not to the screen position.
float grain(long long column, int row)
{
    uint32_t h = static_cast<uint32_t>(column) * 0x9E3779B1u ^ static_cast<uint32_t>(row) * 0x85EBCA77u;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    return 0.3f + 0.7f * static_cast<float>(h & 0xFFFF) / 65535.0f;
}
}

namespace {
//! One channel's band energies, column by column (or the mono sum: channel -1).
struct BandEnergies {
    Butterworth4 lp, bandHp, bandLp, hp;
    std::vector<double> low, mid, high;
    double sumLow = 0, sumMid = 0, sumHigh = 0;
    explicit BandEnergies(double rate)
        : lp(false, kLowSplitHz, rate), bandHp(true, kLowSplitHz, rate), bandLp(false, kHighSplitHz, rate),
        hp(true, kHighSplitHz, rate) {}
    void add(double x)
    {
        const double l = lp.run(x);
        const double m = bandLp.run(bandHp.run(x));
        const double h = hp.run(x);
        sumLow += l * l;
        sumMid += m * m;
        sumHigh += h * h;
    }
    void close(long long n)
    {
        low.push_back(sumLow / n);
        mid.push_back(sumMid / n);
        high.push_back(sumHigh / n);
        sumLow = sumMid = sumHigh = 0;
    }
};

//! Decodes once and measures each of `outputs` (-1 = the mono sum, else a channel).
std::vector<BandEnergies> measure(const QString& filePath, const std::vector<int>& outputs)
{
    const AudioInfo opened = probeAudioFile(filePath);
    if (!opened.valid) {
        return {};
    }
    const int channels = std::max(1, opened.channels);
    const double rate = opened.sampleRate > 0 ? opened.sampleRate : 44100.0;
    const double hop = rate / kColumnsPerSecond;
    double nextEdge = hop;
    long long frameIndex = 0, inColumn = 0;
    std::vector<BandEnergies> bands;
    for (size_t k = 0; k < outputs.size(); ++k) {
        bands.emplace_back(rate);
    }
    const AudioInfo info = decodeAudioFile(filePath, [&](const float* buffer, long long frames) {
        for (long long f = 0; f < frames; ++f) {
            for (size_t k = 0; k < outputs.size(); ++k) {
                double x = 0.0;
                if (outputs[k] < 0) {
                    for (int c = 0; c < channels; ++c) {
                        x += buffer[f * channels + c];
                    }
                    x /= channels;
                } else {
                    x = buffer[f * channels + std::min(outputs[k], channels - 1)];
                }
                bands[k].add(x);
            }
            ++inColumn;
            if (++frameIndex >= nextEdge) {
                for (BandEnergies& b : bands) {
                    b.close(inColumn);
                }
                inColumn = 0;
                nextEdge += hop;
            }
        }
        return true;
    });
    if (!info.valid || bands.empty() || bands.front().low.empty()) {
        return {};
    }
    return bands;
}

MixHouseHdWavePtr toWave(const BandEnergies& e)
{
    auto wave = std::make_shared<MixHouseHdWave>();
    wave->rate = kColumnsPerSecond;
    wave->seconds = static_cast<double>(e.low.size()) / kColumnsPerSecond;
    wave->low = toLevels(e.low, wave->refLow);
    wave->mid = toLevels(e.mid, wave->refMid);
    wave->high = toLevels(e.high, wave->refHigh);
    return wave;
}
}

MixHouseHdWavePtr analyzeHdWave(const QString& filePath)
{
    const std::vector<BandEnergies> bands = measure(filePath, { -1 });
    return bands.empty() ? nullptr : toWave(bands.front());
}

std::pair<MixHouseHdWavePtr, MixHouseHdWavePtr> analyzeHdWaveStereo(const QString& filePath)
{
    const std::vector<BandEnergies> bands = measure(filePath, { 0, 1 });
    if (bands.size() != 2) {
        return {};
    }
    // Both sides against the same loud levels, so a quieter side looks quieter.
    BandEnergies both = bands[0];
    both.low.insert(both.low.end(), bands[1].low.begin(), bands[1].low.end());
    both.mid.insert(both.mid.end(), bands[1].mid.begin(), bands[1].mid.end());
    both.high.insert(both.high.end(), bands[1].high.begin(), bands[1].high.end());
    const MixHouseHdWavePtr shared = toWave(both);
    const auto side = [&](const BandEnergies& e) {
        auto wave = std::make_shared<MixHouseHdWave>();
        wave->rate = kColumnsPerSecond;
        wave->seconds = static_cast<double>(e.low.size()) / kColumnsPerSecond;
        wave->refLow = shared->refLow;
        wave->refMid = shared->refMid;
        wave->refHigh = shared->refHigh;
        const auto levels = [](const std::vector<double>& energy, double ref) {
            std::vector<uint8_t> out(energy.size());
            for (size_t i = 0; i < energy.size(); ++i) {
                out[i] = static_cast<uint8_t>(std::lround(levelOf(energy[i], ref) * 255.0));
            }
            return out;
        };
        wave->low = levels(e.low, wave->refLow);
        wave->mid = levels(e.mid, wave->refMid);
        wave->high = levels(e.high, wave->refHigh);
        return MixHouseHdWavePtr(wave);
    };
    return { side(bands[0]), side(bands[1]) };
}

namespace {
//! One pixel column of style E: three layered band envelopes.
void paintColumn(QImage& image, int x, const float v[3], const float hueLevel[3], float soften, long long anchor)
{
    // Bass red, mids green, highs blue: bass and highs together come out pink.
    static const float kColour[3][3] = { { 235, 40, 70 }, { 60, 220, 120 }, { 40, 120, 255 } };
    const int height = image.height();
    const double mid = height / 2.0;
    float half[3], weight[3];
    for (int k = 0; k < 3; ++k) {
        half[k] = std::max(0.5f, v[k] * static_cast<float>(mid - 1.0));
        const float h2 = hueLevel[k] * hueLevel[k];
        weight[k] = h2 * h2;   // the stronger band sets the hue
    }
    for (int y = 0; y < height; ++y) {
        const float dy = std::abs(static_cast<float>(y + 0.5 - mid));
        float r = 0, g = 0, bl = 0, wsum = 0, bright = 0;
        for (int k = 0; k < 3; ++k) {
            const float d = dy / half[k];
            if (d >= 1.0f) {
                continue;
            }
            // Bright in the middle, fading into a grainy edge.
            float glow = 1.0f - d * d;
            if (d > 0.6f) {
                glow *= grain(anchor * 3 + k, y);
            }
            const float w = glow * weight[k];
            r += kColour[k][0] * w;
            g += kColour[k][1] * w;
            bl += kColour[k][2] * w;
            wsum += w;
            bright = std::max(bright, glow);
        }
        if (bright <= 0.02f || wsum <= 1e-12f) {
            continue;
        }
        r /= wsum;
        g /= wsum;
        bl /= wsum;
        if (soften > 0.0f) {
            const float grey = (r + g + bl) / 3.0f;
            r += (grey - r) * soften;
            g += (grey - g) * soften;
            bl += (grey - bl) * soften;
        }
        const float top = std::max({ r, g, bl, 1.0f });
        const float scale = 245.0f / top * (0.25f + 0.75f * bright);
        const auto c = [&](float value) { return std::clamp(static_cast<int>(value * scale), 0, 255); };
        reinterpret_cast<QRgb*>(image.scanLine(y))[x] = qRgb(c(r), c(g), c(bl));
    }
}
}

void paintHdWave(QImage& image, const MixHouseHdWave& wave, double fromSec, double toSec, const std::vector<double>* gain)
{
    const int width = image.width();
    const int height = image.height();
    image.fill(Qt::transparent);
    const long long count = static_cast<long long>(wave.size());
    if (width <= 0 || height <= 0 || count == 0 || toSec <= fromSec) {
        return;
    }
    const std::vector<uint8_t>* bands[3] = { &wave.low, &wave.mid, &wave.high };

    const double perPixel = (toSec - fromSec) * wave.rate / width;   // file columns per pixel
    for (int x = 0; x < width; ++x) {
        const double c0 = (fromSec * wave.rate) + x * perPixel;
        const double c1 = c0 + perPixel;
        if (c1 <= 0.0 || c0 >= count) {
            continue;
        }
        float v[3];
        float hueLevel[3];
        float soften = 0.0f;
        long long anchor;
        if (perPixel >= 1.0) {
            // Several columns in this pixel: the height from the loudest of
            // each band; the colour from the bands' average balance, since
            // over a second or more every band peaks somewhere and the peaks
            // alone blend to a pale grey.
            const long long a = std::max(0LL, static_cast<long long>(std::floor(c0)));
            const long long b = std::min(count, std::max(a + 1, static_cast<long long>(std::ceil(c1))));
            const float wide = static_cast<float>(std::clamp((perPixel - 1.0) / 8.0, 0.0, 1.0));
            soften = 0.22f * wide;   // far out, colours a little softer, as djay's overview
            for (int k = 0; k < 3; ++k) {
                uint8_t m = 0;
                double sum = 0;
                for (long long i = a; i < b; ++i) {
                    const uint8_t x = (*bands[k])[static_cast<size_t>(i)];
                    m = std::max(m, x);
                    sum += x;
                }
                const float mean = static_cast<float>(sum / (b - a)) / 255.0f;
                v[k] = (1.0f - 0.6f * wide) * (m / 255.0f) + 0.6f * wide * mean;
                hueLevel[k] = (1.0f - wide) * v[k] + wide * mean;
            }
            anchor = a;
        } else {
            // Zoomed in past the analysis: between two columns, smoothly.
            const double centre = std::clamp((c0 + c1) / 2.0 - 0.5, 0.0, static_cast<double>(count - 1));
            const long long i = static_cast<long long>(centre);
            const long long j = std::min(count - 1, i + 1);
            const float t = static_cast<float>(centre - i);
            for (int k = 0; k < 3; ++k) {
                v[k] = ((1 - t) * (*bands[k])[static_cast<size_t>(i)] + t * (*bands[k])[static_cast<size_t>(j)]) / 255.0f;
                hueLevel[k] = v[k];
            }
            anchor = static_cast<long long>(std::floor(c0 * 4.0));   // grain finer than a column when zoomed in
        }
        // The clip's volume: the stored levels are on a dB scale, so a gain
        // shifts them down by its dB (the colours stay the song's own).
        if (gain && x < static_cast<int>(gain->size())) {
            const double g = (*gain)[static_cast<size_t>(x)];
            if (g < 0.999) {
                const double shiftDb = g <= 1e-6 ? -kRangeDb : 20.0 * std::log10(g);
                for (int k = 0; k < 3; ++k) {
                    const double db = std::pow(std::max(0.0f, v[k]), 1.0 / kShape) * kRangeDb - kRangeDb + shiftDb;
                    v[k] = static_cast<float>(std::pow(std::clamp((db + kRangeDb) / kRangeDb, 0.0, 1.0), kShape));
                }
            }
        }
        if (v[0] + v[1] + v[2] <= 0.0f) {
            continue;
        }
        paintColumn(image, x, v, hueLevel, soften, anchor);
    }
}

uint32_t hdWaveColour(const MixHouseHdWave& wave, double fromSec, double toSec)
{
    static const float kColour[3][3] = { { 235, 40, 70 }, { 60, 220, 120 }, { 40, 120, 255 } };
    const std::vector<uint8_t>* bands[3] = { &wave.low, &wave.mid, &wave.high };
    const long long count = static_cast<long long>(wave.size());
    if (count == 0) {
        return 0;
    }
    const long long a = std::clamp(static_cast<long long>(std::floor(fromSec * wave.rate)), 0LL, count - 1);
    const long long b = std::clamp(static_cast<long long>(std::ceil(toSec * wave.rate)), a + 1, count);
    float r = 0, g = 0, bl = 0, wsum = 0;
    for (int k = 0; k < 3; ++k) {
        uint8_t m = 0;
        for (long long i = a; i < b; ++i) {
            m = std::max(m, (*bands[k])[static_cast<size_t>(i)]);
        }
        const float v = m / 255.0f;
        const float w = v * v * v * v;
        r += kColour[k][0] * w;
        g += kColour[k][1] * w;
        bl += kColour[k][2] * w;
        wsum += w;
    }
    if (wsum <= 1e-9f) {
        return 0;
    }
    const float top = std::max({ r, g, bl }) / wsum;
    const float scale = 255.0f / std::max(top, 1.0f) / wsum;
    return (static_cast<uint32_t>(r * scale) << 16) | (static_cast<uint32_t>(g * scale) << 8) | static_cast<uint32_t>(bl * scale);
}

void paintHdCloseUp(QImage& image, const MixHouseHdWave& wave, const float* mono, size_t count, size_t warmup,
                    double sampleRate, long long grainStart)
{
    Q_UNUSED(grainStart);
    image.fill(Qt::transparent);
    const int width = image.width();
    const int height = image.height();
    if (width <= 0 || height <= 2 || !mono || count <= warmup + 1 || sampleRate <= 0) {
        return;
    }
    // The colour: each band's energy, smoothed over about one stored column
    // (1/150 s), so it follows the music, not every single wave.
    Butterworth4 lp(false, kLowSplitHz, sampleRate), bandHp(true, kLowSplitHz, sampleRate);
    Butterworth4 bandLp(false, kHighSplitHz, sampleRate), hp(true, kHighSplitHz, sampleRate);
    const double smooth = 1.0 - std::exp(-1.0 / (0.004 * sampleRate));
    double e[3] = { 0, 0, 0 };
    std::vector<std::array<float, 3> > energy(count);
    for (size_t i = 0; i < count; ++i) {
        const double x = mono[i];
        const double b[3] = { lp.run(x), bandLp.run(bandHp.run(x)), hp.run(x) };
        for (int k = 0; k < 3; ++k) {
            e[k] += (b[k] * b[k] - e[k]) * smooth;
            energy[i][k] = static_cast<float>(e[k]);
        }
    }
    const double refs[3] = { wave.refLow, wave.refMid, wave.refHigh };
    // The height: the audio itself on the same dB scale as the stored picture,
    // so a loud peak reaches the top as the zoomed-out view does.
    const double refAmp = std::sqrt(std::max(wave.refLow + wave.refMid + wave.refHigh, 1e-12)) * 1.4;
    const auto heightOf = [&](double a) {
        const double m = std::abs(a) / refAmp;
        if (m <= 0.0) {
            return 0.0;
        }
        const double db = 20.0 * std::log10(m);
        const double v = std::pow(std::clamp((db + kRangeDb) / kRangeDb, 0.0, 1.0), kShape);
        return a < 0 ? -v : v;
    };
    static const float kColour[3][3] = { { 235, 40, 70 }, { 60, 220, 120 }, { 40, 120, 255 } };
    const double mid = height / 2.0;
    const double half = mid - 1.0;
    const double perPixel = static_cast<double>(count - 1 - warmup) / width;   // samples per pixel
    double prevY = std::numeric_limits<double>::quiet_NaN();
    for (int x = 0; x < width; ++x) {
        const double p0 = warmup + x * perPixel;
        const double p1 = p0 + perPixel;
        double lo, hi;
        size_t colourAt;
        if (perPixel >= 1.0) {
            const size_t a = static_cast<size_t>(p0);
            const size_t b = std::min(count, std::max(a + 1, static_cast<size_t>(std::ceil(p1))));
            float mn = mono[a], mx = mono[a];
            for (size_t i = a + 1; i < b; ++i) {
                mn = std::min(mn, mono[i]);
                mx = std::max(mx, mono[i]);
            }
            lo = heightOf(mn);
            hi = heightOf(mx);
            colourAt = (a + b) / 2;
        } else {
            // Past one sample a pixel: the line between samples, joined up.
            const size_t i = std::min(count - 2, static_cast<size_t>(p0));
            const double t = p0 - i;
            const double y = heightOf((1 - t) * mono[i] + t * mono[i + 1]);
            lo = hi = y;
            colourAt = i;
        }
        // Joined to the previous pixel, so the shape never breaks into dots.
        if (!std::isnan(prevY)) {
            lo = std::min(lo, prevY);
            hi = std::max(hi, prevY);
        }
        prevY = perPixel >= 1.0 ? (lo + hi) / 2.0 : lo;
        // A line is at least two pixels thick.
        const int yTop = std::clamp(static_cast<int>(std::floor(mid - hi * half)) - (perPixel < 4.0 ? 1 : 0), 0, height - 1);
        const int yBottom = std::clamp(static_cast<int>(std::ceil(mid - lo * half)) + (perPixel < 4.0 ? 1 : 0), 0, height - 1);

        float r = 0, g = 0, bl = 0, wsum = 0;
        for (int k = 0; k < 3; ++k) {
            const float v = static_cast<float>(levelOf(energy[colourAt][k], refs[k]));
            const float w = v * v * v * v;
            r += kColour[k][0] * w;
            g += kColour[k][1] * w;
            bl += kColour[k][2] * w;
            wsum += w;
        }
        if (wsum <= 1e-12f) {
            r = 90, g = 90, bl = 110, wsum = 1;
        }
        r /= wsum;
        g /= wsum;
        bl /= wsum;
        const float top = std::max({ r, g, bl, 1.0f });
        for (int y = yTop; y <= yBottom; ++y) {
            // Brighter toward the middle line, a touch darker at the edges.
            const float d = static_cast<float>(std::abs(y + 0.5 - mid) / half);
            const float scale = 245.0f / top * (0.25f + 0.75f * (1.0f - d * d));
            const auto c = [&](float value) { return std::clamp(static_cast<int>(value * scale), 0, 255); };
            reinterpret_cast<QRgb*>(image.scanLine(y))[x] = qRgb(c(r), c(g), c(bl));
        }
    }
}
}
