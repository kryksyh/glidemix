/*
 * GlideMix
 */
#include "mixhouseprojectlistmodel.h"

#include "mixhouse/mixhousewaveform.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <QFileInfo>
#include <QTimer>
#include <QUrl>

#include "log.h"

using namespace au::mixhouse;

namespace {
constexpr double kLinked = 0.05;    // anchors this close are one transition point (MixHouseBeatGrids)
constexpr double kSameStart = 1e-3; // a stem starts with its song

QString rowLabel(int number, int overlay)
{
    // "1", then "1a", "1b"... for clips laid over song 1 (MixMeister's overlays).
    if (overlay <= 0) {
        return QString::number(number);
    }
    QString suffix;
    for (int n = overlay; n > 0; n = (n - 1) / 26) {
        suffix.prepend(QChar('a' + (n - 1) % 26));
    }
    return QString::number(number) + suffix;
}
}

MixHouseProjectListModel::MixHouseProjectListModel(QObject* parent)
    : QAbstractListModel(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

void MixHouseProjectListModel::componentComplete()
{
    if (transitions()) {
        transitions()->zonesChanged().onNotify(this, [this]() { scheduleReload(); });
    }
    if (beatGrids()) {
        // Anchors moved, a grid fixed, an analysis finished.
        beatGrids()->beatGridsChanged().onNotify(this, [this]() { scheduleReload(); });
    }
    if (tempoMap()) {
        tempoMap()->tempoMapChanged().onNotify(this, [this]() { scheduleReload(); });
    }
    if (projectHistory()) {
        projectHistory()->historyChanged().onReceive(this, [this](trackedit::HistoryEvent event) {
            // Undo and redo bring back a state as it was: only new edits are kept chained.
            if (event == trackedit::HistoryEvent::NewState && !m_applying) {
                m_checkChain = true;
            }
            scheduleReload();
        });
    }
    if (globalContext()) {
        globalContext()->currentTrackeditProjectChanged().onNotify(this, [this]() {
            m_pending.clear();
            m_songs.clear();
            m_checkChain = false;
            scheduleReload();
        });
        if (const auto state = globalContext()->playbackState()) {
            state->playbackPositionChanged().onReceive(this, [this](muse::secs_t) { updateProgress(); });
        }
    }
    if (transitionConfiguration()) {
        transitionConfiguration()->changed().onNotify(this, [this]() { emit transitionNamesChanged(); });
    }
    if (library()) {
        // A song added here waits for its analysis.
        library()->libraryChanged().onNotify(this, [this]() {
            m_trackCache.clear();   // BPM, key or analysis may have changed
            if (!m_pending.empty()) {
                scheduleReload();
            }
        });
    }
    reload();
}

int MixHouseProjectListModel::songCount() const
{
    return static_cast<int>(m_songs.size());
}

QStringList MixHouseProjectListModel::transitionNames() const
{
    QStringList names;
    // MixMeister's presets first (Beatmix 8 bars is the default), then the
    // transitions from Preferences > Transitions.
    for (const MixHouseTransitionPreset& preset : IMixHouseTransitions::presets()) {
        if (!names.contains(QString::fromStdString(preset.name))) {
            names << QString::fromStdString(preset.name);
        }
    }
    if (transitionConfiguration()) {
        for (const MixHouseTransitionSpec& spec : transitionConfiguration()->specs()) {
            if (!names.contains(QString::fromStdString(spec.name))) {
                names << QString::fromStdString(spec.name);
            }
        }
    }
    return names;
}

void MixHouseProjectListModel::scheduleReload()
{
    // Several changes arrive together (each clip of a reorder, a history state): rebuild once.
    if (m_reloadQueued || m_applying) {
        return;
    }
    m_reloadQueued = true;
    QTimer::singleShot(0, this, [this]() {
        m_reloadQueued = false;
        reload();
    });
}

std::vector<MixHouseProjectListModel::Song> MixHouseProjectListModel::buildSongs(std::vector<trackedit::Clip>& loose) const
{
    std::vector<Song> songs;
    loose.clear();
    const auto project = globalContext() ? globalContext()->currentTrackeditProject() : nullptr;
    if (!project || !beatGrids()) {
        return songs;
    }
    struct Anchored {
        trackedit::Clip clip;
        IMixHouseBeatGrids::Anchors anchors;
    };
    std::vector<Anchored> anchored;
    for (const trackedit::TrackId trackId : project->trackIdList()) {
        for (const trackedit::Clip& clip : project->clipList(trackId)) {
            const auto anchors = beatGrids()->clipAnchors(clip.key);
            if (anchors.valid) {
                anchored.push_back({ clip, anchors });
            } else {
                loose.push_back(clip);
            }
        }
    }
    std::sort(anchored.begin(), anchored.end(), [](const Anchored& a, const Anchored& b) {
        return a.clip.startTime < b.clip.startTime;
    });
    // The pieces of a song split at grid jumps share its row, its name and
    // (to a few ms) its anchors: one song (insertAtTransition has the same rule).
    for (const Anchored& a : anchored) {
        Song* owner = nullptr;
        for (Song& s : songs) {
            if (s.inClip.trackId == a.clip.key.trackId && std::abs(s.intro - a.anchors.intro) < 0.5
                && project->clip(s.inClip).title == a.clip.title) {
                owner = &s;
            }
        }
        if (!owner) {
            Song s;
            s.inClip = a.clip.key;
            s.outClip = a.clip.key;
            s.start = a.clip.startTime;
            s.end = a.clip.endTime;
            s.intro = a.anchors.intro;
            s.outro = a.anchors.outro;
            songs.push_back(s);
            owner = &songs.back();
        }
        owner->clips.push_back(a.clip.key);
        owner->start = std::min(owner->start, a.clip.startTime);
        owner->end = std::max(owner->end, a.clip.endTime);
        if (a.clip.startTime <= owner->intro + 1e-6 && owner->intro < a.clip.endTime) {
            owner->inClip = a.clip.key;
        }
        if (a.clip.startTime <= owner->outro + 1e-6 && owner->outro < a.clip.endTime) {
            owner->outClip = a.clip.key;
        }
    }
    // Stems and other layers that start with a song move with it.
    for (auto it = loose.begin(); it != loose.end();) {
        Song* owner = nullptr;
        for (Song& s : songs) {
            if (std::abs(s.start - it->startTime) < kSameStart) {
                owner = &s;
            }
        }
        if (owner) {
            owner->clips.push_back(it->key);
            it = loose.erase(it);
        } else {
            ++it;
        }
    }
    std::stable_sort(songs.begin(), songs.end(), [](const Song& a, const Song& b) { return a.intro < b.intro; });
    return songs;
}

void MixHouseProjectListModel::fillMetadata(Row& row, const trackedit::ClipKey& clip) const
{
    const auto project = globalContext() ? globalContext()->currentTrackeditProject() : nullptr;
    if (project) {
        row.title = project->clip(clip).title.toQString();
    }
    IMixHouseBeatGrids::ClipFile file;
    if (!beatGrids() || !beatGrids()->clipFile(clip, file)) {
        return;
    }
    row.filePath = file.path;
    auto cached = m_trackCache.find(file.path);
    if (cached == m_trackCache.end() && library() && library()->isAvailable()) {
        IMixHouseLibrary::TrackQuery query;
        query.location = file.path;
        const MixHouseTrackList found = library()->tracks(query);
        cached = m_trackCache.insert(file.path, found.empty() ? std::nullopt : std::optional<MixHouseTrack>(found.front()));
    }
    if (cached != m_trackCache.end()) {
        if (cached->has_value()) {
            const MixHouseTrack& t = **cached;
            if (!t.title.isEmpty()) {
                row.title = t.title;
            }
            row.artist = t.artist;
            row.libraryTrackId = t.id;
            row.key = t.key;
            row.year = t.year;
            row.genre = t.genre;
            row.keyId = t.keyId;
            row.energy = t.energy;
            row.durationSec = t.durationSec;
            row.rating = t.rating;
            row.comment = t.comment;
            row.dateAdded = t.dateAdded;
            row.bpm = t.bpm;
            row.analysing = t.bpm <= 0.0;
        }
    }
    if (row.bpm <= 0.0) {
        row.bpm = beatGrids()->clipBpm(clip);
    }
    // File second a plays at sequenceStart + a * ratio: a ratio above 1 is slower.
    if (file.ratio > 0.0) {
        row.stretch = (1.0 / file.ratio - 1.0) * 100.0;
        row.bpm = row.bpm > 0.0 ? row.bpm / file.ratio : 0.0;
    }
}

bool MixHouseProjectListModel::zoneBetween(const Song& a, const Song& b, IMixHouseTransitions::Zone& out) const
{
    if (!transitions()) {
        return false;
    }
    for (const IMixHouseTransitions::Zone& z : transitions()->zones()) {
        const bool fromA = std::find(a.clips.begin(), a.clips.end(), z.outgoing) != a.clips.end();
        const bool intoB = std::find(b.clips.begin(), b.clips.end(), z.incoming) != b.clips.end();
        if (fromA && intoB) {
            out = z;
            return true;
        }
    }
    return false;
}

void MixHouseProjectListModel::reload()
{
    if (m_applying) {
        return;
    }
    chainPending();

    std::vector<trackedit::Clip> loose;
    std::vector<Song> songs = buildSongs(loose);
    if (m_checkChain) {
        m_checkChain = false;
        keepChained(songs);
        if (m_applying) {
            return;
        }
        songs = buildSongs(loose);
    }
    std::sort(loose.begin(), loose.end(), [](const trackedit::Clip& a, const trackedit::Clip& b) {
        return a.startTime < b.startTime;
    });
    const auto isPending = [&](const trackedit::ClipKey& key) {
        return std::any_of(m_pending.begin(), m_pending.end(), [&](const Pending& p) {
            return std::find(p.clips.begin(), p.clips.end(), key) != p.clips.end();
        });
    };

    std::vector<Row> rows;
    size_t nextLoose = 0;
    int overlayCount = 0;
    const auto addLooseUpTo = [&](double limit, int number) {
        // Clips without anchors that start before `limit`: listed under the
        // song before them (song 0 = before the first song).
        for (; nextLoose < loose.size() && loose[nextLoose].startTime < limit; ++nextLoose) {
            Row row;
            row.overlay = true;
            row.clip = loose[nextLoose].key;
            row.start = loose[nextLoose].startTime;
            row.end = loose[nextLoose].endTime;
            row.label = rowLabel(number, ++overlayCount);
            fillMetadata(row, row.clip);
            // A library song added any other way (a double-click) that isn't
            // analysed yet waits to join the end of the mix too.
            if (!isPending(row.clip) && row.analysing && row.libraryTrackId >= 0) {
                m_pending.push_back({ { row.clip }, {}, true });
            }
            row.analysing = isPending(row.clip);
            // Not analysed yet: to the analysis now, not when someone opens
            // the Analyze view (MixMeister moves a dropped song to the front
            // of its queue). A batch already running finishes first.
            if (row.analysing && row.libraryTrackId >= 0 && !m_analysisAsked.count(row.libraryTrackId) && library()
                && !library()->isAnalyzing()) {
                m_analysisAsked.insert(row.libraryTrackId);
                library()->startBackgroundAnalysis({ row.libraryTrackId });
                LOGI() << "GlideMix Project tab: analysing " << row.title << " so it can join the mix";
            }
            rows.push_back(row);
        }
    };
    for (size_t i = 0; i < songs.size(); ++i) {
        addLooseUpTo(songs[i].start, static_cast<int>(i));
        overlayCount = 0;
        Row row;
        row.song = static_cast<int>(i);
        row.start = songs[i].start;
        row.end = songs[i].end;
        row.label = rowLabel(static_cast<int>(i) + 1, 0);
        fillMetadata(row, songs[i].inClip);
        row.analysing = false;   // it has anchors
        if (i + 1 < songs.size()) {
            IMixHouseTransitions::Zone zone;
            if (zoneBetween(songs[i], songs[i + 1], zone)) {
                row.hasTransition = true;
                row.transition = !zone.crossfaded ? QString()
                                 : zone.preset.empty() ? QStringLiteral("Custom")
                                 : QString::fromStdString(zone.preset);
            }
        }
        rows.push_back(row);
    }
    addLooseUpTo(std::numeric_limits<double>::infinity(), static_cast<int>(songs.size()));

    // Clips are stretched to the project tempo; playback then runs at line /
    // project tempo. So a row shows what is heard, at the middle of its song,
    // a half-tempo match staying at half.
    const auto project = globalContext() ? globalContext()->currentTrackeditProject() : nullptr;
    const double projectBpm = project ? project->timeSignature().tempo : 0.0;
    if (tempoMap() && !tempoMap()->isConstant() && projectBpm > 0.0) {
        for (Row& row : rows) {
            const double speed = tempoMap()->bpmAt((row.start + row.end) / 2.0) / projectBpm;
            row.bpm *= speed;
            row.stretch = ((1.0 + row.stretch / 100.0) * speed - 1.0) * 100.0;
        }
    }

    // Each song's transition out by name, as its row shows it.
    m_outNames.clear();
    for (const Row& row : rows) {
        if (!row.overlay && !row.transition.isEmpty() && row.transition != QStringLiteral("Custom")) {
            m_outNames[songs[static_cast<size_t>(row.song)].inClip] = row.transition.toStdString();
        }
    }
    beginResetModel();
    m_songs = std::move(songs);
    m_rows = std::move(rows);
    m_progress.assign(m_rows.size(), -1);
    endResetModel();
    emit songsChanged();
    updateProgress();
}

void MixHouseProjectListModel::updateProgress()
{
    if (m_rows.empty() || !globalContext() || !globalContext()->playbackState()) {
        return;
    }
    const double pos = globalContext()->playbackState()->playbackPosition().to_double();
    // Only the rows whose bar moved by a visible step: the playhead moves every frame.
    int first = -1;
    int last = -1;
    for (size_t i = 0; i < m_rows.size(); ++i) {
        const Row& r = m_rows[i];
        const double len = r.end - r.start;
        const int tenths = len > 0.0 ? static_cast<int>(std::clamp((pos - r.start) / len, 0.0, 1.0) * 1000.0) : 0;
        if (tenths != m_progress[i]) {
            m_progress[i] = tenths;
            first = first < 0 ? static_cast<int>(i) : first;
            last = static_cast<int>(i);
        }
    }
    if (first >= 0) {
        emit dataChanged(index(first), index(last), { ProgressRole, PlayingRole });
    }
}

int MixHouseProjectListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant MixHouseProjectListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) {
        return {};
    }
    const size_t i = static_cast<size_t>(index.row());
    const Row& r = m_rows[i];
    switch (role) {
    case LabelRole: return r.label;
    case TitleRole: return r.title;
    case ArtistRole: return r.artist;
    case BpmRole: return r.bpm;
    case StretchRole: return r.stretch;
    case KeyRole: return r.key;
    case TransitionRole: return r.transition;
    case HasTransitionRole: return r.hasTransition;
    case OverlayRole: return r.overlay;
    case AnalysingRole: return r.analysing;
    case ProgressRole: return m_progress[i] < 0 ? 0.0 : m_progress[i] / 1000.0;
    case PlayingRole: return m_progress[i] > 0 && m_progress[i] < 1000;
    case TrackIdRole: return r.libraryTrackId;
    case FilePathRole: return r.filePath;
    case YearRole: return r.year;
    case GenreRole: return r.genre;
    case KeyIdRole: return r.keyId;
    case EnergyRole: return r.energy;
    case DurationRole: return r.durationSec;
    case RatingRole: return r.rating;
    case CommentRole: return r.comment;
    case DateAddedRole: return r.dateAdded;
    default: return {};
    }
}

