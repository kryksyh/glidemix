/*
 * GlideMix
 */
#include "mixhousetempomap.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

#include "au3-mixer/Envelope.h"
#include "au3-mixer/MixerOptions.h"
#include "au3-mixer/PlaybackSpeedCurve.h"

#include "trackedit/itrackeditproject.h"

#include "log.h"

using namespace au::mixhouse;

namespace {
constexpr double kFallbackBpm = 120.0;
constexpr double kSameBpm = 1e-6;
}

double IMixHouseTempoMap::curveExponent(double curve)
{
    return std::pow(2.0, -2.0 * std::clamp(curve, -1.0, 1.0));
}

double IMixHouseTempoMap::curveShape(double t, double curve)
{
    if (std::abs(curve) < 1e-6) {
        return t;   // straight line: the common case, no pow()
    }
    return std::pow(std::clamp(t, 0.0, 1.0), curveExponent(curve));
}

MixHouseTempoMap::MixHouseTempoMap(const muse::modularity::ContextPtr& iocCtx)
    : muse::Contextable(iocCtx)
{
    m_points = { Point { 0.0, kFallbackBpm, 0.0 } };
    m_warpToken = std::make_unique<BoundedEnvelope>(false, 0.01, 100.0, 1.0);
    m_warpToken->SetTrackLen(DBL_MAX);
}

MixHouseTempoMap::~MixHouseTempoMap()
{
    m_warpHook.reset();
    PlaybackSpeedCurve::SetLive(nullptr, nullptr);
}

void MixHouseTempoMap::init()
{
    // Playback asks MixerOptions::Warp::DefaultWarp for its time warp when a
    // stream starts (ProjectAudioIO::GetDefaultOptions); AU4 has no time track,
    // so the tempo line takes that hook.
    m_warpHook = std::make_shared<MixerOptions::Warp::DefaultWarp::Scope>(
        [this](const AudacityProject*) { return playbackWarp(); });

    globalContext()->currentTrackeditProjectChanged().onNotify(this, [this]() {
        onProjectChanged();
    });
    onProjectChanged();
}

void MixHouseTempoMap::onProjectChanged()
{
    // A new project starts with a flat line at its tempo. Nothing is saved yet
    // (persistence comes with project saving).
    m_points = { Point { 0.0, projectTempo(), 0.0 } };
    if (auto project = globalContext()->currentTrackeditProject()) {
        project->timeSignatureChanged().onReceive(this, [this](const trackedit::TimeSignature& ts) {
            followProjectTempo(ts.tempo);
        });
    }
    rebuildPlaybackWarp();
    m_changed.notify();
}

double MixHouseTempoMap::projectTempo() const
{
    const auto project = globalContext()->currentTrackeditProject();
    const double tempo = project ? project->timeSignature().tempo : 0.0;
    return tempo > 0.0 ? std::clamp(tempo, kMinBpm, kMaxBpm) : kFallbackBpm;
}

void MixHouseTempoMap::followProjectTempo(double tempo)
{
    // The project tempo is the flat line; a drawn ramp is left alone (the
    // parked app's m_tempoEdited rule).
    if (tempo <= 0.0) {
        return;
    }
    if (!isConstant()) {
        rebuildPlaybackWarp();   // speed is relative to the project tempo
        return;
    }
    tempo = std::clamp(tempo, kMinBpm, kMaxBpm);
    if (std::abs(m_points.front().bpm - tempo) < kSameBpm) {
        return;
    }
    for (Point& p : m_points) {
        p.bpm = tempo;
    }
    rebuildPlaybackWarp();
    m_changed.notify();
}

IMixHouseTempoMap::Points MixHouseTempoMap::sanitized(Points points)
{
    for (Point& p : points) {
        p.time = std::isfinite(p.time) ? std::max(0.0, p.time) : 0.0;
        p.bpm = std::isfinite(p.bpm) ? std::clamp(p.bpm, kMinBpm, kMaxBpm) : kFallbackBpm;
        p.curve = std::isfinite(p.curve) ? std::clamp(p.curve, -1.0, 1.0) : 0.0;
    }
    std::stable_sort(points.begin(), points.end(), [](const Point& a, const Point& b) { return a.time < b.time; });
    if (points.empty()) {
        points.push_back(Point { 0.0, kFallbackBpm, 0.0 });
    }
    return points;
}

