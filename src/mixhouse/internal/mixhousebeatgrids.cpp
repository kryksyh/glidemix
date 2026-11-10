/*
 * GlideMix
 */
#include "mixhousebeatgrids.h"

#include <QCoreApplication>
#include <QHash>
#include <QFileInfo>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <thread>

#include "au3-mixer/Envelope.h"
#include "au3-wave-track/WaveClip.h"
#include "au3-wave-track/WaveTrack.h"
#include "au3wrap/internal/domaccessor.h"
#include "project/iaudacityproject.h"
#include "projectscene/iprojectviewstate.h"
#include "log.h"
#include "translation.h"

#include "mixhouseanalyzer.h"
#include "mixhouse/mixhousephrasedetector.h"
#include "mixhouseprojectaligner.h"

using namespace au::mixhouse;

namespace {
// Mixxx's own grid steps (engine/controls/bpmcontrol.cpp): a translate is
// sampleRate * 0.01 samples split over two channels, i.e. 5 ms; a tempo
// adjust is kBpmAdjustStep = 0.01 BPM.
constexpr double kTranslateStepSec = 0.005;
constexpr double kBpmAdjustStep = 0.01;
constexpr size_t kMaxUndo = 200;
// A clip's tick loop is bounded so a zoomed-out hour-long mix can't stall a paint.
constexpr int kMaxTicks = 20000;
}

MixHouseBeatGrids::MixHouseBeatGrids(const muse::modularity::ContextPtr& iocCtx)
    : muse::Contextable(iocCtx), m_alive(std::make_shared<std::atomic<bool> >(true))
{
}

MixHouseBeatGrids::~MixHouseBeatGrids()
{
    *m_alive = false;
}

void MixHouseBeatGrids::applyRekordboxGrids()
{
    // rekordbox is the reference grid (its phrases count its beats, and it's
    // what we check against). The library's own analysis puts about a
    // third of the tracks half a beat off, on the offbeat (measured
    // 2026-09-15: 330 of 1118 tracks both apps analysed). Tracks whose grid
    // was set by hand ("manual") or is locked keep theirs.
    if (m_rekordboxPassRunning || !library() || !library()->isAvailable() || !rekordboxPhrases()) {
        return;
    }
    struct Job {
        int trackId;
        QString filePath;
        double bpm;
        double downbeatSec;
    };
    std::vector<Job> jobs;
    IMixHouseLibrary::TrackQuery query;   // no limit: the whole library
    for (const MixHouseTrack& t : library()->tracks(query)) {
        if (t.bpmLocked || t.filePath.isEmpty() || t.downbeatDetector == QStringLiteral("manual")
            || t.downbeatDetector == QStringLiteral("rekordbox")) {
            continue;
        }
        jobs.push_back({ t.id, t.filePath, t.bpm, t.downbeatSec });
    }
    if (jobs.empty()) {
        return;
    }
    m_rekordboxPassRunning = true;
    const auto alive = m_alive;
    const auto phrases = rekordboxPhrases();
    std::thread([this, alive, phrases, jobs = std::move(jobs)]() {
        std::vector<IMixHouseLibrary::BeatGridEdit> edits;
        for (const Job& job : jobs) {
            if (!*alive) {
                return;
            }
            const MixHouseRekordboxGrid rb = phrases->beatGrid(job.filePath, job.bpm);
            if (!rb.valid) {
                continue;
            }
            // Bar 1: rekordbox's downbeat nearest the one the library found at
            // the music's start (that one may sit on the offbeat, but it's in
            // the right bar); none found -> rekordbox's first downbeat.
            const double bar = 4.0 * 60.0 / rb.bpm;
            double downbeat = rb.firstDownbeatSec;
            if (job.downbeatSec >= 0.0) {
                downbeat += std::max(0.0, std::round((job.downbeatSec - rb.firstDownbeatSec) / bar)) * bar;
            }
            IMixHouseLibrary::BeatGridEdit edit;
            edit.trackId = job.trackId;
            edit.bpm = rb.bpm;
            edit.firstBeatSec = rb.firstBeatSec;
            edit.downbeatSec = downbeat;
            edit.detector = QStringLiteral("rekordbox");
            edits.push_back(edit);
        }
        if (!*alive) {
            return;
        }
        QTimer::singleShot(0, qApp, [this, alive, edits = std::move(edits), scanned = jobs.size()]() {
            if (!*alive) {
                return;
            }
            m_rekordboxPassRunning = false;
            const int written = edits.empty() ? 0 : library()->setTrackBeatGrids(edits);
            LOGI() << "GlideMix: rekordbox beat grids applied to " << written << " of " << scanned << " tracks";
            m_gridByClipName.clear();
            m_beatGridsChanged.notify();
        });
    }).detach();
}

void MixHouseBeatGrids::init()
{
    if (library()) {
        library()->libraryChanged().onNotify(this, [this]() {
            m_gridByClipName.clear();
            m_songEnergy.clear();
            m_anchorCache.clear();
            m_waveCache.clear();
            m_beatGridsChanged.notify();
        });
    }
    // An external library finished (re)reading, or rekordbox's phrases are
    // indexed: clips may gain grids, cues and phrases.
    if (rekordboxXml()) {
        rekordboxXml()->changed().onNotify(this, [this]() {
            m_gridByClipName.clear();
            m_songEnergy.clear();
            m_beatGridsChanged.notify();
        });
    }
    if (rekordboxPhrases()) {
        rekordboxPhrases()->ready().onNotify(this, [this]() {
            m_gridByClipName.clear();
            // Its colour waveforms are readable now too.
            m_waveCache.clear();
            m_beatGridsChanged.notify();
            applyRekordboxGrids();
        });
    }
    // Re-analysis writes the library's own grid again: take rekordbox's back.
    if (library()) {
        library()->analysisFinished().onReceive(this, [this](const IMixHouseLibrary::AnalysisSummary&) {
            applyRekordboxGrids();
        });
    }
    // Show / Hide Phrase Detection repaints the clips.
    if (libraryConfiguration()) {
        libraryConfiguration()->settingsChanged().onNotify(this, [this]() {
            if (m_phraseRibbonVisible != libraryConfiguration()->phraseRibbonVisible()) {
                m_phraseRibbonVisible = libraryConfiguration()->phraseRibbonVisible();
                m_beatGridsChanged.notify();
            }
        });
        m_phraseRibbonVisible = libraryConfiguration()->phraseRibbonVisible();
    }
}

muse::async::Notification MixHouseBeatGrids::beatGridsChanged() const
{
    return m_beatGridsChanged;
}

int MixHouseBeatGrids::clipLibraryTrackId(const trackedit::ClipKey& clip) const
{
    const Grid grid = gridForClipName(clipName(clip), false);
    return grid.trackId >= 0 ? grid.trackId : -1;
}

// GlideMix: the colour waveform of a clip, the way rekordbox draws it on
// a deck -- blue bass, orange mids, white highs. The colours are rekordbox's
// own where the track has been analysed by it (PWV5, 150 columns a second);
// otherwise this app's analysis, which only has a colour every eighth of a
// second, so the lane is coarser but still reads.
std::vector<IMixHouseBeatGrids::WaveColumn> MixHouseBeatGrids::clipWaveColumns(const trackedit::ClipKey& key,
                                                                              double fromTime, double toTime,
                                                                              int columns) const
{
    std::vector<WaveColumn> out;
    if (columns <= 0 || toTime <= fromTime) {
        return out;
    }
    const Grid grid = gridForClipName(clipName(key), false);
    if (grid.filePath.isEmpty()) {
        return out;
    }

    auto cached = m_waveCache.constFind(grid.filePath);
    if (cached == m_waveCache.constEnd()) {
        MixHouseWaveformOverview wave;
        if (rekordboxPhrases()) {
            wave = rekordboxPhrases()->waveformDetail(grid.filePath, grid.bpm);
        }
        if (!wave.valid && library() && grid.trackId >= 0) {
            wave = decodeWaveformOverview(library()->overviewAnalysisBlob(grid.trackId));
        }
        cached = m_waveCache.insert(grid.filePath, wave);
    }
    if (!cached->valid) {
        return out;
    }
    // Two interleaved channels per stored column (the overview format).
    const size_t stored = cached->all.size() / 2;
    if (stored == 0) {
        return out;
    }

    const auto project = globalContext() ? globalContext()->currentProject() : nullptr;
    if (!project) {
        return out;
    }
    auto* au3Project = reinterpret_cast<AudacityProject*>(project->au3ProjectPtr());
    WaveTrack* track = au::au3::DomAccessor::findWaveTrack(*au3Project, ::TrackId(key.trackId));
    const auto clip = track ? au::au3::DomAccessor::findWaveClip(track, key.itemId) : nullptr;
    if (!clip) {
        return out;
    }
    // Same mapping as the beat ticks: file second `a` plays at
    // sequenceStart + a * ratio. The stored columns span the whole file.
    const double ratio = clip->GetStretchRatio();
    const double sequenceStart = clip->GetPlayStartTime() - clip->GetTrimLeft();
    // The sequence spans the whole imported file, which is what the stored
    // columns cover; its project length divides out the stretch.
    const double fileSeconds = (clip->GetSequenceEndTime() - clip->GetSequenceStartTime()) / ratio;
    if (ratio <= 0.0 || fileSeconds <= 0.0) {
        return out;
    }
    const double columnsPerFileSecond = static_cast<double>(stored) / fileSeconds;

    const auto colourOf = [&](size_t i) -> uint32_t {
        if (i < cached->rgb.size()) {
            return cached->rgb[i];
        }
        // No stored colour (this app's own analysis): mix rekordbox's own band
        // colours -- blue bass, orange mids, white highs -- by how much of
        // each band is there, against the loudest of the three.
        const auto band = [&](const std::vector<uint8_t>& v) -> double {
            const size_t a = 2 * i;
            if (a + 1 < v.size()) {
                return std::max(v[a], v[a + 1]) / 255.0;
            }
            return a < v.size() ? v[a] / 255.0 : 0.0;
        };
        const double low = band(cached->low);
        const double mid = band(cached->mid);
        const double high = band(cached->high);
        const double peak = std::max({ low, mid, high, 1e-6 });
        const auto mix = [&](int lowC, int midC, int highC) {
            return static_cast<uint32_t>(std::lround(std::clamp((low * lowC + mid * midC + high * highC) / peak, 0.0, 255.0)));
        };
        return (mix(0x2A, 0xFF, 0xF2) << 16) | (mix(0x6B, 0x9A, 0xF2) << 8) | mix(0xFF, 0x1A, 0xF2);
    };

    out.assign(static_cast<size_t>(columns), WaveColumn {});
    const double span = (toTime - fromTime) / columns;
    for (int c = 0; c < columns; ++c) {
        const double t0 = fromTime + c * span;
        const double a0 = (t0 - sequenceStart) / ratio;
        const double a1 = (t0 + span - sequenceStart) / ratio;
        long long i0 = static_cast<long long>(std::floor(a0 * columnsPerFileSecond));
        long long i1 = static_cast<long long>(std::ceil(a1 * columnsPerFileSecond));
        i0 = std::max<long long>(0, i0);
        i1 = std::min<long long>(static_cast<long long>(stored), std::max(i0 + 1, i1));
        if (i0 >= static_cast<long long>(stored)) {
            continue;
        }
        // The tallest stored column in this pixel gives the peak and the colour
        // (rekordbox's own rule when it packs a waveform into fewer pixels);
        // the average of the window gives the brighter body drawn inside it.
        // Zoomed out a pixel can span a second of audio, where peaks alone
        // would fill the lane with a solid block.
        size_t tallest = static_cast<size_t>(i0);
        uint8_t level = 0;
        double sum = 0.0;
        for (long long i = i0; i < i1; ++i) {
            const size_t a = 2 * static_cast<size_t>(i);
            const uint8_t v = a + 1 < cached->all.size() ? std::max(cached->all[a], cached->all[a + 1])
                              : (a < cached->all.size() ? cached->all[a] : uint8_t(0));
            if (v >= level) {
                level = v;
                tallest = static_cast<size_t>(i);
            }
            sum += v;
        }
        const uint8_t core = static_cast<uint8_t>(std::lround(sum / std::max<long long>(1, i1 - i0)));
        out[static_cast<size_t>(c)] = WaveColumn { colourOf(tallest), level, core, true };
    }
    return out;
}

