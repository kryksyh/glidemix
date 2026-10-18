/*
 * GlideMix
 */
#pragma once

#include <atomic>
#include <memory>
#include <mutex>

#include <QHash>
#include <QStringList>

#include "../imixhouserekordboxphrases.h"
#include "../imixhouselibraryconfiguration.h"
#include "modularity/ioc.h"

namespace au::mixhouse {
class MixHouseRekordboxPhrases : public IMixHouseRekordboxPhrases
{
    muse::GlobalInject<IMixHouseLibraryConfiguration> libraryConfiguration;

public:
    MixHouseRekordboxPhrases();
    ~MixHouseRekordboxPhrases() override;

    void init();

    std::vector<MixHousePhrase> phrases(const QString& filePath, double bpmHint) const override;
    MixHouseRekordboxGrid beatGrid(const QString& filePath, double bpmHint) const override;
    double timingOffsetSec(const QString& filePath) const override;
    MixHouseWaveformOverview waveformOverview(const QString& filePath, double bpmHint) const override;
    MixHouseWaveformOverview waveformDetail(const QString& filePath, double bpmHint) const override;
    muse::async::Notification ready() const override;

    static QString analysisRoot();

private:
    void buildIndex();
    //! The analysis folder of the file: best tempo match among same-named ones.
    QString analysisFolder(const QString& filePath, double bpmHint) const;

    //! file name -> analysis folders (ANLZ0000.DAT/.EXT)
    std::shared_ptr<const QHash<QString, QStringList> > m_index;
    mutable std::mutex m_cacheMutex;
    mutable QHash<QString, std::vector<MixHousePhrase> > m_cache;
    mutable QHash<QString, double> m_offsetCache;   // guarded by m_cacheMutex
    std::shared_ptr<std::atomic<bool> > m_alive;
    muse::async::Notification m_ready;
};
}
