/*
 * GlideMix
 */
#include "mixhousefolderwatcher.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QFileSystemWatcher>

#include "log.h"

using namespace au::mixhouse;

namespace {
// Quiet time after the last change before a folder is scanned: a download
// or a Finder copy of an album touches the folder many times.
constexpr int kSettleMs = 3000;
// A scan that found files still being written looks again this much later.
constexpr int kPendingRetryMs = 8000;
// One watch per folder; a music tree deeper than this is still rescanned at
// start-up, its folders past the limit just aren't watched live.
constexpr int kMaxWatchedDirs = 4000;
}

MixHouseFolderWatcher::MixHouseFolderWatcher(const muse::modularity::ContextPtr& iocCtx)
    : muse::Contextable(iocCtx)
{
    m_settle.setSingleShot(true);
    QObject::connect(&m_settle, &QTimer::timeout, this, [this]() { startScan(); });
}

MixHouseFolderWatcher::~MixHouseFolderWatcher() = default;

void MixHouseFolderWatcher::init()
{
    if (!library() || !libraryConfiguration()) {
        return;
    }
    rewatch();
    m_started = true;
    libraryConfiguration()->settingsChanged().onNotify(this, [this]() { rewatch(); });

    library()->scanFinished().onReceive(this, [this](const IMixHouseLibrary::ScanResult& result) {
        if (result.watch) {
            if (result.pending > 0) {
                for (const QString& dir : std::as_const(m_scanning)) {
                    m_changed.insert(dir);
                }
                m_settle.start(kPendingRetryMs);
            }
            m_scanning.clear();
        }
        if (!result.addedIds.isEmpty() && libraryConfiguration()->analyzeNewTracks()) {
            analyzeNew(result.addedIds);
        }
        // Changes that came in while another scan was running.
        if (!m_changed.isEmpty() && !m_settle.isActive()) {
            m_settle.start(kSettleMs);
        }
    });
    library()->analysisFinished().onReceive(this, [this](const IMixHouseLibrary::AnalysisSummary&) {
        // After the library has finished tearing the last batch down.
        QTimer::singleShot(0, this, [this]() { startQueuedAnalysis(); });
    });
}

void MixHouseFolderWatcher::rewatch()
{
    QStringList roots;
    if (libraryConfiguration()->watchMusicDirectories()) {
        for (const QString& dir : libraryConfiguration()->musicDirectories()) {
            if (QFileInfo(dir).isDir()) {
                roots << QDir::cleanPath(dir);
            }
        }
    }
    if (roots == m_roots) {
        return;
    }
    // A folder just added in Preferences: its songs come in now, not at the
    // next start (Add there only remembers the folder). The folders known at
    // start-up are the startup rescan's job.
    QStringList added;
    if (m_started) {
        for (const QString& root : std::as_const(roots)) {
            if (!m_roots.contains(root)) {
                added << root;
            }
        }
    }
    m_roots = roots;
    m_watchedDirs.clear();
    m_changed.clear();
    m_settle.stop();
    m_watcher.reset();
    if (m_roots.isEmpty()) {
        return;
    }
    m_watcher = std::make_unique<QFileSystemWatcher>();
    QObject::connect(m_watcher.get(), &QFileSystemWatcher::directoryChanged, this,
                     [this](const QString& dir) { onDirectoryChanged(dir); });
    for (const QString& root : std::as_const(m_roots)) {
        watchTree(root);
    }
    LOGI() << "GlideMix watch folders: " << m_roots.size() << " folder(s), " << m_watchedDirs.size() << " watched";
    for (const QString& root : std::as_const(added)) {
        m_changed.insert(root);
    }
    if (!added.isEmpty()) {
        m_settle.start(kSettleMs);
    }
}

void MixHouseFolderWatcher::watchTree(const QString& dir)
{
    QStringList fresh;
    const auto add = [&](const QString& path) {
        if (m_watchedDirs.size() + fresh.size() >= kMaxWatchedDirs || m_watchedDirs.contains(path)) {
            return;
        }
        fresh << path;
    };
    add(QDir::cleanPath(dir));
    QDirIterator it(dir, QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (it.hasNext() && m_watchedDirs.size() + fresh.size() < kMaxWatchedDirs) {
        add(QDir::cleanPath(it.next()));
    }
    if (fresh.isEmpty()) {
        return;
    }
    m_watcher->addPaths(fresh);
    for (const QString& path : std::as_const(fresh)) {
        m_watchedDirs.insert(path);
    }
}

void MixHouseFolderWatcher::onDirectoryChanged(const QString& dir)
{
    const QString path = QDir::cleanPath(dir);
    if (!QFileInfo(path).isDir()) {
        // Deleted or moved away: the watcher dropped it already.
        m_watchedDirs.remove(path);
        return;
    }
    m_changed.insert(path);
    m_settle.start(kSettleMs);
}

void MixHouseFolderWatcher::startScan()
{
    if (m_changed.isEmpty() || !m_watcher) {
        return;
    }
    if (library()->isScanning()) {
        return;   // scanFinished starts the timer again
    }
    // A folder inside another changed folder is covered by that one's scan.
    QStringList dirs = QStringList(m_changed.begin(), m_changed.end());
    dirs.sort();
    QStringList scan;
    for (const QString& dir : std::as_const(dirs)) {
        if (!scan.isEmpty() && dir.startsWith(scan.last() + QLatin1Char('/'))) {
            continue;
        }
        scan << dir;
    }
    m_changed.clear();
    // New subfolders (an album copied in) are watched from now on.
    for (const QString& dir : std::as_const(scan)) {
        watchTree(dir);
    }
    m_scanning = scan;
    library()->startBackgroundScan(scan, true);
}

void MixHouseFolderWatcher::analyzeNew(const QList<int>& trackIds)
{
    for (const int id : trackIds) {
        if (!m_analysisQueue.contains(id)) {
            m_analysisQueue << id;
        }
    }
    startQueuedAnalysis();
}

void MixHouseFolderWatcher::startQueuedAnalysis()
{
    if (m_analysisQueue.isEmpty() || library()->isAnalyzing()) {
        return;
    }
    const QList<int> ids = m_analysisQueue;
    m_analysisQueue.clear();
    LOGI() << "GlideMix: analysing " << ids.size() << " new song(s)";
    library()->startBackgroundAnalysis(ids);
}
