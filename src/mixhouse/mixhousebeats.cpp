/*
 * MixHouse DJ 2
 */
#include "mixhousebeats.h"

#include <cmath>
#include <cstdint>
#include <cstring>

using namespace au::mixhouse;

namespace {
// --- minimal protobuf wire reader ------------------------------------------
// Wire types used by beats.proto: 0 varint, 1 fixed64, 2 length-delimited.
struct Reader {
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

    double fixed64()
    {
        if (end - p < 8) { ok = false; return 0.0; }
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i) {           // little-endian on the wire
            v |= static_cast<uint64_t>(*p++) << (8 * i);
        }
        double d;
        static_assert(sizeof(d) == sizeof(v), "double must be 8 bytes");
        std::memcpy(&d, &v, sizeof(d));
        return d;
    }

    Reader sub()
    {
        const uint64_t n = varint();
        if (!ok || static_cast<uint64_t>(end - p) < n) { ok = false; return { p, p }; }
        Reader r{ p, p + n };
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

// A negative int32 is sign-extended to a 64-bit varint on the wire, so take the
// low 32 bits and reinterpret. 2^64-88 -> 0xFFFFFFA8 -> -88.
inline int32_t asInt32(uint64_t raw)
{
    return static_cast<int32_t>(static_cast<uint32_t>(raw & 0xFFFFFFFFull));
}

// Beat { frame_position = 1 }
bool readBeatFramePos(Reader r, int32_t* out)
{
    while (!r.atEnd() && r.ok) {
        const uint64_t key = r.varint();
        if (!r.ok) { break; }
        const int field = static_cast<int>(key >> 3);
        const int wt = static_cast<int>(key & 7);
        if (field == 1 && wt == 0) {
            *out = asInt32(r.varint());
            if (!r.ok) { return false; }
            return true;
        }
        r.skip(wt);
    }
    return false;
}

// --- minimal protobuf wire WRITER (encodeBeatGrid's counterpart to Reader) --
void appendVarint(QByteArray& out, uint64_t value)
{
    while (value >= 0x80) {
        out.append(static_cast<char>((value & 0x7f) | 0x80));
        value >>= 7;
    }
    out.append(static_cast<char>(value & 0x7f));
}

void appendTag(QByteArray& out, int field, int wireType)
{
    appendVarint(out, (static_cast<uint64_t>(field) << 3) | static_cast<uint64_t>(wireType));
}

// Sign-extends a negative value into the full 64-bit varint, mirroring
// asInt32()'s own "take the low 32 bits back out" reversal above.
void appendVarintField(QByteArray& out, int field, int64_t value)
{
    appendTag(out, field, 0);
    appendVarint(out, static_cast<uint64_t>(value));
}

void appendFixed64Field(QByteArray& out, int field, double value)
{
    appendTag(out, field, 1);
    uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value), "double must be 8 bytes");
    std::memcpy(&bits, &value, sizeof(bits));
    for (int i = 0; i < 8; ++i) {
        out.append(static_cast<char>((bits >> (8 * i)) & 0xFF));
    }
}

void appendLengthDelimitedField(QByteArray& out, int field, const QByteArray& payload)
{
    appendTag(out, field, 2);
    appendVarint(out, static_cast<uint64_t>(payload.size()));
    out.append(payload);
}

// Bpm { bpm = 1 }
bool readBpm(Reader r, double* out)
{
    while (!r.atEnd() && r.ok) {
        const uint64_t key = r.varint();
        if (!r.ok) { break; }
        const int field = static_cast<int>(key >> 3);
        const int wt = static_cast<int>(key & 7);
        if (field == 1 && wt == 1) {
            *out = r.fixed64();
            return r.ok;
        }
        r.skip(wt);
    }
    return false;
}
}  // namespace

