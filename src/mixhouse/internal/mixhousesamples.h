/*
 * GlideMix Samples: see imixhousesamples.h.
 */
#pragma once

#include <QSqlDatabase>
#include <QThread>
#include <atomic>
#include <memory>

#include "async/asyncable.h"
#include "mixhouse/imixhousesamples.h"

namespace au::mixhouse {
class MixHouseSamples : public IMixHouseSamples, public muse::async::Asyncable
{
public:
    MixHouseSamples();
    ~MixHouseSamples() override;

    void init();
    void deinit();

    void load() override;
    int sampleCount() const override;
    QStringList folders() const override;
    void addFolder(const QString& path) override;
    void removeFolder(const QString& path) override;
    void rescan() override;

    bool scanning() const override { return m_scanning; }
    muse::async::Channel<int, int> scanProgress() const override { return m_scanProgress; }

    const std::vector<MixHouseSample>& samples() const override { return m_samples; }
    muse::async::Notification samplesChanged() const override { return m_samplesChanged; }
    void showInSamplesTab(const QString& path, bool similar) override
    {
        m_showPath = path;
        m_showSimilar = similar;
        m_showRequested.notify();
    }
    bool takeShowRequest(QString& path, bool& similar) override
    {
        if (m_showPath.isEmpty()) {
            return false;
        }
        path = m_showPath;
        similar = m_showSimilar;
        m_showPath.clear();
        return true;
    }
    muse::async::Notification showRequested() const override { return m_showRequested; }

    static QString databasePath();

private:
    bool ensureOpen();
    void reload();
    void startScan(const QStringList& folders);

    QSqlDatabase m_db;
    QStringList m_folders;
    std::vector<MixHouseSample> m_samples;
    bool m_scanning = false;
    bool m_loaded = false;
    mutable int m_countCache = -1;
    QStringList m_pendingFolders;
    QThread* m_thread = nullptr;
    std::shared_ptr<std::atomic<bool> > m_stop = std::make_shared<std::atomic<bool> >(false);
    muse::async::Channel<int, int> m_scanProgress;
    muse::async::Notification m_samplesChanged;
    muse::async::Notification m_showRequested;
    QString m_showPath;
    bool m_showSimilar = false;
};
}
