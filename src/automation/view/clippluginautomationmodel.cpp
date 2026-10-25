/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "log.h"
#include "clippluginautomationmodel.h"
#include "au3wrap/internal/domaccessor.h"
#include "au3-wave-track/WaveClip.h"
#include "au3-wave-track/WaveTrack.h"
#include "au3-wave-track/TrackParameterAutomation.h"
#include "au3-realtime-effects/RealtimeEffectList.h"
#include "au3-realtime-effects/RealtimeEffectState.h"

using namespace au::automation;
namespace {
std::shared_ptr<WaveClip> Find(const au::project::IAudacityProjectPtr& project, const au::projectscene::ClipKey& key)
{
    if (!project || !key.isValid()) { return {}; }
    auto& dom = *reinterpret_cast<au::au3::Au3Project*>(project->au3ProjectPtr());
    auto* track = au::au3::DomAccessor::findWaveTrack(dom, au::au3::Au3TrackId(key.trackId()));
    return track ? au::au3::DomAccessor::findWaveClip(track, key.itemId()) : nullptr;
}

WaveTrack* FindTrack(const au::project::IAudacityProjectPtr& project, const au::projectscene::ClipKey& key)
{
    if (!project || !key.isValid()) { return nullptr; }
    auto& dom = *reinterpret_cast<au::au3::Au3Project*>(project->au3ProjectPtr());
    return au::au3::DomAccessor::findWaveTrack(dom, au::au3::Au3TrackId(key.trackId()));
}
}

ClipPluginAutomationModel::ClipPluginAutomationModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this)) {}

void ClipPluginAutomationModel::init()
{
    globalContext()->currentProjectChanged().onNotify(this, [this] { init(); }, muse::async::Asyncable::Mode::SetReplace);
    const auto project = globalContext()->currentProject();
    if (project) {
        project->viewState()->pluginAutomationFocusChanged().onNotify(this, [this] { emit changed(); },
            muse::async::Asyncable::Mode::SetReplace);
    }
    projectHistory()->historyChanged().onReceive(this, [this](auto) { emit changed(); }, muse::async::Asyncable::Mode::SetReplace);
    clipsInteraction()->clipStartTimeChanged().onReceive(this, [this](const auto& key, auto, bool) {
        if (key == m_key) { emit changed(); }
    }, muse::async::Asyncable::Mode::SetReplace);
    // GlideMix: a transition (or anything else) writing this clip's lines
    // without an undo step yet -- redraw now, so no old points stay on screen.
    clipGainInteraction()->clipGainChanged().onReceive(this, [this](const auto& key, bool) {
        if (key == m_key.key) { emit changed(); }
    }, muse::async::Asyncable::Mode::SetReplace);
    emit changed();
}

au::projectscene::IProjectViewState::PluginAutomationFocus ClipPluginAutomationModel::spec() const
{
    if (!m_lane.isEmpty()) {
        projectscene::IProjectViewState::PluginAutomationFocus f;
        f.trackId = m_key.trackId();
        f.instanceId = m_lane.value("instanceId").toString().toStdString();
        f.pluginId = m_lane.value("pluginId").toString().toStdString();
        f.parameterId = m_lane.value("parameterId").toString().toStdString();
        f.label = m_lane.value("label").toString().toStdString();
        f.color = m_lane.value("color").toString().toStdString();
        f.defaultValue = m_lane.value("defaultValue").toDouble();
        f.stepped = m_lane.value("stepped").toBool();
        f.visible = true;
        return f;
    }
    const auto project = globalContext()->currentProject();
    return project ? project->viewState()->pluginAutomationFocus() : projectscene::IProjectViewState::PluginAutomationFocus {};
}

void ClipPluginAutomationModel::showInOwnLane()
{
    const auto project = globalContext()->currentProject();
    const auto f = spec();
    if (!project || f.parameterId.empty() || !m_key.isValid()) { return; }
    const auto vs = project->viewState();
    if (vs->addAutomationLane(m_key.trackId(), f)) {
        vs->changeTrackHeight(m_key.trackId(), projectscene::IProjectViewState::kAutomationLaneHeight);
    }
}

bool ClipPluginAutomationModel::visible() const
{
    const auto project = globalContext()->currentProject();
    if (!project) { return false; }
    const auto f = spec();
    return f.visible && f.trackId == m_key.trackId() && !f.parameterId.empty() && bool(Find(project, m_key));
}
double ClipPluginAutomationModel::sequenceStart() const
{
    const auto clip = Find(globalContext()->currentProject(), m_key);
    return clip ? clip->GetSequenceStartTime() : 0;
}
QString ClipPluginAutomationModel::label() const
{
    const auto p = globalContext()->currentProject();
    return p ? QString::fromStdString(spec().label) : QString();
}
QString ClipPluginAutomationModel::color() const
{
    const auto p = globalContext()->currentProject();
    return p ? QString::fromStdString(spec().color) : QStringLiteral("#F5BB42");
}
bool ClipPluginAutomationModel::active() const
{
    const auto project = globalContext()->currentProject();
    const auto clip = Find(project, m_key);
    if (!clip || !visible()) { return true; }
    const auto f = spec();
    // A deleted line (no points) isn't overridden: it's simply not there.
    return !automated() || clip->GetParameterAutomation().FindCurve(f.instanceId, f.parameterId)->enabled;
}

bool ClipPluginAutomationModel::automated() const
{
    const auto project = globalContext()->currentProject();
    const auto clip = Find(project, m_key);
    if (!clip || !visible()) { return false; }
    const auto f = spec();
    const auto* stored = clip->GetParameterAutomation().FindCurve(f.instanceId, f.parameterId);
    return stored && (stored->envelope.GetNumberOfPoints() > 0 || !stored->bezier.segments.empty());
}

