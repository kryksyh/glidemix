/*
 * GlideMix
 */
#pragma once

#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include "async/asyncable.h"
#include "modularity/ioc.h"

#include "effects/effects_base/ieffectsconfiguration.h"
#include "effects/effects_base/ieffectsprovider.h"

namespace au::appshell {
//! Preferences > Effects > Visible Effects: which DJ Effects the Add effect
//! menu lists, in what order (Mixxx's DlgPrefEffects visible/hidden tables).
//! Every change is written straight to the settings; the Preferences
//! dialog's transaction makes it Apply / Cancel-able.
class DjEffectsPreferencesModel : public QObject, public muse::Contextable, public muse::async::Asyncable
{
    Q_OBJECT
    QML_ELEMENT

    //! [{ id, title }]
    Q_PROPERTY(QVariantList visibleEffects READ visibleEffects NOTIFY listsChanged)
    Q_PROPERTY(QVariantList hiddenEffects READ hiddenEffects NOTIFY listsChanged)
    //! Effect load behavior: keep the meta knob position when an effect is replaced.
    Q_PROPERTY(bool keepMetaknobPosition READ keepMetaknobPosition WRITE setKeepMetaknobPosition NOTIFY keepMetaknobPositionChanged)

    muse::GlobalInject<effects::IEffectsConfiguration> effectsConfiguration;
    muse::GlobalInject<effects::IEffectsProvider> effectsProvider;

public:
    explicit DjEffectsPreferencesModel(QObject* parent = nullptr);

    Q_INVOKABLE void load();

    QVariantList visibleEffects() const;
    QVariantList hiddenEffects() const;
    bool keepMetaknobPosition() const;
    void setKeepMetaknobPosition(bool keep);

    //! Moves an effect within or between the lists (`toHidden` picks the
    //! destination list; `toIndex` -1 appends).
    Q_INVOKABLE void moveEffect(const QString& effectId, bool toHidden, int toIndex);
    //! { name, version, type, author, description }
    Q_INVOKABLE QVariantMap effectInfo(const QString& effectId) const;

signals:
    void listsChanged();
    void keepMetaknobPositionChanged();

private:
    void reload();
    void save();

    effects::EffectMetaList m_visible;
    effects::EffectMetaList m_hidden;
};
}
