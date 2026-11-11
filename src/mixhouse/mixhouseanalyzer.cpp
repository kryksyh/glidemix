/*
 * MixHouse DJ 2
 */
#include "mixhouseanalyzer.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QStandardPaths>
#include "mixhousewaveform.h"
#include "mixhousephrasedetector.h"
#include "mixhousephrasemodel.h"
#include "mixhouseenergy.h"
#include "mixhousesoundslike.h"
#ifdef MIXHOUSE_HAVE_COREML
#include "mixhousecoremlbeats.h"
#endif

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <vector>

#include "mixhouse/mixhouseaudiosource.h"
#include "internal/openkeyscanclient.h"
#include "log.h"


#ifdef MIXHOUSE_HAVE_BEAT_THIS
#include "beat_this_api.h"
#ifdef __APPLE__
#include <sys/sysctl.h>   // hw.memsize, for the model pool size (macOS-only feature)
#endif
#endif

// GlideMix: Fast (small model, default) or Accurate (full model) analysis.
static std::atomic<bool> s_accurateBeatAnalysisFlag { false };

// Real mixxx's own Queen Mary DSP library (vendored verbatim, see
// thirdparty/qm-dsp/CMakeLists.txt) -- BPM/beat detection only. Key
// detection used to run through here too (GetKeyMode, Krumhansl-Schmuckler
// pitch-class profile matching) but was replaced with OpenKeyScanClient
// (see that file's own header comment) after we found it producing
// wrong keys on his real library.
#include "dsp/onsets/DetectionFunction.h"
#include "dsp/tempotracking/TempoTrackV2.h"
#include "maths/MathUtilities.h"

using namespace au::mixhouse;

QString au::mixhouse::mixHouseModelPath(const QString& fileName, const QString& devPath)
{
    // GlideMix: the models ship inside the app (Contents/Resources/models on
    // macOS, models/ beside the exe elsewhere). The development path is only a
    // fallback for developer builds: a release must never depend on the build
    // machine's checkout (it did -- Beat This! silently failed on other Macs).
    // A model trained further on this computer (the GlideMix Trainer) lives in
    // the app's data folder and wins over the one that shipped: the app
    // bundle itself is signed and must not change.
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString userModels = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                               + QStringLiteral("/Glidemix/models/");
    for (const QString& candidate : { userModels + fileName,
                                      appDir + QStringLiteral("/../Resources/models/") + fileName,
                                      appDir + QStringLiteral("/models/") + fileName }) {
        if (QFileInfo::exists(candidate)) {
            return QFileInfo(candidate).canonicalFilePath();
        }
    }
#ifdef GLIDEMIX_DEV_HELPERS
    if (!devPath.isEmpty() && QFileInfo::exists(devPath)) {
        return devPath;
    }
#else
    Q_UNUSED(devPath);
#endif
    return QString();
}

// Per-stage timing for the standalone analyzer harness only (never defined
// in the app build).
#ifdef MIXHOUSE_ANALYZER_TIMING
#include <QElapsedTimer>
#include <QStandardPaths>
#include <cstdio>
#define ANALYZER_TIMING_START() QElapsedTimer analyzerTimer; analyzerTimer.start()
#define ANALYZER_TIMING_MARK(stage) fprintf(stderr, "  [timing] %-20s %6lld ms\n", stage, analyzerTimer.restart())
#else
#define ANALYZER_TIMING_START()
#define ANALYZER_TIMING_MARK(stage)
#endif

namespace {
// Loudness blocks for finding where the music starts.
constexpr double kOnsetBlockSec = 0.01;
// A block this far below the loudest block is still silence/noise floor.
constexpr double kOnsetRelativeRms = 0.06;   // about -24 dB
// A grid beat may sit up to this share of a beat before the detected onset
// block and still count as the onset's beat (grid and block edges differ).
constexpr double kOnsetBeatTolerance = 0.25;

#ifdef MIXHOUSE_HAVE_BEAT_THIS
// Beat This! -- see thirdparty/beat_this_cpp/README.mixhouse.md for the
// measurements behind every constant here.

// Each loaded model peaks at ~1.5GB during inference (mostly ONNX Runtime
// overhead, not track length), so the pool is sized from RAM: one model per
// 8GB, 2..6. Models run single-threaded (thirdparty README, patch 3).
// Measured in the app with a 2-model cap: 61% of analyzer-worker time was
// spent waiting for a free model while ~56% of the CPU sat idle.
int maxConcurrentBeatThis()
{
    static const int count = [] {
#ifdef __APPLE__
        size_t size = sizeof(uint64_t);
        uint64_t bytes = 0;
        if (sysctlbyname("hw.memsize", &bytes, &size, nullptr, 0) != 0) {
            return 2;
        }
        const int byRam = static_cast<int>(bytes / (8ULL << 30));
        return std::clamp(byRam, 2, 6);
#else
        // Windows: the smallest pool until RAM is read here too
        // (GlobalMemoryStatusEx); kept out so no <windows.h> joins this
        // unity-built file.
        return 2;
#endif
    }();
    return count;
}

// The parked MixHouse DJ fork measured this model's beat times landing a
// constant ~23.5ms late against real transients: half its n_fft=1024 STFT
// window at 22050Hz (frame start reported instead of frame centre).
constexpr double kBeatThisFrameCentreSec = 1024.0 / (2.0 * 22050.0);
// A Beat This! downbeat only votes if it lands this close to a grid beat.
constexpr double kDownbeatGridToleranceSec = 0.06;
constexpr int kBeatsPerBar = 4;

// Bar 1 is the track's first beat: the first grid beat where the music
// starts. That is how rekordbox sets it, and it is what real DJ edits
// measured (every Z3TO edit had its downbeat on the first beat). Beat This!
// guessed it wrong on the first tracks we checked in the app -- a beat
// late on "Zenith vs. Sweet Dreams", two beats late on both rekordbox
// beat-test tracks -- so it no longer decides bar 1 (and is not run, which
// also takes ~5 s off every track). A real pickup is fixed by hand with
// "Move bar start one beat earlier/later".
constexpr bool kDownbeatFromBeatThis = false;
// GlideMix: the beat grid itself (tempo, beat positions and bar 1) comes from
// Beat This! over the whole track, fitted to one DJ grid (fitDjGrid). Scored
// against rekordbox's grids on 150 tracks: beats on the grid 94.8% (Queen
// Mary's grid: 67.6%), bar 1 right 90.1% (first-beat rule: 54.0%), tempo
// 100% for both. Queen Mary stays the fallback when the fit is unsure.
constexpr bool kGridFromBeatThis = true;
// Below this, Beat This!'s beats do not sit on one steady grid (a tempo
// change, live drumming): Queen Mary's grid is kept. Starting value, to be
// checked on the 2,000-track run.
constexpr double kDjGridMinCoherence = 0.5;
// The DJ grid's tempo range, [min, 2 x min): rekordbox's on Chris's library.
constexpr double kDjGridMinBpm = 98.0;

/// Loaded models are expensive (~80MB file, session setup), so they're kept
/// and handed out one per concurrent inference. Intentionally leaked:
/// tearing down ONNX Runtime during static destruction at quit isn't worth
/// the risk of a hang for memory the OS reclaims anyway.
class BeatThisModelPool
{
public:
    static BeatThisModelPool& instance()
    {
        static auto* pool = new BeatThisModelPool();
        return *pool;
    }

    /// Blocks until a slot is free. nullptr if the model can't be loaded
    /// (logged once; later calls fail fast).
    std::unique_ptr<BeatThis::BeatThis> acquire()
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [this] { return m_failed || !m_idle.empty() || m_created < maxConcurrentBeatThis(); });
        if (m_failed) {
            return nullptr;
        }
        // The Fast/Accurate choice changed: idle models of the other kind go.
        const bool wantSmall = !s_accurateBeatAnalysisFlag.load();
        if (wantSmall != m_idleSmall && !m_idle.empty()) {
            m_created -= static_cast<int>(m_idle.size());
            m_idle.clear();
        }
        m_idleSmall = wantSmall;
        if (!m_idle.empty()) {
            auto model = std::move(m_idle.back());
            m_idle.pop_back();
            return model;
        }
        ++m_created;
        lock.unlock();
        try {
            // No DBN: the grid fit below needs the network's own beats.
            // Small machines (4 threads or fewer) use Beat This!'s small model
            // (CPJKU small0, MIT, 2.1M parameters): on 196 collection tracks
            // 1.7x faster, tempo the same 99.5%, beats 76.5% vs 78.4%, bar 1
            // 63.3% vs 64.8%. GLIDEMIX_BEAT_MODEL picks a file, for measuring.
#ifdef MIXHOUSE_HAVE_COREML
            // Mac: the full model on the GPU (mixhousecoremlbeats.h); the
            // ONNX session below is only the fallback, so the small one.
            static const bool coreMl = installCoreMlBeatThis(
                mixHouseModelPath(QStringLiteral("beat_this.mlmodelc"), QStringLiteral(MIXHOUSE_BEAT_THIS_COREML_PATH)));
            const bool small = coreMl || !s_accurateBeatAnalysisFlag.load();
#else
            const bool small = !s_accurateBeatAnalysisFlag.load();
#endif
            QString path = qEnvironmentVariable("GLIDEMIX_BEAT_MODEL");
            if (path.isEmpty() && small) {
                path = mixHouseModelPath(QStringLiteral("beat_this_small.onnx"),
                                         QStringLiteral(MIXHOUSE_BEAT_THIS_SMALL_MODEL_PATH));
            }
            if (path.isEmpty()) {
                path = mixHouseModelPath(QStringLiteral("beat_this.onnx"), QStringLiteral(MIXHOUSE_BEAT_THIS_MODEL_PATH));
            }
            return std::make_unique<BeatThis::BeatThis>(path.toStdString(), false);
        } catch (const std::exception& e) {
            LOGE() << "Beat This! model could not be loaded: " << e.what() << " -- the beat grid falls back to Queen Mary";
            std::lock_guard<std::mutex> relock(m_mutex);
            --m_created;
            m_failed = true;
            m_cv.notify_all();
            return nullptr;
        }
    }

    void release(std::unique_ptr<BeatThis::BeatThis> model)
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_idle.push_back(std::move(model));
        }
        m_cv.notify_one();
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::vector<std::unique_ptr<BeatThis::BeatThis> > m_idle;
    int m_created = 0;
    bool m_failed = false;
    bool m_idleSmall = true;
};

