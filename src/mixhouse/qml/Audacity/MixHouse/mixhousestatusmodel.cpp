/*
 * GlideMix
 */
#include "mixhousestatusmodel.h"

#include <QFileInfo>
#include <QLocale>

#include "translation.h"

using namespace au::mixhouse;

namespace {
// How long a "finished" message stays before the line goes back to idle.
constexpr int kMessageMs = 12000;
// The quiet update check: shortly after start, then every few hours.
constexpr int kFirstUpdateCheckMs = 15000;
constexpr int kUpdateCheckEveryMs = 6 * 60 * 60 * 1000;
// How long the line keeps showing a finished activity before going idle.
constexpr int kSettleMs = 1500;

QString duration(qint64 ms)
{
    const qint64 s = ms / 1000;
    if (s < 60) {
        return muse::qtrc("mixhouse", "%n second(s)", nullptr, int(std::max<qint64>(1, s)));
    }
    if (s < 3600) {
        return muse::qtrc("mixhouse", "%n minute(s)", nullptr, int((s + 30) / 60));
    }
    return muse::qtrc("mixhouse", "%1 h %2 min").arg(s / 3600).arg((s % 3600) / 60);
}
}

MixHouseStatusModel::MixHouseStatusModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
    m_messageTimer.setSingleShot(true);
    QObject::connect(&m_messageTimer, &QTimer::timeout, this, [this]() {
        m_message.clear();
        refresh();
    });
    m_rekordboxPoll.setInterval(250);
    QObject::connect(&m_rekordboxPoll, &QTimer::timeout, this, [this]() { refresh(); });
    m_settleTimer.setSingleShot(true);
    QObject::connect(&m_settleTimer, &QTimer::timeout, this, [this]() { refresh(); });
    m_updateTimer.setSingleShot(true);
    QObject::connect(&m_updateTimer, &QTimer::timeout, this, [this]() { checkForUpdate(); });
}

void MixHouseStatusModel::componentComplete()
{
    // GlideMix: the demo's voice would sound like a bug if nobody said so first.
    if (playbackController() && licence() && licence()->tier() == LicenceTier::Demo) {
        playbackController()->isPlayingChanged().onNotify(this, [this]() {
            static bool told = false;
            if (!told && playbackController()->isPlaying()) {
                told = true;
                m_message = muse::qtrc("mixhouse", "Demo version: a voice says \u201cGlide Mix demo\u201d every 2 minutes. A licence removes it: Help > Licence");
                m_messageTimer.start(20000);
                refresh();
            }
        });
    }
    if (library()) {
        m_scanning = library()->isScanning();
        m_analysing = library()->isAnalyzing();
        library()->scanProgress().onReceive(this, [this](int scanned, int added) {
            m_scanning = true;
            m_scanned = scanned;
            m_scanAdded = added;
            refresh();
        });
        library()->scanFinished().onReceive(this, [this](const IMixHouseLibrary::ScanResult& result) {
            m_scanning = false;
            if (!result.error.isEmpty()) {
                showMessage(muse::qtrc("mixhouse", "The scan stopped: %1").arg(result.error));
            } else if (result.added > 0) {
                showMessage(muse::qtrc("mixhouse", "%n new song(s) added to your collection", nullptr, result.added));
            } else if (!result.watch) {
                showMessage(muse::qtrc("mixhouse", "Scan finished: no new songs"));
            }
            refresh();
        });
        library()->analysisProgress().onReceive(this, [this](int processed, int total) {
            if (!m_analysing) {
                m_analysisClock.start();
            }
            m_analysing = true;
            m_analysed = processed;
            m_analyseTotal = total;
            refresh();
        });
        library()->trackAnalysisProgress().onReceive(this, [this](int trackId, int percent) {
            if (percent >= 0 && trackId != m_currentTrack) {
                if (!m_analysing) {
                    m_analysisClock.start();
                    m_analysing = true;
                }
                m_currentTrack = trackId;
                refresh();
            }
        });
        library()->analysisFinished().onReceive(this, [this](const IMixHouseLibrary::AnalysisSummary& s) {
            const qint64 ms = m_analysisClock.isValid() ? m_analysisClock.elapsed() : 0;
            m_analysing = false;
            m_currentTrack = -1;
            m_trackNames.clear();
            m_analysisClock.invalidate();
            if (s.total > 0) {
                QString msg = s.stopped
                              ? muse::qtrc("mixhouse", "Analysis stopped: %1 of %2 songs done").arg(s.succeeded).arg(s.total)
                              : muse::qtrc("mixhouse", "Analysis finished: %n song(s)", nullptr, s.succeeded);
                if (ms > 0 && !s.stopped) {
                    msg += muse::qtrc("mixhouse", " in %1").arg(duration(ms));
                }
                if (s.failed > 0) {
                    msg += muse::qtrc("mixhouse", ", %n could not be read", nullptr, s.failed);
                }
                showMessage(msg);
            }
            refresh();
        });
        library()->libraryChanged().onNotify(this, [this]() {
            if (!m_busy && m_message.isEmpty()) {
                refresh();
            }
        });
    }
    if (samples()) {
        samples()->samplesChanged().onNotify(this, [this]() {
            if (!m_busy && m_message.isEmpty()) {
                refresh();
            }
        });
    }
    if (rekordboxXml()) {
        rekordboxXml()->changed().onNotify(this, [this]() {
            if (rekordboxXml()->loading()) {
                m_rekordboxPoll.start();
            } else if (m_rekordboxPoll.isActive()) {
                m_rekordboxPoll.stop();
                const QString error = rekordboxXml()->loadError();
                showMessage(error.isEmpty() ? muse::qtrc("mixhouse", "rekordbox XML loaded") : error);
            }
            refresh();
        });
    }
    if (activity()) {
        activity()->changed().onNotify(this, [this]() {
            const QString msg = activity()->lastMessage();
            if (!msg.isEmpty() && msg != m_message) {
                showMessage(msg);
            }
            refresh();
        });
    }
    m_updateTimer.start(kFirstUpdateCheckMs);
    refresh();
}

