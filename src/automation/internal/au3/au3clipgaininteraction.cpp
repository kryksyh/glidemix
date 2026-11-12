/*
* Audacity: A Digital Audio Editor
*/

#include "au3clipgaininteraction.h"

#include "au3-mixer/Envelope.h"
#include "au3-wave-track/WaveClip.h"
#include "au3-wave-track/WaveTrack.h"
#include "au3-wave-track/TrackParameterAutomation.h"
#include "au3-realtime-effects/RealtimeEffectList.h"
#include "au3-realtime-effects/RealtimeEffectState.h"
#include "log.h"
#include "au3wrap/au3types.h"

#include "au3wrap/internal/domaccessor.h"
#include "au3wrap/internal/domconverter.h"

#include "automationtypes.h"

#include <algorithm>
#include <cmath>

using namespace au::automation;
using namespace au::au3;

namespace {
// NOTE: newly recorded clips doesn't have offset/sequenceLength synced with envelope.
// We need to sync it before interacting with clip gain
void syncEnvelopeOffset(const std::shared_ptr<Au3WaveClip>& clip, Envelope& env)
{
    const double sequenceStartTime = clip->GetSequenceStartTime();
    if (env.GetOffset() != sequenceStartTime) {
        env.SetOffset(sequenceStartTime);
    }

    // Recording path may keep clip envelope length stale (e.g. still zero)
    // while sequence data grows via linked pending clip updates.
    // Ensure envelope domain matches current sequence duration.
    const double sequenceLen = clip->GetSequenceEndTime() - sequenceStartTime;
    if (env.GetTrackLen() != sequenceLen) {
        env.SetTrackLen(sequenceLen, 1.0 / clip->GetRate());
    }
}

void restoreEnvelopeFromSnapshot(Envelope& env, const AutomationDragSession& session)
{
    env.Clear();
    for (const auto& p : session.originalPoints) {
        env.Insert(p.xValue, p.yValue);
    }
}

// How many original points have we crossed while dragging to new time?
int computeRightCrossedIndex(const AutomationDragSession& session, double time)
{
    int idx = session.originalIndex;
    for (int i = session.originalIndex + 1; i < static_cast<int>(session.originalPoints.size()); ++i) {
        const double pointTime = session.originalPoints[i].xValue;
        if (pointTime > session.originalXValue && pointTime < time) {
            idx = i;
        } else {
            break;
        }
    }
    return idx;
}

// How many original points have we crossed while dragging to new time?
int computeLeftCrossedIndex(const AutomationDragSession& session, double time)
{
    int idx = session.originalIndex;
    for (int i = session.originalIndex - 1; i >= 0; --i) {
        const double pointTime = session.originalPoints[i].xValue;
        if (pointTime < session.originalXValue && pointTime > time) {
            idx = i;
        } else {
            break;
        }
    }
    return idx;
}

void applyDragWithCrossingRemoval(Envelope& env, AutomationDragSession& session, double newTime, double newValue)
{
    const int desiredRight = computeRightCrossedIndex(session, newTime);
    const int desiredLeft = computeLeftCrossedIndex(session, newTime);

    while (session.lastConsumedRight < desiredRight) {
        env.Delete(session.currentDragIndex + 1);
        ++session.lastConsumedRight;
    }
    while (session.lastConsumedRight > desiredRight) {
        const auto& restore = session.originalPoints[session.lastConsumedRight];
        env.Insert(session.currentDragIndex + 1, EnvPoint { restore.xValue, restore.yValue });
        --session.lastConsumedRight;
    }

    while (session.lastConsumedLeft > desiredLeft) {
        env.Delete(session.currentDragIndex - 1);
        --session.currentDragIndex;
        --session.lastConsumedLeft;
    }
    while (session.lastConsumedLeft < desiredLeft) {
        const auto& restore = session.originalPoints[session.lastConsumedLeft];
        env.Insert(session.currentDragIndex, EnvPoint { restore.xValue, restore.yValue });
        ++session.currentDragIndex;
        ++session.lastConsumedLeft;
    }

    env.SetDragPoint(session.currentDragIndex);
    env.MoveDragPoint(newTime, newValue);
}
}

Au3ClipGainInteraction::Au3ClipGainInteraction(const muse::modularity::ContextPtr& ctx)
    : muse::Contextable(ctx) {}

std::optional<AutomationInfo> Au3ClipGainInteraction::clipGainInfo(
    const trackedit::ClipKey& clipKey) const
{
    Au3WaveTrack* waveTrack = DomAccessor::findWaveTrack(projectRef(), Au3TrackId(clipKey.trackId));
    if (!waveTrack) {
        return {};
    }

    std::shared_ptr<Au3WaveClip> clip = DomAccessor::findWaveClip(waveTrack, clipKey.itemId);
    if (!clip) {
        return {};
    }

    auto& env = clip->GetEnvelope();
    syncEnvelopeOffset(clip, env);

    AutomationInfo info;
    info.minValue = env.GetMinValue();
    info.maxValue = env.GetMaxValue();
    info.defaultValue = env.GetDefaultValue();
    info.exponential = env.GetExponential();
    info.version = env.GetVersion();

    return info;
}

