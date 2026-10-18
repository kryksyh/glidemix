/*
 * GlideMix -- DJ mixer beat effects, in the Mixxx effect framework so they
 * run, automate and beat-sync like the Mixxx built-ins:
 *
 *   Ping Pong  an echo bouncing between left and right, in beats
 *   Roll       a slice of the beat looped while Roll is on (1/16 .. 1 beat)
 *   Trans      a beat gate: the sound cut on and off in time
 *   Dub Echo   an echo whose repeats get darker and dirtier as they fade
 *   Sweep      a band-pass filter swept up and down in time with the beat
 *   Tape Stop  the track slows to a stop; drawn fast it is a vinyl brake
 *
 * All are our own code (GPL like the rest of the app).
 */
#pragma once

#include <QMap>
#include <vector>

#include "effects/backends/effectprocessor.h"
#include "engine/effects/engineeffect.h"
#include "engine/effects/engineeffectparameter.h"
#include "engine/engine.h"
#include "util/class.h"
#include "util/defs.h"

//! Stereo delay line in frames.
class GlideMixDelayState : public EffectState {
  public:
    explicit GlideMixDelayState(const mixxx::EngineParameters& p);
    std::vector<float> left, right;
    size_t write = 0;
    double sampleRate = 44100.0;
    // Filters and state used by some of the effects.
    double lp[2] = { 0, 0 };
    double hp[2] = { 0, 0 };
    double hpIn[2] = { 0, 0 };
    double bp1[2] = { 0, 0 };
    double bp2[2] = { 0, 0 };
    double gate = 1.0;
    // Roll / Tape Stop: where the loop or the slowed read is.
    bool active = false;
    size_t loopStart = 0;
    size_t loopLength = 0;
    size_t loopPos = 0;
    size_t recorded = 0;
    double readPos = 0.0;
    double speed = 1.0;
};

class GlideMixPingPongEffect : public EffectProcessorImpl<GlideMixDelayState> {
  public:
    GlideMixPingPongEffect() = default;
    static QString getId();
    static EffectManifestPointer getManifest();
    void loadEngineEffectParameters(const QMap<QString, EngineEffectParameterPointer>& parameters) override;
    void processChannel(GlideMixDelayState* s, const CSAMPLE* in, CSAMPLE* out, const mixxx::EngineParameters& p,
                        const EffectEnableState enableState, const GroupFeatureState& gf) override;

  private:
    EngineEffectParameterPointer m_time, m_feedback, m_send;
    DISALLOW_COPY_AND_ASSIGN(GlideMixPingPongEffect);
};

class GlideMixRollEffect : public EffectProcessorImpl<GlideMixDelayState> {
  public:
    GlideMixRollEffect() = default;
    static QString getId();
    static EffectManifestPointer getManifest();
    void loadEngineEffectParameters(const QMap<QString, EngineEffectParameterPointer>& parameters) override;
    void processChannel(GlideMixDelayState* s, const CSAMPLE* in, CSAMPLE* out, const mixxx::EngineParameters& p,
                        const EffectEnableState enableState, const GroupFeatureState& gf) override;

  private:
    EngineEffectParameterPointer m_length, m_roll;
    DISALLOW_COPY_AND_ASSIGN(GlideMixRollEffect);
};

class GlideMixTransEffect : public EffectProcessorImpl<GlideMixDelayState> {
  public:
    GlideMixTransEffect() = default;
    static QString getId();
    static EffectManifestPointer getManifest();
    void loadEngineEffectParameters(const QMap<QString, EngineEffectParameterPointer>& parameters) override;
    void processChannel(GlideMixDelayState* s, const CSAMPLE* in, CSAMPLE* out, const mixxx::EngineParameters& p,
                        const EffectEnableState enableState, const GroupFeatureState& gf) override;

  private:
    EngineEffectParameterPointer m_rate, m_depth;
    DISALLOW_COPY_AND_ASSIGN(GlideMixTransEffect);
};

class GlideMixDubEchoEffect : public EffectProcessorImpl<GlideMixDelayState> {
  public:
    GlideMixDubEchoEffect() = default;
    static QString getId();
    static EffectManifestPointer getManifest();
    void loadEngineEffectParameters(const QMap<QString, EngineEffectParameterPointer>& parameters) override;
    void processChannel(GlideMixDelayState* s, const CSAMPLE* in, CSAMPLE* out, const mixxx::EngineParameters& p,
                        const EffectEnableState enableState, const GroupFeatureState& gf) override;

  private:
    EngineEffectParameterPointer m_time, m_feedback, m_send;
    DISALLOW_COPY_AND_ASSIGN(GlideMixDubEchoEffect);
};

class GlideMixSweepEffect : public EffectProcessorImpl<GlideMixDelayState> {
  public:
    GlideMixSweepEffect() = default;
    static QString getId();
    static EffectManifestPointer getManifest();
    void loadEngineEffectParameters(const QMap<QString, EngineEffectParameterPointer>& parameters) override;
    void processChannel(GlideMixDelayState* s, const CSAMPLE* in, CSAMPLE* out, const mixxx::EngineParameters& p,
                        const EffectEnableState enableState, const GroupFeatureState& gf) override;

  private:
    EngineEffectParameterPointer m_rate, m_depth;
    DISALLOW_COPY_AND_ASSIGN(GlideMixSweepEffect);
};

class GlideMixTapeStopEffect : public EffectProcessorImpl<GlideMixDelayState> {
  public:
    GlideMixTapeStopEffect() = default;
    static QString getId();
    static EffectManifestPointer getManifest();
    void loadEngineEffectParameters(const QMap<QString, EngineEffectParameterPointer>& parameters) override;
    void processChannel(GlideMixDelayState* s, const CSAMPLE* in, CSAMPLE* out, const mixxx::EngineParameters& p,
                        const EffectEnableState enableState, const GroupFeatureState& gf) override;

  private:
    EngineEffectParameterPointer m_speed;
    DISALLOW_COPY_AND_ASSIGN(GlideMixTapeStopEffect);
};
