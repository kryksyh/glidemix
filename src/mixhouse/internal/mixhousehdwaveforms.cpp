/*
 * GlideMix
 */
#include "mixhousehdwaveforms.h"
#include "mixhouse/glidemixpaths.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTimer>
#include <QUrl>

#include "log.h"

using namespace au::mixhouse;

namespace {
constexpr size_t kKeptInMemory = 400;   // ~100 KB each
constexpr quint32 kMagic = 0x474D5732;  // "GMW2": with the loud levels
}

MixHouseHdWaveforms::MixHouseHdWaveforms()
{
    // Analysis decodes a whole song; three at a time keep the UI smooth.
    m_pool.setMaxThreadCount(3);
}

MixHouseHdWaveforms::~MixHouseHdWaveforms()
{
    m_pool.clear();
    m_pool.waitForDone();
}

QString MixHouseHdWaveforms::cacheDir()
{
    return paths::glidemixDataPath(QStringLiteral("Library/Waveforms"));
}

//! Named after the file and its size and date, so an edited file is analysed again.
QString MixHouseHdWaveforms::cacheFile(const QString& filePath, const QString& variant)
{
    const QFileInfo info(filePath);
    const QByteArray key = (filePath + QLatin1Char('|') + QString::number(info.size()) + QLatin1Char('|')
                            + QString::number(info.lastModified().toSecsSinceEpoch())).toUtf8();
    return cacheDir() + QLatin1Char('/')
           + QString::fromLatin1(QCryptographicHash::hash(key, QCryptographicHash::Sha1).toHex()) + variant + QStringLiteral(".gmw");
}

MixHouseHdWavePtr MixHouseHdWaveforms::load(const QString& cachePath)
{
    QFile file(cachePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return nullptr;
    }
    QDataStream in(&file);
    quint32 magic = 0;
    double rate = 0, seconds = 0, refLow = 1, refMid = 1, refHigh = 1;
    QByteArray packed;
    in >> magic;
    if (magic != kMagic) {
        return nullptr;   // an older analysis: done again
    }
    in >> rate >> seconds >> refLow >> refMid >> refHigh >> packed;
    const QByteArray raw = qUncompress(packed);
    if (magic != kMagic || in.status() != QDataStream::Ok || raw.size() % 3 != 0 || raw.isEmpty() || rate <= 0) {
        return nullptr;
    }
    auto wave = std::make_shared<MixHouseHdWave>();
    wave->rate = rate;
    wave->seconds = seconds;
    wave->refLow = refLow;
    wave->refMid = refMid;
    wave->refHigh = refHigh;
    const size_t n = static_cast<size_t>(raw.size() / 3);
    const auto* bytes = reinterpret_cast<const uint8_t*>(raw.constData());
    wave->low.assign(bytes, bytes + n);
    wave->mid.assign(bytes + n, bytes + 2 * n);
    wave->high.assign(bytes + 2 * n, bytes + 3 * n);
    return wave;
}

void MixHouseHdWaveforms::save(const QString& cachePath, const MixHouseHdWave& wave)
{
    QDir().mkpath(cacheDir());
    QByteArray raw;
    raw.reserve(static_cast<int>(wave.size() * 3));
    raw.append(reinterpret_cast<const char*>(wave.low.data()), static_cast<int>(wave.low.size()));
    raw.append(reinterpret_cast<const char*>(wave.mid.data()), static_cast<int>(wave.mid.size()));
    raw.append(reinterpret_cast<const char*>(wave.high.data()), static_cast<int>(wave.high.size()));
    QSaveFile file(cachePath);
    if (!file.open(QIODevice::WriteOnly)) {
        return;
    }
    QDataStream out(&file);
    out << kMagic << wave.rate << wave.seconds << wave.refLow << wave.refMid << wave.refHigh << qCompress(raw, 6);
    file.commit();
}

void MixHouseHdWaveforms::remember(const QString& filePath, MixHouseHdWavePtr wave)
{
    // Called with m_mutex held.
    m_waves.insert(filePath, std::move(wave));
    m_order.push_back(filePath);
    while (m_order.size() > kKeptInMemory) {
        m_waves.remove(m_order.front());
        m_order.pop_front();
    }
}

