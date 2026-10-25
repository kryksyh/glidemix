/*
 * GlideMix
 */
#include "mixhousetransitionzonesmodel.h"

#include <QTimer>

using namespace au::mixhouse;

MixHouseTransitionZonesModel::MixHouseTransitionZonesModel(QObject* parent)
    : QAbstractListModel(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

void MixHouseTransitionZonesModel::componentComplete()
{
    if (transitions()) {
        transitions()->zonesChanged().onNotify(this, [this]() {
            // Several changes arrive together (both clips of a crossfade, a
            // history state); rebuild once.
            QTimer::singleShot(0, this, [this]() { reload(); });
        });
    }
    // Any edit or undo can change the clips' volume, bass or effect lines.
    if (projectHistory()) {
        projectHistory()->historyChanged().onReceive(this, [this](trackedit::HistoryEvent) {
            emit linesChanged();
        });
    }
    reload();
}

void MixHouseTransitionZonesModel::reload()
{
    std::vector<IMixHouseTransitions::Zone> zones = transitions() ? transitions()->zones()
                                                    : std::vector<IMixHouseTransitions::Zone> {};
    // The same transitions (only their lines or preset changed): update the
    // rows in place, so an open transition panel and the chosen lane stay.
    bool same = zones.size() == m_zones.size();
    for (size_t i = 0; same && i < zones.size(); ++i) {
        same = zones[i].outgoing == m_zones[i].outgoing && zones[i].incoming == m_zones[i].incoming;
    }
    if (same) {
        m_zones = std::move(zones);
        if (!m_zones.empty()) {
            emit dataChanged(index(0), index(static_cast<int>(m_zones.size()) - 1));
        }
    } else {
        beginResetModel();
        m_zones = std::move(zones);
        endResetModel();
    }
    emit linesChanged();
}

int MixHouseTransitionZonesModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_zones.size());
}

QVariant MixHouseTransitionZonesModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) {
        return {};
    }
    const auto& z = m_zones[static_cast<size_t>(index.row())];
    switch (role) {
    case OutgoingTrackIdRole: return QVariant::fromValue<qint64>(z.outgoing.trackId);
    case IncomingTrackIdRole: return QVariant::fromValue<qint64>(z.incoming.trackId);
    case StartTimeRole: return z.start;
    case EndTimeRole: return z.end;
    case CrossfadedRole: return z.crossfaded;
    case PresetRole: return QString::fromStdString(z.preset);
    case DisabledRole: return z.disabled;
    default: return {};
    }
}

QHash<int, QByteArray> MixHouseTransitionZonesModel::roleNames() const
{
    return {
        { OutgoingTrackIdRole, "outgoingTrackId" },
        { IncomingTrackIdRole, "incomingTrackId" },
        { StartTimeRole, "startTime" },
        { EndTimeRole, "endTime" },
        { CrossfadedRole, "crossfaded" },
        { PresetRole, "preset" },
        { DisabledRole, "disabled" },
    };
}

void MixHouseTransitionZonesModel::applyCrossfade(int row)
{
    if (row >= 0 && row < rowCount() && transitions()) {
        const auto zone = m_zones[static_cast<size_t>(row)];
        transitions()->applyCrossfade(zone.outgoing, zone.incoming);
    }
}

void MixHouseTransitionZonesModel::applyPreset(int row, int presetIndex)
{
    if (row >= 0 && row < rowCount() && transitions()) {
        const auto zone = m_zones[static_cast<size_t>(row)];
        transitions()->applyPreset(zone.outgoing, zone.incoming, presetIndex);
    }
}

void MixHouseTransitionZonesModel::applyTransition(int row, const QString& name)
{
    if (row >= 0 && row < rowCount() && transitions()) {
        const auto zone = m_zones[static_cast<size_t>(row)];
        transitions()->applyTransition(zone.outgoing, zone.incoming, name.toStdString());
    }
}

QVariantList MixHouseTransitionZonesModel::presetMenu(int row) const
{
    QVariantList menu;
    const bool valid = row >= 0 && row < rowCount();
    const std::string current = valid ? m_zones[static_cast<size_t>(row)].preset : std::string();
    const bool crossfaded = valid && m_zones[static_cast<size_t>(row)].crossfaded;

    // A plain crossfade that fits the overlap, from the linked anchors when there
    // are some. MixMeister's other presets stay in IMixHouseTransitions::presets()
    // but aren't offered: the list here is the one on Preferences > Transitions.
    menu << QVariantMap { { "id", QStringLiteral("preset:0") }, { "title", tr("Crossfade") }, { "enabled", true },
                          { "checkable", true },
                          // An unnamed fade (one that did not come through this menu)
                          // reads as the plain crossfade, as it always has.
                          { "checked", crossfaded && (current.empty() || current == IMixHouseTransitions::presets().front().name) } };

    if (!transitionConfiguration()) {
        return menu;
    }

    //! Mixtape's built-ins, then yours, in the order the Transitions page lists
    //! them. What you build there is what you can put on a zone here.
    bool separated = false;
    bool yoursStarted = false;
    for (const MixHouseTransitionSpec& spec : transitionConfiguration()->specs()) {
        if (!separated) {
            menu << QVariantMap {};
            separated = true;
        }
        if (!spec.builtIn && !yoursStarted) {
            menu << QVariantMap {};
            yoursStarted = true;
        }
        const QString name = QString::fromStdString(spec.name);
        menu << QVariantMap { { "id", QStringLiteral("transition:") + name }, { "title", name }, { "enabled", true },
                              { "checkable", true }, { "checked", crossfaded && current == spec.name } };
    }
    return menu;
}

