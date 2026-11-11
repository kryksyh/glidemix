/*
 * GlideMix -- playback engine
 *
 * A mono copy of the mix that goes to the speakers, for the LED meter above
 * the clock. The transport's render thread writes each block with the time
 * it will be heard; the UI reads the samples being heard now. Lock-free, one
 * writer (the audience output, never the headphone cue) and one reader; a
 * rare torn read only shows as a flicker on the meter.
 */
#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace mixtape {
class SpectrumTap
{
public:
    using Clock = std::chrono::steady_clock;

    static SpectrumTap& Instance();

    //! Render thread: one block of planar channels, heard from `heardAt` on.
    void Write(const float* const* planar, size_t channels, size_t frames, double sampleRate, Clock::time_point heardAt);

    //! UI thread: the `count` samples that end where the speakers are now.
    //! False when nothing is playing (no block heard in the last 0.25 s).
    bool Read(float* out, size_t count, Clock::time_point now, double* sampleRate) const;

    static constexpr size_t kSize = size_t(1) << 16;   // ~1.4 s at 48 kHz

private:
    struct Mark {
        std::atomic<uint64_t> start { 0 };   // first sample of the block
        std::atomic<uint32_t> frames { 0 };
        std::atomic<int64_t> heardNs { 0 };  // Clock time it is heard, ns since epoch
    };
    static constexpr size_t kMarks = 64;

    std::array<float, kSize> mRing {};
    std::atomic<uint64_t> mWritten { 0 };
    std::array<Mark, kMarks> mMarks;
    std::atomic<uint64_t> mMarkCount { 0 };
    std::atomic<double> mSampleRate { 44100.0 };
};
}