// Bar phase on a constant grid doesn't need the whole track. Measured
// against full-track results on "#Selfie NRG", "Almost Love" (a 2-beat
// pickup), "All About House", "Acid Infusion", "Back to the Beat" and "666":
// one 30s window at the loudest part gave the identical downbeat on all six
// at 3-7x less inference time; 20s windows got "Acid Infusion" 3 beats wrong.
// In a 35k-track library: full-track inference made analysis ~5s/track.
constexpr double kDownbeatWindowSec = 30.0;
// A first window this unsure (or with this few on-grid downbeats) gets a
// second, non-overlapping window before deciding.
constexpr double kDownbeatConfidentShare = 0.75;
constexpr int kDownbeatMinVotes = 8;

struct DownbeatVotes {
    int votes[kBeatsPerBar] = {};
    int total = 0;
    double firstDetectedSec = -1.0;   ///< only used when there's no grid
};

/// Adds every detected downbeat (times relative to a window starting at
/// windowStartSec) as a vote for its bar phase on the constant grid
/// (bpm, firstBeatSec). Measured on "#Selfie NRG": DBN post-processing gave
/// 105/105 downbeats on one phase; relying on only the FIRST detected
/// downbeat (what the fork did) is fragile.
void addDownbeatVotes(DownbeatVotes& acc, const std::vector<float>& downbeats, double windowStartSec,
                      double bpm, double firstBeatSec)
{
    for (float detected : downbeats) {
        const double t = windowStartSec + static_cast<double>(detected) - kBeatThisFrameCentreSec;
        if (acc.firstDetectedSec < 0.0 || t < acc.firstDetectedSec) {
            acc.firstDetectedSec = std::max(0.0, t);
        }
        if (bpm <= 0.0) {
            continue;
        }
        const double period = 60.0 / bpm;
        const long long beat = std::llround((t - firstBeatSec) / period);
        if (std::abs(t - (firstBeatSec + static_cast<double>(beat) * period)) > kDownbeatGridToleranceSec) {
            continue;
        }
        ++acc.votes[((beat % kBeatsPerBar) + kBeatsPerBar) % kBeatsPerBar];
        ++acc.total;
    }
}

struct DownbeatEstimate {
    double sec = -1.0;
    double confidence = 0.0;
};

DownbeatEstimate resolveDownbeat(const DownbeatVotes& acc, double bpm, double firstBeatSec)
{
    DownbeatEstimate out;
    if (bpm <= 0.0) {
        out.sec = acc.firstDetectedSec;   // no grid to vote against
        return out;
    }
    if (acc.total == 0) {
        // Nothing landed on the grid -- usually the grid itself is wrong
        // (measured: "Beauty Off Alone" at a 150 BPM grid). An off-grid time
        // would misalign worse than no downbeat, so report none.
        return out;
    }
    const int phase = static_cast<int>(std::max_element(acc.votes, acc.votes + kBeatsPerBar) - acc.votes);
    out.sec = firstBeatSec + phase * (60.0 / bpm);
    out.confidence = static_cast<double>(acc.votes[phase]) / acc.total;
    return out;
}

struct DjGrid {
    bool valid = false;
    double bpm = 0.0;
    double barStartSec = 0.0;   ///< any bar 1 of the grid (a downbeat)
    double coherence = 0.0;     ///< 0..1: how well the beats line up on one grid
};

/// One constant tempo and phase for Beat This!'s beats: of the tempos within
/// 3% of the median beat gap, the one whose grid the beats line up on best
/// (the length of the mean phase vector), which shrugs off missed and extra
/// beats and beatless intros; a straight-line fit over the beat numbers did
/// not (Beat This! gives 20 ms steps, so counting beats off the median gap
/// drifts). Bar 1: the beat of the bar most of its downbeats land on.
//! Beat This! is most of the analysis time (17-30 s a track on an M1 Max,
//! one thread). A DJ grid has one tempo and phase, so the model hears three
//! 30-second windows -- start, middle, end -- instead of the whole track: the
//! far-apart windows keep the tempo fit's long baseline at about a third of
//! the work. Short tracks are heard whole.
//! OFF by default: on 196 tracks it scored beats 75.3% vs 78.4% and bar 1
//! 60.7% vs 64.8% (whole track). GLIDEMIX_BEAT_WINDOWS=1 turns it on for
//! measuring better window layouts.
BeatThis::BeatResult beatThisWindows(BeatThis::BeatThis& model, const std::vector<float>& mono, int sampleRate)
{
    constexpr double kWindowSec = 30.0;
    constexpr int kWindows = 3;
    const size_t window = static_cast<size_t>(kWindowSec * sampleRate);
    if (!qEnvironmentVariableIsSet("GLIDEMIX_BEAT_WINDOWS") || mono.size() < window * (kWindows + 1)) {
        return model.process_audio(mono.data(), mono.size(), sampleRate, 1);
    }
    BeatThis::BeatResult all;
    // Skip the first and last 5% (silence, fade) when placing the windows.
    const size_t lo = mono.size() / 20, hi = mono.size() - mono.size() / 20 - window;
    for (int w = 0; w < kWindows; ++w) {
        const size_t start = lo + (hi - lo) * w / (kWindows - 1);
        const BeatThis::BeatResult part = model.process_audio(mono.data() + start, window, sampleRate, 1);
        const float offset = static_cast<float>(static_cast<double>(start) / sampleRate);
        // Drop the edges, where the model has no context.
        for (float b : part.beats) {
            if (b > 1.0f && b < kWindowSec - 1.0f) { all.beats.push_back(b + offset); }
        }
        for (float d : part.downbeats) {
            if (d > 1.0f && d < kWindowSec - 1.0f) { all.downbeats.push_back(d + offset); }
        }
    }
    return all;
}

DjGrid fitDjGrid(const std::vector<float>& beats, const std::vector<float>& downbeats)
{
    DjGrid grid;
    if (beats.size() < 16) {
        return grid;
    }
    std::vector<double> gaps;
    gaps.reserve(beats.size());
    for (size_t i = 1; i < beats.size(); ++i) {
        gaps.push_back(beats[i] - beats[i - 1]);
    }
    std::nth_element(gaps.begin(), gaps.begin() + gaps.size() / 2, gaps.end());
    const double medianGap = gaps[gaps.size() / 2];
    if (medianGap <= 0.0) {
        return grid;
    }
    const double bpm0 = 60.0 / medianGap;
    double bestLength = -1.0, bestPeriod = 0.0, bestPhase = 0.0;
    for (double bpm = bpm0 * 0.97; bpm <= bpm0 * 1.03; bpm += 0.005) {
        const double period = 60.0 / bpm;
        double re = 0.0, im = 0.0;
        for (float b : beats) {
            const double a = 2.0 * M_PI * b / period;
            re += std::cos(a);
            im += std::sin(a);
        }
        const double length = std::hypot(re, im) / beats.size();
        if (length > bestLength) {
            bestLength = length;
            bestPeriod = period;
            bestPhase = std::atan2(im, re);
        }
    }
    double phaseSec = bestPhase / (2.0 * M_PI) * bestPeriod;
    phaseSec -= std::floor(phaseSec / bestPeriod) * bestPeriod;

    // The tempo octave: rekordbox keeps every BPM in 98-196 (all 35k tracks of
    // Chris's collection); folding into it took tempo-right from 93.3% to
    // 97.9% on 2,000 tracks. Halved, the beats kept are the downbeats' ones.
    double bpm = 60.0 / bestPeriod;
    while (bpm < kDjGridMinBpm) {
        bpm *= 2.0;
    }
    while (bpm >= 2.0 * kDjGridMinBpm) {
        bpm /= 2.0;
    }
    const double period = 60.0 / bpm;
    if (period > bestPeriod * 1.5 && downbeats.size() >= 4) {
        double re = 0.0, im = 0.0;
        for (float d : downbeats) {
            const double a = 2.0 * M_PI * d / period;
            re += std::cos(a);
            im += std::sin(a);
        }
        phaseSec = std::atan2(im, re) / (2.0 * M_PI) * period;
        phaseSec -= std::floor(phaseSec / period) * period;
    }
    bestPeriod = period;

    int votes[kBeatsPerBar] = {};
    for (float d : downbeats) {
        const long long k = std::llround((d - phaseSec) / bestPeriod);
        if (std::abs(phaseSec + k * bestPeriod - d) < kDownbeatGridToleranceSec) {
            ++votes[((k % kBeatsPerBar) + kBeatsPerBar) % kBeatsPerBar];
        }
    }
    const int bar = static_cast<int>(std::max_element(votes, votes + kBeatsPerBar) - votes);
    grid.valid = true;
    grid.bpm = 60.0 / bestPeriod;
    grid.barStartSec = phaseSec + bar * bestPeriod;
    grid.coherence = bestLength;
    return grid;
}

/// Start (in samples) of the loudest windowLen-sample stretch of `mono`,
/// ignoring [excludeStart, excludeEnd). Mean power sampled every 64 samples,
/// scanned in 1s hops -- plenty to find a drop vs a breakdown.
size_t loudestWindowStart(const std::vector<float>& mono, size_t windowLen, int sampleRate,
                          size_t excludeStart = 0, size_t excludeEnd = 0)
{
    const size_t hop = static_cast<size_t>(std::max(1, sampleRate));
    size_t best = 0;
    double bestEnergy = -1.0;
    for (size_t start = 0; start + windowLen <= mono.size(); start += hop) {
        if (start < excludeEnd && start + windowLen > excludeStart) {
            continue;
        }
        double energy = 0.0;
        for (size_t i = start; i < start + windowLen; i += 64) {
            energy += static_cast<double>(mono[i]) * mono[i];
        }
        if (energy > bestEnergy) {
            bestEnergy = energy;
            best = start;
        }
    }
    return bestEnergy < 0.0 ? mono.size() : best;   // mono.size() = no eligible window
}
#endif

