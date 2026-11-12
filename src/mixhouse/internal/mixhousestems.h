/*
 * GlideMix
 */
#pragma once

#include <memory>
#include <optional>
#include <vector>

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
class WaveClip;
class WaveTrack;
namespace au::au3 {
class ProgressDialog;
}

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
    bool removeStemFromRange(const trackedit::ClipKey& clip, double t0, double t1, int stem) override;
    bool splitRangeIntoStems(const trackedit::ClipKey& clip, double t0, double t1) override;
    QStringList saveClipStems(const trackedit::ClipKey& clip, double t0, double t1, const QList<int>& stems,
                              const QString& dir, const QString& namePattern, bool keepExisting) override;
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
    //! The clip, its four stems over its whole source, and [t0, t1] in its
    //! frames: `first` from the start of the source, `offset` from its play
    //! start (what GetSamples/SetSamples take). False = failed (said) or cancelled.
    struct RangeStems {
        WaveTrack* track = nullptr;
        std::shared_ptr<WaveClip> clip;
        std::vector<std::vector<std::vector<float> > > stems;
        long long first = 0;
        long long offset = 0;
        long long count = 0;
        double start = 0.0;
        double end = 0.0;
    };
    bool stemsOfRange(const trackedit::ClipKey& key, double t0, double t1, RangeStems& out);
    //! One clip's four stems (Demucs, or the cache): `out[stem][channel]`
    //! over the clip's whole source. False when it failed (already said)
    //! or was cancelled.
    bool separateClip(const WaveClip& clip, const Launch& launch, au::au3::ProgressDialog& progress, size_t index,
                      size_t count, std::vector<std::vector<std::vector<float> > >& out);

    QProcess* m_installer = nullptr;
    muse::async::Channel<QString> m_installProgress;
    muse::async::Channel<bool> m_installFinished;
};
}