QHash<int, QByteArray> MixHouseProjectListModel::roleNames() const
{
    return {
        { LabelRole, "label" },
        { TitleRole, "title" },
        { ArtistRole, "artist" },
        { BpmRole, "bpm" },
        { StretchRole, "stretch" },
        { KeyRole, "songKey" },
        { TransitionRole, "transition" },
        { HasTransitionRole, "hasTransition" },
        { OverlayRole, "overlay" },
        { AnalysingRole, "analysing" },
        { ProgressRole, "progress" },
        { PlayingRole, "playing" },
        { TrackIdRole, "trackId" },
        { FilePathRole, "filePath" },
        { YearRole, "year" },
        { GenreRole, "genre" },
        { KeyIdRole, "keyId" },
        { EnergyRole, "energy" },
        { DurationRole, "durationSec" },
        { RatingRole, "rating" },
        { CommentRole, "comment" },
        { DateAddedRole, "dateAdded" },
    };
}

int MixHouseProjectListModel::waveformOverviewType() const
{
    return libraryConfiguration() ? static_cast<int>(libraryConfiguration()->waveformOverviewType()) : 0;
}

int MixHouseProjectListModel::rowHeight() const
{
    return libraryConfiguration() ? libraryConfiguration()->libraryRowHeight() : 28;
}