// ---------------------------------------------------------------------------
// Ported verbatim from real mixxx's analyzer/plugins/buffering_utils.{h,cpp}
// (DownmixAndOverlapHelper) -- downmixes to mono and slides a fixed-size,
// fixed-step analysis window across the stream, calling back once per full
// window. Generalized here from a hardcoded 2-channel downmix to N channels
// (real mixxx forces stereo via AudioSourceStereoProxy before this ever
// runs; this app's own decoder can hand back a genuinely mono file, so the
// downmix averages however many channels are actually present instead of
// assuming 2).
class DownmixAndOverlapHelper
{
public:
    using WindowReadyCallback = std::function<bool(double* window, size_t frames)>;

    bool initialize(size_t windowSize, size_t stepSize, int channels, WindowReadyCallback callback)
    {
        m_buffer.assign(windowSize, 0.0);
        m_callback = std::move(callback);
        m_windowSize = windowSize;
        m_stepSize = stepSize;
        m_channels = std::max(1, channels);
        // Center the first frame in the window, same as real mixxx, so the
        // result is meaningful starting from the first step.
        m_bufferWritePosition = windowSize / 2;
        return m_windowSize > 0 && m_stepSize > 0 && m_stepSize <= m_windowSize && m_callback;
    }

    bool processSamples(const float* input, long long inputFrames)
    {
        return processInner(input, static_cast<size_t>(inputFrames));
    }

    bool finalize()
    {
        // Real mixxx: append at least windowSize/2 - stepSize of silence so
        // the last real samples still get a valid analysis window.
        const size_t framesToFillWindow = m_windowSize - m_bufferWritePosition;
        const size_t numInputFrames = std::max(framesToFillWindow,
                                                m_windowSize / 2 >= 1 ? m_windowSize / 2 - 1 : 0);
        return processInner(nullptr, numInputFrames);
    }

private:
    bool processInner(const float* input, size_t numInputFrames)
    {
        size_t inRead = 0;
        double* pDownmix = m_buffer.data();

        while (inRead < numInputFrames) {
            const size_t readAvailable = numInputFrames - inRead;
            const size_t writeAvailable = m_windowSize - m_bufferWritePosition;
            const size_t numFrames = std::min(readAvailable, writeAvailable);

            if (input) {
                for (size_t i = 0; i < numFrames; ++i) {
                    double sum = 0.0;
                    const size_t base = (inRead + i) * static_cast<size_t>(m_channels);
                    for (int c = 0; c < m_channels; ++c) {
                        sum += input[base + static_cast<size_t>(c)];
                    }
                    pDownmix[m_bufferWritePosition + i] = sum / m_channels;
                }
            } else {
                for (size_t i = 0; i < numFrames; ++i) {
                    pDownmix[m_bufferWritePosition + i] = 0.0;
                }
            }
            m_bufferWritePosition += numFrames;
            inRead += numFrames;

            if (m_bufferWritePosition == m_windowSize) {
                if (!m_callback(pDownmix, m_windowSize)) {
                    return false;
                }
                for (size_t i = 0; i < (m_windowSize - m_stepSize); ++i) {
                    pDownmix[i] = pDownmix[i + m_stepSize];
                }
                m_bufferWritePosition -= m_stepSize;
            }
        }
        return true;
    }

    std::vector<double> m_buffer;
    size_t m_windowSize = 0;
    size_t m_stepSize = 0;
    size_t m_bufferWritePosition = 0;
    int m_channels = 2;
    WindowReadyCallback m_callback;
};

// ---------------------------------------------------------------------------
// Ported from real mixxx's AnalyzerQueenMaryBeats (analyzer/plugins/
// analyzerqueenmarybeats.cpp) -- same config values, same DetectionFunction/
// TempoTrackV2 usage, same frame-position math.
class BeatsDetector
{
public:
    // Real mixxx's exact constants (analyzerqueenmarybeats.cpp): ~12ms
    // (86Hz) step, matching the VAMP beat tracker's preferred window/step;
    // window derived from a 50Hz-max detection-function bin size.
    static constexpr float kStepSecs = 0.01161f;
    static constexpr int kMaximumBinSizeHz = 50;

    void initialize(int sampleRate)
    {
        m_sampleRate = sampleRate;
        m_stepSizeFrames = static_cast<int>(sampleRate * kStepSecs);
        m_windowSize = MathUtilities::nextPowerOfTwo(sampleRate / kMaximumBinSizeHz);

        DFConfig config;
        config.DFType = DF_COMPLEXSD;
        config.stepSize = m_stepSizeFrames;
        config.frameLength = m_windowSize;
        config.dbRise = 3;
        config.adaptiveWhitening = false;
        config.whiteningRelaxCoeff = -1;
        config.whiteningFloor = -1;
        m_detectionFunction = std::make_unique<DetectionFunction>(config);

        m_helper.initialize(static_cast<size_t>(m_windowSize), static_cast<size_t>(m_stepSizeFrames),
                            m_channels, [this](double* window, size_t) {
            m_detectionResults.push_back(m_detectionFunction->processTimeDomain(window));
            return true;
        });
    }

    void setChannels(int channels) { m_channels = channels; }

    bool processSamples(const float* input, long long frames)
    {
        return m_helper.processSamples(input, frames);
    }

    /// Real mixxx's own TempoTrackV2 pass, then the frame-position math from
    /// AnalyzerQueenMaryBeats::finalize(). Beat positions come back in
    /// FRAMES, not seconds -- real mixxx's own BeatUtils (see
    /// mixhouseCalculateBpm below) does all of its tempo-detection math in
    /// frames, and converting to seconds first would just mean re-deriving
    /// the same frame-based tolerances by hand instead of matching the real
    /// code exactly.
    std::vector<double> finalize()
    {
        m_helper.finalize();

        std::size_t nonZeroCount = m_detectionResults.size();
        while (nonZeroCount > 0 && m_detectionResults.at(nonZeroCount - 1) <= 0.0) {
            --nonZeroCount;
        }
        const std::size_t requiredSize = std::max(static_cast<std::size_t>(2), nonZeroCount) - 2;

        std::vector<double> df;
        df.reserve(requiredSize);
        auto beatPeriod = std::vector<int>(requiredSize / 128 + 1);
        // Skip the first 2 results -- they can register a false onset at the
        // very start of the file (real mixxx's own comment: "that's how vamp
        // does and seems works best this way").
        for (std::size_t i = 2; i < nonZeroCount; ++i) {
            df.push_back(m_detectionResults.at(i));
        }

        TempoTrackV2 tt(m_sampleRate, m_stepSizeFrames);
        tt.calculateBeatPeriod(df, beatPeriod);

        std::vector<double> beatSteps;
        tt.calculateBeats(df, beatPeriod, beatSteps);

        std::vector<double> beatFrames;
        beatFrames.reserve(beatSteps.size());
        for (double stepIndex : beatSteps) {
            // Real mixxx adds half a step: the beat is detected BETWEEN two
            // analysis frames, not exactly on one.
            const double frame = (stepIndex * m_stepSizeFrames) + (m_stepSizeFrames / 2.0);
            beatFrames.push_back(frame);
        }
        return beatFrames;
    }

private:
    int m_sampleRate = 44100;
    int m_channels = 2;
    int m_windowSize = 0;
    int m_stepSizeFrames = 0;
    std::unique_ptr<DetectionFunction> m_detectionFunction;
    DownmixAndOverlapHelper m_helper;
    std::vector<double> m_detectionResults;
};

// ---------------------------------------------------------------------------
// A simple whole-track amplitude envelope, fed from the SAME decode pass
// beats/key already run over. 1920 entries regardless of track length --
// real mixxx's own fixed resolution for its Overview widget
// (library/waveform/overviewcache.cpp's WOverview), confirmed against
// real analysis files earlier this project (2*1920 = 3842 raw
// entries once real mixxx's L/R interleaving is accounted for).
//
// Per-bucket aggregation is RMS (root-mean-square energy), NOT real mixxx's
// own peak/max rule -- a deliberate, disclosed departure. Real mixxx's own
// "loudest sample in the window wins" approach was ported here first, then
// replaced after we compared it directly against real rekordbox
// screenshots of his own (heavily-mastered, loud) library: peak-picking
// flattens that kind of material into a near-solid block (measured directly
// by decoding a real stored analysis file -- 255 for ~98% of the track),
// while RMS naturally shows the organic contour rekordbox's own preview
// does, without needing any rendering-side trick to compensate.
class AmplitudeEnvelopeCollector
{
public:
    static constexpr int kVisualSamples = 1920;

    // `totalFrames` should be the decoder's own best estimate (AudioInfo::
    // frames) -- if it's not positive, this collector stays disabled and
    // finalize() returns an empty vector rather than guessing a bucket
    // width from nothing.
    void initialize(long long totalFrames, int channels)
    {
        m_channels = std::max(1, channels);
        if (totalFrames > 0) {
            m_framesPerBucket = std::max<long long>(1, totalFrames / kVisualSamples);
            m_bucketSumSquares.assign(static_cast<size_t>(kVisualSamples), 0.0);
            m_bucketSampleCounts.assign(static_cast<size_t>(kVisualSamples), 0);
            m_enabled = true;
        }
    }

    void processSamples(const float* input, long long frames)
    {
        if (!m_enabled) {
            return;
        }
        for (long long i = 0; i < frames; ++i) {
            double sumSq = 0.0;
            for (int c = 0; c < m_channels; ++c) {
                const double v = input[i * m_channels + c];
                sumSq += v * v;
            }
            const size_t bucket = std::min(m_bucketSumSquares.size() - 1,
                static_cast<size_t>(m_frameIndex / m_framesPerBucket));
            m_bucketSumSquares[bucket] += sumSq / m_channels;
            ++m_bucketSampleCounts[bucket];
            ++m_frameIndex;
        }
    }

