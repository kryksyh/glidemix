/*
 * GlideMix: Create variations. See mixhousevariations.h.
 */
#include "mixhousevariations.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <random>
#include <vector>

#include <QDir>
#include <sndfile.h>

#include "mixhouse/mixhouseaudiosource.h"
#include "mixhouse/mixhousenativepath.h"

namespace au::mixhouse {
namespace {
struct Sound {
    int rate = 44100;
    std::vector<float> l, r;
    size_t size() const { return l.size(); }
};

bool readSound(const QString& path, Sound& out)
{
    const AudioInfo first = decodeAudioFile(path, [](const float*, long long) { return false; });
    const int ch = first.channels;
    if (!first.valid || ch <= 0 || first.sampleRate <= 0) {
        return false;
    }
    std::vector<float> data;
    const AudioInfo info = decodeAudioFile(path, [&](const float* buf, long long frames) {
        data.insert(data.end(), buf, buf + frames * ch);
        return true;
    });
    if (!info.valid || data.empty()) {
        return false;
    }
    out.rate = info.sampleRate;
    const size_t n = data.size() / size_t(ch);
    out.l.resize(n);
    out.r.resize(n);
    for (size_t i = 0; i < n; ++i) {
        out.l[i] = data[i * size_t(ch)];
        out.r[i] = data[i * size_t(ch) + size_t(std::min(1, ch - 1))];
    }
    return true;
}

bool writeSound(const QString& path, const Sound& s)
{
    float peak = 0.0f;
    for (size_t i = 0; i < s.size(); ++i) {
        peak = std::max({ peak, std::abs(s.l[i]), std::abs(s.r[i]) });
    }
    const float scale = peak > 0.98f ? 0.98f / peak : 1.0f;
    SF_INFO info {};
    info.samplerate = s.rate;
    info.channels = 2;
    info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_24;
    SNDFILE* file = openSndfile(path, SFM_WRITE, &info);
    if (!file) {
        return false;
    }
    std::vector<float> inter(s.size() * 2);
    for (size_t i = 0; i < s.size(); ++i) {
        inter[i * 2] = s.l[i] * scale;
        inter[i * 2 + 1] = s.r[i] * scale;
    }
    sf_writef_float(file, inter.data(), sf_count_t(s.size()));
    sf_close(file);
    return true;
}

Sound reversed(const Sound& s)
{
    Sound o = s;
    std::reverse(o.l.begin(), o.l.end());
    std::reverse(o.r.begin(), o.r.end());
    return o;
}

//! The loop cut in `slices` equal parts and played in the order `order`,
//! each part with a short fade at both ends (no clicks).
Sound rearranged(const Sound& s, int slices, const std::vector<int>& order)
{
    Sound o = s;
    const size_t len = s.size() / size_t(slices);
    const size_t fade = std::min<size_t>(len / 4, size_t(s.rate * 0.003));
    for (int k = 0; k < slices && len > 0; ++k) {
        const size_t from = size_t(order[size_t(k)]) * len;
        const size_t to = size_t(k) * len;
        for (size_t i = 0; i < len; ++i) {
            float g = 1.0f;
            if (fade > 0 && i < fade) {
                g = float(i) / float(fade);
            } else if (fade > 0 && i >= len - fade) {
                g = float(len - 1 - i) / float(fade);
            }
            o.l[to + i] = s.l[from + i] * g;
            o.r[to + i] = s.r[from + i] * g;
        }
    }
    return o;
}

//! A two-pole low-pass at `hz`, a little louder to make up for the lost top.
Sound darker(const Sound& s, double hz)
{
    Sound o = s;
    const double w = 2.0 * 3.14159265358979 * hz / s.rate;
    const double q = 0.707;
    const double alpha = std::sin(w) / (2.0 * q);
    const double cw = std::cos(w);
    const double a0 = 1.0 + alpha;
    const double b0 = (1.0 - cw) / 2.0 / a0, b1 = (1.0 - cw) / a0, b2 = b0;
    const double a1 = -2.0 * cw / a0, a2 = (1.0 - alpha) / a0;
    for (std::vector<float>* ch : { &o.l, &o.r }) {
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        for (float& v : *ch) {
            const double x = v;
            const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1;
            x1 = x;
            y2 = y1;
            y1 = y;
            v = float(y * 1.25);
        }
    }
    return o;
}

//! Played faster or slower, like a sampler (shorter and higher, or longer and lower).
Sound pitched(const Sound& s, double semitones)
{
    const double step = std::pow(2.0, semitones / 12.0);
    Sound o;
    o.rate = s.rate;
    const size_t n = size_t(double(s.size()) / step);
    o.l.resize(n);
    o.r.resize(n);
    for (size_t i = 0; i < n; ++i) {
        const double src = double(i) * step;
        const size_t i0 = std::min(size_t(src), s.size() - 1);
        const size_t i1 = std::min(i0 + 1, s.size() - 1);
        const float f = float(src - double(i0));
        o.l[i] = s.l[i0] + (s.l[i1] - s.l[i0]) * f;
        o.r[i] = s.r[i0] + (s.r[i1] - s.r[i0]) * f;
    }
    return o;
}
}

QStringList makeSoundVariations(const QString& sourcePath, const QString& folder, const QString& baseName, bool loop)
{
    Sound s;
    if (!readSound(sourcePath, s) || s.size() < 64) {
        return {};
    }
    QDir().mkpath(folder);
    std::vector<std::pair<QString, Sound> > out;
    out.push_back({ QStringLiteral("Reversed"), reversed(s) });
    if (loop) {
        // 16 parts for a loop of two bars or more, 8 for a shorter one.
        const int slices = s.size() > size_t(s.rate) * 3 ? 16 : 8;
        std::vector<int> order(static_cast<size_t>(slices));
        for (int i = 0; i < slices; ++i) {
            order[size_t(i)] = i;
        }
        // Chopped: the parts shuffled, the first kept (the loop still starts on its one).
        std::mt19937 rng(std::hash<std::string>()(baseName.toStdString()) & 0xffffffffu);
        std::shuffle(order.begin() + 1, order.end(), rng);
        out.push_back({ QStringLiteral("Chopped"), rearranged(s, slices, order) });
        // Stutter: every other part played twice (1 1 3 3 5 5 ...).
        for (int i = 0; i < slices; ++i) {
            order[size_t(i)] = i - i % 2;
        }
        out.push_back({ QStringLiteral("Stutter"), rearranged(s, slices, order) });
    } else {
        out.push_back({ QStringLiteral("Up 5"), pitched(s, 5.0) });
        out.push_back({ QStringLiteral("Down 7"), pitched(s, -7.0) });
    }
    out.push_back({ QStringLiteral("Dark"), darker(s, 900.0) });

    QStringList written;
    for (const auto& [what, sound] : out) {
        const QString path = folder + QStringLiteral("/") + baseName + QStringLiteral(" - ") + what + QStringLiteral(".wav");
        if (writeSound(path, sound)) {
            written << path;
        }
    }
    return written;
}
}