MixHouseHdWavePtr MixHouseHdWaveforms::wave(const QString& filePath)
{
    if (filePath.isEmpty()) {
        return nullptr;
    }
    std::lock_guard lock(m_mutex);
    if (const auto it = m_waves.constFind(filePath); it != m_waves.constEnd()) {
        return it.value();
    }
    if (m_pending.count(filePath) || m_failed.count(filePath)) {
        return nullptr;
    }
    m_pending.insert(filePath);
    m_pool.start([this, filePath]() {
        const QString cachePath = cacheFile(filePath);
        MixHouseHdWavePtr wave = load(cachePath);
        if (!wave) {
            wave = analyzeHdWave(filePath);
            if (wave) {
                save(cachePath, *wave);
            } else {
                LOGW() << "GlideMix waveform: could not analyse " << filePath;
            }
        }
        finished(filePath, std::move(wave));
    });
    return nullptr;
}

std::pair<MixHouseHdWavePtr, MixHouseHdWavePtr> MixHouseHdWaveforms::stereoWave(const QString& filePath)
{
    if (filePath.isEmpty()) {
        return {};
    }
    std::lock_guard lock(m_mutex);
    if (const auto it = m_stereo.constFind(filePath); it != m_stereo.constEnd()) {
        return it.value();
    }
    const QString key = QStringLiteral("stereo|") + filePath;
    if (m_pending.count(key) || m_failed.count(key)) {
        return {};
    }
    m_pending.insert(key);
    m_pool.start([this, filePath, key]() {
        const QString leftPath = cacheFile(filePath, QStringLiteral(".l"));
        const QString rightPath = cacheFile(filePath, QStringLiteral(".r"));
        std::pair<MixHouseHdWavePtr, MixHouseHdWavePtr> sides { load(leftPath), load(rightPath) };
        if (!sides.first || !sides.second) {
            sides = analyzeHdWaveStereo(filePath);
            if (sides.first && sides.second) {
                save(leftPath, *sides.first);
                save(rightPath, *sides.second);
            }
        }
        std::lock_guard lock(m_mutex);
        m_pending.erase(key);
        if (!sides.first || !sides.second) {
            m_failed.insert(key);
            return;
        }
        m_stereo.insert(filePath, sides);
        m_stereoOrder.push_back(filePath);
        while (m_stereoOrder.size() > kKeptInMemory / 2) {
            m_stereo.remove(m_stereoOrder.front());
            m_stereoOrder.pop_front();
        }
        queueNotify();
    });
    return {};
}

void MixHouseHdWaveforms::queueNotify()
{
    // Called with m_mutex held. Many songs finish together while the library
    // scrolls: one repaint for them all, a moment later, on the main thread.
    if (!m_notifyQueued) {
        m_notifyQueued = true;
        QMetaObject::invokeMethod(qApp, [this]() {
            QTimer::singleShot(120, qApp, [this]() {
                {
                    std::lock_guard lock(m_mutex);
                    m_notifyQueued = false;
                }
                m_ready.notify();
            });
        }, Qt::QueuedConnection);
    }
}

void MixHouseHdWaveforms::finished(const QString& filePath, MixHouseHdWavePtr wave)
{
    std::lock_guard lock(m_mutex);
    m_pending.erase(filePath);
    if (!wave) {
        m_failed.insert(filePath);
        return;
    }
    remember(filePath, std::move(wave));
    queueNotify();
}

muse::async::Notification MixHouseHdWaveforms::ready() const
{
    return m_ready;
}

MixHouseHdWaveImageProvider::MixHouseHdWaveImageProvider(std::shared_ptr<IMixHouseHdWaveforms> waves)
    : QQuickImageProvider(QQuickImageProvider::Image, QQmlImageProviderBase::ForceAsynchronousImageLoading),
    m_waves(std::move(waves))
{
}

QImage MixHouseHdWaveImageProvider::requestImage(const QString& id, QSize* size, const QSize& requestedSize)
{
    const QString path = QUrl::fromPercentEncoding(id.section(QLatin1Char('?'), 0, 0).toUtf8());
    const MixHouseHdWavePtr wave = m_waves ? m_waves->wave(path) : nullptr;
    const int w = requestedSize.width() > 0 ? std::min(requestedSize.width(), 4096) : 600;
    const int h = requestedSize.height() > 0 ? std::min(requestedSize.height(), 512) : 40;
    if (!wave) {
        return QImage();
    }
    QImage image(w, h, QImage::Format_ARGB32_Premultiplied);
    paintHdWave(image, *wave, 0.0, wave->seconds);
    if (size) {
        *size = image.size();
    }
    return image;
}