AutomationPoints Au3ClipGainInteraction::clipGainPoints(const trackedit::ClipKey& clipKey) const
{
    Au3WaveTrack* waveTrack = DomAccessor::findWaveTrack(projectRef(), Au3TrackId(clipKey.trackId));
    if (!waveTrack) {
        return {};
    }

    std::shared_ptr<Au3WaveClip> clip = DomAccessor::findWaveClip(waveTrack, clipKey.itemId);
    if (!clip) {
        return {};
    }

    auto& env = clip->GetEnvelope();
    syncEnvelopeOffset(clip, env);

    AutomationPoints pts;
    const auto n = env.GetNumberOfPoints();
    pts.reserve(n);

    const double offset = env.GetOffset();
    for (size_t i = 0; i < n; ++i) {
        const auto& p = env[int(i)];
        pts.push_back({ offset + p.GetT(), p.GetVal() });
    }
    return pts;
}

namespace {
// The points of a clip's EQ or effect line into its envelope, kept inside the
// clip and in order. At most two points share a time (the two sides of a
// jump): points before the clip start all clamp to 0, and a third one there
// made the line one the project loader refuses -- after which every save
// failed and the app quit. A third takes the second one's place instead.
void insertClipLinePoints(Envelope& envelope, const AutomationPoints& points, double offset, double length)
{
    double last = 0.0;
    for (const AutomationPoint& p : points) {
        const double t = std::clamp(p.xValue - offset, last, std::max(last, length));
        const double v = std::clamp(p.yValue, 0.0, 1.0);
        const int n = static_cast<int>(envelope.GetNumberOfPoints());
        if (n > 1 && envelope[n - 1].GetT() == t && envelope[n - 2].GetT() == t) {
            envelope.Delete(n - 1);
        }
        envelope.Insert(t, v);
        last = t;
    }
}

const char* bandParameter(int band)
{
    switch (band) {
    case 1: return clipautomation::kMixtapeMidBandParameter;
    case 2: return clipautomation::kMixtapeHighBandParameter;
    default: return clipautomation::kMixtapeLowBandParameter;
    }
}
}

AutomationPoints Au3ClipGainInteraction::clipBassPoints(const trackedit::ClipKey& clipKey) const
{
    return clipBandPoints(clipKey, 0);
}

bool Au3ClipGainInteraction::clipDisabled(const trackedit::ClipKey& clipKey) const
{
    Au3WaveTrack* waveTrack = DomAccessor::findWaveTrack(projectRef(), Au3TrackId(clipKey.trackId));
    const std::shared_ptr<Au3WaveClip> clip = waveTrack ? DomAccessor::findWaveClip(waveTrack, clipKey.itemId) : nullptr;
    return clip && clip->GetParameterAutomation().IsClipDisabled();
}

bool Au3ClipGainInteraction::setClipDisabled(const trackedit::ClipKey& clipKey, bool disabled)
{
    Au3WaveTrack* waveTrack = DomAccessor::findWaveTrack(projectRef(), Au3TrackId(clipKey.trackId));
    const std::shared_ptr<Au3WaveClip> clip = waveTrack ? DomAccessor::findWaveClip(waveTrack, clipKey.itemId) : nullptr;
    if (!clip) {
        return false;
    }
    // A stepped switch in the clip's own curve store, so it follows the clip
    // through moves, copies, undo and saving (like the transition EQ lines).
    auto data = clip->GetParameterAutomation();
    const double offset = clip->GetSequenceStartTime();
    data.SetOffset(offset);
    data.SetTrackLen(clip->GetSequenceEndTime() - offset, 1.0 / clip->GetRate());
    auto* curve = data.FindCurve(clipautomation::kMixtapeBuiltinInstanceId, clipautomation::kMixtapeDisabledParameter);
    if (!curve && !disabled) {
        return true;
    }
    if (!curve) {
        curve = data.AddCurve(clipautomation::kMixtapeBuiltinInstanceId, clipautomation::kMixtapeBuiltinPluginId,
                              clipautomation::kMixtapeDisabledParameter, 1.0);
    }
    if (!curve) {
        return false;
    }
    curve->enabled = disabled;
    curve->stepped = true;
    curve->bezier = {};
    curve->envelope.Flatten(1.0);
    clip->GetParameterAutomation() = std::move(data);
    return true;
}

bool Au3ClipGainInteraction::setClipBassPoints(const trackedit::ClipKey& clipKey, const AutomationPoints& points)
{
    return setClipBandPoints(clipKey, 0, points);
}

