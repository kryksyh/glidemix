/*
* Audacity: A Digital Audio Editor
*/
#pragma once

#include "../ieffectsconfiguration.h"

#include "modularity/ioc.h"
#include "global/iglobalconfiguration.h"

namespace au::effects {
class EffectsConfiguration : public IEffectsConfiguration
{
    muse::GlobalInject<muse::IGlobalConfiguration> globalConfiguration;

public:
    EffectsConfiguration() = default;

    void init();

    bool applyEffectToAllAudio() const override;
    void setApplyEffectToAllAudio(bool value) override;
    muse::async::Notification applyEffectToAllAudioChanged() const override;

    EffectMenuOrganization effectMenuOrganization() const override;
    void setEffectMenuOrganization(EffectMenuOrganization) override;
    muse::async::Notification effectMenuOrganizationChanged() const override;

    double previewMaxDuration() const override;
    void setPreviewMaxDuration(double value) override;

    EffectUIMode effectUIMode(const EffectId& effectId) const override;
    void setEffectUIMode(const EffectId& effectId, EffectUIMode mode) override;
    muse::async::Notification effectUIModeChanged() const override;

    std::string lastUsedPreset(const EffectId& effectId) const override;
    void setLastUsedPreset(const EffectId& effectId, const std::string& presetId) override;

    muse::io::paths_t lv2CustomPaths() const override;
    void setLv2CustomPaths(const muse::io::paths_t& paths) override;
    muse::async::Notification lv2CustomPathsChanged() const override;

    muse::io::paths_t vst3CustomPaths() const override;
    void setVst3CustomPaths(const muse::io::paths_t& paths) override;
    muse::async::Notification vst3CustomPathsChanged() const override;

    std::vector<EffectId> djVisibleEffects() const override;
    std::vector<EffectId> djHiddenEffects() const override;
    void setDjEffectLists(const std::vector<EffectId>& visible, const std::vector<EffectId>& hidden) override;
    muse::async::Notification djEffectListsChanged() const override;
    bool djKeepMetaknobPosition() const override;
    void setDjKeepMetaknobPosition(bool keep) override;
    muse::async::Notification djKeepMetaknobPositionChanged() const override;
    double djLowEqFrequency() const override;
    double djHighEqFrequency() const override;
    void setDjEqFrequencies(double low, double high) override;
    muse::async::Notification djEqFrequenciesChanged() const override;
    bool djEffectListsImported() const override;
    void setDjEffectListsImported(bool imported) override;

private:
    muse::async::Notification m_applyEffectToAllAudioChanged;
    muse::async::Notification m_effectMenuOrganizationChanged;
    muse::async::Notification m_effectUIModeChanged;
    muse::async::Notification m_lv2CustomPathsChanged;
    muse::async::Notification m_vst3CustomPathsChanged;
    muse::async::Notification m_djEffectListsChanged;
    muse::async::Notification m_djKeepMetaknobPositionChanged;
    muse::async::Notification m_djEqFrequenciesChanged;
};
}
