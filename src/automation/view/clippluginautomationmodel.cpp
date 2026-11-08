/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <algorithm>
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

namespace {
//! GlideMix (Chris): every line is drawn like the volume line -- the
//! parameter's normal setting sits at 80% of the height, below it cuts and
//! above it boosts. Lines whose normal setting is an end (a send at 0) stay linear.
constexpr double kNormalAt = 0.8;   // Chris: other automation sits at 80%, read as 100%
double toView(double value, double normal)
{
    if (normal >= 1.0) { return kNormalAt * value; }   // a band at full: 100% at 80%, nothing above
    if (!(normal > 0.0 && normal < 1.0)) { return value; }
    return value <= normal ? kNormalAt * value / normal
                           : kNormalAt + (1.0 - kNormalAt) * (value - normal) / (1.0 - normal);
}
double fromView(double y, double normal)
{
    y = std::clamp(y, 0.0, 1.0);
    if (normal >= 1.0) { return std::min(1.0, y / kNormalAt); }
    if (!(normal > 0.0 && normal < 1.0)) { return y; }
    return y <= kNormalAt ? y / kNormalAt * normal
                          : normal + (y - kNormalAt) / (1.0 - kNormalAt) * (1.0 - normal);
}
}

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
    if (isColor()) { return true; }   // the filter's own lines (automated() below)
    // A deleted line (no points) isn't overridden: it's simply not there.
    return !automated() || clip->GetParameterAutomation().FindCurve(f.instanceId, f.parameterId)->enabled;
}

bool ClipPluginAutomationModel::automated() const
{
    const auto project = globalContext()->currentProject();
    const auto clip = Find(project, m_key);
    if (!clip || !visible()) { return false; }
    const auto f = spec();
    if (isColor()) {
        // Color is the filter's low-pass and high-pass lines: drawn solid once either exists.
        const std::string filter = filterEffectId();
        return !filter.empty() && clipGainInteraction()
               && (!clipGainInteraction()->clipEffectPoints(m_key.key, filter, "lpf").empty()
                   || !clipGainInteraction()->clipEffectPoints(m_key.key, filter, "hpf").empty());
    }
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
    if (isColor()) { return {}; }   // Color already is the filter's two lines
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
        const double normal = curve.envelope.GetDefaultValue();
        QVariantList segments;
        for (const auto& s : bezier.segments) {
            segments.push_back(QVariantMap{{"startTime", s.startPoint.timeOffset}, {"startValue", toView(s.startPoint.value, normal)},
                {"endTime", s.endPoint.timeOffset}, {"endValue", toView(s.endPoint.value, normal)}, {"curvature", s.curvature},
                {"eased", s.eased}});
        }
        out.push_back(QVariantMap{{"segments", segments},
            {"color", QString::fromStdString(projectscene::IProjectViewState::pluginParameterColor(curve.effectInstanceId,
                                                                                                 curve.parameterId))},
            {"stepped", curve.stepped}, {"active", curve.enabled}});
    }
    return out;
}

namespace {
constexpr const char* kColorInstance = "glidemix-color";
// One Color value from the filter's two: 0..0.5 low-pass closing, 0.5..1 high-pass opening.
double combineColor(double lpf, double hpf)
{
    return lpf < 0.999 ? 0.5 * std::clamp(lpf, 0.0, 1.0) : std::clamp(0.5 + 0.5 * hpf, 0.0, 1.0);
}
double lineAt(const au::automation::AutomationPoints& points, double t, double fallback)
{
    if (points.empty()) { return fallback; }
    if (t <= points.front().xValue) { return points.front().yValue; }
    for (size_t i = 1; i < points.size(); ++i) {
        if (t <= points[i].xValue) {
            const double span = points[i].xValue - points[i - 1].xValue;
            const double u = span > 0.0 ? (t - points[i - 1].xValue) / span : 1.0;
            return points[i - 1].yValue + u * (points[i].yValue - points[i - 1].yValue);
        }
    }
    return points.back().yValue;
}
}

bool ClipPluginAutomationModel::isColor() const
{
    return spec().instanceId == kColorInstance;
}

std::string ClipPluginAutomationModel::filterEffectId() const
{
    return mixxxEffectIds() ? mixxxEffectIds()->effectIdForMixxxId("org.mixxx.effects.filter").toStdString() : std::string();
}

