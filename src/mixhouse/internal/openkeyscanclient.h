/*
 * MixHouse DJ 2
 *
 * Real musical-key detection via OpenKeyScan (github.com/rekordcloud/
 * openkeyscan-analyzer, MIT), a convolutional neural network (Korzeniowski &
 * Widmer 2018) trained on the GiantSteps datasets -- swapped in to REPLACE
 * mixhouseanalyzer.cpp's previous key detector, real mixxx's own default
 * Queen Mary DSP `GetKeyMode` (Krumhansl-Schmuckler-style pitch-class
 * profile matching), after we found it producing wrong keys on his real
 * library and asked for OpenKeyScan specifically because he judged it the
 * closest open-source detector to correct real-world keys. BPM/beatgrid/
 * waveform detection in mixhouseanalyzer.cpp are untouched -- this only
 * replaces the key sub-analyzer.
 *
 * OpenKeyScan is a PyTorch tool, not a C++ library -- there is no in-process
 * port here (see the project's own README/model.py: a CNN over librosa CQT
 * spectrograms). Its own project ships a ready-made stdin/stdout NDJSON
 * server (openkeyscan_analyzer_server.py, documented in its INTERFACING.md
 * for exactly this "spawn as a persistent subprocess" integration, there
 * aimed at Electron but the protocol is language-agnostic) -- this client
 * spawns that real, unmodified script inside a dedicated Python virtualenv
 * (`~/Developer/openkeyscan-analyzer/.venv`, pinned deps from that project's
 * own Pipfile: torch==2.2.2, torchaudio==2.2.2, librosa) and speaks its
 * documented protocol, rather than re-implementing CQT feature extraction
 * and CNN inference in C++ (real correctness risk: an inexact CQT port
 * would silently produce subtly-wrong keys, the exact class of bug this
 * swap exists to fix -- see the OpenKeyScan integration decision recorded
 * alongside this file).
 *
 * Where the helper is found (bundled, installed add-on, developer checkout)
 * is findKeyAnalyzer() in the .cpp. Licence: OpenKeyScan's code is MIT; its
 * model was trained on the GiantSteps datasets, which are CC BY-NC -- check
 * before shipping it in a paid build.
 */
#pragma once

#include <atomic>

#include <QObject>
#include <QProcess>
#include <QString>
#include <QThread>

namespace au::mixhouse {

class OpenKeyScanHost;   ///< defined privately in openkeyscanclient.cpp

struct OpenKeyScanResult {
    bool valid = false;
    /// Mixxx's own ChromaticKey enum value (proto/keys.proto: INVALID=0,
    /// C_MAJOR=1..B_MAJOR=12, C_MINOR=13..B_MINOR=24) -- the SAME id
    /// MixHouseAnalysisResult::keyId and MixHouseKeyWheel::colorForKeyId
    /// already use, so nothing downstream of analyzeTrackFile() needs to
    /// change for this new source of keyId.
    int keyId = 0;
    QString camelot;   ///< e.g. "8A" -- OpenKeyScan's own notation, kept for diagnostics/logging only.
    QString keyName;   ///< e.g. "A minor" -- ditto.
    QString error;
};

/// Persistent, thread-safe client for the OpenKeyScan subprocess. One
/// instance is shared by every analyzer worker thread in
/// MixHouseAnalyzerWorker's pool (see startBackgroundAnalysis's real,
/// QThread::idealThreadCount()-sized pool) -- detectKey() is safe to call
/// concurrently from any thread and blocks the CALLING thread only, not the
/// process's own thread, so the worker pool's real parallelism survives
/// this swap.
///
/// Internally owns a QProcess + the actual Python subprocess, both living on
/// a dedicated QThread this class starts and keeps for the app's lifetime
/// (the process is expensive to start -- ~1.5-2.5s model load per
/// OpenKeyScan's own INTERFACING.md -- so it is started lazily once, on
/// first use, and reused for every subsequent track, matching that same
/// doc's own recommended Electron lifecycle: "start on app ready, keep
/// running for app lifetime, stop on quit").
class OpenKeyScanClient : public QObject
{
    Q_OBJECT

public:
    static OpenKeyScanClient& instance();

    /// Blocking call, safe from ANY thread (including multiple analyzer
    /// worker threads at once) -- starts the subprocess lazily on first
    /// call. Returns valid=false with `error` set if the process could not
    /// be started, the request timed out, or the file could not be
    /// analyzed (OpenKeyScan's own "status":"error" response) -- callers
    /// treat that the same as any other analyzeTrackFile() sub-failure:
    /// keyId stays 0, the rest of the analysis (BPM/waveform) is unaffected.
    OpenKeyScanResult detectKey(const QString& filePath, int timeoutMs = 30000);

private:
    OpenKeyScanClient();
    ~OpenKeyScanClient() override;
    OpenKeyScanClient(const OpenKeyScanClient&) = delete;
    OpenKeyScanClient& operator=(const OpenKeyScanClient&) = delete;

    QThread* m_thread = nullptr;
    OpenKeyScanHost* m_host = nullptr;
    //! Set when the app starts quitting: a detectKey() waiting on the host
    //! thread gives up instead of waiting for a thread that has stopped.
    std::atomic<bool> m_stopping { false };
};

}  // namespace au::mixhouse

Q_DECLARE_METATYPE(au::mixhouse::OpenKeyScanResult)