int MixHouseBeatGrid::beatIndexAt(double sec) const
{
    if (!valid) {
        return 0;
    }
    if (!constantTempo) {
        int idx = -1;
        for (size_t i = 0; i < beatSecs.size(); ++i) {
            if (beatSecs[i] > sec) { break; }
            idx = static_cast<int>(i);
        }
        return idx;
    }
    if (beatLengthSec <= 0.0) {
        return 0;
    }
    return static_cast<int>(std::floor((sec - firstBeatSec) / beatLengthSec));
}

MixHouseBeatGrid au::mixhouse::decodeBeats(const QByteArray& blob,
                                           const QString& beatsVersion,
                                           int sampleRate)
{
    MixHouseBeatGrid g;
    if (blob.isEmpty() || sampleRate <= 0) {
        return g;
    }

    const auto* base = reinterpret_cast<const uint8_t*>(blob.constData());
    Reader top{ base, base + blob.size() };

    const bool isMap = beatsVersion.startsWith(QStringLiteral("BeatMap"));

    if (isMap) {
        // BeatMap { repeated Beat beat = 1 }
        while (!top.atEnd() && top.ok) {
            const uint64_t key = top.varint();
            if (!top.ok) { break; }
            const int field = static_cast<int>(key >> 3);
            const int wt = static_cast<int>(key & 7);
            if (field == 1 && wt == 2) {
                int32_t frame = 0;
                Reader beat = top.sub();
                if (top.ok && readBeatFramePos(beat, &frame)) {
                    g.beatSecs.push_back(static_cast<double>(frame) / sampleRate);
                }
            } else {
                top.skip(wt);
            }
        }
        if (g.beatSecs.size() >= 2) {
            g.valid = true;
            g.constantTempo = false;
            g.firstBeatSec = g.beatSecs.front();
            const double span = g.beatSecs.back() - g.beatSecs.front();
            const double n = static_cast<double>(g.beatSecs.size() - 1);
            g.beatLengthSec = (n > 0.0) ? span / n : 0.0;   // average, for display
            g.bpm = (g.beatLengthSec > 0.0) ? 60.0 / g.beatLengthSec : 0.0;
        }
        return g;
    }

    // BeatGrid { Bpm bpm = 1; Beat first_beat = 2 }
    double bpm = 0.0;
    int32_t firstFrame = 0;
    bool haveBpm = false, haveFirst = false;
    while (!top.atEnd() && top.ok) {
        const uint64_t key = top.varint();
        if (!top.ok) { break; }
        const int field = static_cast<int>(key >> 3);
        const int wt = static_cast<int>(key & 7);
        if (field == 1 && wt == 2) {
            Reader sub = top.sub();
            if (top.ok) { haveBpm = readBpm(sub, &bpm); }
        } else if (field == 2 && wt == 2) {
            Reader sub = top.sub();
            if (top.ok) { haveFirst = readBeatFramePos(sub, &firstFrame); }
        } else {
            top.skip(wt);
        }
    }

    if (haveBpm && haveFirst && bpm > 0.0) {
        g.valid = true;
        g.constantTempo = true;
        g.bpm = bpm;
        g.beatLengthSec = 60.0 / bpm;
        g.firstBeatSec = static_cast<double>(firstFrame) / sampleRate;
    }
    return g;
}

QByteArray au::mixhouse::encodeBeatGrid(double bpm, double firstBeatSec, int sampleRate)
{
    QByteArray bpmMsg;
    appendFixed64Field(bpmMsg, 1, bpm);

    const int32_t firstBeatFrame = static_cast<int32_t>(std::llround(firstBeatSec * sampleRate));
    QByteArray beatMsg;
    appendVarintField(beatMsg, 1, static_cast<int64_t>(firstBeatFrame));

    QByteArray grid;
    appendLengthDelimitedField(grid, 1, bpmMsg);   // BeatGrid.bpm = 1
    appendLengthDelimitedField(grid, 2, beatMsg);  // BeatGrid.first_beat = 2
    return grid;
}
