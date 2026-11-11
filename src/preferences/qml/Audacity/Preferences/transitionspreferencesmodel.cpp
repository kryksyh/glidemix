/*
 * GlideMix
 */
#include "transitionspreferencesmodel.h"

#include <algorithm>
#include <cmath>

#include "mixhouse/imixhousetransitions.h"

using namespace au::preferences;
using namespace au::mixhouse;

namespace {
constexpr int kBeatsPerBar = 4;

QVariantMap laneToVariant(const MixHouseTransitionLane& lane)
{
    return QVariantMap {
        { "curve", static_cast<int>(lane.curve) },
        { "shape", static_cast<int>(lane.shape) },
        { "strength", std::round(lane.strength * 100.0) },
        { "crossing", std::round(lane.crossing * 100.0) },
        { "leaveOutBeats", lane.leaveOutBeats },
    };
}

MixHouseTransitionLane laneFromVariant(const QVariantMap& map)
{
    MixHouseTransitionLane lane;
    const int curve = std::clamp(map.value("curve").toInt(), 0, 4);
    lane.curve = static_cast<MixHouseCurveKind>(curve);
    lane.shape = static_cast<MixHouseFadeShape>(std::clamp(map.value("shape").toInt(), 0, 4));
    lane.strength = map.value("strength", 100).toDouble() / 100.0;
    lane.crossing = map.value("crossing", 50).toDouble() / 100.0;
    lane.leaveOutBeats = map.value("leaveOutBeats", 0).toDouble();
    return lane;
}

QVariantMap specToVariant(const MixHouseTransitionSpec& spec)
{
    return QVariantMap {
        { "name", QString::fromStdString(spec.name) },
        { "builtIn", spec.builtIn },
        { "lengthBars", spec.lengthBeats / kBeatsPerBar },
        { "volume", laneToVariant(spec.volume) },
        { "bass", laneToVariant(spec.bass) },
        { "mid", laneToVariant(spec.mid) },
        { "high", laneToVariant(spec.high) },
        { "filter", static_cast<int>(spec.filter) },
        { "noise", static_cast<int>(spec.noise) },
        { "effectOut", static_cast<int>(spec.effectOut) },
        // Not on the page; carried so a duplicate of Volume Fade keeps its cut.
        { "cutAtEnd", spec.cutAtEnd },
    };
}

MixHouseTransitionSpec specFromVariant(const QVariantMap& map)
{
    MixHouseTransitionSpec spec;
    spec.name = map.value("name").toString().trimmed().toStdString();
    spec.lengthBeats = map.value("lengthBars", 2).toDouble() * kBeatsPerBar;
    spec.volume = laneFromVariant(map.value("volume").toMap());
    spec.bass = laneFromVariant(map.value("bass").toMap());
    spec.mid = laneFromVariant(map.value("mid").toMap());
    spec.high = laneFromVariant(map.value("high").toMap());
    spec.filter = static_cast<MixHouseFilterSweep>(std::clamp(map.value("filter").toInt(), 0, 4));
    spec.noise = static_cast<MixHouseNoiseSweep>(std::clamp(map.value("noise").toInt(), 0, 2));
    spec.effectOut = static_cast<MixHouseEffectOut>(std::clamp(map.value("effectOut").toInt(), 0, 2));
    spec.cutAtEnd = map.value("cutAtEnd", false).toBool();
    return spec;
}

QString shapeSummary(MixHouseFadeShape shape)
{
    switch (shape) {
    case MixHouseFadeShape::EqualPower: return {};
    case MixHouseFadeShape::Linear: return QString(" (linear)");
    case MixHouseFadeShape::SCurve: return QString(" (S-curve)");
    case MixHouseFadeShape::Early: return QString(" (early)");
    case MixHouseFadeShape::Late: return QString(" (late)");
    }
    return {};
}

QString laneSummary(const char* what, const MixHouseTransitionLane& lane)
{
    switch (lane.curve) {
    case MixHouseCurveKind::None: return {};
    case MixHouseCurveKind::Crossfade: return QString("%1 crossfade%2").arg(what, shapeSummary(lane.shape));
    case MixHouseCurveKind::FadeIn: return QString("%1 fade in%2").arg(what, shapeSummary(lane.shape));
    case MixHouseCurveKind::FadeOut: return QString("%1 fade out%2").arg(what, shapeSummary(lane.shape));
    case MixHouseCurveKind::Swap:
        return lane.leaveOutBeats > 0.0 ? QString("%1 swap, %2 beats out").arg(what).arg(lane.leaveOutBeats)
               : QString("%1 swap").arg(what);
    }
    return {};
}

QString specSummary(const MixHouseTransitionSpec& spec)
{
    QStringList parts;
    const double bars = spec.lengthBeats / kBeatsPerBar;
    parts << (bars == 1.0 ? QString("1 bar") : QString("%1 bars").arg(bars));
    for (const QString& lane : { laneSummary("volume", spec.volume), laneSummary("bass", spec.bass),
                                 laneSummary("mids", spec.mid), laneSummary("highs", spec.high) }) {
        if (!lane.isEmpty()) {
            parts << lane;
        }
    }
    if (spec.cutAtEnd) {
        parts << QString("half way, then cut");
    }
    switch (spec.filter) {
    case MixHouseFilterSweep::None: break;
    case MixHouseFilterSweep::OutHighPass: parts << QString("filter out (high-pass)");
        break;
    case MixHouseFilterSweep::OutLowPass: parts << QString("filter out (low-pass)");
        break;
    case MixHouseFilterSweep::InHighPass: parts << QString("filter in (high-pass)");
        break;
    case MixHouseFilterSweep::InLowPass: parts << QString("filter in (low-pass)");
        break;
    }
    switch (spec.noise) {
    case MixHouseNoiseSweep::None: break;
    case MixHouseNoiseSweep::Riser: parts << QString("noise riser");
        break;
    case MixHouseNoiseSweep::Downsweep: parts << QString("noise downsweep");
        break;
    }
    switch (spec.effectOut) {
    case MixHouseEffectOut::None: break;
    case MixHouseEffectOut::Echo: parts << QString("echo out");
        break;
    case MixHouseEffectOut::Reverb: parts << QString("reverb out");
        break;
    }
    return parts.join(QString::fromUtf8(" \xC2\xB7 "));
}

QVariantList points(const std::vector<std::pair<double, double> >& line)
{
    QVariantList list;
    for (const auto& p : line) {
        list.push_back(QVariantList { p.first, p.second });
    }
    return list;
}

double amplitudeOfDb(double db)
{
    return db <= -49.0 ? 0.0 : std::pow(10.0, db / 20.0);
}
}

