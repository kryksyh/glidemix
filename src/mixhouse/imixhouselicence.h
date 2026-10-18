/*
 * GlideMix licence: demo, GlideMix or GlideMix Pro. Keys are sold through
 * Polar (polar.sh/glidemix) and activated once online; afterwards GlideMix
 * works offline, checking the key again now and then when online.
 *
 * GlideMix is GPL: this check is for the official builds, not copy
 * protection, and anyone may build the source without it.
 */
#pragma once

#include <functional>

#include <QString>

#include "async/notification.h"
#include "modularity/imoduleinterface.h"

namespace au::mixhouse {
enum class LicenceTier {
    Demo = 0,
    Standard = 1,
    Pro = 2,
};

class IMixHouseLicence : MODULE_GLOBAL_INTERFACE
{
    INTERFACE_ID(IMixHouseLicence)

public:
    virtual ~IMixHouseLicence() = default;

    virtual LicenceTier tier() const = 0;
    //! The key as entered, or empty in the demo.
    virtual QString key() const = 0;

    using Done = std::function<void (bool ok, const QString& message)>;
    //! Registers this computer with the key (one of its 3 activations).
    virtual void activate(const QString& key, Done done) = 0;
    //! Frees this computer's activation and goes back to the demo.
    virtual void deactivate(Done done) = 0;

    //! The checkout for GlideMix (Standard) or GlideMix Pro (Pro).
    virtual QString buyUrl(LicenceTier tier) const = 0;
    virtual muse::async::Notification tierChanged() const = 0;
};
}
