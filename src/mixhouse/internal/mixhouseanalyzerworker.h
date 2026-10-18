/*
 * MixHouse DJ 2
 */
#pragma once

#include <QObject>
#include <QString>

#include "mixhouse/mixhouseanalyzer.h"

namespace au::mixhouse {
/// One real worker thread's-worth of the background analyzer pool -- see
/// IMixHouseLibrary::startBackgroundAnalysis's own comment for the real
/// mixxx precedent this ports (TrackAnalysisScheduler + AnalyzerThread) and
/// why it exists.
///
/// PURE COMPUTE, deliberately -- no QSqlDatabase, no file path lookup, no
/// writing. The first version of this class owned its own DB connection
/// and wrote its own track's result directly, mirroring the rule
/// MixHouseLibraryScanner established for background scanning; that turned
/// out to be the wrong precedent to copy for THIS job -- measured on a
/// real 1372-track batch, 1240 failed with "database is locked" even with
/// a 5s busy_timeout, because N worker threads all writing to the same
/// SQLite file at once is too much contention for its single-writer model,
/// no matter how long each one waits. Confirmed against real mixxx's own
/// upstream AnalyzerThread before rearchitecting: it deliberately never
/// lets a worker thread trigger a database write either (its own comment:
/// "Release all references of the track before emitting the signal to
/// ensure that the last reference is not dropped in this worker thread
/// that might trigger database actions!"). See
/// mixhouseanalysiswriter.h's own comment for where the actual write lives
/// now (the main thread, one connection, no contention at all) -- this
/// class only ever runs the CPU-bound decode + Queen Mary DSP pass, which
/// is where essentially all the real wall-clock time goes anyway.
///
/// Moved to its own QThread by MixHouseLibrary (startBackgroundAnalysis);
/// analyzeTrack() is only ever invoked cross-thread via a queued
/// QMetaObject::invokeMethod call from the main thread, so it always
/// actually runs ON this worker's own thread, never the main thread that
/// dispatches it.
class MixHouseAnalyzerWorker : public QObject
{
    Q_OBJECT

public:
    MixHouseAnalyzerWorker() = default;

public slots:
    /// filePath/bpmLocked are looked up by the MAIN thread (see
    /// MixHouseLibrary::dispatchNextTrack) BEFORE this is invoked -- this
    /// slot never touches the database, only the real audio file.
    void analyzeTrack(int trackId, const QString& filePath, bool bpmLocked,
                      double knownBpm = 0.0, double knownFirstBeat = 0.0, double knownDownbeat = -1.0,
                      int knownKey = 0, const QString& knownSource = QString());

signals:
    /// analysis.valid is false on any real decode failure -- the caller
    /// (MixHouseLibrary::onAnalyzerWorkerFinished) writes the result (or
    /// counts the failure) and decides what track this worker gets next.
    void analyzed(int trackId, bool bpmLocked, const MixHouseAnalysisResult& analysis);
    /// Per-track progress, 0-95 (mixxx's analyzerProgressPercent() of
    /// kMixHouseAnalyzerProgressNone..Finalizing -- 100 is never sent here,
    /// analyzed() is the "done" signal). Emitted only when the integer
    /// percent actually changes, at most every 60ms (real mixxx's own
    /// AnalyzerThread kBusyProgressInhibitDuration) so N parallel workers
    /// can't flood the main thread's event queue.
    void progress(int trackId, int percent);
};
}