QVariantList MixHouseProjectListModel::overviewBuckets(int row, int bucketCount)
{
    QVariantList out;
    if (row < 0 || row >= rowCount() || bucketCount <= 0) {
        return out;
    }
    const Row& r = m_rows[static_cast<size_t>(row)];
    auto it = m_overviewCache.find(r.filePath);
    if (it == m_overviewCache.end()) {
        // The same order as the library table: rekordbox's own, then ours.
        MixHouseWaveformOverview overview;
        if (rekordboxAnalysis() && !r.filePath.isEmpty()) {
            overview = rekordboxAnalysis()->waveformOverview(r.filePath, r.bpm);
        }
        if (!overview.valid && r.libraryTrackId >= 0 && library()) {
            overview = decodeWaveformOverview(library()->overviewAnalysisBlob(r.libraryTrackId));
        }
        if (!overview.valid) {
            return out;   // not analysed yet: asked again on the next refresh
        }
        it = m_overviewCache.insert(r.filePath, overview);
    }
    const std::vector<MixHouseOverviewBucket> buckets = resampleOverview(*it, bucketCount);
    out.reserve(static_cast<int>(buckets.size()));
    for (const MixHouseOverviewBucket& b : buckets) {
        QVariantMap m;
        m["low"] = b.low;
        m["mid"] = b.mid;
        m["high"] = b.high;
        m["all"] = b.all;
        m["hasColor"] = b.hasColor;
        m["color"] = static_cast<uint>(b.color);
        out.push_back(m);
    }
    return out;
}