IMixHouseTempoMap::Points MixHouseTempoMap::points() const
{
    return m_points;
}

void MixHouseTempoMap::setPoints(const Points& points, bool completed)
{
    if (completed && m_followTracks) {
        setFollowsTracks(false);   // drawn by hand: the tracks no longer drive it
    }
    m_points = sanitized(points);
    rebuildPlaybackWarp();   // heard while dragging, too
    m_changed.notify();

    if (!completed || !isConstant()) {
        return;
    }
    // A flat line is the project tempo: clips stretched to the project tempo
    // follow it (au3's WaveClip::OnProjectTempoChange).
    auto project = globalContext()->currentTrackeditProject();
    if (!project) {
        return;
    }
    trackedit::TimeSignature ts = project->timeSignature();
    const double bpm = m_points.front().bpm;
    if (std::abs(ts.tempo - bpm) >= kSameBpm) {
        ts.tempo = bpm;
        project->setTimeSignature(ts);
    }
}

void MixHouseTempoMap::reset()
{
    setFollowsTracks(false);
    m_masterFactor = 1.0;
    setPoints({ Point { 0.0, projectTempo(), 0.0 } }, true);
}

double MixHouseTempoMap::bpmAt(double time) const
{
    if (time <= m_points.front().time) {
        return m_points.front().bpm;
    }
    for (size_t i = 0; i + 1 < m_points.size(); ++i) {
        const Point& a = m_points[i];
        const Point& b = m_points[i + 1];
        if (time <= b.time) {
            const double dur = b.time - a.time;
            if (dur <= 0.0) {
                return b.bpm;
            }
            return a.bpm + (b.bpm - a.bpm) * curveShape((time - a.time) / dur, a.curve);
        }
    }
    return m_points.back().bpm;
}

bool MixHouseTempoMap::isConstant() const
{
    return std::all_of(m_points.begin(), m_points.end(), [this](const Point& p) {
        return std::abs(p.bpm - m_points.front().bpm) < kSameBpm;
    });
}

std::vector<MixHouseTempoMap::TablePoint> MixHouseTempoMap::table() const
{
    // The parked buildTempoTable(): cumulative beats at every point. Before the
    // first point the first BPM holds, so the table starts at time 0.
    std::vector<TablePoint> t;
    const Point& first = m_points.front();
    t.push_back({ 0.0, 0.0, first.bpm, 0.0 });
    double cum = 0.0;
    double prevSec = 0.0;
    double prevBpm = first.bpm;
    double prevCurve = 0.0;
    for (const Point& p : m_points) {
        if (p.time > prevSec) {
            // bpm(t) = bpm0 + (bpm1 - bpm0) * t^k over the segment integrates to
            // (dur / 60) * [bpm0 + (bpm1 - bpm0) / (k + 1)].
            const double k = curveExponent(prevCurve);
            cum += (p.time - prevSec) / 60.0 * (prevBpm + (p.bpm - prevBpm) / (k + 1.0));
            t.push_back({ p.time, cum, p.bpm, p.curve });
        } else {
            t.back().bpm = p.bpm;   // a point at the same time replaces the value
            t.back().curve = p.curve;
        }
        prevSec = p.time;
        prevBpm = p.bpm;
        prevCurve = p.curve;
    }
    return t;
}

double MixHouseTempoMap::secToBeat(double sec) const
{
    const std::vector<TablePoint> t = table();
    if (sec <= 0.0) {
        return sec * t.front().bpm / 60.0;
    }
    for (size_t j = 0; j + 1 < t.size(); ++j) {
        if (sec <= t[j + 1].sec) {
            const double dur = t[j + 1].sec - t[j].sec;
            if (dur <= 0.0) {
                return t[j].beats;
            }
            const double k = curveExponent(t[j].curve);
            const double u = std::clamp((sec - t[j].sec) / dur, 0.0, 1.0);
            return t[j].beats + dur / 60.0 * (t[j].bpm * u + (t[j + 1].bpm - t[j].bpm) * std::pow(u, k + 1.0) / (k + 1.0));
        }
    }
    return t.back().beats + (sec - t.back().sec) * t.back().bpm / 60.0;
}