    std::vector<uint8_t> finalize() const
    {
        if (!m_enabled) {
            return {};
        }
        // RMS (root-mean-square) energy per bucket, not peak. A pure
        // peak/max envelope (this collector's original design, matching
        // real mixxx's own analyzerwaveform.cpp exactly) flattens almost
        // any heavily-mastered/loud modern track into a near-solid block --
        // measured directly on a real track in the reference library (decoded the
        // actual stored analysis bytes: 255 for ~98% of the track, only
        // dropping in a genuine silent tail) -- because a single loud
        // transient anywhere in a bucket's window makes the whole bucket
        // read as "loud" regardless of the surrounding material. Real
        // rekordbox's own preview clearly shows a varying, organic envelope
        // even for the same kind of material, which RMS (averaging energy
        // across the whole window rather than taking its single loudest
        // instant) naturally produces -- this is a deliberate, disclosed
        // departure from real mixxx's own peak-based approach, matching
        // rekordbox instead, same reasoning as the color/headroom work in
        // MixHouseLibraryPanel.qml. Only affects tracks MixHouse itself
        // analyzes (new tracks, or an explicit re-analyze) -- the project's
        // already-analyzed library keeps real mixxx's own stored data
        // as-is, untouched, unless re-analyzed.
        std::vector<uint8_t> out(m_bucketSumSquares.size());
        for (size_t i = 0; i < m_bucketSumSquares.size(); ++i) {
            const double count = m_bucketSampleCounts[i] > 0
                ? static_cast<double>(m_bucketSampleCounts[i]) : 1.0;
            const double rms = std::sqrt(m_bucketSumSquares[i] / count);
            const double clamped = std::min(1.0, rms);
            out[i] = static_cast<uint8_t>(clamped * 255.0);
        }
        return out;
    }

private:
    bool m_enabled = false;
    int m_channels = 2;
    long long m_framesPerBucket = 1;
    long long m_frameIndex = 0;
    std::vector<double> m_bucketSumSquares;
    std::vector<long long> m_bucketSampleCounts;
};

// ---------------------------------------------------------------------------
// A single biquad section (Direct Form II Transposed) -- textbook, not
// mixxx's own EngineFilterIIR, but the same well-known difference equation
// any correct biquad implementation reduces to.
struct Biquad {
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double z1 = 0.0, z2 = 0.0;

    float process(float x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return static_cast<float>(y);
    }
};

// RBJ Audio EQ Cookbook lowpass/highpass biquad coefficients (the standard,
// well-known bilinear-transform derivation every correct biquad lowpass/
// highpass reduces to -- not mixxx-specific, this is the same math real
// mixxx's own fidlib arrives at via prewarp + bilinear transform, just
// without needing fidlib's own analog-prototype machinery since a 2nd-order
// section only ever needs one Q).
Biquad makeLowpassBiquad(double sampleRate, double freqHz, double q)
{
    const double w0 = 2.0 * M_PI * freqHz / sampleRate;
    const double cosw0 = std::cos(w0);
    const double alpha = std::sin(w0) / (2.0 * q);
    const double a0 = 1.0 + alpha;
    Biquad bq;
    bq.b0 = ((1.0 - cosw0) / 2.0) / a0;
    bq.b1 = (1.0 - cosw0) / a0;
    bq.b2 = bq.b0;
    bq.a1 = (-2.0 * cosw0) / a0;
    bq.a2 = (1.0 - alpha) / a0;
    return bq;
}

Biquad makeHighpassBiquad(double sampleRate, double freqHz, double q)
{
    const double w0 = 2.0 * M_PI * freqHz / sampleRate;
    const double cosw0 = std::cos(w0);
    const double alpha = std::sin(w0) / (2.0 * q);
    const double a0 = 1.0 + alpha;
    Biquad bq;
    bq.b0 = ((1.0 + cosw0) / 2.0) / a0;
    bq.b1 = (-(1.0 + cosw0)) / a0;
    bq.b2 = bq.b0;
    bq.a1 = (-2.0 * cosw0) / a0;
    bq.a2 = (1.0 - alpha) / a0;
    return bq;
}

// Two cascaded biquads at these two specific Q values is the standard,
// well-known way to build a 4th-order Butterworth filter (the Butterworth
// pole angles for order 4, Q = 1 / (2*cos((2k-1)*pi/8)) for k=1,2) -- a
// textbook DSP identity, independent of mixxx.
constexpr double kButterworth4Q1 = 0.5411961001461969;  // 1 / (2*cos(pi/8))
constexpr double kButterworth4Q2 = 1.3065629648763764;  // 1 / (2*cos(3*pi/8))

struct Butterworth4Lowpass {
    Biquad stage1, stage2;
    void initialize(double sampleRate, double freqHz)
    {
        stage1 = makeLowpassBiquad(sampleRate, freqHz, kButterworth4Q1);
        stage2 = makeLowpassBiquad(sampleRate, freqHz, kButterworth4Q2);
    }
    float process(float x) { return stage2.process(stage1.process(x)); }
};

struct Butterworth4Highpass {
    Biquad stage1, stage2;
    void initialize(double sampleRate, double freqHz)
    {
        stage1 = makeHighpassBiquad(sampleRate, freqHz, kButterworth4Q1);
        stage2 = makeHighpassBiquad(sampleRate, freqHz, kButterworth4Q2);
    }
    float process(float x) { return stage2.process(stage1.process(x)); }
};

// ---------------------------------------------------------------------------
// Real mixxx's own 3-band split for the waveform overview/deck display
// (analyzer/analyzerwaveform.cpp): three independent filters over the SAME
// decoded audio -- a 600Hz lowpass, a 600-4000Hz bandpass, and a 4000Hz
// highpass, each 4th order -- per bucket, written into
// signal_filtered.{low,mid,high}. This is what makes the library's RGB
// Waveform Overview column actually colorful instead of solid black (see
// MixHouseAnalysisResult::waveformLow/Mid/High's own comment, and
// MixHouseLibraryPanel.qml's overview Canvas, which already renders real
// color whenever this data is present -- no rendering-code change needed
// for this to show up).
//
// Per-bucket aggregation is RMS, matching AmplitudeEnvelopeCollector's own
// switch away from real mixxx's own peak/max rule (real mixxx's
// analyzerwaveform.cpp: storeIfGreater() is a plain running max, despite
// waveform.proto's field literally being named "RMS" -- an RMS-averaging
// alternative is visibly commented out right beside it in real mixxx's own
// source, unused there). Ported here first as real mixxx's own peak rule,
// then changed to genuine RMS for the same reason as the overall envelope
// above: peak-picking flattens loud, heavily-mastered tracks (confirmed
// directly on a real file in the reference library) into a near-solid block,
// which doesn't match real rekordbox's own organic-looking preview.
//
// Two disclosed simplifications from real mixxx's own exact pipeline:
//  1. Real mixxx's filters are 4th-order BESSEL (fidlib's hardcoded analog
//     pole table, fed through "LpBe4"/"BpBe4"/"HpBe4"), chosen for flat
//     group delay (minimal ringing on transients) rather than the sharpest
//     possible roll-off. This uses the SAME topology -- independent low/
//     band/high filters, same 600Hz/4000Hz crossovers, same 4th order (two
//     cascaded biquads, same as any standard 4th-order design) -- but a
//     textbook BUTTERWORTH cascade instead of reproducing fidlib's Bessel
//     pole table. Bessel vs. Butterworth only meaningfully differs in
//     phase/ringing behavior on LIVE playback audio; for a whole-track
//     summary bar that gets peak-reduced down to a handful of buckets per
//     track regardless, the difference doesn't survive to be visible.
//     "Mid" is a genuine bandpass here too (highpass cascaded into lowpass),
//     not a low/high subtraction -- matching real mixxx's own topology,
//     just not its exact analog-bandpass pole transform.
//  2. Real mixxx filters genuine stereo (mono duplicated to stereo, kept
//     separate throughout) and keeps L/R separate all the way into
//     signal_filtered. This downmixes to mono FIRST -- matching this same
//     file's own BeatsDetector/KeyDetector/AmplitudeEnvelopeCollector, which
//     already do exactly that -- filters the mono signal, then writes each
//     band's one mono value TWICE per bucket, the same established "fake
//     the real 2-interleaved-channels wire format" trick
//     encodeWaveformOverview() already uses for signal_all.
//  3. Real mixxx computes a peak per 441Hz "visual sample" (analyzerwaveform.
//     cpp's own mainWaveformSampleRate), THEN averages consecutive peaks
//     down into the final 1920-bucket overview. This collapses that into
//     ONE stage -- a straight max across every raw frame in each of 1920
//     buckets -- matching AmplitudeEnvelopeCollector above (already the
//     same simplification, already shipped for signal_all). Keeping all
//     four bands on identical single-stage bucket boundaries means they
//     stay pixel-aligned with each other, which matters more for this
//     port's rendering than an extra averaging stage would.
// GlideMix: loudness of three bands in 10 ms blocks, summed per beat once the
// grid is known -- the phrase model's view of where the kick and bass are
// (a drop vs a build vs a break). Same split as the experiment that trained
// it: sub/kick <150 Hz, low-mid 150-2000 Hz, high >2 kHz.
class PhraseEnergyCollector
{
public:
    static constexpr double kBlockSec = 0.01;

    void initialize(int channels, int sampleRate)
    {
        m_channels = std::max(1, channels);
        m_sampleRate = sampleRate;
        m_blockFrames = std::max<long long>(1, static_cast<long long>(kBlockSec * sampleRate));
        m_sub.initialize(sampleRate, 150.0);
        m_midHp.initialize(sampleRate, 150.0);
        m_midLp.initialize(sampleRate, 2000.0);
        m_high.initialize(sampleRate, 2000.0);
    }

    void processSamples(const float* input, long long frames)
    {
        for (long long i = 0; i < frames; ++i) {
            double sum = 0.0;
            for (int c = 0; c < m_channels; ++c) {
                sum += input[i * m_channels + c];
            }
            const float mono = static_cast<float>(sum / m_channels);
            const double sub = m_sub.process(mono);
            const double mid = m_midLp.process(m_midHp.process(mono));
            const double high = m_high.process(mono);
            m_acc[0] += sub * sub;
            m_acc[1] += mid * mid;
            m_acc[2] += high * high;
            if (++m_fill == m_blockFrames) {
                for (int b = 0; b < 3; ++b) {
                    m_blocks.push_back(static_cast<float>(m_acc[b] / m_blockFrames));
                    m_acc[b] = 0.0;
                }
                m_fill = 0;
            }
        }
    }

