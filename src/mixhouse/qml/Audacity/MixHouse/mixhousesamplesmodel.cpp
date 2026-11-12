/*
 * GlideMix Samples tab: see mixhousesamplesmodel.h.
 */
#include "mixhousesamplesmodel.h"

#include <QCollator>
#include <QDateTime>
#include <QDir>
#include <QProcess>
#include <QSet>
#include <QSettings>
#include <QUrl>
#include <algorithm>
#include <cmath>
#include <thread>
#include <QCoreApplication>
#include <QCursor>
#include <QDrag>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMimeData>
#include <QMouseEvent>
#include <QWindow>
#include <map>
#if defined(Q_OS_MACOS)
#include <ApplicationServices/ApplicationServices.h>
#elif defined(Q_OS_WIN)
#include <windows.h>
#endif

#include "mixhouse/imixhousekeymatch.h"
#include "mixhouse/internal/mixhousepreviewplayer.h"
#include "mixhouse/mixhouseaudiosource.h"
#include "mixhousetracklistmodel.h"
#include "global/settings.h"
#include "log.h"
#include "translation.h"

namespace au::mixhouse {
namespace {
std::vector<MixHouseSamplesModel*> s_sampleModels;
}

MixHouseSamplesModel::MixHouseSamplesModel(QObject* parent)
    : QAbstractListModel(parent), muse::Injectable(muse::iocCtxForQmlObject(this))
{
    s_sampleModels.push_back(this);
    // A loop starts over when it ends; a one-shot's button goes back to play.
    m_previewPoll.setInterval(40);
    connect(&m_previewPoll, &QTimer::timeout, this, [this]() {
        if (!m_player) {
            return;
        }
        if (m_player->isPlaying()) {
            const double pos = m_player->position();
            if (pos != m_previewPosition) {
                m_previewPosition = pos;
                emit previewPositionChanged();
            }
            return;
        }
        if (m_previewLoop) {
            m_player->playFrom(0.0);
        } else {
            stopPreview();
        }
    });
}

MixHouseSamplesModel::~MixHouseSamplesModel()
{
    *m_alive = false;
    s_sampleModels.erase(std::remove(s_sampleModels.begin(), s_sampleModels.end(), this), s_sampleModels.end());
    if (m_player) {
        m_player->stop();
    }
}

void MixHouseSamplesModel::stopAllPreviews()
{
    for (MixHouseSamplesModel* m : s_sampleModels) {
        m->stopPreview();
    }
}

double MixHouseSamplesModel::previewRate(const MixHouseSample& s) const
{
    // A loop plays at the tempo under the playhead (same pitch), folded by
    // halves and doubles: a 70 BPM loop in a 140 BPM mix plays at its own speed.
    if (!s.loop || s.bpm <= 0.0 || !tempoMap() || !globalContext() || !globalContext()->playbackState()) {
        return 1.0;
    }
    const double mixBpm = tempoMap()->bpmAt(globalContext()->playbackState()->playbackPosition().to_double());
    if (mixBpm <= 0.0) {
        return 1.0;
    }
    double rate = mixBpm / s.bpm;
    while (rate >= 1.5) {
        rate /= 2.0;
    }
    while (rate < 0.75) {
        rate *= 2.0;
    }
    return rate;
}

void MixHouseSamplesModel::togglePreview(int row)
{
    const QString path = pathAt(row);
    if (path.isEmpty()) {
        return;
    }
    if (path == m_previewPath) {
        stopPreview();
        return;
    }
    startPreview(row, 0.0);
}

void MixHouseSamplesModel::previewAt(int row, double fraction)
{
    const QString path = pathAt(row);
    if (path.isEmpty()) {
        return;
    }
    fraction = std::clamp(fraction, 0.0, 0.999);
    if (path == m_previewPath && m_player) {
        m_player->playFrom(fraction);   // already playing this one: jump there
        return;
    }
    startPreview(row, fraction);
}

void MixHouseSamplesModel::startPreview(int row, double fraction)
{
    const QString path = pathAt(row);
    MixHouseTrackListModel::stopAllPreviews();   // one preview at a time
    for (MixHouseSamplesModel* m : s_sampleModels) {
        if (m != this) {
            m->stopPreview();
        }
    }
    const MixHouseSample& s = samples()->samples()[static_cast<size_t>(m_rows[static_cast<size_t>(row)])];
    if (!m_player) {
        m_player = std::make_unique<MixHousePreviewPlayer>();
    }
    const std::string device = muse::settings()->value(
        muse::Settings::Key("au3audio", "AudioIO/PlaybackDevice")).toString();
    if (!m_player->load(path.toStdString(), device)) {
        LOGW() << "GlideMix samples: cannot preview " << path;
        stopPreview();
        return;
    }
    m_player->setVolume(previewVolume());
    m_player->setRate(previewRate(s));
    m_player->playFrom(fraction);
    m_previewPath = path;
    m_previewPosition = fraction;
    m_previewLoop = s.loop;
    m_previewPoll.start();
    emit previewChanged();
}

double MixHouseSamplesModel::previewVolume() const
{
    return QSettings().value(QStringLiteral("GlideMix/previewVolume"), 0.7).toDouble();
}

void MixHouseSamplesModel::setPreviewVolume(double volume)
{
    volume = std::clamp(volume, 0.0, 1.0);
    QSettings().setValue(QStringLiteral("GlideMix/previewVolume"), volume);
    if (m_player) {
        m_player->setVolume(volume);
    }
    emit previewChanged();
}

int MixHouseSamplesModel::previewRow() const
{
    if (m_previewPath.isEmpty() || !samples()) {
        return -1;
    }
    for (size_t r = 0; r < m_rows.size(); ++r) {
        if (samples()->samples()[static_cast<size_t>(m_rows[r])].path == m_previewPath) {
            return static_cast<int>(r);
        }
    }
    return -1;
}

void MixHouseSamplesModel::requestOverview(const QString& path) const
{
    if (!path.isEmpty() && !m_overviewQueue.contains(path)) {
        // The newest request first: the rows on screen now.
        m_overviewQueue.prepend(path);
        while (m_overviewQueue.size() > 200) {
            m_overviewQueue.removeLast();   // scrolled past long ago
        }
    }
    if (m_overviewBusy || m_overviewQueue.isEmpty()) {
        return;
    }
    m_overviewBusy = true;
    const QString next = m_overviewQueue.takeFirst();
    auto* self = const_cast<MixHouseSamplesModel*>(this);
    const auto alive = m_alive;
    std::thread([self, alive, next]() {
        // 160 peaks (0-255) over the whole sample: a decode of a few ms for
        // a typical one-shot or loop.
        constexpr int kBuckets = 800;
        const AudioInfo info = probeAudioFile(next);
        QVariantList peaks;
        if (info.valid && info.frames > 0) {
            std::vector<float> maxes(kBuckets, 0.0f);
            long long at = 0;
            const int channels = std::max(1, info.channels);
            decodeAudioFile(next, [&](const float* samples, long long frames) {
                for (long long i = 0; i < frames; ++i, ++at) {
                    const int bucket = static_cast<int>(std::min<long long>(kBuckets - 1, at * kBuckets / info.frames));
                    for (int c = 0; c < channels; ++c) {
                        maxes[bucket] = std::max(maxes[bucket], std::abs(samples[i * channels + c]));
                    }
                }
                return alive->load();
            });
            // Scaled to the sample's own peak, as sample players show them
            // (a quiet field recording would otherwise be a flat line).
            const float top = std::max(1e-4f, *std::max_element(maxes.begin(), maxes.end()));
            peaks.reserve(kBuckets);
            for (float m : maxes) {
                peaks << static_cast<int>(std::lround(std::min(1.0f, m / top) * 255.0f));
            }
        } else {
            peaks << 0;   // unreadable: a flat line, and not asked again
        }
        QMetaObject::invokeMethod(qApp, [self, alive, next, peaks]() {
            if (!*alive) {
                return;
            }
            self->m_overviews.insert(next, peaks);
            self->m_overviewBusy = false;
            ++self->m_overviewsReady;
            emit self->overviewsChanged();
            for (size_t r = 0; r < self->m_rows.size(); ++r) {
                if (self->samples()->samples()[static_cast<size_t>(self->m_rows[r])].path == next) {
                    const QModelIndex i = self->index(static_cast<int>(r));
                    emit self->dataChanged(i, i, { OverviewRole });
                    break;
                }
            }
            self->requestOverview(QString());
        }, Qt::QueuedConnection);
    }).detach();
}

void MixHouseSamplesModel::stopPreview()
{
    m_previewPoll.stop();
    if (m_player) {
        m_player->stop();
    }
    if (!m_previewPath.isEmpty()) {
        m_previewPath.clear();
        emit previewChanged();
    }
}

void MixHouseSamplesModel::componentComplete()
{
    if (!samples()) {
        return;
    }
    samples()->samplesChanged().onNotify(this, [this]() {
        updatePacks();
        rebuild();
        emit samplesChanged();
    });
    samples()->scanProgress().onReceive(this, [this](int done, int found) {
        m_scanText = muse::qtrc("mixhouse", "Scanning… %1 of %2 files").arg(done).arg(found);
        emit scanChanged();
    });
    samples()->load();
    updatePacks();
    rebuild();
    emit samplesChanged();
}

int MixHouseSamplesModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QHash<int, QByteArray> MixHouseSamplesModel::roleNames() const
{
    return {
        { NameRole, "name" }, { CategoryRole, "category" }, { PackRole, "pack" }, { TypeRole, "type" },
        { BpmRole, "bpm" }, { KeyRole, "key" }, { KeyIdRole, "keyId" }, { BarsRole, "bars" },
        { LengthRole, "length" }, { FormatRole, "format" }, { LocationRole, "location" },
        { DateAddedRole, "dateAdded" }, { MissingRole, "missing" }, { PathRole, "path" },
        { OverviewRole, "overview" },
    };
}

QVariant MixHouseSamplesModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= static_cast<int>(m_rows.size()) || !samples()) {
        return {};
    }
    const MixHouseSample& s = samples()->samples()[static_cast<size_t>(m_rows[static_cast<size_t>(index.row())])];
    switch (role) {
    case NameRole: return s.name;
    case CategoryRole: return s.category;
    case PackRole: return s.pack;
    case TypeRole: return s.loop ? muse::qtrc("mixhouse", "Loop") : muse::qtrc("mixhouse", "One-shot");
    case BpmRole: return s.bpm;
    case KeyRole:
        return s.keyId > 0 && libraryConfiguration()
               ? IMixHouseKeyMatch::keyText(s.keyId, libraryConfiguration()->keyNotation()) : QString();
    case KeyIdRole: return s.keyId;
    case BarsRole: return s.bars;
    case LengthRole: return s.durationSec;
    case FormatRole: return s.format;
    case LocationRole: return QDir::toNativeSeparators(s.folder + (s.location.isEmpty() ? QString() : QStringLiteral("/") + s.location));
    case DateAddedRole: return QDateTime::fromSecsSinceEpoch(s.dateAdded).toString(QStringLiteral("yyyy-MM-dd"));
    case MissingRole: return s.missing;
    case PathRole: return s.path;
    case OverviewRole: {
        const auto it = m_overviews.constFind(s.path);
        if (it != m_overviews.constEnd()) {
            return *it;
        }
        if (!s.missing) {
            requestOverview(s.path);
        }
        return QVariantList();
    }
    }
    return {};
}