AutomationPoints Au3ClipGainInteraction::clipBandPoints(const trackedit::ClipKey& clipKey, int band) const
{
    Au3WaveTrack* waveTrack = DomAccessor::findWaveTrack(projectRef(), Au3TrackId(clipKey.trackId));
    if (!waveTrack) {
        return {};
    }
    const std::shared_ptr<Au3WaveClip> clip = DomAccessor::findWaveClip(waveTrack, clipKey.itemId);
    if (!clip) {
        return {};
    }
    AutomationPoints points;
    for (const auto& curve : clip->GetParameterAutomation().Curves()) {
        if (!curve.enabled || curve.effectInstanceId != clipautomation::kMixtapeBuiltinInstanceId
            || curve.parameterId != bandParameter(band)) {
            continue;
        }
        const double offset = curve.envelope.GetOffset();
        for (size_t i = 0; i < curve.envelope.GetNumberOfPoints(); ++i) {
            const auto& p = curve.envelope[int(i)];
            points.push_back({ offset + p.GetT(), p.GetVal() });
        }
        break;
    }
    return points;
}

bool Au3ClipGainInteraction::setClipBandPoints(const trackedit::ClipKey& clipKey, int band, const AutomationPoints& points)
{
    Au3WaveTrack* waveTrack = DomAccessor::findWaveTrack(projectRef(), Au3TrackId(clipKey.trackId));
    if (!waveTrack) {
        return false;
    }
    const std::shared_ptr<Au3WaveClip> clip = DomAccessor::findWaveClip(waveTrack, clipKey.itemId);
    if (!clip) {
        return false;
    }
    // Edited as a copy and swapped in, so a refused edit leaves the clip as it was.
    auto data = clip->GetParameterAutomation();
    const double offset = clip->GetSequenceStartTime();
    data.SetOffset(offset);
    data.SetTrackLen(clip->GetSequenceEndTime() - offset, 1.0 / clip->GetRate());
    auto* curve = data.FindCurve(clipautomation::kMixtapeBuiltinInstanceId, bandParameter(band));
    if (points.empty()) {
        if (!curve) {
            return true;   // nothing to remove
        }
        // There is no way to delete a curve, so it is switched off; the engine ignores disabled ones.
        curve->enabled = false;
        curve->bezier = {};
        curve->envelope.Flatten(1.0);
    } else {
        if (!curve) {
            curve = data.AddCurve(clipautomation::kMixtapeBuiltinInstanceId, clipautomation::kMixtapeBuiltinPluginId,
                                  bandParameter(band), 1.0);
        }
        if (!curve) {
            return false;
        }
        curve->enabled = true;
        curve->bezier = {};   // the plain envelope is authoritative when there is no curved form
        curve->envelope.Clear();
        // Kept inside the clip: its start in project seconds can round to a
        // hair before the sequence start, and the engine refuses a negative time.
        insertClipLinePoints(curve->envelope, points, offset, clip->GetSequenceEndTime() - offset);
    }
    clip->GetParameterAutomation() = std::move(data);
    return true;
}

AutomationPoints Au3ClipGainInteraction::clipEffectPoints(const trackedit::ClipKey& clipKey, const std::string& effectId,
                                                         const std::string& parameterId) const
{
    Au3WaveTrack* waveTrack = DomAccessor::findWaveTrack(projectRef(), Au3TrackId(clipKey.trackId));
    const std::shared_ptr<Au3WaveClip> clip = waveTrack ? DomAccessor::findWaveClip(waveTrack, clipKey.itemId) : nullptr;
    if (!clip) {
        return {};
    }
    auto& effects = RealtimeEffectList::Get(*waveTrack);
    for (size_t i = 0; i < effects.GetStatesCount(); ++i) {
        const auto state = effects.GetStateAt(i);
        if (!state || state->GetID().ToStdString(wxConvUTF8) != effectId) {
            continue;
        }
        for (const auto& curve : clip->GetParameterAutomation().Curves()) {
            if (!curve.enabled || curve.effectInstanceId != state->GetInstanceId() || curve.parameterId != parameterId) {
                continue;
            }
            AutomationPoints points;
            const double offset = curve.envelope.GetOffset();
            for (size_t k = 0; k < curve.envelope.GetNumberOfPoints(); ++k) {
                points.push_back({ offset + curve.envelope[int(k)].GetT(), curve.envelope[int(k)].GetVal() });
            }
            return points;
        }
        break;
    }
    return {};
}

