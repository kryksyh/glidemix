/*
 * MixHouse DJ 2
 */
#include <QColor>
#include "mixhousewaveform.h"

#include <algorithm>

using namespace au::mixhouse;

namespace {
// --- minimal protobuf wire reader -------------------------------------------
// Identical shape to mixhousebeats.cpp's own (differently-named, to avoid a
// unity-build symbol clash between the two files' anonymous namespaces)
// Reader struct -- same tiny hand-rolled wire-format walker, extended here
// for a repeated scalar field and one more level of submessage nesting.
// Wire types used by waveform.proto: 0 varint, 2 length-delimited.
struct WaveformProtoReader {
    const uint8_t* p;
    const uint8_t* end;
    bool ok = true;

    bool atEnd() const { return p >= end; }

    uint64_t varint()
    {
        uint64_t r = 0;
        int shift = 0;
        while (p < end) {
            const uint8_t b = *p++;
            r |= static_cast<uint64_t>(b & 0x7f) << shift;
            if (!(b & 0x80)) {
                return r;
            }
            shift += 7;
            if (shift > 63) {
                break;
            }
        }
        ok = false;
        return 0;
    }

    WaveformProtoReader sub()
    {
        const uint64_t n = varint();
        if (!ok || static_cast<uint64_t>(end - p) < n) { ok = false; return { p, p }; }
        WaveformProtoReader r{ p, p + n };
        p += n;
        return r;
    }

    void skip(int wireType)
    {
        switch (wireType) {
        case 0: varint(); break;
        case 1: if (end - p >= 8) { p += 8; } else { ok = false; } break;
        case 2: { const uint64_t n = varint();
                  if (ok && static_cast<uint64_t>(end - p) >= n) { p += n; } else { ok = false; } break; }
        case 5: if (end - p >= 4) { p += 4; } else { ok = false; } break;
        default: ok = false; break;
        }
    }
};

// Signal { repeated int32 value = 1; ... } -- appends every occurrence of
// field 1 found, since this field carries NO [packed=true] in the real
// .proto (proto2 default: unpacked, one (tag, varint) pair per value,
// repeated N times) rather than one length-delimited packed blob. A packed
// encoding (wire type 2) is also handled defensively in case an alternate
// encoder ever produced one, even though the real schema doesn't ask for it.
void readSignalValues(WaveformProtoReader r, std::vector<uint8_t>* out)
{
    while (!r.atEnd() && r.ok) {
        const uint64_t key = r.varint();
        if (!r.ok) { break; }
        const int field = static_cast<int>(key >> 3);
        const int wt = static_cast<int>(key & 7);
        if (field == 1 && wt == 0) {
            const uint64_t v = r.varint();
            if (!r.ok) { break; }
            out->push_back(static_cast<uint8_t>(v & 0xFF));
        } else if (field == 1 && wt == 2) {
            WaveformProtoReader packed = r.sub();
            if (!r.ok) { break; }
            while (!packed.atEnd() && packed.ok) {
                const uint64_t v = packed.varint();
                if (!packed.ok) { break; }
                out->push_back(static_cast<uint8_t>(v & 0xFF));
            }
        } else {
            r.skip(wt);
        }
    }
}

// FilteredSignal { Signal low = 1; Signal mid = 2; Signal high = 3; ... }
void readFilteredSignal(WaveformProtoReader r, std::vector<uint8_t>* low, std::vector<uint8_t>* mid,
                        std::vector<uint8_t>* high)
{
    while (!r.atEnd() && r.ok) {
        const uint64_t key = r.varint();
        if (!r.ok) { break; }
        const int field = static_cast<int>(key >> 3);
        const int wt = static_cast<int>(key & 7);
        if (field == 1 && wt == 2) {
            WaveformProtoReader s = r.sub();
            if (r.ok) { readSignalValues(s, low); }
        } else if (field == 2 && wt == 2) {
            WaveformProtoReader s = r.sub();
            if (r.ok) { readSignalValues(s, mid); }
        } else if (field == 3 && wt == 2) {
            WaveformProtoReader s = r.sub();
            if (r.ok) { readSignalValues(s, high); }
        } else {
            r.skip(wt);
        }
    }
}
}  // namespace

MixHouseWaveformOverview au::mixhouse::decodeWaveformOverview(const QByteArray& protobufBytes)
{
    MixHouseWaveformOverview w;
    if (protobufBytes.isEmpty()) {
        return w;
    }

    const auto* base = reinterpret_cast<const uint8_t*>(protobufBytes.constData());
    WaveformProtoReader top{ base, base + protobufBytes.size() };

    std::vector<uint8_t> all, low, mid, high;
    // Waveform { ... Signal signal_all = 3; FilteredSignal signal_filtered = 4; ... }
    while (!top.atEnd() && top.ok) {
        const uint64_t key = top.varint();
        if (!top.ok) { break; }
        const int field = static_cast<int>(key >> 3);
        const int wt = static_cast<int>(key & 7);
        if (field == 3 && wt == 2) {
            WaveformProtoReader s = top.sub();
            if (top.ok) { readSignalValues(s, &all); }
        } else if (field == 4 && wt == 2) {
            WaveformProtoReader s = top.sub();
            if (top.ok) { readFilteredSignal(s, &low, &mid, &high); }
        } else {
            top.skip(wt);
        }
    }

    if (all.empty()) {
        return w;   // no signal_all data at all -- nothing to draw
    }
    w.all = std::move(all);
    w.low = std::move(low);
    w.mid = std::move(mid);
    w.high = std::move(high);
    w.valid = true;
    return w;
}