void MixHouseSamplesModel::setCategory(const QString& category)
{
    if (m_category == category) {
        return;
    }
    m_category = category;
    m_pack.clear();   // a pack belongs to its category
    updatePacks();
    rebuild();
    emit filterChanged();
}

void MixHouseSamplesModel::setPack(const QString& pack)
{
    if (m_pack == pack) {
        return;
    }
    m_pack = pack;
    rebuild();
    emit filterChanged();
}

void MixHouseSamplesModel::setSearch(const QString& search)
{
    if (m_search == search) {
        return;
    }
    m_search = search;
    rebuild();
    emit filterChanged();
}

int MixHouseSamplesModel::totalCount() const
{
    return samples() ? static_cast<int>(samples()->samples().size()) : 0;
}

QVariantList MixHouseSamplesModel::categories() const
{
    QVariantList out;
    if (!samples()) {
        return out;
    }
    std::map<QString, int> counts;
    for (const MixHouseSample& s : samples()->samples()) {
        ++counts[s.category];
    }
    out << QVariantMap { { "name", QString() }, { "title", muse::qtrc("mixhouse", "All") },
                         { "count", totalCount() } };
    for (const auto& [name, count] : counts) {
        out << QVariantMap { { "name", name },
                             { "title", name.isEmpty() ? muse::qtrc("mixhouse", "Other") : name },
                             { "count", count } };
    }
    return out;
}

