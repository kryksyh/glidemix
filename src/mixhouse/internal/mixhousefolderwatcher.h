/*
 * GlideMix: watch folders. The music directories (Preferences > Music
 * library) are watched while the app runs: a song copied or downloaded into
 * one is added to the Music Collection a few seconds later, and -- when
 * "Analyse new songs automatically" is on -- analysed right away, like
 * Lexicon's and rekordbox's watch folders. Songs added by any other scan
 * (Add Folder, the startup rescan) are analysed the same way.
 */
#pragma once

#include <memory>

#include <QList>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QTimer>

#include "async/asyncable.h"
#include "modularity/ioc.h"

#include "mixhouse/imixhouselibrary.h"
#include "mixhouse/imixhouselibraryconfiguration.h"

class QFileSystemWatcher;

namespace au::mixhouse {
class MixHouseFolderWatcher : public QObject, public muse::Contextable, public muse::async::Asyncable
{
    muse::ContextInject<IMixHouseLibrary> library { this };
    muse::GlobalInject<IMixHouseLibraryConfiguration> libraryConfiguration;

public:
    explicit MixHouseFolderWatcher(const muse::modularity::ContextPtr& iocCtx);
    ~MixHouseFolderWatcher() override;

    void init();

private:
    void rewatch();
    void watchTree(const QString& dir);
    void onDirectoryChanged(const QString& dir);
    void startScan();
    void analyzeNew(const QList<int>& trackIds);
    void startQueuedAnalysis();

    std::unique_ptr<QFileSystemWatcher> m_watcher;
    QStringList m_roots;              ///< the music directories being watched
    QSet<QString> m_watchedDirs;      ///< every folder under them
    QSet<QString> m_changed;          ///< folders to scan once things settle
    QStringList m_scanning;           ///< the folders of the watch scan running now
    QTimer m_settle;
    bool m_started = false;           ///< past the first rewatch() at start-up
    QList<int> m_analysisQueue;       ///< new songs waiting for a running analysis to end
};
}
