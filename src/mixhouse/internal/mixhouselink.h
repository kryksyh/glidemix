/*
 * GlideMix: Ableton Link (Ableton's Link library, GPL v2 or later). While the
 * mix plays, GlideMix leads the Link session: its tempo (the tempo line,
 * glides included), its beat and bar, and play / stop. VJ software (Resolume,
 * TouchDesigner) and other Link apps on the network follow the mix. GlideMix
 * does not follow their tempo: a prepared mix keeps its own.
 *
 * Ableton's guidelines: "Ableton Link" in full in text, the state as Enabled /
 * Disabled; the toolbar button reads LINK like the other toggle buttons.
 */
#pragma once

#include <memory>

#include <QObject>
#include <QTimer>

#include "async/asyncable.h"
#include "async/notification.h"
#include "modularity/ioc.h"
#include "context/iglobalcontext.h"
#include "mixhouse/imixhousetempomap.h"

namespace au::mixhouse {
class MixHouseLink : public QObject, public muse::Injectable, public muse::async::Asyncable
{
public:
    muse::Inject<context::IGlobalContext> globalContext = { this };
    muse::Inject<IMixHouseTempoMap> tempoMap = { this };

    explicit MixHouseLink(const muse::modularity::ContextPtr& ctx);
    ~MixHouseLink() override;

    void init();
    bool enabled() const;
    void setEnabled(bool enabled);
    int peers() const;
    //! Enabled / disabled or the number of apps changed.
    muse::async::Notification changed() const { return m_changed; }

    //! The one instance (the toolbar button and its tooltip ask it).
    static MixHouseLink* instance();

private:
    void tick();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
    QTimer m_timer;
    muse::async::Notification m_changed;
    bool m_wasPlaying = false;
};
}