QStringList MixHouseSamplesModel::folders() const
{
    return samples() ? samples()->folders() : QStringList();
}

bool MixHouseSamplesModel::scanning() const
{
    return samples() && samples()->scanning();
}

void MixHouseSamplesModel::updatePacks()
{
    QSet<QString> set;
    if (samples()) {
        for (const MixHouseSample& s : samples()->samples()) {
            if (!s.pack.isEmpty() && (m_category.isEmpty() || s.category == m_category)) {
                set.insert(s.pack);
            }
        }
    }
    QStringList packs(set.begin(), set.end());
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(packs.begin(), packs.end(), [&](const QString& a, const QString& b) { return collator.compare(a, b) < 0; });
    if (packs != m_packs) {
        m_packs = packs;
        emit packsChanged();
    }
    emit scanChanged();
}

void MixHouseSamplesModel::rebuild()
{
    beginResetModel();
    m_rows.clear();
    if (samples()) {
        const auto& all = samples()->samples();
        const QStringList words = m_search.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        for (size_t i = 0; i < all.size(); ++i) {
            const MixHouseSample& s = all[i];
            if (!m_category.isEmpty() && s.category != m_category) {
                continue;
            }
            if (!m_pack.isEmpty() && s.pack != m_pack) {
                continue;
            }
            bool match = true;
            for (const QString& w : words) {
                if (!s.name.contains(w, Qt::CaseInsensitive) && !s.location.contains(w, Qt::CaseInsensitive)) {
                    match = false;
                    break;
                }
            }
            if (match) {
                m_rows.push_back(static_cast<int>(i));
            }
        }
        QCollator collator;
        collator.setCaseSensitivity(Qt::CaseInsensitive);
        collator.setNumericMode(true);   // "Kick 2" before "Kick 10"
        const QString key = m_sortKey;
        const bool asc = m_ascending;
        auto text = [&](const QString& a, const QString& b) { return collator.compare(a, b); };
        auto num = [](double a, double b) { return a < b ? -1 : (a > b ? 1 : 0); };
        std::stable_sort(m_rows.begin(), m_rows.end(), [&](int ia, int ib) {
            const MixHouseSample& a = all[static_cast<size_t>(ia)];
            const MixHouseSample& b = all[static_cast<size_t>(ib)];
            // Unknown tempo, key, bars or length: last, in either direction.
            const auto known = [&](const MixHouseSample& s) {
                if (key == u"bpm") return s.bpm > 0.0;
                if (key == u"key") return s.keyId > 0;
                if (key == u"bars") return s.bars > 0;
                if (key == u"length") return s.durationSec > 0.0;
                return true;
            };
            const bool ka = known(a), kb = known(b);
            if (ka != kb) {
                return ka;
            }
            int c = 0;
            if (key == u"category") c = text(a.category, b.category);
            else if (key == u"pack") c = text(a.pack, b.pack);
            else if (key == u"type") c = num(a.loop, b.loop);
            else if (key == u"bpm") c = num(a.bpm, b.bpm);
            else if (key == u"key") c = num(a.keyId, b.keyId);
            else if (key == u"bars") c = num(a.bars, b.bars);
            else if (key == u"length") c = num(a.durationSec, b.durationSec);
            else if (key == u"format") c = text(a.format, b.format);
            else if (key == u"location") c = text(a.location, b.location);
            else if (key == u"dateAdded") c = num(static_cast<double>(a.dateAdded), static_cast<double>(b.dateAdded));
            if (c == 0) {
                c = text(a.name, b.name);
            }
            return asc ? c < 0 : c > 0;
        });
    }
    endResetModel();
}

