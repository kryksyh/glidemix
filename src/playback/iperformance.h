/*
 * GlideMix
 */
#pragma once

#include <string>
#include <vector>

#include "framework/global/async/channel.h"
#include "framework/global/async/notification.h"
#include "framework/global/modularity/imoduleinterface.h"
#include "framework/global/types/secs.h"

namespace au::playback {
/// MixMeister's Perform: the audience playback. A second renderer of the same
/// arrangement with its own position (the red playhead) on outputs 1-2, while
/// the ordinary playhead becomes the headphone preview (outputs 3-4 when the
/// interface has them). What the audience has heard is locked. Mixtape engine only.
class IPerformance : MODULE_CONTEXT_INTERFACE
{
    INTERFACE_ID(IPerformance)

public:
    virtual ~IPerformance() = default;

    virtual bool performanceAvailable() const = 0;
    //! Started (playing or paused) since the last reset.
    virtual bool performanceActive() const = 0;
    virtual bool performing() const = 0;
    virtual muse::secs_t performancePosition() const = 0;

    //! Perform / Pause.
    virtual void togglePerform() = 0;
    //! Stops the performance and puts the red playhead back at the start.
    virtual void resetPerformance() = 0;
    //! Moves the red playhead (not while performing).
    virtual void setPerformancePosition(muse::secs_t position) = 0;

    //! Where Play goes while performing: "" = outputs 3-4 of the audience
    //! device, otherwise a separate output device (e.g. the Mac's headphone jack).
    virtual std::vector<std::string> outputDevices() const = 0;
    virtual std::string headphonesDevice() const = 0;
    virtual void setHeadphonesDevice(const std::string& device) = 0;

    virtual muse::async::Channel<muse::secs_t> performancePositionChanged() const = 0;
    virtual muse::async::Notification performanceStateChanged() const = 0;
};
}