double MixHouseTempoMap::beatToSec(double beat) const
{
    const std::vector<TablePoint> t = table();
    if (beat <= 0.0) {
        return beat * 60.0 / t.front().bpm;
    }
    for (size_t j = 0; j + 1 < t.size(); ++j) {
        if (beat <= t[j + 1].beats) {
            const double dur = t[j + 1].sec - t[j].sec;
            const double bpm0 = t[j].bpm;
            const double bpm1 = t[j + 1].bpm;
            const double target = beat - t[j].beats;
            if (dur <= 0.0) {
                return t[j].sec;
            }
            if (std::abs(bpm1 - bpm0) < kSameBpm) {
                return t[j].sec + target * 60.0 / bpm0;
            }
            // Beats are strictly increasing inside a segment (BPM >= 50), so
            // bisection on the forward integral finds the one root.
            const double k = curveExponent(t[j].curve);
            auto beatsAt = [&](double u) {
                return dur / 60.0 * (bpm0 * u + (bpm1 - bpm0) * std::pow(u, k + 1.0) / (k + 1.0));
            };
            double lo = 0.0, hi = 1.0;
            for (int it = 0; it < 48; ++it) {
                const double mid = 0.5 * (lo + hi);
                (beatsAt(mid) < target ? lo : hi) = mid;
            }
            return t[j].sec + 0.5 * (lo + hi) * dur;
        }
    }
    return t.back().sec + (beat - t.back().beats) * 60.0 / t.back().bpm;
}

void MixHouseTempoMap::rebuildPlaybackWarp()
{
    const auto project = globalContext()->currentTrackeditProject();
    const double reference = project ? project->timeSignature().tempo : 0.0;
    PlaybackSpeedCurve::SetProjectTempo(reference);
    if (reference <= 0.0) {
        PlaybackSpeedCurve::SetLive(nullptr, nullptr);
        return;
    }

    // The timeline stays in bars at the project tempo; playback runs through
    // it at speed BPM / project tempo. Curved segments are sampled.
    auto curve = std::make_shared<PlaybackSpeedCurve>();
    // A flat line is the project tempo: exactly speed 1, even when the
    // project tempo lies outside the lane's 50..300 range.
    const bool flat = isConstant();
    auto add = [&](double time, double bpm) {
        const double speed = flat ? 1.0 : bpm / reference;
        if (!curve->knots.empty() && curve->knots.back().first >= time) {
            curve->knots.back().second = speed;
        } else {
            curve->knots.emplace_back(time, speed);
        }
    };
    for (size_t i = 0; i < m_points.size(); ++i) {
        const Point& a = m_points[i];
        add(a.time, a.bpm);
        if (i + 1 < m_points.size() && std::abs(a.curve) >= 1e-6) {
            const Point& b = m_points[i + 1];
            constexpr int steps = 64;
            for (int k = 1; k < steps; ++k) {
                const double t = double(k) / steps;
                add(a.time + (b.time - a.time) * t, a.bpm + (b.bpm - a.bpm) * curveShape(t, a.curve));
            }
        }
    }

    // The resamplers' speed range is fixed when a stream starts.
    m_warpToken->SetRangeLower(kMinBpm / reference);
    m_warpToken->SetRangeUpper(kMaxBpm / reference);
    PlaybackSpeedCurve::SetLive(m_warpToken.get(), std::move(curve));
}

const BoundedEnvelope* MixHouseTempoMap::playbackWarp()
{
    // A stream gets the warp when the line isn't flat, or when the lane is
    // shown -- so a ramp drawn while playing is heard without a restart. A
    // flat line with the lane hidden plays with no resampling at all.
    const bool laneShown = configuration() && configuration()->tempoLaneVisible();
    if (!laneShown && isConstant()) {
        return nullptr;
    }
    return PlaybackSpeedCurve::LiveFor(m_warpToken.get()) ? m_warpToken.get() : nullptr;
}

muse::async::Notification MixHouseTempoMap::tempoMapChanged() const
{
    return m_changed;
}

void MixHouseTempoMap::setFollowsTracks(bool follow)
{
    if (m_followTracks == follow) {
        return;
    }
    m_followTracks = follow;
    if (follow) {
        m_masterFactor = 1.0;
        // Clip moves and drops don't all reach a single notification here;
        // a light check a few times a second catches every change.
        m_followTimer.setInterval(200);
        QObject::connect(&m_followTimer, &QTimer::timeout, [this]() { followTracksNow(false); });
        m_followTimer.start();
        followTracksNow(true);
    } else {
        m_followTimer.stop();
        QObject::disconnect(&m_followTimer, nullptr, nullptr, nullptr);
    }
    m_changed.notify();
}

