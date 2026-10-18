/*
 * GlideMix
 */
#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include "async/asyncable.h"
#include "modularity/ioc.h"

#include "effects/effects_base/ieffectchainpresets.h"
#include "framework/interactive/iinteractive.h"

namespace au::appshell {
//! Preferences > Effects > Effect Chain Presets (Mixxx's DlgPrefEffects chain
//! preset list): order, Import, Export, Rename, Delete. The order is a
//! setting (Apply / Cancel); file operations happen right away, as in Mixxx.
class DjChainPresetsPreferencesModel : public QObject, public muse::Contextable, public muse::async::Asyncable
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QStringList presets READ presets NOTIFY presetsChanged)

    muse::ContextInject<effects::IEffectChainPresets> chainPresets { this };
    muse::ContextInject<muse::IInteractive> interactive { this };

public:
    explicit DjChainPresetsPreferencesModel(QObject* parent = nullptr);

    Q_INVOKABLE void load();

    QStringList presets() const;
    //! [{ title, available }]
    Q_INVOKABLE QVariantList effectsIn(const QString& name) const;

    Q_INVOKABLE void movePreset(const QString& name, int toIndex);
    //! Returns the name of the last imported preset, or "".
    Q_INVOKABLE QString importPresets();
    Q_INVOKABLE void exportPreset(const QString& name);
    //! Returns the new name, or "".
    Q_INVOKABLE QString renamePreset(const QString& name);
    Q_INVOKABLE bool deletePreset(const QString& name);

signals:
    void presetsChanged();

private:
    QStringList m_presets;
};
}