bool Au3ClipGainInteraction::setClipEffectPoints(const trackedit::ClipKey& clipKey, const std::string& effectId,
                                                 const std::string& parameterId, const AutomationPoints& points,
                                                 bool addIfMissing)
{
    Au3WaveTrack* waveTrack = DomAccessor::findWaveTrack(projectRef(), Au3TrackId(clipKey.trackId));
    if (!waveTrack) {
        return false;
    }
    const std::shared_ptr<Au3WaveClip> clip = DomAccessor::findWaveClip(waveTrack, clipKey.itemId);
    if (!clip) {
        return false;
    }

    // The track's own instance of the effect: a DJ's one filter per deck.
    auto& effects = RealtimeEffectList::Get(*waveTrack);
    std::shared_ptr<RealtimeEffectState> state;
    for (size_t i = 0; i < effects.GetStatesCount(); ++i) {
        const auto candidate = effects.GetStateAt(i);
        if (candidate && candidate->GetID().ToStdString(wxConvUTF8) == effectId) {
            state = candidate;
            break;
        }
    }
    if (!state) {
        if (points.empty() || !addIfMissing) {
            return true;   // no effect, so nothing to change
        }
        if (!effectsProvider() || !audioEngine() || !effectsProvider()->loadEffect(muse::String::fromStdString(effectId))) {
            LOGW() << "transition effect not available: " << effectId;
            return false;
        }
        state = audioEngine()->addRealtimeEffectState(projectRef(), waveTrack, effectId);
        if (!state) {
            return false;
        }
    }
    const std::string instanceId = state->GetInstanceId();

    auto data = clip->GetParameterAutomation();
    const double offset = clip->GetSequenceStartTime();
    data.SetOffset(offset);
    data.SetTrackLen(clip->GetSequenceEndTime() - offset, 1.0 / clip->GetRate());
    auto* curve = data.FindCurve(instanceId, parameterId);
    if (points.empty()) {
        if (!curve) {
            return true;
        }
        // Removed, not overridden: no points left for Re-enable to bring back.
        curve->enabled = false;
        curve->bezier = {};
        curve->envelope.Clear();
    } else {
        if (!curve) {
            curve = data.AddCurve(instanceId, effectId, parameterId, std::clamp(points.front().yValue, 0.0, 1.0));
        }
        if (!curve) {
            return false;
        }
        curve->enabled = true;
        curve->bezier = {};   // the plain envelope is authoritative when there is no curved form
        curve->envelope.Clear();
        // Kept inside the clip: its start in project seconds can round to a
        // hair before the sequence start, and the engine refuses a negative time.
        insertClipLinePoints(curve->envelope, points, offset, clip->GetSequenceEndTime() - offset);
    }
    clip->GetParameterAutomation() = std::move(data);

    // Playing now: hand the effect its new lines, as the automation lane does.
    if (const auto instance = state->GetInstance()) {
        try {
            instance->ConfigureParameterAutomation(std::make_shared<const ParameterAutomationSnapshot>(
                                                       BuildTrackParameterAutomation(*waveTrack, instanceId, effectId)));
        } catch (const std::exception& e) {
            LOGW() << "live transition automation update skipped: " << e.what();
        }
    }
    return true;
}

namespace {
//! A line the user can see: a removed transition line keeps its binding but no points.
bool HasLine(const ClipParameterAutomation::Curve& curve)
{
    return curve.envelope.GetNumberOfPoints() > 0 || !curve.bezier.segments.empty();
}
}

std::optional<bool> Au3ClipGainInteraction::pluginAutomationActive(trackedit::TrackId trackId,
                                                                   const std::string& instanceId,
                                                                   const std::string& parameterId) const
{
    const Au3WaveTrack* waveTrack = DomAccessor::findWaveTrack(projectRef(), Au3TrackId(trackId));
    if (!waveTrack) {
        return std::nullopt;
    }
    std::optional<bool> active;
    for (const std::shared_ptr<const Au3WaveClip>& clip : waveTrack->Intervals()) {
        for (const auto& curve : clip->GetParameterAutomation().Curves()) {
            if (curve.effectInstanceId == instanceId && curve.parameterId == parameterId && HasLine(curve)) {
                active = active.value_or(false) || curve.enabled;
            }
        }
    }
    return active;
}

std::map<std::string, bool> Au3ClipGainInteraction::pluginAutomationStates(trackedit::TrackId trackId,
                                                                           const std::string& instanceId) const
{
    std::map<std::string, bool> states;
    const Au3WaveTrack* waveTrack = DomAccessor::findWaveTrack(projectRef(), Au3TrackId(trackId));
    if (!waveTrack) {
        return states;
    }
    for (const std::shared_ptr<const Au3WaveClip>& clip : waveTrack->Intervals()) {
        for (const auto& curve : clip->GetParameterAutomation().Curves()) {
            if (curve.effectInstanceId == instanceId && HasLine(curve)) {
                states[curve.parameterId] = states[curve.parameterId] || curve.enabled;
            }
        }
    }
    return states;
}

