/*
 * GlideMix
 */
#pragma once

#include <memory>

#include "au3-mixer/AudioIOSequences.h"
#include "au3-channel/Channel.h"

#include "playback/imetronome.h"

class AudacityProject;
class ProjectTimeSignature;

namespace au::playback {
//! The ChannelGroup the audio engine requires of every played sequence (it
//! hangs the realtime-effect state off it). The metronome has no clips and no
//! effects, so there is nothing in it.
class Au3MetronomeChannelGroup final : public ChannelGroup
{
public:
    void ShiftBy(double, double) override {}
    void MoveTo(double) override {}
    size_t NChannels() const override { return 1; }
    size_t NIntervals() const override { return 0; }

protected:
    std::shared_ptr<Channel> DoGetChannel(size_t) override { return {}; }
    std::shared_ptr<Interval> DoGetInterval(size_t) override { return {}; }
};

//! The metronome as one more sequence for the mixer: a mono stream, silent
//! except for a click on every beat of the project's time signature, with the
//! accent on beat 1 of each bar. Samples are computed from the time asked for,
//! so seeking, looping and scrubbing stay in step without any state.
//!
//! It is always part of playback; switching the metronome off makes it silent
//! at once, so the button works while the transport is running.
class Au3MetronomeSequence final : public PlayableSequence
{
public:
    Au3MetronomeSequence(AudacityProject& project, std::shared_ptr<IMetronome> metronome, double rate);

    // WideSampleSequence
    size_t NChannels() const override { return 1; }
    float GetChannelVolume(int) const override { return 1.0f; }
    bool DoGet(size_t iChannel, size_t nBuffers, const samplePtr buffers[], sampleFormat format, sampleCount start, size_t len,
               bool backwards, fillFormat fill, bool mayThrow, sampleCount* pNumWithinClips) const override;
    double GetStartTime() const override { return 0.0; }
    double GetEndTime() const override;
    double GetRate() const override { return m_rate; }
    sampleFormat WidestEffectiveFormat() const override { return floatSample; }
    bool HasTrivialEnvelope() const override { return true; }
    void GetEnvelopeValues(double* buffer, size_t bufferLen, double t0, bool backwards) const override;

    // AudioGraph::Channel
    AudioGraph::ChannelType GetChannelType() const override { return AudioGraph::MonoChannel; }

    // PlayableSequence
    const ChannelGroup* FindChannelGroup() const override { return &m_group; }
    bool GetSolo() const override { return false; }
    bool GetMute() const override { return false; }
    bool IgnoresSolo() const override { return true; }

private:
    const ProjectTimeSignature& m_timeSignature;
    std::shared_ptr<IMetronome> m_metronome;
    double m_rate = 0.0;
    Au3MetronomeChannelGroup m_group;
};
}
