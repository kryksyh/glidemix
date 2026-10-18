/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <algorithm>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <vector>

//! Carries source timestamps through a latency-compensated effect. Prepare on
//! the control thread; Process performs no allocation. Unlike subtracting a
//! duration, retaining exact timestamps works across loop boundaries and warps.
class ParameterAutomationTimeQueue final
{
public:
    void Prepare(size_t maxFrames, size_t latency)
    {
        if (latency > std::numeric_limits<size_t>::max() - maxFrames) {
            throw std::length_error("Automation timing capacity overflow");
        }
        mPending.resize(maxFrames + latency);
        mOutput.resize(maxFrames);
        Reset();
    }

    void Reset() noexcept { mHead = mSize = 0; }

    //! Whether Process can take this block without throwing. The audio thread
    //! checks first: an effect added or replaced during playback has no
    //! prepared queue until playback restarts.
    bool CanProcess(const double* input, size_t frames, size_t discarded) const noexcept
    {
        return discarded <= frames && frames <= mOutput.size()
               && frames <= mPending.size() - mSize && (!frames || input);
    }

    const double* Process(const double* input, size_t frames, size_t discarded)
    {
        if (!CanProcess(input, frames, discarded)) {
            throw std::runtime_error("Plugin latency changed: restart playback to rebuild automation timing");
        }
        for (size_t i = 0; i < frames; ++i) {
            mPending[(mHead + mSize + i) % mPending.size()] = input[i];
        }
        mSize += frames;
        const auto produced = frames - discarded;
        for (size_t i = 0; i < produced; ++i) {
            mOutput[i] = mPending[(mHead + i) % mPending.size()];
        }
        if (!mPending.empty()) { mHead = (mHead + produced) % mPending.size(); }
        mSize -= produced;
        return mOutput.data();
    }

private:
    std::vector<double> mPending, mOutput;
    size_t mHead{}, mSize{};
};
