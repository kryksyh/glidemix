/*
 * GlideMix -- playback engine: see MixtapeSpectrumTap.h.
 */
#include "MixtapeSpectrumTap.h"

#include <algorithm>

namespace mixtape {
SpectrumTap& SpectrumTap::Instance()
{
    static SpectrumTap tap;
    return tap;
}

void SpectrumTap::Write(const float* const* planar, size_t channels, size_t frames, double sampleRate,
                        Clock::time_point heardAt)
{
    if (channels == 0 || frames == 0) {
        return;
    }
    const uint64_t start = mWritten.load(std::memory_order_relaxed);
    const float scale = 1.0f / static_cast<float>(channels);
    for (size_t i = 0; i < frames; ++i) {
        float sum = 0.0f;
        for (size_t c = 0; c < channels; ++c) {
            sum += planar[c][i];
        }
        mRing[(start + i) & (kSize - 1)] = sum * scale;
    }
    mSampleRate.store(sampleRate, std::memory_order_relaxed);
    mWritten.store(start + frames, std::memory_order_release);
    const uint64_t n = mMarkCount.load(std::memory_order_relaxed);
    Mark& mark = mMarks[n % kMarks];
    mark.start.store(start, std::memory_order_relaxed);
    mark.frames.store(static_cast<uint32_t>(frames), std::memory_order_relaxed);
    mark.heardNs.store(std::chrono::duration_cast<std::chrono::nanoseconds>(heardAt.time_since_epoch()).count(),
                       std::memory_order_relaxed);
    mMarkCount.store(n + 1, std::memory_order_release);
}

bool SpectrumTap::Read(float* out, size_t count, Clock::time_point now, double* sampleRate) const
{
    const uint64_t marks = mMarkCount.load(std::memory_order_acquire);
    if (marks == 0 || count == 0 || count > kSize / 2) {
        return false;
    }
    const int64_t nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
    const double rate = mSampleRate.load(std::memory_order_relaxed);
    // The newest block already reaching the speakers.
    const uint64_t oldest = marks > kMarks ? marks - kMarks : 0;
    for (uint64_t m = marks; m-- > oldest;) {
        const Mark& mark = mMarks[m % kMarks];
        const int64_t heard = mark.heardNs.load(std::memory_order_relaxed);
        if (heard > nowNs) {
            continue;   // still queued
        }
        const double into = (nowNs - heard) * 1e-9;
        const uint32_t frames = mark.frames.load(std::memory_order_relaxed);
        if (into > 0.25 + frames / rate) {
            return false;   // the last block ended a while ago: stopped
        }
        const uint64_t end = mark.start.load(std::memory_order_relaxed)
                             + std::min<uint64_t>(frames, static_cast<uint64_t>(into * rate));
        if (end < count || mWritten.load(std::memory_order_acquire) < end) {
            return false;
        }
        for (size_t i = 0; i < count; ++i) {
            out[i] = mRing[(end - count + i) & (kSize - 1)];
        }
        if (sampleRate) {
            *sampleRate = rate;
        }
        return true;
    }
    return false;
}
}
