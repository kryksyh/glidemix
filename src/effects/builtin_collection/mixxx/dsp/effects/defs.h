// GlideMix: the parts of Mixxx's effects/defs.h its built-in effects use.
#pragma once

#include <QString>

enum class EffectEnableState {
    Disabled,
    Enabled,
    Disabling,
    Enabling
};

const QString kMixerProfile = QStringLiteral("[Mixer Profile]");
const QString kHighEqFrequency = QStringLiteral("HiEQFrequency");
const QString kLowEqFrequency = QStringLiteral("LoEQFrequency");

constexpr bool kEffectDebugOutput = false;