void MixHouseTransitionZonesModel::removeCrossfade(int row)
{
    if (row >= 0 && row < rowCount() && transitions()) {
        const auto zone = m_zones[static_cast<size_t>(row)];
        transitions()->removeCrossfade(zone.outgoing, zone.incoming);
    }
}

QVariantMap MixHouseTransitionZonesModel::laneLines(int row, int lane) const
{
    QVariantMap result;
    if (row < 0 || row >= rowCount() || !transitions()) {
        return result;
    }
    const auto& zone = m_zones[static_cast<size_t>(row)];
    IMixHouseTransitions::LaneLine out, in;
    if (!transitions()->laneLines(zone.outgoing, zone.incoming,
                                  static_cast<IMixHouseTransitions::Lane>(std::clamp(lane, 0, 4)), out, in)) {
        return result;
    }
    const auto toList = [](const std::vector<std::pair<double, double> >& v) {
        QVariantList list;
        for (const auto& [t, y] : v) {
            list.push_back(QVariantList { t, y });
        }
        return list;
    };
    const auto toMap = [&](const IMixHouseTransitions::LaneLine& l) {
        return QVariantMap { { "samples", toList(l.samples) }, { "points", toList(l.points) } };
    };
    result["out"] = toMap(out);
    result["in"] = toMap(in);
    return result;
}

bool MixHouseTransitionZonesModel::setLanePoints(int row, int lane, bool outgoingSide, const QVariantList& points)
{
    if (row < 0 || row >= rowCount() || !transitions()) {
        return false;
    }
    const auto zone = m_zones[static_cast<size_t>(row)];
    std::vector<std::pair<double, double> > list;
    for (const QVariant& p : points) {
        const QVariantList pair = p.toList();
        if (pair.size() == 2) {
            list.push_back({ pair[0].toDouble(), pair[1].toDouble() });
        }
    }
    return transitions()->setLanePoints(zone.outgoing, zone.incoming,
                                        static_cast<IMixHouseTransitions::Lane>(std::clamp(lane, 0, 4)), outgoingSide, list);
}

bool MixHouseTransitionZonesModel::addEffect(int row, const QString& kind)
{
    if (row < 0 || row >= rowCount() || !transitions()) {
        return false;
    }
    const auto zone = m_zones[static_cast<size_t>(row)];
    return transitions()->addEffect(zone.outgoing, zone.incoming, kind.toStdString());
}

QVariantList MixHouseTransitionZonesModel::addedEffects(int row) const
{
    QVariantList result;
    if (row < 0 || row >= rowCount() || !transitions()) {
        return result;
    }
    const auto& zone = m_zones[static_cast<size_t>(row)];
    for (const auto& e : transitions()->addedEffects(zone.outgoing, zone.incoming)) {
        result.push_back(QVariantMap { { "kind", QString::fromStdString(e.kind) },
                                       { "name", QString::fromStdString(e.name) },
                                       { "amount", e.amount },
                                       { "enabled", e.enabled } });
    }
    return result;
}

bool MixHouseTransitionZonesModel::setEffectAmount(int row, const QString& kind, double amount)
{
    if (row < 0 || row >= rowCount() || !transitions()) {
        return false;
    }
    const auto zone = m_zones[static_cast<size_t>(row)];
    return transitions()->setEffectAmount(zone.outgoing, zone.incoming, kind.toStdString(), amount);
}

bool MixHouseTransitionZonesModel::removeEffect(int row, const QString& kind)
{
    if (row < 0 || row >= rowCount() || !transitions()) {
        return false;
    }
    const auto zone = m_zones[static_cast<size_t>(row)];
    return transitions()->removeEffect(zone.outgoing, zone.incoming, kind.toStdString());
}

bool MixHouseTransitionZonesModel::setEffectEnabled(int row, const QString& kind, bool enabled)
{
    if (row < 0 || row >= rowCount() || !transitions()) {
        return false;
    }
    const auto zone = m_zones[static_cast<size_t>(row)];
    return transitions()->setEffectEnabled(zone.outgoing, zone.incoming, kind.toStdString(), enabled);
}

bool MixHouseTransitionZonesModel::setTransitionEnabled(int row, bool enabled)
{
    if (row < 0 || row >= rowCount() || !transitions()) {
        return false;
    }
    const auto zone = m_zones[static_cast<size_t>(row)];
    return transitions()->setTransitionEnabled(zone.outgoing, zone.incoming, enabled);
}

double MixHouseTransitionZonesModel::lengthBars(int row) const
{
    const auto project = globalContext() ? globalContext()->currentTrackeditProject() : nullptr;
    if (row < 0 || row >= rowCount() || !project) {
        return 0.0;
    }
    const auto ts = project->timeSignature();
    const double bar = ts.tempo > 0.0 ? 60.0 / ts.tempo * (4.0 / std::max(1, ts.lower)) * std::max(1, ts.upper) : 2.0;
    const auto& zone = m_zones[static_cast<size_t>(row)];
    return (zone.end - zone.start) / bar;
}