void MixHouseProjectListModel::revealRow(int row)
{
    if (row < 0 || row >= rowCount() || !trackAligner()) {
        return;
    }
    const Row& r = m_rows[static_cast<size_t>(row)];
    const trackedit::ClipKey clip = r.overlay ? r.clip : m_songs[static_cast<size_t>(r.song)].inClip;
    trackAligner()->requestViewFit(r.start, r.end, clip.trackId);
}

void MixHouseProjectListModel::showInLibrary(int row, int mode)
{
    if (row < 0 || row >= rowCount() || !library()) {
        return;
    }
    const int trackId = m_rows[static_cast<size_t>(row)].libraryTrackId;
    if (trackId <= 0) {
        return;
    }
    if (dispatcher()) {
        dispatcher()->dispatch("dock-set-open",
                               muse::actions::ActionData::make_arg2<QString, bool>(QStringLiteral("mixHouseLibraryPanel"), true));
    }
    library()->requestRevealTrack(trackId, static_cast<IMixHouseLibrary::RevealMode>(mode));
}

void MixHouseProjectListModel::seekRow(int row, double fraction)
{
    if (row < 0 || row >= rowCount() || !playback() || !playback()->player()) {
        return;
    }
    const Row& r = m_rows[static_cast<size_t>(row)];
    playback()->player()->seek(r.start + std::clamp(fraction, 0.0, 1.0) * (r.end - r.start), true);
}

