/*
 * GlideMix: the whole mix as the playback engine renders it, for export.
 */
#include "MixtapeRenderedSequence.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "au3-mixer/PlaybackSpeedCurve.h"
#include "MixtapeRenderer.h"
#include "MixtapeTransport.h"

namespace mixtape {
RenderedSequence::RenderedSequence(std::unique_ptr<Renderer> renderer, std::shared_ptr<void> keepAlive,
                                   double startTime, double outputSeconds, double rate)
    : mRenderer(std::move(renderer))
    , mKeepAlive(std::move(keepAlive))
    , mStartTime(startTime)
    , mOutputSeconds(outputSeconds)
    , mRate(rate)
    , mBuffer(mRenderer ? mRenderer->OutputChannels() : 2)
{
    if (mRenderer) {
        mRenderer->Seek(startTime);
    }
}

RenderedSequence::~RenderedSequence()
{
    // The renderer before what it uses.
    mRenderer.reset();
    mKeepAlive.reset();
}

size_t RenderedSequence::NChannels() const
{
    return mBuffer.size();
}

double RenderedSequence::OutputSeconds(double t0, double t1)
{
    const auto curve = PlaybackSpeedCurve::Live();
    if (!curve || t1 <= t0) {
        return std::max(0.0, t1 - t0);
    }
    // Step through output time as the renderer does, a block at a time.
    const double step = static_cast<double>(kRenderBlockFrames) / 48000.0;
    double t = t0;
    double seconds = 0.0;
    while (t < t1) {
        const double next = curve->SolveIntegralOfInverse(t, step);
        if (!(next > t)) {
            return t1 - t0;   // a degenerate curve: fall back to the timeline
        }
        if (next >= t1) {
            seconds += step * (t1 - t) / (next - t);
            break;
        }
        seconds += step;
        t = next;
    }
    return seconds;
}

void RenderedSequence::RenderUpTo(int64_t frame) const
{
    const size_t channels = mBuffer.size();
    std::vector<std::vector<float> > block(channels, std::vector<float>(kRenderBlockFrames));
    std::vector<float*> ptrs(channels);
    for (size_t c = 0; c < channels; ++c) {
        ptrs[c] = block[c].data();
    }
    while (mBufferStart + static_cast<int64_t>(mBuffer[0].size()) < frame) {
        mRenderer->Render(ptrs.data(), kRenderBlockFrames);
        size_t from = 0;
        if (mToSkip < 0) {
            mToSkip = static_cast<int64_t>(mRenderer->OutputDelayFrames());
        }
        if (mToSkip > 0) {
            from = static_cast<size_t>(std::min<int64_t>(mToSkip, kRenderBlockFrames));
            mToSkip -= static_cast<int64_t>(from);
        }
        for (size_t c = 0; c < channels; ++c) {
            mBuffer[c].insert(mBuffer[c].end(), block[c].begin() + from, block[c].end());
        }
        mRendered += static_cast<int64_t>(kRenderBlockFrames - from);
    }
}

bool RenderedSequence::DoGet(size_t iChannel, size_t nBuffers, const samplePtr buffers[], sampleFormat format,
                             sampleCount start, size_t len, bool backward, fillFormat, bool, sampleCount* pNumWithinClips) const
{
    std::lock_guard<std::mutex> lock(mMutex);
    const int64_t first = start.as_long_long() - static_cast<int64_t>(std::llround(mStartTime * mRate));
    const int64_t total = static_cast<int64_t>(std::llround(mOutputSeconds * mRate));
    std::vector<float> tmp(len);
    if (mRenderer && !backward && len > 0) {
        RenderUpTo(std::min(first + static_cast<int64_t>(len), total));
    }
    for (size_t b = 0; b < nBuffers; ++b) {
        const size_t channel = iChannel + b;
        std::fill(tmp.begin(), tmp.end(), 0.0f);
        if (mRenderer && !backward && channel < mBuffer.size()) {
            const auto& data = mBuffer[channel];
            for (size_t i = 0; i < len; ++i) {
                const int64_t f = first + static_cast<int64_t>(i);
                if (f >= 0 && f < total && f >= mBufferStart && f - mBufferStart < static_cast<int64_t>(data.size())) {
                    tmp[i] = data[static_cast<size_t>(f - mBufferStart)];
                }
            }
        }
        CopySamples(reinterpret_cast<constSamplePtr>(tmp.data()), floatSample, buffers[b], format, len);
    }
    // Reads move forward: what lies before this request is no longer needed.
    if (!backward && first > mBufferStart) {
        const int64_t drop = std::min<int64_t>(first - mBufferStart, static_cast<int64_t>(mBuffer[0].size()));
        for (auto& channel : mBuffer) {
            channel.erase(channel.begin(), channel.begin() + drop);
        }
        mBufferStart += drop;
    }
    if (pNumWithinClips) {
        *pNumWithinClips = static_cast<long long>(len);
    }
    return true;
}

void RenderedSequence::GetEnvelopeValues(double* buffer, size_t bufferLen, double, bool) const
{
    std::fill(buffer, buffer + bufferLen, 1.0);
}
}
