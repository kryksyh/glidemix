/*
* Audacity: A Digital Audio Editor
*/
#pragma once

#include "effectstypes.h"
#include "framework/global/async/notification.h"
#include "framework/global/io/path.h"
#include "framework/global/modularity/imoduleinterface.h"

namespace au::effects {
class IEffectsConfiguration : MODULE_GLOBAL_INTERFACE
{
    INTERFACE_ID(IEffectsConfiguration)
public:

    virtual ~IEffectsConfiguration() = default;

    virtual bool applyEffectToAllAudio() const = 0;
    virtual void setApplyEffectToAllAudio(bool value) = 0;
    virtual muse::async::Notification applyEffectToAllAudioChanged() const = 0;

    virtual EffectMenuOrganization effectMenuOrganization() const = 0;
    virtual void setEffectMenuOrganization(EffectMenuOrganization) = 0;
    virtual muse::async::Notification effectMenuOrganizationChanged() const = 0;

    virtual double previewMaxDuration() const = 0;
    virtual void setPreviewMaxDuration(double value) = 0;

    virtual EffectUIMode effectUIMode(const EffectId& effectId) const = 0;
    virtual void setEffectUIMode(const EffectId& effectId, EffectUIMode mode) = 0;
    virtual muse::async::Notification effectUIModeChanged() const = 0;

    virtual std::string lastUsedPreset(const EffectId& effectId) const = 0;
    virtual void setLastUsedPreset(const EffectId& effectId, const std::string& presetId) = 0;

    virtual muse::io::paths_t lv2CustomPaths() const = 0;
    virtual void setLv2CustomPaths(const muse::io::paths_t& paths) = 0;
    virtual muse::async::Notification lv2CustomPathsChanged() const = 0;

    //! GlideMix: the DJ Effects menu (Preferences > Effects > Visible
    //! Effects), as effect ids -- visible ones in menu order, then hidden
    //! ones. Built-in effects in neither list are visible, after the rest.
    virtual std::vector<EffectId> djVisibleEffects() const = 0;
    virtual std::vector<EffectId> djHiddenEffects() const = 0;
    virtual void setDjEffectLists(const std::vector<EffectId>& visible, const std::vector<EffectId>& hidden) = 0;
    virtual muse::async::Notification djEffectListsChanged() const = 0;
    //! Preferences > Effects > Effect load behavior: an effect replacing a
    //! DJ Effect keeps its meta knob position (Mixxx's AdoptMetaknobValue),
    //! or starts at its own default.
    virtual bool djKeepMetaknobPosition() const = 0;
    virtual void setDjKeepMetaknobPosition(bool keep) = 0;
    virtual muse::async::Notification djKeepMetaknobPositionChanged() const = 0;

    //! Preferences > Mixer > Equalizer frequency Shelves: the DJ EQ effects'
    //! low/high crossovers in Hz (Mixxx's [Mixer Profile] Lo/HiEQFrequency).
    virtual double djLowEqFrequency() const = 0;
    virtual double djHighEqFrequency() const = 0;
    virtual void setDjEqFrequencies(double low, double high) = 0;
    virtual muse::async::Notification djEqFrequenciesChanged() const = 0;

    //! Set once Mixxx's visible effects list has been taken over.
    virtual bool djEffectListsImported() const = 0;
    virtual void setDjEffectListsImported(bool imported) = 0;

    virtual muse::io::paths_t vst3CustomPaths() const = 0;
    virtual void setVst3CustomPaths(const muse::io::paths_t& paths) = 0;
    virtual muse::async::Notification vst3CustomPathsChanged() const = 0;
};
}
