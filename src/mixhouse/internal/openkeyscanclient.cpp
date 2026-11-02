/*
 * MixHouse DJ 2
 */
#include "openkeyscanclient.h"
#include "mixhouse/mixhousenativepath.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QEventLoop>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

#include <algorithm>
#include <array>
#include <chrono>
#include <future>
#include <optional>
#include <utility>

#include <sndfile.h>
#include <vector>

#include <QTemporaryDir>

#include "log.h"
#include "mixhouse/mixhouseaudiosource.h"

namespace au::mixhouse {

namespace {

// GlideMix: where the key analyzer helper can be. The first that exists wins:
//  1. $GLIDEMIX_KEY_ANALYZER -- an executable, or a .py run with python3.
//  2. Bundled in the app: Contents/Resources/openkeyscan-analyzer/ -- the
//     PyInstaller build (that project's build-mac-arm64.sh), no Python needed.
//  3. Installed as an add-on: ~/Library/Application Support/Glidemix/Helpers/
//     openkeyscan-analyzer/ -- the same build, installed separately.
//  4. A developer checkout: ~/Developer/openkeyscan-analyzer with its .venv
//     (development builds only).
//  Linux: bundled as usr/lib/openkeyscan-analyzer/ (AppImage), or the add-on
//     in ~/.local/share/Glidemix/Helpers/openkeyscan-analyzer/.
// None found: key detection is off (keys stay empty; BPM, grid and waveform
// are still analysed), logged once rather than tried again for every track.
struct AnalyzerLaunch {
    QString program;
    QStringList args;
    QString workingDir;
};

std::optional<AnalyzerLaunch> findKeyAnalyzer()
{
    const auto executable = [](const QString& path) {
        const QFileInfo info(path);
        return info.isFile() && info.isExecutable();
    };
    const QString env = qEnvironmentVariable("GLIDEMIX_KEY_ANALYZER");
    if (!env.isEmpty() && QFileInfo::exists(env)) {
        if (env.endsWith(QStringLiteral(".py"))) {
            return AnalyzerLaunch { QStringLiteral("python3"), { env }, QFileInfo(env).absolutePath() };
        }
        return AnalyzerLaunch { env, {}, QFileInfo(env).absolutePath() };
    }
    const QString bundled = QCoreApplication::applicationDirPath()
                            + QStringLiteral("/../Resources/openkeyscan-analyzer/openkeyscan-analyzer");
    if (executable(bundled)) {
        return AnalyzerLaunch { QFileInfo(bundled).canonicalFilePath(), {}, QFileInfo(bundled).canonicalPath() };
    }
    const QString addOn = QDir::homePath()
                          + QStringLiteral("/Library/Application Support/Glidemix/Helpers/openkeyscan-analyzer/openkeyscan-analyzer");
    if (executable(addOn)) {
        return AnalyzerLaunch { addOn, {}, QFileInfo(addOn).absolutePath() };
    }
#ifdef Q_OS_LINUX
    //! Linux: bundled in the AppImage (usr/bin/.. -> usr/lib), or installed
    //! as an add-on in the user's data folder.
    for (const QString& linuxPath : {
            QCoreApplication::applicationDirPath() + QStringLiteral("/../lib/openkeyscan-analyzer/openkeyscan-analyzer"),
            QDir::homePath() + QStringLiteral("/.local/share/Glidemix/Helpers/openkeyscan-analyzer/openkeyscan-analyzer") }) {
        if (executable(linuxPath)) {
            return AnalyzerLaunch { QFileInfo(linuxPath).canonicalFilePath(), {}, QFileInfo(linuxPath).canonicalPath() };
        }
    }
#endif
#ifdef GLIDEMIX_DEV_HELPERS
    const QString dev = QDir::homePath() + QStringLiteral("/Developer/openkeyscan-analyzer");
    const QString devPython = dev + QStringLiteral("/.venv/bin/python3");
    const QString devScript = dev + QStringLiteral("/openkeyscan_analyzer_server.py");
    if (executable(devPython) && QFileInfo::exists(devScript)) {
        return AnalyzerLaunch { devPython, { devScript }, dev };
    }
#endif
    return std::nullopt;
}

// class_id (0-23) -> Mixxx's real ChromaticKey enum value, derived from
// OpenKeyScan's own dataset.py CAMELOT_MAPPING (class_id 0-11 = Camelot
// 1A-12A minor, 12-23 = Camelot 1B-12B major -- the real, standard Camelot
// wheel) and cross-checked entry-by-entry against this app's OWN already-
// verified kOpenKeyNumberByChromaticKey table (mixhousekeywheel.cpp) rather
// than trusted blind: e.g. class_id 7 = "A minor" -> keyId 22 (A_MINOR);
// mixhousekeywheel.cpp's table independently gives OpenKey number 1 for
// keyId 22, which is exactly Traktor's real "1m" = A minor. Several other
// entries were spot-checked the same way before this table was trusted.
constexpr std::array<int, 24> kChromaticKeyByOpenKeyScanClass = { {
    // 0-11: minor keys, Camelot 1A..12A (Ab, Eb, Bb, F, C, G, D, A, E, B, F#, C#)
    21, 16, 23, 18, 13, 20, 15, 22, 17, 24, 19, 14,
    // 12-23: major keys, Camelot 1B..12B (B, F#, C#, G#, Eb, Bb, F, C, G, D, A, E)
    12, 7, 2, 9, 4, 11, 6, 1, 8, 3, 10, 5,
} };

int chromaticKeyForOpenKeyScanClass(int classId)
{
    if (classId < 0 || classId >= static_cast<int>(kChromaticKeyByOpenKeyScanClass.size())) {
        return 0;
    }
    return kChromaticKeyByOpenKeyScanClass[static_cast<size_t>(classId)];
}

}  // namespace

// ---------------------------------------------------------------------------
// The real subprocess host -- owns the QProcess and lives on its own
// dedicated QThread (started once by OpenKeyScanClient's constructor and
// kept for the app's lifetime). analyzeSync() is invoked from OTHER threads
// via Qt::BlockingQueuedConnection, which runs it on THIS thread while
// blocking the CALLING thread -- so multiple analyzer worker threads can
// each have one call in flight, safely serialized/interleaved by Qt's own
// event loop rather than by hand-rolled locking.
//
// A plain top-level class in this namespace, NOT a nested class of
// OpenKeyScanClient -- moc does not reliably process a Q_OBJECT class
// defined out-of-line via the `Outer::Inner` qualified-name syntax, only
// classes that are either nested INLINE in their enclosing class body, or
// (as here) ordinary top-level/namespace-scope classes processed via the
// `#include "openkeyscanclient.moc"` at the end of this file.
class OpenKeyScanHost : public QObject
{
    Q_OBJECT

public:
    explicit OpenKeyScanHost(QObject* parent = nullptr)
        : QObject(parent) {}

