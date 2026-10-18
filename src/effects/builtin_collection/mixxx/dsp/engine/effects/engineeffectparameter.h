// GlideMix: Mixxx's EngineEffectParameter -- the value the DSP reads.
#pragma once

#include <memory>

#include "effects/backends/effectmanifestparameter.h"

class EngineEffectParameter {
public:
    explicit EngineEffectParameter(EffectManifestParameterPointer manifest)
        : m_pParameterManifest(std::move(manifest)), m_value(m_pParameterManifest->getDefault()) {}

    double value() const { return m_value; }
    void setValue(double value)
    {
        if (value >= m_pParameterManifest->getMinimum() && value <= m_pParameterManifest->getMaximum()) {
            m_value = value;
        }
    }
    int toInt() const { return static_cast<int>(m_value); }
    bool toBool() const { return m_value > 0.0; }

private:
    EffectManifestParameterPointer m_pParameterManifest;
    double m_value;
};

using EngineEffectParameterPointer = std::shared_ptr<EngineEffectParameter>;
