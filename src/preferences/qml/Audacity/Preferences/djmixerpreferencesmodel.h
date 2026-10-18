/*
 * GlideMix
 */
#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include "async/asyncable.h"
#include "modularity/ioc.h"

#include "effects/effects_base/ieffectsconfiguration.h"

namespace au::appshell {
//! Preferences > Effects > DJ EQ crossover frequencies (Mixxx's DlgPrefMixer):
//! the DJ EQ effects' low and high crossovers, on Mixxx's slider curve.
class DjMixerPreferencesModel : public QObject, public muse::Contextable, public muse::async::Asyncable
{
    Q_OBJECT
    QML_ELEMENT

    //! Slider positions 0..1 (frequency = 16 + position^4 * (20050 - 16) Hz).
    Q_PROPERTY(double lowEqPosition READ lowEqPosition NOTIFY eqChanged)
    Q_PROPERTY(double highEqPosition READ highEqPosition NOTIFY eqChanged)
    Q_PROPERTY(QString lowEqText READ lowEqText NOTIFY eqChanged)
    Q_PROPERTY(QString highEqText READ highEqText NOTIFY eqChanged)

    muse::GlobalInject<effects::IEffectsConfiguration> effectsConfiguration;

public:
    explicit DjMixerPreferencesModel(QObject* parent = nullptr);

    Q_INVOKABLE void load();

    double lowEqPosition() const;
    double highEqPosition() const;
    QString lowEqText() const;
    QString highEqText() const;

    Q_INVOKABLE void setLowEqPosition(double position);
    Q_INVOKABLE void setHighEqPosition(double position);

signals:
    void eqChanged();
};
}