    //! Per beat of the grid (3 values each), in dB relative to the 95th
    //! percentile of each band over the track.
    std::vector<float> perBeat(double bpm, double firstBeatSec) const
    {
        const size_t blocks = m_blocks.size() / 3;
        if (bpm <= 0.0 || blocks < 2) {
            return {};
        }
        const double beatSec = 60.0 / bpm;
        const double trackSec = blocks * kBlockSec;
        double t = firstBeatSec - std::floor(firstBeatSec / beatSec) * beatSec;
        std::vector<float> out;
        for (; t + beatSec <= trackSec + 1e-9; t += beatSec) {
            const size_t a = static_cast<size_t>(t / kBlockSec);
            const size_t e = std::max(a + 1, std::min(blocks, static_cast<size_t>((t + beatSec) / kBlockSec)));
            for (int b = 0; b < 3; ++b) {
                double sum = 0.0;
                for (size_t k = a; k < e; ++k) {
                    sum += m_blocks[k * 3 + b];
                }
                out.push_back(static_cast<float>(10.0 * std::log10(sum / (e - a) + 1e-10)));
            }
        }
        const size_t beats = out.size() / 3;
        for (int b = 0; b < 3 && beats > 0; ++b) {
            std::vector<float> band(beats);
            for (size_t k = 0; k < beats; ++k) {
                band[k] = out[k * 3 + b];
            }
            const size_t p95 = std::min(beats - 1, static_cast<size_t>(0.95 * (beats - 1) + 0.5));
            std::nth_element(band.begin(), band.begin() + p95, band.end());
            const float ref = band[p95];
            for (size_t k = 0; k < beats; ++k) {
                out[k * 3 + b] -= ref;
            }
        }
        return out;
    }

    //! Whole-track features for the energy rating (mixhouseenergy.h), from
    //! the same 10 ms blocks, in absolute dB.
    std::vector<float> energyFeatures(double bpm) const
    {
        const size_t blocks = m_blocks.size() / 3;
        if (blocks < 500) {
            return {};   // under 5 s
        }
        const auto db = [](double v) { return 10.0 * std::log10(v + 1e-12); };
        std::vector<double> total(blocks), sub(blocks), mid(blocks), high(blocks);
        for (size_t k = 0; k < blocks; ++k) {
            sub[k] = db(m_blocks[k * 3]);
            mid[k] = db(m_blocks[k * 3 + 1]);
            high[k] = db(m_blocks[k * 3 + 2]);
            total[k] = db(double(m_blocks[k * 3]) + m_blocks[k * 3 + 1] + m_blocks[k * 3 + 2]);
        }
        const auto pct = [](std::vector<double> v, double p) {
            const size_t i = std::min(v.size() - 1, static_cast<size_t>(p * (v.size() - 1) + 0.5));
            std::nth_element(v.begin(), v.begin() + i, v.end());
            return v[i];
        };
        const double top = pct(total, 0.95);
        // The part of the song that is playing (not the silence around it).
        double power = 0.0, subP = 0.0, midP = 0.0, highP = 0.0;
        size_t active = 0, full = 0;
        double subFlux = 0.0, highFlux = 0.0;
        size_t subHits = 0, highHits = 0;
        for (size_t k = 0; k < blocks; ++k) {
            if (total[k] < top - 30.0) {
                continue;
            }
            ++active;
            power += m_blocks[k * 3] + m_blocks[k * 3 + 1] + m_blocks[k * 3 + 2];
            subP += m_blocks[k * 3];
            midP += m_blocks[k * 3 + 1];
            highP += m_blocks[k * 3 + 2];
            full += total[k] >= top - 6.0 ? 1 : 0;
            if (k > 0) {
                const double ds = sub[k] - sub[k - 1];
                const double dh = high[k] - high[k - 1];
                subFlux += std::abs(ds);
                highFlux += std::abs(dh);
                subHits += ds > 6.0 ? 1 : 0;   // a kick, about
                highHits += dh > 6.0 ? 1 : 0;  // a hat or snare, about
            }
        }
        if (active < 100) {
            return {};
        }
        const double seconds = active * kBlockSec;
        return {
            static_cast<float>(db(power / active)),               // 0 loudness
            static_cast<float>(db(subP / power)),                 // 1 sub share
            static_cast<float>(db(midP / power)),                 // 2 mid share
            static_cast<float>(db(highP / power)),                // 3 high share
            static_cast<float>(double(full) / active),            // 4 time at full power
            static_cast<float>(top - pct(total, 0.10)),           // 5 dynamic range
            static_cast<float>(subFlux / active),                 // 6 kick pumping
            static_cast<float>(highFlux / active),                // 7 hat activity
            static_cast<float>(subHits / seconds),                // 8 kicks per second
            static_cast<float>(highHits / seconds),               // 9 hits per second
            static_cast<float>(bpm),                              // 10 tempo
            static_cast<float>(top),                              // 11 peak loudness
        };
    }

private:
    int m_channels = 1;
    int m_sampleRate = 0;
    long long m_blockFrames = 1;
    long long m_fill = 0;
    double m_acc[3] = {};
    Butterworth4Lowpass m_sub;
    Butterworth4Highpass m_midHp;
    Butterworth4Lowpass m_midLp;
    Butterworth4Highpass m_high;
    std::vector<float> m_blocks;   // 3 per block
};

class WaveformBandCollector
{
public:
    static constexpr int kVisualSamples = AmplitudeEnvelopeCollector::kVisualSamples;
    static constexpr double kLowMidHz = 600.0;
    static constexpr double kMidHighHz = 4000.0;

    void initialize(long long totalFrames, int channels, int sampleRate)
    {
        m_channels = std::max(1, channels);
        m_low.initialize(sampleRate, kLowMidHz);
        m_bandHp.initialize(sampleRate, kLowMidHz);
        m_bandLp.initialize(sampleRate, kMidHighHz);
        m_high.initialize(sampleRate, kMidHighHz);
        if (totalFrames > 0) {
            m_framesPerBucket = std::max<long long>(1, totalFrames / kVisualSamples);
            m_lowSumSquares.assign(static_cast<size_t>(kVisualSamples), 0.0);
            m_midSumSquares.assign(static_cast<size_t>(kVisualSamples), 0.0);
            m_highSumSquares.assign(static_cast<size_t>(kVisualSamples), 0.0);
            m_sampleCounts.assign(static_cast<size_t>(kVisualSamples), 0);
            m_enabled = true;
        }
    }

    void processSamples(const float* input, long long frames)
    {
        if (!m_enabled) {
            return;
        }
        for (long long i = 0; i < frames; ++i) {
            double sum = 0.0;
            for (int c = 0; c < m_channels; ++c) {
                sum += input[i * m_channels + c];
            }
            const float mono = static_cast<float>(sum / m_channels);

            const float low = m_low.process(mono);
            const float band = m_bandLp.process(m_bandHp.process(mono));
            const float high = m_high.process(mono);

            const size_t bucket = std::min(m_lowSumSquares.size() - 1,
                static_cast<size_t>(m_frameIndex / m_framesPerBucket));
            m_lowSumSquares[bucket] += static_cast<double>(low) * low;
            m_midSumSquares[bucket] += static_cast<double>(band) * band;
            m_highSumSquares[bucket] += static_cast<double>(high) * high;
            ++m_sampleCounts[bucket];
            ++m_frameIndex;
        }
    }

    std::vector<uint8_t> finalizeLow() const { return finalizeBand(m_lowSumSquares); }
    std::vector<uint8_t> finalizeMid() const { return finalizeBand(m_midSumSquares); }
    std::vector<uint8_t> finalizeHigh() const { return finalizeBand(m_highSumSquares); }

private:
    // RMS per bucket, not real mixxx's own peak-based formula (its exact
    // float->byte formula, min(255, floor(255*peak + 0.5)), is preserved
    // only in the final linear scale/round/clamp step below) -- same
    // reasoning as AmplitudeEnvelopeCollector's own comment: peak-picking
    // flattens loud, heavily-mastered material into a near-solid block,
    // RMS shows the organic contour real rekordbox's own preview does.
    std::vector<uint8_t> finalizeBand(const std::vector<double>& sumSquares) const
    {
        if (!m_enabled) {
            return {};
        }
        std::vector<uint8_t> out(sumSquares.size());
        for (size_t i = 0; i < sumSquares.size(); ++i) {
            const double count = m_sampleCounts[i] > 0 ? static_cast<double>(m_sampleCounts[i]) : 1.0;
            const double rms = std::sqrt(sumSquares[i] / count);
            const double clamped = std::min(1.0, rms);
            out[i] = static_cast<uint8_t>(std::min(255.0, 255.0 * clamped + 0.5));
        }
        return out;
    }

    bool m_enabled = false;
    int m_channels = 2;
    long long m_framesPerBucket = 1;
    long long m_frameIndex = 0;
    Butterworth4Lowpass m_low;
    Butterworth4Highpass m_bandHp;
    Butterworth4Lowpass m_bandLp;
    Butterworth4Highpass m_high;
    std::vector<double> m_lowSumSquares, m_midSumSquares, m_highSumSquares;
    std::vector<long long> m_sampleCounts;
};

// ---------------------------------------------------------------------------
// Ported from real mixxx's track/beatutils.{h,cpp} and the real production
// call path track/beatfactory.cpp's BeatFactory::makePreferredBeats() takes
// for a fixed-tempo grid (the default -- BeatDetectionSettings::
// getFixedTempoAssumption() -- and the only kind of grid this port produces):
// retrieveConstRegions() -> makeConstBpm() -> adjustPhase(). This REPLACES an
// earlier version of this port that just averaged BPM across every detected
// beat -- real detector jitter (each beat step has its own +-12ms wobble,
// documented in retrieveConstRegions' own comment below) means a naive
// average almost never lands on the clean round BPM a real track is actually
// produced at (132.629 instead of the real 132.0), which is exactly what
// showed up in the reference library after using this analyzer. Real mixxx
// solves this with a real, specific algorithm -- iron the jittery beats into
// "constant regions" of one true local tempo, find and extend the best-
// supported region, then snap the result to the nearest musically-sensible
// BPM within that region's own measured phase-error tolerance -- so this
// ports that algorithm instead of re-approximating it.
//
// mixxx::audio::FramePos / FrameDiff_t / mixxx::Bpm (typed wrappers this app
// doesn't have) are all plain `double` here -- frame positions and frame
// differences throughout, matching real mixxx's own frame-based arithmetic
// exactly rather than converting to/from seconds partway through.
constexpr double kMaxSecsPhaseError = 0.025;
constexpr double kMaxSecsPhaseErrorSum = 0.1;
constexpr int kMaxOutliersCount = 1;
constexpr int kMinRegionBeatCount = 16;

struct MixHouseConstRegion {
    double firstBeat = 0.0;   // frame position
    double beatLength = 0.0;  // frames
};