std::vector<double> MixHouseBeatGrids::barLevels(const Grid& grid, double bar1, double bar, double fileSeconds) const
{
    std::vector<double> levels;
    if (bar <= 0.0 || fileSeconds <= bar1) {
        return levels;
    }
    auto cached = m_waveCache.constFind(grid.filePath);
    if (cached == m_waveCache.constEnd()) {
        MixHouseWaveformOverview wave;
        if (rekordboxPhrases()) {
            wave = rekordboxPhrases()->waveformDetail(grid.filePath, grid.bpm);
        }
        if (!wave.valid && library() && grid.trackId >= 0) {
            wave = decodeWaveformOverview(library()->overviewAnalysisBlob(grid.trackId));
        }
        cached = m_waveCache.insert(grid.filePath, wave);
    }
    const size_t stored = cached->valid ? cached->all.size() / 2 : 0;
    if (stored == 0) {
        return levels;
    }
    // The stored columns span the whole file; one average per bar from bar 1.
    const double perSecond = static_cast<double>(stored) / fileSeconds;
    for (double t = bar1; t < fileSeconds; t += bar) {
        const size_t i0 = std::min(stored, static_cast<size_t>(std::max(0.0, t * perSecond)));
        const size_t i1 = std::min(stored, std::max(i0 + 1, static_cast<size_t>((t + bar) * perSecond)));
        double sum = 0.0;
        for (size_t i = i0; i < i1; ++i) {
            sum += std::max(cached->all[2 * i], cached->all[2 * i + 1]);
        }
        levels.push_back(i1 > i0 ? sum / static_cast<double>(i1 - i0) : 0.0);
    }
    return levels;
}

bool MixHouseBeatGrids::clipFile(const trackedit::ClipKey& key, ClipFile& out) const
{
    const Grid grid = gridForClipName(clipName(key), false);
    const auto project = globalContext() ? globalContext()->currentProject() : nullptr;
    if (grid.filePath.isEmpty() || !project) {
        return false;
    }
    auto* au3Project = reinterpret_cast<AudacityProject*>(project->au3ProjectPtr());
    WaveTrack* track = au::au3::DomAccessor::findWaveTrack(*au3Project, ::TrackId(key.trackId));
    const auto clip = track ? au::au3::DomAccessor::findWaveClip(track, key.itemId) : nullptr;
    if (!clip || clip->GetStretchRatio() <= 0.0) {
        return false;
    }
    out.path = grid.filePath;
    out.ratio = clip->GetStretchRatio();
    out.sequenceStart = clip->GetPlayStartTime() - clip->GetTrimLeft();
    return true;
}

bool MixHouseBeatGrids::clipVolume(const trackedit::ClipKey& key, double t0, double step, size_t count,
                                   std::vector<double>& out) const
{
    out.clear();
    const auto project = globalContext() ? globalContext()->currentProject() : nullptr;
    if (!project || count == 0) {
        return false;
    }
    auto* au3Project = reinterpret_cast<AudacityProject*>(project->au3ProjectPtr());
    WaveTrack* track = au::au3::DomAccessor::findWaveTrack(*au3Project, ::TrackId(key.trackId));
    const auto clip = track ? au::au3::DomAccessor::findWaveClip(track, key.itemId) : nullptr;
    if (!clip) {
        return false;
    }
    out.resize(count);
    clip->GetEnvelope().GetValues(out.data(), static_cast<int>(count), t0, step);
    return true;
}

bool MixHouseBeatGrids::clipMonoSamples(const trackedit::ClipKey& key, double fromSec, double toSec,
                                        std::vector<float>& out, double& sampleRate, double& startSec, int channel) const
{
    out.clear();
    const auto project = globalContext() ? globalContext()->currentProject() : nullptr;
    if (!project || toSec <= fromSec) {
        return false;
    }
    auto* au3Project = reinterpret_cast<AudacityProject*>(project->au3ProjectPtr());
    WaveTrack* track = au::au3::DomAccessor::findWaveTrack(*au3Project, ::TrackId(key.trackId));
    const auto clip = track ? au::au3::DomAccessor::findWaveClip(track, key.itemId) : nullptr;
    if (!clip || clip->GetRate() <= 0) {
        return false;
    }
    // File sample i is sequence sample i; GetSamples counts from the play start
    // (the same mapping stem separation uses).
    const double rate = clip->GetRate();
    const long long total = clip->GetSequenceSamplesCount().as_long_long() / std::max<size_t>(1, clip->NChannels());
    const long long a = std::clamp(static_cast<long long>(std::floor(fromSec * rate)), 0LL, total);
    const long long b = std::clamp(static_cast<long long>(std::ceil(toSec * rate)), a, total);
    if (b - a < 2 || b - a > 60 * 48000LL) {
        return false;   // nothing there, or far more than a close-up needs
    }
    const sampleCount trimLeft = clip->TimeToSamples(clip->GetTrimLeft());
    const size_t n = static_cast<size_t>(b - a);
    out.assign(n, 0.0f);
    std::vector<float> plane(n);
    const size_t channels = clip->NChannels();
    const size_t first = channel >= 0 ? std::min<size_t>(static_cast<size_t>(channel), channels - 1) : 0;
    const size_t last = channel >= 0 ? first + 1 : channels;
    for (size_t ch = first; ch < last; ++ch) {
        clip->GetSamples(ch, reinterpret_cast<samplePtr>(plane.data()), floatSample, sampleCount(a) - trimLeft, n, false);
        for (size_t i = 0; i < n; ++i) {
            out[i] += plane[i] / (last - first);
        }
    }
    sampleRate = rate;
    startSec = a / rate;
    return true;
}

//! Stored columns per second of project time: what the lane can resolve before
//! it has to draw the audio itself.
double MixHouseBeatGrids::clipWaveColumnRate(const trackedit::ClipKey& key) const
{
    const Grid grid = gridForClipName(clipName(key), false);
    if (grid.filePath.isEmpty()) {
        return 0.0;
    }
    const auto cached = m_waveCache.constFind(grid.filePath);
    if (cached == m_waveCache.constEnd() || !cached->valid) {
        return 0.0;   // clipWaveColumns() fills the cache; nothing to report yet
    }
    const size_t stored = cached->all.size() / 2;

    const auto project = globalContext() ? globalContext()->currentProject() : nullptr;
    if (!project || stored == 0) {
        return 0.0;
    }
    auto* au3Project = reinterpret_cast<AudacityProject*>(project->au3ProjectPtr());
    WaveTrack* track = au::au3::DomAccessor::findWaveTrack(*au3Project, ::TrackId(key.trackId));
    const auto clip = track ? au::au3::DomAccessor::findWaveClip(track, key.itemId) : nullptr;
    if (!clip) {
        return 0.0;
    }
    const double projectSeconds = clip->GetSequenceEndTime() - clip->GetSequenceStartTime();
    return projectSeconds > 0.0 ? static_cast<double>(stored) / projectSeconds : 0.0;
}

double MixHouseBeatGrids::clipBpm(const trackedit::ClipKey& key) const
{
    // The tempo line runs in project-tempo units: speed = line BPM / project
    // tempo. A clip stretched to the project tempo from raw tempo R has
    // stretch ratio R / project, so the line reaches the track's own tempo at
    // R = project tempo x stretch. R is the library BPM, or half or double it
    // when the aligner matched the track half- or double-time
    // (compatibleTrackBpm): 150 BPM La La La dropped at 98 was stretched from
    // 75, and a line at "150" played it at 300.
    const Grid grid = gridForClipName(clipName(key), false);
    const auto project = globalContext() ? globalContext()->currentProject() : nullptr;
    const auto trackeditProject = globalContext() ? globalContext()->currentTrackeditProject() : nullptr;
    if (grid.bpm <= 0.0 || !project || !trackeditProject) {
        return 0.0;
    }
    auto* au3Project = reinterpret_cast<AudacityProject*>(project->au3ProjectPtr());
    WaveTrack* track = au::au3::DomAccessor::findWaveTrack(*au3Project, ::TrackId(key.trackId));
    const auto clip = track ? au::au3::DomAccessor::findWaveClip(track, key.itemId) : nullptr;
    const double tempo = trackeditProject->timeSignature().tempo;
    if (!clip || tempo <= 0.0) {
        return grid.bpm;
    }
    return tempo * clip->GetStretchRatio();
}

int MixHouseBeatGrids::selectedLibraryTrackId() const
{
    if (!selectionController()) {
        return -1;
    }
    trackedit::ClipKeyList targets = selectionController()->selectedClips();
    if (targets.empty()) {
        const auto trackeditProject = globalContext() ? globalContext()->currentTrackeditProject() : nullptr;
        if (trackeditProject) {
            for (const trackedit::TrackId trackId : selectionController()->selectedTracks()) {
                for (const trackedit::Clip& clip : trackeditProject->clipList(trackId)) {
                    targets.push_back(clip.key);
                }
            }
        }
    }
    for (const trackedit::ClipKey& key : targets) {
        const int id = clipLibraryTrackId(key);
        if (id >= 0 && !isExternalLibraryTrack(id)) {
            return id;
        }
    }
    return -1;
}

