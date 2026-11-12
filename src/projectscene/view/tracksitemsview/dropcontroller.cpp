#include "dropcontroller.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

#include "mixhouse/mixhousefiletags.h"
#include "mixhouse/mixhousesamplename.h"
#include "translation.h"
#include "projectscene/iprojectviewstate.h"
#include "project/iaudacityproject.h"

#include <algorithm>
#include <cmath>

using namespace au::projectscene;

namespace {
auto isAudioTrack = [](au::trackedit::TrackType type) {
    return type == au::trackedit::TrackType::Mono || type == au::trackedit::TrackType::Stereo;
};
}

DropController::DropController(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{}

void DropController::probeAudioFiles(const QStringList& fileUrls)
{
    m_lastDraggedUrls.clear();
    m_lastDraggedFilesInfo.clear();
    m_lastDraggedTitles.clear();

    std::vector<muse::io::path_t> localPaths;
    localPaths.reserve(fileUrls.size());

    const auto exts = importer()->supportedExtensions();
    for (const auto& fileUrl : fileUrls) {
        const QUrl url(fileUrl);
        QString local = url.isLocalFile() ? url.toLocalFile() : fileUrl;
        muse::io::path_t path = muse::io::path_t(local);
        if (muse::contains(exts, muse::io::suffix(path))) {
            localPaths.push_back(path);
            m_lastDraggedUrls.push_back(fileUrl);
        }
    }

    if (localPaths.empty()) {
        return;
    }

    for (const auto& path : localPaths) {
        au::importexport::FileInfo fileInfo = importer()->fileInfo(path);
        if (fileInfo.isEmpty()) {
            continue;
        }
        m_lastDraggedFilesInfo.push_back(std::move(fileInfo));
        // GlideMix: who it is while dragging, the file name when it has no tags.
        // Read here, once per drag: the names are asked for on every enter.
        const au::mixhouse::MixHouseFileTags tags = au::mixhouse::readFileTags(path.toQString());
        m_lastDraggedTitles.push_back(!tags.fromTags ? muse::io::filename(path, false /* including extension */).toQString()
                                      : tags.artist.isEmpty() ? tags.title : tags.artist + QStringLiteral(" - ") + tags.title);
    }
}

QVariantList DropController::lastProbedDurations() const
{
    QVariantList out;
    out.reserve(static_cast<int>(m_lastDraggedFilesInfo.size()));
    for (const auto& info : m_lastDraggedFilesInfo) {
        for (int i = 0; i < info.trackCount; ++i) {
            out.push_back(info.duration);
        }
    }
    return out;
}

QVariantList DropController::lastProbedFileNames() const
{
    QVariantList out;
    out.reserve(static_cast<int>(requiredTracksCount()));

    auto titleIter = m_lastDraggedTitles.cbegin();
    auto infoIter = m_lastDraggedFilesInfo.cbegin();
    for (; titleIter != m_lastDraggedTitles.cend() && infoIter != m_lastDraggedFilesInfo.cend();
         ++titleIter, ++infoIter) {
        for (int n = 0; n < infoIter->trackCount; ++n) {
            out.push_back(*titleIter);
        }
    }

    return out;
}

void DropController::startImportSession()
{
    if (m_trackCountBeforeImport != -1) {
        return;
    }

    au::trackedit::ITrackeditProjectPtr prj = globalContext()->currentTrackeditProject();
    if (!prj) {
        return;
    }

    m_trackCountBeforeImport = prj->trackList().size();
}

void DropController::endImportSession()
{
    tracksInteraction()->removeDragAddedTracks(m_trackCountBeforeImport, true /* emptyOnly */);

    m_trackCountBeforeImport = -1;
    m_lastDraggedFilesInfo.clear();
    m_lastDraggedUrls.clear();
}

int DropController::requiredTracksCount() const
{
    int count = 0;
    for (const auto& info : m_lastDraggedFilesInfo) {
        count += info.trackCount;
    }

    return count;
}

void DropController::prepareConditionalTracks(int currentTrackId, int draggedFilesCount)
{
    if (draggedFilesCount <= 0) {
        return;
    }

    au::trackedit::ITrackeditProjectPtr prj = globalContext()->currentTrackeditProject();
    if (!prj) {
        return;
    }

    std::vector<trackedit::Track> trackList = prj->trackList();

    const int totalTracks = static_cast<int>(trackList.size());

    const int tracksCreated
        =(m_trackCountBeforeImport >= 0)
          ? std::max(0, totalTracks - m_trackCountBeforeImport)
          : 0;

    if (tracksCreated >= draggedFilesCount) {
        return;
    }

    int currentIndex = -1;
    for (int i = 0; i < totalTracks; ++i) {
        if (trackList[i].id == currentTrackId) {
            currentIndex = i;
            break;
        }
    }

    int startIndex = (currentIndex < 0) ? totalTracks : currentIndex;

    int availableAudioTracks = 0;
    for (int i = startIndex; i < totalTracks; ++i) {
        if (isAudioTrack(trackList[i].type)) {
            ++availableAudioTracks;
        }
    }

    int missingTracks = draggedFilesCount - availableAudioTracks;
    if (missingTracks <= 0) {
        return;
    }

    const int maxNewAllowed = draggedFilesCount - tracksCreated;
    if (maxNewAllowed <= 0) {
        return;
    }

    const int toCreate = std::min(missingTracks, maxNewAllowed);

    for (int i = 0; i < toCreate; ++i) {
        tracksInteraction()->addWaveTrack(1);
    }
}

QVariantList DropController::draggedTracksIds(int currentTrackId, int draggedFilesCount)
{
    au::trackedit::ITrackeditProjectPtr prj = globalContext()->currentTrackeditProject();
    if (!prj) {
        return {};
    }

    std::vector<trackedit::Track> trackList = prj->trackList();

    QVariantList trackIds;
    if (draggedFilesCount <= 0 || trackList.empty()) {
        return trackIds;
    }

    const int total = static_cast<int>(trackList.size());

    // collect indices of all audio tracks
    std::vector<int> audioIndices;
    audioIndices.reserve(total);
    for (int i = 0; i < total; ++i) {
        if (isAudioTrack(trackList[i].type)) {
            audioIndices.push_back(i);
        }
    }

    if (audioIndices.empty()) {
        return trackIds;
    }

    int currentRow = -1;
    if (currentTrackId >= 0) {
        for (int i = 0; i < total; ++i) {
            if (trackList[i].id == currentTrackId) {
                currentRow = i;
                break;
            }
        }
    }

    int thresholdRow;
    if (currentRow >= 0) {
        // cursor is over a track, start from that track
        thresholdRow = currentRow;
    } else {
        // cursor is below tracks, start from newly created track
        thresholdRow = std::max(0, m_trackCountBeforeImport);
    }

    int startAudioPos = 0;
    while (startAudioPos < static_cast<int>(audioIndices.size())
           && audioIndices[startAudioPos] < thresholdRow) {
        ++startAudioPos;
    }

    const int availFromStart = static_cast<int>(audioIndices.size()) - startAudioPos;

    if (availFromStart >= draggedFilesCount) {
        for (int i = 0; i < draggedFilesCount; ++i) {
            int trackIndex = audioIndices[startAudioPos + i];
            trackIds.push_back(static_cast<int>(trackList[trackIndex].id));
        }
        return trackIds;
    }

    // fallback, not enough tracks: use last `draggedFilesCount` audio tracks
    const int totalAudio = static_cast<int>(audioIndices.size());
    const int toTake = std::min(draggedFilesCount, totalAudio);
    const int firstIndex = totalAudio - toTake;

    for (int i = firstIndex; i < totalAudio; ++i) {
        int trackIndex = audioIndices[i];
        trackIds.push_back(static_cast<int>(trackList[trackIndex].id));
    }

    return trackIds;
}

void DropController::removeDragAddedTracks(int currentTrackId, int draggedFilesCount)
{
    if (draggedFilesCount <= 0 || m_trackCountBeforeImport < 0) {
        return;
    }

    au::trackedit::ITrackeditProjectPtr prj = globalContext()->currentTrackeditProject();
    if (!prj) {
        return;
    }

    std::vector<trackedit::Track> trackList = prj->trackList();

    const int total = static_cast<int>(trackList.size());
    if (total <= m_trackCountBeforeImport) {
        return;
    }

    int currentIndex = -1;
    for (int i = 0; i < total; ++i) {
        if (trackList[i].id == currentTrackId) {
            currentIndex = i;
            break;
        }
    }

    int startIndex;
    if (currentIndex >= 0) {
        // cursor is over some existing track (label or audio)
        startIndex = currentIndex;
    } else if (m_trackCountBeforeImport >= 0
               && m_trackCountBeforeImport < total) {
        // cursor is below the last track
        startIndex = m_trackCountBeforeImport;
    } else {
        // fallback
        startIndex = 0;
    }

    int remaining = draggedFilesCount;
    int highestUsedIndex = m_trackCountBeforeImport - 1;

    // detect where dragged files will land: walk from startIndex downwards,
    // counting only audio tracks.
    for (int i = startIndex; i < total && remaining > 0; ++i) {
        if (!isAudioTrack(trackList[i].type)) {
            continue;
        }

        highestUsedIndex = std::max(highestUsedIndex, i);
        --remaining;
    }

    int neededTracksCount = m_trackCountBeforeImport;
    if (highestUsedIndex >= 0) {
        neededTracksCount = std::max(neededTracksCount, highestUsedIndex + 1);
    }

    // remove only extra empty tracks beyond neededTracksCount
    tracksInteraction()->removeDragAddedTracks(neededTracksCount, true /* emptyOnly */);
}

void DropController::listenForPlacedSongs()
{
    if (!mixhouseAligner()) {
        return;
    }
    mixhouseAligner()->viewFitRequested().onReceive(this, [this](double start, double end, trackedit::TrackId track) {
        emit viewFitRequested(start, end, static_cast<int>(track));
    });
    mixhouseAligner()->viewFocusRequested().onReceive(this, [this](double start, double end, trackedit::TrackId track) {
        emit viewFocusRequested(start, end, static_cast<int>(track));
    });
}

QVariantList DropController::lastDropFocusRange() const
{
    if (m_focusEnd <= m_focusStart) {
        return {};
    }
    return { m_focusStart, m_focusEnd };
}

bool DropController::isSampleLine(trackedit::TrackId trackId) const
{
    const auto prj = globalContext()->currentProject();
    const auto vs = prj ? prj->viewState() : nullptr;
    return vs && vs->isSampleLine(trackId);
}

DropController::SampleDrop DropController::describeSample(const au::importexport::FileInfo& info) const
{
    SampleDrop out;
    const QString path = info.path.toQString();
    if (samples()) {
        for (const mixhouse::MixHouseSample& s : samples()->samples()) {
            if (s.path == path) {
                out.known = true;
                out.loopBpm = s.loop ? s.bpm : 0.0;
                return out;
            }
        }
    }
    // Not in the Samples tab (dragged from Finder): what its name says.
    const QFileInfo fi(path);
    const mixhouse::SampleNameInfo named = mixhouse::parseSampleName(fi.dir().dirName() + QStringLiteral("/") + fi.fileName());
    mixhouse::SampleNameInfo::Type type = named.type;
    if (type == mixhouse::SampleNameInfo::Type::Unknown) {
        type = named.bpm > 0.0 ? mixhouse::guessTypeFromLength(info.duration, named.bpm) : mixhouse::SampleNameInfo::Type::OneShot;
    }
    out.loopBpm = type == mixhouse::SampleNameInfo::Type::Loop ? named.bpm : 0.0;
    return out;
}

void DropController::dropOnSampleLine(trackedit::TrackId sampleLine, double startTime)
{
    project::IAudacityProjectPtr prj = globalContext()->currentProject();
    const auto editPrj = globalContext()->currentTrackeditProject();
    if (!prj || !editPrj) {
        return;
    }
    const trackedit::TimeSignature ts = editPrj->timeSignature();
    const double tempo = ts.tempo > 0.0 ? ts.tempo : 120.0;
    const double bar = 60.0 / tempo * (4.0 / std::max(1, ts.lower)) * std::max(1, ts.upper);

    trackedit::ClipKeyList dropped;
    double at = std::max(0.0, startTime);
    for (const auto& info : m_lastDraggedFilesInfo) {
        const SampleDrop sample = describeSample(info);
        const bool loop = sample.loopBpm > 0.0;
        // Its length once placed: a loop is stretched to the song tempo,
        // folded by halves and doubles (as the aligner does).
        double length = info.duration;
        if (loop) {
            double raw = sample.loopBpm, best = raw, bestError = std::abs(tempo / raw - 1.0);
            for (double candidate : { raw * 2.0, raw / 2.0 }) {
                const double error = std::abs(tempo / candidate - 1.0);
                if (error < bestError) {
                    best = candidate;
                    bestError = error;
                }
            }
            length = info.duration * best / tempo;
        }
        double start = loop ? std::max(0.0, std::round(at / bar) * bar) : at;
        // An import cuts whatever it lands on: start after any sample in the way.
        for (bool moved = true; moved;) {
            moved = false;
            for (const trackedit::Clip& clip : editPrj->clipList(sampleLine)) {
                if (clip.startTime < start + length - 1e-3 && clip.endTime > start + 1e-3) {
                    start = loop ? std::ceil(clip.endTime / bar - 1e-3) * bar : double(clip.endTime);
                    moved = true;
                }
            }
        }

        std::vector<trackedit::ClipKey> before;
        for (const trackedit::Clip& clip : editPrj->clipList(sampleLine)) {
            before.push_back(clip.key);
        }
        prj->importIntoTracks({ info.path }, { sampleLine }, start, { false });
        trackedit::ClipKeyList created;
        for (const trackedit::Clip& clip : editPrj->clipList(sampleLine)) {
            if (std::find_if(before.begin(), before.end(), [&](const trackedit::ClipKey& k) {
                return k.itemId == clip.key.itemId;
            }) == before.end()) {
                created.push_back(clip.key);
            }
        }
        if (created.empty()) {
            continue;
        }
        if (mixhouseAligner()) {
            mixhouseAligner()->prepareSampleClips(created, sample.loopBpm);
        }
        double end = start;
        for (const trackedit::ClipKey& key : created) {
            end = std::max(end, double(editPrj->clip(key).endTime));
        }
        at = end;
        dropped.insert(dropped.end(), created.begin(), created.end());
    }
    if (dropped.empty()) {
        return;
    }
    if (projectHistory()) {
        projectHistory()->modifyState();
    }
    if (selectionController()) {
        selectionController()->setSelectedClips(dropped, true);
    }
    m_focusTrack = static_cast<int>(sampleLine);
}

void DropController::handleDroppedFiles(const std::vector<trackedit::TrackId>& trackIds, double startTime)
{
    m_focusStart = m_focusEnd = 0.0;
    m_focusTrack = -1;

    // GlideMix Samples: the sample line takes every dropped file as a
    // sample. A sample from the Samples tab belongs on a sample line only:
    // dropped on a song or an empty place, it is refused with a hint.
    if (!trackIds.empty() && isSampleLine(trackIds.front())) {
        dropOnSampleLine(trackIds.front(), startTime);
        return;
    }
    for (const auto& info : m_lastDraggedFilesInfo) {
        if (describeSample(info).known) {
            if (activity()) {
                activity()->post(muse::qtrc("projectscene", "Samples go on a song's sample line: press SAMPLES on the song, then drop it there"));
            }
            return;
        }
    }
    std::vector<muse::io::path_t> localPaths;

    // NOTE: importer only needs the first trackId (out of many) for multichannel files
    // while `trackIds` contains all, we may need to skip some of them
    std::vector<trackedit::TrackId> adjustedDstTrackIds;
    auto dstTrackIter = trackIds.begin();
    for (const auto& info : m_lastDraggedFilesInfo) {
        localPaths.push_back(info.path);

        adjustedDstTrackIds.push_back(*dstTrackIter);
        std::advance(dstTrackIter, info.trackCount);
    }

    // GlideMix: a song dropped onto another song would cut it (Audacity
    // trims whatever an import lands on). Each file goes to a row that is
    // clear where it lands -- the one it was dropped on if it is, else the
    // next clear row below, else a new row. A library song can still move
    // after the drop (bar lock, and its IN pulled onto an OUT up to 64 bars
    // away), so its row must be clear that far around it too.
    if (beatGrids()) {
        const auto trackeditPrj = globalContext()->currentTrackeditProject();
        const double tempo = trackeditPrj && trackeditPrj->timeSignature().tempo > 0.0 ? trackeditPrj->timeSignature().tempo : 120.0;
        const double reach = 64.0 * 4.0 * 60.0 / tempo;
        trackedit::TrackIdList taken;
        for (size_t i = 0; i < adjustedDstTrackIds.size() && i < m_lastDraggedFilesInfo.size(); ++i) {
            const auto& info = m_lastDraggedFilesInfo[i];
            const double margin = mixhouseAligner() && mixhouseAligner()->knowsTempo(info.path) ? reach : 0.0;
            adjustedDstTrackIds[i] = beatGrids()->clearRow(adjustedDstTrackIds[i], startTime - margin,
                                                           startTime + info.duration + margin, 1, {}, taken);
            taken.push_back(adjustedDstTrackIds[i]);
        }
    }

    project::IAudacityProjectPtr prj = globalContext()->currentProject();

    // MixHouse: remember which clips already existed on each destination
    // track, so the clips a drop creates can be handed to beat-to-bar
    // alignment afterwards (a track can already hold other clips).
    const auto trackeditProject = globalContext()->currentTrackeditProject();
    const bool projectWasEmpty = !trackeditProject || !trackeditProject->hasAudioContent().val;
    auto clipKeysOn = [&](trackedit::TrackId trackId) {
        std::vector<trackedit::ClipKey> keys;
        if (trackeditProject) {
            for (const trackedit::Clip& clip : trackeditProject->clipList(trackId)) {
                keys.push_back(clip.key);
            }
        }
        return keys;
    };
    std::vector<std::vector<trackedit::ClipKey> > clipsBefore;
    for (const trackedit::TrackId trackId : adjustedDstTrackIds) {
        clipsBefore.push_back(clipKeysOn(trackId));
    }

    //! A library track already has its BPM and beat grid, and the aligner below
    //! applies them. Running the import's own tempo detection on it as well
    //! would only guess again and ask you to confirm a guess that is then
    //! thrown away. Plain audio files still get it: nothing else gives them a
    //! tempo.
    std::vector<bool> detectTempo;
    for (const muse::io::path_t& path : localPaths) {
        detectTempo.push_back(!(mixhouseAligner() && mixhouseAligner()->knowsTempo(path)));
    }

    prj->importIntoTracks(localPaths, adjustedDstTrackIds, startTime, detectTempo);

    // GlideMix: a track still called "Audio 3" and holding only the song just
    // dropped takes the song's name. A name the user gave is kept.
    bool renamed = false;
    static const QRegularExpression kDefaultName(QStringLiteral("^Audio \\d+$"));
    const auto editProject = globalContext()->currentTrackeditProject();
    for (const trackedit::TrackId trackId : editProject ? adjustedDstTrackIds : std::vector<trackedit::TrackId> {}) {
        const auto track = editProject->track(trackId);
        const auto clips = editProject->clipList(trackId);
        if (!track || clips.size() != 1 || clips.begin()->title.empty()
            || !kDefaultName.match(track->title.toQString()).hasMatch()) {
            continue;
        }
        renamed = tracksInteraction()->changeTrackTitle(trackId, clips.begin()->title) || renamed;
    }
    if (renamed && projectHistory()) {
        projectHistory()->modifyState();
    }

    if (mixhouseAligner()) {
        std::vector<trackedit::ClipKeyList> newClipsPerFile;
        for (size_t i = 0; i < adjustedDstTrackIds.size(); ++i) {
            trackedit::ClipKeyList created;
            for (const trackedit::ClipKey& key : clipKeysOn(adjustedDstTrackIds[i])) {
                if (std::find_if(clipsBefore[i].begin(), clipsBefore[i].end(), [&](const trackedit::ClipKey& old) {
                    return old.itemId == key.itemId;
                }) == clipsBefore[i].end()) {
                    created.push_back(key);
                }
            }
            newClipsPerFile.push_back(created);
        }
        mixhouseAligner()->alignDroppedFiles(localPaths, newClipsPerFile, projectWasEmpty, m_pxPerSec);
        // GlideMix: the dropped songs alone are selected -- a clip left
        // selected from before moved along with them on the next drag.
        if (selectionController()) {
            trackedit::ClipKeyList all;
            for (const trackedit::ClipKeyList& clips : newClipsPerFile) {
                all.insert(all.end(), clips.begin(), clips.end());
            }
            if (!all.empty()) {
                selectionController()->setSelectedClips(all, true);
            }
        }
        // GlideMix: the span to show -- the dropped songs, the song that
        // starts before them and the one that starts after them.
        const auto prj = globalContext()->currentTrackeditProject();
        if (prj) {
            double newStart = 1e300, newEnd = -1e300;
            std::vector<trackedit::Clip> others;
            for (const trackedit::TrackId id : prj->trackIdList()) {
                for (const trackedit::Clip& clip : prj->clipList(id)) {
                    bool dropped = false;
                    for (const trackedit::ClipKeyList& keys : newClipsPerFile) {
                        dropped |= std::find(keys.begin(), keys.end(), clip.key) != keys.end();
                    }
                    if (dropped) {
                        if (m_focusTrack < 0) {
                            m_focusTrack = static_cast<int>(clip.key.trackId);
                        }
                        newStart = std::min(newStart, double(clip.startTime));
                        newEnd = std::max(newEnd, double(clip.endTime));
                    } else {
                        others.push_back(clip);
                    }
                }
            }
            if (newEnd > newStart) {
                m_focusStart = newStart;
                m_focusEnd = newEnd;
            }
        }
    }
}
