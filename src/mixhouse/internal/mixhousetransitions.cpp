/*
 * GlideMix
 */
#include "mixhousetransitions.h"

#include <algorithm>
#include <cmath>

#include "trackedit/itrackeditproject.h"
#include "log.h"

using namespace au::mixhouse;

namespace {
// An overlap shorter than this isn't a transition (a clip nudged a hair onto
// its neighbour).
constexpr double kMinOverlapSec = 0.25;
// Sampled equal-power curve; the clip envelope interpolates between points.
// The clip envelope's floor (WaveClip: Envelope(true, 1e-7, 4.0, 1.0)).
constexpr double kSilent = 1e-7;
}

MixHouseTransitions::MixHouseTransitions(const muse::modularity::ContextPtr& iocCtx)
    : muse::Contextable(iocCtx)
{
}

void MixHouseTransitions::init()
{
    // Every edit that can create, move or remove an overlap ends in a history
    // state (adds, drops, drags, undo/redo).
    projectHistory()->historyChanged().onReceive(this, [this](auto) {
        m_zonesChanged.notify();
    });
    globalContext()->currentTrackeditProjectChanged().onNotify(this, [this]() {
        m_zonesChanged.notify();
    });
}

muse::async::Notification MixHouseTransitions::zonesChanged() const
{
    return m_zonesChanged;
}

std::vector<IMixHouseTransitions::Zone> MixHouseTransitions::zones() const
{
    std::vector<Zone> result;
    const auto project = globalContext() ? globalContext()->currentTrackeditProject() : nullptr;
    if (!project) {
        return result;
    }

    std::vector<trackedit::Clip> clips;
    for (const trackedit::TrackId trackId : project->trackIdList()) {
        for (const trackedit::Clip& clip : project->clipList(trackId)) {
            clips.push_back(clip);
        }
    }

    for (const trackedit::Clip& a : clips) {
        for (const trackedit::Clip& b : clips) {
            if (a.key.trackId == b.key.trackId) {
                continue;   // same track: clips there can't overlap
            }
            // a is outgoing when it starts first (ties broken by clip id so a
            // pair is listed once).
            if (b.startTime < a.startTime || (b.startTime == a.startTime && b.key.itemId <= a.key.itemId)) {
                continue;
            }
            const double start = b.startTime;
            const double end = std::min(a.endTime, b.endTime);
            if (end - start < kMinOverlapSec) {
                continue;
            }
            // Clips laid over each other (a song and its stems, a copy on
            // another row) are layers, not a transition: a transition's
            // overlap never covers most of either song.
            const double shorter = std::min(a.endTime - a.startTime, b.endTime - b.startTime);
            if (shorter > 0.0 && (end - start) / shorter > 0.9) {
                continue;
            }
            Zone zone;
            zone.outgoing = a.key;
            zone.incoming = b.key;
            zone.start = start;
            zone.end = end;
            // Read from the volume lines themselves, so it survives undo,
            // edits by hand and reopening the project.
            zone.crossfaded = gainAt(a.key, end) < 0.01 && gainAt(b.key, start) < 0.01;
            const auto applied = m_applied.find({ a.key, b.key });
            if (zone.crossfaded && applied != m_applied.end()) {
                zone.preset = applied->second;
            }
            zone.disabled = m_disabled.count({ a.key, b.key }) > 0;
            result.push_back(zone);
        }
    }
    return result;
}

double MixHouseTransitions::gainAt(const trackedit::ClipKey& clip, double time) const
{
    const automation::AutomationSegments segments = clipGain() ? clipGain()->clipGainSegments(clip)
                                                               : automation::AutomationSegments {};
    for (const auto& s : segments) {
        if (time <= s.end.xValue) {
            const double dur = s.end.xValue - s.start.xValue;
            const double u = dur > 0.0 ? std::clamp((time - s.start.xValue) / dur, 0.0, 1.0) : 1.0;
            return s.start.yValue + (s.end.yValue - s.start.yValue) * u;
        }
    }
    return segments.empty() ? 1.0 : segments.back().end.yValue;
}

bool MixHouseTransitions::findZone(const trackedit::ClipKey& outgoing, const trackedit::ClipKey& incoming,
                                   Zone& zone) const
{
    for (const Zone& z : zones()) {
        if (z.outgoing == outgoing && z.incoming == incoming) {
            zone = z;
            return true;
        }
    }
    return false;
}

