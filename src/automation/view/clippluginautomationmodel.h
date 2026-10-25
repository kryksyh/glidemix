/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include "modularity/ioc.h"
#include "async/asyncable.h"
#include "context/iglobalcontext.h"
#include "trackedit/iprojecthistory.h"
#include "trackedit/iclipsinteraction.h"
#include "automation/iclipgaininteraction.h"
#include "projectscene/types/projectscenetypes.h"
#include "projectscene/iprojectviewstate.h"

namespace au::automation {
class ClipPluginAutomationModel : public QObject, public muse::Contextable, public muse::async::Asyncable
{
    Q_OBJECT
    Q_PROPERTY(projectscene::ClipKey clipKey READ clipKey WRITE setClipKey NOTIFY changed)
    Q_PROPERTY(QVariantList segments READ segments NOTIFY changed)
    Q_PROPERTY(bool visible READ visible NOTIFY changed)
    Q_PROPERTY(double sequenceStart READ sequenceStart NOTIFY changed)
    Q_PROPERTY(QString label READ label NOTIFY changed)
    Q_PROPERTY(QString color READ color NOTIFY changed)
    //! False when the line was overridden by moving its control (Ableton): drawn grey, not played.
    Q_PROPERTY(bool active READ active NOTIFY changed)
    //! The parameter has a line on this clip (else the lane shows its value, dashed).
    Q_PROPERTY(bool automated READ automated NOTIFY changed)
    //! A switch or stepped choice: the lane draws hard steps.
    Q_PROPERTY(bool stepped READ stepped NOTIFY changed)
    //! The clip's other automated parameters of the same track, drawn faintly
    //! behind the edited one so every line can be seen at once:
    //! [{ segments, color, stepped, active }].
    Q_PROPERTY(QVariantList otherLines READ otherLines NOTIFY changed)
    //! GlideMix: a lane of its own under the track (Ableton's "Show
    //! Automation in New Lane"): this parameter, whatever the focus is.
    //! { instanceId, pluginId, parameterId, label, color, defaultValue, stepped }
    Q_PROPERTY(QVariantMap lane READ lane WRITE setLane NOTIFY changed)
    muse::ContextInject<context::IGlobalContext> globalContext{this};
    muse::ContextInject<trackedit::IProjectHistory> projectHistory{this};
    muse::ContextInject<trackedit::IClipsInteraction> clipsInteraction{this};
    muse::ContextInject<IClipGainInteraction> clipGainInteraction{this};
public:
    explicit ClipPluginAutomationModel(QObject* parent = nullptr);
    Q_INVOKABLE void init();
    Q_INVOKABLE bool commit(const QVariantList& segments);
    projectscene::ClipKey clipKey() const { return m_key; }
    void setClipKey(const projectscene::ClipKey& key) { m_key = key; emit changed(); }
    QVariantList segments() const;
    bool visible() const;
    double sequenceStart() const;
    QString label() const;
    QString color() const;
    bool active() const;
    bool automated() const;
    bool stepped() const;
    QVariantList otherLines() const;
    QVariantMap lane() const { return m_lane; }
    void setLane(const QVariantMap& lane) { m_lane = lane; emit changed(); }
    //! The edited line gets a lane of its own under its track.
    Q_INVOKABLE void showInOwnLane();
signals:
    void changed();
    void editRejected(const QString& reason);
private:
    projectscene::IProjectViewState::PluginAutomationFocus spec() const;
    projectscene::ClipKey m_key;
    QVariantMap m_lane;
};
}
