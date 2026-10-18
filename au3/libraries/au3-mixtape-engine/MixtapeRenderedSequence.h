/*
 * GlideMix: the whole mix as the playback engine renders it, for export.
 *
 * Export used Audacity's own mixer over the tracks, so a file differed
 * from what was heard: no master limiter (1.2% of a 40-minute mix's
 * samples clipped), no plugin automation. This sequence hands Audacity's
 * export the playback renderer's output instead, block by block.
 */
#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include "au3-mixer/WideSampleSequence.h"

namespace mixtape {
class Renderer;

class RenderedSequence final : public WideSampleSequence
{
public:
    //! `renderer` is seeked to `startTime` and renders at `rate`.
    //! `keepAlive` holds what the renderer uses (its effects host).
    RenderedSequence(std::unique_ptr<Renderer> renderer, std::shared_ptr<void> keepAlive, double startTime,
                     double outputSeconds, double rate);
    ~RenderedSequence() override;

    size_t NChannels() const override;
    float GetChannelVolume(int) const override { return 1.0f; }
    bool DoGet(size_t iChannel, size_t nBuffers, const samplePtr buffers[], sampleFormat format, sampleCount start,
               size_t len, bool backward, fillFormat fill, bool mayThrow, sampleCount* pNumWithinClips) const override;
    double GetStartTime() const override { return mStartTime; }
    double GetEndTime() const override { return mStartTime + mOutputSeconds; }
    double GetRate() const override { return mRate; }
    sampleFormat WidestEffectiveFormat() const override { return floatSample; }
    bool HasTrivialEnvelope() const override { return true; }
    void GetEnvelopeValues(double* buffer, size_t bufferLen, double, bool) const override;
    //! Left (with a right): the mixer then keeps the channels apart. Mono
    //! had it add both channels into both outputs, 6 dB too loud.
    AudioGraph::ChannelType GetChannelType() const override
    {
        return mBuffer.size() == 1 ? AudioGraph::MonoChannel : AudioGraph::LeftChannel;
    }

    //! Seconds of output the renderer gives for timeline t0..t1 with the
    //! tempo line as it is now.
    static double OutputSeconds(double t0, double t1);

private:
    void RenderUpTo(int64_t frame) const;

    mutable std::mutex mMutex;
    std::unique_ptr<Renderer> mRenderer;
    std::shared_ptr<void> mKeepAlive;
    const double mStartTime;
    const double mOutputSeconds;
    const double mRate;
    // Rendered frames not yet handed out, per channel, starting at mBufferStart.
    mutable std::vector<std::vector<float> > mBuffer;
    mutable int64_t mBufferStart = 0;
    mutable int64_t mRendered = 0;   // frames rendered so far (output time)
    mutable int64_t mToSkip = -1;    // latency compensation still to drop
};
}