    ~OpenKeyScanHost() override
    {
        if (m_process) {
            m_process->kill();
            m_process->waitForFinished(2000);
        }
    }

    Q_INVOKABLE OpenKeyScanResult analyzeSync(const QString& filePath, int timeoutMs)
    {
        OpenKeyScanResult failure;

        QString startError;
        if (!ensureStarted(&startError)) {
            failure.error = startError;
            return failure;
        }

        const QString requestId = QString::number(m_nextId++);

        QEventLoop loop;
        PendingRequest pending;
        pending.loop = &loop;
        m_pendingRequests.insert(requestId, &pending);

        QTimer timeoutTimer;
        timeoutTimer.setSingleShot(true);
        connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);
        timeoutTimer.start(timeoutMs);

        QJsonObject request;
        request[QStringLiteral("id")] = requestId;
        request[QStringLiteral("path")] = filePath;
        const QByteArray line = QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n';
        m_process->write(line);

        loop.exec();

        m_pendingRequests.remove(requestId);

        if (!pending.done) {
            failure.error = QStringLiteral("OpenKeyScan request timed out or process stopped responding");
            return failure;
        }

        const QJsonObject& response = pending.response;
        if (response.value(QStringLiteral("status")).toString() != QLatin1String("success")) {
            failure.error = response.value(QStringLiteral("error")).toString(
                QStringLiteral("OpenKeyScan analysis failed"));
            return failure;
        }

