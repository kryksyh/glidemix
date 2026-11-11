/*
 * GlideMix Samples: see imixhousesamples.h.
 *
 * One scan thread at a time, with its own database connection; the main
 * thread reads the result once the scan ends (and every few thousand files
 * while a long first scan runs, so the list fills up as it goes).
 */
#include "mixhousesamples.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include "log.h"
#include "mixhouse/glidemixpaths.h"
#include "mixhouse/mixhouseaudiosource.h"
#include "mixhouse/mixhousesamplename.h"

using namespace au::mixhouse;

namespace {
const char* kMainConnection = "glidemix_samples_main";
const char* kScanConnection = "glidemix_samples_scan";

const char* kSchema[] = {
    "CREATE TABLE IF NOT EXISTS sample_folders (path TEXT PRIMARY KEY, added INTEGER NOT NULL)",
    "CREATE TABLE IF NOT EXISTS samples ("
    " id INTEGER PRIMARY KEY AUTOINCREMENT,"
    " path TEXT NOT NULL UNIQUE,"
    " folder TEXT NOT NULL,"
    " location TEXT, category TEXT, pack TEXT, name TEXT, format TEXT,"
    " size INTEGER, mtime INTEGER,"
    " duration REAL, bpm REAL, key_id INTEGER, loop INTEGER, bars INTEGER,"
    " added INTEGER NOT NULL, rules INTEGER NOT NULL DEFAULT 0)",
    "CREATE INDEX IF NOT EXISTS samples_folder ON samples(folder)",
};

bool isAudioFile(const QString& suffix)
{
    static const QSet<QString> audio = { QStringLiteral("wav"), QStringLiteral("wave"), QStringLiteral("aif"),
                                         QStringLiteral("aiff"), QStringLiteral("mp3"), QStringLiteral("flac"),
                                         QStringLiteral("ogg"), QStringLiteral("m4a") };
    return audio.contains(suffix.toLower());
}

bool openDb(QSqlDatabase& db, const QString& connection, const QString& path)
{
    db = QSqlDatabase::contains(connection) ? QSqlDatabase::database(connection, false)
                                            : QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
    db.setDatabaseName(path);
    if (!db.open()) {
        LOGW() << "GlideMix samples: cannot open " << path << ": " << db.lastError().text();
        return false;
    }
    QSqlQuery(db).exec(QStringLiteral("PRAGMA busy_timeout=5000"));
    QSqlQuery(db).exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    for (const char* sql : kSchema) {
        QSqlQuery q(db);
        if (!q.exec(QString::fromLatin1(sql))) {
            LOGW() << "GlideMix samples: " << q.lastError().text();
        }
    }
    return true;
}

// Runs on the scan thread. `progress(done, found)` and `partial()` are
// called from this thread; they post to the main thread themselves.
void scanFolders(const QStringList& folders, const QString& dbPath, const std::shared_ptr<std::atomic<bool> >& stop,
                 const std::function<void(int, int)>& progress, const std::function<void()>& partial)
{
    QElapsedTimer took;
    took.start();
    int done = 0, found = 0, read = 0, dropped = 0;
    {
        QSqlDatabase db;
        if (!openDb(db, QString::fromLatin1(kScanConnection), dbPath)) {
            return;
        }
        QSqlQuery upsert(db);
        upsert.prepare(QStringLiteral(
            "INSERT INTO samples (path, folder, location, category, pack, name, format, size, mtime, duration, bpm,"
            " key_id, loop, bars, added, rules) VALUES (:path, :folder, :location, :category, :pack, :name, :format,"
            " :size, :mtime, :duration, :bpm, :key, :loop, :bars, :added, :rules)"
            " ON CONFLICT(path) DO UPDATE SET folder = excluded.folder, location = excluded.location,"
            " category = excluded.category, pack = excluded.pack, name = excluded.name, format = excluded.format,"
            " size = excluded.size, mtime = excluded.mtime, duration = excluded.duration, bpm = excluded.bpm,"
            " key_id = excluded.key_id, loop = excluded.loop, bars = excluded.bars, rules = excluded.rules"));
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        for (const QString& folder : folders) {
            if (*stop) {
                break;
            }
            if (!QFileInfo(folder).isDir()) {
                LOGI() << "GlideMix samples: " << folder << " is not connected; its samples are kept";
                continue;
            }
            // What the database already knows about this folder.
            struct Known {
                qint64 size = 0, mtime = 0;
                double duration = 0.0;
                int rules = 0;
            };
            QHash<QString, Known> known;
            {
                QSqlQuery q(db);
                q.setForwardOnly(true);
                q.prepare(QStringLiteral("SELECT path, size, mtime, duration, rules FROM samples WHERE folder = :folder"));
                q.bindValue(QStringLiteral(":folder"), folder);
                if (q.exec()) {
                    while (q.next()) {
                        known.insert(q.value(0).toString(), { q.value(1).toLongLong(), q.value(2).toLongLong(),
                                                              q.value(3).toDouble(), q.value(4).toInt() });
                    }
                }
            }
            QSet<QString> seen;
            const QDir root(folder);
            db.transaction();
            int inTransaction = 0;
            QDirIterator it(folder, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
            while (it.hasNext() && !*stop) {
                it.next();
                const QFileInfo fi = it.fileInfo();
                if (fi.fileName().startsWith(QLatin1Char('.')) || !isAudioFile(fi.suffix())) {
                    continue;   // hidden, "._" resource forks, MIDI, Ableton files...
                }
                ++found;
                const QString path = fi.absoluteFilePath();
                seen.insert(path);
                const qint64 size = fi.size();
                const qint64 mtime = fi.lastModified().toSecsSinceEpoch();
                const auto k = known.constFind(path);
                const bool unchanged = k != known.constEnd() && k->size == size && k->mtime == mtime;
                if (unchanged && k->rules >= kSampleNameRulesVersion) {
                    ++done;
                } else {
                    const QString relative = root.relativeFilePath(path);
                    SampleNameInfo info = parseSampleName(relative);
                    // An unchanged file under newer name rules: its name only.
                    const AudioInfo audio = unchanged ? AudioInfo {} : probeAudioFile(path);
                    const double duration = unchanged ? k->duration : (audio.valid ? audio.durationSec : 0.0);
                    if (info.type == SampleNameInfo::Type::Unknown) {
                        info.type = info.bpm > 0.0 ? guessTypeFromLength(duration, info.bpm)
                                                   : SampleNameInfo::Type::OneShot;
                    }
                    const bool loop = info.type == SampleNameInfo::Type::Loop;
                    const int slash = relative.lastIndexOf(QLatin1Char('/'));
                    upsert.bindValue(QStringLiteral(":path"), path);
                    upsert.bindValue(QStringLiteral(":folder"), folder);
                    upsert.bindValue(QStringLiteral(":location"), slash > 0 ? relative.left(slash) : QString());
                    upsert.bindValue(QStringLiteral(":category"), info.category);
                    upsert.bindValue(QStringLiteral(":pack"), info.pack);
                    upsert.bindValue(QStringLiteral(":name"), info.name);
                    upsert.bindValue(QStringLiteral(":format"), fi.suffix().toLower());
                    upsert.bindValue(QStringLiteral(":size"), size);
                    upsert.bindValue(QStringLiteral(":mtime"), mtime);
                    upsert.bindValue(QStringLiteral(":duration"), duration);
                    upsert.bindValue(QStringLiteral(":bpm"), info.bpm);
                    upsert.bindValue(QStringLiteral(":key"), info.keyId);
                    upsert.bindValue(QStringLiteral(":loop"), loop ? 1 : 0);
                    upsert.bindValue(QStringLiteral(":bars"), loop ? loopBars(duration, info.bpm) : 0);
                    upsert.bindValue(QStringLiteral(":added"), now);
                    upsert.bindValue(QStringLiteral(":rules"), kSampleNameRulesVersion);
                    if (!upsert.exec()) {
                        LOGW() << "GlideMix samples: " << upsert.lastError().text();
                    }
                    ++read;
                    ++done;
                    if (++inTransaction >= 500) {
                        db.commit();
                        db.transaction();
                        inTransaction = 0;
                    }
                }
                if (found % 200 == 0) {
                    progress(done, found);
                }
                if (found % 5000 == 0) {
                    db.commit();
                    db.transaction();
                    inTransaction = 0;
                    partial();
                }
            }
            // Files gone from a folder that is connected: drop them (not when stopped half way).
            if (!*stop) {
                QSqlQuery del(db);
                del.prepare(QStringLiteral("DELETE FROM samples WHERE path = :path"));
                for (auto k = known.constBegin(); k != known.constEnd(); ++k) {
                    if (!seen.contains(k.key())) {
                        del.bindValue(QStringLiteral(":path"), k.key());
                        del.exec();
                        ++dropped;
                    }
                }
            }
            db.commit();
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(QString::fromLatin1(kScanConnection));
    progress(done, found);
    LOGI() << "GlideMix samples: scanned " << found << " files (" << read << " read, " << dropped << " gone) in "
           << took.elapsed() / 1000 << " s";
}
}

MixHouseSamples::MixHouseSamples() = default;

MixHouseSamples::~MixHouseSamples()
{
    deinit();
}

QString MixHouseSamples::databasePath()
{
    return paths::glidemixDataPath(QStringLiteral("Library/samples.sqlite"));
}

bool MixHouseSamples::ensureOpen()
{
    if (m_db.isOpen()) {
        return true;
    }
    QDir().mkpath(QFileInfo(databasePath()).absolutePath());
    return openDb(m_db, QString::fromLatin1(kMainConnection), databasePath());
}

void MixHouseSamples::init()
{
    // Nothing is read until the Samples tab asks: most sessions never open it.
}

void MixHouseSamples::deinit()
{
    *m_stop = true;
    if (m_thread) {
        m_thread->wait();
        m_thread = nullptr;
    }
    if (m_db.isOpen()) {
        m_db.close();
    }
    m_db = QSqlDatabase();
    if (QSqlDatabase::contains(QString::fromLatin1(kMainConnection))) {
        QSqlDatabase::removeDatabase(QString::fromLatin1(kMainConnection));
    }
}

void MixHouseSamples::load()
{
    if (m_loaded) {
        return;
    }
    m_loaded = true;
    if (!QFileInfo::exists(databasePath())) {
        return;   // no sample folders yet: no file until one is added
    }
    reload();
    startScan(m_folders);   // new, changed and deleted files since last time
}

int MixHouseSamples::sampleCount() const
{
    if (m_loaded) {
        return static_cast<int>(m_samples.size());
    }
    if (m_countCache < 0) {
        m_countCache = 0;
        // Only an existing database: a user without samples gets no new file.
        if (QFileInfo::exists(databasePath()) && const_cast<MixHouseSamples*>(this)->ensureOpen()) {
            QSqlQuery q(m_db);
            if (q.exec(QStringLiteral("SELECT COUNT(*) FROM samples")) && q.next()) {
                m_countCache = q.value(0).toInt();
            }
        }
    }
    return m_countCache;
}

QStringList MixHouseSamples::folders() const
{
    return m_folders;
}

void MixHouseSamples::reload()
{
    if (!ensureOpen()) {
        return;
    }
    QElapsedTimer took;
    took.start();
    m_folders.clear();
    {
        QSqlQuery q(m_db);
        q.exec(QStringLiteral("SELECT path FROM sample_folders ORDER BY added"));
        while (q.next()) {
            m_folders << q.value(0).toString();
        }
    }
    QHash<QString, bool> connected;
    for (const QString& f : std::as_const(m_folders)) {
        connected.insert(f, QFileInfo(f).isDir());
    }
    std::vector<MixHouseSample> samples;
    QSqlQuery q(m_db);
    q.setForwardOnly(true);
    if (q.exec(QStringLiteral("SELECT id, path, folder, location, category, pack, name, format, duration, bpm, key_id,"
                              " loop, bars, added FROM samples"))) {
        while (q.next()) {
            MixHouseSample s;
            s.id = q.value(0).toInt();
            s.path = q.value(1).toString();
            s.folder = q.value(2).toString();
            if (!connected.contains(s.folder)) {
                continue;   // a folder removed while a scan still wrote it
            }
            s.location = q.value(3).toString();
            s.category = q.value(4).toString();
            s.pack = q.value(5).toString();
            s.name = q.value(6).toString();
            s.format = q.value(7).toString();
            s.durationSec = q.value(8).toDouble();
            s.bpm = q.value(9).toDouble();
            s.keyId = q.value(10).toInt();
            s.loop = q.value(11).toInt() != 0;
            s.bars = q.value(12).toInt();
            s.dateAdded = q.value(13).toLongLong();
            s.missing = !connected.value(s.folder);
            samples.push_back(std::move(s));
        }
    }
    m_samples = std::move(samples);
    LOGI() << "GlideMix samples: " << m_samples.size() << " samples in " << m_folders.size() << " folders, read in "
           << took.elapsed() << " ms";
    m_samplesChanged.notify();
}

void MixHouseSamples::addFolder(const QString& path)
{
    const QString folder = QDir::cleanPath(QDir::fromNativeSeparators(path));
    if (folder.isEmpty() || !ensureOpen()) {
        return;
    }
    m_loaded = true;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT OR IGNORE INTO sample_folders (path, added) VALUES (:path, :added)"));
    q.bindValue(QStringLiteral(":path"), folder);
    q.bindValue(QStringLiteral(":added"), QDateTime::currentSecsSinceEpoch());
    q.exec();
    reload();
    startScan({ folder });
}

void MixHouseSamples::removeFolder(const QString& path)
{
    if (!ensureOpen()) {
        return;
    }
    for (const char* sql : { "DELETE FROM samples WHERE folder = :path", "DELETE FROM sample_folders WHERE path = :path" }) {
        QSqlQuery q(m_db);
        q.prepare(QString::fromLatin1(sql));
        q.bindValue(QStringLiteral(":path"), path);
        q.exec();
    }
    reload();
}

void MixHouseSamples::rescan()
{
    if (!ensureOpen()) {
        return;
    }
    if (m_folders.isEmpty() && m_samples.empty()) {
        reload();
    }
    startScan(m_folders);
}

void MixHouseSamples::startScan(const QStringList& folders)
{
    if (folders.isEmpty()) {
        return;
    }
    if (m_scanning) {
        // One scan at a time: this one follows the running one.
        for (const QString& f : folders) {
            if (!m_pendingFolders.contains(f)) {
                m_pendingFolders << f;
            }
        }
        return;
    }
    m_scanning = true;
    *m_stop = false;
    const auto stop = m_stop;
    const QString dbPath = databasePath();
    auto post = [stop](std::function<void()> f) {
        QMetaObject::invokeMethod(qApp, [stop, f]() {
            if (!*stop) {
                f();
            }
        }, Qt::QueuedConnection);
    };
    m_thread = QThread::create([this, folders, dbPath, stop, post]() {
        scanFolders(folders, dbPath, stop,
                    [this, post](int done, int found) { post([this, done, found]() { m_scanProgress.send(done, found); }); },
                    [this, post]() { post([this]() { reload(); }); });
    });
    QObject::connect(m_thread, &QThread::finished, qApp, [this, stop, thread = m_thread]() {
        thread->deleteLater();
        if (*stop) {
            return;   // shutting down
        }
        m_thread = nullptr;
        m_scanning = false;
        reload();
        if (!m_pendingFolders.isEmpty()) {
            const QStringList next = m_pendingFolders;
            m_pendingFolders.clear();
            startScan(next);
        }
    });
    m_thread->start(QThread::LowPriority);
}
