/*
 * GlideMix
 */
#pragma once

#include <list>
#include <mutex>
#include <set>

#include <QHash>
#include <QString>
#include <QThreadPool>
#include <QQuickImageProvider>

#include "async/asyncable.h"
#include "mixhouse/mixhousehdwaveform.h"

namespace au::mixhouse {
//! Waves in memory by file, analysed on a small thread pool the first time
//! they are asked for, and kept on disk in Library/Waveforms so the analysis
//! happens once per song.
class MixHouseHdWaveforms : public IMixHouseHdWaveforms, public muse::async::Asyncable
{
public:
    MixHouseHdWaveforms();
    ~MixHouseHdWaveforms() override;

    MixHouseHdWavePtr wave(const QString& filePath) override;
    std::pair<MixHouseHdWavePtr, MixHouseHdWavePtr> stereoWave(const QString& filePath) override;
    muse::async::Notification ready() const override;

    static QString cacheDir();

private:
    static QString cacheFile(const QString& filePath, const QString& variant = QString());
    static MixHouseHdWavePtr load(const QString& cachePath);
    static void save(const QString& cachePath, const MixHouseHdWave& wave);
    void remember(const QString& filePath, MixHouseHdWavePtr wave);
    void finished(const QString& filePath, MixHouseHdWavePtr wave);
    void queueNotify();

    std::mutex m_mutex;
    QHash<QString, MixHouseHdWavePtr> m_waves;
    std::list<QString> m_order;          // oldest first, to keep memory bounded
    QHash<QString, std::pair<MixHouseHdWavePtr, MixHouseHdWavePtr> > m_stereo;
    std::list<QString> m_stereoOrder;
    std::set<QString> m_pending;
    std::set<QString> m_failed;          // files that could not be decoded
    QThreadPool m_pool;
    muse::async::Notification m_ready;
    bool m_notifyQueued = false;
};

//! image://glidemixwave/<percent-encoded file path>?rev=<n>: the whole song's
//! HD waveform at the size asked for, for the library's Waveform Overview
//! column. Empty (an error, so the old overview stays) until it is analysed.
class MixHouseHdWaveImageProvider : public QQuickImageProvider
{
public:
    explicit MixHouseHdWaveImageProvider(std::shared_ptr<IMixHouseHdWaveforms> waves);
    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

private:
    std::shared_ptr<IMixHouseHdWaveforms> m_waves;
};
}