QString MixHouseBeatGrids::clipName(const trackedit::ClipKey& clip) const
{
    const auto project = globalContext() ? globalContext()->currentTrackeditProject() : nullptr;
    return project ? project->clip(clip).title.toQString() : QString();
}

double MixHouseBeatGrids::SongEnergy::levelDb(double a, double b) const
{
    if (power.empty() || durationSec <= 0.0) {
        return refDb;
    }
    const double perSec = power.size() / durationSec;
    const long n = static_cast<long>(power.size());
    long i0 = std::clamp(static_cast<long>(std::floor(a * perSec)), 0L, n - 1);
    long i1 = std::clamp(static_cast<long>(std::ceil(b * perSec)), i0 + 1, n);
    double sum = 0.0;
    for (long i = i0; i < i1; ++i) {
        sum += power[static_cast<size_t>(i)];
    }
    return 10.0 * std::log10(sum / (i1 - i0) + 1e-9);
}

const MixHouseBeatGrids::SongEnergy& MixHouseBeatGrids::songEnergy(const QString& name) const
{
    const auto cached = m_songEnergy.constFind(name);
    if (cached != m_songEnergy.constEnd()) {
        return *cached;
    }
    SongEnergy song;
    if (!name.isEmpty() && library() && library()->isAvailable()) {
        IMixHouseLibrary::TrackQuery query;
        query.fileBaseName = name;
        query.limit = 2;
        MixHouseTrackList found = library()->tracks(query);
        if (found.empty()) {
            found = externalLibraryTracks(lexicon().get(), rekordboxXml().get(), query);
        }
        if (!found.empty()) {
            const MixHouseTrack& t = found.front();
            song.energy = t.energy;
            song.durationSec = t.durationSec;
            // The loudness over time comes from GlideMix's own analysis of
            // the file (a Lexicon song too, once it has been analysed here).
            int libraryId = isExternalLibraryTrack(t.id) ? -1 : t.id;
            if (libraryId < 0 && !t.filePath.isEmpty()) {
                IMixHouseLibrary::TrackQuery byFile;
                byFile.location = t.filePath;
                byFile.limit = 1;
                const MixHouseTrackList own = library()->tracks(byFile);
                if (!own.empty()) {
                    libraryId = own.front().id;
                    if (song.energy <= 0.0) {
                        song.energy = own.front().energy;
                    }
                }
            }
            if (libraryId >= 0 && song.durationSec > 0.0) {
                const MixHouseWaveformOverview overview = decodeWaveformOverview(library()->overviewAnalysisBlob(libraryId));
                if (overview.valid && !overview.all.empty()) {
                    song.power.reserve(overview.all.size());
                    for (const uint8_t v : overview.all) {
                        const float amp = v / 255.0f;
                        song.power.push_back(amp * amp);
                    }
                }
            }
            if ((song.energy <= 0.0 || song.power.empty()) && !t.filePath.isEmpty()) {
                const auto computed = m_computedEnergy.constFind(t.filePath);
                if (computed != m_computedEnergy.constEnd()) {
                    if (song.energy <= 0.0) {
                        song.energy = computed->energy;
                    }
                    if (song.power.empty()) {
                        song.power = computed->power;
                    }
                    if (song.durationSec <= 0.0) {
                        song.durationSec = computed->durationSec;
                    }
                } else {
                    requestEnergy(libraryId, t.filePath, t.bpm, t.beats.valid ? t.beats.firstBeatSec : 0.0, t.downbeatSec,
                                  song.energy <= 0.0);
                }
            }
            if (!song.power.empty()) {
                std::vector<float> sorted = song.power;
                const size_t p90 = static_cast<size_t>(0.9 * (sorted.size() - 1));
                std::nth_element(sorted.begin(), sorted.begin() + p90, sorted.end());
                song.refDb = 10.0 * std::log10(sorted[p90] + 1e-9);
            }
        }
    }
    return *m_songEnergy.insert(name, song);
}

void MixHouseBeatGrids::requestEnergy(int libraryId, const QString& filePath, double bpm, double firstBeatSec,
                                      double downbeatSec, bool storeEnergy) const
{
    if (m_energyRequested.contains(filePath)) {
        return;
    }
    m_energyRequested.insert(filePath);
    m_energyQueue.push_back({ libraryId, filePath, bpm, firstBeatSec, downbeatSec, storeEnergy });
    // Not from inside energyCurve()'s loop: songEnergy() hands out references
    // into m_songEnergy.
    QTimer::singleShot(0, qApp, [this, alive = m_alive]() {
        if (*alive) {
            startEnergyJob();
        }
    });
}

void MixHouseBeatGrids::startEnergyJob() const
{
    if (m_energyJobRunning || m_energyQueue.isEmpty()) {
        return;
    }
    m_energyJobRunning = true;
    const EnergyJob job = m_energyQueue.takeFirst();
    if (activity()) {
        const QString name = QFileInfo(job.filePath).completeBaseName();
        activity()->set(QStringLiteral("energy"), m_energyQueue.isEmpty()
                        ? muse::qtrc("mixhouse", "Working out the energy of %1").arg(name)
                        : muse::qtrc("mixhouse", "Working out the energy of %1 (%2 more)").arg(name).arg(m_energyQueue.size()));
    }
    const auto alive = m_alive;
    std::thread([this, alive, job]() {
        // A known grid skips the beat network and puts the phrases on the
        // song's own bars; nothing found here is saved to the song.
        MixHouseKnownAnalysis known;
        known.bpm = job.bpm;
        known.firstBeatSec = job.firstBeatSec;
        known.downbeatSec = job.downbeatSec;
        const MixHouseAnalysisResult analysis = analyzeTrackFile(job.filePath, {}, known);
        ComputedEnergy computed;
        if (analysis.valid) {
            computed.energy = analysis.energy;
            computed.durationSec = analysis.durationSec;
            computed.power.reserve(analysis.waveformOverview.size());
            for (const uint8_t v : analysis.waveformOverview) {
                const float amp = v / 255.0f;
                computed.power.push_back(amp * amp);
            }
            computed.phrases = analysis.modelPhrases;
            if (computed.phrases.empty()) {
                const double downbeat = analysis.downbeatSec >= 0.0 ? analysis.downbeatSec : analysis.firstBeatSec;
                computed.phrases = detectPhrases(decodeWaveformOverview(qUncompress(encodeWaveformOverview(
                                                     analysis.waveformOverview, analysis.waveformLow, analysis.waveformMid,
                                                     analysis.waveformHigh))),
                                                 analysis.durationSec, analysis.bpm, downbeat);
            }
        }
        if (!*alive) {
            return;
        }
        QTimer::singleShot(0, qApp, [this, alive, job, computed = std::move(computed)]() {
            if (!*alive) {
                return;
            }
            m_energyJobRunning = false;
            ++m_energyJobsDone;
            if (m_energyQueue.isEmpty() && activity()) {
                activity()->finish(QStringLiteral("energy"),
                                   muse::qtrc("mixhouse", "Energy worked out for %n song(s)", nullptr, m_energyJobsDone));
                m_energyJobsDone = 0;
            }
            if (computed.energy > 0.0) {
                m_computedEnergy.insert(job.filePath, computed);
                // Only a song without a rating: Lexicon's own stays.
                if (job.libraryId >= 0 && library() && job.storeEnergy) {
                    library()->setTrackEnergy(job.libraryId, computed.energy);
                }
                m_songEnergy.clear();
                LOGI() << "GlideMix: energy " << computed.energy << ", " << computed.phrases.size() << " phrases for "
                       << job.filePath;
                if (!computed.phrases.empty()) {
                    muse::async::Notification changed = m_beatGridsChanged;   // a copy shares the channel
                    changed.notify();   // the phrase ribbon
                }
            } else {
                LOGW() << "GlideMix: no energy for " << job.filePath;
            }
            startEnergyJob();
        });
    }).detach();
}

std::vector<double> MixHouseBeatGrids::energyCurve(double t0, double step, size_t count) const
{
    std::vector<double> out(count, 0.0);
    const auto project = globalContext() ? globalContext()->currentProject() : nullptr;
    const auto trackeditProject = globalContext() ? globalContext()->currentTrackeditProject() : nullptr;
    if (!project || !trackeditProject || count == 0 || step <= 0.0) {
        return out;
    }
    auto* au3Project = reinterpret_cast<AudacityProject*>(project->au3ProjectPtr());
    const double tEnd = t0 + step * count;
    std::vector<double> gains;
    for (const trackedit::TrackId trackId : trackeditProject->trackIdList()) {
        WaveTrack* track = au::au3::DomAccessor::findWaveTrack(*au3Project, ::TrackId(trackId));
        if (!track) {
            continue;
        }
        for (const trackedit::Clip& c : trackeditProject->clipList(trackId)) {
            if (c.endTime <= t0 || c.startTime >= tEnd) {
                continue;
            }
            const SongEnergy& song = songEnergy(c.title.toQString());
            if (song.energy <= 0.0) {
                continue;
            }
            const auto clip = au::au3::DomAccessor::findWaveClip(track, c.key.itemId);
            if (!clip || clip->GetStretchRatio() <= 0.0) {
                continue;
            }
            const double ratio = clip->GetStretchRatio();
            const double sequenceStart = clip->GetPlayStartTime() - clip->GetTrimLeft();
            const size_t i0 = static_cast<size_t>(std::max(0.0, std::ceil((c.startTime - t0) / step)));
            const size_t i1 = std::min(count, static_cast<size_t>(std::max(0.0, std::ceil((c.endTime - t0) / step))));
            if (i1 <= i0) {
                continue;
            }
            gains.assign(i1 - i0, 1.0);
            clip->GetEnvelope().GetValues(gains.data(), static_cast<int>(gains.size()), t0 + i0 * step, step);
            // Average over at least two seconds of the song: the energy of
            // a passage, not of single kicks.
            const double half = std::max(step, 2.0) / ratio / 2.0;
            for (size_t i = i0; i < i1; ++i) {
                const double gain = gains[i - i0];
                if (gain < 0.03) {
                    continue;   // faded out
                }
                const double src = (t0 + i * step - sequenceStart) / ratio;
                // A breakdown 12 dB under the song's loud parts is 3 points
                // lower; a fade lowers it the same way.
                const double quieter = std::min(1.0, (song.levelDb(src - half, src + half) - song.refDb) / 4.0);
                const double faded = 20.0 * std::log10(std::min(1.0, gain)) / 4.0;
                out[i] = std::max(out[i], std::clamp(song.energy + quieter + faded, 1.0, 10.0));
            }
        }
    }
    return out;
}