std::vector<MixHouseOverviewBucket> au::mixhouse::resampleOverview(
    const MixHouseWaveformOverview& overview, int bucketCount)
{
    std::vector<MixHouseOverviewBucket> out;
    if (!overview.valid || bucketCount <= 0 || overview.all.empty()) {
        return out;
    }
    out.resize(static_cast<size_t>(bucketCount));

    // signal_all/signal_filtered interleave 2 audio channels per real visual
    // sample (Signal::channels defaults to 2) -- collapse L/R by taking the
    // max of each pair first, matching mixxx's own real renderer rule ("max
    // across the visual samples, both channels, in this pixel's window";
    // waveform/renderers/waveformrendererrgb.cpp).
    const size_t visualSamples = overview.all.size() / 2;
    if (visualSamples == 0) {
        return out;
    }

    auto maxOfPair = [](const std::vector<uint8_t>& v, size_t i) -> uint8_t {
        const size_t a = 2 * i;
        const size_t b = 2 * i + 1;
        if (a >= v.size()) { return 0; }
        if (b >= v.size()) { return v[a]; }
        return std::max(v[a], v[b]);
    };

    for (int bucket = 0; bucket < bucketCount; ++bucket) {
        const size_t startI = (visualSamples * static_cast<size_t>(bucket)) / static_cast<size_t>(bucketCount);
        size_t endI = (visualSamples * static_cast<size_t>(bucket + 1)) / static_cast<size_t>(bucketCount);
        if (endI <= startI) {
            endI = std::min(startI + 1, visualSamples);
        }

        uint8_t mAll = 0, mLow = 0, mMid = 0, mHigh = 0;
        size_t tallestI = startI;
        uint8_t tallestAll = 0;
        bool haveTallest = false;
        for (size_t i = startI; i < endI; ++i) {
            const uint8_t columnAll = maxOfPair(overview.all, i);
            if (!haveTallest || columnAll > tallestAll) {
                tallestI = i;
                tallestAll = columnAll;
                haveTallest = true;
            }
            mAll = std::max(mAll, columnAll);
            if (!overview.low.empty())  { mLow  = std::max(mLow,  maxOfPair(overview.low,  i)); }
            if (!overview.mid.empty())  { mMid  = std::max(mMid,  maxOfPair(overview.mid,  i)); }
            if (!overview.high.empty()) { mHigh = std::max(mHigh, maxOfPair(overview.high, i)); }
        }
        MixHouseOverviewBucket resampled{ mLow, mMid, mHigh, mAll };
        if (overview.rekordboxPreview && !overview.low.empty() && !overview.mid.empty() && !overview.high.empty()) {
            // Average the bands over the window, widened to at least three
            // columns (about what rekordbox's own smoothing spans), and only then
            // pick a colour and height. Measured against rekordbox's own screen:
            //   hue         the bands as R, G, B, normalised by the strongest
            //   saturation  0.573 * (1 - min/max) + 0.0011 * max + 0.088
            //   brightness  0.0017 * max + 0.722
            //   height      0.115 * max + 1 px of a 19 px row
            size_t from = startI;
            size_t to = endI;
            while (to - from < 3 && (from > 0 || to < visualSamples)) {
                if (from > 0) { --from; }
                if (to < visualSamples && to - from < 3) { ++to; }
            }
            double sumLow = 0, sumMid = 0, sumHigh = 0;
            for (size_t i = from; i < to; ++i) {
                sumLow += maxOfPair(overview.low, i) / 2.0;
                sumMid += maxOfPair(overview.mid, i) / 2.0;
                sumHigh += maxOfPair(overview.high, i) / 2.0;
            }
            const double n = static_cast<double>(to - from);
            const double aLow = sumLow / n, aMid = sumMid / n, aHigh = sumHigh / n;
            const double peakBand = std::max({ aLow, aMid, aHigh });
            const double floorBand = std::min({ aLow, aMid, aHigh });
            if (peakBand > 0.0) {
                const QColor chroma = QColor::fromRgbF(static_cast<float>(aLow / peakBand),
                                                       static_cast<float>(aMid / peakBand),
                                                       static_cast<float>(aHigh / peakBand));
                const float hue = std::max(0.0f, chroma.hsvHueF());
                const float sat = std::clamp(0.573f * static_cast<float>(1.0 - floorBand / peakBand)
                                             + 0.0011f * static_cast<float>(peakBand) + 0.088f, 0.05f, 1.0f);
                const float val = std::clamp(0.0017f * static_cast<float>(peakBand) + 0.722f, 0.4f, 1.0f);
                resampled.color = QColor::fromHsvF(hue, sat, val).rgb() & 0xFFFFFF;
                resampled.hasColor = true;
                resampled.all = static_cast<uint8_t>(
                    std::clamp((0.115 * peakBand + 1.0) / 19.0, 0.0, 1.0) * 255.0);
                out[static_cast<size_t>(bucket)] = resampled;
                continue;
            }
        }
        // rekordbox draws each column in its own stored colour and never blends
        // neighbours, so a bucket keeps the colour of its tallest column rather
        // than an average -- averaging is what turns crisp pink/cyan into mud.
        if (haveTallest && tallestI < overview.rgb.size()) {
            resampled.color = overview.rgb[tallestI];
            resampled.hasColor = true;
        }
        out[static_cast<size_t>(bucket)] = resampled;
    }
    return out;
}