bool Au3ClipGainInteraction::hasOverriddenPluginAutomation() const
{
    for (const Au3WaveTrack* track : Au3TrackList::Get(projectRef()).Any<const Au3WaveTrack>()) {
        for (const std::shared_ptr<const Au3WaveClip>& clip : track->Intervals()) {
            for (const auto& curve : clip->GetParameterAutomation().Curves()) {
                if (!curve.enabled && HasLine(curve) && curve.effectInstanceId != clipautomation::kMixtapeBuiltinInstanceId) {
                    return true;
                }
            }
        }
    }
    return false;
}

bool Au3ClipGainInteraction::reenableAllPluginAutomation()
{
    // Every (track, instance, parameter) with an overridden line.
    std::vector<std::tuple<trackedit::TrackId, std::string, std::string>> overridden;
    for (const Au3WaveTrack* track : Au3TrackList::Get(projectRef()).Any<const Au3WaveTrack>()) {
        for (const std::shared_ptr<const Au3WaveClip>& clip : track->Intervals()) {
            for (const auto& curve : clip->GetParameterAutomation().Curves()) {
                if (!curve.enabled && HasLine(curve) && curve.effectInstanceId != clipautomation::kMixtapeBuiltinInstanceId) {
                    overridden.emplace_back(track->GetId(), curve.effectInstanceId, curve.parameterId);
                }
            }
        }
    }
    bool changed = false;
    for (const auto& [trackId, instanceId, parameterId] : overridden) {
        changed = setPluginAutomationActive(trackId, instanceId, parameterId, true) || changed;
    }
    return changed;
}

bool Au3ClipGainInteraction::setPluginAutomationActive(trackedit::TrackId trackId, const std::string& instanceId,
                                                       const std::string& parameterId, bool active)
{
    Au3WaveTrack* waveTrack = DomAccessor::findWaveTrack(projectRef(), Au3TrackId(trackId));
    if (!waveTrack) {
        return false;
    }
    bool changed = false;
    for (const std::shared_ptr<Au3WaveClip>& clip : waveTrack->Intervals()) {
        auto data = clip->GetParameterAutomation();
        auto* curve = data.FindCurve(instanceId, parameterId);
        if (!curve || curve->enabled == active || !HasLine(*curve)) {
            continue;
        }
        curve->enabled = active;
        clip->GetParameterAutomation() = std::move(data);
        changed = true;
    }
    if (!changed) {
        return false;
    }
    reconfigureEffect(*waveTrack, instanceId);
    return true;
}

void Au3ClipGainInteraction::reconfigureEffect(Au3WaveTrack& waveTrack, const std::string& instanceId)
{
    // Hand the effect its lines now, so the change is heard at once.
    auto& effects = RealtimeEffectList::Get(waveTrack);
    for (size_t i = 0; i < effects.GetStatesCount(); ++i) {
        const auto state = effects.GetStateAt(i);
        if (!state || state->GetInstanceId() != instanceId) {
            continue;
        }
        if (const auto instance = state->GetInstance()) {
            try {
                instance->ConfigureParameterAutomation(std::make_shared<const ParameterAutomationSnapshot>(
                                                           BuildTrackParameterAutomation(waveTrack, instanceId,
                                                                                         state->GetID().ToStdString(wxConvUTF8))));
            } catch (const std::exception& e) {
                LOGW() << "live automation update skipped: " << e.what();
            }
        }
        break;
    }
}

double Au3ClipGainInteraction::recordPluginAutomation(trackedit::TrackId trackId, const std::string& instanceId,
                                                      const std::string& pluginId, const std::string& parameterId,
                                                      double time, double previousTime, double value, bool stepped)
{
    Au3WaveTrack* waveTrack = DomAccessor::findWaveTrack(projectRef(), Au3TrackId(trackId));
    if (!waveTrack) {
        return -1.0;
    }
    for (const std::shared_ptr<Au3WaveClip>& clip : waveTrack->Intervals()) {
        if (time < clip->GetPlayStartTime() || time >= clip->GetPlayEndTime()) {
            continue;
        }
        auto data = clip->GetParameterAutomation();
        const double offset = clip->GetSequenceStartTime();
        const double length = clip->GetSequenceEndTime() - offset;
        data.SetOffset(offset);
        data.SetTrackLen(length, 1.0 / clip->GetRate());
        value = std::clamp(value, 0.0, 1.0);
        auto* curve = data.FindCurve(instanceId, parameterId);
        if (!curve) {
            curve = data.AddCurve(instanceId, pluginId, parameterId, value);
            if (!curve) {
                return -1.0;
            }
        }
        // Record into the plain envelope: a curved line becomes its points first.
        if (!curve->bezier.segments.empty()) {
            const auto points = curve->bezier.Linearize();
            curve->envelope.Clear();
            for (const auto& p : points) {
                curve->envelope.InsertOrReplace(p.timeOffset, p.value);
            }
            curve->bezier = {};
        }
        // While a pass records, the line doesn't play: the hand drives the
        // sound (and the value read back), not what was just written.
        curve->enabled = false;
        curve->stepped = stepped;
        const double t = std::clamp(time - offset, 0.0, length);
        const double from = previousTime < 0.0 ? t : std::clamp(previousTime - offset, 0.0, length);
        // Replace what this pass has swept over since its last write.
        for (int i = int(curve->envelope.GetNumberOfPoints()) - 1; i >= 0; --i) {
            const double pt = curve->envelope[i].GetT();
            if (pt > from + 1e-9 && pt <= t + 1e-9) {
                curve->envelope.Delete(i);
            }
        }
        if (previousTime < 0.0 && curve->envelope.GetNumberOfPoints() > 0) {
            // First write of a pass: keep the old value up to here, then the new one.
            curve->envelope.InsertOrReplace(std::max(0.0, t - 0.001), curve->envelope.GetValue(std::max(0.0, t - 0.001) + offset));
        }
        curve->envelope.InsertOrReplace(t, value);
        clip->GetParameterAutomation() = std::move(data);
        reconfigureEffect(*waveTrack, instanceId);
        return time;
    }
    return -1.0;
}

