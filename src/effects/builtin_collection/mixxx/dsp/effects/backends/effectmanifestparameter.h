// GlideMix: Mixxx's EffectManifestParameter, keeping what describes a
// parameter (id, names, range, scale, units) and accepting the chain/link
// settings GlideMix has no use for.
#pragma once

#include <QList>
#include <QPair>
#include <QSharedPointer>
#include <QString>

class EffectManifestParameter {
public:
    enum class ValueScaler { Unknown, Linear, LinearInverse, Logarithmic, LogarithmicInverse, Integral, Toggle };
    enum class UnitsHint { Unknown, Time, Hertz, SampleRate, Beats, Millisecond, Semitones, Coefficient, Decibel };
    enum class LinkType { None, Linked, LinkedLeft, LinkedRight, LinkedLeftRight };
    enum class LinkInversion { NotInverted, Inverted };

    const QString& id() const { return m_id; }
    void setId(const QString& id) { m_id = id; }
    const QString& name() const { return m_name; }
    void setName(const QString& name) { m_name = name; }
    const QString& shortName() const { return m_shortName; }
    void setShortName(const QString& name) { m_shortName = name; }
    const QString& description() const { return m_description; }
    void setDescription(const QString& description) { m_description = description; }

    ValueScaler valueScaler() const { return m_valueScaler; }
    void setValueScaler(ValueScaler scaler) { m_valueScaler = scaler; }
    UnitsHint unitsHint() const { return m_unitsHint; }
    void setUnitsHint(UnitsHint hint) { m_unitsHint = hint; }

    LinkType defaultLinkType() const { return m_linkType; }
    void setDefaultLinkType(LinkType type) { m_linkType = type; }
    LinkInversion defaultLinkInversion() const { return m_linkInversion; }
    void setDefaultLinkInversion(LinkInversion inversion) { m_linkInversion = inversion; }
    double neutralPointOnScale() const { return m_neutralPointOnScale; }
    void setNeutralPointOnScale(double point) { m_neutralPointOnScale = point; }

    //! Named positions of a stepped parameter (Distortion's Mode).
    void appendStep(const QPair<QString, double>& step) { m_steps.append(step); }
    const QList<QPair<QString, double> >& getSteps() const { return m_steps; }

    double getMinimum() const { return m_minimum; }
    double getDefault() const { return m_default; }
    double getMaximum() const { return m_maximum; }
    void setRange(double minimum, double defaultValue, double maximum)
    {
        m_minimum = minimum;
        m_default = defaultValue;
        m_maximum = maximum;
    }

private:
    QString m_id, m_name, m_shortName, m_description;
    ValueScaler m_valueScaler = ValueScaler::Linear;
    UnitsHint m_unitsHint = UnitsHint::Unknown;
    double m_minimum = 0.0, m_default = 0.0, m_maximum = 1.0;
    QList<QPair<QString, double> > m_steps;
    LinkType m_linkType = LinkType::None;
    LinkInversion m_linkInversion = LinkInversion::NotInverted;
    double m_neutralPointOnScale = 0.0;
};

using EffectManifestParameterPointer = QSharedPointer<EffectManifestParameter>;