void MixHouseStatusModel::showMessage(const QString& message)
{
    m_message = message;
    m_messageTimer.start(kMessageMs);
}

QString MixHouseStatusModel::trackName(int trackId)
{
    if (trackId < 0 || !library()) {
        return QString();
    }
    const auto cached = m_trackNames.constFind(trackId);
    if (cached != m_trackNames.constEnd()) {
        return *cached;
    }
    IMixHouseLibrary::TrackQuery one;
    one.trackId = trackId;
    one.limit = 1;
    const MixHouseTrackList found = library()->tracks(one);
    QString name;
    if (!found.empty()) {
        const MixHouseTrack& t = found.front();
        name = t.artist.isEmpty() ? t.title : t.artist + QStringLiteral(" – ") + t.title;
        if (name.trimmed().isEmpty()) {
            name = QFileInfo(t.filePath).completeBaseName();
        }
    }
    m_trackNames.insert(trackId, name);
    return name;
}

QString MixHouseStatusModel::idleText() const
{
    if (!library() || !library()->isAvailable()) {
        return QString();
    }
    const int count = library()->trackCount();
    if (count <= 0) {
        return muse::qtrc("mixhouse", "Add your music in Preferences → Music library");
    }
    // GlideMix (Chris): the samples beside the songs.
    const int sampleCount = samples() ? samples()->sampleCount() : 0;
    if (sampleCount > 0) {
        return muse::qtrc("mixhouse", "%1 songs and %2 samples in your collection")
               .arg(QLocale().toString(count), QLocale().toString(sampleCount));
    }
    return muse::qtrc("mixhouse", "%1 songs in your collection").arg(QLocale().toString(count));
}

void MixHouseStatusModel::refresh()
{
    bool busy = true;
    QString text;
    int percent = -1;

    if (m_analysing) {
        const int total = std::max(m_analyseTotal, 1);
        if (m_analyseTotal > 0) {
            text = muse::qtrc("mixhouse", "Analysing %1 of %2").arg(std::min(m_analysed + 1, m_analyseTotal)).arg(m_analyseTotal);
            percent = m_analysed * 100 / total;
        } else {
            text = muse::qtrc("mixhouse", "Analysing");
        }
        // No song names: several songs are analysed at once, so naming one
        // made the line flicker. Count, bar and time left only.
        // A rough time left once a few songs are done.
        if (m_analysisClock.isValid() && m_analysed >= 3 && m_analyseTotal > m_analysed) {
            const qint64 left = m_analysisClock.elapsed() / m_analysed * (m_analyseTotal - m_analysed);
            text += muse::qtrc("mixhouse", " · about %1 left").arg(duration(left));
        }
    } else if (m_scanning) {
        text = muse::qtrc("mixhouse", "Scanning your music folders: %1 files checked, %2 new")
               .arg(QLocale().toString(m_scanned)).arg(QLocale().toString(m_scanAdded));
    } else if (rekordboxXml() && rekordboxXml()->loading()) {
        text = muse::qtrc("mixhouse", "Loading rekordbox XML");
        percent = rekordboxXml()->loadProgress();
    } else if (activity() && !activity()->activities().isEmpty()) {
        const IMixHouseActivity::Activity a = activity()->activities().constLast();
        text = a.text;
        percent = a.percent;
    } else {
        busy = false;
        text = m_message.isEmpty() ? idleText() : m_message;
    }

    // Between two batches (watch folders queue new songs as they arrive) the
    // line stays on the last activity for a moment instead of flashing idle.
    if (busy) {
        m_lastBusy.restart();
    } else if (m_busy && m_lastBusy.isValid() && m_lastBusy.elapsed() < kSettleMs) {
        if (!m_settleTimer.isActive()) {
            m_settleTimer.start(kSettleMs - int(m_lastBusy.elapsed()) + 50);
        }
        return;
    }
    if (busy != m_busy || text != m_text || percent != m_percent) {
        m_busy = busy;
        m_text = text;
        m_percent = percent;
        emit statusChanged();
    }
}

void MixHouseStatusModel::checkForUpdate()
{
    m_updateTimer.start(kUpdateCheckEveryMs);
    if (!updateService() || !updateConfiguration() || !updateConfiguration()->needCheckForUpdate()) {
        return;
    }
    updateService()->checkForUpdate().onResolve(this, [this](const muse::RetVal<muse::update::ReleaseInfo>& res) {
        const QString version = res.ret ? QString::fromStdString(res.val.version) : QString();
        if (version != m_updateVersion) {
            m_updateVersion = version;
            emit updateChanged();
        }
    });
}

void MixHouseStatusModel::openUpdate()
{
    if (!updateScenario()) {
        return;
    }
#ifdef Q_OS_MAC
    // Sparkle shows the new version and installs it.
    updateScenario()->checkForUpdate(true);
#else
    updateScenario()->showUpdate();
#endif
}

void MixHouseStatusModel::stop()
{
    if (m_analysing && library()) {
        library()->stopBackgroundAnalysis();
    }
}

bool MixHouseStatusModel::busy() const { return m_busy; }
QString MixHouseStatusModel::text() const { return m_text; }
int MixHouseStatusModel::percent() const { return m_percent; }
bool MixHouseStatusModel::canStop() const { return m_analysing; }
QString MixHouseStatusModel::updateVersion() const { return m_updateVersion; }
