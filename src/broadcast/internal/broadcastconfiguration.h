/*
 * GlideMix
 */
#pragma once

#include "async/notification.h"
#include "modularity/ioc.h"

#include "broadcast/ibroadcastconfiguration.h"

namespace au::broadcast {
class BroadcastConfiguration : public IBroadcastConfiguration
{
public:
    void init();

    BroadcastServer server() const override;
    void setServer(const BroadcastServer& server) override;

    IBroadcaster::Source source() const override;
    void setSource(IBroadcaster::Source source) override;

    float gainDb() const override;
    void setGainDb(float db) override;
    bool reconnect() const override;
    void setReconnect(bool reconnect) override;
    int reconnectDelaySec() const override;
    void setReconnectDelaySec(int seconds) override;

    bool sendMetadata() const override;
    bool titleFirst() const override;
    void setTitleFirst(bool titleFirst) override;
    void setSendMetadata(bool send) override;

    muse::async::Notification changed() const override;

private:
    muse::async::Notification m_changed;
};
}
