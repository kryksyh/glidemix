/*
 * GlideMix
 */
#include "mixxxeffectviewmodel.h"

#include <cmath>

#include "mixxxeffect.h"

using namespace au::effects;

namespace {
QString unitText(mixxxeffects::Units units)
{
    switch (units) {
    case mixxxeffects::Units::Hertz: return QStringLiteral("Hz");
    case mixxxeffects::Units::Beats: return QStringLiteral("beats");
    case mixxxeffects::Units::Millisecond: return QStringLiteral("ms");
    case mixxxeffects::Units::Semitones: return QStringLiteral("st");
    case mixxxeffects::Units::Decibel: return QStringLiteral("dB");
    case mixxxeffects::Units::Time: return QStringLiteral("s");
    default: return QString();
    }
}

QString valueText(const mixxxeffects::ParameterInfo& p, double value)
{
    if (p.scale == mixxxeffects::Scale::Toggle) {
        return value > 0.0 ? QStringLiteral("On") : QStringLiteral("Off");
    }
    const double magnitude = std::fabs(value);
    const int decimals = p.scale == mixxxeffects::Scale::Integral ? 0 : (magnitude >= 100 ? 0 : magnitude >= 10 ? 1 : 2);
    const QString unit = unitText(p.units);
    const QString number = QString::number(value, 'f', decimals);
    return unit.isEmpty() ? number : number + QStringLiteral(" ") + unit;
}
}

MixxxEffectViewModel::MixxxEffectViewModel(QObject* parent, int instanceId)
    : BuiltinEffectModel(parent, instanceId)
{
}

QString MixxxEffectViewModel::title() const
{
    return m_title;
}

int MixxxEffectViewModel::parameterCount() const
{
    return static_cast<int>(m_parameters.size());
}

QVariantMap MixxxEffectViewModel::parameter(int row) const
{
    return row >= 0 && row < m_parameters.size() ? m_parameters.at(row).toMap() : QVariantMap();
}

void MixxxEffectViewModel::doReload()
{
    const auto& effect = this->effect<MixxxEffectBase>();
    const auto& info = effect.Info();
    const auto& settings = this->settings<MixxxEffectSettings>();

    const QString title = QString::fromStdString(info.name);
    const int oldCount = parameterCount();
    m_parameters.clear();
    // The meta knob first (index -1), as Mixxx's effect slot shows it.
    {
        QVariantMap meta;
        meta["index"] = kMetaIndex;
        meta["title"] = QStringLiteral("Meta");
        meta["description"] = QStringLiteral("Moves the linked parameters together");
        meta["isToggle"] = false;
        meta["isMeta"] = true;
        meta["normalized"] = settings.meta;
        meta["defaultNormalized"] = info.metaknobDefault;
        meta["valueText"] = QString::number(settings.meta, 'f', 2);
        m_parameters.push_back(meta);
    }
    for (size_t i = 0; i < info.parameters.size() && i < MixxxEffectSettings::kMaxParameters; ++i) {
        const auto& p = info.parameters[i];
        const double value = settings.values[i];
        QVariantMap item;
        item["index"] = static_cast<int>(i);
        item["title"] = QString::fromStdString(p.name);
        item["description"] = QString::fromStdString(p.description);
        item["isToggle"] = p.scale == mixxxeffects::Scale::Toggle;
        item["normalized"] = p.toNormalized(value);
        item["defaultNormalized"] = p.toNormalized(p.defaultValue);
        item["valueText"] = valueText(p, value);
        item["isMeta"] = false;
        item["isLinked"] = p.link != mixxxeffects::Link::None;
        m_parameters.push_back(item);
    }
    if (title != m_title) {
        m_title = title;
        emit titleChanged();
    }
    if (parameterCount() != oldCount) {
        emit parameterCountChanged();
    }
    emit valuesChanged();
}

void MixxxEffectViewModel::setValue(int index, double value)
{
    modifySettings<MixxxEffectSettings>([index, value](EffectSettings& settings) {
        settings.cast<MixxxEffectSettings>()->values[static_cast<size_t>(index)] = value;
    });
    doReload();
}

void MixxxEffectViewModel::setNormalized(int index, double normalized)
{
    // The automation lane follows the knob you turn (Ableton's "click a
    // device control to show its envelope"); kMetaIndex (-1) reports 0.
    if (const auto i = instance()) {
        i->NotifyParameterInteraction(static_cast<uint32_t>(index + 1));
    }
    if (index == kMetaIndex) {
        const auto& effect = this->effect<MixxxEffectBase>();
        modifySettings<MixxxEffectSettings>([&effect, normalized](EffectSettings& settings) {
            auto& s = *settings.cast<MixxxEffectSettings>();
            effect.MoveMeta(s, s.meta, normalized);
        });
        doReload();
        return;
    }
    const auto& info = effect<MixxxEffectBase>().Info();
    if (index < 0 || static_cast<size_t>(index) >= info.parameters.size()) {
        return;
    }
    setValue(index, info.parameters[static_cast<size_t>(index)].fromNormalized(normalized));
}

void MixxxEffectViewModel::resetToDefault(int index)
{
    if (index == kMetaIndex) {
        setNormalized(kMetaIndex, effect<MixxxEffectBase>().Info().metaknobDefault);
        return;
    }
    const auto& info = effect<MixxxEffectBase>().Info();
    if (index < 0 || static_cast<size_t>(index) >= info.parameters.size()) {
        return;
    }
    setValue(index, info.parameters[static_cast<size_t>(index)].defaultValue);
}
