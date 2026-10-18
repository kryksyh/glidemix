/*
* Audacity: A Digital Audio Editor
*/
#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>

#include "framework/global/modularity/ioc.h"
#include "iappshellconfiguration.h"
#include "framework/global/iglobalconfiguration.h"
#include "framework/global/iapplication.h"
#include "framework/global/io/ifilesystem.h"

namespace au::appshell {
class AboutModel : public QObject, public muse::Contextable
{
    Q_OBJECT
    QML_ELEMENT

    muse::GlobalInject<IAppShellConfiguration> configuration;
    muse::GlobalInject<muse::IGlobalConfiguration> globalConfiguration;
    muse::GlobalInject<muse::IApplication> application;
    muse::GlobalInject<muse::io::IFileSystem> fileSystem;

public:
    explicit AboutModel(QObject* parent = nullptr);

    Q_INVOKABLE QString appName() const;
    Q_INVOKABLE QString appVersion() const;
    Q_INVOKABLE QString appRevision() const;
    Q_INVOKABLE QVariantList creditList() const;

    /// GlideMix's own license first, then every bundled component, in
    /// the order of resources/about/components.json.
    Q_INVOKABLE QVariantList legalList() const;
    /// The text of one entry of a legalList() item's "files"; "gpl-30.html"
    /// is rich text, everything else is a verbatim plain-text license file.
    Q_INVOKABLE QString licenseText(const QString& file) const;

    Q_INVOKABLE void copyRevisionToClipboard() const;

    Q_INVOKABLE void toggleDevMode();

private:
    QString readResource(const QString& path) const;
};
}