bool Au3ClipGainInteraction::deletePluginAutomation(trackedit::TrackId trackId, const std::string& instanceId,
                                                    const std::string& parameterId)
{
    Au3WaveTrack* waveTrack = DomAccessor::findWaveTrack(projectRef(), Au3TrackId(trackId));
    if (!waveTrack) {
        return false;
    }
    bool changed = false;
    for (const std::shared_ptr<Au3WaveClip>& clip : waveTrack->Intervals()) {
        auto data = clip->GetParameterAutomation();
        auto* curve = data.FindCurve(instanceId, parameterId);
        if (!curve || !HasLine(*curve)) {
            continue;
        }
        curve->enabled = false;
        curve->bezier = {};
        curve->envelope.Clear();
        clip->GetParameterAutomation() = std::move(data);
        changed = true;
    }
    if (changed) {
        reconfigureEffect(*waveTrack, instanceId);
    }
    return changed;
}

AutomationSegments Au3ClipGainInteraction::clipGainSegments(const trackedit::ClipKey& clipKey) const
{
    Au3WaveTrack* waveTrack = DomAccessor::findWaveTrack(projectRef(), Au3TrackId(clipKey.trackId));
    if (!waveTrack) {
        return {};
    }
    const auto clip = DomAccessor::findWaveClip(waveTrack, clipKey.itemId);
    if (!clip) {
        return {};
    }

    auto& env = clip->GetEnvelope();
    syncEnvelopeOffset(clip, env);
    std::vector<EnvPoint> points;
    const double start = clip->GetPlayStartTime();
    const double end = clip->GetPlayEndTime();
    const double offset = env.GetOffset();
    points.emplace_back(start - offset, env.GetValue(start));
    for (size_t i = 0; i < env.GetNumberOfPoints(); ++i) {
        const auto& point = env[static_cast<int>(i)];
        const double absolute = offset + point.GetT();
        if (absolute > start && absolute < end) {
            points.push_back(point);
        } else if (absolute == start) {
            points.front() = point;
        }
    }
    EnvPoint last(end - offset, env.GetValue(end));
    if (env.GetNumberOfPoints() && offset + env[static_cast<int>(env.GetNumberOfPoints() - 1)].GetT() == end) {
        last = env[static_cast<int>(env.GetNumberOfPoints() - 1)];
    }
    points.push_back(last);

    AutomationSegments result;
    result.reserve(points.size() - 1);
    for (size_t i = 0; i + 1 < points.size(); ++i) {
        // GlideMix: a level held flat turns into an S-curve once a point on it moves.
        const bool flat = points[i].GetVal() == points[i + 1].GetVal() && points[i].GetCurvature() == 0.0f;
        result.push_back({{offset + points[i].GetT(), points[i].GetVal()},
            {offset + points[i + 1].GetT(), points[i + 1].GetVal()}, points[i].GetCurvature(),
            points[i].IsEased() || flat});
    }
    return result;
}

