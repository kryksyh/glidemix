/*
 * GlideMix
 */
#include "mixhouseactivity.h"

using namespace au::mixhouse;

void MixHouseActivity::set(const QString& id, const QString& text, int percent)
{
    for (Activity& a : m_activities) {
        if (a.id == id) {
            if (a.text == text && a.percent == percent) {
                return;
            }
            a.text = text;
            a.percent = percent;
            m_changed.notify();
            return;
        }
    }
    m_activities.push_back({ id, text, percent });
    m_changed.notify();
}

void MixHouseActivity::finish(const QString& id, const QString& message)
{
    for (int i = 0; i < m_activities.size(); ++i) {
        if (m_activities[i].id == id) {
            m_activities.removeAt(i);
            break;
        }
    }
    if (!message.isEmpty()) {
        m_lastMessage = message;
    }
    m_changed.notify();
}

void MixHouseActivity::post(const QString& message)
{
    m_lastMessage = message;
    m_changed.notify();
}

QList<IMixHouseActivity::Activity> MixHouseActivity::activities() const
{
    return m_activities;
}

QString MixHouseActivity::lastMessage() const
{
    return m_lastMessage;
}

muse::async::Notification MixHouseActivity::changed() const
{
    return m_changed;
}