MixHouseBeatGrids::Grid MixHouseBeatGrids::gridForClipName(const QString& name, bool fresh) const
{
    if (name.isEmpty() || !library() || !library()->isAvailable()) {
        return {};
    }
    if (!fresh) {
        auto it = m_gridByClipName.constFind(name);
        if (it != m_gridByClipName.constEnd()) {
            return it.value();
        }
    }

    IMixHouseLibrary::TrackQuery query;
    query.fileBaseName = name;
    query.limit = 2;
    MixHouseTrackList found = library()->tracks(query);
    if (found.empty()) {
        found = externalLibraryTracks(lexicon().get(), rekordboxXml().get(), query);   // loaded from Lexicon DJ / rekordbox
    }

    Grid grid;
    for (const IMixHouseExternalLibrary* external :
         { static_cast<const IMixHouseExternalLibrary*>(lexicon().get()), static_cast<const IMixHouseExternalLibrary*>(rekordboxXml().get()) }) {
        if (!found.empty() && external && external->ownsTrack(found.front().id)) {
            grid.cues = external->cues(found.front().id);
        }
    }
    if (!found.empty()) {
        const MixHouseTrack& t = found.front();
        grid.filePath = t.filePath;
        if (grid.bpm <= 0.0) {
            grid.bpm = t.bpm;   // a tempo hint for the phrases, even without a usable grid
        }
        if (t.beats.valid && t.beats.constantTempo && t.beats.bpm > 0.0) {
            grid.trackId = t.id;
            grid.title = t.title;
            grid.bpm = t.beats.bpm;
            grid.firstBeatSec = t.beats.firstBeatSec;
            grid.downbeatSec = t.downbeatSec;
            grid.downbeatConfidence = t.downbeatConfidence;
        }
    }
    m_gridByClipName.insert(name, grid);
    return grid;
}

std::vector<IMixHouseBeatGrids::Tick> MixHouseBeatGrids::clipTicks(const trackedit::ClipKey& key, double fromTime,
                                                                   double toTime) const
{
    std::vector<Tick> ticks;
    const auto project = globalContext() ? globalContext()->currentProject() : nullptr;
    const auto trackeditProject = globalContext() ? globalContext()->currentTrackeditProject() : nullptr;
    if (!project || !trackeditProject) {
        return ticks;
    }

    const Grid grid = gridForClipName(clipName(key), false);
    if (!grid.valid()) {
        return ticks;
    }

    auto* au3Project = reinterpret_cast<AudacityProject*>(project->au3ProjectPtr());
    WaveTrack* track = au::au3::DomAccessor::findWaveTrack(*au3Project, ::TrackId(key.trackId));
    const auto clip = track ? au::au3::DomAccessor::findWaveClip(track, key.itemId) : nullptr;
    if (!clip) {
        return ticks;
    }

    const double ratio = clip->GetStretchRatio();
    const double playStart = clip->GetPlayStartTime();
    const double playEnd = clip->GetPlayEndTime();
    // Same mapping the aligner uses: a source second `a` plays at
    // (playStart - trimLeft) + a * ratio.
    const double sequenceStart = playStart - clip->GetTrimLeft();
    const double beatSec = 60.0 / grid.bpm * ratio;
    if (beatSec <= 0.0) {
        return ticks;
    }

    // Bars count from the detected downbeat when there is one, else from the
    // grid's first beat -- the same anchor the aligner puts on a bar line.
    const bool haveDownbeat = grid.downbeatSec >= 0.0;
    const double anchor = sequenceStart + (haveDownbeat ? grid.downbeatSec : grid.firstBeatSec) * ratio;
    const int upper = std::max(1, trackeditProject->timeSignature().upper);

    // A beat a few ms before the clip's first sample (a grid that starts at
    // -6 ms on a kick at 0) still marks that kick.
    constexpr double kEdgeToleranceSec = 0.03;
    const double from = std::max(fromTime, playStart - kEdgeToleranceSec);
    const double to = std::min(toTime, playEnd);
    if (to <= from) {
        return ticks;
    }
    const long long kFirst = static_cast<long long>(std::ceil((from - anchor) / beatSec - 1e-9));
    const long long kLast = static_cast<long long>(std::floor((to - anchor) / beatSec + 1e-9));
    if (kLast - kFirst > kMaxTicks) {
        return ticks;
    }
    ticks.reserve(static_cast<size_t>(std::max(0LL, kLast - kFirst + 1)));
    for (long long k = kFirst; k <= kLast; ++k) {
        ticks.push_back({ anchor + k * beatSec, ((k % upper) + upper) % upper == 0 });
    }
    return ticks;
}

std::vector<IMixHouseBeatGrids::Cue> MixHouseBeatGrids::clipCues(const trackedit::ClipKey& key, double fromTime,
                                                                 double toTime) const
{
    std::vector<Cue> result;
    const auto project = globalContext() ? globalContext()->currentProject() : nullptr;
    if (!project) {
        return result;
    }
    const Grid grid = gridForClipName(clipName(key), false);
    if (grid.cues.empty()) {
        return result;
    }
    auto* au3Project = reinterpret_cast<AudacityProject*>(project->au3ProjectPtr());
    WaveTrack* track = au::au3::DomAccessor::findWaveTrack(*au3Project, ::TrackId(key.trackId));
    const auto clip = track ? au::au3::DomAccessor::findWaveClip(track, key.itemId) : nullptr;
    if (!clip) {
        return result;
    }
    // Same source -> timeline mapping as the ticks.
    const double ratio = clip->GetStretchRatio();
    const double sequenceStart = clip->GetPlayStartTime() - clip->GetTrimLeft();
    const double from = std::max(fromTime, clip->GetPlayStartTime());
    const double to = std::min(toTime, clip->GetPlayEndTime());
    for (const MixHouseCue& c : grid.cues) {
        Cue cue;
        cue.time = sequenceStart + c.startSec * ratio;
        cue.endTime = c.loop && c.endSec > c.startSec ? sequenceStart + c.endSec * ratio : -1.0;
        const double end = cue.endTime > 0.0 ? cue.endTime : cue.time;
        if (end < from || cue.time > to) {
            continue;
        }
        cue.hotCueIndex = c.hotCueIndex;
        cue.name = c.name;
        cue.color = c.color;
        result.push_back(cue);
    }
    return result;
}

std::vector<IMixHouseBeatGrids::Phrase> MixHouseBeatGrids::clipPhrases(const trackedit::ClipKey& key, double fromTime,
                                                                       double toTime) const
{
    std::vector<Phrase> result;
    const auto project = globalContext() ? globalContext()->currentProject() : nullptr;
    if (!project || !rekordboxPhrases() || (libraryConfiguration() && !libraryConfiguration()->phraseRibbonVisible())) {
        return result;
    }
    const Grid grid = gridForClipName(clipName(key), false);
    if (grid.filePath.isEmpty()) {
        return result;
    }
    // Phrases edited in the Beatgrid editor win over rekordbox's.
    const std::vector<MixHousePhrase> phrases = phrasesFor(grid);
    if (phrases.empty()) {
        return result;
    }
    auto* au3Project = reinterpret_cast<AudacityProject*>(project->au3ProjectPtr());
    WaveTrack* track = au::au3::DomAccessor::findWaveTrack(*au3Project, ::TrackId(key.trackId));
    const auto clip = track ? au::au3::DomAccessor::findWaveClip(track, key.itemId) : nullptr;
    if (!clip) {
        return result;
    }
    // Same source -> timeline mapping as the ticks and cues.
    const double ratio = clip->GetStretchRatio();
    const double sequenceStart = clip->GetPlayStartTime() - clip->GetTrimLeft();
    const double from = std::max(fromTime, clip->GetPlayStartTime());
    const double to = std::min(toTime, clip->GetPlayEndTime());
    for (const MixHousePhrase& p : phrases) {
        Phrase phrase;
        phrase.time = std::max(from, sequenceStart + p.startSec * ratio);
        phrase.endTime = std::min(to, sequenceStart + p.endSec * ratio);
        if (phrase.endTime <= phrase.time) {
            continue;
        }
        phrase.label = p.label;
        phrase.color = p.color;
        if (p.fillStartSec >= 0.0) {
            phrase.fillTime = std::clamp(sequenceStart + p.fillStartSec * ratio, phrase.time, phrase.endTime);
        }
        result.push_back(phrase);
    }
    return result;
}

double MixHouseBeatGrids::lockedClipStart(const trackedit::ClipKey& key, double proposedStart, double pxPerSec) const
{
    // SYNC off: the clip goes exactly where it is dropped -- no bars and no
    // magnetic anchors either (the anchors used to pull with SYNC off too).
    if (libraryConfiguration() && !libraryConfiguration()->syncEnabled()) {
        return proposedStart;
    }
    const double locked = barLockedClipStart(key, proposedStart);
    if (pxPerSec <= 0.0) {
        return locked;
    }
    // Strong enough to feel, as MixMeister's does: 40 pixels, never less
    // than a bar when zoomed right in, and never more than 8 bars when
    // zoomed out (40 px could be a minute: the clip jumped away).
    // GlideMix: 80 pixels, the same reach as a library drop, 1 to 64 bars.
    // 40 px / 8 bars (15 s) let drags land a minute off the OUT unlinked.
    constexpr double kMagnetPx = 80.0;
    constexpr double kMagnetMaxBars = 64.0;
    const auto project = globalContext() ? globalContext()->currentTrackeditProject() : nullptr;
    const trackedit::TimeSignature ts = project ? project->timeSignature() : trackedit::TimeSignature {};
    const double bar = ts.tempo > 0.0 ? 60.0 / ts.tempo * (4.0 / std::max(1, ts.lower)) * std::max(1, ts.upper) : 2.0;
    const double snapped = magneticClipStart(key, locked, std::clamp(kMagnetPx / pxPerSec, bar, bar * kMagnetMaxBars));
    return snapped;
}

double MixHouseBeatGrids::magneticClipStart(const trackedit::ClipKey& key, double locked, double thresholdSec) const
{
    // MixMeister's magnetic anchors: this clip's Intro onto another clip's
    // Outro, or its Outro onto another's Intro.
    const Anchors mine = clipAnchors(key);
    const auto trackeditProject = globalContext() ? globalContext()->currentTrackeditProject() : nullptr;
    if (!mine.valid || !trackeditProject) {
        return locked;
    }
    const trackedit::Clip self = trackeditProject->clip(key);
    const double shift = locked - self.startTime;
    const double introAt = mine.intro + shift;
    const double outroAt = mine.outro + shift;
    double best = locked;
    double bestDistance = thresholdSec;
    for (const trackedit::TrackId trackId : trackeditProject->trackIdList()) {
        if (trackId == key.trackId) {
            continue;
        }
        for (const trackedit::Clip& other : trackeditProject->clipList(trackId)) {
            const Anchors theirs = clipAnchors(other.key);
            if (!theirs.valid) {
                continue;
            }
            if (std::abs(introAt - theirs.outro) < bestDistance) {
                bestDistance = std::abs(introAt - theirs.outro);
                best = locked + (theirs.outro - introAt);
            }
            if (std::abs(outroAt - theirs.intro) < bestDistance) {
                bestDistance = std::abs(outroAt - theirs.intro);
                best = locked + (theirs.intro - outroAt);
            }
        }
    }
    return std::max(0.0, best);
}

