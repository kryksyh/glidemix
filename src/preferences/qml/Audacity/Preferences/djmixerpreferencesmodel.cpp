/*
 * GlideMix
 */
#include "djmixerpreferencesmodel.h"

#include <algorithm>
#include <cmath>

using namespace au::appshell;

namespace {
// DlgPrefMixer's shelf range and x^4 slider mapping.
constexpr double kFrequencyUpperLimit = 20050;
constexpr double kFrequencyLowerLimit = 16;

double positionToFrequency(double position)
{
    const double p = std::clamp(position, 0.0, 1.0);
    return p * p * p * p * (kFrequencyUpperLimit - kFrequencyLowerLimit) + kFrequencyLowerLimit;
}

double frequencyToPosition(double frequency)
{
    const double f = std::clamp(frequency, kFrequencyLowerLimit, kFrequencyUpperLimit);
    return std::pow((f - kFrequencyLowerLimit) / (kFrequencyUpperLimit - kFrequencyLowerLimit), 1.0 / 4.0);
}

QString frequencyText(double frequency)
{
    // DlgPrefMixer::slotHiEqSliderChanged
    return frequency < 1000 ? QStringLiteral("%1 Hz").arg(std::round(frequency))
           : QStringLiteral("%1 kHz").arg(std::round(frequency) / 1000.);
}
}

DjMixerPreferencesModel::DjMixerPreferencesModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

void DjMixerPreferencesModel::load()
{
    effectsConfiguration()->djEqFrequenciesChanged().onNotify(this, [this]() { emit eqChanged(); });
    emit eqChanged();
}

double DjMixerPreferencesModel::lowEqPosition() const
{
    return frequencyToPosition(effectsConfiguration()->djLowEqFrequency());
}

double DjMixerPreferencesModel::highEqPosition() const
{
    return frequencyToPosition(effectsConfiguration()->djHighEqFrequency());
}

QString DjMixerPreferencesModel::lowEqText() const
{
    return frequencyText(effectsConfiguration()->djLowEqFrequency());
}

QString DjMixerPreferencesModel::highEqText() const
{
    return frequencyText(effectsConfiguration()->djHighEqFrequency());
}

void DjMixerPreferencesModel::setLowEqPosition(double position)
{
    // The low shelf stays below the high one (slotLoEqSliderChanged).
    const double high = effectsConfiguration()->djHighEqFrequency();
    const double low = std::min(positionToFrequency(position), high - 1.0);
    effectsConfiguration()->setDjEqFrequencies(std::max(kFrequencyLowerLimit, low), high);
}

void DjMixerPreferencesModel::setHighEqPosition(double position)
{
    const double low = effectsConfiguration()->djLowEqFrequency();
    const double high = std::max(positionToFrequency(position), low + 1.0);
    effectsConfiguration()->setDjEqFrequencies(low, std::min(kFrequencyUpperLimit, high));
}
