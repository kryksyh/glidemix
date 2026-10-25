/*
 * GlideMix
 */
#pragma once

#include <optional>

#include <QString>
#include <QStringList>

#include "modularity/ioc.h"
#include "context/iglobalcontext.h"
#include "framework/interactive/iinteractive.h"
#include "trackedit/iprojecthistory.h"

#include "mixhouse/imixhouseactivity.h"
#include "mixhouse/imixhousestems.h"
#include "mixhouse/imixhouseclipcolors.h"
#include "mixhouse/imixhouselibraryconfiguration.h"

class QProcess;

namespace au::mixhouse {
class MixHouseStems : public IMixHouseStems, public muse::Contextable
{
    muse::ContextInject<context::IGlobalContext> globalContext { this };
    muse::ContextInject<muse::IInteractive> interactive { this };
    muse::ContextInject<trackedit::IProjectHistory> projectHistory { this };
    muse::ContextInject<IMixHouseClipColors> clipColors { this };
    muse::ContextInject<IMixHouseActivity> activity { this };
    muse::GlobalInject<IMixHouseLibraryConfiguration> configuration;

public:
    explicit MixHouseStems(const muse::modularity::ContextPtr& iocCtx);

    bool available() const override;
    void separateTrack(const trackedit::TrackId& track) override;
    qint64 cacheSize() const override;
    void clearCache() override;
    bool installing() const override;
    void installEngine() override;
    void cancelInstall() override;
    bool engineInHelpers() const override;
    void uninstallEngine() override;
    muse::async::Channel<QString> installProgress() const override;
    muse::async::Channel<bool> installFinished() const override;

    //! Where the in-app install goes.
    static QString helpersEngineDir();

    //! Where separated stems are kept: Application Support/Glidemix/Stems.
    static QString cacheRoot();

private:
    struct Launch {
        QString program;
        QStringList prefixArgs;   ///< before Demucs's own arguments
    };
    static std::optional<Launch> findDemucs();
    void trimCache(const QString& keep) const;

    QProcess* m_installer = nullptr;
    muse::async::Channel<QString> m_installProgress;
    muse::async::Channel<bool> m_installFinished;
};
}