        OpenKeyScanResult result;
        result.valid = true;
        result.camelot = response.value(QStringLiteral("camelot")).toString();
        result.keyName = response.value(QStringLiteral("key")).toString();
        result.keyId = chromaticKeyForOpenKeyScanClass(response.value(QStringLiteral("class_id")).toInt(-1));
        return result;
    }

private slots:
    void onReadyReadStandardOutput()
    {
        m_pendingBytes += m_process->readAllStandardOutput();
        int newlineIndex;
        while ((newlineIndex = m_pendingBytes.indexOf('\n')) >= 0) {
            const QByteArray line = m_pendingBytes.left(newlineIndex).trimmed();
            m_pendingBytes.remove(0, newlineIndex + 1);
            if (line.isEmpty()) {
                continue;
            }
            handleLine(line);
        }
    }

    void onErrorOccurred(QProcess::ProcessError)
    {
        failAllPending(QStringLiteral("OpenKeyScan process error: %1").arg(m_process->errorString()));
        m_ready = false;
    }

    void onFinished(int, QProcess::ExitStatus)
    {
        failAllPending(QStringLiteral("OpenKeyScan process exited unexpectedly"));
        m_ready = false;
    }

private:
    struct PendingRequest {
        QEventLoop* loop = nullptr;
        QJsonObject response;
        bool done = false;
    };

    void handleLine(const QByteArray& line)
    {
        const QJsonDocument doc = QJsonDocument::fromJson(line);
        if (!doc.isObject()) {
            return;
        }
        const QJsonObject obj = doc.object();

        const QString type = obj.value(QStringLiteral("type")).toString();
        if (type == QLatin1String("ready")) {
            m_ready = true;
            return;
        }
        if (type == QLatin1String("heartbeat")) {
            return;
        }

        const QString id = obj.value(QStringLiteral("id")).toString();
        if (id.isEmpty()) {
            return;
        }
        PendingRequest* pending = m_pendingRequests.value(id, nullptr);
        if (!pending) {
            return;   // already timed out, or an id we never sent
        }
        pending->response = obj;
        pending->done = true;
        pending->loop->quit();
    }

    void failAllPending(const QString& error)
    {
        QJsonObject errorResponse;
        errorResponse[QStringLiteral("status")] = QStringLiteral("error");
        errorResponse[QStringLiteral("error")] = error;
        for (PendingRequest* pending : std::as_const(m_pendingRequests)) {
            pending->response = errorResponse;
            pending->done = true;
            pending->loop->quit();
        }
    }

