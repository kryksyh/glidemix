/*
 * GlideMix
 */
#pragma once

#include "async/asyncable.h"
#include "modularity/ioc.h"

#include "../ieffectchainpresets.h"
#include "../ieffectinstancesregister.h"
#include "../ieffectsprovider.h"
#include "../irealtimeeffectservice.h"

namespace au::effects {
class EffectChainPresets : public IEffectChainPresets, public muse::Contextable, public muse::async::Asyncable
{
    muse::GlobalInject<IEffectsProvider> effectsProvider;
    muse::GlobalInject<IEffectInstancesRegister> instancesRegister;
    muse::GlobalInject<IMixxxEffectIds> mixxxEffectIds;
    muse::ContextInject<IRealtimeEffectService> realtimeEffectService { this };

public:
    explicit EffectChainPresets(const muse::modularity::ContextPtr& ctx)
        : muse::Contextable(ctx) {}

    void init();

    std::vector<std::string> presetNames() const override;
    std::vector<PresetEffect> presetEffects(const std::string& name) const override;
    bool hasPreset(const std::string& name) const override;

    muse::Ret saveTrackAsPreset(TrackId trackId, const std::string& name) override;
    muse::Ret loadPresetOntoTrack(const std::string& name, TrackId trackId) override;

    muse::Ret renamePreset(const std::string& name, const std::string& newName) override;
    muse::Ret removePreset(const std::string& name) override;
    void setPresetOrder(const std::vector<std::string>& names) override;
    muse::RetVal<std::string> importPreset(const muse::io::path_t& filePath) override;
    muse::Ret exportPreset(const std::string& name, const muse::io::path_t& filePath) override;

    muse::async::Notification presetsChanged() const override;

    //! One chain preset file, as Mixxx writes it.
    struct Parameter {
        std::string id;
        std::string value;
        std::string linkType = "NONE";
        std::string linkInversion = "0";
        std::string hidden = "0";
    };
    struct Effect {
        std::string id;
        std::string backendType;
        std::string metaParameterValue = "0";
        std::vector<Parameter> parameters;
    };
    struct Chain {
        std::string name;
        std::string mixMode = "DRY/WET";
        std::string superParameterValue = "0";
        std::vector<Effect> effects;   // empty ids are Mixxx's empty slots
    };

private:
    void importMixxxPresetsOnce();
    std::string filePathFor(const std::string& name) const;
    std::vector<std::string> storedOrder() const;
    void writeOrder(const std::vector<std::string>& names);
    //! The effect a preset entry refers to here, or empty.
    EffectId resolve(const Effect& effect) const;

    muse::async::Notification m_presetsChanged;
};
}