bool ClipPluginAutomationModel::stepped() const
{
    const auto p = globalContext()->currentProject();
    return p && spec().stepped;
}

QVariantList ClipPluginAutomationModel::otherLines() const
{
    const auto project = globalContext()->currentProject();
    const auto clip = Find(project, m_key);
    if (!clip || !visible()) { return {}; }
    const auto f = spec();
    QVariantList out;
    for (const auto& curve : clip->GetParameterAutomation().Curves()) {
        if (curve.effectInstanceId == clipautomation::kMixtapeBuiltinInstanceId
            || (curve.effectInstanceId == f.instanceId && curve.parameterId == f.parameterId)) { continue; }
        if (curve.envelope.GetNumberOfPoints() == 0 && curve.bezier.segments.empty()) { continue; }   // removed line
        const auto bezier = curve.Bezier();
        QVariantList segments;
        for (const auto& s : bezier.segments) {
            segments.push_back(QVariantMap{{"startTime", s.startPoint.timeOffset}, {"startValue", s.startPoint.value},
                {"endTime", s.endPoint.timeOffset}, {"endValue", s.endPoint.value}, {"curvature", s.curvature},
                {"eased", s.eased}});
        }
        out.push_back(QVariantMap{{"segments", segments},
            {"color", QString::fromStdString(projectscene::IProjectViewState::pluginParameterColor(curve.effectInstanceId,
                                                                                                 curve.parameterId))},
            {"stepped", curve.stepped}, {"active", curve.enabled}});
    }
    return out;
}

QVariantList ClipPluginAutomationModel::segments() const
{
    const auto project = globalContext()->currentProject();
    const auto clip = Find(project, m_key);
    if (!clip || !visible()) { return {}; }
    const auto f = spec();
    const auto* stored = clip->GetParameterAutomation().FindCurve(f.instanceId, f.parameterId);
    clipautomation::Curve curve;
    if (stored) { curve = stored->Bezier(); }
    else { curve.segments.push_back({{0, f.defaultValue},
        {clip->GetSequenceEndTime() - clip->GetSequenceStartTime(), f.defaultValue}, 0, true}); }
    QVariantList out;
    for (const auto& s : curve.segments) {
        out.push_back(QVariantMap{{"startTime", s.startPoint.timeOffset}, {"startValue", s.startPoint.value},
            {"endTime", s.endPoint.timeOffset}, {"endValue", s.endPoint.value}, {"curvature", s.curvature},
                {"eased", s.eased}});
    }
    return out;
}
bool ClipPluginAutomationModel::commit(const QVariantList& segments)
{
    const auto project = globalContext()->currentProject();
    const auto clip = Find(project, m_key);
    auto* track = FindTrack(project, m_key);
    if (!clip || !track || !visible()) { return false; }
    clipautomation::Curve shape;
    for (const auto& entry : segments) {
        const auto s = entry.toMap();
        shape.segments.push_back({{s.value("startTime", -1).toDouble(), s.value("startValue", -1).toDouble()},
            {s.value("endTime", -1).toDouble(), s.value("endValue", -1).toDouble()}, s.value("curvature", 2).toFloat(),
            s.value("eased", false).toBool()});
    }
    if (!shape.Valid() || shape.segments.empty()) { return false; }
    const auto f = spec();
    auto data = clip->GetParameterAutomation(); // atomic edit on the UI thread
    data.SetOffset(clip->GetSequenceStartTime());
    data.SetTrackLen(clip->GetSequenceEndTime() - clip->GetSequenceStartTime(), 1.0 / clip->GetRate());
    auto* curve = data.FindCurve(f.instanceId, f.parameterId);
    if (!curve) { curve = data.AddCurve(f.instanceId, f.pluginId, f.parameterId, f.defaultValue); }
    if (!curve) { return false; }
    curve->enabled = true;   // editing an overridden line brings it back, as in Ableton
    curve->stepped = f.stepped;
    try { curve->SetBezier(std::move(shape)); }
    catch (const std::exception& e) { emit editRejected(QString::fromUtf8(e.what())); return false; }
    clip->GetParameterAutomation() = std::move(data);

    // VST3 wrappers publish this immutable snapshot atomically. The audio
    // thread retains one generation for an entire processing block, so edits
    // made during ordinary or loop playback become audible on the next block
    // without locks or access to mutable project data.
    auto& effects = RealtimeEffectList::Get(*track);
    for (size_t i = 0; i < effects.GetStatesCount(); ++i) {
        auto state = effects.GetStateAt(i);
        if (!state || state->GetInstanceId() != f.instanceId) { continue; }
        const auto pluginId = state->GetID().ToStdString(wxConvUTF8);
        // GlideMix: Mixxx effects take live snapshots the same way.
        if (pluginId.rfind("Effect_VST3_", 0) == 0 || pluginId.rfind("Effect_Audacity_Mixxx_", 0) == 0) {
            // GlideMix: a snapshot that can't be built must not end the app;
            // playback picks the curve up when it restarts.
            try {
                auto snapshot = std::make_shared<const ParameterAutomationSnapshot>(
                    BuildTrackParameterAutomation(*track, f.instanceId, pluginId));
                if (const auto instance = state->GetInstance()) {
                    instance->ConfigureParameterAutomation(std::move(snapshot));
                }
            } catch (const std::exception& e) {
                LOGW() << "live plugin automation update skipped: " << e.what();
            }
        }
        break;
    }
    projectHistory()->pushHistoryState("Edited clip plugin automation", "Plugin automation");
    emit changed();
    return true;
}