void MixHouseProjectListModel::setTransition(int row, const QString& name)
{
    if (row < 0 || row >= rowCount() || !transitions()) {
        return;
    }
    const Row& r = m_rows[static_cast<size_t>(row)];
    if (r.overlay || r.song + 1 >= static_cast<int>(m_songs.size())) {
        return;
    }
    IMixHouseTransitions::Zone zone;
    if (zoneBetween(m_songs[static_cast<size_t>(r.song)], m_songs[static_cast<size_t>(r.song) + 1], zone)) {
        transitions()->applyTransition(zone.outgoing, zone.incoming, name.toStdString());
    }
}

void MixHouseProjectListModel::moveRow(int from, int to)
{
    if (from < 0 || from >= rowCount() || m_rows[static_cast<size_t>(from)].overlay) {
        return;
    }
    // Rows to chain positions: `to` is the row the song goes in front of.
    const int fromSong = m_rows[static_cast<size_t>(from)].song;
    int toSong = static_cast<int>(m_songs.size());
    for (int r = std::max(0, to); r < rowCount(); ++r) {
        if (!m_rows[static_cast<size_t>(r)].overlay) {
            toSong = m_rows[static_cast<size_t>(r)].song;
            break;
        }
    }
    if (toSong == fromSong || toSong == fromSong + 1) {
        return;   // let go where it was
    }
    std::vector<Song> order = m_songs;
    const Song moving = order[static_cast<size_t>(fromSong)];
    order.erase(order.begin() + fromSong);
    const int insertAt = toSong > fromSong ? toSong - 1 : toSong;
    order.insert(order.begin() + insertAt, moving);
    applyOrder(m_songs, std::move(order), "Reorder songs");

    // Show where the song went, not the empty lanes left in view (Chris, 2026-11-01).
    const auto project = globalContext() ? globalContext()->currentTrackeditProject() : nullptr;
    if (project && trackAligner() && !moving.clips.empty()) {
        double start = project->clip(moving.inClip).startTime;
        double end = project->clip(moving.inClip).endTime;
        for (const trackedit::ClipKey& clip : moving.clips) {
            start = std::min<double>(start, project->clip(clip).startTime);
            end = std::max<double>(end, project->clip(clip).endTime);
        }
        trackAligner()->requestViewFit(start, end, moving.inClip.trackId);
    }
}