void MixHouseSamplesModel::sortBy(const QString& key)
{
    if (m_sortKey == key) {
        m_ascending = !m_ascending;
    } else {
        m_sortKey = key;
        m_ascending = key != u"dateAdded";
    }
    rebuild();
    emit sortChanged();
}

void MixHouseSamplesModel::addFolder(const QString& folder)
{
    if (samples() && !folder.isEmpty()) {
        samples()->addFolder(folder);
        emit scanChanged();
    }
}

void MixHouseSamplesModel::removeFolder(const QString& folder)
{
    if (samples()) {
        samples()->removeFolder(folder);
    }
}

void MixHouseSamplesModel::rescan()
{
    if (samples()) {
        samples()->rescan();
        emit scanChanged();
    }
}

QVariantMap MixHouseSamplesModel::sampleInfo(int row) const
{
    QVariantMap m;
    if (row < 0 || row >= static_cast<int>(m_rows.size()) || !samples()) {
        return m;
    }
    const QModelIndex i = index(row);
    const QHash<int, QByteArray> roles = roleNames();   // one copy: iterators of two temporaries never meet
    for (auto it = roles.cbegin(); it != roles.cend(); ++it) {
        m.insert(QString::fromLatin1(it.value()), data(i, it.key()));
    }
    m.insert(QStringLiteral("row"), row);
    return m;
}

