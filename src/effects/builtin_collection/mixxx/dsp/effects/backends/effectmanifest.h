// GlideMix: Mixxx's EffectManifest -- an effect's identity and its
// parameter list.
#pragma once

#include <QList>
#include <QSharedPointer>
#include <QString>

#include "effects/backends/effectmanifestparameter.h"

enum class EffectBackendType { BuiltIn, AudioUnit, LV2, Unknown };

class EffectManifest {
public:
    const QString& id() const { return m_id; }
    void setId(const QString& id) { m_id = id; }
    const QString& name() const { return m_name; }
    void setName(const QString& name) { m_name = name; }
    const QString& shortName() const { return m_shortName; }
    void setShortName(const QString& name) { m_shortName = name; }
    const QString& author() const { return m_author; }
    void setAuthor(const QString& author) { m_author = author; }
    const QString& version() const { return m_version; }
    void setVersion(const QString& version) { m_version = version; }
    const QString& description() const { return m_description; }
    void setDescription(const QString& description) { m_description = description; }

    bool addDryToWet() const { return m_addDryToWet; }
    void setAddDryToWet(bool add) { m_addDryToWet = add; }
    bool effectRampsFromDry() const { return m_effectRampsFromDry; }
    void setEffectRampsFromDry(bool ramps) { m_effectRampsFromDry = ramps; }
    void setBackendType(EffectBackendType) {}
    double metaknobDefault() const { return m_metaknobDefault; }
    void setMetaknobDefault(double value) { m_metaknobDefault = value; }
    void setIsMixingEQ(bool) {}
    void setIsMainEQ(bool) {}

    EffectManifestParameterPointer addParameter()
    {
        m_parameters.append(EffectManifestParameterPointer::create());
        return m_parameters.last();
    }
    const QList<EffectManifestParameterPointer>& parameters() const { return m_parameters; }

private:
    QString m_id, m_name, m_shortName, m_author, m_version, m_description;
    bool m_addDryToWet = false;
    bool m_effectRampsFromDry = false;
    double m_metaknobDefault = 0.0;
    QList<EffectManifestParameterPointer> m_parameters;
};

using EffectManifestPointer = QSharedPointer<EffectManifest>;
