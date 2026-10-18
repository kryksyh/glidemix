/*
 * MixHouse DJ 2
 */
#include "mixhouselibraryscanner.h"

#include <QDirIterator>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

#include "mixhouse/mixhouseaudiosource.h"
#include "mixhouse/mixhousefiletags.h"
#include "log.h"

namespace au::mixhouse {
// Deliberately its OWN constant, not a reuse of mixhouselibrary.cpp's own
// file-scope `static const QStringList kAudioExtensions` -- this module's
// CMake build concatenates every .cpp under src/mixhouse/ into one
// translation unit (see mixhouselibrary.h's own sqlInClause() comment for
// why that convention exists), and two `static` file-scope constants with
// the identical name in the same TU is a real redefinition risk, not a
// hypothetical one -- safer to duplicate this one short list than depend on
// which .cpp happens to come first in the unity build.
static const QStringList kScannerAudioExtensions = {
    QStringLiteral("mp3"), QStringLiteral("m4a"), QStringLiteral("aac"),
    QStringLiteral("flac"), QStringLiteral("ogg"), QStringLiteral("opus"),
    QStringLiteral("wav"), QStringLiteral("aiff"), QStringLiteral("aif"),
    QStringLiteral("wv")
};

MixHouseLibraryScanner::MixHouseLibraryScanner(QString libraryPath, QStringList directories, QString connectionName)
    : m_libraryPath(std::move(libraryPath)), m_directories(std::move(directories)),
    m_connectionName(std::move(connectionName))
{
}

void MixHouseLibraryScanner::run()
{
    // Runs here, on this object's OWN thread (see this class's own header
    // comment for why) -- opening the connection here, not in the
    // constructor (which still runs on whichever thread created this
    // object, before moveToThread()), is what makes it usable at all: Qt's
    // SQL module requires a QSqlDatabase be both created AND used from the
    // same thread.
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    db.setDatabaseName(m_libraryPath);
    if (!db.open()) {
        LOGE() << "background scan: could not open " << m_libraryPath << ": " << db.lastError().text();
        const QString error = db.lastError().text();
        db = QSqlDatabase();
        QSqlDatabase::removeDatabase(m_connectionName);
        emit scanFinished(0, 0, 0, 0, error);
        return;
    }
    // Same reasoning as mixhouselibrary.cpp's own ensureWritable() index
    // fix: a second connection to the same file writing concurrently with
    // the main thread's own m_writeDb is a real, expected situation here
    // (that's the whole point of moving this off the UI thread), and
    // SQLite's default behavior on a locked file is to fail the query
    // immediately (SQLITE_BUSY) rather than wait. A short busy timeout lets
    // the two connections take turns instead of one of them just failing.
    QSqlQuery(db).exec(QStringLiteral("PRAGMA busy_timeout=5000"));

    int scanned = 0, added = 0, skipped = 0, unreadable = 0;
    QString error;

    // Same INSERT-only scan addTracksFromFolder() runs, deliberately
    // duplicated here rather than shared -- that function is tied to
    // MixHouseLibrary's own m_writeDb member and ensureWritable()/
    // backupOnce() session state, and refactoring already-verified,
    // shipped code to take an arbitrary QSqlDatabase& was a bigger, riskier
    // change than re-stating these dozen lines against this scanner's own
    // connection.
    QSqlQuery findLoc(db);
    findLoc.prepare(QStringLiteral(
        "SELECT l.id, l.duration FROM library l JOIN track_locations t "
        "ON t.id = l.location WHERE t.location = :loc"));
    QSqlQuery repairDuration(db);
    repairDuration.prepare(QStringLiteral(
        "UPDATE library SET duration = :duration WHERE id = :id"));
    QSqlQuery insLoc(db);
    insLoc.prepare(QStringLiteral(
        "INSERT INTO track_locations (location, directory, filename, filesize, "
        "fs_deleted, needs_verification) "
        "VALUES (:location, :directory, :filename, :filesize, 0, 0)"));
    QSqlQuery insLib(db);
    insLib.prepare(QStringLiteral(
        "INSERT INTO library (title, artist, album, genre, year, filetype, location, header_parsed, "
        "mixxx_deleted, played, timesplayed, rating, bpm, duration, datetime_added) "
        "VALUES (:title, :artist, :album, :genre, :year, :filetype, :location, :parsed, 0, 0, 0, 0, 0, :duration, CURRENT_TIMESTAMP)"));

    db.transaction();
    for (const QString& dir : std::as_const(m_directories)) {
        if (m_canceled.load()) {
            break;
        }
        QDirIterator it(dir, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            if (m_canceled.load()) {
                break;
            }
            const QString path = it.next();
            const QFileInfo fi(path);
            if (!kScannerAudioExtensions.contains(fi.suffix().toLower())) {
                continue;
            }
            ++scanned;
            if ((scanned % 100) == 0) {
                emit scanProgress(scanned, added);
            }

            findLoc.bindValue(QStringLiteral(":loc"), path);
            if (findLoc.exec() && findLoc.next()) {
                // A prior version inserted every newly discovered row with
                // duration 0 and then forgot to persist the duration during
                // analysis. Repair only those legacy/unknown rows while we
                // are already walking the directory; do not overwrite a
                // nonzero duration supplied by Mixxx or another importer.
                const QVariant oldDuration = findLoc.value(1);
                if (oldDuration.isNull() || oldDuration.toDouble() <= 0.0) {
                    const AudioInfo info = probeAudioFile(path);
                    if (info.valid && info.durationSec > 0.0) {
                        repairDuration.bindValue(QStringLiteral(":duration"), info.durationSec);
                        repairDuration.bindValue(QStringLiteral(":id"), findLoc.value(0));
                        if (!repairDuration.exec()) {
                            LOGW() << "background scan: could not repair duration for " << path << ": "
                                   << repairDuration.lastError().text();
                        }
                    }
                }
                ++skipped;
                continue;
            }

            // GlideMix: not readable audio (damaged, empty...) -- not added.
            const AudioInfo info = probeAudioFile(path);
            if (!info.valid) {
                ++unreadable;
                LOGW() << "background scan: not readable audio, not added: " << path << ": " << info.error;
                continue;
            }
            insLoc.bindValue(QStringLiteral(":location"), path);
            insLoc.bindValue(QStringLiteral(":directory"), fi.absolutePath());
            insLoc.bindValue(QStringLiteral(":filename"), fi.fileName());
            insLoc.bindValue(QStringLiteral(":filesize"), fi.size());
            if (!insLoc.exec()) {
                LOGW() << "background scan: track_locations insert failed for " << path << ": "
                       << insLoc.lastError().text();
                continue;
            }
            const int locId = insLoc.lastInsertId().toInt();

            {
            // GlideMix: the file's tags (Artist, Title...), the file name only
            // when it has none -- the stream and the library showed file names.
            const MixHouseFileTags tags = readFileTags(path);
            insLib.bindValue(QStringLiteral(":title"), tags.title);
            insLib.bindValue(QStringLiteral(":artist"), tags.artist);
            insLib.bindValue(QStringLiteral(":album"), tags.album);
            insLib.bindValue(QStringLiteral(":genre"), tags.genre);
            insLib.bindValue(QStringLiteral(":year"), tags.year);
            insLib.bindValue(QStringLiteral(":parsed"), tags.fromTags ? 1 : 0);
        }
            insLib.bindValue(QStringLiteral(":filetype"), fi.suffix().toLower());
            insLib.bindValue(QStringLiteral(":location"), locId);
            // Duration is lightweight header data, not an analysis result.
            // Populate it during scanning so a newly added track never sits
            // in the table as the misleading 0:00 until the user runs the
            // much more expensive BPM/key analysis.
            insLib.bindValue(QStringLiteral(":duration"),
                             info.valid && info.durationSec > 0.0 ? QVariant(info.durationSec) : QVariant());
            if (!insLib.exec()) {
                LOGW() << "background scan: library insert failed for " << path << ": "
                       << insLib.lastError().text();
                continue;
            }
            ++added;
        }
    }

    if (!db.commit()) {
        db.rollback();
        error = db.lastError().text();
    }

    // Must happen on this same thread, before the QThread this object lives
    // on is asked to quit -- a QSqlDatabase's underlying driver is tied to
    // the thread that opened it. Cleared BEFORE emitting scanFinished: the
    // finished handler on the main thread may turn straight around and
    // start a new scan (a fresh connection name), and this one should
    // already be fully torn down by then.
    db = QSqlDatabase();
    QSqlDatabase::removeDatabase(m_connectionName);
    emit scanFinished(scanned, added, skipped, unreadable, error);
}
}