void MixHouseTempoMap::trackPlaced(double stretchRatio)
{
    if (m_followTracks) {
        followTracksNow(true);
        return;
    }
    if (stretchRatio <= 0.0 || std::abs(stretchRatio - 1.0) <= kFarTempo || !isConstant()) {
        return;   // near the project tempo, or a line drawn by hand: leave it
    }
    LOGI() << "Tempo follows tracks: a track " << (stretchRatio - 1.0) * 100.0 << "% off the project tempo";
    setFollowsTracks(true);
}

void MixHouseTempoMap::followTracksNow(bool force)
{
    const auto trackeditProject = globalContext() ? globalContext()->currentTrackeditProject() : nullptr;
    if (!trackeditProject || !beatGrids()) {
        return;
    }
    struct Span {
        double start;
        double end;
        double bpm;
    };
    std::vector<Span> spans;
    QString signature;
    // Tracks near the project tempo play at it, as SYNC stretched them: the
    // line only moves where a far track takes over (points where the tempo
    // really changes, not one per 0.1 BPM of detection noise).
    const double project = projectTempo();
    const auto lineBpm = [project](double bpm) {
        return std::abs(bpm / project - 1.0) <= kFarTempo ? project : bpm;
    };
    for (const trackedit::TrackId trackId : trackeditProject->trackIdList()) {
        for (const trackedit::Clip& clip : trackeditProject->clipList(trackId)) {
            const double bpm = lineBpm(beatGrids()->clipBpm(clip.key));
            signature += QStringLiteral("%1:%2:%3;").arg(clip.startTime, 0, 'f', 4).arg(clip.endTime, 0, 'f', 4).arg(bpm, 0, 'f', 3);
            if (bpm > 0.0 && clip.endTime > clip.startTime) {
                spans.push_back({ clip.startTime, clip.endTime, bpm });
            }
        }
    }
    if (!force && signature == m_trackSignature) {
        return;
    }
    m_trackSignature = signature;
    if (spans.empty()) {
        return;
    }
    std::sort(spans.begin(), spans.end(), [](const Span& a, const Span& b) { return a.start < b.start; });

    Points points;
    const auto add = [&points](double time, double bpm) {
        if (!points.empty() && time <= points.back().time + 1e-6) {
            points.back().bpm = bpm;   // same moment: the later value wins
            return;
        }
        points.push_back(Point { time, bpm, 0.0 });
    };
    Span current = spans.front();
    add(0.0, current.bpm);
    for (size_t i = 1; i < spans.size(); ++i) {
        const Span& next = spans[i];
        if (next.end <= current.end) {
            continue;   // a short clip inside the playing track: the track leads
        }
        if (std::abs(next.bpm - current.bpm) < kSameBpm) {
            current = next;   // same tempo: no point
            continue;
        }
        if (next.start < current.end) {
            // Beatmix: over the overlap the tempo glides from the outgoing
            // track's BPM to the incoming one's.
            add(next.start, current.bpm);
            add(current.end, next.bpm);
        } else {
            // A gap: hold, then the next track starts at its own tempo.
            add(next.start, current.bpm);
            add(next.start + 0.001, next.bpm);
        }
        current = next;
    }
    for (Point& p : points) {
        p.bpm = std::clamp(p.bpm * m_masterFactor, kMinBpm, kMaxBpm);
    }
    m_points = sanitized(points);
    rebuildPlaybackWarp();
    m_changed.notify();
    LOGI() << "Tempo follows tracks: " << spans.size() << " clips, " << m_points.size() << " points";
}

void MixHouseTempoMap::nudgeMasterBpm(double deltaBpm, double atTime)
{
    const double at = std::max(1.0, bpmAt(atTime));
    const double factor = std::clamp(at + deltaBpm, kMinBpm, kMaxBpm) / at;
    if (std::abs(factor - 1.0) < 1e-9) {
        return;
    }
    m_masterFactor *= factor;
    Points points = m_points;
    for (Point& p : points) {
        p.bpm = std::clamp(p.bpm * factor, kMinBpm, kMaxBpm);
    }
    // Not "completed": the project tempo (and so every clip's stretch) stays;
    // only the playback speed moves, like MixMeister's Master BPM.
    setPoints(points, false);
}