// Note: real mixxx's own BeatUtils::calculateBpm() has a short-track (<16
// beats) fallback that calls calculateAverageBpm() instead of the const-
// region pipeline below -- but that's a DIFFERENT, separate public entry
// point (used elsewhere in mixxx, e.g. an on-demand "recalculate BPM for
// selection" tool) from the one this ports: the real analyzer-to-database
// path (AnalyzerBeats::storeResults() -> BeatFactory::makePreferredBeats())
// calls retrieveConstRegions()/makeConstBpm() directly and never goes
// through calculateBpm() or its short-track fallback at all. So there's no
// naive-average fallback ported here on purpose, not by oversight.

// BeatUtils::retrieveConstRegions -- same constants, same two-pointer
// shrink-the-window search, same outlier/drift tolerances.
std::vector<MixHouseConstRegion> mixhouseRetrieveConstRegions(
    const std::vector<double>& coarseBeats, double sampleRate)
{
    if (coarseBeats.size() < 2) {
        return {};
    }

    // The QM Beat detector has a step size of 512 frames @ 44100 Hz. This means
    // Single beats have a jitter of +- 12 ms around the actual position.
    // A pure electronic 120.000 BPM track will have many 120.185 BPM beats and
    // a few 117.454 BPM beats to adjust the collected offset. This function
    // irons these adjustment beats by adjusting every beat to the average of a
    // likely constant region.
    const double maxPhaseError = kMaxSecsPhaseError * sampleRate;
    const double maxPhaseErrorSum = kMaxSecsPhaseErrorSum * sampleRate;
    int leftIndex = 0;
    int rightIndex = static_cast<int>(coarseBeats.size()) - 1;

    std::vector<MixHouseConstRegion> constantRegions;
    while (leftIndex < static_cast<int>(coarseBeats.size()) - 1) {
        const double meanBeatLength =
            (coarseBeats[rightIndex] - coarseBeats[leftIndex]) / (rightIndex - leftIndex);
        int outliersCount = 0;
        double ironedBeat = coarseBeats[leftIndex];
        double phaseErrorSum = 0.0;
        int i = leftIndex + 1;
        for (; i <= rightIndex; ++i) {
            ironedBeat += meanBeatLength;
            const double phaseError = ironedBeat - coarseBeats[i];
            phaseErrorSum += phaseError;
            if (std::fabs(phaseError) > maxPhaseError) {
                ++outliersCount;
                if (outliersCount > kMaxOutliersCount || i == leftIndex + 1) {
                    // the first beat must not be an outlier -- region is not const.
                    break;
                }
            }
            if (std::fabs(phaseErrorSum) > maxPhaseErrorSum) {
                // we drift away in one direction, the meanBeatLength is not optimal.
                break;
            }
        }
        if (i > rightIndex) {
            // Verify the first and last beat aren't correction beats in the
            // same direction -- that would bend meanBeatLength unfavorably.
            double regionBorderError = 0.0;
            if (rightIndex > leftIndex + 2) {
                const double firstBeatLength = coarseBeats[leftIndex + 1] - coarseBeats[leftIndex];
                const double lastBeatLength = coarseBeats[rightIndex] - coarseBeats[rightIndex - 1];
                regionBorderError =
                    std::fabs(firstBeatLength + lastBeatLength - (2 * meanBeatLength));
            }
            if (regionBorderError < maxPhaseError / 2) {
                constantRegions.push_back({coarseBeats[leftIndex], meanBeatLength});
                leftIndex = rightIndex;
                rightIndex = static_cast<int>(coarseBeats.size()) - 1;
                continue;
            }
        }
        // Try a by-one-beat smaller region.
        --rightIndex;
    }

    // A final region with zero length to mark the end.
    constantRegions.push_back({coarseBeats.back(), 0.0});
    return constantRegions;
}

bool mixhouseTrySnap(double minBpm, double centerBpm, double maxBpm, double fraction, double* outBpm)
{
    const double snapBpm = std::round(centerBpm * fraction) / fraction;
    if (snapBpm > minBpm && snapBpm < maxBpm) {
        *outBpm = snapBpm;
        return true;
    }
    return false;
}

// BeatUtils::roundBpmWithinRange -- try a full integer BPM first, then
// halves (only under 85, where a double-time misdetection is plausible),
// then 2/3 (only over 127, optimizing for the classic 174-going-to-116-ish
// halftime/2:3 relationship), then thirds, then twelfths; give up and return
// the unsnapped center if nothing in range matches any of those.
double mixhouseRoundBpmWithinRange(double minBpm, double centerBpm, double maxBpm)
{
    double snap = 0.0;
    if (mixhouseTrySnap(minBpm, centerBpm, maxBpm, 1.0, &snap)) {
        return snap;
    }
    if (centerBpm < 85.0 && mixhouseTrySnap(minBpm, centerBpm, maxBpm, 2.0, &snap)) {
        return snap;
    }
    if (centerBpm > 127.0 && mixhouseTrySnap(minBpm, centerBpm, maxBpm, 2.0 / 3.0, &snap)) {
        return snap;
    }
    if (mixhouseTrySnap(minBpm, centerBpm, maxBpm, 3.0, &snap)) {
        return snap;
    }
    if (mixhouseTrySnap(minBpm, centerBpm, maxBpm, 12.0, &snap)) {
        return snap;
    }
    return centerBpm;
}

// BeatUtils::makeConstBpm -- find the longest constant region, try to extend
// it with a matching-tempo-and-phase region before and after it, then snap
// the result. Returns 0.0 (invalid) if no tempo could be inferred, matching
// real mixxx's default-invalid mixxx::Bpm{}.
double mixhouseMakeConstBpm(const std::vector<MixHouseConstRegion>& constantRegions,
                             double sampleRate, double* outFirstBeat)
{
    int midRegionIndex = 0;
    double longestRegionLength = 0.0;
    double longestRegionBeatLength = 0.0;
    for (int i = 0; i < static_cast<int>(constantRegions.size()) - 1; ++i) {
        const double length = constantRegions[i + 1].firstBeat - constantRegions[i].firstBeat;
        if (length > longestRegionLength) {
            longestRegionLength = length;
            longestRegionBeatLength = constantRegions[i].beatLength;
            midRegionIndex = i;
        }
    }
    if (longestRegionLength == 0.0) {
        return 0.0;
    }

    int longestRegionNumberOfBeats =
        static_cast<int>((longestRegionLength / longestRegionBeatLength) + 0.5);
    double longestRegionBeatLengthMin = longestRegionBeatLength
        - ((kMaxSecsPhaseError * sampleRate) / longestRegionNumberOfBeats);
    double longestRegionBeatLengthMax = longestRegionBeatLength
        + ((kMaxSecsPhaseError * sampleRate) / longestRegionNumberOfBeats);

    int startRegionIndex = midRegionIndex;

    // Find a region at the beginning of the track with a similar tempo/phase.
    for (int i = 0; i < midRegionIndex; ++i) {
        const double length = constantRegions[i + 1].firstBeat - constantRegions[i].firstBeat;
        const int numberOfBeats = static_cast<int>((length / constantRegions[i].beatLength) + 0.5);
        if (numberOfBeats < kMinRegionBeatCount) {
            continue;
        }
        const double thisMin = constantRegions[i].beatLength
            - ((kMaxSecsPhaseError * sampleRate) / numberOfBeats);
        const double thisMax = constantRegions[i].beatLength
            + ((kMaxSecsPhaseError * sampleRate) / numberOfBeats);
        if (longestRegionBeatLength > thisMin && longestRegionBeatLength < thisMax) {
            const double newLongestRegionLength =
                constantRegions[midRegionIndex + 1].firstBeat - constantRegions[i].firstBeat;
            const double beatLengthMin = std::max(longestRegionBeatLengthMin, thisMin);
            const double beatLengthMax = std::min(longestRegionBeatLengthMax, thisMax);
            const int maxNumberOfBeats = static_cast<int>(std::round(newLongestRegionLength / beatLengthMin));
            const int minNumberOfBeats = static_cast<int>(std::round(newLongestRegionLength / beatLengthMax));
            if (minNumberOfBeats != maxNumberOfBeats) {
                continue;
            }
            const double newBeatLength = newLongestRegionLength / minNumberOfBeats;
            if (newBeatLength > longestRegionBeatLengthMin && newBeatLength < longestRegionBeatLengthMax) {
                longestRegionLength = newLongestRegionLength;
                longestRegionBeatLength = newBeatLength;
                longestRegionNumberOfBeats = minNumberOfBeats;
                longestRegionBeatLengthMin = longestRegionBeatLength
                    - ((kMaxSecsPhaseError * sampleRate) / longestRegionNumberOfBeats);
                longestRegionBeatLengthMax = longestRegionBeatLength
                    + ((kMaxSecsPhaseError * sampleRate) / longestRegionNumberOfBeats);
                startRegionIndex = i;
                break;
            }
        }
    }

    // Find a region at the end of the track with a similar tempo/phase.
    for (int i = static_cast<int>(constantRegions.size()) - 2; i > midRegionIndex; --i) {
        const double length = constantRegions[i + 1].firstBeat - constantRegions[i].firstBeat;
        const int numberOfBeats = static_cast<int>((length / constantRegions[i].beatLength) + 0.5);
        if (numberOfBeats < kMinRegionBeatCount) {
            continue;
        }
        const double thisMin = constantRegions[i].beatLength
            - ((kMaxSecsPhaseError * sampleRate) / numberOfBeats);
        const double thisMax = constantRegions[i].beatLength
            + ((kMaxSecsPhaseError * sampleRate) / numberOfBeats);
        if (longestRegionBeatLength > thisMin && longestRegionBeatLength < thisMax) {
            const double newLongestRegionLength =
                constantRegions[i + 1].firstBeat - constantRegions[startRegionIndex].firstBeat;
            const double minBeatLength = std::max(longestRegionBeatLengthMin, thisMin);
            const double maxBeatLength = std::min(longestRegionBeatLengthMax, thisMax);
            const int maxNumberOfBeats = static_cast<int>(std::round(newLongestRegionLength / minBeatLength));
            const int minNumberOfBeats = static_cast<int>(std::round(newLongestRegionLength / maxBeatLength));
            if (minNumberOfBeats != maxNumberOfBeats) {
                continue;
            }
            const double newBeatLength = newLongestRegionLength / minNumberOfBeats;
            if (newBeatLength > longestRegionBeatLengthMin && newBeatLength < longestRegionBeatLengthMax) {
                longestRegionLength = newLongestRegionLength;
                longestRegionBeatLength = newBeatLength;
                longestRegionNumberOfBeats = minNumberOfBeats;
                break;
            }
        }
    }

    longestRegionBeatLengthMin = longestRegionBeatLength
        - ((kMaxSecsPhaseError * sampleRate) / longestRegionNumberOfBeats);
    longestRegionBeatLengthMax = longestRegionBeatLength
        + ((kMaxSecsPhaseError * sampleRate) / longestRegionNumberOfBeats);

    const double minRoundBpm = 60.0 * sampleRate / longestRegionBeatLengthMax;
    const double maxRoundBpm = 60.0 * sampleRate / longestRegionBeatLengthMin;
    const double centerBpm = 60.0 * sampleRate / longestRegionBeatLength;
    const double roundBpm = mixhouseRoundBpmWithinRange(minRoundBpm, centerBpm, maxRoundBpm);

    if (outFirstBeat) {
        // Move the first beat as close to the start of the track as we can --
        // this is a constant grid, so "first beat" only matters as the phase
        // anchor bpm adjustments are made from.
        const double roundedBeatLength = 60.0 * sampleRate / roundBpm;
        *outFirstBeat = std::fmod(constantRegions[startRegionIndex].firstBeat, roundedBeatLength);
    }
    return roundBpm;
}