std::vector<MixHousePhrase> MixHouseBeatGrids::phrasesFor(const Grid& grid) const
{
    if (grid.filePath.isEmpty()) {
        return {};
    }
    if (grid.trackId >= 0 && library() && !isExternalLibraryTrack(grid.trackId)) {
        if (auto edited = library()->trackPhraseEdits(grid.trackId)) {
            return *edited;
        }
    }
    std::vector<MixHousePhrase> phrases = rekordboxPhrases() ? rekordboxPhrases()->phrases(grid.filePath, grid.bpm)
                                                             : std::vector<MixHousePhrase>();
    // Tracks rekordbox never analysed: GlideMix's own.
    if (phrases.empty() && grid.trackId >= 0 && library() && !isExternalLibraryTrack(grid.trackId)) {
        phrases = library()->detectedPhrases(grid.trackId);
    }
    // A Lexicon song (no rekordbox analysis on this computer, e.g. Windows):
    // worked out in the background, like its energy curve.
    if (phrases.empty() && isExternalLibraryTrack(grid.trackId)) {
        const auto computed = m_computedEnergy.constFind(grid.filePath);
        if (computed != m_computedEnergy.constEnd()) {
            phrases = computed->phrases;
        } else {
            requestEnergy(-1, grid.filePath, grid.bpm, grid.firstBeatSec, grid.downbeatSec, false);
        }
    }
    return phrases;
}

MixHouseBeatGrids::SourceAnchors MixHouseBeatGrids::sourceAnchors(const trackedit::ClipKey& key) const
{
    SourceAnchors result;
    const auto project = globalContext() ? globalContext()->currentProject() : nullptr;
    if (!project) {
        return result;
    }
    const Grid grid = gridForClipName(clipName(key), false);
    if (grid.bpm <= 0.0 || grid.filePath.isEmpty()) {
        return result;
    }
    auto* au3Project = reinterpret_cast<AudacityProject*>(project->au3ProjectPtr());
    WaveTrack* track = au::au3::DomAccessor::findWaveTrack(*au3Project, ::TrackId(key.trackId));
    const auto clip = track ? au::au3::DomAccessor::findWaveClip(track, key.itemId) : nullptr;
    if (!clip || clip->GetStretchRatio() <= 0.0) {
        return result;
    }
    result.ratio = clip->GetStretchRatio();
    result.sequenceStart = clip->GetPlayStartTime() - clip->GetTrimLeft();
    const double fileSeconds = (clip->GetPlayEndTime() + clip->GetTrimRight() - result.sequenceStart) / result.ratio;

    const auto drag = m_anchorDrag.constFind(grid.filePath);
    std::optional<std::pair<double, double> > saved;
    if (drag != m_anchorDrag.constEnd()) {
        saved = drag.value();
    } else if (library()) {
        auto cached = m_anchorCache.constFind(grid.filePath);
        if (cached == m_anchorCache.constEnd()) {
            cached = m_anchorCache.insert(grid.filePath, library()->trackAnchors(grid.filePath));
        }
        saved = cached.value();
    }
    if (saved) {
        result.intro = saved->first;
        result.outro = saved->second;
        result.moved = true;
        result.valid = true;
        return result;
    }

    // Defaults, MixMeister's Beatmix anchors on GlideMix's phrases
    // (docs/mixmeister-research.md 8.1): both on the grid, in phrase steps
    // from bar 1. Outro: rekordbox's (or the edited) Outro phrase start, else
    // the last phrase line that still leaves a phrase to play. Intro: where
    // the leading Intro phrases end, else bar 1. A phrase that isn't loud
    // enough (8 bars averaging under 15 % of the loudest bar) is stepped over,
    // and a song too short for 8-bar phrases tries 4, 2, then 1.
    const double beat = 60.0 / grid.bpm;
    const double bar = 4.0 * beat;
    const double bar1 = grid.downbeatSec >= 0.0 ? grid.downbeatSec : std::max(0.0, grid.firstBeatSec);
    const std::vector<MixHousePhrase> phrases = phrasesFor(grid);
    const std::vector<double> levels = barLevels(grid, bar1, bar, fileSeconds);   // empty: no waveform yet
    const double loudest = levels.empty() ? 0.0 : *std::max_element(levels.begin(), levels.end());
    const auto loudEnough = [&](double from, int bars) {
        if (loudest <= 0.0) {
            return true;   // nothing to judge by: every phrase counts
        }
        const long long first = std::llround((from - bar1) / bar);
        double sum = 0.0;
        for (long long b = first; b < first + bars; ++b) {
            sum += (b >= 0 && b < static_cast<long long>(levels.size())) ? levels[static_cast<size_t>(b)] : 0.0;
        }
        return sum / bars >= 0.15 * loudest;
    };
    const auto onBar = [&](double sec) { return bar1 + std::round((sec - bar1) / bar) * bar; };

    double labelOutro = -1.0;
    for (const MixHousePhrase& p : phrases) {
        if (p.label.startsWith(QStringLiteral("Outro")) && p.startSec > bar1 + 8.0 * bar) {
            labelOutro = onBar(p.startSec);
            break;
        }
    }
    double labelIntro = bar1;
    for (const MixHousePhrase& p : phrases) {
        if (!p.label.startsWith(QStringLiteral("Intro"))) {
            if (p.startSec > bar1 + beat) {
                labelIntro = onBar(p.startSec);
            }
            break;
        }
    }

    double intro = bar1;
    double outro = bar1 + bar;
    for (const int bars : { 8, 4, 2, 1 }) {
        const double phrase = bars * bar;
        double out = labelOutro;
        if (out >= 0.0) {
            // A labelled Outro whose music has already faded out (rekordbox
            // marks where the outro begins, often near-silent) left a gap of
            // silence in every blend: the outgoing song must still play
            // during the mix, as MixMeister's last-loud-phrase rule ensures.
            while (out - phrase > labelIntro && !loudEnough(out, bars)) {
                out -= phrase;
            }
        } else {
            // B1: the last phrase line inside the song; back while the phrase before it is quiet.
            double b1 = bar1 + std::floor((fileSeconds - bar1) / phrase) * phrase;
            while (b1 - 2.0 * phrase > bar1 && !loudEnough(b1 - phrase, bars)) {
                b1 -= phrase;
            }
            out = b1 - phrase;
        }
        double in = labelIntro;
        while (in + phrase < out && !loudEnough(in, bars)) {
            in += phrase;   // a quiet first phrase isn't where the song has arrived
        }
        // The IN is where the song has arrived: its first bar with its music
        // really playing, not a label on a still-quiet lead-in. The blend is
        // complete there (the song fades in before it), so a quiet stretch
        // after it left a hole in the mix (Chris, 2026-11-01).
        const double levelFloor = 0.4 * loudest;
        for (int walked = 0; walked < 16 && in + phrase < out; ++walked) {
            const long long b = std::llround((in - bar1) / bar);
            if (loudest <= 0.0 || b < 0 || b >= static_cast<long long>(levels.size())
                || levels[static_cast<size_t>(b)] >= levelFloor) {
                break;
            }
            in += bar;
        }
        intro = in;
        outro = out;
        if (in + phrase <= out) {
            break;   // the intro's mix range ends before the outro's begins
        }
    }
    if (intro >= outro - beat) {
        intro = bar1;
    }
    result.intro = intro;
    result.outro = std::max(bar1 + beat, std::min(outro, fileSeconds));
    result.valid = true;
    return result;
}

IMixHouseBeatGrids::Anchors MixHouseBeatGrids::clipAnchors(const trackedit::ClipKey& key) const
{
    Anchors anchors;
    const SourceAnchors source = sourceAnchors(key);
    if (!source.valid) {
        return anchors;
    }
    anchors.valid = true;
    anchors.intro = source.sequenceStart + source.intro * source.ratio;
    anchors.outro = source.sequenceStart + source.outro * source.ratio;
    anchors.moved = source.moved;
    return anchors;
}

void MixHouseBeatGrids::startRipple(const trackedit::ClipKey& key, int which)
{
    m_ripple = RippleDrag {};
    m_ripple.clip = key;
    m_ripple.which = which;
    const Anchors mine = clipAnchors(key);
    const auto project = globalContext() ? globalContext()->currentTrackeditProject() : nullptr;
    if (!mine.valid || !project) {
        return;
    }
    constexpr double kLinked = 0.05;   // anchors this close are one transition point
    const double point = which == 1 ? mine.outro : mine.intro;
    m_ripple.originalAnchor = point;

    // The other side of the transition: an Intro on this Outro, or an Outro
    // on this Intro, on another clip.
    std::optional<trackedit::ClipKey> partner;
    std::vector<trackedit::Clip> all;
    for (const trackedit::TrackId trackId : project->trackIdList()) {
        for (const trackedit::Clip& clip : project->clipList(trackId)) {
            all.push_back(clip);
            if (clip.key == key || partner) {
                continue;
            }
            const Anchors theirs = clipAnchors(clip.key);
            if (theirs.valid && std::abs((which == 1 ? theirs.intro : theirs.outro) - point) < kLinked) {
                partner = clip.key;
            }
        }
    }
    if (!partner) {
        return;   // not a transition: only the anchor moves
    }
    // The track that comes in here, and everything that comes in after it.
    const trackedit::ClipKey incoming = which == 1 ? *partner : key;
    const trackedit::ClipKey outgoing = which == 1 ? key : *partner;
    const auto startsAfter = [&](const trackedit::Clip& clip) {
        const Anchors a = clipAnchors(clip.key);
        return (a.valid ? a.intro : clip.startTime) > point + kLinked;
    };
    for (const trackedit::Clip& clip : all) {
        if (clip.key == outgoing) {
            continue;
        }
        if (clip.key == incoming || startsAfter(clip)) {
            m_ripple.followers.push_back({ clip.key, clip.startTime });
        }
    }
    // Moving left, a follower may not run into a clip that stays where it is.
    m_ripple.minShift = -std::numeric_limits<double>::infinity();
    for (const auto& [followerKey, start] : m_ripple.followers) {
        for (const trackedit::Clip& clip : all) {
            if (clip.key.trackId != followerKey.trackId || clip.endTime > start + 1e-6) {
                continue;
            }
            const bool follows = std::any_of(m_ripple.followers.begin(), m_ripple.followers.end(),
                                             [&](const auto& f) { return f.first == clip.key; });
            if (!follows) {
                m_ripple.minShift = std::max(m_ripple.minShift, clip.endTime - start);
            }
        }
        m_ripple.minShift = std::max(m_ripple.minShift, -start);   // nor before 0
    }
    m_ripple.active = true;
}

