/*
 * GlideMix
 */
#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include "async/asyncable.h"
#include "modularity/ioc.h"

#include "mixhouse/imixhouselibraryconfiguration.h"
#include "mixhouse/imixhousestems.h"

namespace au::preferences {
/// Preferences > Stems: where Demucs runs, which model, and the cache of
/// separated songs. After DJ.Studio's own Stem Separation settings, minus its
/// memory limit (stems play from disk here like any clip).
class StemsPreferencesModel : public QObject, public muse::Contextable, public muse::async::Asyncable
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool available READ available NOTIFY installChanged)
    Q_PROPERTY(bool installing READ installing NOTIFY installChanged)
    Q_PROPERTY(bool engineInHelpers READ engineInHelpers NOTIFY installChanged)
    Q_PROPERTY(QString installStatus READ installStatus NOTIFY installChanged)
    Q_PROPERTY(int device READ device WRITE setDevice NOTIFY changed)
    Q_PROPERTY(bool highQuality READ highQuality WRITE setHighQuality NOTIFY changed)
    Q_PROPERTY(int cacheLimitGb READ cacheLimitGb WRITE setCacheLimitGb NOTIFY changed)
    Q_PROPERTY(QString cacheUsed READ cacheUsed NOTIFY cacheChanged)

    muse::GlobalInject<mixhouse::IMixHouseLibraryConfiguration> configuration;
    muse::ContextInject<mixhouse::IMixHouseStems> stems { this };

public:
    explicit StemsPreferencesModel(QObject* parent = nullptr);

    bool available() const;
    int device() const;
    void setDevice(int device);
    bool highQuality() const;
    void setHighQuality(bool high);
    int cacheLimitGb() const;
    void setCacheLimitGb(int gb);
    QString cacheUsed() const;

    Q_INVOKABLE void clearCache();
    Q_INVOKABLE void installEngine();
    Q_INVOKABLE void cancelInstall();
    Q_INVOKABLE void uninstallEngine();

    bool installing() const;
    bool engineInHelpers() const;
    QString installStatus() const { return m_installStatus; }

signals:
    void changed();
    void cacheChanged();
    void installChanged();

private:
    void subscribe();
    QString m_installStatus;
    bool m_subscribed = false;
};
}