namespace {
// --- minimal protobuf wire writer -------------------------------------------
// Mirrors mixhousebeats.cpp's own encodeBeatGrid() helpers exactly (varint/
// tag/length-delimited), duplicated with a distinct name prefix rather than
// shared: this module's CMake build is a UNITY build (every .cpp in
// src/mixhouse/ concatenated into one translation unit), and anonymous-
// namespace names must stay globally unique across every file in it, not
// just per-file -- a real collision already happened once this project
// (see mixhousebeats.cpp/mixhousewaveform.cpp's own Reader/WaveformProtoReader
// history above).
void waveformAppendVarint(QByteArray& out, quint64 value)
{
    while (value >= 0x80) {
        out.append(static_cast<char>((value & 0x7f) | 0x80));
        value >>= 7;
    }
    out.append(static_cast<char>(value));
}

void waveformAppendTag(QByteArray& out, int fieldNumber, int wireType)
{
    waveformAppendVarint(out, (static_cast<quint64>(fieldNumber) << 3) | static_cast<quint64>(wireType));
}

// Builds one Signal { repeated int32 value = 1; optional int32 channels = 2; }
// submessage body (NOT yet wrapped in its own outer tag+length -- the
// caller does that, since signal_all and each signal_filtered band are
// wrapped at different field numbers). Each entry appended TWICE:
// resampleOverview() always divides by 2 to undo real mixxx's genuine L/R
// interleaving, so mono data must fake that shape to come back out the
// other end correctly -- see encodeWaveformOverview()'s own header comment.
QByteArray waveformEncodeSignal(const std::vector<uint8_t>& monoValues)
{
    QByteArray signal;
    for (uint8_t v : monoValues) {
        waveformAppendTag(signal, 1, 0);
        waveformAppendVarint(signal, v);
        waveformAppendTag(signal, 1, 0);
        waveformAppendVarint(signal, v);
    }
    // Signal.channels = 2 (field 2) -- explicit rather than relying on the
    // proto default, since this is (nominally) claiming 2 interleaved
    // channels per the point above.
    waveformAppendTag(signal, 2, 0);
    waveformAppendVarint(signal, 2);
    return signal;
}

void waveformAppendSubmessage(QByteArray& out, int fieldNumber, const QByteArray& body)
{
    waveformAppendTag(out, fieldNumber, 2);
    waveformAppendVarint(out, static_cast<quint64>(body.size()));
    out.append(body);
}
} // namespace

QByteArray au::mixhouse::encodeWaveformOverview(const std::vector<uint8_t>& monoAmplitude,
                                                 const std::vector<uint8_t>& low,
                                                 const std::vector<uint8_t>& mid,
                                                 const std::vector<uint8_t>& high)
{
    QByteArray waveform;

    // Waveform.signal_all (field 3, length-delimited Signal submessage).
    waveformAppendSubmessage(waveform, 3, waveformEncodeSignal(monoAmplitude));

    // Waveform.signal_filtered (field 4, length-delimited FilteredSignal
    // submessage: Signal low = 1; Signal mid = 2; Signal high = 3;) -- only
    // written when real band data is actually available (see this
    // function's own header comment). Omitting it entirely for an older/
    // amplitude-only caller is exactly what already renders as solid black,
    // matching real mixxx's own convention for a track with no filtered
    // data yet.
    if (!low.empty() && !mid.empty() && !high.empty()) {
        QByteArray filtered;
        waveformAppendSubmessage(filtered, 1, waveformEncodeSignal(low));
        waveformAppendSubmessage(filtered, 2, waveformEncodeSignal(mid));
        waveformAppendSubmessage(filtered, 3, waveformEncodeSignal(high));
        waveformAppendSubmessage(waveform, 4, filtered);
    }

    // Real files on disk are qCompress()'d (confirmed against the own
    // real analysis files earlier this project) -- overviewAnalysisBlob()
    // already qUncompress()es on read, so this must produce exactly that
    // container format.
    return qCompress(waveform);
}