void MixHouseBeatGrids::subscribeToHistory()
{
    if (m_subscribedToHistory || !projectHistory()) {
        return;
    }
    m_subscribedToHistory = true;
    projectHistory()->historyChanged().onReceive(this, [this](trackedit::HistoryEvent event) {
        const size_t current = projectHistory()->currentStateIndex();
        if (event == trackedit::HistoryEvent::NewState) {
            // A new step after an undo replaces the steps that were undone.
            std::erase_if(m_anchorHistory, [current](const AnchorRecord& r) { return r.stateIndex >= current; });
            return;
        }
        // Undo / redo: each song gets the anchors of the step now current.
        QHash<QString, std::pair<double, double> > wanted;
        for (const AnchorRecord& r : m_anchorHistory) {
            if (r.stateIndex <= current) {
                wanted.insert(r.filePath, r.after);
            } else if (!wanted.contains(r.filePath)) {
                wanted.insert(r.filePath, r.before);
            }
        }
        for (auto it = wanted.constBegin(); it != wanted.constEnd(); ++it) {
            setSongAnchors(it.key(), it.value());
        }
    });
}

void MixHouseBeatGrids::setSongAnchors(const QString& filePath, const std::pair<double, double>& anchors)
{
    const auto cached = m_anchorCache.constFind(filePath);
    if (cached != m_anchorCache.constEnd() && cached.value() && *cached.value() == anchors) {
        return;
    }
    m_anchorDrag.remove(filePath);
    m_anchorCache.insert(filePath, anchors);
    if (library()) {
        library()->setTrackAnchors(filePath, anchors.first, anchors.second);
    }
    m_beatGridsChanged.notify();
}

void MixHouseBeatGrids::applyRipple(double anchorNow, bool completed, const std::pair<double, double>& anchorsAfter)
{
    if (!m_ripple.active || !trackeditInteraction()) {
        return;
    }
    // Dragging the Outro moves the transition: the rest follows it. Dragging
    // the Intro changes where the incoming track mixes in: it slides so that
    // point stays on the Outro, and the rest follows it.
    const double moved = anchorNow - m_ripple.originalAnchor;
    const double shift = std::max(m_ripple.which == 1 ? moved : -moved, m_ripple.minShift);
    if (std::abs(shift - m_ripple.applied) > 1e-9 || completed) {
        for (const auto& [key, start] : m_ripple.followers) {
            trackeditInteraction()->changeClipStartTime(key, start + shift, false);
        }
        m_ripple.applied = shift;
    }
    if (completed) {
        if (projectHistory() && std::abs(shift) > 1e-9) {
            subscribeToHistory();
            projectHistory()->pushHistoryState(muse::trc("mixhouse", "Moved a transition"),
                                               muse::trc("mixhouse", "Move transition"));
            if (!m_ripple.filePath.isEmpty()) {
                m_anchorHistory.push_back({ projectHistory()->currentStateIndex(), m_ripple.filePath,
                                            m_ripple.anchorsBefore, anchorsAfter });
            }
        }
        m_ripple = RippleDrag {};
    }
}

void MixHouseBeatGrids::beginClipMove(const trackedit::ClipKey& key)
{
    // Once per drag: the clip's key changes when it's dragged to another row.
    if (m_moveCaptured) {
        return;
    }
    m_moveCaptured = true;
    m_moveFromTrack = key.trackId;
    LOGI() << "GlideMix move start: clip " << key.trackId << "/" << key.itemId;
    m_moveFromTitle.clear();
    if (const auto project = globalContext() ? globalContext()->currentTrackeditProject() : nullptr) {
        for (const trackedit::Track& track : project->trackList()) {
            if (track.id == key.trackId) {
                m_moveFromTitle = track.title.toQString();
            }
        }
    }
    const Anchors anchors = clipAnchors(key);
    m_moveBefore = anchors.valid ? std::optional<Anchors>(anchors) : std::nullopt;
}

void MixHouseBeatGrids::tidyRowsAfterMove(const trackedit::ClipKey& key)
{
    const trackedit::TrackId from = m_moveFromTrack;
    const QString fromTitle = m_moveFromTitle;
    m_moveFromTrack = -1;
    m_moveFromTitle.clear();
    LOGI() << "GlideMix tidy rows: from track " << from << " to " << key.trackId << "/" << key.itemId;
    const auto project = globalContext() ? globalContext()->currentTrackeditProject() : nullptr;
    if (!project || !tracksInteraction() || from < 0 || from == key.trackId) {
        return;
    }
    bool changed = false;
    // The new row is called "Audio N": it takes the name of the row the song came from.
    if (project->clipList(key.trackId).size() == 1 && !fromTitle.isEmpty()) {
        for (const trackedit::Track& track : project->trackList()) {
            if (track.id == key.trackId && track.title.toQString().startsWith(QStringLiteral("Audio "))) {
                changed |= tracksInteraction()->changeTrackTitle(key.trackId, muse::String::fromQString(fromTitle));
            }
        }
    }
    // The row it left, now empty, goes (it held only this song).
    const std::vector<trackedit::Track> tracks = project->trackList();
    const bool exists = std::any_of(tracks.begin(), tracks.end(), [&](const trackedit::Track& t) { return t.id == from; });
    if (exists && project->clipList(from).empty()) {
        changed |= tracksInteraction()->deleteTracks({ from });
    }
    if (changed && projectHistory()) {
        projectHistory()->modifyState();   // part of the move's undo step
    }
}

void MixHouseBeatGrids::cancelClipMove()
{
    m_moveBefore.reset();
    m_moveCaptured = false;
}

au::trackedit::TrackId MixHouseBeatGrids::clearRow(trackedit::TrackId preferred, double start, double end, int channels,
                                               const trackedit::ClipKeyList& ignore, const trackedit::TrackIdList& taken)
{
    const auto project = globalContext() ? globalContext()->currentTrackeditProject() : nullptr;
    if (!project) {
        return preferred;
    }
    const std::vector<trackedit::Track> tracks = project->trackList();
    const auto isClear = [&](const trackedit::Track& track) {
        if (track.type != trackedit::TrackType::Mono && track.type != trackedit::TrackType::Stereo) {
            return false;
        }
        if (std::find(taken.begin(), taken.end(), track.id) != taken.end()) {
            return false;
        }
        for (const trackedit::Clip& clip : project->clipList(track.id)) {
            if (std::find(ignore.begin(), ignore.end(), clip.key) != ignore.end()) {
                continue;
            }
            if (clip.startTime < end - 1e-6 && clip.endTime > start + 1e-6) {
                return false;
            }
        }
        return true;
    };
    // The row it was let go on, else the one right below, else a new row
    // inserted right there -- not the next free row further down or a new
    // one at the bottom, where a song couldn't be put between two rows and
    // landed out of sight.
    const auto preferredAt = std::find_if(tracks.begin(), tracks.end(),
                                          [&](const trackedit::Track& t) { return t.id == preferred; });
    for (auto it = preferredAt; it != tracks.end() && it - preferredAt < 2; ++it) {
        if (isClear(*it)) {
            return it->id;
        }
    }
    if (!tracksInteraction()) {
        return preferred;
    }
    const trackedit::TrackId added = tracksInteraction()->addWaveTrack(channels);
    if (preferredAt != tracks.end()) {
        tracksInteraction()->moveTracksTo({ added }, static_cast<int>(preferredAt - tracks.begin()) + 1);
    }
    return added;
}

