/*
 * GlideMix
 */
#include "mixhousetransitionconfiguration.h"
#include "mixhouse/glidemixpaths.h"

#include <algorithm>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include "global/settings.h"

using namespace au::mixhouse;

namespace {
//! Where your transitions were kept before they moved to their own file. Read
//! once, to carry them over, and never written again.
const muse::Settings::Key TRANSITIONS_USER("mixhouse", "mixhouse/transitions/user");
const muse::Settings::Key TRANSITIONS_MIX_NOW("mixhouse", "mixhouse/transitions/mixNow");
const muse::Settings::Key TRANSITIONS_MIX_NOW_LANDING("mixhouse", "mixhouse/transitions/mixNowLanding");

// Stored by name, so reordering the enum never changes what a saved transition means.
const char* transitionCurveName(MixHouseCurveKind kind)
{
    switch (kind) {
    case MixHouseCurveKind::Crossfade: return "crossfade";
    case MixHouseCurveKind::FadeIn: return "fadeIn";
    case MixHouseCurveKind::FadeOut: return "fadeOut";
    case MixHouseCurveKind::Swap: return "swap";
    case MixHouseCurveKind::None: break;
    }
    return "none";
}

MixHouseCurveKind transitionCurveFromName(const QString& name)
{
    for (const MixHouseCurveKind kind : { MixHouseCurveKind::Crossfade, MixHouseCurveKind::FadeIn,
                                          MixHouseCurveKind::FadeOut, MixHouseCurveKind::Swap }) {
        if (name == QLatin1String(transitionCurveName(kind))) {
            return kind;
        }
    }
    return MixHouseCurveKind::None;
}

const char* fadeShapeName(MixHouseFadeShape shape)
{
    switch (shape) {
    case MixHouseFadeShape::Linear: return "linear";
    case MixHouseFadeShape::SCurve: return "sCurve";
    case MixHouseFadeShape::Early: return "early";
    case MixHouseFadeShape::Late: return "late";
    case MixHouseFadeShape::EqualPower: break;
    }
    return "equalPower";
}

MixHouseFadeShape fadeShapeFromName(const QString& name)
{
    for (const MixHouseFadeShape shape : { MixHouseFadeShape::Linear, MixHouseFadeShape::SCurve,
                                           MixHouseFadeShape::Early, MixHouseFadeShape::Late }) {
        if (name == QLatin1String(fadeShapeName(shape))) {
            return shape;
        }
    }
    return MixHouseFadeShape::EqualPower;
}

// The effect lanes, also by name.
const char* filterSweepName(MixHouseFilterSweep sweep)
{
    switch (sweep) {
    case MixHouseFilterSweep::OutHighPass: return "outHighPass";
    case MixHouseFilterSweep::OutLowPass: return "outLowPass";
    case MixHouseFilterSweep::InHighPass: return "inHighPass";
    case MixHouseFilterSweep::InLowPass: return "inLowPass";
    case MixHouseFilterSweep::None: break;
    }
    return "none";
}

MixHouseFilterSweep filterSweepFromName(const QString& name)
{
    for (const MixHouseFilterSweep sweep : { MixHouseFilterSweep::OutHighPass, MixHouseFilterSweep::OutLowPass,
                                             MixHouseFilterSweep::InHighPass, MixHouseFilterSweep::InLowPass }) {
        if (name == QLatin1String(filterSweepName(sweep))) {
            return sweep;
        }
    }
    return MixHouseFilterSweep::None;
}

const char* noiseSweepName(MixHouseNoiseSweep sweep)
{
    switch (sweep) {
    case MixHouseNoiseSweep::Riser: return "riser";
    case MixHouseNoiseSweep::Downsweep: return "downsweep";
    case MixHouseNoiseSweep::None: break;
    }
    return "none";
}

MixHouseNoiseSweep noiseSweepFromName(const QString& name)
{
    for (const MixHouseNoiseSweep sweep : { MixHouseNoiseSweep::Riser, MixHouseNoiseSweep::Downsweep }) {
        if (name == QLatin1String(noiseSweepName(sweep))) {
            return sweep;
        }
    }
    return MixHouseNoiseSweep::None;
}

const char* effectOutName(MixHouseEffectOut effect)
{
    switch (effect) {
    case MixHouseEffectOut::Echo: return "echo";
    case MixHouseEffectOut::Reverb: return "reverb";
    case MixHouseEffectOut::None: break;
    }
    return "none";
}

MixHouseEffectOut effectOutFromName(const QString& name)
{
    for (const MixHouseEffectOut effect : { MixHouseEffectOut::Echo, MixHouseEffectOut::Reverb }) {
        if (name == QLatin1String(effectOutName(effect))) {
            return effect;
        }
    }
    return MixHouseEffectOut::None;
}

QJsonObject transitionLaneToJson(const MixHouseTransitionLane& lane)
{
    return QJsonObject {
        { "curve", QLatin1String(transitionCurveName(lane.curve)) },
        { "shape", QLatin1String(fadeShapeName(lane.shape)) },
        { "strength", lane.strength },
        { "crossing", lane.crossing },
        { "leaveOutBeats", lane.leaveOutBeats },
    };
}

MixHouseTransitionLane transitionLaneFromJson(const QJsonObject& json)
{
    MixHouseTransitionLane lane;
    lane.curve = transitionCurveFromName(json.value("curve").toString());
    lane.shape = fadeShapeFromName(json.value("shape").toString());
    lane.strength = json.value("strength").toDouble(1.0);
    lane.crossing = json.value("crossing").toDouble(0.5);
    lane.leaveOutBeats = json.value("leaveOutBeats").toDouble(0.0);
    return lane;
}
}

