/*
 * MixHouse DJ 2
 */
#include "mixhouseprojectaligner.h"

#include <algorithm>
#include <cmath>

#include "au3-wave-track/WaveClip.h"
#include "au3-wave-track/WaveTrack.h"
#include "au3wrap/internal/domaccessor.h"
#include "au3wrap/internal/domconverter.h"
#include "project/iaudacityproject.h"
#include "../mixhousegridshifts.h"
#include "log.h"

namespace au::mixhouse {
namespace {
// A detected downbeat this unsure is ignored in favour of the grid's first
// beat. Measured spread on real tracks (see mixhouseanalyzer.cpp): clear
// dance tracks vote 0.87-1.00; an ambient album intro voted 0.40.
constexpr double kMinDownbeatConfidence = 0.5;
// Pre-downbeat audio at most this long is trimmed instead of pushing the
// track a whole bar later: it's the MP3 encoder delay/padding before beat 1
// (Selfie NRG's downbeat sits 9.8ms in), not an intro anyone would miss.
constexpr double kMaxLeadInTrimSec = 0.05;
// A grid jump is cut this long (source seconds) before the late section's
// first beat, so the cut lands in front of its transient.
constexpr double kJumpCutLeadSec = 0.010;

/// GlideMix: a song whose beats jump off its grid part-way (mixhousegridshifts.h)
/// is split just before each jump and the later part slid back onto the
/// grid, so the click and the other songs stay on its kicks. The late part's
/// first moment of overlap is cut from the end of the earlier part (an early
/// jump instead lets the earlier part play on into the gap). Returns the new
/// clips, left to right.
std::vector<std::shared_ptr<WaveClip> > splitAtGridJumps(WaveTrack* track, const std::shared_ptr<WaveClip>& clip,
                                                          double bpm, double barOneInFile)
{
    std::vector<std::shared_ptr<WaveClip> > added;
    const double ratio = clip->GetStretchRatio();
    const size_t count = clip->GetVisibleSampleCount().as_size_t();
    const size_t channels = clip->NChannels();
    if (count == 0 || channels == 0 || ratio <= 0.0) {
        return added;
    }
    std::vector<float> mono(count, 0.0f), buf(count);
    for (size_t ch = 0; ch < channels; ++ch) {
        if (!clip->GetSamples(ch, reinterpret_cast<samplePtr>(buf.data()), floatSample, sampleCount(0), count, false)) {
            return added;
        }
        for (size_t i = 0; i < count; ++i) {
            mono[i] += buf[i] / channels;
        }
    }
    // mono[0] is this file second (the trim is in play seconds).
    const double fileOffset = clip->GetTrimLeft() / ratio;
    const std::vector<MixHouseGridShift> shifts
        = detectGridShifts(mono.data(), count, clip->GetRate(), bpm, barOneInFile - fileOffset);
    // Timeline second of a file second, before anything below moves.
    const double base = clip->GetPlayStartTime() - clip->GetTrimLeft();
    std::shared_ptr<WaveClip> current = clip;
    double moved = 0.0, previous = 0.0;
    for (const MixHouseGridShift& shift : shifts) {
        const double fileStart = shift.startSec + fileOffset;
        const double delta = (shift.shiftSec - previous) * ratio;   // play seconds the rest moves back
        const double at = base + (fileStart + shift.shiftSec - kJumpCutLeadSec) * ratio - moved;
        if (at <= current->GetPlayStartTime() + 0.1 || at >= current->GetPlayEndTime() - 0.1) {
            continue;
        }
        auto [left, right] = track->SplitAt(at);
        if (!left || !right) {
            continue;
        }
        right->ShiftBy(-delta);
        left->TrimRightTo(left->GetPlayEndTime() - delta);
        LOGI() << "GlideMix grid jump: " << shift.shiftSec * 1000.0 << " ms from " << fileStart
               << " s in the file; split at " << at << " s, later part moved " << -delta * 1000.0 << " ms";
        added.push_back(right);
        current = right;
        moved += delta;
        previous = shift.shiftSec;
    }
    return added;
}
}

double compatibleTrackBpm(double trackBpm, double projectBpm)
{
    if (trackBpm <= 0.0 || projectBpm <= 0.0) {
        return trackBpm;
    }
    double best = trackBpm;
    double bestError = std::abs(projectBpm / trackBpm - 1.0);
    for (double candidate : { trackBpm * 2.0, trackBpm / 2.0 }) {
        const double error = std::abs(projectBpm / candidate - 1.0);
        if (error < bestError) {
            best = candidate;
            bestError = error;
        }
    }
    return best;
}

MixHouseAlignResult alignImportedTrackToBars(const std::shared_ptr<context::IGlobalContext>& globalContext,
                                             const std::shared_ptr<trackedit::ITrackeditInteraction>& trackeditInteraction,
                                             const MixHouseAlignRequest& request)
{
    MixHouseAlignResult result;
    if (request.bpm <= 0.0) {
        result.reason = QStringLiteral("track has no BPM (not analyzed)");
        return result;
    }
    const auto project = globalContext ? globalContext->currentProject() : nullptr;
    const auto trackeditProject = globalContext ? globalContext->currentTrackeditProject() : nullptr;
    if (!project || !trackeditProject || !trackeditInteraction) {
        result.reason = QStringLiteral("no open project");
        return result;
    }
    auto* au3Project = reinterpret_cast<AudacityProject*>(project->au3ProjectPtr());

    struct ImportedClip {
        trackedit::TrackId trackId;
        WaveTrack* track;
        std::shared_ptr<WaveClip> clip;
    };
    std::vector<ImportedClip> clips;
    for (const trackedit::ClipKey& key : request.clips) {
        WaveTrack* track = au::au3::DomAccessor::findWaveTrack(*au3Project, ::TrackId(key.trackId));
        if (auto clip = track ? au::au3::DomAccessor::findWaveClip(track, key.itemId) : nullptr) {
            clips.push_back({ key.trackId, track, clip });
        }
    }
    for (const trackedit::TrackId trackId : request.clips.empty() ? request.importedTracks : trackedit::TrackIdList()) {
        WaveTrack* track = au::au3::DomAccessor::findWaveTrack(*au3Project, ::TrackId(trackId));
        if (!track) {
            continue;
        }
        for (const auto& interval : track->SortedIntervalArray()) {
            clips.push_back({ trackId, track, interval });
        }
    }
    if (clips.empty()) {
        result.reason = QStringLiteral("imported track has no clips");
        return result;
    }

    // --- Tempo -----------------------------------------------------------
    // Exactly TempoDetection's two paths (importexport/import/internal/au3/
    // tempodetection.cpp): setRawAudioTempoOnClips + setProjectTempo for an
    // empty project, applyTempoToClips otherwise.
    // Half/double tempo counts as the same tempo (compatibleTrackBpm).
    const double rawTempo = request.projectWasEmpty
                            ? request.bpm
                            : compatibleTrackBpm(request.bpm, trackeditProject->timeSignature().tempo);
    for (const ImportedClip& c : clips) {
        c.clip->SetRawAudioTempo(rawTempo);
    }
    if (request.projectWasEmpty) {
        trackedit::TimeSignature ts = trackeditProject->timeSignature();
        ts.tempo = request.bpm;
        trackeditProject->setTimeSignature(ts);   // the project tempo listener re-stretches clips
        result.setProjectTempo = true;
    } else {
        const double projectTempo = trackeditProject->timeSignature().tempo;
        for (const ImportedClip& c : clips) {
            c.clip->OnProjectTempoChange(std::nullopt, projectTempo);
        }
    }
    for (const ImportedClip& c : clips) {
        trackeditProject->notifyAboutClipChanged(au::au3::DomConverter::clip(c.track, c.clip.get()));
    }

    // --- Placement -------------------------------------------------------
    const trackedit::TimeSignature ts = trackeditProject->timeSignature();
    result.projectTempo = ts.tempo;
    if (!request.lockToBars) {
        // SYNC off: no bar lock. A drop stays where it landed; a library load
        // starts at the playhead, like a plain import.
        const ImportedClip& first = clips.front();
        if (request.placement == MixHouseBarPlacement::NextBarAtPlayhead) {
            const double playhead = globalContext->playbackState()
                                    ? static_cast<double>(globalContext->playbackState()->playbackPosition()) : 0.0;
            const double offset = std::max(0.0, playhead) - first.clip->GetPlayStartTime();
            for (const ImportedClip& c : clips) {
                trackeditInteraction->changeClipStartTime(trackedit::ClipKey(c.trackId, c.clip->GetId()),
                                                          c.clip->GetPlayStartTime() + offset, true);
            }
        }
        result.stretchRatio = first.clip->GetStretchRatio();
        result.aligned = true;   // placed as SYNC-off asks; lets a multi-file drop stretch the rest to the first
        result.reason = QStringLiteral("SYNC is off: tempo applied, not locked to bars");
        LOGI() << "GlideMix align: bpm " << request.bpm << " -> project " << ts.tempo << ", SYNC off, no bar lock";
        return result;
    }
    if (ts.tempo <= 0.0 || ts.upper <= 0 || ts.lower <= 0) {
        result.reason = QStringLiteral("project has no valid time signature");
        return result;
    }
    // ProjectTimeSignature::GetBarDuration(): quarter * 4/lower * upper.
    // Placement steps by a phrase (snapBars bars) from project bar 1.
    const double barLen = (60.0 / ts.tempo) * (4.0 / ts.lower) * ts.upper * std::max(1, request.snapBars);

    result.usedDetectedDownbeat = request.downbeatSec >= 0.0 && request.downbeatConfidence >= kMinDownbeatConfidence;
    double downbeatInFile = result.usedDetectedDownbeat ? request.downbeatSec : request.firstBeatSec;
    // A grid's first beat may sit before the file starts (mixhousebeats.h:
    // "may be negative"); step forward whole bars of the TRACK's own tempo so
    // the anchor is real audio with the same bar phase.
    const double trackBarSec = 4.0 * 60.0 / request.bpm;
    // A grid beat a few ms before the file's first sample (a kick at 0 with the
    // grid at -6 ms) is still bar 1: it stays put, so that kick lands on the bar.
    while (downbeatInFile < -kMaxLeadInTrimSec) {
        downbeatInFile += trackBarSec;
    }

    const ImportedClip first = clips.front();   // a copy: grid jumps add clips below
    result.stretchRatio = first.clip->GetStretchRatio();
    // Source second -> play seconds after the clip's start: a source second a
    // plays at sequenceOffset + a * ratio, and the clip's audible start is
    // sequenceOffset + trimLeft (trim is already in play seconds).
    double downbeatAfterStart = downbeatInFile * result.stretchRatio - first.clip->GetTrimLeft();

    double bar = 0.0;
    if (request.placement == MixHouseBarPlacement::NearestBar) {
        const double landedAt = first.clip->GetPlayStartTime() + downbeatAfterStart;
        bar = std::max(0.0, std::round(landedAt / barLen)) * barLen;
    } else {
        const double playhead = globalContext->playbackState()
                                ? static_cast<double>(globalContext->playbackState()->playbackPosition()) : 0.0;
        bar = std::ceil(std::max(0.0, playhead) / barLen - 1e-9) * barLen;
    }
    // Up to one beat before the first downbeat is trimmed so the track can sit
    // on this bar (bar 1 in an empty project) -- the same rule as dragging a
    // clip to bar 1 (MixHouseBeatGrids::lockedClipStart). More than a beat is a
    // real intro and moves to the next phrase instead.
    const double beatSec = 60.0 / ts.tempo * (4.0 / ts.lower);
    if (bar - downbeatAfterStart < 0.0 && downbeatAfterStart > 0.0
        && downbeatAfterStart <= std::max(kMaxLeadInTrimSec, beatSec + 1e-9)
        && clips.size() == 1) {
        first.clip->TrimLeftTo(first.clip->GetPlayStartTime() + downbeatAfterStart);
        trackeditProject->notifyAboutClipChanged(au::au3::DomConverter::clip(first.track, first.clip.get()));
        downbeatAfterStart = 0.0;
    }
    while (bar - downbeatAfterStart < 0.0) {
        bar += barLen;   // a real intro must not start before 0 -- it hangs left of a later bar
    }
    const double newStart = bar - downbeatAfterStart;

    // Every clip of a (normally single-clip) import moves by the same offset,
    // so their relative positions are kept. Computed once: the first clip's
    // start changes as soon as it's moved.
    const double offset = newStart - first.clip->GetPlayStartTime();
    if (request.splitAtGridJumps && clips.size() == 1) {
        for (const auto& piece : splitAtGridJumps(first.track, first.clip, request.bpm, downbeatInFile)) {
            trackeditProject->notifyAboutClipAdded(au::au3::DomConverter::clip(first.track, piece.get()));
            clips.push_back({ first.trackId, first.track, piece });
            result.splitClips.push_back(trackedit::ClipKey(first.trackId, piece->GetId()));
        }
        if (clips.size() > 1) {
            trackeditProject->notifyAboutClipChanged(au::au3::DomConverter::clip(first.track, first.clip.get()));
            // Moving right, the rightmost piece goes first so no piece is
            // moved onto the next one.
            if (offset > 0.0) {
                std::reverse(clips.begin(), clips.end());
            }
        }
    }
    for (const ImportedClip& c : clips) {
        trackeditInteraction->changeClipStartTime(trackedit::ClipKey(c.trackId, c.clip->GetId()),
                                                  c.clip->GetPlayStartTime() + offset, true);
    }

    result.barSec = bar;
    result.aligned = true;
    LOGI() << "GlideMix align: bpm " << request.bpm << " -> project " << ts.tempo
           << (result.setProjectTempo ? " (set from track)" : " (stretched)")
           << ", ratio " << result.stretchRatio
           << ", downbeat " << downbeatInFile << "s (" << (result.usedDetectedDownbeat ? "detected" : "grid first beat")
           << ") on bar at " << bar << "s, clip start " << newStart << "s";
    return result;
}

trackedit::ClipKeyList splitClipAtGridJumps(const std::shared_ptr<context::IGlobalContext>& globalContext,
                                            const trackedit::ClipKey& key, double bpm, double barOneInFile)
{
    trackedit::ClipKeyList added;
    const auto project = globalContext ? globalContext->currentProject() : nullptr;
    const auto trackeditProject = globalContext ? globalContext->currentTrackeditProject() : nullptr;
    if (!project || !trackeditProject || bpm <= 0.0) {
        return added;
    }
    auto* au3Project = reinterpret_cast<AudacityProject*>(project->au3ProjectPtr());
    WaveTrack* track = au::au3::DomAccessor::findWaveTrack(*au3Project, ::TrackId(key.trackId));
    const auto clip = track ? au::au3::DomAccessor::findWaveClip(track, key.itemId) : nullptr;
    if (!clip) {
        return added;
    }
    // A grid's bar 1 may sit before the file starts: the same bar phase, a
    // whole bar later (as alignImportedTrackToBars).
    while (barOneInFile < -kMaxLeadInTrimSec) {
        barOneInFile += 4.0 * 60.0 / bpm;
    }
    for (const auto& piece : splitAtGridJumps(track, clip, bpm, barOneInFile)) {
        trackeditProject->notifyAboutClipAdded(au::au3::DomConverter::clip(track, piece.get()));
        added.push_back(trackedit::ClipKey(key.trackId, piece->GetId()));
    }
    if (!added.empty()) {
        trackeditProject->notifyAboutClipChanged(au::au3::DomConverter::clip(track, clip.get()));
    }
    return added;
}
}
