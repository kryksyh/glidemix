/*
 * GlideMix
 */
#pragma once

#include <QAbstractListModel>
#include <QQmlParserStatus>
#include <QtQmlIntegration>

#include "async/asyncable.h"
#include "modularity/ioc.h"
#include "context/iglobalcontext.h"

#include "mixhouse/imixhousetransitions.h"
#include "mixhouse/imixhousetransitionconfiguration.h"
#include "trackedit/iprojecthistory.h"

namespace au::mixhouse {
//! The BEATMATCH zones for the timeline overlay.
class MixHouseTransitionZonesModel : public QAbstractListModel, public QQmlParserStatus, public muse::Contextable,
    public muse::async::Asyncable
{
    Q_OBJECT
    Q_INTERFACES(QQmlParserStatus)
    QML_ELEMENT

    muse::ContextInject<IMixHouseTransitions> transitions { this };
    muse::ContextInject<context::IGlobalContext> globalContext { this };
    muse::GlobalInject<IMixHouseTransitionConfiguration> transitionConfiguration;
    muse::ContextInject<trackedit::IProjectHistory> projectHistory { this };

    //! GlideMix: the pill opens the two-channel mixer instead of the transition window (Preferences).
    Q_PROPERTY(bool mixerView READ mixerView NOTIFY mixerViewChanged)

public:
    explicit MixHouseTransitionZonesModel(QObject* parent = nullptr);

    bool mixerView() const;

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void applyCrossfade(int row);
    Q_INVOKABLE void removeCrossfade(int row);
    Q_INVOKABLE void applyPreset(int row, int presetIndex);
    //! Applies a transition from Preferences > Transitions by name.
    Q_INVOKABLE void applyTransition(int row, const QString& name);
    //! The zone's menu: [{id: "preset:N" | "transition:<name>", title, checkable, checked}].
    Q_INVOKABLE QVariantList presetMenu(int row) const;
    //! One lane (0 volume, 1 low, 2 color, 3 mid, 4 high) of both clips across the zone:
    //! { out: { samples: [[t, v]], points: [[t, v]] }, in: {...} }, t in project seconds.
    Q_INVOKABLE QVariantMap laneLines(int row, int lane) const;
    //! Replaces one side's points of a lane inside the overlap: [[t, v], ...]. One undo step.
    Q_INVOKABLE bool setLanePoints(int row, int lane, bool outgoingSide, const QVariantList& points,
                                   bool consolidate = false);
    //! ADD FX: filter-out, filter-in, echo, reverb, riser, downsweep.
    Q_INVOKABLE bool addEffect(int row, const QString& kind);
    //! The effects ADD FX put on this transition: [{ kind, name, amount }].
    Q_INVOKABLE QVariantList addedEffects(int row) const;
    Q_INVOKABLE bool setEffectAmount(int row, const QString& kind, double amount);
    Q_INVOKABLE bool removeEffect(int row, const QString& kind);
    Q_INVOKABLE bool setEffectEnabled(int row, const QString& kind, bool enabled);
    //! Off: everything the transition wrote goes, the pill turns grey. On: back.
    Q_INVOKABLE bool setTransitionEnabled(int row, bool enabled);
    //! Length of the zone in bars (for the panel's header).
    Q_INVOKABLE double lengthBars(int row) const;

signals:
    //! Clip volume, bass or effect lines may have changed: redraw the lanes.
    void linesChanged();
    void mixerViewChanged();

public:

    void classBegin() override {}
    void componentComplete() override;

private:
    enum Role {
        OutgoingTrackIdRole = Qt::UserRole + 1,
        IncomingTrackIdRole,
        StartTimeRole,
        EndTimeRole,
        CrossfadedRole,
        PresetRole,
        DisabledRole,
    };

    void reload();

    std::vector<IMixHouseTransitions::Zone> m_zones;
};
}
