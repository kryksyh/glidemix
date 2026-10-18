/*
 * GlideMix
 */
#pragma once

#include <QColor>

#include "async/channel.h"
#include "modularity/imoduleinterface.h"
#include "trackedit/trackedittypes.h"

namespace au::mixhouse {
/// The parked MixHouse DJ app's deck-line colour settings, per track: colour
/// intensity (how strongly the clip body carries its colour; the waveform
/// takes the full colour as the body fades) and Colour by key (each clip's
/// colour from its musical key, the Key column's palette). Not saved with the
/// project yet.
class IMixHouseClipColors : MODULE_CONTEXT_INTERFACE
{
    INTERFACE_ID(IMixHouseClipColors)

public:
    virtual ~IMixHouseClipColors() = default;

    static constexpr int kFullIntensity = 255;

    virtual int intensity(const trackedit::TrackId& track) const = 0;   ///< 0..255
    virtual void setIntensity(const trackedit::TrackId& track, int intensity) = 0;

    virtual bool colorByKey(const trackedit::TrackId& track) const = 0;
    virtual void setColorByKey(const trackedit::TrackId& track, bool on) = 0;

    /// The key colour of a clip (after any transpose), or an invalid colour
    /// when Colour by key is off for its track or the clip has no known key.
    virtual QColor keyColor(const trackedit::ClipKey& clip) const = 0;

    virtual muse::async::Channel<trackedit::TrackId> trackColorsChanged() const = 0;
};
}