// BeatUtils::adjustPhase -- refine the anchor using every original detected
// beat (not just the winning const region), averaging the offset of every
// beat that lands within tolerance of the chosen grid.
double mixhouseAdjustPhase(double firstBeat, double bpm, double sampleRate,
                            const std::vector<double>& beats)
{
    const double beatLength = 60.0 * sampleRate / bpm;
    const double startOffset = std::fmod(firstBeat, beatLength);
    double offsetAdjust = 0.0;
    double offsetAdjustCount = 0.0;
    for (double beat : beats) {
        double offset = std::fmod(beat - startOffset, beatLength);
        if (offset > beatLength / 2.0) {
            offset -= beatLength;
        }
        if (std::fabs(offset) < (kMaxSecsPhaseError * sampleRate)) {
            offsetAdjust += offset;
            offsetAdjustCount += 1.0;
        }
    }
    if (offsetAdjustCount > 0.0) {
        offsetAdjust /= offsetAdjustCount;
    }
    return firstBeat + offsetAdjust;
}

// BeatFactory::makePreferredBeats's BEAT_GRID_2_VERSION branch, the fixed-
// tempo path this port always takes: retrieveConstRegions -> makeConstBpm ->
// adjustPhase. Returns bpm=0.0 (invalid) if no tempo could be inferred.
void mixhouseCalculateBpmAndFirstBeat(const std::vector<double>& beatFrames, double sampleRate,
                                       double* outBpm, double* outFirstBeatFrame)
{
    *outBpm = 0.0;
    *outFirstBeatFrame = 0.0;
    const std::vector<MixHouseConstRegion> constantRegions =
        mixhouseRetrieveConstRegions(beatFrames, sampleRate);
    if (constantRegions.empty()) {
        return;
    }
    double firstBeat = 0.0;
    const double bpm = mixhouseMakeConstBpm(constantRegions, sampleRate, &firstBeat);
    if (bpm <= 0.0) {
        return;
    }
    firstBeat = mixhouseAdjustPhase(firstBeat, bpm, sampleRate, beatFrames);
    *outBpm = bpm;
    *outFirstBeatFrame = std::round(firstBeat);
}

}  // namespace

namespace {
std::atomic<bool> s_keyDetectionEnabled { true };
}

void au::mixhouse::setMixHouseAccurateBeatAnalysis(bool accurate)
{
    s_accurateBeatAnalysisFlag = accurate;
}

void au::mixhouse::setMixHouseKeyDetectionEnabled(bool enabled)
{
    s_keyDetectionEnabled = enabled;
}

