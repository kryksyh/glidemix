// GlideMix: Mixxx's EffectState / EffectProcessorImpl, for one effect
// instance processing one interleaved stereo stream (Mixxx keeps a state per
// input/output channel pair; a GlideMix track is one such pair).
#pragma once

#include <memory>

#include <QMap>
#include <QVarLengthArray>
#include <QObject>
#include <QString>

#include "effects/backends/effectmanifest.h"
#include "effects/defs.h"
#include "engine/effects/engineeffectparameter.h"
#include "engine/effects/groupfeaturestate.h"
#include "engine/engine.h"
#include "util/types.h"

class EffectState {
public:
    explicit EffectState(const mixxx::EngineParameters&) {}
    virtual ~EffectState() = default;
};

class EffectProcessor {
public:
    virtual ~EffectProcessor() = default;
    virtual void loadEngineEffectParameters(const QMap<QString, EngineEffectParameterPointer>& parameters) = 0;
    //! `maxFramesPerBuffer` sizes the state's work buffers; each process()
    //! call passes at most that many frames.
    virtual void process(const CSAMPLE* pInput, CSAMPLE* pOutput, const mixxx::EngineParameters& engineParameters,
                         SINT maxFramesPerBuffer, EffectEnableState enableState, const GroupFeatureState& groupFeatures) = 0;
    virtual void reset() = 0;
    virtual SINT getGroupDelayFrames() { return 0; }
};

template<typename EffectSpecificState>
class EffectProcessorImpl : public EffectProcessor {
public:
    virtual void processChannel(EffectSpecificState* channelState, const CSAMPLE* pInput, CSAMPLE* pOutput,
                                const mixxx::EngineParameters& engineParameters, const EffectEnableState enableState,
                                const GroupFeatureState& groupFeatures) = 0;

    void process(const CSAMPLE* pInput, CSAMPLE* pOutput, const mixxx::EngineParameters& engineParameters,
                 SINT maxFramesPerBuffer, EffectEnableState enableState, const GroupFeatureState& groupFeatures) override
    {
        if (!m_state || m_stateSampleRate != engineParameters.sampleRate() || m_stateFrames < maxFramesPerBuffer) {
            m_state = std::make_unique<EffectSpecificState>(
                mixxx::EngineParameters(engineParameters.sampleRate(), maxFramesPerBuffer));
            m_stateSampleRate = engineParameters.sampleRate();
            m_stateFrames = maxFramesPerBuffer;
        }
        processChannel(m_state.get(), pInput, pOutput, engineParameters, enableState, groupFeatures);
    }

    void reset() override { m_state.reset(); }

private:
    std::unique_ptr<EffectSpecificState> m_state;
    double m_stateSampleRate = 0.0;
    SINT m_stateFrames = 0;
};