void MixHouseProjectListModel::applyOrder(const std::vector<Song>& before, std::vector<Song> order, const std::string& name)
{
    const auto project = globalContext() ? globalContext()->currentTrackeditProject() : nullptr;
    if (!project || !transitions() || !trackeditInteraction() || order.empty()) {
        return;
    }
    using Pair = std::pair<trackedit::ClipKey, trackedit::ClipKey>;   // the two songs' IN clips
    const auto pairsOf = [](const std::vector<Song>& songs) {
        std::set<Pair> pairs;
        for (size_t i = 0; i + 1 < songs.size(); ++i) {
            pairs.insert({ songs[i].inClip, songs[i + 1].inClip });
        }
        return pairs;
    };
    const std::set<Pair> oldPairs = pairsOf(before);
    const std::set<Pair> newPairs = pairsOf(order);

    m_applying = true;
    transitions()->beginEdit();

    // 1. The transitions whose neighbours change go first, while their overlap
    //    is still there; each song's transition out (its row's drop-down) is
    //    kept by name for its new pair (MixMeister's TRSO).
    std::map<trackedit::ClipKey, std::string> outName;
    for (size_t i = 0; i + 1 < before.size(); ++i) {
        if (newPairs.count({ before[i].inClip, before[i + 1].inClip })) {
            continue;
        }
        IMixHouseTransitions::Zone zone;
        const bool hasZone = zoneBetween(before[i], before[i + 1], zone);
        if (hasZone && zone.crossfaded && !zone.preset.empty()) {
            outName[before[i].inClip] = zone.preset;
        } else if (const auto shown = m_outNames.find(before[i].inClip); shown != m_outNames.end()) {
            outName[before[i].inClip] = shown->second;   // its overlap is gone already (the next song was deleted)
        }
        if (hasZone) {
            transitions()->removeCrossfade(zone.outgoing, zone.incoming);
        }
    }

    // 2. The chain: every song's IN on the OUT of the song before it.
    std::vector<double> shift(order.size(), 0.0);
    // The mix keeps its start: the song now first starts where the first
    // song started before (a song dragged to the top left a gap of silence).
    if (!before.empty()) {
        double mixStart = before.front().start;
        for (const Song& s : before) {
            mixStart = std::min(mixStart, s.start);
        }
        shift[0] = mixStart - order[0].start;
    }
    for (size_t i = 1; i < order.size(); ++i) {
        const double prevOutro = order[i - 1].outro + shift[i - 1];
        shift[i] = prevOutro - order[i].intro;
    }
    // Nothing may start before 0:00 (the clip would be cut short there and
    // the chain broken): the whole mix moves later instead.
    double earliest = 0.0;
    for (size_t i = 0; i < order.size(); ++i) {
        earliest = std::min(earliest, order[i].start + shift[i]);
    }
    for (double& s : shift) {
        s -= earliest;
    }
    for (size_t i = 0; i < order.size(); ++i) {
        if (std::abs(shift[i]) < 1e-9) {
            continue;
        }
        trackedit::ClipKeyList clips = order[i].clips;
        // Pieces on one row: the one the move runs towards goes first.
        std::sort(clips.begin(), clips.end(), [&](const trackedit::ClipKey& a, const trackedit::ClipKey& b) {
            const double sa = project->clip(a).startTime;
            const double sb = project->clip(b).startTime;
            return shift[i] > 0.0 ? sa > sb : sa < sb;
        });
        for (const trackedit::ClipKey& clip : clips) {
            trackeditInteraction()->changeClipStartTime(clip, project->clip(clip).startTime + shift[i], false);
        }
        order[i].start += shift[i];
        order[i].end += shift[i];
        order[i].intro += shift[i];
        order[i].outro += shift[i];
    }

    // 3. The new pairs get their transition: the outgoing song's own, else the default.
    const std::string fallback = transitionConfiguration() ? transitionConfiguration()->mixNowTransition() : std::string();
    for (size_t i = 0; i + 1 < order.size(); ++i) {
        IMixHouseTransitions::Zone zone;
        const bool hasZone = zoneBetween(order[i], order[i + 1], zone);
        // A pair that stayed together keeps its transition -- unless it has
        // none: in MixMeister every pair has one.
        if (oldPairs.count({ order[i].inClip, order[i + 1].inClip }) && (!hasZone || zone.crossfaded || zone.disabled)) {
            continue;
        }
        if (!hasZone) {
            LOGW() << "GlideMix Project tab: no overlap between songs " << i + 1 << " and " << i + 2;
            continue;
        }
        const auto kept = outName.find(order[i].inClip);
        const std::string preset = kept != outName.end() ? kept->second : fallback;
        if (preset.empty() || !transitions()->applyTransition(zone.outgoing, zone.incoming, preset)) {
            transitions()->applyCrossfade(zone.outgoing, zone.incoming);
        }
    }

    orderLanes(order);

    // Read the anchors back: every OUT should now meet the next IN.
    for (size_t i = 0; i + 1 < order.size(); ++i) {
        const auto out = beatGrids()->clipAnchors(order[i].outClip);
        const auto in = beatGrids()->clipAnchors(order[i + 1].inClip);
        const double miss = (out.valid && in.valid) ? in.intro - out.outro : 0.0;
        if (std::abs(miss) >= kLinked) {
            LOGW() << "GlideMix Project tab: songs " << i + 1 << " and " << i + 2 << " miss by " << miss << " s";
        }
    }

    // Still "applying" while the history state lands, so it isn't taken for an edit made elsewhere.
    transitions()->endEdit(name);
    m_applying = false;
    LOGI() << "GlideMix Project tab: " << (name.empty() ? std::string("kept chained") : name) << ", " << order.size()
           << " songs chained";
    scheduleReload();
}

