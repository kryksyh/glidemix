/*
 * GlideMix
 */
#include "broadcastnowplaying.h"

#include <algorithm>

#include <QRegularExpression>
#include <vector>

#include "trackedit/itrackeditproject.h"

using namespace au::projectscene;

void BroadcastNowPlaying::init()
{
    m_timer.setInterval(1000);
    QObject::connect(&m_timer, &QTimer::timeout, [this]() { update(); });
    m_timer.start();
}

void BroadcastNowPlaying::update()
{
    if (!broadcaster() || broadcaster()->state() == broadcast::IBroadcaster::State::Off) {
        return;
    }
    const auto project = globalContext() ? globalContext()->currentTrackeditProject() : nullptr;
    if (!project) {
        broadcaster()->setNowPlaying({});
        return;
    }

    // The output being streamed: Perform's playhead if it has been started,
    // else the ordinary one.
    double position = 0.0;
    if (performance() && performance()->performanceActive()) {
        position = performance()->performancePosition();
    } else if (playback() && playback()->player()) {
        position = playback()->player()->playbackPosition();
    }

    struct Covering {
        double start;
        double end;
        muse::String title;
        trackedit::ClipKey key;
    };
    std::vector<Covering> covering;
    for (const auto& track : project->trackList()) {
        if (track.mute) {
            continue;
        }
        for (const auto& clip : project->clipList(track.id)) {
            if (clip.startTime <= position && position < clip.endTime && !clip.title.empty()) {
                covering.push_back({ clip.startTime, clip.endTime, clip.title, clip.key });
            }
        }
    }
    if (covering.empty()) {
        broadcaster()->setNowPlaying({});
        return;
    }
    std::sort(covering.begin(), covering.end(), [](const Covering& a, const Covering& b) { return a.start < b.start; });
    // In a transition the song coming in counts once half the overlap has played.
    size_t current = 0;
    for (size_t i = 1; i < covering.size(); ++i) {
        const double overlapEnd = std::min(covering[current].end, covering[i].end);
        if (position >= covering[i].start + (overlapEnd - covering[i].start) / 2.0) {
            current = i;
        }
    }
    broadcaster()->setNowPlaying(streamTitle(covering[current].key, covering[current].title));
}

std::string BroadcastNowPlaying::streamTitle(const trackedit::ClipKey& clip, const muse::String& clipTitle) const
{
    // The library's tags when the track has them.
    const int trackId = beatGrids() ? beatGrids()->clipLibraryTrackId(clip) : -1;
    if (trackId >= 0 && library()) {
        mixhouse::IMixHouseLibrary::TrackQuery query;
        query.trackId = trackId;
        query.limit = 1;
        const auto found = library()->tracks(query);
        if (!found.empty() && !found.front().artist.trimmed().isEmpty() && !found.front().title.trimmed().isEmpty()) {
            const QString artist = found.front().artist.trimmed();
            const QString title = found.front().title.trimmed();
            const bool titleFirst = broadcastConfiguration() && broadcastConfiguration()->titleFirst();
            return ((titleFirst ? title : artist) + QStringLiteral(" - ") + (titleFirst ? artist : title)).toStdString();
        }
    }
    // Else the file name, without a leading track number ("083 - ", "01. ").
    static const QRegularExpression kNumber(QStringLiteral("^\\s*\\d{1,4}\\s*[-.)_]\\s*"));
    return clipTitle.toQString().remove(kNumber).trimmed().toStdString();
}
