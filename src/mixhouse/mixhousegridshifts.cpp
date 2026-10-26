/*
 * GlideMix
 */
#include "mixhousegridshifts.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#ifdef GRIDSHIFT_DEBUG
#include <cstdio>
#endif

namespace au::mixhouse {
namespace {
constexpr double kMinJumpSec = 0.020;      // smaller is not heard as a flam
constexpr double kSteadySec = 0.008;       // a section's bars sit this close to its median (MAD)
constexpr int kMinShiftBars = 4;           // the shortest piece the fit considers
constexpr size_t kMinSteadyBars = 8;       // a section that counts (reference or jump) holds this long
constexpr double kMaxDriftDiffSec = 0.002;  // per bar: steady sections share the grid's tempo error
constexpr double kMinSteadyShare = 0.5;    // less of the song steady: its beats are not clear enough to move
constexpr double kSplitPenalty = 0.002;    // squared seconds of residual a new piece has to explain

// In-place radix-2 FFT (n a power of two).
void fft(std::vector<std::complex<double> >& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            std::swap(a[i], a[j]);
        }
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const std::complex<double> wl = std::polar(1.0, -2.0 * M_PI / len);
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k) {
                const std::complex<double> u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

double median(std::vector<double> v)
{
    if (v.empty()) {
        return 0.0;
    }
    const size_t mid = v.size() / 2;
    std::nth_element(v.begin(), v.begin() + mid, v.end());
    double m = v[mid];
    if (v.size() % 2 == 0) {
        m = (m + *std::max_element(v.begin(), v.begin() + mid)) / 2.0;
    }
    return m;
}

/// Onset times (seconds, with a constant small bias, which cancels out: only
/// differences between sections matter): spectral flux of the whole band at
/// about 11 kHz. The kick's click times a hit the same way whatever the kick
/// sounds like; the low band alone did not (a new kick sound in a new section
/// moved its onsets by up to 40 ms in a steady DAW track).
std::vector<double> onsetTimes(const float* mono, size_t frames, int sampleRate)
{
    const size_t decim = std::max<size_t>(1, static_cast<size_t>(std::lround(sampleRate / 11025.0)));
    const double rate = static_cast<double>(sampleRate) / decim;
    std::vector<double> x(frames / decim);
    for (size_t i = 0; i < x.size(); ++i) {
        double sum = 0.0;
        for (size_t k = 0; k < decim; ++k) {
            sum += mono[i * decim + k];
        }
        x[i] = sum / decim;
    }
    constexpr size_t kWindow = 128, kHop = 32;
    if (x.size() < kWindow * 4) {
        return {};
    }
    const size_t n = (x.size() - kWindow) / kHop;
    std::vector<double> hann(kWindow);
    for (size_t i = 0; i < kWindow; ++i) {
        hann[i] = 0.5 - 0.5 * std::cos(2.0 * M_PI * i / kWindow);
    }
    std::vector<double> flux(n, 0.0), previous(kWindow / 2 + 1, 0.0);
    std::vector<std::complex<double> > buf(kWindow);
    for (size_t j = 0; j < n; ++j) {
        for (size_t i = 0; i < kWindow; ++i) {
            buf[i] = x[j * kHop + i] * hann[i];
        }
        fft(buf);
        double f = 0.0;
        for (size_t k = 0; k <= kWindow / 2; ++k) {
            const double level = std::log1p(100.0 * std::abs(buf[k]));
            if (j > 0) {
                f += std::max(0.0, level - previous[k]);
            }
            previous[k] = level;
        }
        flux[j] = f;
    }
    double mean = 0.0;
    for (double f : flux) {
        mean += f;
    }
    mean /= n;
    double var = 0.0;
    for (double f : flux) {
        var += (f - mean) * (f - mean);
    }
    const double stddev = std::sqrt(var / n);

    // Local peaks (60 ms either side) above the local average (+-0.5 s) plus
    // the overall spread, at least 100 ms apart.
    const double hopSec = kHop / rate;
    const long peakHalf = std::max(1L, std::lround(0.060 / hopSec));
    const long avgHalf = peakHalf * 8;
    std::vector<double> prefix(n + 1, 0.0);
    for (size_t j = 0; j < n; ++j) {
        prefix[j + 1] = prefix[j] + flux[j];
    }
    std::vector<double> onsets;
    double last = -1.0;
    for (long j = peakHalf; j + peakHalf < static_cast<long>(n); ++j) {
        const long lo = std::max(0L, j - avgHalf), hi = std::min(static_cast<long>(n), j + avgHalf);
        if (flux[j] <= (prefix[hi] - prefix[lo]) / (hi - lo) + stddev) {
            continue;
        }
        if (flux[j] < *std::max_element(flux.begin() + (j - peakHalf), flux.begin() + (j + peakHalf))) {
            continue;
        }
        const double t = (j * kHop + kWindow / 2.0) / rate;
        if (t - last >= 0.100) {
            onsets.push_back(t);
            last = t;
        }
    }
    return onsets;
}
}

std::vector<MixHouseGridShift> detectGridShifts(const float* mono, size_t frames, int sampleRate,
                                                double bpm, double barOneSec, int beatsPerBar)
{
    std::vector<MixHouseGridShift> shifts;
    if (!mono || sampleRate <= 0 || bpm <= 0.0 || beatsPerBar <= 0 || frames < static_cast<size_t>(sampleRate) * 10) {
        return shifts;
    }
    const double beat = 60.0 / bpm;
    const double bar = beat * beatsPerBar;
    const double duration = static_cast<double>(frames) / sampleRate;
    const std::vector<double> onsets = onsetTimes(mono, frames, sampleRate);

    // One median offset (onset - nearest grid beat) per bar, from the
    // onsets within a sixth of a beat of a beat (a quarter let sections
    // whose loudest hits are 16ths count as jumps).
    const long firstBar = static_cast<long>(std::floor((0.0 - barOneSec) / bar));
    const long lastBar = static_cast<long>(std::ceil((duration - barOneSec) / bar));
    std::vector<std::vector<double> > perBar(static_cast<size_t>(lastBar - firstBar + 1));
    for (double t : onsets) {
        const double k = std::round((t - barOneSec) / beat);
        const double r = t - (barOneSec + k * beat);
        if (std::abs(r) >= beat / 6.0) {
            continue;
        }
        const long b = static_cast<long>(std::floor((barOneSec + k * beat - barOneSec) / bar + 1e-9));
        if (b >= firstBar && b <= lastBar) {
            perBar[static_cast<size_t>(b - firstBar)].push_back(r);
        }
    }
    std::vector<double> m;
    std::vector<long> barIndex;
    for (size_t i = 0; i < perBar.size(); ++i) {
        if (perBar[i].size() >= 2) {
            m.push_back(median(perBar[i]));
            barIndex.push_back(firstBar + static_cast<long>(i));
        }
    }
    const size_t n = std::min<size_t>(m.size(), 800);
    if (n < 2 * kMinShiftBars) {
        return shifts;
    }

    // Fewest straight pieces (offset plus drift: a grid a hair off the
    // song's tempo drifts steadily, which is not a jump) that explain the
    // per-bar offsets, by least squares over bar times: dynamic programming
    // over the piece boundaries, prefix sums for each piece's line.
    std::vector<double> t(n);
    for (size_t i = 0; i < n; ++i) {
        t[i] = barIndex[i] * bar;
    }
    std::vector<double> st(n + 1, 0.0), sm(n + 1, 0.0), stt(n + 1, 0.0), stm(n + 1, 0.0), smm(n + 1, 0.0);
    for (size_t i = 0; i < n; ++i) {
        st[i + 1] = st[i] + t[i];
        sm[i + 1] = sm[i] + m[i];
        stt[i + 1] = stt[i] + t[i] * t[i];
        stm[i + 1] = stm[i] + t[i] * m[i];
        smm[i + 1] = smm[i] + m[i] * m[i];
    }
    struct Line {
        double a = 0.0, b = 0.0;   // offset = a + b * t
        double at(double x) const { return a + b * x; }
    };
    auto line = [&](size_t i, size_t j) {
        const double k = static_cast<double>(j - i);
        const double mt = (st[j] - st[i]) / k, mm = (sm[j] - sm[i]) / k;
        const double vtt = (stt[j] - stt[i]) / k - mt * mt;
        const double vtm = (stm[j] - stm[i]) / k - mt * mm;
        Line l;
        l.b = vtt > 1e-12 ? vtm / vtt : 0.0;
        l.a = mm - l.b * mt;
        return l;
    };
    auto cost = [&](size_t i, size_t j) {
        const Line l = line(i, j);
        const double k = static_cast<double>(j - i);
        // Sum of (m - a - b t)^2 from the prefix sums.
        const double c = (smm[j] - smm[i]) - 2.0 * l.a * (sm[j] - sm[i]) - 2.0 * l.b * (stm[j] - stm[i])
                         + k * l.a * l.a + 2.0 * l.a * l.b * (st[j] - st[i]) + l.b * l.b * (stt[j] - stt[i]);
        return std::max(0.0, c);
    };
    std::vector<double> best(n + 1, std::numeric_limits<double>::infinity());
    std::vector<size_t> from(n + 1, 0);
    best[0] = 0.0;
    for (size_t j = kMinShiftBars; j <= n; ++j) {
        for (size_t i = 0; i + kMinShiftBars <= j; ++i) {
            if (!std::isfinite(best[i])) {
                continue;
            }
            const double c = best[i] + cost(i, j) + kSplitPenalty;
            if (c < best[j]) {
                best[j] = c;
                from[j] = i;
            }
        }
    }
    std::vector<std::pair<size_t, size_t> > pieces;
    for (size_t j = n; j > 0; j = from[j]) {
        pieces.emplace_back(from[j], j);
    }
    std::reverse(pieces.begin(), pieces.end());

    // Jumps between steady pieces: the step from the earlier piece's line to
    // the later one's where the later one starts. The cut goes right after
    // the earlier piece's last bar: what lies between (a fill, the loop
    // change) has no steady beat to keep, but a late bar left unmoved flams. Unsteady or short pieces
    // (an intro, a breakdown without kicks, the loop change itself) move
    // nothing. A song that is mostly unsteady is left alone altogether.
    struct Piece {
        size_t first, end;
        Line line;
    };
    std::vector<Piece> steady;
    size_t steadyBars = 0;
    for (const auto& [i, j] : pieces) {
        const Line l = line(i, j);
        std::vector<double> dev;
        for (size_t q = i; q < j; ++q) {
            dev.push_back(std::abs(m[q] - l.at(t[q])));
        }
#ifdef GRIDSHIFT_DEBUG
        std::fprintf(stderr, "piece bars %ld-%ld (%zu) offset %+.1f ms drift %+.2f ms/bar MAD %.1f ms\n", barIndex[i],
                     barIndex[j - 1], j - i, l.at(t[i]) * 1000, l.b * bar * 1000, median(dev) * 1000);
#endif
        // A steep line through a breakdown can look steady; a real section
        // drifts like the rest of the song (the same small tempo error).
        const bool drifts = !steady.empty() && std::abs(l.b - steady.front().line.b) * bar > kMaxDriftDiffSec;
        if (j - i >= kMinSteadyBars && median(dev) <= kSteadySec && !drifts) {
            steady.push_back({ i, j, l });
            steadyBars += j - i;
        }
    }
    if (steady.size() < 2 || steadyBars < kMinSteadyShare * n) {
        return shifts;
    }
    double total = 0.0;
    for (size_t p = 1; p < steady.size(); ++p) {
        const double at = t[steady[p].first];
        const double step = steady[p].line.at(at) - steady[p - 1].line.at(at);
        if (std::abs(step) >= kMinJumpSec && std::abs(total + step) < beat / 6.0) {
            total += step;
            shifts.push_back({ barOneSec + (barIndex[steady[p - 1].end - 1] + 1) * bar, total });
        }
    }
    return shifts;
}
}
