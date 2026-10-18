// GlideMix: Mixxx's engine/engine.h. Effects always run on an
// interleaved stereo buffer, as in Mixxx's engine.
#pragma once

#include "audio/types.h"
#include "util/assert.h"
#include "util/types.h"

namespace mixxx {
static constexpr audio::ChannelCount kEngineChannelOutputCount = audio::ChannelCount::stereo();

class EngineParameters final {
public:
    EngineParameters(audio::SampleRate sampleRate, SINT framesPerBuffer)
        : m_sampleRate(sampleRate), m_framesPerBuffer(framesPerBuffer) {}

    SINT framesPerBuffer() const { return m_framesPerBuffer; }
    SINT samplesPerBuffer() const { return m_framesPerBuffer * kEngineChannelOutputCount; }
    audio::ChannelCount channelCount() const { return kEngineChannelOutputCount; }
    audio::SampleRate sampleRate() const { return m_sampleRate; }

private:
    audio::SampleRate m_sampleRate;
    SINT m_framesPerBuffer;
};
}