MixHouseAnalysisResult au::mixhouse::analyzeTrackFile(const QString& filePath,
                                                     const MixHouseAnalyzerProgressFn& onProgress,
                                                     const MixHouseKnownAnalysis& known)
{
    MixHouseAnalysisResult result;
    // GlideMix: a grid from rekordbox/Lexicon skips the beat network.
    const bool knownGrid = known.bpm > 0.0;

    ANALYZER_TIMING_START();
    const AudioInfo info = probeAudioFile(filePath);
    if (!info.valid || info.sampleRate <= 0 || info.channels <= 0) {
        result.error = info.error.isEmpty()
            ? QStringLiteral("Could not read audio info for %1").arg(filePath)
            : info.error;
        return result;
    }

    BeatsDetector beats;
    beats.setChannels(info.channels);
    beats.initialize(info.sampleRate);

    // Whole-track amplitude envelope (signal_all) -- initialized from the
    // probe's own frame estimate so it can bucket correctly without a
    // second pass.
    AmplitudeEnvelopeCollector envelope;
    envelope.initialize(info.frames, info.channels);

    // The real 3-band split (signal_filtered.low/mid/high) -- see
    // WaveformBandCollector's own comment.
    WaveformBandCollector bands;
    PhraseEnergyCollector phraseEnergy;
    phraseEnergy.initialize(info.channels, info.sampleRate);
    bands.initialize(info.frames, info.channels, info.sampleRate);
    SoundsLikeCollector soundsLike;
    soundsLike.initialize(info.channels, info.sampleRate);

#ifdef MIXHOUSE_HAVE_BEAT_THIS
    // Whole-track mono copy for Beat This! (the model needs the full
    // spectrogram at once, it can't stream); it resamples to 22.05kHz itself.
    std::vector<float> monoForDownbeats;
    if (info.frames > 0) {
        monoForDownbeats.reserve(static_cast<size_t>(info.frames));
    }
#endif

    // Mean-square loudness per kOnsetBlockSec block, to find where the music
    // starts (see kDownbeatFromBeatThis).
    std::vector<float> onsetBlocks;
    const long long onsetBlockFrames = std::max<long long>(1, static_cast<long long>(kOnsetBlockSec * info.sampleRate));
    double onsetBlockSum = 0.0;
    long long onsetBlockFill = 0;

    double totalFrames = 0.0;
    double loudestFrame = 0.0;   // mean square, for "is there any sound at all"
    const AudioInfo decodeInfo = decodeAudioFile(filePath, [&](const float* samples, long long frames) {
        for (long long i = 0; i < frames; ++i) {
            double frameSum = 0.0;
            for (int ch = 0; ch < info.channels; ++ch) {
                const double v = samples[i * info.channels + ch];
                frameSum += v * v;
            }
            onsetBlockSum += frameSum / std::max(1, info.channels);
            loudestFrame = std::max(loudestFrame, frameSum / std::max(1, info.channels));
            if (++onsetBlockFill == onsetBlockFrames) {
                onsetBlocks.push_back(static_cast<float>(onsetBlockSum / onsetBlockFrames));
                onsetBlockSum = 0.0;
                onsetBlockFill = 0;
            }
        }
        totalFrames += static_cast<double>(frames);
        // Real mixxx's AnalyzerThread: one shared decode pass feeds every
        // active analyzer the SAME chunk -- not an independent decode per
        // analyzer. Key detection is no longer one of these in-process
        // sub-analyzers (see OpenKeyScanClient's own header comment) -- it
        // runs as a separate subprocess call, by file path, after this
        // decode pass finishes.
        const bool okBeats = beats.processSamples(samples, frames);
        envelope.processSamples(samples, frames);
        bands.processSamples(samples, frames);
        phraseEnergy.processSamples(samples, frames);
        soundsLike.processSamples(samples, frames);
#ifdef MIXHOUSE_HAVE_BEAT_THIS
        for (long long i = 0; !knownGrid && (kDownbeatFromBeatThis || kGridFromBeatThis) && i < frames; ++i) {
            float sum = 0.0f;
            for (int ch = 0; ch < info.channels; ++ch) {
                sum += samples[i * info.channels + ch];
            }
            monoForDownbeats.push_back(sum / static_cast<float>(info.channels));
        }
#endif
        // Real mixxx AnalyzerThread::analyzeTrack's "3rd step": frame
        // progress scaled into [None, Finalizing], capped at Finalizing.
        // No frame count up front means no percentage -- stays at None
        // until the Finalizing report below, rather than a made-up number.
        if (onProgress && info.frames > 0) {
            // GlideMix: decoding is the smaller part when the beat network
            // runs after it (it sat at 95% for most of the analysis).
            const double share = knownGrid ? 1.0 : 0.4;
            const double frameProgress = totalFrames / static_cast<double>(info.frames);
            onProgress(std::min(kMixHouseAnalyzerProgressFinalizing,
                                share * frameProgress * (kMixHouseAnalyzerProgressFinalizing - kMixHouseAnalyzerProgressNone)));
        }
        return okBeats;
    });

    if (onProgress && decodeInfo.valid) {
        onProgress(knownGrid ? kMixHouseAnalyzerProgressFinalizing : 0.45 * kMixHouseAnalyzerProgressFinalizing);
    }

    if (!decodeInfo.valid) {
        result.error = decodeInfo.error.isEmpty()
            ? QStringLiteral("Could not decode %1").arg(filePath)
            : decodeInfo.error;
        return result;
    }

    const std::vector<double> beatFrames = beats.finalize();

    // Real neural key detection (OpenKeyScan), by file path, over the
    // subprocess -- not fed from the decode loop above like BPM/waveform
    // are, since it does its own independent audio load + CQT spectrogram
    // extraction (see OpenKeyScanClient's own header comment for why this
    // isn't ported into this decode pass in-process). A failure here (the
    // subprocess not running, a timeout, an unsupported file) degrades to
    // keyId=0 -- treated the same as "no key detected", a real, already-
    // documented possibility for this struct, rather than failing the
    // whole analysis (BPM/waveform are still valid and get written).
    ANALYZER_TIMING_MARK("decode+qm+waveform");
    // GlideMix: silence (nothing above about -60 dB) has no key -- the
    // analyzer still names one.
    const bool silent = loudestFrame < 1e-6;
    const bool wantKey = !silent && s_keyDetectionEnabled.load() && known.keyId <= 0;
    const OpenKeyScanResult keyResult = wantKey ? OpenKeyScanClient::instance().detectKey(filePath) : OpenKeyScanResult {};
    if (!keyResult.valid && wantKey) {
        LOGW() << "OpenKeyScan key detection failed for " << filePath << ": " << keyResult.error;
    }

    result.valid = true;
    result.sampleRate = info.sampleRate;
    // GlideMix: the length actually decoded. A file cut short (an interrupted
    // download or copy) keeps its header's full length, so the library said
    // 3:07 for 9 seconds of audio.
    const double decodedSec = info.sampleRate > 0 ? static_cast<double>(totalFrames) / info.sampleRate : 0.0;
    result.durationSec = decodedSec > 0.0 && std::abs(decodedSec - info.durationSec) > 1.0 ? decodedSec : info.durationSec;
    result.keyId = keyResult.valid ? keyResult.keyId : (known.keyId > 0 ? known.keyId : 0);
    result.waveformOverview = envelope.finalize();
    result.waveformLow = bands.finalizeLow();
    result.waveformMid = bands.finalizeMid();
    result.waveformHigh = bands.finalizeHigh();
    // GlideMix: a file that ended well before its header said (cut short)
    // filled only the start of its overview: stretch what was decoded over
    // the whole width, which is its real length now.
    if (info.frames > 0 && totalFrames > 0.0 && totalFrames < 0.98 * static_cast<double>(info.frames)) {
        const auto stretch = [&](std::vector<uint8_t>& v) {
            if (v.size() < 2) {
                return;
            }
            const size_t used = std::clamp<size_t>(
                static_cast<size_t>(std::ceil(totalFrames / static_cast<double>(info.frames) * v.size())), 1, v.size());
            std::vector<uint8_t> out(v.size());
            for (size_t i = 0; i < v.size(); ++i) {
                out[i] = v[std::min(used - 1, i * used / v.size())];
            }
            v = std::move(out);
        };
        stretch(result.waveformOverview);
        stretch(result.waveformLow);
        stretch(result.waveformMid);
        stretch(result.waveformHigh);
    }

    if (beatFrames.size() >= 2) {
        // Real mixxx's own BeatFactory::makePreferredBeats() fixed-tempo path
        // (mixhouseCalculateBpmAndFirstBeat above), not a naive average --
        // see that function's own comment for why the difference matters. A
        // full variable-tempo BeatMap (mixxx's OTHER branch, for tracks with
        // genuinely shifting tempo) is a real, separate, disclosed follow-up:
        // this always resolves to one constant-tempo grid, matching what
        // this app's own "Adjust BPM" feature already assumes (a single bpm
        // value, not a variable map).
        double bpm = 0.0;
        double firstBeatFrame = 0.0;
        mixhouseCalculateBpmAndFirstBeat(beatFrames, info.sampleRate, &bpm, &firstBeatFrame);
        result.bpm = bpm;
        result.firstBeatSec = firstBeatFrame / info.sampleRate;
    }

    // Where the music starts.
    double musicStartSec = 0.0;
    if (!onsetBlocks.empty()) {
        const float loudest = *std::max_element(onsetBlocks.begin(), onsetBlocks.end());
        size_t onsetBlock = 0;
        while (onsetBlock < onsetBlocks.size() && onsetBlocks[onsetBlock] < loudest * kOnsetRelativeRms * kOnsetRelativeRms) {
            ++onsetBlock;
        }
        musicStartSec = static_cast<double>(onsetBlock) * kOnsetBlockSec;
    }

    // Bar 1 = the first grid beat at the start of the music.
    if (result.bpm > 0.0 && !onsetBlocks.empty()) {
        const double onsetSec = musicStartSec;
        const double beatSec = 60.0 / result.bpm;
        const double beatsToOnset = std::ceil((onsetSec - result.firstBeatSec) / beatSec - kOnsetBeatTolerance);
        result.downbeatSec = result.firstBeatSec + beatsToOnset * beatSec;
        result.downbeatConfidence = 1.0;
        result.downbeatDetector = QStringLiteral("FirstBeat-1");
    }

    if (knownGrid) {
        result.bpm = known.bpm;
        result.firstBeatSec = known.firstBeatSec;
        if (known.downbeatSec >= 0.0) {
            result.downbeatSec = known.downbeatSec;
            result.downbeatConfidence = 1.0;
        }
        result.downbeatDetector = known.source;
    }

    if (onProgress && !knownGrid) {
        onProgress(0.5 * kMixHouseAnalyzerProgressFinalizing);
    }
    ANALYZER_TIMING_MARK("key+grid");
#ifdef MIXHOUSE_HAVE_BEAT_THIS
    if (kGridFromBeatThis && !monoForDownbeats.empty()) {
        if (auto model = BeatThisModelPool::instance().acquire()) {
            try {
                const BeatThis::BeatResult beatThis = beatThisWindows(*model, monoForDownbeats, info.sampleRate);
                const DjGrid grid = fitDjGrid(beatThis.beats, beatThis.downbeats);
                if (grid.valid && grid.coherence >= kDjGridMinCoherence) {
                    const double beatSec = 60.0 / grid.bpm;
                    const double barSec = kBeatsPerBar * beatSec;
                    result.bpm = grid.bpm;
                    result.firstBeatSec = grid.barStartSec - std::floor(grid.barStartSec / beatSec) * beatSec;
                    // Bar 1: the first of its bars that starts where the music does.
                    const double first = grid.barStartSec - std::floor(grid.barStartSec / barSec) * barSec;
                    const double bars = std::ceil((musicStartSec - first) / barSec - kOnsetBeatTolerance / kBeatsPerBar);
                    result.downbeatSec = first + std::max(0.0, bars) * barSec;
                    result.downbeatConfidence = grid.coherence;
                    result.downbeatDetector = QStringLiteral("BeatThis-Grid-1");
                }
            } catch (const std::exception& e) {
                LOGW() << "Beat This! failed for " << filePath << ": " << e.what();
            }
            BeatThisModelPool::instance().release(std::move(model));
        }
        if (!kDownbeatFromBeatThis) {
            std::vector<float>().swap(monoForDownbeats);
        }
    }
    if (kDownbeatFromBeatThis && !monoForDownbeats.empty()) {
        if (auto model = BeatThisModelPool::instance().acquire()) {
            try {
                const size_t windowLen = static_cast<size_t>(kDownbeatWindowSec * info.sampleRate);
                DownbeatVotes votes;
                auto runWindow = [&](size_t start, size_t len) {
                    const BeatThis::BeatResult beatThis = model->process_audio(
                        monoForDownbeats.data() + start, len, info.sampleRate, 1);
                    addDownbeatVotes(votes, beatThis.downbeats, static_cast<double>(start) / info.sampleRate,
                                     result.bpm, result.firstBeatSec);
                };
                if (monoForDownbeats.size() <= windowLen) {
                    runWindow(0, monoForDownbeats.size());   // short track: all of it
                } else {
                    const size_t first = loudestWindowStart(monoForDownbeats, windowLen, info.sampleRate);
                    runWindow(first, windowLen);
                    DownbeatEstimate firstPass = resolveDownbeat(votes, result.bpm, result.firstBeatSec);
                    if (votes.total < kDownbeatMinVotes || firstPass.confidence < kDownbeatConfidentShare) {
                        const size_t second = loudestWindowStart(monoForDownbeats, windowLen, info.sampleRate,
                                                                 first, first + windowLen);
                        if (second < monoForDownbeats.size()) {
                            runWindow(second, windowLen);
                        }
                    }
                }
                const DownbeatEstimate downbeat = resolveDownbeat(votes, result.bpm, result.firstBeatSec);
                result.downbeatSec = downbeat.sec;
                result.downbeatConfidence = downbeat.confidence;
                result.downbeatDetector = QStringLiteral("BeatThis-DBN-1");
            } catch (const std::exception& e) {
                LOGW() << "Beat This! downbeat detection failed for " << filePath << ": " << e.what();
            }
            BeatThisModelPool::instance().release(std::move(model));
        }
        std::vector<float>().swap(monoForDownbeats);   // ~35MB for a 3-4 minute track
    }
#endif

    if (onProgress) {
        onProgress(kMixHouseAnalyzerProgressFinalizing);
    }
    ANALYZER_TIMING_MARK("downbeat");
    result.beatBandEnergy = phraseEnergy.perBeat(result.bpm, result.firstBeatSec);
    result.energyFeatures = phraseEnergy.energyFeatures(result.bpm);
    result.energy = rateEnergy(result.energyFeatures);
    result.soundsLike = soundsLike.finalize(result.energyFeatures);
    if (!result.beatBandEnergy.empty()) {
        // The loudness detector's phrases are one of the model's inputs.
        const double downbeat = result.downbeatSec >= 0.0 ? result.downbeatSec : result.firstBeatSec;
        const MixHouseWaveformOverview overview = decodeWaveformOverview(qUncompress(
            encodeWaveformOverview(result.waveformOverview, result.waveformLow, result.waveformMid, result.waveformHigh)));
        const std::vector<MixHousePhrase> builtIn = detectPhrases(overview, result.durationSec, result.bpm, downbeat);
        result.modelPhrases = modelPhrases(result.beatBandEnergy, result.bpm, result.firstBeatSec, downbeat,
                                           result.durationSec, builtIn);
    }
    return result;
}

std::vector<float> au::mixhouse::analyzeSoundsLike(const QString& filePath)
{
    const AudioInfo info = probeAudioFile(filePath);
    if (!info.valid || info.sampleRate <= 0 || info.channels <= 0) {
        return {};
    }
    PhraseEnergyCollector phraseEnergy;
    phraseEnergy.initialize(info.channels, info.sampleRate);
    SoundsLikeCollector soundsLike;
    soundsLike.initialize(info.channels, info.sampleRate);
    const AudioInfo decodeInfo = decodeAudioFile(filePath, [&](const float* samples, long long frames) {
        phraseEnergy.processSamples(samples, frames);
        soundsLike.processSamples(samples, frames);
        return true;
    });
    if (!decodeInfo.valid) {
        return {};
    }
    // The groove values (energy features 4-9) don't depend on the tempo.
    return soundsLike.finalize(phraseEnergy.energyFeatures(0.0));
}