QVariantList ClipPluginAutomationModel::colorSegments() const
{
    const auto project = globalContext()->currentProject();
    const auto clip = Find(project, m_key);
    if (!clip || !clipGainInteraction()) { return {}; }
    const double seq = clip->GetSequenceStartTime();
    const double len = clip->GetSequenceEndTime() - seq;
    const std::string filter = filterEffectId();
    const auto lpf = filter.empty() ? au::automation::AutomationPoints {} : clipGainInteraction()->clipEffectPoints(m_key.key, filter, "lpf");
    const auto hpf = filter.empty() ? au::automation::AutomationPoints {} : clipGainInteraction()->clipEffectPoints(m_key.key, filter, "hpf");
    std::vector<double> times { 0.0, len };
    for (const auto* line : { &lpf, &hpf }) {
        for (const auto& p : *line) {
            const double t = p.xValue - seq;
            if (t > 0.0 && t < len) { times.push_back(t); }
        }
    }
    std::sort(times.begin(), times.end());
    times.erase(std::unique(times.begin(), times.end(), [](double a, double b) { return std::abs(a - b) < 1e-6; }), times.end());
    QVariantList out;
    for (size_t i = 0; i + 1 < times.size(); ++i) {
        const double a = times[i], b = times[i + 1];
        const double va = combineColor(lineAt(lpf, a + seq, 1.0), lineAt(hpf, a + seq, 0.0));
        const double vb = combineColor(lineAt(lpf, b + seq, 1.0), lineAt(hpf, b + seq, 0.0));
        out.push_back(QVariantMap{{"startTime", a}, {"startValue", toView(va, 0.5)}, {"endTime", b},
            {"endValue", toView(vb, 0.5)}, {"curvature", 0.0}, {"eased", false}});
    }
    return out;
}

bool ClipPluginAutomationModel::commitColor(const QVariantList& segments)
{
    const auto project = globalContext()->currentProject();
    const auto clip = Find(project, m_key);
    const std::string filter = filterEffectId();
    if (!clip || !clipGainInteraction() || filter.empty() || segments.isEmpty()) { return false; }
    const double seq = clip->GetSequenceStartTime();
    au::automation::AutomationPoints lpf, hpf;
    auto add = [&](double t, double view) {
        const double c = fromView(view, 0.5);
        lpf.push_back({ seq + t, std::min(1.0, 2.0 * c) });
        hpf.push_back({ seq + t, std::max(0.0, 2.0 * c - 1.0) });
    };
    for (int i = 0; i < segments.size(); ++i) {
        const auto s = segments[i].toMap();
        add(s.value("startTime").toDouble(), s.value("startValue").toDouble());
        if (i + 1 == segments.size()) { add(s.value("endTime").toDouble(), s.value("endValue").toDouble()); }
    }
    // The filter effect is added to the track when the line first leaves "open".
    const bool open = std::all_of(lpf.begin(), lpf.end(), [](const auto& p) { return p.yValue > 0.999; })
                      && std::all_of(hpf.begin(), hpf.end(), [](const auto& p) { return p.yValue < 0.001; });
    clipGainInteraction()->setClipEffectPoints(m_key.key, filter, "lpf", lpf, !open);
    clipGainInteraction()->setClipEffectPoints(m_key.key, filter, "hpf", hpf, !open);
    projectHistory()->pushHistoryState("Edited the Color line", "Color line");
    emit changed();
    return true;
}

QVariantList ClipPluginAutomationModel::segments() const
{
    if (isColor()) { return colorSegments(); }
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
        out.push_back(QVariantMap{{"startTime", s.startPoint.timeOffset}, {"startValue", toView(s.startPoint.value, f.defaultValue)},
            {"endTime", s.endPoint.timeOffset}, {"endValue", toView(s.endPoint.value, f.defaultValue)}, {"curvature", s.curvature},
                {"eased", s.eased}});
    }
    return out;
}
bool ClipPluginAutomationModel::commit(const QVariantList& segments)
{
    if (isColor()) { return commitColor(segments); }
    const auto project = globalContext()->currentProject();
    const auto clip = Find(project, m_key);
    auto* track = FindTrack(project, m_key);
    if (!clip || !track || !visible()) { return false; }
    clipautomation::Curve shape;
    const double normal = spec().defaultValue;
    for (const auto& entry : segments) {
        const auto s = entry.toMap();
        shape.segments.push_back({{s.value("startTime", -1).toDouble(), fromView(s.value("startValue", -1).toDouble(), normal)},
            {s.value("endTime", -1).toDouble(), fromView(s.value("endValue", -1).toDouble(), normal)}, s.value("curvature", 2).toFloat(),
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