void MixHouseTransitionConfiguration::init()
{
    muse::settings()->setDefaultValue(TRANSITIONS_USER, muse::Val(std::string("[]")));
    muse::settings()->setDefaultValue(TRANSITIONS_MIX_NOW, muse::Val(std::string(kDefaultMixNowTransition)));
    muse::settings()->setDefaultValue(TRANSITIONS_MIX_NOW_LANDING, muse::Val(static_cast<int>(MixHouseMixNowLanding::NextPhrase)));
}

//! Your transitions are content, like an effect preset, not preferences. Kept as
//! preferences they sat inside the Preferences dialog's transaction, so pressing
//! Save and then Cancel threw the saved transition away. In their own file, Save
//! and Delete take effect when you press them.
//!
//! Next to the library, in the same folder the library uses.
QString MixHouseTransitionConfiguration::transitionsFilePath()
{
    return paths::glidemixDataPath(QStringLiteral("Transitions.json"));
}

namespace {
std::vector<MixHouseTransitionSpec> specsFromJson(const QByteArray& raw)
{
    std::vector<MixHouseTransitionSpec> result;
    const QJsonDocument doc = QJsonDocument::fromJson(raw);
    for (const QJsonValue& value : doc.array()) {
        const QJsonObject json = value.toObject();
        MixHouseTransitionSpec spec;
        spec.name = json.value("name").toString().toStdString();
        spec.lengthBeats = json.value("lengthBeats").toDouble(8.0);
        spec.volume = transitionLaneFromJson(json.value("volume").toObject());
        spec.bass = transitionLaneFromJson(json.value("bass").toObject());
        spec.mid = transitionLaneFromJson(json.value("mid").toObject());
        spec.high = transitionLaneFromJson(json.value("high").toObject());
        spec.filter = filterSweepFromName(json.value("filter").toString());
        spec.noise = noiseSweepFromName(json.value("noise").toString());
        spec.effectOut = effectOutFromName(json.value("effectOut").toString());
        spec.builtIn = false;
        // A hand-edited or damaged entry is dropped rather than trusted.
        if (validateTransitionSpec(spec).empty()) {
            result.push_back(spec);
        }
    }
    return result;
}
}

std::vector<MixHouseTransitionSpec> MixHouseTransitionConfiguration::userSpecs() const
{
    QFile file(transitionsFilePath());
    if (file.open(QIODevice::ReadOnly)) {
        return specsFromJson(file.readAll());
    }

    // No file yet: anything saved the old way, as a preference, is carried over
    // the next time a transition is saved or deleted.
    return specsFromJson(QByteArray::fromStdString(muse::settings()->value(TRANSITIONS_USER).toString()));
}

void MixHouseTransitionConfiguration::storeUserSpecs(const std::vector<MixHouseTransitionSpec>& specs)
{
    QJsonArray array;
    for (const MixHouseTransitionSpec& spec : specs) {
        array.append(QJsonObject {
            { "name", QString::fromStdString(spec.name) },
            { "lengthBeats", spec.lengthBeats },
            { "volume", transitionLaneToJson(spec.volume) },
            { "bass", transitionLaneToJson(spec.bass) },
            { "mid", transitionLaneToJson(spec.mid) },
            { "high", transitionLaneToJson(spec.high) },
            { "filter", QLatin1String(filterSweepName(spec.filter)) },
            { "noise", QLatin1String(noiseSweepName(spec.noise)) },
            { "effectOut", QLatin1String(effectOutName(spec.effectOut)) },
        });
    }

    const QString path = transitionsFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    //! Written whole or not at all, so a crash mid-save cannot leave half a file.
    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(array).toJson(QJsonDocument::Indented));
        file.commit();
    }
}

