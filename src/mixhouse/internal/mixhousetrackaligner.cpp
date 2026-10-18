/*
 * MixHouse DJ 2
 */
#include "mixhousetrackaligner.h"

#include <algorithm>

#include "mixhouseprojectaligner.h"
#include "log.h"

using namespace au::mixhouse;

MixHouseTrackAligner::MixHouseTrackAligner(const muse::modularity::ContextPtr& iocCtx)
    : muse::Contextable(iocCtx)
{
}

MixHouseTrackList MixHouseTrackAligner::findLibraryTrack(const muse::io::path_t& filePath) const
{
    if (!library() || !library()->isAvailable()) {
        return {};
    }
    IMixHouseLibrary::TrackQuery query;
    query.location = filePath.toQString();
    MixHouseTrackList found = library()->tracks(query);
    if (found.empty()) {
        found = externalLibraryTracks(lexicon().get(), rekordboxXml().get(), query);   // dragged from Lexicon DJ / rekordbox: its beat grid
    }
    return found;
}

bool MixHouseTrackAligner::knowsTempo(const muse::io::path_t& filePath) const
{
    const MixHouseTrackList found = findLibraryTrack(filePath);
    return !found.empty() && found.front().bpm > 0.0;
}

void MixHouseTrackAligner::alignDroppedFiles(const std::vector<muse::io::path_t>& filePaths,
                                             const std::vector<trackedit::ClipKeyList>& newClipsPerFile,
                                             bool projectWasEmpty, double pxPerSec)
{
    if (!library() || !library()->isAvailable()) {
        return;
    }
    bool firstAligned = true;
    for (size_t i = 0; i < filePaths.size() && i < newClipsPerFile.size(); ++i) {
        if (newClipsPerFile[i].empty()) {
            continue;
        }
        const MixHouseTrackList found = findLibraryTrack(filePaths[i]);
        if (found.empty()) {
            continue;   // not a library track -- a plain audio file drop stays Audacity's own business
        }
        const MixHouseTrack& t = found.front();

        MixHouseAlignRequest request;
        request.clips = newClipsPerFile[i];
        request.placement = MixHouseBarPlacement::NearestBar;
        request.bpm = t.bpm;
        request.firstBeatSec = t.beats.firstBeatSec;
        request.downbeatSec = t.downbeatSec;
        request.downbeatConfidence = t.downbeatConfidence;
        // Only the first aligned file of a drop into an empty project sets
        // the tempo; the rest stretch to it.
        request.projectWasEmpty = projectWasEmpty && firstAligned;
        request.lockToBars = !libraryConfiguration() || libraryConfiguration()->syncEnabled();
        request.snapBars = 1;

        const MixHouseAlignResult result = alignImportedTrackToBars(globalContext(), trackeditInteraction(), request);
        if (result.aligned) {
            firstAligned = false;
            // MixMeister: a track dropped near another track's Outro anchor
            // snaps its Intro anchor onto it (or its Outro onto an Intro).
            // The pull reaches 80 pixels at the drop's zoom, between one
            // 16-bar phrase and 64 bars: a fixed 16 bars was a few pixels
            // zoomed out (drops didn't link), a fixed 64 bars moved drops a
            // minute away zoomed in.
            const auto project = globalContext() ? globalContext()->currentTrackeditProject() : nullptr;
            if (beatGrids() && project && !request.clips.empty()) {
                const trackedit::TimeSignature ts = project->timeSignature();
                const double bar = ts.tempo > 0.0 ? 60.0 / ts.tempo * (4.0 / std::max(1, ts.lower)) * std::max(1, ts.upper) : 2.0;
                const trackedit::ClipKey first = request.clips.front();
                const double start = project->clip(first).startTime;
                const double snapped = beatGrids()->magneticClipStart(first, start, std::clamp(pxPerSec > 0.0 ? 80.0 / pxPerSec : 0.0, bar * 16.0, bar * 64.0) + 1e-3);
                if (std::abs(snapped - start) > 1e-6) {
                    const double offset = snapped - start;
                    for (const trackedit::ClipKey& clip : request.clips) {
                        trackeditInteraction()->changeClipStartTime(clip, project->clip(clip).startTime + offset, true);
                    }
                    LOGI() << "GlideMix drop: anchors linked, moved " << offset << " s: " << t.title;
                }
                // Dropped into a transition: the songs after it make room.
                beatGrids()->insertAtTransition(first, false);
            }
            if (tempoMap() && !request.projectWasEmpty) {
                tempoMap()->trackPlaced(result.stretchRatio);
            }
        } else {
            LOGI() << "GlideMix drop: not aligned to bars (" << result.reason << "): " << t.title;
        }
    }
    // The import's undo step was recorded before the alignment: bring it up
    // to date, or redo after an undo would bring the drop back unaligned.
    if (!firstAligned && projectHistory()) {
        projectHistory()->modifyState();
    }
}

void MixHouseTrackAligner::requestViewFocus(double start, double end, au::trackedit::TrackId track)
{
    m_viewFocusRequested.send(start, end, track);
}

muse::async::Channel<double, double, au::trackedit::TrackId> MixHouseTrackAligner::viewFocusRequested() const
{
    return m_viewFocusRequested;
}
