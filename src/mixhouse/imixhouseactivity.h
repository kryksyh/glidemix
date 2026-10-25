/*
 * GlideMix: what the app is busy with in the background, for the status
 * line at the bottom of the window (like Ableton's and rekordbox's). The
 * library's scan and analysis report through their own channels; anything
 * else posts here: energy ratings, the stems engine, ...
 *
 * Main thread only.
 */
#pragma once

#include <QList>
#include <QString>

#include "async/notification.h"
#include "modularity/imoduleinterface.h"

namespace au::mixhouse {
class IMixHouseActivity : MODULE_CONTEXT_INTERFACE
{
    INTERFACE_ID(IMixHouseActivity)

public:
    virtual ~IMixHouseActivity() = default;

    struct Activity {
        QString id;
        QString text;        ///< "Rating energy: La Muralla"
        int percent = -1;    ///< 0-100, or -1 when there is no measure
    };

    //! Starts or updates the activity `id`.
    virtual void set(const QString& id, const QString& text, int percent = -1) = 0;
    //! Ends it; `message` (if any) is shown for a few seconds afterwards.
    virtual void finish(const QString& id, const QString& message = QString()) = 0;
    //! A one-off message, nothing running ("Exported: mix.wav").
    virtual void post(const QString& message) = 0;

    virtual QList<Activity> activities() const = 0;
    virtual QString lastMessage() const = 0;
    virtual muse::async::Notification changed() const = 0;
};
}