TransitionsPreferencesModel::TransitionsPreferencesModel(QObject* parent)
    : QObject(parent)
{
}

QVariantList TransitionsPreferencesModel::transitions() const
{
    QVariantList list;
    for (const MixHouseTransitionSpec& spec : configuration()->specs()) {
        list.push_back(QVariantMap {
            { "name", QString::fromStdString(spec.name) },
            { "builtIn", spec.builtIn },
            { "summary", specSummary(spec) },
        });
    }
    return list;
}

QVariantList TransitionsPreferencesModel::mixNowChoices() const
{
    QVariantList list;
    list.push_back(QVariantMap { { "text", tr("None (only pull the track forward)") }, { "value", QString() } });
    std::vector<std::string> ours;
    for (const MixHouseTransitionSpec& spec : configuration()->specs()) {
        ours.push_back(spec.name);
        list.push_back(QVariantMap { { "text", QString::fromStdString(spec.name) },
                                     { "value", QString::fromStdString(spec.name) } });
    }
    // MixMeister's own presets (volume only), which the static list carries after Mixtape's own.
    for (const MixHouseTransitionPreset& preset : IMixHouseTransitions::presets()) {
        if (std::find(ours.begin(), ours.end(), preset.name) != ours.end() || (preset.outro.empty() && preset.intro.empty())) {
            continue;
        }
        list.push_back(QVariantMap { { "text", QString::fromStdString(preset.name) + tr(" (MixMeister)") },
                                     { "value", QString::fromStdString(preset.name) } });
    }
    return list;
}

