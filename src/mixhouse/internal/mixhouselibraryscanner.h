/*
 * MixHouse DJ 2
 */
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <atomic>

namespace au::mixhouse {
/// Real Mixxx: library/scanner/libraryscanner.h -- a QThread-based scanner
/// with its own DAOs and its own DB connection, reporting progress back
/// over signals instead of blocking whichever thread asked for a scan. This
/// is a faithful-but-simplified port of that SAME core idea (dedicated
/// thread, dedicated connection, plain Qt signals for progress) -- NOT a
/// port of mixxx's full scanner machinery (LibraryHashes directory-hash
/// skip-unchanged-dirs optimization, the ScannerTask/
/// RecursiveScanDirectoryTask class hierarchy, DbConnectionPool pooling).
/// See IMixHouseLibrary::startBackgroundScan's own comment for the real bug
/// this exists to fix and why that extra machinery isn't needed here.
///
/// Communicates back to MixHouseLibrary via plain Qt signals, deliberately
/// -- NOT muse::async::Channel/Notification. Confirmed by reading
/// kors_async's own source (muse/framework/global/thirdparty/kors_async):
/// it has no Qt event-loop integration anywhere in the real (non-example)
/// implementation, so nothing pumps a cross-thread delivery on the
/// receiving thread automatically the way Qt's own queued connections do.
/// This app has already crashed once this session from an unsafe
/// cross-thread path (see MixHouseLibraryPanel.qml's own cover-art Image
/// comment: the image provider called straight into a QSqlDatabase/
/// QSqlQuery opened on a different thread, EXC_BAD_ACCESS, confirmed from
/// real .ips crash reports). A real Qt signal/slot connection between
/// threads is Qt's own guaranteed-safe mechanism (queued automatically
/// whenever sender and receiver live on different threads) -- also exactly
/// what real mixxx's own LibraryScanner uses for this same job.
///
/// Lives entirely on its own worker thread once MixHouseLibrary::
/// startBackgroundScan() calls QObject::moveToThread() on it and starts the
/// thread -- run() is invoked via QThread::started, itself a cross-thread
/// signal Qt queues automatically, so everything from run() onward
/// (including opening its own QSqlDatabase connection) genuinely executes
/// there. Qt's SQL module is not thread-safe to share a single QSqlDatabase
/// across threads -- each thread that runs queries needs its own
/// connection to the same file, which is exactly why this can't reuse
/// MixHouseLibrary's own m_writeDb, and exactly the mistake the cover-art
/// crash above made.
class MixHouseLibraryScanner : public QObject
{
    Q_OBJECT
public:
    MixHouseLibraryScanner(QString libraryPath, QStringList directories, QString connectionName);

signals:
    void scanProgress(int scanned, int added);
    // Plain ints/QString, not IMixHouseLibrary::ScanResult -- a queued
    // cross-thread signal needs its argument types registered with Qt's
    // meta-object system (Q_DECLARE_METATYPE/qRegisterMetaType); this app's
    // own ScanResult has never needed that anywhere else, so passing its
    // three ints and one QString as separate plain arguments (types Qt
    // already knows) avoids adding that registration for one call site.
    void scanFinished(int scanned, int added, int skipped, int unreadable, const QString& error);

public slots:
    void run();

    // Real mixxx: LibraryScanner::slotCancel(). Safe to call from any
    // thread (std::atomic, checked between files in run()'s own loop) --
    // used so the app can quit promptly instead of blocking on
    // QThread::wait() for an arbitrarily long scan to finish naturally (see
    // MixHouseLibrary's own destructor).
    void cancel() { m_canceled.store(true); }

private:
    QString m_libraryPath;
    QStringList m_directories;
    QString m_connectionName;
    std::atomic<bool> m_canceled{ false };
};
}
