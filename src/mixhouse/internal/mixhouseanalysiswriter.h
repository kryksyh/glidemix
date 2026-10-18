/*
 * MixHouse DJ 2
 */
#pragma once

#include <QSqlDatabase>
#include <QString>

#include "mixhouse/mixhouseanalyzer.h"
#include "mixhouse/mixhousetypes.h"

namespace au::mixhouse {
/// Real, measured bug this exists to fix: the background analyzer pool's
/// FIRST design gave each worker thread its own QSqlDatabase connection and
/// had each one write its own track's bpm/beatgrid/key/waveform directly --
/// a faithful-looking generalization of the rule startBackgroundScan()
/// established for the scanner, but WRONG for this specific operation.
/// Measured on a real 1372-track batch: 1240 of 1372 failed with "database
/// is locked" even with a 5s busy_timeout on every connection -- N worker
/// threads all trying to write to the same SQLite file at once is simply
/// too much contention for SQLite's single-writer model to absorb, no
/// matter how long each one is willing to wait. Confirmed directly against
/// real mixxx's own upstream GitHub source before rearchitecting (not
/// guessed): analyzer/analyzerthread.cpp's own real comment -- "Release all
/// references of the track before emitting the signal to ensure that the
/// last reference is not dropped in this worker thread that might trigger
/// database actions!" -- real mixxx's own AnalyzerThread deliberately never
/// lets a worker thread's dropped Track reference trigger a save; the
/// actual database write always happens elsewhere, off the worker thread.
///
/// So: worker threads here now do ONLY the CPU-bound work (decode +
/// Queen Mary DSP, analyzeTrackFile() below) and touch no SQL at all --
/// see MixHouseAnalyzerWorker's own header comment. The actual write goes
/// through writeAnalysisResult() below, called ONLY from the main thread's
/// own onAnalyzerWorkerFinished handler using MixHouseLibrary's own single
/// m_writeDb connection -- back to this app's normal single-writer rule,
/// with the expensive part (decode+DSP) still genuinely parallel across N
/// threads, which is where essentially all the real wall-clock time goes
/// anyway (a SQLite UPDATE of a few hundred bytes is not the bottleneck).
struct MixHouseTrackFileInfo {
    bool valid = false;
    QString filePath;
    bool bpmLocked = false;
    QString error;
};
/// The read half of the old analyzeTrackAndWrite -- looks up the real file
/// path + bpm_lock flag for `trackId`. Called from the MAIN thread only
/// (MixHouseLibrary::dispatchNextTrack, using m_writeDb), BEFORE a track is
/// ever handed to a worker -- the worker itself never touches the database.
MixHouseTrackFileInfo lookupTrackFileForAnalysis(QSqlDatabase& db, int trackId);

/// The write half -- given an already-computed `analysis` (from
/// analyzeTrackFile(), run on a worker thread with no DB involved at all),
/// writes bpm/beats/key/waveform-overview using the given, ALREADY-OPEN
/// QSqlDatabase connection. Called from the MAIN thread only
/// (MixHouseLibrary::onAnalyzerWorkerFinished, using m_writeDb) for the
/// background pool, and also reused unchanged by the old single-track
/// analyzeTrackAndWrite() below (main thread, m_writeDb, same as always --
/// that path was never the one with a concurrency problem).
MixHouseAnalyzeResult writeAnalysisResult(const QString& libraryPath, QSqlDatabase& db, int trackId,
                                          bool bpmLocked, const MixHouseAnalysisResult& analysis);

/// The original single-track entry point, unchanged in behavior --
/// MixHouseLibrary::analyzeTrack() (main thread, m_writeDb) still calls
/// this directly; it's just composed from the two halves above now instead
/// of being one monolithic function, so both halves can be reused
/// separately by the background pool without duplicating the real
/// BeatGrid-2.0/waveform-blob binary encoding logic anywhere.
MixHouseAnalyzeResult analyzeTrackAndWrite(const QString& libraryPath, QSqlDatabase& db, int trackId);
}