void MixHouseBeatGrids::insertAtTransition(const trackedit::ClipKey& key, bool reordered)
{
    const std::optional<Anchors> before = reordered ? m_moveBefore : std::nullopt;
    m_moveBefore.reset();
    m_moveCaptured = false;

    // SYNC off: clips go exactly where they are put, nothing else moves.
    if (libraryConfiguration() && !libraryConfiguration()->syncEnabled()) {
        return;
    }
    const auto project = globalContext() ? globalContext()->currentTrackeditProject() : nullptr;
    const Anchors mine = clipAnchors(key);
    if (!project || !mine.valid || !trackeditInteraction()) {
        return;
    }
    constexpr double kLinked = 0.05;   // anchors this close are one transition point

    struct Item {
        trackedit::ClipKey key;
        double start = 0.0;
        bool anchored = false;
        double intro = 0.0;
        double outro = 0.0;
    };
    std::vector<Item> others;
    // The later pieces of this song when it was split at grid jumps
    // (mixhouseprojectaligner.cpp): same row, same song, its anchors a few ms
    // off. Not songs of their own -- taken for the song coming in at this
    // IN, they were pushed a whole song's length down the row.
    trackedit::ClipKeyList pieces;
    const QString myName = clipName(key);
    for (const trackedit::TrackId trackId : project->trackIdList()) {
        for (const trackedit::Clip& clip : project->clipList(trackId)) {
            if (clip.key == key) {
                continue;
            }
            const Anchors a = clipAnchors(clip.key);
            if (a.valid && trackId == key.trackId && clip.title.toQString() == myName
                && std::abs(a.intro - mine.intro) < 0.5) {
                pieces.push_back(clip.key);
                continue;
            }
            others.push_back({ clip.key, clip.startTime, a.valid, a.intro, a.outro });
        }
    }
    const auto find = [&](auto&& pred) -> Item* {
        for (Item& item : others) {
            if (item.anchored && pred(item)) {
                return &item;
            }
        }
        return nullptr;
    };

    // Where it went in: its IN on a song's OUT, or its OUT on the IN of a
    // song that another song already mixes into (then it slides back so its
    // IN takes that transition).
    LOGI() << "GlideMix insert check: clip " << key.trackId << "/" << key.itemId << " IN " << mine.intro << " OUT " << mine.outro
           << (before ? QStringLiteral(", dragged from IN %1").arg(before->intro).toStdString() : std::string(", dropped"));
    double point = 0.0;
    double selfShift = 0.0;
    Item* outgoing = find([&](const Item& i) { return std::abs(i.outro - mine.intro) < kLinked; });
    if (outgoing) {
        point = mine.intro;
    } else {
        if (before) {
            // Dragged: it stays where it was let go. (Sliding it back so its
            // IN took that transition moved it a whole song's length.)
            return;
        }
        Item* next = find([&](const Item& i) { return std::abs(i.intro - mine.outro) < kLinked; });
        outgoing = next ? find([&](const Item& i) { return std::abs(i.outro - next->intro) < kLinked; }) : nullptr;
        if (!outgoing) {
            return;   // not placed into the mix: nothing to make room for
        }
        point = next->intro;
        selfShift = point - mine.intro;
    }

    // The songs from `from` on, and the clips without anchors (stems, plain
    // audio) that start with them or after `from`.
    const auto shiftFrom = [&](double from, double by) {
        std::vector<Item*> moving;
        for (Item& item : others) {
            if (item.anchored && item.intro > from - kLinked) {
                moving.push_back(&item);
            }
        }
        for (Item& item : others) {
            if (item.anchored) {
                continue;
            }
            const bool withSong = std::any_of(moving.begin(), moving.end(), [&](const Item* m) {
                return m->anchored && std::abs(m->start - item.start) < 1e-3;
            });
            if (withSong || item.start > from - kLinked) {
                moving.push_back(&item);
            }
        }
        for (Item* item : moving) {
            item->start += by;
            item->intro += by;
            item->outro += by;
            trackeditInteraction()->changeClipStartTime(item->key, item->start, false);
        }
        return moving;
    };

    // Dragged: no closing of the gap it left, no moving rows (MixMeister did
    // both; on Chris's drags the song slid to another row and to the end of
    // the mix). Only a song let go right in a transition makes room below.
    if (before && std::abs(before->intro - point) <= kLinked) {
        return;
    }

    if (std::abs(selfShift) > 1e-9) {
        // Its pieces with it; moving right, the rightmost first.
        trackedit::ClipKeyList self = { key };
        self.insert(self.end(), pieces.begin(), pieces.end());
        if (selfShift > 0.0) {
            std::reverse(self.begin(), self.end());
        }
        for (const trackedit::ClipKey& clip : self) {
            trackeditInteraction()->changeClipStartTime(clip, project->clip(clip).startTime + selfShift, false);
        }
    }
    const double myOutro = mine.outro + (point - mine.intro);   // its IN is on `point` now

    // Its row goes in with it, when the row holds only this song: above the
    // song that comes in after it, else under the song it follows (and that
    // song's stems).
    const std::vector<trackedit::TrackId> rows = project->trackIdList();
    const auto rowOf = [&](trackedit::TrackId id) {
        return static_cast<int>(std::distance(rows.begin(), std::find(rows.begin(), rows.end(), id)));
    };
    const auto moveRowTo = [&](int target) {
        if (!tracksInteraction() || project->clipList(key.trackId).size() != 1 + pieces.size()) {
            return;
        }
        const int from = rowOf(key.trackId);
        const int to = from < target ? target - 1 : target;
        if (to != from) {
            tracksInteraction()->moveTracksTo({ key.trackId }, to);
        }
    };

    // Make room: the song that came in here, and everything after it, now
    // comes in on this song's OUT.
    Item* incoming = find([&](const Item& i) { return std::abs(i.intro - point) < kLinked; });
    if (incoming) {
        const trackedit::TrackId incomingTrack = incoming->key.trackId;
        shiftFrom(point, myOutro - point);
        if (incomingTrack != key.trackId && !before) {
            moveRowTo(rowOf(incomingTrack));
        }
    } else if (before) {
        return;   // dragged onto the last song's OUT: it stays where it was let go
    } else {
        return;   // a drop at the end of the mix: the magnet already placed it
    }

    if (projectHistory()) {
        projectHistory()->modifyState();
    }
    m_beatGridsChanged.notify();
    LOGI() << "GlideMix insert: placed at " << point << " s, "
           << (incoming ? "the songs after it pushed back" : "at the end of the mix");
}

std::optional<std::pair<double, double> > MixHouseBeatGrids::takeClipAnchorMove(const trackedit::ClipKey& key)
{
    const QString file = gridForClipName(clipName(key), false).filePath;
    const auto it = m_anchorDrag.find(file);
    if (it == m_anchorDrag.end()) {
        return std::nullopt;
    }
    const std::pair<double, double> anchors = it.value();
    m_anchorDrag.erase(it);
    if (m_ripple.clip == key) {
        m_ripple = RippleDrag {};
    }
    m_beatGridsChanged.notify();
    return anchors;
}

void MixHouseBeatGrids::putClipAnchorMove(const trackedit::ClipKey& key, const std::pair<double, double>& anchors)
{
    m_anchorDrag.insert(gridForClipName(clipName(key), false).filePath, anchors);
    m_beatGridsChanged.notify();
}

void MixHouseBeatGrids::moveClipAnchor(const trackedit::ClipKey& key, int which, double projectTime, bool completed)
{
    const bool starting = !m_ripple.active || m_ripple.clip != key || m_ripple.which != which;
    if (starting) {
        startRipple(key, which);
    }
    const SourceAnchors source = sourceAnchors(key);
    const Grid grid = gridForClipName(clipName(key), false);
    if (!source.valid || grid.filePath.isEmpty()) {
        return;
    }
    if (starting && m_ripple.active) {
        m_ripple.filePath = grid.filePath;
        m_ripple.anchorsBefore = { source.intro, source.outro };
    }
    const double beat = 60.0 / grid.bpm;
    // Dragging a linked Intro slides its own clip: measure against where the
    // clip was when the drag began, or the drag would chase itself.
    const double selfShift = (m_ripple.active && m_ripple.which == 0) ? m_ripple.applied : 0.0;
    const double sequenceStart = source.sequenceStart - selfShift;
    double sec = (projectTime - sequenceStart) / source.ratio;
    if (!libraryConfiguration() || libraryConfiguration()->syncEnabled()) {
        const double first = grid.downbeatSec >= 0.0 ? grid.downbeatSec : grid.firstBeatSec;
        sec = first + std::round((sec - first) / beat) * beat;   // on the beats
    }
    double intro = source.intro;
    double outro = source.outro;
    // At least four bars between IN and OUT: dragged together they left a
    // song with no mix at all (nothing for the next song to link to).
    const double minGap = 16.0 * beat;
    if (which == 0) {
        intro = std::clamp(sec, 0.0, std::max(0.0, outro - minGap));
    } else {
        outro = std::max(sec, intro + minGap);
    }
    if (completed) {
        m_anchorDrag.remove(grid.filePath);
        m_anchorCache.insert(grid.filePath, std::make_pair(intro, outro));
        if (library()) {
            library()->setTrackAnchors(grid.filePath, intro, outro);
        }
    } else {
        m_anchorDrag.insert(grid.filePath, { intro, outro });
    }
    // Where the dragged anchor is now, in project time, before its clip moves.
    const double anchorNow = sequenceStart + (which == 1 ? outro : intro) * source.ratio;
    applyRipple(anchorNow, completed, { intro, outro });
    if (completed) {
        m_ripple = RippleDrag {};
    }
    m_beatGridsChanged.notify();
}

void MixHouseBeatGrids::resetClipAnchors(const trackedit::ClipKey& key)
{
    const Grid grid = gridForClipName(clipName(key), false);
    if (grid.filePath.isEmpty()) {
        return;
    }
    m_anchorDrag.remove(grid.filePath);
    m_anchorCache.insert(grid.filePath, std::nullopt);
    if (library()) {
        library()->setTrackAnchors(grid.filePath, -1.0, -1.0);
    }
    m_beatGridsChanged.notify();
}

double MixHouseBeatGrids::syncGridSec() const
{
    const auto project = globalContext() ? globalContext()->currentProject() : nullptr;
    const auto trackeditProject = globalContext() ? globalContext()->currentTrackeditProject() : nullptr;
    if (!trackeditProject) {
        return 0.0;
    }
    const trackedit::TimeSignature ts = trackeditProject->timeSignature();
    if (ts.tempo <= 0.0 || ts.upper <= 0 || ts.lower <= 0) {
        return 0.0;
    }
    const double quarter = 60.0 / ts.tempo;
    const double bar = quarter * (4.0 / ts.lower) * ts.upper;
    const auto viewState = project ? project->viewState() : nullptr;
    if (!viewState || !viewState->isSnapEnabled()) {
        return bar;
    }
    double unit = bar;
    using projectscene::SnapType;
    switch (viewState->snapType()) {
    case SnapType::Bar: unit = bar;
        break;
    case SnapType::Half: unit = quarter * 2.0;
        break;
    case SnapType::Quarter: unit = quarter;
        break;
    case SnapType::Eighth: unit = quarter / 2.0;
        break;
    case SnapType::Sixteenth: unit = quarter / 4.0;
        break;
    case SnapType::ThirtySecond: unit = quarter / 8.0;
        break;
    case SnapType::SixtyFourth: unit = quarter / 16.0;
        break;
    case SnapType::HundredTwentyEighth: unit = quarter / 32.0;
        break;
    case SnapType::Beat: unit = quarter * (4.0 / ts.lower);
        break;
    case SnapType::Smart: {
        // The same rule the timeline's Smart snap uses: the finest unit still
        // wide enough to aim at, from the bar down to a quarter of a beat.
        constexpr double kMinPixels = 24.0;
        const double beat = quarter * (4.0 / ts.lower);
        const double zoom = viewState->zoomState().zoom;
        unit = bar;
        while (zoom > 0.0 && unit / 2.0 >= beat / 4.0 && (unit / 2.0) * zoom >= kMinPixels) {
            unit /= 2.0;
        }
        break;
    }
    default:
        return bar;   // seconds, samples, frames: not musical
    }
    if (viewState->isSnapTripletsEnabled() && unit < bar) {
        unit *= 2.0 / 3.0;
    }
    return unit;
}