bool Au3ClipGainInteraction::setClipGainSegments(
    const trackedit::ClipKey& clipKey, const AutomationSegments& segments, bool completed)
{
    Au3WaveTrack* waveTrack = DomAccessor::findWaveTrack(projectRef(), Au3TrackId(clipKey.trackId));
    IF_ASSERT_FAILED(waveTrack) { return false; }
    const auto clip = DomAccessor::findWaveClip(waveTrack, clipKey.itemId);
    IF_ASSERT_FAILED(clip) { return false; }
    if (segments.empty()) { return false; }

    auto& env = clip->GetEnvelope();
    syncEnvelopeOffset(clip, env);
    const double start = clip->GetPlayStartTime();
    const double end = clip->GetPlayEndTime();
    for (size_t i = 0; i < segments.size(); ++i) {
        const auto& s = segments[i];
        if (!std::isfinite(s.start.xValue) || !std::isfinite(s.end.xValue)
            || !std::isfinite(s.start.yValue) || !std::isfinite(s.end.yValue)
            || !std::isfinite(s.curvature) || std::abs(s.curvature) > 1.0f
            // A hair of tolerance: after a clip moves, its line's ends can differ
            // from the clip's by rounding (GlideMix's chain moves refused here).
            || s.start.xValue < start - 1e-6 || s.end.xValue > end + 1e-6 || s.start.xValue > s.end.xValue
            || s.start.yValue < env.GetMinValue() || s.start.yValue > env.GetMaxValue()
            || s.end.yValue < env.GetMinValue() || s.end.yValue > env.GetMaxValue()
            || (i && (s.start.xValue != segments[i - 1].end.xValue
                      || s.start.yValue != segments[i - 1].end.yValue))) {
            return false;
        }
    }

    env.Clear();
    const double offset = env.GetOffset();
    for (const auto& s : segments) {
        env.Insert(static_cast<int>(env.GetNumberOfPoints()),
            EnvPoint{s.start.xValue - offset, s.start.yValue, s.curvature, s.eased && s.curvature == 0.0f});
    }
    const auto& last = segments.back().end;
    env.Insert(static_cast<int>(env.GetNumberOfPoints()), EnvPoint{last.xValue - offset, last.yValue});

    if (auto prj = globalContext()->currentTrackeditProject()) {
        prj->notifyAboutClipChanged(DomConverter::clip(waveTrack, clip.get()));
    }
    if (completed) {
        projectHistory()->pushHistoryState(muse::trc("trackedit", "Curved volume automation"),
            muse::trc("trackedit", "Clip envelope edit"));
    }
    m_clipEnvelopeChanged.send(clipKey, completed);
    return true;
}

bool Au3ClipGainInteraction::setClipGainPoint(const trackedit::ClipKey& clipKey, double time, double value, bool completed)
{
    Au3WaveTrack* waveTrack = DomAccessor::findWaveTrack(projectRef(), Au3TrackId(clipKey.trackId));
    IF_ASSERT_FAILED(waveTrack) {
        return false;
    }

    std::shared_ptr<Au3WaveClip> clip = DomAccessor::findWaveClip(waveTrack, clipKey.itemId);
    IF_ASSERT_FAILED(clip) {
        return false;
    }

    auto& env = clip->GetEnvelope();
    syncEnvelopeOffset(clip, env);

    const double clampedTime = std::clamp(time, clip->GetPlayStartTime(), clip->GetPlayEndTime());
    const double v = std::clamp(value, env.GetMinValue(), env.GetMaxValue());
    env.InsertOrReplace(clampedTime, v);

    if (auto prj = globalContext()->currentTrackeditProject()) {
        prj->notifyAboutClipChanged(DomConverter::clip(waveTrack, clip.get()));
    }

    if (completed) {
        projectHistory()->pushHistoryState(muse::trc("trackedit", "Added enveloped point"), muse::trc("trackedit",
                                                                                                      "Clip envelope edit"));
    }

    m_clipEnvelopeChanged.send(clipKey, completed);
    return true;
}

bool Au3ClipGainInteraction::removeClipGainPoint(const trackedit::ClipKey& clipKey, int index, bool completed)
{
    Au3WaveTrack* waveTrack = DomAccessor::findWaveTrack(projectRef(), Au3TrackId(clipKey.trackId));
    IF_ASSERT_FAILED(waveTrack) {
        return false;
    }

    std::shared_ptr<Au3WaveClip> clip = DomAccessor::findWaveClip(waveTrack, clipKey.itemId);
    IF_ASSERT_FAILED(clip) {
        return false;
    }

    auto& env = clip->GetEnvelope();
    syncEnvelopeOffset(clip, env);
    if (index < 0 || index >= int(env.GetNumberOfPoints())) {
        return false;
    }

    env.Delete(index);

    if (auto prj = globalContext()->currentTrackeditProject()) {
        prj->notifyAboutClipChanged(DomConverter::clip(waveTrack, clip.get()));
    }

    if (completed) {
        projectHistory()->pushHistoryState(muse::trc("trackedit", "Removed enveloped point"), muse::trc("trackedit",
                                                                                                        "Clip envelope edit"));
    }

    m_clipEnvelopeChanged.send(clipKey, completed);
    return true;
}

bool Au3ClipGainInteraction::setClipGainPointAtIndex(
    const trackedit::ClipKey& key, int index, double time, double value, bool completed)
{
    const auto points = clipGainPoints(key);
    if (index < 0 || index >= static_cast<int>(points.size())) {
        return false;
    }

    const double oldTime = points.at(index).xValue;
    if (muse::is_equal(oldTime, time)) {
        return setClipGainPoint(key, time, value, completed);
    }

    return setClipGainPoint(key, time, value, completed);
}