void MixHouseProjectListModel::addFiles(const QList<QUrl>& urls, int row)
{
    const auto project = globalContext() ? globalContext()->currentProject() : nullptr;
    const auto trackeditProject = globalContext() ? globalContext()->currentTrackeditProject() : nullptr;
    if (!project || !trackeditProject) {
        return;
    }
    // The song it goes after: the chain song in the row above the drop.
    trackedit::ClipKey after;
    bool append = row < 0 || row >= rowCount();
    if (!append) {
        for (int r = row - 1; r >= 0; --r) {
            if (!m_rows[static_cast<size_t>(r)].overlay) {
                after = m_songs[static_cast<size_t>(m_rows[static_cast<size_t>(r)].song)].inClip;
                break;
            }
        }
    }
    for (const QUrl& url : urls) {
        const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
        static const QStringList kAudio = { "mp3", "wav", "wave", "aif", "aiff", "flac", "m4a", "aac", "alac",
                                            "ogg", "oga", "opus", "wma", "mp4" };
        if (path.isEmpty() || !kAudio.contains(QFileInfo(path).suffix().toLower())) {
            LOGI() << "GlideMix Project tab: not an audio file, skipped: " << path;
            continue;
        }
        const std::vector<trackedit::TrackId> tracksBefore = trackeditProject->trackIdList();
        const bool wasEmpty = !trackeditProject->hasAudioContent().val;
        const muse::io::path_t file(path.toStdString());
        const muse::Ret ret = project->import(file, false, !trackAligner() || !trackAligner()->knowsTempo(file));
        if (!ret) {
            LOGE() << "GlideMix Project tab: import failed for " << path << ": " << ret.toString();
            continue;
        }
        trackedit::ClipKeyList newClips;
        for (const trackedit::TrackId id : trackeditProject->trackIdList()) {
            if (std::find(tracksBefore.begin(), tracksBefore.end(), id) == tracksBefore.end()) {
                for (const trackedit::Clip& c : trackeditProject->clipList(id)) {
                    newClips.push_back(c.key);
                }
            }
        }
        if (newClips.empty()) {
            continue;
        }
        // Stretched to the project tempo and seated on its bars, the way a drop is;
        // the chain below then puts it in its one place.
        if (trackAligner()) {
            trackAligner()->alignDroppedFiles({ file }, { newClips }, wasEmpty, 0.0);
        }
        // A song split at a grid jump is more than one clip now.
        newClips.clear();
        for (const trackedit::TrackId id : trackeditProject->trackIdList()) {
            if (std::find(tracksBefore.begin(), tracksBefore.end(), id) == tracksBefore.end()) {
                for (const trackedit::Clip& c : trackeditProject->clipList(id)) {
                    newClips.push_back(c.key);
                }
            }
        }
        m_pending.push_back({ newClips, after, append });
        // The next song dropped at once goes after this one.
        after = newClips.front();
    }
    reload();
}