double MixHouseBeatGrids::barLockedClipStart(const trackedit::ClipKey& key, double proposedStart) const
{
    if (libraryConfiguration() && !libraryConfiguration()->syncEnabled()) {
        return proposedStart;
    }
    const auto project = globalContext() ? globalContext()->currentProject() : nullptr;
    const auto trackeditProject = globalContext() ? globalContext()->currentTrackeditProject() : nullptr;
    if (!project || !trackeditProject) {
        return proposedStart;
    }
    const Grid grid = gridForClipName(clipName(key), false);
    if (!grid.valid()) {
        return proposedStart;
    }
    auto* au3Project = reinterpret_cast<AudacityProject*>(project->au3ProjectPtr());
    WaveTrack* track = au::au3::DomAccessor::findWaveTrack(*au3Project, ::TrackId(key.trackId));
    const auto clip = track ? au::au3::DomAccessor::findWaveClip(track, key.itemId) : nullptr;
    const trackedit::TimeSignature ts = trackeditProject->timeSignature();
    if (!clip || ts.tempo <= 0.0 || ts.upper <= 0 || ts.lower <= 0) {
        return proposedStart;
    }

    // Bar 1 of the track, measured from the clip's visible start (same mapping
    // as the aligner): bar1 = start + anchor * ratio - trimLeft.
    const double anchor = grid.downbeatSec >= 0.0 ? grid.downbeatSec : grid.firstBeatSec;
    const double bar1Offset = anchor * clip->GetStretchRatio() - clip->GetTrimLeft();
    // The Snap grid (a bar unless Snap says otherwise): bar 1 of the track on its lines.
    const double barLen = syncGridSec() > 0.0 ? syncGridSec() : (60.0 / ts.tempo) * (4.0 / ts.lower) * ts.upper;

    double bar = std::round((proposedStart + bar1Offset) / barLen) * barLen;
    // The clip can't start before 0. Bar 1 of the project is still allowed
    // when the track's lead-in before its first downbeat is at most a beat:
    // the clip sits at 0 while dragged and the lead-in is trimmed on drop
    // (seatBarOneAtProjectStart).
    const double beatLen = (60.0 / ts.tempo) * (4.0 / ts.lower);
    while (bar - bar1Offset < 0.0) {
        if (bar1Offset - bar <= beatLen + 1e-9) {
            return 0.0;
        }
        bar += barLen;
    }
    return bar - bar1Offset;
}

void MixHouseBeatGrids::seatBarOneAtProjectStart(const trackedit::ClipKey& key)
{
    if ((libraryConfiguration() && !libraryConfiguration()->syncEnabled()) || !trackeditInteraction()) {
        return;
    }
    const auto project = globalContext() ? globalContext()->currentProject() : nullptr;
    const auto trackeditProject = globalContext() ? globalContext()->currentTrackeditProject() : nullptr;
    if (!project || !trackeditProject) {
        return;
    }
    const Grid grid = gridForClipName(clipName(key), false);
    if (!grid.valid()) {
        return;
    }
    auto* au3Project = reinterpret_cast<AudacityProject*>(project->au3ProjectPtr());
    WaveTrack* track = au::au3::DomAccessor::findWaveTrack(*au3Project, ::TrackId(key.trackId));
    const auto clip = track ? au::au3::DomAccessor::findWaveClip(track, key.itemId) : nullptr;
    const trackedit::TimeSignature ts = trackeditProject->timeSignature();
    if (!clip || ts.tempo <= 0.0 || ts.upper <= 0 || ts.lower <= 0 || clip->GetPlayStartTime() > 1e-6) {
        return;
    }
    const double anchor = grid.downbeatSec >= 0.0 ? grid.downbeatSec : grid.firstBeatSec;
    const double leadIn = anchor * clip->GetStretchRatio() - clip->GetTrimLeft();
    const double beatLen = (60.0 / ts.tempo) * (4.0 / ts.lower);
    if (leadIn <= 1e-4 || leadIn > beatLen + 1e-9) {
        return;
    }
    // Trim the lead-in (the clip's start moves right by it), then back to 0:
    // bar 1 of the track is now at 0.
    trackeditInteraction()->trimClipsLeft({ key }, leadIn, 0.01, false, trackedit::UndoPushType::NONE);
    trackeditInteraction()->changeClipStartTime(key, 0.0, true);
    LOGI() << "GlideMix grid: trimmed " << leadIn << " s lead-in so bar 1 of " << grid.title << " sits on bar 1";
}

bool MixHouseBeatGrids::setSelectedClipAnchorAt(int which, double projectTime)
{
    const auto trackeditProject = globalContext() ? globalContext()->currentTrackeditProject() : nullptr;
    if (!trackeditProject || !selectionController()) {
        return false;
    }
    trackedit::ClipKeyList targets = selectionController()->selectedClips();
    if (targets.empty()) {
        for (const trackedit::TrackId trackId : selectionController()->selectedTracks()) {
            for (const trackedit::Clip& clip : trackeditProject->clipList(trackId)) {
                targets.push_back(clip.key);
            }
        }
    }
    for (const trackedit::ClipKey& key : targets) {
        const trackedit::Clip clip = trackeditProject->clip(key);
        if (!clip.isValid() || projectTime < clip.startTime || projectTime > clip.endTime) {
            continue;
        }
        if (!clipAnchors(key).valid) {
            continue;
        }
        // Same as the clip menu's "Set IN/OUT anchor here", at the playhead.
        moveClipAnchor(key, which, projectTime, true);
        return true;
    }
    return false;
}

int MixHouseBeatGrids::splitSelectedAtGridJumps()
{
    if (!selectionController()) {
        return 0;
    }
    trackedit::ClipKeyList targets = selectionController()->selectedClips();
    const auto trackeditProject = globalContext() ? globalContext()->currentTrackeditProject() : nullptr;
    if (targets.empty() && trackeditProject) {
        for (const trackedit::TrackId trackId : selectionController()->selectedTracks()) {
            for (const trackedit::Clip& clip : trackeditProject->clipList(trackId)) {
                targets.push_back(clip.key);
            }
        }
    }
    int cuts = 0;
    for (const trackedit::ClipKey& key : targets) {
        const Grid grid = gridForClipName(clipName(key), false);
        if (!grid.valid()) {
            continue;
        }
        // Bar 1 as a drop places it (alignImportedTrackToBars): the detected
        // downbeat when it is sure, else the grid's first beat.
        const double barOne = grid.downbeatSec >= 0.0 && grid.downbeatConfidence >= 0.5 ? grid.downbeatSec : grid.firstBeatSec;
        cuts += static_cast<int>(splitClipAtGridJumps(globalContext(), key, grid.bpm, barOne).size());
    }
    if (cuts > 0) {
        m_beatGridsChanged.notify();
    }
    return cuts;
}

void MixHouseBeatGrids::adjustSelectedClips(Adjust adjust)
{
    if (!library() || !library()->isAvailable() || !selectionController()) {
        return;
    }

    if (adjust == Adjust::Undo) {
        if (m_undo.empty()) {
            return;
        }
        const Grid previous = m_undo.back();
        m_undo.pop_back();
        if (library()->setTrackBeatGrid(previous.trackId, previous.bpm, previous.firstBeatSec, previous.downbeatSec,
                                        QStringLiteral("manual"))) {
            m_gridByClipName.clear();
            reseatClipsOf(previous);
        }
        return;
    }

    // Clicking in a waveform selects time and its track, not the clip, so with
    // no clip selected the tools act on the selected tracks' clips.
    trackedit::ClipKeyList targets = selectionController()->selectedClips();
    if (targets.empty()) {
        const auto trackeditProject = globalContext() ? globalContext()->currentTrackeditProject() : nullptr;
        if (trackeditProject) {
            for (const trackedit::TrackId trackId : selectionController()->selectedTracks()) {
                for (const trackedit::Clip& clip : trackeditProject->clipList(trackId)) {
                    targets.push_back(clip.key);
                }
            }
        }
    }

    // One adjustment per library track, however many of its clips are selected.
    QList<int> done;
    for (const trackedit::ClipKey& key : targets) {
        const Grid grid = gridForClipName(clipName(key), true);
        if (!grid.valid() || done.contains(grid.trackId)) {
            continue;
        }
        if (isExternalLibraryTrack(grid.trackId)) {
            LOGI() << "GlideMix grid: " << grid.title << " comes from another DJ app; edit its grid there";
            continue;
        }
        done << grid.trackId;

        Grid next = grid;
        switch (adjust) {
        case Adjust::Earlier:
            next.firstBeatSec -= kTranslateStepSec;
            if (next.downbeatSec >= 0.0) {
                next.downbeatSec = std::max(0.0, next.downbeatSec - kTranslateStepSec);
            }
            break;
        case Adjust::Later:
            next.firstBeatSec += kTranslateStepSec;
            if (next.downbeatSec >= 0.0) {
                next.downbeatSec += kTranslateStepSec;
            }
            break;
        case Adjust::Faster:
            next.bpm += kBpmAdjustStep;
            break;
        case Adjust::Slower:
            next.bpm = std::max(kBpmAdjustStep, next.bpm - kBpmAdjustStep);
            break;
        case Adjust::BarEarlier:
        case Adjust::BarLater: {
            // Bar phase: move the bar anchor (the detected downbeat, or the
            // grid's first beat when there is none) by one beat. Kept inside
            // the file by wrapping a whole bar -- same bar phase either way.
            const double beat = 60.0 / next.bpm;
            const double bar = beat * 4.0;
            const double sign = adjust == Adjust::BarEarlier ? -1.0 : 1.0;
            double& anchor = next.downbeatSec >= 0.0 ? next.downbeatSec : next.firstBeatSec;
            anchor += sign * beat;
            while (anchor < 0.0) {
                anchor += bar;
            }
            break;
        }
        case Adjust::Undo:
            break;
        }

        if (!library()->setTrackBeatGrid(next.trackId, next.bpm, next.firstBeatSec, next.downbeatSec,
                                         QStringLiteral("manual"))) {
            LOGW() << "GlideMix grid: could not adjust " << grid.title << " (BPM locked or no sample rate)";
            continue;
        }
        m_undo.push_back(grid);
        if (m_undo.size() > kMaxUndo) {
            m_undo.erase(m_undo.begin());
        }
        m_gridByClipName.clear();
        reseatClipsOf(next);
        LOGI() << "GlideMix grid: " << grid.title << " bpm " << grid.bpm << " -> " << next.bpm
               << ", first beat " << grid.firstBeatSec << " -> " << next.firstBeatSec;
    }
}

void MixHouseBeatGrids::reseatClipsOf(const Grid& grid)
{
    const auto trackeditProject = globalContext() ? globalContext()->currentTrackeditProject() : nullptr;
    if (!trackeditProject || !trackeditInteraction()) {
        return;
    }

    for (const trackedit::TrackId trackId : trackeditProject->trackIdList()) {
        for (const trackedit::Clip& clip : trackeditProject->clipList(trackId)) {
            if (gridForClipName(clip.title.toQString(), false).trackId != grid.trackId) {
                continue;
            }
            // The corrected beats go back on the bar the clip already sits
            // on: tempo from the new BPM, downbeat on the nearest bar.
            MixHouseAlignRequest request;
            request.clips = { clip.key };
            request.placement = MixHouseBarPlacement::NearestBar;
            request.bpm = grid.bpm;
            request.firstBeatSec = grid.firstBeatSec;
            request.downbeatSec = grid.downbeatSec;
            request.downbeatConfidence = grid.downbeatConfidence;
            request.projectWasEmpty = false;
            request.lockToBars = !libraryConfiguration() || libraryConfiguration()->syncEnabled();
            alignImportedTrackToBars(globalContext(), trackeditInteraction(), request);
        }
    }
    m_beatGridsChanged.notify();
}
