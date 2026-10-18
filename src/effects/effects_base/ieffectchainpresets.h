/*
 * GlideMix
 */
#pragma once

#include <string>
#include <vector>

#include "async/notification.h"
#include "global/io/path.h"
#include "global/modularity/imoduleinterface.h"
#include "global/types/ret.h"
#include "global/types/retval.h"

#include "effectstypes.h"

namespace au::effects {
//! Effect chain presets: a track's realtime effect stack with its settings,
//! saved by name (Mixxx's EffectChainPreset, same XML file format), listed
//! in Preferences > Effects and loaded from Add effect > Chain presets.
class IEffectChainPresets : MODULE_EXPORT_INTERFACE
{
    INTERFACE_ID(IEffectChainPresets);

public:
    struct PresetEffect {
        std::string title;       ///< effect name, or the id when the effect isn't installed
        bool available = false;  ///< the effect exists here and can be loaded
    };

    virtual ~IEffectChainPresets() = default;

    //! Preset names in list order.
    virtual std::vector<std::string> presetNames() const = 0;
    virtual std::vector<PresetEffect> presetEffects(const std::string& name) const = 0;
    virtual bool hasPreset(const std::string& name) const = 0;

    virtual muse::Ret saveTrackAsPreset(TrackId trackId, const std::string& name) = 0;
    //! Replaces the track's effects with the preset's (effects that aren't
    //! installed are skipped).
    virtual muse::Ret loadPresetOntoTrack(const std::string& name, TrackId trackId) = 0;

    virtual muse::Ret renamePreset(const std::string& name, const std::string& newName) = 0;
    virtual muse::Ret removePreset(const std::string& name) = 0;
    virtual void setPresetOrder(const std::vector<std::string>& names) = 0;
    //! Imports a Mixxx / GlideMix chain preset file; returns its name.
    virtual muse::RetVal<std::string> importPreset(const muse::io::path_t& filePath) = 0;
    virtual muse::Ret exportPreset(const std::string& name, const muse::io::path_t& filePath) = 0;

    virtual muse::async::Notification presetsChanged() const = 0;
};

//! Mixxx effect ids ("org.mixxx.effects.echo") <-> GlideMix effect
//! ids of the DJ Effects built from them; registered by the built-in effects
//! collection.
class IMixxxEffectIds : MODULE_GLOBAL_EXPORT_INTERFACE
{
    INTERFACE_ID(IMixxxEffectIds);

public:
    virtual ~IMixxxEffectIds() = default;

    virtual EffectId effectIdForMixxxId(const std::string& mixxxId) const = 0;
    virtual std::string mixxxIdForEffectId(const EffectId& effectId) const = 0;
};
}