std::vector<MixHouseTransitionSpec> MixHouseTransitionConfiguration::specs() const
{
    std::vector<MixHouseTransitionSpec> result = builtInTransitionSpecs();
    for (const MixHouseTransitionSpec& spec : userSpecs()) {
        result.push_back(spec);
    }
    return result;
}

std::optional<MixHouseTransitionSpec> MixHouseTransitionConfiguration::spec(const std::string& name) const
{
    for (const MixHouseTransitionSpec& candidate : specs()) {
        if (candidate.name == name) {
            return candidate;
        }
    }
    return std::nullopt;
}

std::string MixHouseTransitionConfiguration::saveSpec(const MixHouseTransitionSpec& spec)
{
    const std::string problem = validateTransitionSpec(spec);
    if (!problem.empty()) {
        return problem;
    }
    for (const MixHouseTransitionSpec& builtIn : builtInTransitionSpecs()) {
        if (builtIn.name == spec.name) {
            return "That name belongs to a built-in transition. Save it under another name.";
        }
    }
    for (const MixHouseTransitionPreset& mixmeister : IMixHouseTransitions::presets()) {
        if (mixmeister.name == spec.name) {
            return "That name belongs to one of MixMeister's transitions. Save it under another name.";
        }
    }
    std::vector<MixHouseTransitionSpec> user = userSpecs();
    MixHouseTransitionSpec stored = spec;
    stored.builtIn = false;
    const auto existing = std::find_if(user.begin(), user.end(), [&](const auto& s) { return s.name == spec.name; });
    if (existing != user.end()) {
        *existing = stored;
    } else {
        user.push_back(stored);
    }
    storeUserSpecs(user);
    m_changed.notify();
    return {};
}

bool MixHouseTransitionConfiguration::removeSpec(const std::string& name)
{
    std::vector<MixHouseTransitionSpec> user = userSpecs();
    const auto it = std::find_if(user.begin(), user.end(), [&](const auto& s) { return s.name == name; });
    if (it == user.end()) {
        return false;
    }
    user.erase(it);
    storeUserSpecs(user);
    // Mix Now must not be left pointing at a transition that no longer exists.
    if (mixNowTransition() == name) {
        setMixNowTransition(kDefaultMixNowTransition);
    }
    m_changed.notify();
    return true;
}

std::string MixHouseTransitionConfiguration::mixNowTransition() const
{
    const std::string name = muse::settings()->value(TRANSITIONS_MIX_NOW).toString();
    //! Empty means Mix Now only pulls the track, with no transition -- a real
    //! choice. A name that no longer resolves is not: your transition was
    //! deleted (which happens at once) while the Mix Now choice pointing at it
    //! is a preference that Cancel can bring back. Fall back rather than apply
    //! nothing.
    if (name.empty() || spec(name)) {
        return name;
    }
    for (const MixHouseTransitionPreset& mixmeister : IMixHouseTransitions::presets()) {
        if (mixmeister.name == name) {
            return name;
        }
    }
    return kDefaultMixNowTransition;
}

void MixHouseTransitionConfiguration::setMixNowTransition(const std::string& name)
{
    if (mixNowTransition() == name) {
        return;
    }
    muse::settings()->setSharedValue(TRANSITIONS_MIX_NOW, muse::Val(name));
    m_changed.notify();
}

MixHouseMixNowLanding MixHouseTransitionConfiguration::mixNowLanding() const
{
    return muse::settings()->value(TRANSITIONS_MIX_NOW_LANDING).toInt() == static_cast<int>(MixHouseMixNowLanding::NextBar)
           ? MixHouseMixNowLanding::NextBar : MixHouseMixNowLanding::NextPhrase;
}

void MixHouseTransitionConfiguration::setMixNowLanding(MixHouseMixNowLanding landing)
{
    if (mixNowLanding() == landing) {
        return;
    }
    muse::settings()->setSharedValue(TRANSITIONS_MIX_NOW_LANDING, muse::Val(static_cast<int>(landing)));
    m_changed.notify();
}

muse::async::Notification MixHouseTransitionConfiguration::changed() const
{
    return m_changed;
}