bool Au3ClipGainInteraction::beginClipGainPointDrag(const trackedit::ClipKey& clipKey, int pointIndex)
{
    Au3WaveTrack* waveTrack = DomAccessor::findWaveTrack(projectRef(), Au3TrackId(clipKey.trackId));
    IF_ASSERT_FAILED(waveTrack) {
        return false;
    }

    std::shared_ptr<Au3WaveClip> clip = DomAccessor::findWaveClip(waveTrack, clipKey.itemId);
    IF_ASSERT_FAILED(clip) {
        return false;
    }

    auto& env = clip->GetEnvelope();
    syncEnvelopeOffset(clip, env);

    const int n = static_cast<int>(env.GetNumberOfPoints());
    if (pointIndex < 0 || pointIndex >= n) {
        return false;
    }

    if (m_envDrag && m_envDrag->active) {
        endClipGainPointDrag(m_envDrag->clip, true);
    }

    AutomationDragSession s;
    s.clip = clipKey;
    s.originalIndex = pointIndex;
    s.originalXValue = env[pointIndex].GetT();
    s.originalYValue = env[pointIndex].GetVal();
    s.active = true;

    s.currentDragIndex = pointIndex;
    s.lastConsumedLeft = pointIndex;
    s.lastConsumedRight = pointIndex;
    s.originalPoints.reserve(n);
    for (int i = 0; i < n; ++i) {
        const auto& p = env[i];
        s.originalPoints.push_back({ p.GetT(), p.GetVal() });
    }

    env.SetDragPoint(pointIndex);

    m_envDrag = s;
    return true;
}

bool Au3ClipGainInteraction::updateClipGainPointDrag(const trackedit::ClipKey& clipKey, double tAbs, double value)
{
    if (!m_envDrag || !m_envDrag->active || !(m_envDrag->clip == clipKey)) {
        return false;
    }

    Au3WaveTrack* waveTrack = DomAccessor::findWaveTrack(projectRef(), Au3TrackId(clipKey.trackId));
    IF_ASSERT_FAILED(waveTrack) {
        return false;
    }

    std::shared_ptr<Au3WaveClip> clip = DomAccessor::findWaveClip(waveTrack, clipKey.itemId);
    IF_ASSERT_FAILED(clip) {
        return false;
    }

    auto& env = clip->GetEnvelope();
    syncEnvelopeOffset(clip, env);
    const double clampedTAbs = std::clamp(tAbs, clip->GetPlayStartTime(), clip->GetPlayEndTime());
    const double clampedValue = std::clamp(value, env.GetMinValue(), env.GetMaxValue());
    applyDragWithCrossingRemoval(env, *m_envDrag, clampedTAbs - env.GetOffset(), clampedValue);

    if (auto prj = globalContext()->currentTrackeditProject()) {
        prj->notifyAboutClipChanged(DomConverter::clip(waveTrack, clip.get()));
    }

    m_clipEnvelopeChanged.send(clipKey, false);
    return true;
}

bool Au3ClipGainInteraction::endClipGainPointDrag(const trackedit::ClipKey& clipKey, bool commit)
{
    if (!m_envDrag || !m_envDrag->active || !(m_envDrag->clip == clipKey)) {
        return false;
    }

    Au3WaveTrack* waveTrack = DomAccessor::findWaveTrack(projectRef(), Au3TrackId(clipKey.trackId));
    IF_ASSERT_FAILED(waveTrack) {
        return false;
    }

    std::shared_ptr<Au3WaveClip> clip = DomAccessor::findWaveClip(waveTrack, clipKey.itemId);
    IF_ASSERT_FAILED(clip) {
        return false;
    }

    auto& env = clip->GetEnvelope();
    syncEnvelopeOffset(clip, env);

    if (commit) {
        env.ClearDragPoint();
        projectHistory()->pushHistoryState(muse::trc("trackedit", "Dragged enveloped point"), muse::trc("trackedit",
                                                                                                        "Clip envelope edit"));
    } else {
        restoreEnvelopeFromSnapshot(env, *m_envDrag);
        env.ClearDragPoint();
    }

    if (auto prj = globalContext()->currentTrackeditProject()) {
        prj->notifyAboutClipChanged(DomConverter::clip(waveTrack, clip.get()));
    }

    m_clipEnvelopeChanged.send(clipKey, true);
    m_envDrag.reset();

    return true;
}

muse::async::Channel<au::trackedit::ClipKey, bool> Au3ClipGainInteraction::clipGainChanged() const
{
    return m_clipEnvelopeChanged;
}

au::au3::Au3Project& Au3ClipGainInteraction::projectRef() const
{
    Au3Project* project = reinterpret_cast<Au3Project*>(globalContext()->currentProject()->au3ProjectPtr());
    return *project;
}