bool MixHouseTransitions::replaceGain(const trackedit::ClipKey& clip, double from, double to,
                                      const std::vector<GainPoint>& points, bool completed)
{
    const automation::AutomationSegments current = clipGain()->clipGainSegments(clip);
    if (current.empty()) {
        return false;
    }
    const double clipStart = current.front().start.xValue;
    const double clipEnd = current.back().end.xValue;

    // The line as points (each carrying the curvature of the segment after it).
    std::vector<GainPoint> line;
    for (const auto& s : current) {
        line.push_back({ s.start.xValue, s.start.yValue, s.curvature, s.eased });
    }
    line.push_back({ current.back().end.xValue, current.back().end.yValue, 0.0f });

    std::vector<GainPoint> merged;
    for (const GainPoint& p : line) {
        if (p.time < from) {
            merged.push_back(p);
        }
    }
    for (GainPoint p : points) {
        p.time = std::clamp(p.time, clipStart, clipEnd);
        merged.push_back(p);
    }
    for (const GainPoint& p : line) {
        if (p.time > to) {
            merged.push_back(p);
        }
    }
    std::stable_sort(merged.begin(), merged.end(), [](const GainPoint& x, const GainPoint& y) {
        return x.time < y.time;
    });
    // The line must span the whole clip.
    if (merged.empty() || merged.front().time > clipStart) {
        merged.insert(merged.begin(), { clipStart, merged.empty() ? 1.0 : merged.front().value, 0.0f });
    }
    if (merged.back().time < clipEnd) {
        merged.push_back({ clipEnd, merged.back().value, 0.0f });
    }
    // GlideMix: a point on a flat stretch, at the level of both neighbours,
    // changes nothing but shows as a stray dot (a transition switched off left
    // one where its fade began). Drop it.
    for (size_t i = 1; i + 1 < merged.size();) {
        const GainPoint& prev = merged[i - 1];
        const GainPoint& p = merged[i];
        if (prev.value == p.value && p.value == merged[i + 1].value && prev.curvature == 0.0f
            && p.curvature == 0.0f) {
            merged.erase(merged.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            ++i;
        }
    }

    automation::AutomationSegments segments;
    for (size_t i = 0; i + 1 < merged.size(); ++i) {
        segments.push_back({ { merged[i].time, merged[i].value }, { merged[i + 1].time, merged[i + 1].value },
                             merged[i].curvature, merged[i].eased });
    }
    if (segments.empty()) {
        return false;
    }
    return clipGain()->setClipGainSegments(clip, segments, completed);
}

const std::vector<MixHouseTransitionPreset>& IMixHouseTransitions::presets()
{
    // MixMeister Fusion's own transitions.xml, as the parked app transcribed
    // it (src/mixxxmeister/transitions.cpp). Beatmix 8 bars is Fusion's default.
    // Mixtape's own bass-swap transitions follow them (see builtInTransitionSpecs());
    // they are appended after these, so the indices of Fusion's presets stay put.
    static const std::vector<MixHouseTransitionPreset> kPresets = [] {
        std::vector<MixHouseTransitionPreset> list = {
        { "Crossfade (fit overlap)", false, {}, {} },
        { "Simple Cut", false, { { 0, 0 }, { 0, -50 } }, { { 0, -50 }, { 0, 0 } } },
        { "Simple Crossfade", false, { { 0, 0 }, { 4, -6.5 }, { 8, -50 } }, { { -8, -50 }, { -4, -6.5 }, { 0, 0 } } },
        { "Beatmix Cut", true, { { 0, 0 }, { 0, -50 } }, { { 0, -50 }, { 0, 0 } } },
        { "Beatmix 8 bars", true, { { 0, 0 }, { 32, -6 }, { 32.25, -50 } }, { { -0.25, -50 }, { 0, -6 }, { 32, 0 } } },
        { "Beatmix 16 bars", true, { { 0, 0 }, { 64, -6 }, { 64.25, -50 } }, { { -0.25, -50 }, { 0, -6 }, { 64, 0 } } },
        { "Beatmix 32 bars", true, { { 0, 0 }, { 128, -6 }, { 128.25, -50 } }, { { -0.25, -50 }, { 0, -6 }, { 128, 0 } } },
        { "Beatmix 64 bars", true, { { 0, 0 }, { 256, -6 }, { 256.25, -50 } }, { { -0.25, -50 }, { 0, -6 }, { 256, 0 } } },
        };
        for (const MixHouseTransitionSpec& spec : builtInTransitionSpecs()) {
            list.push_back(presetFromSpec(spec));
        }
        return list;
    }();
    return kPresets;
}

void MixHouseTransitions::clearTransitionEffects(const trackedit::ClipKey& clip)
{
    if (!clipGain() || !mixxxEffectIds()) {
        return;
    }
    // Not switched off: a line switched off leaves the effect at its own
    // setting over the whole song (an echo's send is up by default), so the
    // line is held where the effect does nothing. Tracks without the effect
    // are left alone.
    const auto project = globalContext()->currentTrackeditProject();
    if (!project) {
        return;
    }
    const trackedit::Clip c = project->clip(clip);
    for (const MixHouseTransitionEffectParameter& p : transitionEffectParameters()) {
        const std::string effectId = mixxxEffectIds()->effectIdForMixxxId(p.mixxxId).toStdString();
        if (!effectId.empty()) {
            clipGain()->setClipEffectPoints(clip, effectId, p.parameterId,
                                            { { c.startTime, p.neutral }, { c.endTime, p.neutral } }, false);
        }
    }
}

bool MixHouseTransitions::applyPreset(const trackedit::ClipKey& outgoing, const trackedit::ClipKey& incoming, int index)
{
    const auto& all = presets();
    if (index < 0 || index >= static_cast<int>(all.size())) {
        return false;
    }
    return applyPresetValue(outgoing, incoming, all[static_cast<size_t>(index)]);
}

bool MixHouseTransitions::applyTransition(const trackedit::ClipKey& outgoing, const trackedit::ClipKey& incoming,
                                          const std::string& name)
{
    // The same conversion Mix Now uses, so a transition applied from the zone's
    // menu is exactly the one the Preferences preview draws.
    const auto spec = transitionConfiguration() ? transitionConfiguration()->spec(name) : std::nullopt;
    if (!spec) {
        return false;
    }
    return applyPresetValue(outgoing, incoming, presetFromSpec(*spec));
}

bool MixHouseTransitions::applyPresetValue(const trackedit::ClipKey& outgoing, const trackedit::ClipKey& incoming,
                                           const MixHouseTransitionPreset& preset)
{
    if (preset.outro.empty() && preset.intro.empty() && preset.bassOutro.empty() && preset.bassIntro.empty()
        && preset.midOutro.empty() && preset.midIntro.empty() && preset.highOutro.empty() && preset.highIntro.empty()
        && preset.effects.empty()) {
        const bool ok = applyCrossfade(outgoing, incoming);
        if (ok) {
            m_applied[{ outgoing, incoming }] = preset.name;
            m_zonesChanged.notify();
        }
        return ok;
    }
    Zone zone;
    const auto project = globalContext()->currentTrackeditProject();
    if (!clipGain() || !project || !findZone(outgoing, incoming, zone)) {
        return false;
    }
    const trackedit::TimeSignature ts = project->timeSignature();
    const double beat = ts.tempo > 0.0 ? 60.0 / ts.tempo * (4.0 / std::max(1, ts.lower)) : 0.5;

    double anchor = zone.start;
    if (preset.beatmix) {
        const double bar = beat * std::max(1, ts.upper);
        anchor = std::ceil(zone.start / bar - 1e-6) * bar;
        if (anchor > zone.end) {
            anchor = zone.start;
        }
    }
    return writePreset(outgoing, incoming, preset, anchor);
}

bool MixHouseTransitions::writePreset(const trackedit::ClipKey& outgoing, const trackedit::ClipKey& incoming,
                                      const MixHouseTransitionPreset& preset, double anchor)
{
    const auto project = globalContext()->currentTrackeditProject();
    if (!clipGain() || !project) {
        return false;
    }
    const trackedit::Clip outClip = project->clip(outgoing);
    const trackedit::Clip inClip = project->clip(incoming);
    const trackedit::TimeSignature ts = project->timeSignature();
    const double beat = ts.tempo > 0.0 ? 60.0 / ts.tempo * (4.0 / std::max(1, ts.lower)) : 0.5;
    const double unit = preset.beatmix ? beat : 1.0;

    const auto gain = [](double db) { return db <= -49.0 ? kSilent : std::pow(10.0, db / 20.0); };
    const auto clipped = [](const std::vector<GainPoint>& points, const trackedit::Clip& clip) {
        std::vector<GainPoint> result;
        for (const GainPoint& p : points) {
            if (p.time >= clip.startTime - 1e-9 && p.time <= clip.endTime + 1e-9) {
                result.push_back(p);
            }
        }
        return result;
    };

    // Outgoing volume: full up to the anchor, the markers, then held to its end.
    // A preset with no outgoing markers leaves that clip's volume line alone.
    std::vector<GainPoint> fadeOut;
    double outFrom = outClip.endTime;
    if (!preset.outro.empty()) {
        for (const auto& m : preset.outro) {
            fadeOut.push_back({ anchor + m.offset * unit, gain(m.gainDb), m.curvature, m.eased });
        }
        fadeOut.push_back({ outClip.endTime, gain(preset.outro.back().gainDb), 0.0f });
        outFrom = std::max(outClip.startTime, fadeOut.front().time);
        fadeOut = clipped(fadeOut, outClip);
    }

    // Incoming volume: held from its start to the first marker, the markers, then held.
    // Fusion's Simple presets start the intro before the anchor (it finds
    // anchors from each track's loudness); here the anchor is the incoming
    // clip's start, so those intros are moved to begin at it.
    std::vector<GainPoint> fadeIn;
    double inTo = inClip.startTime;
    if (!preset.intro.empty()) {
        double introShift = 0.0;
        if (!preset.beatmix) {
            for (const auto& m : preset.intro) {
                introShift = std::min(introShift, m.offset);
            }
        }
        fadeIn.push_back({ inClip.startTime, gain(preset.intro.front().gainDb), 0.0f });
        for (const auto& m : preset.intro) {
            fadeIn.push_back({ anchor + (m.offset - introShift) * unit, gain(m.gainDb), m.curvature, m.eased });
        }
        inTo = std::min(inClip.endTime, fadeIn.back().time);
        fadeIn = clipped(fadeIn, inClip);
    }

    // The bass lines span the whole clip: the marker values where the markers are,
    // the first value before them and the last one after, as the engine expects.
    const auto bassLine = [&](const std::vector<MixHouseBassMarker>& markers, const trackedit::Clip& clip) {
        automation::AutomationPoints line;
        if (markers.empty()) {
            return line;
        }
        const auto valueAt = [&](double t) {
            if (t <= anchor + markers.front().offset * unit) {
                return markers.front().value;
            }
            for (size_t i = 1; i < markers.size(); ++i) {
                const double t1 = anchor + markers[i].offset * unit;
                if (t <= t1) {
                    const double t0 = anchor + markers[i - 1].offset * unit;
                    const double u = t1 > t0 ? (t - t0) / (t1 - t0) : 1.0;
                    return markers[i - 1].value + u * (markers[i].value - markers[i - 1].value);
                }
            }
            return markers.back().value;
        };
        line.push_back({ clip.startTime, valueAt(clip.startTime) });
        for (const auto& m : markers) {
            const double t = anchor + m.offset * unit;
            if (t > clip.startTime + 1e-6 && t < clip.endTime - 1e-6) {
                line.push_back({ t, m.value });
            }
        }
        line.push_back({ clip.endTime, valueAt(clip.endTime) });
        return line;
    };
    const automation::AutomationPoints bassOut = bassLine(preset.bassOutro, outClip);
    const automation::AutomationPoints bassIn = bassLine(preset.bassIntro, inClip);

    const automation::AutomationPoints midOut = bassLine(preset.midOutro, outClip);
    const automation::AutomationPoints midIn = bassLine(preset.midIntro, inClip);
    const automation::AutomationPoints highOut = bassLine(preset.highOutro, outClip);
    const automation::AutomationPoints highIn = bassLine(preset.highIntro, inClip);

    const bool hasVolume = !fadeOut.empty() || !fadeIn.empty();
    if (!hasVolume && bassOut.empty() && bassIn.empty() && midOut.empty() && midIn.empty() && highOut.empty()
        && highIn.empty() && preset.effects.empty()) {
        return false;
    }
    bool ok = true;
    // The effect lines (filter sweeps, noise, echo / reverb out): whatever an
    // earlier transition here wrote goes first (ADD FX ones too), then this
    // one's, each on the track's own DJ effect. No history of their own either.
    clearTransitionEffects(outgoing);
    clearTransitionEffects(incoming);
    m_effects.erase({ outgoing, incoming });
    m_disabled.erase({ outgoing, incoming });
    for (const MixHouseEffectLine& fx : preset.effects) {
        const std::string effectId = mixxxEffectIds() ? mixxxEffectIds()->effectIdForMixxxId(fx.mixxxId).toStdString() : std::string();
        if (effectId.empty()) {
            LOGW() << "GlideMix transition: no DJ effect " << fx.mixxxId;
            ok = false;
            continue;
        }
        const trackedit::ClipKey& key = fx.outgoing ? outgoing : incoming;
        ok = clipGain()->setClipEffectPoints(key, effectId, fx.parameterId, bassLine(fx.markers, fx.outgoing ? outClip : inClip))
             && ok;
    }
    // The EQ bands first: no history of their own. The last write below pushes
    // the one undo step. A band this transition leaves alone is cleared, so an
    // earlier transition's lines don't linger.
    const auto band = [&](const trackedit::ClipKey& key, int which, const automation::AutomationPoints& line) {
        ok = clipGain()->setClipBandPoints(key, which, line) && ok;
    };
    band(outgoing, 0, bassOut);
    band(incoming, 0, bassIn);
    band(outgoing, 1, midOut);
    band(incoming, 1, midIn);
    band(outgoing, 2, highOut);
    band(incoming, 2, highIn);
    if (!fadeOut.empty()) {
        ok = replaceGain(outgoing, outFrom, outClip.endTime, fadeOut, fadeIn.empty()) && ok;
    }
    if (!fadeIn.empty()) {
        ok = replaceGain(incoming, inClip.startTime, inTo, fadeIn, true) && ok;
    }
    if (!hasVolume) {
        // A bass- or effects-only transition wrote no volume line, so nothing pushed history.
        projectHistory()->pushHistoryState(preset.effects.empty() ? "Bass swap" : preset.name, "Transition");
    }
    if (ok) {
        m_applied[{ outgoing, incoming }] = preset.name;
        m_zonesChanged.notify();
        LOGI() << "GlideMix transition: " << preset.name << " at " << anchor << " s"
               << (bassOut.empty() && bassIn.empty() ? "" : " (with a bass swap)");
    } else {
        LOGW() << "GlideMix transition: could not write " << preset.name;
    }
    return ok;
}

bool MixHouseTransitions::applyCrossfade(const trackedit::ClipKey& outgoing, const trackedit::ClipKey& incoming)
{
    m_disabled.erase({ outgoing, incoming });
    return crossfadeFrom(outgoing, incoming, -1.0);
}

bool MixHouseTransitions::crossfadeFrom(const trackedit::ClipKey& outgoing, const trackedit::ClipKey& incoming,
                                        double fadeStart)
{
    Zone zone;
    if (!clipGain() || !findZone(outgoing, incoming, zone)) {
        return false;
    }
    if (fadeStart > zone.start && fadeStart < zone.end - kMinOverlapSec) {
        zone.start = fadeStart;
    }
    // Linked anchors (MixMeister): the mix starts where the outgoing Outro
    // anchor meets the incoming Intro anchor, not where the clips first touch.
    if (beatGrids()) {
        const auto outAnchors = beatGrids()->clipAnchors(outgoing);
        const auto inAnchors = beatGrids()->clipAnchors(incoming);
        if (outAnchors.valid && inAnchors.valid && std::abs(outAnchors.outro - inAnchors.intro) < 0.05
            && outAnchors.outro > zone.start && outAnchors.outro < zone.end - kMinOverlapSec) {
            zone.start = outAnchors.outro;
        }
    }
    const double len = zone.end - zone.start;

    std::vector<GainPoint> fadeOut;
    std::vector<GainPoint> fadeIn;
    // GlideMix: one S-curve each, as a DJ rides the faders -- two points a
    // line instead of a sampled curve.
    fadeOut.push_back({ zone.start, 1.0, 0.0f, true });
    fadeOut.push_back({ zone.start + len, kSilent, 0.0f });
    fadeIn.push_back({ zone.start, kSilent, 0.0f, true });
    fadeIn.push_back({ zone.start + len, 1.0, 0.0f });

    // Outgoing owns [zone start, its end]: after the fade it stays silent
    // (MixMeister's figures 11/12/21). Incoming owns [its start, zone end].
    const auto project = globalContext()->currentTrackeditProject();
    const trackedit::Clip outClip = project ? project->clip(outgoing) : trackedit::Clip {};
    const trackedit::Clip inClip = project ? project->clip(incoming) : trackedit::Clip {};
    fadeOut.push_back({ outClip.endTime, kSilent, 0.0f });
    fadeIn.insert(fadeIn.begin(), { inClip.startTime, kSilent, 0.0f });

    const bool ok = replaceGain(outgoing, zone.start, outClip.endTime, fadeOut, false)
                    && replaceGain(incoming, inClip.startTime, zone.end, fadeIn, true);
    if (ok) {
        m_zonesChanged.notify();
    } else {
        LOGW() << "GlideMix crossfade: could not write the volume lines";
    }
    return ok;
}

bool MixHouseTransitions::removeCrossfade(const trackedit::ClipKey& outgoing, const trackedit::ClipKey& incoming)
{
    Zone zone;
    if (!clipGain() || !findZone(outgoing, incoming, zone)) {
        return false;
    }
    const auto project = globalContext()->currentTrackeditProject();
    const trackedit::Clip outClip = project ? project->clip(outgoing) : trackedit::Clip {};
    const trackedit::Clip inClip = project ? project->clip(incoming) : trackedit::Clip {};

    m_applied.erase({ outgoing, incoming });
    m_effects.erase({ outgoing, incoming });
    // A bass swap belongs to the transition, so it goes with it, and so do its effects.
    for (int band = 0; band < 3; ++band) {
        clipGain()->setClipBandPoints(outgoing, band, {});
        clipGain()->setClipBandPoints(incoming, band, {});
    }
    clearTransitionEffects(outgoing);
    clearTransitionEffects(incoming);
    // GlideMix: the volume only where the transition owns it -- the outgoing
    // song from the overlap on, the incoming one up to the overlap's end -- so
    // volume drawn elsewhere in the songs stays.
    const double outFrom = std::clamp(zone.start, outClip.startTime, outClip.endTime);
    const double inTo = std::clamp(zone.end, inClip.startTime, inClip.endTime);
    const bool ok = replaceGain(outgoing, outFrom, outClip.endTime,
                                { { outFrom, 1.0, 0.0f }, { outClip.endTime, 1.0, 0.0f } }, false)
                    && replaceGain(incoming, inClip.startTime, inTo,
                                   { { inClip.startTime, 1.0, 0.0f }, { inTo, 1.0, 0.0f } }, true);
    m_zonesChanged.notify();
    return ok;
}

std::string MixHouseTransitions::mixNow(double playhead)
{
    const auto project = globalContext() ? globalContext()->currentTrackeditProject() : nullptr;
    if (!project || !beatGrids() || !trackeditInteraction()) {
        return {};
    }
    struct Item {
        trackedit::Clip clip;
        IMixHouseBeatGrids::Anchors anchors;
    };
    std::vector<Item> items;
    for (const trackedit::TrackId trackId : project->trackIdList()) {
        for (const trackedit::Clip& clip : project->clipList(trackId)) {
            const auto anchors = beatGrids()->clipAnchors(clip.key);
            if (anchors.valid) {
                items.push_back({ clip, anchors });
            }
        }
    }
    // The playing track: of the tracks under the playhead, the one mixed in
    // last (latest Intro anchor already passed).
    // A track whose intro is already audible but whose IN is still ahead is
    // the one coming in, not the one playing.
    const Item* current = nullptr;
    for (const bool passedIn : { true, false }) {
        for (const Item& item : items) {
            if (item.clip.startTime <= playhead && playhead < item.clip.endTime
                && (!passedIn || item.anchors.intro <= playhead)
                && (!current || item.anchors.intro > current->anchors.intro)) {
                current = &item;
            }
        }
        if (current) {
            break;
        }
    }
    if (!current) {
        return "Mix Now: nothing is playing at the playhead";
    }
    // The next track: the earliest Intro anchor still ahead of the playhead.
    const Item* incoming = nullptr;
    for (const Item& item : items) {
        if (&item != current && item.anchors.intro > playhead
            && (!incoming || item.anchors.intro < incoming->anchors.intro)) {
            incoming = &item;
        }
    }
    if (!incoming) {
        return "Mix Now: no next track";
    }
    // The next bar line, leaving time for the audio already rendered ahead.
    const trackedit::TimeSignature ts = project->timeSignature();
    if (ts.tempo <= 0.0) {
        return {};
    }
    // The next line of the Snap grid (a bar unless Snap says otherwise).
    const double gridBar = 60.0 / ts.tempo * (4.0 / std::max(1, ts.lower)) * std::max(1, ts.upper);
    const double bar = beatGrids()->syncGridSec() > 0.0 ? beatGrids()->syncGridSec() : gridBar;
    constexpr double kRenderAheadSec = 0.25;
    double target = std::ceil((playhead + kRenderAheadSec) / bar) * bar;
    std::string landedOn = "the next bar";
    // Phrase matching: land the incoming track's first phrase on a phrase start of the one that is
    // playing, so both change section together (intro against chorus, drop against drop) instead of
    // the mix falling anywhere inside a phrase. The phrase starts come from the playing track's
    // phrase analysis; without it, an 8-bar grid counted from its bar 1 stands in.
    const bool phraseMatching = !transitionConfiguration()
                                || transitionConfiguration()->mixNowLanding() == MixHouseMixNowLanding::NextPhrase;
    if (phraseMatching) {
        const double from = playhead + kRenderAheadSec;
        double phraseStart = -1.0;
        QString phraseLabel;
        // The whole clip is asked for: a narrower window would hand back the phrase that is playing
        // now cut off at `from`, which is not an upcoming boundary at all.
        for (const auto& phrase : beatGrids()->clipPhrases(current->clip.key, current->clip.startTime, current->clip.endTime)) {
            if (phrase.time >= from - 1e-6 && (phraseStart < 0.0 || phrase.time < phraseStart)) {
                phraseStart = phrase.time;
                phraseLabel = phrase.label;
            }
        }
        if (phraseStart >= 0.0) {
            landedOn = "the next phrase (" + phraseLabel.toStdString() + ")";
        } else {
            const double phraseSec = 8.0 * gridBar;
            const double origin = current->anchors.intro;
            phraseStart = origin + std::ceil((from - origin) / phraseSec - 1e-6) * phraseSec;
            landedOn = "the next 8-bar phrase";
        }
        // Only if the playing track is still there to be mixed out of; else fall back to the next bar.
        if (phraseStart < current->clip.endTime - kMinOverlapSec) {
            target = phraseStart;
        } else {
            landedOn = "the next bar (no phrase left in the playing track)";
        }
    }
    const double offset = target - incoming->anchors.intro;
    if (offset > -1e-3) {
        return "Mix Now: the next track already comes in at the next bar";
    }

    // Everything from the next track on moves together, earliest first so
    // each lands in space already vacated.
    std::vector<const Item*> moving;
    for (const Item& item : items) {
        if (&item != current && item.clip.startTime >= incoming->clip.startTime - 1e-6) {
            moving.push_back(&item);
        }
    }
    std::sort(moving.begin(), moving.end(), [](const Item* a, const Item* b) { return a->clip.startTime < b->clip.startTime; });
    // Audio already playing can't jump (and with a performance, it's locked).
    for (const Item* item : moving) {
        if (item->clip.startTime < playhead) {
            return "Mix Now: " + item->clip.title.toStdString() + " has already started playing";
        }
    }
    // A moved clip must not land on a clip that stays (its lane would cut it).
    for (const Item* item : moving) {
        const double newStart = item->clip.startTime + offset;
        const double newEnd = item->clip.endTime + offset;
        for (const Item& other : items) {
            const bool staying = std::find(moving.begin(), moving.end(), &other) == moving.end();
            if (staying && other.clip.key.trackId == item->clip.key.trackId && newStart < other.clip.endTime - 1e-6
                && newEnd > other.clip.startTime + 1e-6) {
                return "Mix Now: " + item->clip.title.toStdString() + " would land on another clip in its lane";
            }
        }
    }
    Zone before;
    const bool wasCrossfaded = findZone(current->clip.key, incoming->clip.key, before) && before.crossfaded;
    for (const Item* item : moving) {
        trackeditInteraction()->changeClipStartTime(item->clip.key, item->clip.startTime + offset, false);
    }
    projectHistory()->pushHistoryState("Mix now", "Mix now");   // one undo step for the pull

    // MixMeister links the two tracks by their anchors: the playing track's Outro Anchor stays
    // lined up with the next track's Intro Anchor, and the fade starts at that anchor. The Intro
    // Anchor has just landed on `target` with the clip; the Outro Anchor goes there too. Only for
    // this session: the anchors saved for the track in the library are its defaults for every
    // future mix, and pressing Mix Now somewhere mid-track must not rewrite them.
    beatGrids()->moveClipAnchor(current->clip.key, 1, target, false);

    // Then the mix itself. MixMeister's Mix Now "mixes immediately in to the track that's
    // playing", through the transition that pair has, so pulling a track forward always
    // ends in a fade rather than two tracks at full volume. Here that is the Mix Now
    // transition (a quick crossfade with a bass swap on the beat unless another is chosen),
    // starting on the bar the track lands on.
    // The choice is in Preferences → Transitions: one of Mixtape's or the user's own, or one of
    // MixMeister's presets; empty means only pull, leaving the volumes as they are.
    const std::string chosen = transitionConfiguration() ? transitionConfiguration()->mixNowTransition()
                               : std::string(kDefaultMixNowTransition);
    std::optional<MixHouseTransitionPreset> mixNowPreset;
    if (!chosen.empty()) {
        if (const auto spec = transitionConfiguration() ? transitionConfiguration()->spec(chosen) : std::nullopt) {
            mixNowPreset = presetFromSpec(*spec);
        } else {
            for (const MixHouseTransitionPreset& candidate : presets()) {
                if (candidate.name == chosen) {
                    mixNowPreset = candidate;
                    break;
                }
            }
        }
        if (!mixNowPreset) {
            LOGW() << "Mix Now: the transition \"" << chosen << "\" no longer exists, only pulling the track";
        }
    }
    if (wasCrossfaded && mixNowPreset) {
        removeCrossfade(current->clip.key, incoming->clip.key);   // start from clean lines
    }
    std::string transition;
    if (mixNowPreset && writePreset(current->clip.key, incoming->clip.key, *mixNowPreset, target)) {
        transition = ", " + mixNowPreset->name;
    } else if (!mixNowPreset && wasCrossfaded) {
        // No transition chosen: a crossfade that was already there follows the track, as before.
        removeCrossfade(current->clip.key, incoming->clip.key);
        if (crossfadeFrom(current->clip.key, incoming->clip.key, target)) {
            transition = ", crossfade";
        }
    }
    m_zonesChanged.notify();
    return "Mix Now: " + incoming->clip.title.toStdString() + " pulled " + std::to_string(-offset) + " s forward to "
           + std::to_string(target) + " s, on " + landedOn + " (" + std::to_string(moving.size()) + " clips)" + transition;
}

bool MixHouseTransitions::laneLines(const trackedit::ClipKey& outgoing, const trackedit::ClipKey& incoming, Lane lane,
                                    LaneLine& out, LaneLine& in) const
{
    Zone zone;
    if (!clipGain() || !findZone(outgoing, incoming, zone) || zone.end <= zone.start) {
        return false;
    }
    using Points = automation::AutomationPoints;
    const auto valueAt = [](const Points& points, double t, double fallback) {
        if (points.empty()) {
            return fallback;
        }
        if (t <= points.front().xValue) {
            return points.front().yValue;
        }
        for (size_t i = 1; i < points.size(); ++i) {
            if (t <= points[i].xValue) {
                const double span = points[i].xValue - points[i - 1].xValue;
                const double u = span > 0.0 ? (t - points[i - 1].xValue) / span : 1.0;
                return points[i - 1].yValue + u * (points[i].yValue - points[i - 1].yValue);
            }
        }
        return points.back().yValue;
    };
    const std::string filterId = mixxxEffectIds()
                                 ? mixxxEffectIds()->effectIdForMixxxId("org.mixxx.effects.filter").toStdString() : std::string();
    const auto build = [&](const trackedit::ClipKey& key, LaneLine& line) {
        Points a, b;   // Color: low-pass and high-pass
        switch (lane) {
        case Lane::Volume: a = clipGain()->clipGainPoints(key);
            break;
        case Lane::Low: a = clipGain()->clipBandPoints(key, 0);
            break;
        case Lane::Mid: a = clipGain()->clipBandPoints(key, 1);
            break;
        case Lane::High: a = clipGain()->clipBandPoints(key, 2);
            break;
        case Lane::Color:
            if (!filterId.empty()) {
                a = clipGain()->clipEffectPoints(key, filterId, "lpf");
                b = clipGain()->clipEffectPoints(key, filterId, "hpf");
            }
            break;
        }
        const auto value = [&](double t) {
            if (lane == Lane::Volume) {
                return std::clamp(valueAt(a, t, 1.0), 0.0, 1.0);
            }
            if (lane == Lane::Low || lane == Lane::Mid || lane == Lane::High) {
                return std::clamp(valueAt(a, t, 1.0), 0.0, 1.0);
            }
            const double lpf = valueAt(a, t, 1.0);
            const double hpf = valueAt(b, t, 0.0);
            return lpf < 0.999 ? 0.5 * lpf : std::clamp(0.5 + 0.5 * hpf, 0.0, 1.0);
        };
        constexpr int kSamples = 160;
        // Volume from the clip's own envelope, so curves (Option-drag) show as curves.
        std::vector<double> envelope;
        if (lane == Lane::Volume && beatGrids()) {
            beatGrids()->clipVolume(key, zone.start, (zone.end - zone.start) / kSamples, kSamples + 1, envelope);
        }
        for (int i = 0; i <= kSamples; ++i) {
            const double t = zone.start + (zone.end - zone.start) * i / kSamples;
            line.samples.push_back({ t, envelope.size() > size_t(i) ? std::clamp(envelope[size_t(i)], 0.0, 1.0) : value(t) });
        }
        for (const Points* points : { &a, &b }) {
            for (const auto& p : *points) {
                if (p.xValue > zone.start + 1e-6 && p.xValue < zone.end - 1e-6) {
                    line.points.push_back({ p.xValue, value(p.xValue) });
                }
            }
        }
    };
    build(outgoing, out);
    build(incoming, in);
    return true;
}

bool MixHouseTransitions::setLanePoints(const trackedit::ClipKey& outgoing, const trackedit::ClipKey& incoming, Lane lane,
                                        bool outgoingSide, const std::vector<std::pair<double, double> >& points)
{
    Zone zone;
    if (!clipGain() || !findZone(outgoing, incoming, zone)) {
        return false;
    }
    const auto project = globalContext()->currentTrackeditProject();
    const trackedit::ClipKey& key = outgoingSide ? outgoing : incoming;
    // The zone's edges stay: only what lies strictly inside is replaced.
    const double from = zone.start + 1e-6;
    const double to = zone.end - 1e-6;
    std::vector<std::pair<double, double> > inside;
    for (const auto& [t, v] : points) {
        if (t >= from && t <= to) {
            inside.push_back({ t, std::clamp(v, 0.0, 1.0) });
        }
    }
    std::sort(inside.begin(), inside.end());

    // A whole-clip line: its own points outside the zone, the edited ones inside.
    const auto merge = [&](const automation::AutomationPoints& current, double fallback,
                           const std::vector<std::pair<double, double> >& inside) {
        automation::AutomationPoints line;
        for (const auto& p : current) {
            if (p.xValue < from) {
                line.push_back(p);
            }
        }
        for (const auto& [t, v] : inside) {
            line.push_back({ t, v });
        }
        for (const auto& p : current) {
            if (p.xValue > to) {
                line.push_back(p);
            }
        }
        if (project) {
            const trackedit::Clip clip = project->clip(key);
            if (line.empty() || line.front().xValue > clip.startTime + 1e-6) {
                line.insert(line.begin(), { clip.startTime, line.empty() ? fallback : line.front().yValue });
            }
            if (line.back().xValue < clip.endTime - 1e-6) {
                line.push_back({ clip.endTime, line.back().yValue });
            }
        }
        return line;
    };

    bool ok = true;
    std::string what;
    switch (lane) {
    case Lane::Volume: {
        // The curves (Option-drag) of the points being edited stay: a point
        // keeps the curvature of the one it was, matched in order.
        std::vector<float> curvature;
        for (const auto& seg : clipGain()->clipGainSegments(key)) {
            if (seg.start.xValue >= from && seg.start.xValue <= to) {
                curvature.push_back(seg.curvature);
            }
        }
        std::vector<GainPoint> gain;
        for (size_t i = 0; i < inside.size(); ++i) {
            const float c = curvature.size() == inside.size() ? curvature[i] : 0.0f;
            gain.push_back({ inside[i].first, std::max(inside[i].second, kSilent), c });
        }
        what = "volume";
        // Pushes the undo step itself.
        ok = replaceGain(key, from, to, gain, true);
        m_zonesChanged.notify();
        return ok;
    }
    case Lane::Low:
    case Lane::Mid:
    case Lane::High: {
        const int band = lane == Lane::Low ? 0 : (lane == Lane::Mid ? 1 : 2);
        ok = clipGain()->setClipBandPoints(key, band, merge(clipGain()->clipBandPoints(key, band), 1.0, inside));
        what = band == 0 ? "bass" : (band == 1 ? "mids" : "highs");
        break;
    }
    case Lane::Color: {
        const std::string filterId = mixxxEffectIds()
                                     ? mixxxEffectIds()->effectIdForMixxxId("org.mixxx.effects.filter").toStdString()
                                     : std::string();
        if (filterId.empty()) {
            return false;
        }
        // One knob to the filter's two: below the middle the low-pass closes,
        // above it the high-pass opens.
        std::vector<std::pair<double, double> > lpf, hpf;
        for (const auto& [t, v] : inside) {
            lpf.push_back({ t, v < 0.5 ? 2.0 * v : 1.0 });
            hpf.push_back({ t, v > 0.5 ? 2.0 * v - 1.0 : 0.0 });
        }
        const auto withInside = [&](const std::vector<std::pair<double, double> >& values, const char* parameter,
                                    double neutral) {
            const auto line = merge(clipGain()->clipEffectPoints(key, filterId, parameter), neutral, values);
            return clipGain()->setClipEffectPoints(key, filterId, parameter, line);
        };
        ok = withInside(lpf, "lpf", 1.0) && ok;
        ok = withInside(hpf, "hpf", 0.0) && ok;
        what = "filter";
        break;
    }
    }
    projectHistory()->pushHistoryState("Edited the transition's " + what, "Transition lane");
    m_zonesChanged.notify();
    return ok;
}

bool MixHouseTransitions::addEffect(const trackedit::ClipKey& outgoing, const trackedit::ClipKey& incoming,
                                    const std::string& kind)
{
    Zone zone;
    const auto project = globalContext()->currentTrackeditProject();
    if (!project || !findZone(outgoing, incoming, zone)) {
        return false;
    }
    const trackedit::TimeSignature ts = project->timeSignature();
    const double beat = ts.tempo > 0.0 ? 60.0 / ts.tempo * (4.0 / std::max(1, ts.lower)) : 0.5;
    const double bar = beat * std::max(1, ts.upper);
    double anchor = std::ceil(zone.start / bar - 1e-6) * bar;
    if (anchor >= zone.end - beat) {
        anchor = zone.start;
    }
    // The effect over the part of the overlap from the anchor, in whole beats.
    MixHouseTransitionSpec spec;
    spec.lengthBeats = std::max(1.0, std::floor((zone.end - anchor) / beat + 1e-6));
    if (kind == "filter-out") {
        spec.filter = MixHouseFilterSweep::OutHighPass;
        spec.name = "Filter out";
    } else if (kind == "filter-in") {
        spec.filter = MixHouseFilterSweep::InHighPass;
        spec.name = "Filter in";
    } else if (kind == "echo") {
        spec.effectOut = MixHouseEffectOut::Echo;
        spec.name = "Echo out";
    } else if (kind == "reverb") {
        spec.effectOut = MixHouseEffectOut::Reverb;
        spec.name = "Reverb out";
    } else if (kind == "riser") {
        spec.noise = MixHouseNoiseSweep::Riser;
        spec.name = "Noise riser";
    } else if (kind == "downsweep") {
        spec.noise = MixHouseNoiseSweep::Downsweep;
        spec.name = "Noise downsweep";
    } else if (kind != "vinyl-brake" && kind != "tape-stop" && kind != "roll-out") {
        return false;
    }
    MixHouseTransitionPreset preset = presetFromSpec(spec);
    // The mixer beat effects: at the end of the overlap, on the song going out.
    const double length = spec.lengthBeats;
    constexpr double kStep = 0.0625;
    if (kind == "vinyl-brake") {
        preset.name = "Vinyl brake";
        preset.effects.push_back({ "org.glidemix.effects.tapestop", "speed", true,
                                   { { length - 1.0, 1.0 }, { length, 0.0 } } });
    } else if (kind == "tape-stop") {
        preset.name = "Tape stop";
        preset.effects.push_back({ "org.glidemix.effects.tapestop", "speed", true,
                                   { { length - 4.0, 1.0 }, { length, 0.0 } } });
    } else if (kind == "roll-out") {
        // Rolls the last two beats in 1/4-beat slices (the length knob at 0.2 is 1/4 beat).
        preset.name = "Beat roll out";
        preset.effects.push_back({ "org.glidemix.effects.roll", "length", true, { { length - 2.0, 0.2 } } });
        preset.effects.push_back({ "org.glidemix.effects.roll", "roll", true,
                                   { { length - 2.0 - kStep, 0.0 }, { length - 2.0, 1.0 }, { length, 1.0 },
                                     { length + kStep, 0.0 } } });
    }
    if (preset.effects.empty() || !clipGain()) {
        return false;
    }
    // GlideMix: kept per transition, so the panel can list it, turn it down
    // and take it off again. Adding the same kind again replaces it.
    const ZoneKey key { outgoing, incoming };
    EffectEntry entry;
    entry.info.kind = kind;
    entry.info.name = preset.name;
    entry.lines = preset.effects;
    entry.anchor = anchor;
    entry.unit = beat;
    auto& list = m_effects[key];
    list.erase(std::remove_if(list.begin(), list.end(), [&](const EffectEntry& e) { return e.info.kind == kind; }),
               list.end());
    const bool ok = writeEffectLines(key, entry, entry.info.amount);
    list.push_back(entry);
    projectHistory()->pushHistoryState("Added " + preset.name + " to the transition", "Transition effect");
    m_zonesChanged.notify();
    return ok;
}

bool MixHouseTransitions::writeEffectLines(const ZoneKey& zone, const EffectEntry& entry, double amount)
{
    const auto project = globalContext()->currentTrackeditProject();
    if (!project || !clipGain() || !mixxxEffectIds()) {
        return false;
    }
    constexpr double kEdge = 0.001;   // a step a millisecond long, not a vertical one
    bool ok = true;
    for (const MixHouseEffectLine& fx : entry.lines) {
        const std::string effectId = mixxxEffectIds()->effectIdForMixxxId(fx.mixxxId).toStdString();
        if (effectId.empty() || fx.markers.empty()) {
            ok = false;
            continue;
        }
        double neutral = fx.markers.front().value;   // a setting, not a sweep (the roll's length)
        for (const MixHouseTransitionEffectParameter& p : transitionEffectParameters()) {
            if (p.mixxxId == fx.mixxxId && p.parameterId == fx.parameterId) {
                neutral = p.neutral;
            }
        }
        const trackedit::ClipKey& key = fx.outgoing ? zone.first : zone.second;
        const trackedit::Clip clip = project->clip(key);
        automation::AutomationPoints line;
        const auto add = [&](double t, double v) {
            if (t < clip.startTime - 1e-9 || t > clip.endTime + 1e-9) {
                return;
            }
            t = std::clamp(t, clip.startTime, clip.endTime);
            if (!line.empty() && t <= line.back().xValue + 1e-9) {
                line.back().yValue = v;   // same moment: the later value wins
                return;
            }
            line.push_back({ t, v });
        };
        const double first = entry.anchor + fx.markers.front().offset * entry.unit;
        const double last = entry.anchor + fx.markers.back().offset * entry.unit;
        add(clip.startTime, neutral);
        add(first - kEdge, neutral);
        for (const MixHouseBassMarker& m : fx.markers) {
            add(entry.anchor + m.offset * entry.unit, neutral + std::clamp(amount, 0.0, 1.0) * (m.value - neutral));
        }
        add(last + kEdge, neutral);
        add(clip.endTime, neutral);
        ok = clipGain()->setClipEffectPoints(key, effectId, fx.parameterId, line) && ok;
    }
    return ok;
}

std::vector<IMixHouseTransitions::AddedEffect> MixHouseTransitions::addedEffects(const trackedit::ClipKey& outgoing,
                                                                                 const trackedit::ClipKey& incoming) const
{
    std::vector<AddedEffect> result;
    const auto it = m_effects.find({ outgoing, incoming });
    if (it != m_effects.end()) {
        for (const EffectEntry& e : it->second) {
            result.push_back(e.info);
        }
    }
    return result;
}

bool MixHouseTransitions::setEffectAmount(const trackedit::ClipKey& outgoing, const trackedit::ClipKey& incoming,
                                          const std::string& kind, double amount)
{
    const auto it = m_effects.find({ outgoing, incoming });
    if (it == m_effects.end()) {
        return false;
    }
    for (EffectEntry& e : it->second) {
        if (e.info.kind == kind) {
            e.info.amount = std::clamp(amount, 0.0, 1.0);
            const bool ok = writeEffectLines(it->first, e, e.info.enabled ? e.info.amount : 0.0);
            projectHistory()->pushHistoryState("Changed " + e.info.name + " in the transition", "Transition effect");
            m_zonesChanged.notify();
            return ok;
        }
    }
    return false;
}

bool MixHouseTransitions::setEffectEnabled(const trackedit::ClipKey& outgoing, const trackedit::ClipKey& incoming,
                                           const std::string& kind, bool enabled)
{
    const auto it = m_effects.find({ outgoing, incoming });
    if (it == m_effects.end()) {
        return false;
    }
    for (EffectEntry& e : it->second) {
        if (e.info.kind == kind) {
            e.info.enabled = enabled;
            const bool ok = writeEffectLines(it->first, e, enabled ? e.info.amount : 0.0);
            projectHistory()->pushHistoryState((enabled ? "Switched on " : "Switched off ") + e.info.name,
                                               "Transition effect");
            m_zonesChanged.notify();
            return ok;
        }
    }
    return false;
}

bool MixHouseTransitions::removeEffect(const trackedit::ClipKey& outgoing, const trackedit::ClipKey& incoming,
                                       const std::string& kind)
{
    const auto it = m_effects.find({ outgoing, incoming });
    if (it == m_effects.end()) {
        return false;
    }
    auto& list = it->second;
    for (auto e = list.begin(); e != list.end(); ++e) {
        if (e->info.kind == kind) {
            const std::string name = e->info.name;
            const bool ok = writeEffectLines(it->first, *e, 0.0);   // neutral all along
            list.erase(e);
            projectHistory()->pushHistoryState("Removed " + name + " from the transition", "Transition effect");
            m_zonesChanged.notify();
            return ok;
        }
    }
    return false;
}

bool MixHouseTransitions::setTransitionEnabled(const trackedit::ClipKey& outgoing, const trackedit::ClipKey& incoming,
                                               bool enabled)
{
    const ZoneKey key { outgoing, incoming };
    if (!enabled) {
        if (m_disabled.count(key)) {
            return true;
        }
        const auto applied = m_applied.find(key);
        const std::string name = applied != m_applied.end() ? applied->second : std::string();
        const bool ok = removeCrossfade(outgoing, incoming);
        m_disabled[key] = name;
        m_zonesChanged.notify();
        return ok;
    }
    const auto it = m_disabled.find(key);
    if (it == m_disabled.end()) {
        return true;
    }
    const std::string name = it->second;
    m_disabled.erase(it);
    if (!name.empty()) {
        for (const MixHouseTransitionPreset& preset : presets()) {
            if (preset.name == name) {
                return applyPresetValue(outgoing, incoming, preset);
            }
        }
        if (applyTransition(outgoing, incoming, name)) {
            return true;
        }
    }
    return applyCrossfade(outgoing, incoming);
}