QString MixHouseSamplesModel::pathAt(int row) const
{
    if (row < 0 || row >= static_cast<int>(m_rows.size()) || !samples()) {
        return QString();
    }
    return samples()->samples()[static_cast<size_t>(m_rows[static_cast<size_t>(row)])].path;
}

void MixHouseSamplesModel::startDrag(int row)
{
    const QString path = pathAt(row);
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        return;   // its disk is not connected
    }
    // Only while the button is really still held (the library's lesson: a
    // drag started after the release stuck to the mouse).
#if defined(Q_OS_MACOS)
    const bool held = CGEventSourceButtonState(kCGEventSourceStateCombinedSessionState, kCGMouseButtonLeft);
#elif defined(Q_OS_WIN)
    const bool held = (GetAsyncKeyState(GetSystemMetrics(SM_SWAPBUTTON) ? VK_RBUTTON : VK_LBUTTON) & 0x8000) != 0;
#else
    const bool held = true;
#endif
    if (!held) {
        return;
    }
    stopPreview();
    auto* mime = new QMimeData();
    mime->setUrls({ QUrl::fromLocalFile(path) });
    mime->setText(QStringLiteral("samples"));
    QDrag* drag = new QDrag(this);
    drag->setMimeData(mime);
    drag->exec(Qt::CopyAction);
    // The OS drag swallowed the release: hand it to the window, or the row
    // keeps the mouse grab and the next press finishes a stale drag.
    if (QWindow* window = QGuiApplication::topLevelAt(QCursor::pos()) ? QGuiApplication::topLevelAt(QCursor::pos())
                                                                        : QGuiApplication::focusWindow()) {
        const QPointF local = window->mapFromGlobal(QCursor::pos());
        QMouseEvent release(QEvent::MouseButtonRelease, local, local, QCursor::pos(), Qt::LeftButton,
                            Qt::NoButton, QGuiApplication::keyboardModifiers());
        QCoreApplication::sendEvent(window, &release);
    }
}

void MixHouseSamplesModel::revealInFileBrowser(int row) const
{
    const QString path = pathAt(row);
    if (path.isEmpty()) {
        return;
    }
#ifdef Q_OS_WIN
    QProcess::startDetached(QStringLiteral("explorer.exe"), { QStringLiteral("/select,"), QDir::toNativeSeparators(path) });
#else
    QProcess::startDetached(QStringLiteral("/usr/bin/open"), { QStringLiteral("-R"), path });
#endif
}
}