    bool ensureStarted(QString* error)
    {
        if (m_process && m_ready) {
            return true;
        }
        if (m_startAttempted && m_process && m_process->state() != QProcess::NotRunning) {
            // A previous call already kicked off startup; give the current
            // caller a real chance to see it become ready rather than
            // failing immediately just because it isn't ready THIS instant.
            QEventLoop waitLoop;
            QTimer readyPoll;
            connect(&readyPoll, &QTimer::timeout, &waitLoop, [this, &waitLoop]() {
                if (m_ready) {
                    waitLoop.quit();
                }
            });
            readyPoll.start(50);
            QTimer giveUp;
            giveUp.setSingleShot(true);
            connect(&giveUp, &QTimer::timeout, &waitLoop, &QEventLoop::quit);
            giveUp.start(15000);
            waitLoop.exec();
            if (!m_ready) {
                *error = QStringLiteral("OpenKeyScan process did not become ready in time");
                return false;
            }
            return true;
        }

        if (m_notInstalled) {
            *error = QStringLiteral("the key analyzer is not installed");
            return false;
        }
        const std::optional<AnalyzerLaunch> launch = findKeyAnalyzer();
        if (!launch) {
            m_notInstalled = true;
            LOGW() << "GlideMix: key analyzer not found (bundled, add-on or developer checkout); keys will not be detected";
            *error = QStringLiteral("the key analyzer is not installed");
            return false;
        }
        LOGI() << "GlideMix: key analyzer " << launch->program;

        m_startAttempted = true;
        m_process = new QProcess(this);
        m_process->setWorkingDirectory(launch->workingDir);
        connect(m_process, &QProcess::readyReadStandardOutput, this, &OpenKeyScanHost::onReadyReadStandardOutput);
        connect(m_process, &QProcess::errorOccurred, this, &OpenKeyScanHost::onErrorOccurred);
        connect(m_process,
                QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, &OpenKeyScanHost::onFinished);

        // --workers keeps memory bounded (~1.1-1.6GB per OpenKeyScan's own
        // INTERFACING.md) even though this app's own analyzer pool can have
        // QThread::idealThreadCount() C++ worker threads calling detectKey()
        // concurrently -- those calls simply queue up on the Python side's
        // own ThreadPoolExecutor rather than each getting a dedicated model
        // instance.
        // GlideMix: no more key workers than the machine has threads to spare
        // (4 on a 2-core laptop starved the audio).
        const int workers = std::clamp(QThread::idealThreadCount() / 2, 1, 4);
        const QStringList args = launch->args + QStringList { QStringLiteral("--workers"), QString::number(workers) };
#ifdef Q_OS_MACOS
        // GlideMix: below the interface, like the analysis threads (its worker
        // processes inherit this).
        m_process->start(QStringLiteral("/usr/bin/nice"), QStringList { QStringLiteral("-n"), QStringLiteral("10"), launch->program } + args);
#elif defined(Q_OS_WIN)
        // Below normal, the Windows counterpart of nice (inherited by its workers).
        m_process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* a) {
            a->flags |= 0x00004000;   // BELOW_NORMAL_PRIORITY_CLASS
        });
        m_process->start(launch->program, args);
#else
        m_process->start(launch->program, args);
#endif

        if (!m_process->waitForStarted(10000)) {
            *error = QStringLiteral("Could not start OpenKeyScan process: %1").arg(m_process->errorString());
            return false;
        }

        // Model load + CQT warmup takes ~1.5-2.5s per OpenKeyScan's own docs.
        QEventLoop waitLoop;
        QTimer readyPoll;
        connect(&readyPoll, &QTimer::timeout, &waitLoop, [this, &waitLoop]() {
            if (m_ready) {
                waitLoop.quit();
            }
        });
        readyPoll.start(50);
        QTimer giveUp;
        giveUp.setSingleShot(true);
        connect(&giveUp, &QTimer::timeout, &waitLoop, &QEventLoop::quit);
        giveUp.start(30000);
        waitLoop.exec();

        if (!m_ready) {
            *error = QStringLiteral("OpenKeyScan process did not send a ready signal in time");
            return false;
        }
        return true;
    }

    QProcess* m_process = nullptr;
    bool m_ready = false;
    bool m_startAttempted = false;
    bool m_notInstalled = false;
    QByteArray m_pendingBytes;
    quint64 m_nextId = 1;
    QHash<QString, PendingRequest*> m_pendingRequests;
};

OpenKeyScanClient& OpenKeyScanClient::instance()
{
    static OpenKeyScanClient s_instance;
    return s_instance;
}

OpenKeyScanClient::OpenKeyScanClient()
{
    m_thread = new QThread();
    m_thread->setObjectName(QStringLiteral("OpenKeyScanClient"));
    m_host = new OpenKeyScanHost();
    m_host->moveToThread(m_thread);
    m_thread->start();

    if (auto* app = QCoreApplication::instance()) {
        // Explicit DirectConnection, and `app` (not m_thread) as the
        // context object: OpenKeyScanClient's singleton can be constructed
        // lazily from whichever thread calls detectKey() first, which may
        // be one of the analyzer pool's own transient QThreads (created and
        // torn down per background-analysis batch) -- if THAT thread's
        // affinity were used for a queued connection, this cleanup could
        // silently never run once its thread is gone. QThread::quit()/
        // wait() are documented safe to call from any thread, so running
        // this synchronously on the main thread (where aboutToQuit is
        // always emitted) avoids the whole problem.
        QObject::connect(app, &QCoreApplication::aboutToQuit, app, [this]() {
            m_stopping = true;
            m_thread->quit();
            m_thread->wait(3000);
        }, Qt::DirectConnection);
    }
}