QString TransitionsPreferencesModel::mixNowTransition() const
{
    return QString::fromStdString(configuration()->mixNowTransition());
}

void TransitionsPreferencesModel::setMixNowTransition(const QString& name)
{
    if (mixNowTransition() == name) {
        return;
    }
    configuration()->setMixNowTransition(name.toStdString());
    emit changed();
}

int TransitionsPreferencesModel::mixNowLanding() const
{
    return static_cast<int>(configuration()->mixNowLanding());
}

void TransitionsPreferencesModel::setMixNowLanding(int landing)
{
    configuration()->setMixNowLanding(landing == static_cast<int>(MixHouseMixNowLanding::NextBar)
                                      ? MixHouseMixNowLanding::NextBar : MixHouseMixNowLanding::NextPhrase);
    emit changed();
}

int TransitionsPreferencesModel::panelView() const
{
    return configuration()->mixerView() ? 1 : 0;
}

void TransitionsPreferencesModel::setPanelView(int view)
{
    configuration()->setMixerView(view == 1);
    emit changed();
}

QVariantMap TransitionsPreferencesModel::newTransition() const
{
    MixHouseTransitionSpec spec;
    spec.name = "";
    spec.lengthBeats = 8.0;
    spec.volume.curve = MixHouseCurveKind::Crossfade;
    spec.bass.curve = MixHouseCurveKind::Swap;
    return specToVariant(spec);
}

QVariantMap TransitionsPreferencesModel::transition(const QString& name) const
{
    const auto spec = configuration()->spec(name.toStdString());
    return spec ? specToVariant(*spec) : QVariantMap();
}

QString TransitionsPreferencesModel::save(const QVariantMap& map, const QString& originalName)
{
    const MixHouseTransitionSpec spec = specFromVariant(map);
    const std::string problem = configuration()->saveSpec(spec);
    if (!problem.empty()) {
        return QString::fromStdString(problem);
    }
    // Renamed: the old entry goes, and Mix Now follows the new name if it was using the old one.
    if (!originalName.isEmpty() && originalName.toStdString() != spec.name) {
        const bool usedByMixNow = mixNowTransition() == originalName;
        configuration()->removeSpec(originalName.toStdString());
        if (usedByMixNow) {
            configuration()->setMixNowTransition(spec.name);
        }
    }
    emit changed();
    return {};
}

bool TransitionsPreferencesModel::remove(const QString& name)
{
    const bool removed = configuration()->removeSpec(name.toStdString());
    if (removed) {
        emit changed();
    }
    return removed;
}

QVariantMap TransitionsPreferencesModel::preview(const QVariantMap& map) const
{
    MixHouseTransitionSpec spec = specFromVariant(map);
    QVariantMap result;
    result["lengthBeats"] = spec.lengthBeats;
    // The preview draws whatever is valid enough to draw; a missing name is only a problem on save.
    MixHouseTransitionSpec forCheck = spec;
    if (forCheck.name.empty()) {
        forCheck.name = "preview";
    }
    const std::string problem = validateTransitionSpec(forCheck);
    result["error"] = QString::fromStdString(problem);
    if (!problem.empty()) {
        return result;
    }
    const MixHouseTransitionPreset preset = presetFromSpec(forCheck);
    const auto volume = [](const std::vector<MixHouseTransitionMarker>& markers) {
        std::vector<std::pair<double, double> > line;
        for (const auto& m : markers) {
            line.push_back({ m.offset, amplitudeOfDb(m.gainDb) });
        }
        return line;
    };
    const auto bass = [](const std::vector<MixHouseBassMarker>& markers) {
        std::vector<std::pair<double, double> > line;
        for (const auto& m : markers) {
            line.push_back({ m.offset, m.value });
        }
        return line;
    };
    result["volumeOut"] = points(volume(preset.outro));
    result["volumeIn"] = points(volume(preset.intro));
    result["bassOut"] = points(bass(preset.bassOutro));
    result["bassIn"] = points(bass(preset.bassIntro));
    return result;
}