void MixHouseProjectListModel::chainPending()
{
    if (m_pending.empty() || !beatGrids()) {
        return;
    }
    const auto project = globalContext() ? globalContext()->currentTrackeditProject() : nullptr;
    if (!project) {
        m_pending.clear();
        return;
    }
    // Clips gone (undo, deleted) drop out of the queue.
    for (Pending& p : m_pending) {
        p.clips.erase(std::remove_if(p.clips.begin(), p.clips.end(), [&](const trackedit::ClipKey& k) {
            return !project->clip(k).key.isValid();
        }), p.clips.end());
    }
    m_pending.erase(std::remove_if(m_pending.begin(), m_pending.end(), [](const Pending& p) {
        return p.clips.empty();
    }), m_pending.end());

    // Oldest first, and only while each is ready: a later song waits for the one before it.
    while (!m_pending.empty()) {
        const Pending p = m_pending.front();
        const bool ready = std::any_of(p.clips.begin(), p.clips.end(), [&](const trackedit::ClipKey& k) {
            return beatGrids()->clipAnchors(k).valid;
        });
        if (!ready) {
            return;
        }
        m_pending.erase(m_pending.begin());

        std::vector<trackedit::Clip> loose;
        const std::vector<Song> before = buildSongs(loose);
        const auto isNew = [&](const Song& s) {
            return std::find(p.clips.begin(), p.clips.end(), s.inClip) != p.clips.end();
        };
        const auto added = std::find_if(before.begin(), before.end(), isNew);
        if (added == before.end()) {
            continue;
        }
        std::vector<Song> chain;
        for (const Song& s : before) {
            if (!isNew(s)) {
                chain.push_back(s);
            }
        }
        // `before` as it was without the new song, so its pairs are the old ones.
        const std::vector<Song> old = chain;
        auto at = chain.end();
        if (!p.append) {
            at = chain.begin();
            for (auto it = chain.begin(); it != chain.end(); ++it) {
                if (it->inClip == p.after) {
                    at = it + 1;
                }
            }
        }
        chain.insert(at, *added);
        applyOrder(old, std::move(chain), "Add song to the mix");

        // Show the new transition, not the end of a 35-song mix (decision 12).
        if (trackAligner()) {
            const trackedit::Clip placed = project->clip(added->inClip);
            const auto anchors = beatGrids()->clipAnchors(added->inClip);
            const double focus = anchors.valid ? anchors.intro : placed.startTime;
            trackAligner()->requestViewFocus(std::max(placed.startTime, focus - 30.0), focus + 30.0, placed.key.trackId);
        }
    }
}

void MixHouseProjectListModel::orderLanes(const std::vector<Song>& order)
{
    const auto project = globalContext() ? globalContext()->currentTrackeditProject() : nullptr;
    if (!project || !tracksInteraction()) {
        return;
    }
    // Each song's lane, then the lanes of its stems, in mix order.
    trackedit::TrackIdList wanted;
    for (const Song& song : order) {
        trackedit::ClipKeyList clips = song.clips;
        std::stable_partition(clips.begin(), clips.end(), [&](const trackedit::ClipKey& k) { return k == song.inClip; });
        for (const trackedit::ClipKey& clip : clips) {
            if (std::find(wanted.begin(), wanted.end(), clip.trackId) == wanted.end()) {
                wanted.push_back(clip.trackId);
            }
        }
    }
    for (size_t i = 0; i < wanted.size(); ++i) {
        const std::vector<trackedit::TrackId> now = project->trackIdList();
        const auto at = std::find(now.begin(), now.end(), wanted[i]);
        if (at != now.end() && static_cast<size_t>(at - now.begin()) != i) {
            tracksInteraction()->moveTracksTo({ wanted[i] }, static_cast<int>(i));
        }
    }
}

void MixHouseProjectListModel::keepChained(std::vector<Song>& songs)
{
    if (m_songs.empty() || songs.size() < 2) {
        return;
    }
    const auto isIn = [](const std::vector<Song>& list, const trackedit::ClipKey& key) {
        return std::any_of(list.begin(), list.end(), [&](const Song& s) { return s.inClip == key; });
    };
    std::vector<Song> kept;
    std::vector<Song> added;
    for (const Song& s : songs) {
        (isIn(m_songs, s.inClip) ? kept : added).push_back(s);
    }
    const auto linked = [](const Song& a, const Song& b) { return std::abs(a.outro - b.intro) < kLinked; };

    // A song was deleted: its neighbours close up (MixMeister). Folded into
    // the delete's own undo step.
    if (added.empty() && kept.size() < m_songs.size()) {
        bool gap = false;
        for (size_t i = 0; i + 1 < kept.size(); ++i) {
            gap = gap || !linked(kept[i], kept[i + 1]);
        }
        if (gap) {
            LOGI() << "GlideMix Project tab: a song was removed, closing the gap";
            applyOrder(m_songs, kept, "");
        }
        return;
    }
    // One song put after the end of the mix (a double-click in the library,
    // a drop past the last song): it joins the chain, whatever the distance.
    // Linked by the library's own placement or not, it is chained and its
    // new pair gets a transition.
    if (added.size() == 1 && kept.size() == m_songs.size() && songs.back().inClip == added.front().inClip) {
        std::vector<Song> order = kept;
        order.push_back(added.front());
        LOGI() << "GlideMix Project tab: a song added after the end of the mix joins it";
        applyOrder(kept, order, "");
    }
}
