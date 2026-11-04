/*
 * Audacity: A Digital Audio Editor
 */
#include "realtimeeffectlistitemmodel.h"

#include "au3-realtime-effects/RealtimeEffectState.h"
#include "au3-components/EffectInterface.h"
#include "au3-effects/EffectPlugin.h"
#include "effects/effects_base/internal/effectsutils.h"

#include "framework/global/log.h"
#include "framework/global/translation.h"

#include <QCoreApplication>

#include <array>
#include <cmath>
#include <functional>

namespace au::projectscene {
RealtimeEffectListItemModel::RealtimeEffectListItemModel(QObject* parent, effects::RealtimeEffectStatePtr effectState)
    : QObject{parent}, muse::Contextable(muse::iocCtxForQmlObject(this)), m_effectState{effectState}
{
    m_gestureTimer.setInterval(16); // UI focus only, never drives DSP values.
    connect(&m_gestureTimer, &QTimer::timeout, this, [this] {
        const auto state = m_effectState.lock();
        const auto project = globalContext()->currentProject();
        if (!state || !project) { return; }
        const auto& focus = project->viewState()->pluginAutomationFocus();
        // GlideMix: never GetInstance() here. With nothing holding the instance it
        // built a new plug-in every 16 ms; a touch needs an open window, which holds one.
        const auto instance = state->ExistingInstance();
        const auto touched = instance ? instance->TakeParameterInteraction() : 0;
        // Ableton: "Clicking on a track's mixer or device controls will
        // display this control's envelope" -- the lane follows the control the
        // user touches in the plugin's window.
        const std::string touchedId = touched ? instance->InteractionParameterId(uint32_t(touched - 1)) : std::string();
        // Shown when the lanes are up: Automation Mode (A), or a lane already open.
        if (touched && (focus.visible || project->viewState()->clipGainAutomationEnabled().val)) {
            focusAutomation(QString::fromStdString(touchedId));
        }
        // Ableton: "If you change an automated control's value while not
        // recording ... Any automation is therefore overridden by the current
        // manual setting" -- until Re-Enable Automation.
        // Armed and playing: the touch writes into the line (Ableton's Automation
        // Arm) instead of overriding it.
        if (touched && project->viewState()->automationArmed() && globalContext()->playbackState()->isPlaying()) {
            recordTouch(touchedId);
        } else if (touched) {
            overrideAutomation(touchedId);
        }
        continueRecording();
        if (++m_gestureTicks % 3 == 0) { followParameterValue(); }
    });
    m_gestureTimer.start();
    const auto watchFocus = [this] {
        if (const auto project = globalContext()->currentProject()) {
            project->viewState()->pluginAutomationFocusChanged().onNotify(this, [this] { emit automationFocusChanged(); },
                muse::async::Asyncable::Mode::SetReplace);
        }
        emit automationFocusChanged();
    };
    globalContext()->currentProjectChanged().onNotify(this, watchFocus, muse::async::Asyncable::Mode::SetReplace);
    watchFocus();
    projectHistory()->historyChanged().onReceive(this, [this](auto) { emit automationFocusChanged(); },
        muse::async::Asyncable::Mode::SetReplace);
    realtimeEffectService()->isActiveChanged().onReceive(this, [this](effects::RealtimeEffectStatePtr state)
    {
        if (state == m_effectState.lock()) {
            emit isActiveChanged();
        }
    });

    realtimeEffectService()->effectSettingsChanged().onNotify(this, [this]
    {
        emit isActiveChanged();
    });
}

bool RealtimeEffectListItemModel::supportsAutomation() const
{
    const auto state = m_effectState.lock();
    if (!state) { return false; }
    const auto meta = effectsProvider()->meta(muse::String::fromStdString(state->GetID().ToStdString()));
    // GlideMix: Mixxx's built-in effects are automatable too.
    return meta.family == effects::EffectFamily::VST3
        || effects::utils::isMixxxDjEffect(meta)
#ifdef Q_OS_MACOS
        || meta.family == effects::EffectFamily::AudioUnit
#endif
        ;
}

QVariantList RealtimeEffectListItemModel::rawAutomationParameters() const
{
    const auto state = m_effectState.lock();
    const auto instance = state ? state->GetInstance() : nullptr;
    if (!instance || !instance->SupportsParameterAutomation() || prop_isMasterEffect()) { return {}; }
    QVariantList result;
    for (const auto& p : instance->AutomationParameters()) {
        result.push_back(QVariantMap{{"id", QString::fromStdString(p.id)}, {"name", QString::fromStdString(p.name)},
            {"value", p.normalizedValue}, {"discrete", p.discrete}});
    }
    if (!result.empty()) { return result; }
    const auto meta = effectsProvider()->meta(muse::String::fromStdString(state->GetID().ToStdString()));
    auto* extractor = parameterExtractors()->extractorForFamily(meta.family);
    if (!extractor) { return {}; }
    auto* extended = dynamic_cast<effects::EffectInstance*>(instance.get());
    if (!extended) { return {}; }
    for (const auto& p : extractor->extractParameters(extended, state->GetAccess())) {
        if (!p.canAutomate || p.isReadOnly || p.isHidden || !p.isValid()) { continue; }
        result.push_back(QVariantMap{{"id", p.id.toQString()}, {"name", p.name.toQString()},
            {"value", p.getNormalizedValue()},
            // GlideMix: steps only for a switch or a short list of choices;
            // a knob with 100 or 127 positions ramps, as in Ableton.
            {"discrete", p.stepCount > 0 && p.stepCount <= 24}});
    }
    return result;
}

// Ableton's choosers: "The labels of automated controls have an LED" -- a
// filled dot for automation that plays, a hollow one when it is overridden.
QVariantList RealtimeEffectListItemModel::automationParameters() const
{
    QVariantList result = rawAutomationParameters();
    const auto state = m_effectState.lock();
    const auto track = state ? realtimeEffectService()->trackId(state) : std::nullopt;
    const auto states = track ? clipGain()->pluginAutomationStates(*track, state->GetInstanceId())
                              : std::map<std::string, bool>{};
    for (auto& entry : result) {
        auto p = entry.toMap();
        const auto it = states.find(p["id"].toString().toStdString());
        const QString name = p["name"].toString();
        p["automated"] = it != states.end();
        p["label"] = it == states.end() ? name
                     : it->second ? QStringLiteral("\u25CF ") + name
                     : QStringLiteral("\u25CB ") + name + muse::qtrc("projectscene", " (overridden)");
        entry = p;
    }
    return result;
}

bool RealtimeEffectListItemModel::focusAutomation(const QString& parameterId)
{
    const auto state = m_effectState.lock();
    const auto project = globalContext()->currentProject();
    const auto track = state ? realtimeEffectService()->trackId(state) : std::nullopt;
    if (!project || !state || !track || prop_isMasterEffect()) { return false; }
    for (const auto& entry : rawAutomationParameters()) {
        const auto p = entry.toMap();
        if (p["id"].toString() != parameterId) { continue; }
        IProjectViewState::PluginAutomationFocus f;
        f.trackId = *track; f.instanceId = state->GetInstanceId(); f.pluginId = state->GetID().ToStdString();
        f.parameterId = parameterId.toStdString(); f.defaultValue = p["value"].toDouble();
        f.stepped = p["discrete"].toBool();
        f.label = effectName().toStdString() + " / " + p["name"].toString().toStdString();
        f.color = parameterColor(f.instanceId, f.parameterId).toStdString(); f.visible = true;
        project->viewState()->setPluginAutomationFocus(std::move(f));
        return true;
    }
    return false;
}

QString RealtimeEffectListItemModel::focusedParameterId() const
{
    const auto state = m_effectState.lock();
    const auto project = globalContext()->currentProject();
    if (!state || !project) { return {}; }
    const auto& f = project->viewState()->pluginAutomationFocus();
    return f.visible && f.instanceId == state->GetInstanceId() ? QString::fromStdString(f.parameterId) : QString();
}

// A parameter without automation (or with its automation overridden) is drawn
// as a flat line at its value; turning the control moves that line with it,
// playing or not. A line that plays is left alone: automation moves the value.
void RealtimeEffectListItemModel::followParameterValue()
{
    const auto state = m_effectState.lock();
    const auto project = globalContext()->currentProject();
    if (!state || !project) { return; }
    const auto& f = project->viewState()->pluginAutomationFocus();
    if (!f.visible || f.instanceId != state->GetInstanceId()) { return; }
    const auto track = realtimeEffectService()->trackId(state);
    if (!track || clipGain()->pluginAutomationActive(*track, f.instanceId, f.parameterId) == true) { return; }
    for (const auto& entry : rawAutomationParameters()) {
        const auto p = entry.toMap();
        if (p["id"].toString().toStdString() != f.parameterId) { continue; }
        const double value = p["value"].toDouble();
        if (std::abs(value - f.defaultValue) > 1e-4) {
            auto moved = f;
            moved.defaultValue = value;
            project->viewState()->setPluginAutomationFocus(std::move(moved));
        }
        return;
    }
}

QString RealtimeEffectListItemModel::parameterColor(const std::string& instanceId, const std::string& parameterId)
{
    return QString::fromStdString(IProjectViewState::pluginParameterColor(instanceId, parameterId));
}

// Ableton's touch recording: from the first move of a control until shortly
// after its last one, its value is written at the playhead every tick.
void RealtimeEffectListItemModel::recordTouch(const std::string& parameterId)
{
    if (m_recordParameter != parameterId) {
        finishRecording();
        m_recordParameter = parameterId;
        m_recordPrevious = -1.0;
    }
    m_recordLastTouch = std::chrono::steady_clock::now();
}

void RealtimeEffectListItemModel::continueRecording()
{
    if (m_recordParameter.empty()) { return; }
    const auto project = globalContext()->currentProject();
    const auto state = m_effectState.lock();
    const auto track = state ? realtimeEffectService()->trackId(state) : std::nullopt;
    const bool playing = globalContext()->playbackState()->isPlaying();
    if (!project || !track || !playing || !project->viewState()->automationArmed()
        || std::chrono::steady_clock::now() - m_recordLastTouch > std::chrono::milliseconds(400)) {
        finishRecording();
        return;
    }
    for (const auto& entry : rawAutomationParameters()) {
        const auto p = entry.toMap();
        if (p["id"].toString().toStdString() != m_recordParameter) { continue; }
        const double time = globalContext()->playbackState()->playbackPosition();
        const double written = clipGain()->recordPluginAutomation(*track, state->GetInstanceId(),
            state->GetID().ToStdString(), m_recordParameter, time, m_recordPrevious, p["value"].toDouble(),
            p["discrete"].toBool());
        if (written >= 0.0) {
            m_recordPrevious = written;
            m_recordWrote = true;
        }
        return;
    }
}

void RealtimeEffectListItemModel::finishRecording()
{
    if (m_recordWrote) {
        const auto state = m_effectState.lock();
        const auto track = state ? realtimeEffectService()->trackId(state) : std::nullopt;
        if (track) {
            clipGain()->setPluginAutomationActive(*track, state->GetInstanceId(), m_recordParameter, true);
        }
        projectHistory()->pushHistoryState("Recorded plugin automation", "Record automation");
        emit automationFocusChanged();
    }
    m_recordParameter.clear();
    m_recordPrevious = -1.0;
    m_recordWrote = false;
}

bool RealtimeEffectListItemModel::automationOverridden() const
{
    const auto state = m_effectState.lock();
    const auto track = state ? realtimeEffectService()->trackId(state) : std::nullopt;
    const auto id = focusedParameterId();
    if (!track || id.isEmpty()) { return false; }
    return clipGain()->pluginAutomationActive(*track, state->GetInstanceId(), id.toStdString()) == false;
}

void RealtimeEffectListItemModel::overrideAutomation(const std::string& parameterId)
{
    const auto state = m_effectState.lock();
    const auto track = state ? realtimeEffectService()->trackId(state) : std::nullopt;
    if (!track || prop_isMasterEffect()) { return; }
    if (clipGain()->pluginAutomationActive(*track, state->GetInstanceId(), parameterId) != true) { return; }
    if (clipGain()->setPluginAutomationActive(*track, state->GetInstanceId(), parameterId, false)) {
        projectHistory()->pushHistoryState("Overrode plugin automation", "Automation overridden");
        emit automationFocusChanged();
    }
}

bool RealtimeEffectListItemModel::automationExists() const
{
    const auto state = m_effectState.lock();
    const auto track = state ? realtimeEffectService()->trackId(state) : std::nullopt;
    const auto id = focusedParameterId();
    return track && !id.isEmpty()
           && clipGain()->pluginAutomationActive(*track, state->GetInstanceId(), id.toStdString()).has_value();
}

void RealtimeEffectListItemModel::deleteAutomation()
{
    const auto state = m_effectState.lock();
    const auto track = state ? realtimeEffectService()->trackId(state) : std::nullopt;
    const auto id = focusedParameterId();
    if (!track || id.isEmpty()) { return; }
    if (clipGain()->deletePluginAutomation(*track, state->GetInstanceId(), id.toStdString())) {
        projectHistory()->pushHistoryState("Deleted plugin automation", "Delete automation");
        emit automationFocusChanged();
    }
}

void RealtimeEffectListItemModel::reenableAutomation()
{
    const auto state = m_effectState.lock();
    const auto track = state ? realtimeEffectService()->trackId(state) : std::nullopt;
    const auto id = focusedParameterId();
    if (!track || id.isEmpty()) { return; }
    if (clipGain()->setPluginAutomationActive(*track, state->GetInstanceId(), id.toStdString(), true)) {
        projectHistory()->pushHistoryState("Re-enabled plugin automation", "Re-enable automation");
        emit automationFocusChanged();
    }
}

void RealtimeEffectListItemModel::hideAutomation()
{
    const auto p = globalContext()->currentProject();
    const auto state = m_effectState.lock();
    if (!p || !state) { return; }
    auto focus = p->viewState()->pluginAutomationFocus();
    if (focus.instanceId == state->GetInstanceId()) {
        focus.visible = false; p->viewState()->setPluginAutomationFocus(std::move(focus));
    }
}

RealtimeEffectListItemModel::~RealtimeEffectListItemModel()
{
    // Undo/redo rebuilds these rows while the effect lives on; hide its lane
    // only if the effect is really gone (checked once the rebuild is done).
    if (const auto state = m_effectState.lock()) {
        const auto project = globalContext()->currentProject();
        const auto track = realtimeEffectService()->trackId(state);
        const auto service = realtimeEffectService().get();
        const std::string instanceId = state->GetInstanceId();
        if (project && track && service) {
            QTimer::singleShot(0, qApp, [project, service, track, instanceId] {
                auto focus = project->viewState()->pluginAutomationFocus();
                if (!focus.visible || focus.instanceId != instanceId) { return; }
                if (const auto stack = service->effectStack(*track)) {
                    for (const auto& s : *stack) {
                        if (s && s->GetInstanceId() == instanceId) { return; }
                    }
                }
                focus.visible = false;
                project->viewState()->setPluginAutomationFocus(std::move(focus));
            });
        }
    }
    // After undo/redo the state may already be destroyed. Then no dialog can
    // be open for it (open dialogs share ownership), so there's nothing to hide.
    if (const auto state = m_effectState.lock()) {
        effectViewController()->hideEffect(state);
    }
}

bool RealtimeEffectListItemModel::prop_isMasterEffect() const
{
    const auto state = m_effectState.lock();
    IF_ASSERT_FAILED(state) {
        return false;
    }
    return realtimeEffectService()->trackId(state) == effects::IRealtimeEffectService::masterTrackId;
}

QString RealtimeEffectListItemModel::prop_automationColor() const
{
    const auto state = m_effectState.lock();
    if (!state) {
        return QStringLiteral("#7C6BE8");
    }
    // Stable per-instance color: the UUID is persisted with the effect state,
    // so reordering or reopening a project does not reshuffle curve colors.
    // Bright, high-fidelity samples from the supplied Logic Pro palette.
    // These stay distinct over every track colour once automation focus dims
    // the waveform behind them.
    static constexpr std::array<const char*, 12> palette {
        "#C93B10", "#C97D11", "#CAA40F", "#A4C90F", "#5AC910", "#0DC92F",
        "#0ECA7F", "#0DC9CA", "#2596CE", "#4644D8", "#8F2ED1", "#C931CA"
    };
    const auto hash = std::hash<std::string>{}(state->GetInstanceId());
    return QString::fromLatin1(palette[hash % palette.size()]);
}

QString RealtimeEffectListItemModel::prop_automationMode() const
{
    const auto state = m_effectState.lock();
    if (!state) { return QStringLiteral("Read"); }
    switch (state->GetAutomationMode()) {
    case RealtimeEffectState::AutomationMode::Touch: return QStringLiteral("Touch");
    case RealtimeEffectState::AutomationMode::Latch: return QStringLiteral("Latch");
    case RealtimeEffectState::AutomationMode::Write: return QStringLiteral("Write");
    case RealtimeEffectState::AutomationMode::Read: return QStringLiteral("Read");
    }
    return QStringLiteral("Read");
}

void RealtimeEffectListItemModel::prop_setAutomationMode(const QString& mode)
{
    const auto state = m_effectState.lock();
    if (!state) { return; }
    RealtimeEffectState::AutomationMode selected = RealtimeEffectState::AutomationMode::Read;
    if (mode == QStringLiteral("Touch")) selected = RealtimeEffectState::AutomationMode::Touch;
    else if (mode == QStringLiteral("Latch")) selected = RealtimeEffectState::AutomationMode::Latch;
    else if (mode == QStringLiteral("Write")) selected = RealtimeEffectState::AutomationMode::Write;
    if (state->GetAutomationMode() == selected) { return; }
    state->SetAutomationMode(selected);
    emit automationModeChanged();
}

bool RealtimeEffectListItemModel::prop_isAvailable() const
{
    return realtimeEffectService()->isAvailable(m_effectState.lock());
}

QString RealtimeEffectListItemModel::effectName() const
{
    const auto state = m_effectState.lock();
    IF_ASSERT_FAILED(state) {
        return QString();
    }

    const auto effectId = state->GetID().ToStdString();
    const auto name = effectsProvider()->effectName(effectId);
    const auto isValid = effectsProvider()->meta(muse::String::fromStdString(effectId)).isValid();

    if (!isValid) {
        //: %1 is the name of the effect that is missing/unavailable
        return muse::qtrc("effects", "Missing - “%1”").arg(name);
    }
    return QString::fromStdString(name);
}

QString RealtimeEffectListItemModel::effectState() const
{
    const auto state = m_effectState.lock();
    IF_ASSERT_FAILED(state) {
        return QString();
    }
    return QString::number(reinterpret_cast<uintptr_t>(state.get()));
}

effects::RealtimeEffectStatePtr RealtimeEffectListItemModel::effectStatePtr() const
{
    return m_effectState.lock();
}

void RealtimeEffectListItemModel::showEffectDialog()
{
    effectViewController()->showEffect(m_effectState.lock());
}

bool RealtimeEffectListItemModel::prop_isActive() const
{
    const auto state = m_effectState.lock();
    return realtimeEffectService()->isActive(state);
}

void RealtimeEffectListItemModel::prop_setIsActive(bool isActive)
{
    const auto state = m_effectState.lock();
    realtimeEffectService()->setIsActive(state, isActive);
}
}
