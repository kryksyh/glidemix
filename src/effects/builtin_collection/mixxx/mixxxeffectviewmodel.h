/*
 * GlideMix
 */
#pragma once

#include <QVariantList>

#include "effects/builtin/view/builtineffectmodel.h"

namespace au::effects {
//! One generic view for every Mixxx effect: Mixxx effects have no UI of
//! their own, only a parameter list (Mixxx shows them as knobs).
class MixxxEffectViewModel : public BuiltinEffectModel
{
    Q_OBJECT
    Q_PROPERTY(QString title READ title NOTIFY titleChanged FINAL)
    // The knobs are built from the count, which only changes on reload of a
    // different effect; values change in place so a knob being dragged keeps
    // its mouse grab.
    Q_PROPERTY(int parameterCount READ parameterCount NOTIFY parameterCountChanged FINAL)

public:
    static constexpr int kMetaIndex = -1;

    MixxxEffectViewModel(QObject* parent, int instanceId);

    QString title() const;
    int parameterCount() const;
    //! The knob in row `row` (row 0 is the meta knob): {index, title,
    //! description, isToggle, isMeta, isLinked, normalized, defaultNormalized,
    //! valueText}; `index` is what setNormalized takes (-1 = meta knob).
    Q_INVOKABLE QVariantMap parameter(int row) const;

    //! Knob position 0..1 on Mixxx's knob curve for that parameter.
    Q_INVOKABLE void setNormalized(int index, double normalized);
    Q_INVOKABLE void resetToDefault(int index);

signals:
    void titleChanged();
    void parameterCountChanged();
    void valuesChanged();

private:
    void doReload() override;
    void setValue(int index, double value);

    QVariantList m_parameters;
    QString m_title;
};

class MixxxEffectViewModelFactory : public EffectViewModelFactory<MixxxEffectViewModel>
{
};
}
