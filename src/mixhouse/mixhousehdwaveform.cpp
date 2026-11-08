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

}

namespace {
//! One channel's band energies, column by column (or the mono sum: channel -1).
//! The Beatgrid editor's band split (mixhousegrideditormodel.cpp): two
//! one-pole low-passes at 200 Hz and at 2500 Hz; bass below, highs above,
//! mids between. The same split everywhere, so every waveform has the same colours.
struct GridSplit {
    double aLow = 0, aHigh = 0, l1 = 0, l2 = 0, h1 = 0, h2 = 0;
    explicit GridSplit(double rate)
        : aLow(1.0 - std::exp(-2.0 * M_PI * kLowSplitHz / rate)), aHigh(1.0 - std::exp(-2.0 * M_PI * kHighSplitHz / rate)) {}
    void run(double x, double& low, double& mid, double& high)
    {
        l1 += aLow * (x - l1);
        l2 += aLow * (l1 - l2);
        h1 += aHigh * (x - h1);
        h2 += aHigh * (h1 - h2);
        low = l2;
        mid = h2 - l2;
        high = x - h2;
    }
};

struct BandEnergies {
    GridSplit split;
    std::vector<double> low, mid, high;
    double sumLow = 0, sumMid = 0, sumHigh = 0;
    explicit BandEnergies(double rate)
        : split(rate) {}
    void add(double x)
    {
        double l, m, h;
        split.run(x, l, m, h);
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

QColor hdBandColor(float low, float mid, float high)
{
    // Mids under-weighted a little: rekordbox's colours lean red/blue (pink
    // kicks, blue hats), green only where the mids really lead.
    // Squared, so the leading band wins clearly and colours don't wash to white.
    double r = std::pow(low, 2.0);
    double g = std::pow(mid * 0.75, 2.0);
    double b = std::pow(high * 0.9, 2.0);
    const double top = std::max({ r, g, b, 1e-6 });
    r /= top;
    g /= top;
    b /= top;
    const double floorLevel = 0.06;
    const auto channel = [floorLevel](double v) { return static_cast<int>(std::lround(255.0 * (floorLevel + (1.0 - floorLevel) * v))); };
    return QColor(channel(r), channel(g), channel(b));
}

namespace {
//! A stored level (0..1 on the drawing scale) back to the band's amplitude
//! against its own loud level (0..1), the scale the Beatgrid editor colours on.
double ampOf(double level)
{
    if (level <= 0.0) {
        return 0.0;
    }
    const double db = std::pow(std::min(level, 1.0), 1.0 / kShape) * kRangeDb - kRangeDb;
    return std::pow(10.0, db / 20.0);
}

//! One pixel column the Beatgrid editor's way: a line from the peak in the
//! band colour, with a brighter core at the RMS.
void paintColumn(QImage& image, int x, double outer, double core, const QColor& colour)
{
    const int height = image.height();
    const double mid = height / 2.0;
    const double half = mid - 1.0;
    outer = std::clamp(outer, 0.0, 1.0);
    core = std::clamp(std::min(core, outer), 0.0, 1.0);
    const QRgb edge = colour.rgb();
    const QRgb bright = colour.lighter(135).rgb();
    const int top = std::clamp(static_cast<int>(std::floor(mid - outer * half)), 0, height - 1);
    const int bottom = std::clamp(static_cast<int>(std::ceil(mid + outer * half)), 0, height - 1);
    const double coreHalf = core * half;
    for (int y = top; y <= bottom; ++y) {
        const bool inCore = std::abs(y + 0.5 - mid) <= coreHalf;
        reinterpret_cast<QRgb*>(image.scanLine(y))[x] = inCore ? bright : edge;
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
    // Height: the column's loudness against the song's loud level, linear like
    // the Beatgrid editor (quiet parts stay small, the drops reach the top).
    const double refs[3] = { wave.refLow, wave.refMid, wave.refHigh };
    const double loud = std::sqrt(std::max(refs[0] + refs[1] + refs[2], 1e-12));
    const auto rmsOf = [&](long long i) {
        double e = 0.0;
        for (int k = 0; k < 3; ++k) {
            const double a = ampOf((*bands[k])[static_cast<size_t>(i)] / 255.0);
            e += a * a * refs[k];
        }
        return std::sqrt(e) / loud;
    };

    const double perPixel = (toSec - fromSec) * wave.rate / width;   // file columns per pixel
    for (int x = 0; x < width; ++x) {
        const double c0 = (fromSec * wave.rate) + x * perPixel;
        const double c1 = c0 + perPixel;
        if (c1 <= 0.0 || c0 >= count) {
            continue;
        }
        double amp[3] = { 0, 0, 0 };
        double peak = 0.0, rms = 0.0;
        if (perPixel >= 1.0) {
            // As the Beatgrid editor: the height from the loudest column, the
            // colour from the bands' average over the pixel.
            const long long a = std::max(0LL, static_cast<long long>(std::floor(c0)));
            const long long b = std::min(count, std::max(a + 1, static_cast<long long>(std::ceil(c1))));
            double sumSq = 0.0;
            for (long long i = a; i < b; ++i) {
                for (int k = 0; k < 3; ++k) {
                    amp[k] += ampOf((*bands[k])[static_cast<size_t>(i)] / 255.0);
                }
                const double r = rmsOf(i);
                peak = std::max(peak, r);
                sumSq += r * r;
            }
            for (double& v : amp) {
                v /= double(b - a);
            }
            rms = std::sqrt(sumSq / double(b - a));
        } else {
            // Zoomed in past the analysis: between two columns, smoothly.
            const double centre = std::clamp((c0 + c1) / 2.0 - 0.5, 0.0, static_cast<double>(count - 1));
            const long long i = static_cast<long long>(centre);
            const long long j = std::min(count - 1, i + 1);
            const double t = centre - i;
            for (int k = 0; k < 3; ++k) {
                amp[k] = (1 - t) * ampOf((*bands[k])[static_cast<size_t>(i)] / 255.0)
                         + t * ampOf((*bands[k])[static_cast<size_t>(j)] / 255.0);
            }
            peak = rms = (1 - t) * rmsOf(i) + t * rmsOf(j);
        }
        // The clip's volume shrinks the picture (the colours stay the song's own).
        double g = 1.0;
        if (gain && x < static_cast<int>(gain->size())) {
            g = std::max(0.0, (*gain)[static_cast<size_t>(x)]);
        }
        const double outer = peak * 0.95 * g;
        const double core = rms * 0.55 * g;
        if (outer <= 0.003) {
            continue;
        }
        paintColumn(image, x, outer, core, hdBandColor(float(amp[0]), float(amp[1]), float(amp[2])));
    }
}

uint32_t hdWaveColour(const MixHouseHdWave& wave, double fromSec, double toSec)
{
    const std::vector<uint8_t>* bands[3] = { &wave.low, &wave.mid, &wave.high };
    const long long count = static_cast<long long>(wave.size());
    if (count == 0) {
        return 0;
    }
    const long long a = std::clamp(static_cast<long long>(std::floor(fromSec * wave.rate)), 0LL, count - 1);
    const long long b = std::clamp(static_cast<long long>(std::ceil(toSec * wave.rate)), a + 1, count);
    double amp[3] = { 0, 0, 0 };
    for (int k = 0; k < 3; ++k) {
        for (long long i = a; i < b; ++i) {
            amp[k] += ampOf((*bands[k])[static_cast<size_t>(i)] / 255.0);
        }
        amp[k] /= double(b - a);
    }
    if (amp[0] + amp[1] + amp[2] <= 1e-9) {
        return 0;
    }
    return hdBandColor(float(amp[0]), float(amp[1]), float(amp[2])).rgb() & 0xFFFFFF;
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
    GridSplit split(sampleRate);
    const double smooth = 1.0 - std::exp(-1.0 / (0.004 * sampleRate));
    double e[3] = { 0, 0, 0 };
    std::vector<std::array<float, 3> > energy(count);
    for (size_t i = 0; i < count; ++i) {
        const double x = mono[i];
        double b[3];
        split.run(x, b[0], b[1], b[2]);
        for (int k = 0; k < 3; ++k) {
            e[k] += (b[k] * b[k] - e[k]) * smooth;
            energy[i][k] = static_cast<float>(e[k]);
        }
    }
    const double refs[3] = { wave.refLow, wave.refMid, wave.refHigh };
    // The height: the audio itself on the same dB scale as the stored picture,
    // so a loud peak reaches the top as the zoomed-out view does.
    // Linear, as the Beatgrid editor and the zoomed-out picture: a loud part's
    // peaks (about 2.6x its loudness) reach the top.
    const double refAmp = std::sqrt(std::max(wave.refLow + wave.refMid + wave.refHigh, 1e-12)) * 2.6;
    const auto heightOf = [&](double a) { return std::clamp(a / refAmp, -1.0, 1.0); };
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

        // The colour: each band's amplitude against its own loud level, as stored.
        float amp[3];
        for (int k = 0; k < 3; ++k) {
            amp[k] = static_cast<float>(std::sqrt(std::max(0.0, double(energy[colourAt][k])) / std::max(refs[k], 1e-14)));
            amp[k] = std::min(amp[k], 1.0f);
        }
        const QRgb pixel = hdBandColor(amp[0], amp[1], amp[2]).rgb();
        for (int y = yTop; y <= yBottom; ++y) {
            reinterpret_cast<QRgb*>(image.scanLine(y))[x] = pixel;
        }
    }
}
}
