/*
 * GlideMix Samples B8: Beat. See mixhousebeat.h.
 */
#include "mixhousebeat.h"

#include <algorithm>
#include <cmath>
#include <map>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFileInfo>
#include <QDateTime>
#include <sndfile.h>

#include "mixhouse/mixhouseaudiosource.h"
#include "mixhouse/mixhousenativepath.h"
#include "translation.h"

namespace au::mixhouse {
namespace {
constexpr int kRate = 44100;

//! A one-shot decoded to stereo at 44.1 kHz (linear resampling is plenty
//! for drum hits).
struct Shot {
    std::vector<float> left, right;
};

bool decodeShot(const QString& path, Shot& out);

//! Decoded one-shots, kept while GlideMix runs: the preview renders the beat
//! again on every click, and a kit is only a handful of files.
bool loadShot(const QString& path, Shot& out)
{
    static std::map<QString, std::pair<qint64, Shot> > cache;
    const qint64 stamp = QFileInfo(path).lastModified().toMSecsSinceEpoch();
    const auto it = cache.find(path);
    if (it != cache.end() && it->second.first == stamp) {
        out = it->second.second;
        return true;
    }
    if (!decodeShot(path, out)) {
        return false;
    }
    if (cache.size() > 64) {
        cache.clear();
    }
    cache[path] = { stamp, out };
    return true;
}

bool decodeShot(const QString& path, Shot& out)
{
    // The decoder's own channel count (MP3 always comes back as stereo,
    // whatever the header says): ask with a decode stopped at once.
    const AudioInfo first = decodeAudioFile(path, [](const float*, long long) { return false; });
    const int channels = first.channels;
    if (!first.valid || channels <= 0 || first.sampleRate <= 0) {
        return false;
    }
    std::vector<float> interleaved;
    const AudioInfo info = decodeAudioFile(path, [&](const float* buf, long long frames) {
        interleaved.insert(interleaved.end(), buf, buf + frames * channels);
        return true;
    });
    if (!info.valid) {
        return false;
    }
    const long long frames = static_cast<long long>(interleaved.size()) / channels;
    if (frames <= 0) {
        return false;
    }
    const double step = double(info.sampleRate) / kRate;
    const long long outFrames = std::max<long long>(1, static_cast<long long>(frames / step));
    out.left.resize(size_t(outFrames));
    out.right.resize(size_t(outFrames));
    for (long long i = 0; i < outFrames; ++i) {
        const double src = i * step;
        const long long i0 = std::min<long long>(static_cast<long long>(src), frames - 1);
        const long long i1 = std::min<long long>(i0 + 1, frames - 1);
        const float frac = static_cast<float>(src - double(i0));
        const auto at = [&](long long f, int ch) {
            return interleaved[size_t(f * channels + std::min(ch, channels - 1))];
        };
        out.left[size_t(i)] = at(i0, 0) + (at(i1, 0) - at(i0, 0)) * frac;
        out.right[size_t(i)] = at(i0, 1) + (at(i1, 1) - at(i0, 1)) * frac;
    }
    return true;
}
}

double beatStepValue(const std::vector<double>& values, int step, double fallback)
{
    return step >= 0 && step < int(values.size()) ? values[size_t(step)] : fallback;
}

int beatStepRepeat(const BeatRow& row, int step)
{
    return step >= 0 && step < int(row.repeat.size()) ? std::clamp(row.repeat[size_t(step)], 1, 4) : 1;
}

std::vector<bool> evenRhythm(int hits, int steps)
{
    std::vector<bool> out(size_t(std::max(0, steps)), false);
    if (steps <= 0 || hits <= 0) {
        return out;
    }
    hits = std::min(hits, steps);
    // Bresenham-style spread: hit i lands on floor(i * steps / hits).
    for (int i = 0; i < hits; ++i) {
        out[size_t((i * steps) / hits)] = true;
    }
    return out;
}

bool renderBeat(const BeatPattern& pattern, double bpm, const QString& outPath, QString& error)
{
    if (bpm <= 0.0 || pattern.bars <= 0) {
        error = muse::qtrc("mixhouse", "No tempo to render the beat at.");
        return false;
    }
    const long long loopFrames = std::llround(pattern.bars * 4 * 60.0 / bpm * kRate);
    const int spb = pattern.stepsPerBar > 0 ? pattern.stepsPerBar : kBeatStepsPerBar;
    const int steps = pattern.bars * spb;
    const double stepFrames = double(loopFrames) / steps;
    std::vector<float> left(size_t(loopFrames), 0.0f), right(size_t(loopFrames), 0.0f);

    const bool anySolo = std::any_of(pattern.rows.begin(), pattern.rows.end(), [](const BeatRow& r) {
        return r.solo && !r.path.isEmpty();
    });
    std::map<QString, Shot> shots;
    bool anyHit = false;
    const long long fade = kRate / 200;   // 5 ms: a cut hit fades out, no click

    // 1. Every hit of every row.
    struct Hit {
        double start;
        float level;
        double rate;
        float panLeft, panRight;
        size_t row;
        const Shot* shot;
        double gate, drive, filter, reverb;
        bool reverse;
        double attackFrames, releaseFrames;
        long long end = 0;   // where it stops (set in 2.)
    };
    std::vector<Hit> events;
    for (size_t ri = 0; ri < pattern.rows.size(); ++ri) {
        const BeatRow& row = pattern.rows[ri];
        if (row.path.isEmpty() || row.mute || (anySolo && !row.solo)) {
            continue;
        }
        // A row shorter than the pattern repeats (polymeter): pattern step s
        // plays the row's step s % length.
        const int len = row.length > 0 ? std::min(row.length, steps) : steps;
        bool any = false;
        for (int s = 0; s < len && s < int(row.steps.size()); ++s) {
            any = any || row.steps[size_t(s)] > 0;
        }
        if (!any) {
            continue;
        }
        auto it = shots.find(row.path);
        if (it == shots.end()) {
            Shot shot;
            if (!loadShot(row.path, shot)) {
                continue;   // a file that can't be read stays silent
            }
            it = shots.emplace(row.path, std::move(shot)).first;
        }
        const Shot* shot = &it->second;
        const double pan = std::clamp(row.pan, -1.0, 1.0);
        // MPC swing: the second 16th lands at swing% of the 8th note; this row
        // takes its swing-mix share of it.
        const double swingFrames = std::clamp(pattern.swing, 0.0, 1.0) * std::clamp(row.swingMix, 0.0, 1.0)
                                   * stepFrames * 0.5;
        for (int ps = 0; ps < steps; ++ps) {
            const int step = ps % len;
            if (step >= int(row.steps.size()) || row.steps[size_t(step)] <= 0) {
                continue;
            }
            // Swing moves every second step of a straight grid (not triplets).
            const double at = ps * stepFrames + (spb % 3 != 0 && ps % 2 == 1 ? swingFrames : 0.0);
            // Rep 3 is three hits inside the step; Shift starts them late.
            const int reps = beatStepRepeat(row, step);
            const double shift = std::clamp(beatStepValue(row.shift, step, 0.0), 0.0, 1.0) * stepFrames;
            const double vel = std::clamp(beatStepValue(row.velocity, step, kBeatDefaultVelocity), 0.0, 1.0);
            const double semis = std::clamp(beatStepValue(row.pitch, step, 0.0) + row.keyShift, -36.0, 36.0);
            const double p = std::clamp(pan + beatStepValue(row.stepPan, step, 0.0), -1.0, 1.0);
            for (int r = 0; r < reps; ++r) {
                events.push_back({ at + shift + r * stepFrames / reps,
                                   static_cast<float>(vel * std::clamp(row.gain, 0.0, 1.0)),
                                   std::pow(2.0, semis / 12.0),
                                   static_cast<float>(std::min(1.0, 1.0 - p)),
                                   static_cast<float>(std::min(1.0, 1.0 + p)), ri, shot,
                                   std::clamp(beatStepValue(row.gate, step, 1.0), 0.05, 1.0),
                                   std::clamp(beatStepValue(row.drive, step, 0.0), 0.0, 1.0),
                                   std::clamp(beatStepValue(row.filter, step, 1.0), 0.0, 1.0),
                                   std::clamp(beatStepValue(row.reverb, step, 0.0), 0.0, 1.0),
                                   beatStepValue(row.reverse, step, 0.0) > 0.5,
                                   std::max(0.0, row.attack) * kRate, std::max(0.0, row.release) * kRate });
            }
        }
    }
    std::sort(events.begin(), events.end(), [](const Hit& a, const Hit& b) { return a.start < b.start; });

    // 2. Where each hit stops: its own length (pitch plays it faster or slower),
    // the next hit on its row (Cut itself), the next hit of another row in its
    // choke group (a closed hat chops the open hat). Wraps round the loop.
    for (size_t h = 0; h < events.size(); ++h) {
        Hit& e = events[h];
        const BeatRow& row = pattern.rows[e.row];
        const long long start = std::llround(e.start);
        long long end = start + std::min(static_cast<long long>(double(e.shot->left.size()) / e.rate * e.gate), loopFrames);
        for (size_t k = 1; k < events.size() + 1; ++k) {
            const Hit& n = events[(h + k) % events.size()];
            const long long nStart = std::llround(n.start) + ((h + k) >= events.size() ? loopFrames : 0);
            if (nStart >= end) {
                break;
            }
            const BeatRow& other = pattern.rows[n.row];
            const bool sameRow = n.row == e.row;
            if ((sameRow && row.cutSelf) || (!sameRow && row.choke > 0 && other.choke == row.choke)) {
                end = nStart;
                break;
            }
        }
        e.end = end;
    }

    // 3. Play them, each hit through its own step effects; the reverb sends
    // are gathered and played through one room afterwards.
    std::vector<float> sendL(size_t(loopFrames), 0.0f), sendR(size_t(loopFrames), 0.0f);
    bool anySend = false;
    for (const Hit& e : events) {
        const long long start = std::llround(e.start);
        const long long natural = static_cast<long long>(double(e.shot->left.size()) / e.rate);
        const long long length = std::max<long long>(0, e.end - start);
        const long long shotFrames = static_cast<long long>(e.shot->left.size());
        // Filter: a two-pole low-pass, 1 = open (20 kHz) .. 0 = 150 Hz.
        const double cutoff = 150.0 * std::pow(20000.0 / 150.0, e.filter);
        const double a = 1.0 - std::exp(-2.0 * 3.14159265358979 * cutoff / kRate);
        double l1 = 0, l2 = 0, r1 = 0, r2 = 0;
        const bool filtering = e.filter < 0.999;
        const double drive = 1.0 + e.drive * 15.0;
        const double driveNorm = e.drive > 0.001 ? 1.0 / std::tanh(drive) : 1.0;
        for (long long i = 0; i < length; ++i) {
            float g = e.level;
            const long long left2end = length - i;
            if (length < natural && left2end < fade) {
                g *= float(left2end) / float(fade);
            }
            // Attack and release: fade in from the start, fade out to the end.
            if (e.attackFrames > 1.0 && i < e.attackFrames) {
                g *= float(i / e.attackFrames);
            }
            if (e.releaseFrames > 1.0 && left2end < e.releaseFrames) {
                g *= float(left2end / e.releaseFrames);
            }
            // Reverse: read the one-shot from its end (the part the gate keeps).
            double src = i * e.rate;
            if (e.reverse) {
                src = std::max(0.0, double(std::min(natural, length)) * e.rate - 1.0 - src);
            }
            const size_t i0 = std::min(size_t(src), size_t(shotFrames - 1));
            const size_t i1 = std::min(i0 + 1, size_t(shotFrames - 1));
            const float frac = static_cast<float>(src - double(i0));
            double l = e.shot->left[i0] + (e.shot->left[i1] - e.shot->left[i0]) * frac;
            double rr = e.shot->right[i0] + (e.shot->right[i1] - e.shot->right[i0]) * frac;
            if (e.drive > 0.001) {
                l = std::tanh(l * drive) * driveNorm;
                rr = std::tanh(rr * drive) * driveNorm;
            }
            if (filtering) {
                l1 += a * (l - l1); l2 += a * (l1 - l2); l = l2;
                r1 += a * (rr - r1); r2 += a * (r1 - r2); rr = r2;
            }
            const size_t at = size_t(((start + i) % loopFrames + loopFrames) % loopFrames);
            const float outL = float(l) * g * e.panLeft;
            const float outR = float(rr) * g * e.panRight;
            left[at] += outL;
            right[at] += outR;
            if (e.reverb > 0.001) {
                sendL[at] += outL * float(e.reverb);
                sendR[at] += outR * float(e.reverb);
                anySend = true;
            }
        }
        anyHit = true;
    }
    if (anySend) {
        // A small room (four combs and two all-passes per side, Schroeder style).
        // Run over the loop twice so its tail wraps into the start: no seam.
        const int combLen[4] = { 1557, 1617, 1491, 1422 };
        const int apLen[2] = { 556, 441 };
        for (int side = 0; side < 2; ++side) {
            const std::vector<float>& in = side ? sendR : sendL;
            std::vector<float>& out = side ? right : left;
            const int spread = side ? 23 : 0;
            std::vector<std::vector<float> > comb(4), ap(2);
            std::vector<size_t> ci(4, 0), ai(2, 0);
            std::vector<float> lp(4, 0.0f);
            for (int c = 0; c < 4; ++c) comb[c].assign(size_t(combLen[c] + spread), 0.0f);
            for (int c = 0; c < 2; ++c) ap[c].assign(size_t(apLen[c] + spread), 0.0f);
            for (int pass = 0; pass < 2; ++pass) {
                for (long long i = 0; i < loopFrames; ++i) {
                    const float x = in[size_t(i)] * 0.3f;
                    float y = 0.0f;
                    for (int c = 0; c < 4; ++c) {
                        const float d = comb[c][ci[c]];
                        lp[c] = d * 0.8f + lp[c] * 0.2f;     // damping
                        comb[c][ci[c]] = x + lp[c] * 0.84f;  // room size
                        ci[c] = (ci[c] + 1) % comb[c].size();
                        y += d;
                    }
                    for (int c = 0; c < 2; ++c) {
                        const float d = ap[c][ai[c]];
                        ap[c][ai[c]] = y + d * 0.5f;
                        y = d - y * 0.5f;
                        ai[c] = (ai[c] + 1) % ap[c].size();
                    }
                    if (pass == 1) {
                        out[size_t(i)] += y;
                    }
                }
            }
        }
    }
    if (!anyHit) {
        error = muse::qtrc("mixhouse", "The beat has no hits yet.");
        return false;
    }
    // Never clip: a loud pattern is turned down as a whole.
    float peak = 0.0f;
    for (size_t i = 0; i < left.size(); ++i) {
        peak = std::max({ peak, std::abs(left[i]), std::abs(right[i]) });
    }
    const float scale = peak > 0.98f ? 0.98f / peak : 1.0f;

    SF_INFO info {};
    info.samplerate = kRate;
    info.channels = 2;
    info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_24;
    SNDFILE* file = openSndfile(outPath, SFM_WRITE, &info);
    if (!file) {
        error = muse::qtrc("mixhouse", "Could not write %1.").arg(outPath);
        return false;
    }
    std::vector<float> interleaved(size_t(loopFrames) * 2);
    for (size_t i = 0; i < left.size(); ++i) {
        interleaved[i * 2] = left[i] * scale;
        interleaved[i * 2 + 1] = right[i] * scale;
    }
    sf_writef_float(file, interleaved.data(), loopFrames);
    sf_close(file);
    return true;
}

QString beatPatternPathFor(const QString& wavPath)
{
    const QFileInfo fi(wavPath);
    return fi.absolutePath() + QStringLiteral("/") + fi.completeBaseName() + QStringLiteral(".glidebeat");
}

namespace {
template<typename T>
QJsonArray toJson(const std::vector<T>& values)
{
    QJsonArray out;
    for (const T& v : values) {
        out.append(v);
    }
    return out;
}
std::vector<double> doublesFrom(const QJsonValue& v)
{
    std::vector<double> out;
    for (const QJsonValue& x : v.toArray()) {
        out.push_back(x.toDouble());
    }
    return out;
}
}

bool saveBeatPattern(const BeatPattern& pattern, double bpm, const QString& path)
{
    QJsonArray rows;
    for (const BeatRow& r : pattern.rows) {
        QJsonArray steps;
        for (int v : r.steps) {
            steps.append(v);
        }
        rows.append(QJsonObject {
            { QStringLiteral("path"), r.path },
            { QStringLiteral("mute"), r.mute },
            { QStringLiteral("solo"), r.solo },
            { QStringLiteral("gain"), r.gain },
            { QStringLiteral("pan"), r.pan },
            { QStringLiteral("cutSelf"), r.cutSelf },
            { QStringLiteral("swingMix"), r.swingMix },
            { QStringLiteral("role"), r.role },
            { QStringLiteral("locked"), r.locked },
            { QStringLiteral("length"), r.length },
            { QStringLiteral("choke"), r.choke },
            { QStringLiteral("attack"), r.attack },
            { QStringLiteral("release"), r.release },
            { QStringLiteral("keyShift"), r.keyShift },
            { QStringLiteral("steps"), steps },
            { QStringLiteral("velocity"), toJson(r.velocity) },
            { QStringLiteral("pitch"), toJson(r.pitch) },
            { QStringLiteral("stepPan"), toJson(r.stepPan) },
            { QStringLiteral("shift"), toJson(r.shift) },
            { QStringLiteral("repeat"), toJson(r.repeat) },
            { QStringLiteral("gate"), toJson(r.gate) },
            { QStringLiteral("reverse"), toJson(r.reverse) },
            { QStringLiteral("drive"), toJson(r.drive) },
            { QStringLiteral("filter"), toJson(r.filter) },
            { QStringLiteral("reverb"), toJson(r.reverb) },
        });
    }
    const QJsonObject root {
        { QStringLiteral("glidemixBeat"), 1 },
        { QStringLiteral("bars"), pattern.bars },
        { QStringLiteral("stepsPerBar"), pattern.stepsPerBar },
        { QStringLiteral("swing"), pattern.swing },
        { QStringLiteral("bpm"), bpm },
        { QStringLiteral("rows"), rows },
    };
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

bool loadBeatPattern(const QString& path, BeatPattern& pattern)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    if (root.value(QStringLiteral("glidemixBeat")).toInt() < 1) {
        return false;
    }
    BeatPattern out;
    const int bars = root.value(QStringLiteral("bars")).toInt(1);
    out.bars = bars <= 1 ? 1 : bars <= 2 ? 2 : bars <= 4 ? 4 : bars <= 8 ? 8 : 16;
    const int spb = root.value(QStringLiteral("stepsPerBar")).toInt(16);
    out.stepsPerBar = spb == 32 || spb == 24 ? spb : 16;
    out.swing = std::clamp(root.value(QStringLiteral("swing")).toDouble(), 0.0, 1.0);
    for (const QJsonValue& v : root.value(QStringLiteral("rows")).toArray()) {
        if (int(out.rows.size()) >= kBeatMaxRows) {
            break;
        }
        const QJsonObject o = v.toObject();
        BeatRow r;
        r.path = o.value(QStringLiteral("path")).toString();
        r.mute = o.value(QStringLiteral("mute")).toBool();
        r.solo = o.value(QStringLiteral("solo")).toBool();
        r.gain = std::clamp(o.value(QStringLiteral("gain")).toDouble(0.78), 0.0, 1.0);
        r.pan = std::clamp(o.value(QStringLiteral("pan")).toDouble(0.0), -1.0, 1.0);
        r.cutSelf = o.value(QStringLiteral("cutSelf")).toBool(true);
        r.swingMix = std::clamp(o.value(QStringLiteral("swingMix")).toDouble(1.0), 0.0, 1.0);
        r.role = o.value(QStringLiteral("role")).toString();
        r.locked = o.value(QStringLiteral("locked")).toBool();
        r.length = std::clamp(o.value(QStringLiteral("length")).toInt(0), 0, 256);
        r.choke = std::clamp(o.value(QStringLiteral("choke")).toInt(0), 0, 4);
        for (const QJsonValue& s : o.value(QStringLiteral("steps")).toArray()) {
            r.steps.push_back(std::clamp(s.toInt(), 0, 3));
        }
        r.velocity = doublesFrom(o.value(QStringLiteral("velocity")));
        r.pitch = doublesFrom(o.value(QStringLiteral("pitch")));
        r.stepPan = doublesFrom(o.value(QStringLiteral("stepPan")));
        r.shift = doublesFrom(o.value(QStringLiteral("shift")));
        r.gate = doublesFrom(o.value(QStringLiteral("gate")));
        r.reverse = doublesFrom(o.value(QStringLiteral("reverse")));
        r.drive = doublesFrom(o.value(QStringLiteral("drive")));
        r.filter = doublesFrom(o.value(QStringLiteral("filter")));
        r.reverb = doublesFrom(o.value(QStringLiteral("reverb")));
        for (const QJsonValue& x : o.value(QStringLiteral("repeat")).toArray()) {
            r.repeat.push_back(std::clamp(x.toInt(1), 1, 4));
        }
        r.steps.resize(size_t(out.bars * out.stepsPerBar), 0);
        r.attack = std::clamp(o.value(QStringLiteral("attack")).toDouble(0.0), 0.0, 2.0);
        r.release = std::clamp(o.value(QStringLiteral("release")).toDouble(0.0), 0.0, 2.0);
        r.keyShift = std::clamp(o.value(QStringLiteral("keyShift")).toInt(0), -24, 24);
        out.rows.push_back(r);
    }
    pattern = out;
    return true;
}

namespace {
std::vector<QString>& beatQueue()
{
    static std::vector<QString> queue;
    return queue;
}
}

void queueBeatSample(const QString& path)
{
    beatQueue().push_back(path);
}

std::vector<QString> takeQueuedBeatSamples()
{
    std::vector<QString> out;
    out.swap(beatQueue());
    return out;
}
}
