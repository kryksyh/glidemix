/*
 * GlideMix
 */
#pragma once

#include "mixhouse/imixhouseactivity.h"

namespace au::mixhouse {
class MixHouseActivity : public IMixHouseActivity
{
public:
    void set(const QString& id, const QString& text, int percent = -1) override;
    void finish(const QString& id, const QString& message = QString()) override;
    void post(const QString& message) override;

    QList<Activity> activities() const override;
    QString lastMessage() const override;
    muse::async::Notification changed() const override;

private:
    QList<Activity> m_activities;
    QString m_lastMessage;
    muse::async::Notification m_changed;
};
}