OpenKeyScanClient::~OpenKeyScanClient()
{
    // In practice this runs (if at all) at static-destruction time, after
    // QCoreApplication -- and therefore the aboutToQuit handler above -- is
    // already gone, so this cannot rely on posting anything to m_thread's
    // event loop (deleteLater() would never be delivered once that loop has
    // stopped). quit()+wait() are safe to call again even if aboutToQuit
    // already stopped the thread; once stopped, deleting m_host directly
    // (not deleteLater) is safe since nothing is left to dispatch events to it.
    m_thread->quit();
    m_thread->wait(3000);
    delete m_host;
    delete m_thread;
}

namespace {
//! What the analyzer opens itself (its own list, "Unsupported format...").
bool analyzerReads(const QString& filePath)
{
    static const QStringList kTypes = { QStringLiteral("aac"), QStringLiteral("aiff"), QStringLiteral("au"),
                                        QStringLiteral("flac"), QStringLiteral("m4a"),
                                        QStringLiteral("mp3"), QStringLiteral("mp4"), QStringLiteral("ogg"),
                                        QStringLiteral("wav") };
    return kTypes.contains(QFileInfo(filePath).suffix().toLower());
}

}

OpenKeyScanResult OpenKeyScanClient::detectKey(const QString& filePath, int timeoutMs)
{
    QString path = filePath;
    QTemporaryDir tempDir;
    if (!analyzerReads(filePath)) {
        const AudioInfo probe = probeAudioFile(filePath);
        if (!probe.valid || probe.channels <= 0 || !tempDir.isValid()) {
            return {};
        }
        path = tempDir.filePath(QStringLiteral("key.wav"));
        SF_INFO sfInfo {};
        sfInfo.samplerate = probe.sampleRate;
        sfInfo.channels = 1;
        sfInfo.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;
        SNDFILE* out = au::mixhouse::openSndfile(path, SFM_WRITE, &sfInfo);
        if (!out) {
            return {};
        }
        const int channels = probe.channels;
        bool ok = true;
        decodeAudioFile(filePath, [&](const float* samples, long long frames) {
            std::vector<short> mono(static_cast<size_t>(frames));
            for (long long f = 0; f < frames; ++f) {
                float sum = 0.0f;
                for (int c = 0; c < channels; ++c) {
                    sum += samples[f * channels + c];
                }
                mono[static_cast<size_t>(f)] = static_cast<short>(std::clamp(sum / channels, -1.0f, 1.0f) * 32767.0f);
            }
            ok = sf_writef_short(out, mono.data(), frames) == frames;
            return ok;
        });
        sf_close(out);
        if (!ok) {
            return {};
        }
    }
    // GlideMix: not a blocking queued call -- if the app quits while this
    // waits, the host thread stops and that call never returned: quitting
    // mid-analysis hung the app for good (the library waits for this thread).
    if (m_stopping) {
        return {};
    }
    auto promise = std::make_shared<std::promise<OpenKeyScanResult> >();
    std::future<OpenKeyScanResult> future = promise->get_future();
    OpenKeyScanHost* host = m_host;
    QMetaObject::invokeMethod(m_host, [host, path, timeoutMs, promise]() {
        promise->set_value(host->analyzeSync(path, timeoutMs));
    }, Qt::QueuedConnection);
    while (future.wait_for(std::chrono::milliseconds(100)) != std::future_status::ready) {
        if (m_stopping) {
            return {};
        }
    }
    return future.get();
}

}  // namespace au::mixhouse

#include "openkeyscanclient.moc"
