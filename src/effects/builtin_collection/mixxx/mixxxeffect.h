/*
 * GlideMix
 *
 * Mixxx's built-in effects as GlideMix effects: usable on a track
 * (realtime) or rendered, with every parameter automatable on a clip's
 * automation line. One class per Mixxx effect, all sharing this base; the DSP
 * is Mixxx's own code behind mixxxeffectsdsp.h.
 */
#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "au3-effects/Effect.h"
#include "au3-effects/PerTrackEffect.h"

#include "mixxxeffectsdsp.h"

struct MixxxEffectSettings {
    static constexpr size_t kMaxParameters = 16;
    std::array<double, kMaxParameters> values {};
    //! The meta knob (0..1): moves the parameters linked to it.
    double meta = 0.0;
};

bool operator==(const MixxxEffectSettings& a, const MixxxEffectSettings& b);
inline bool operator!=(const MixxxEffectSettings& a, const MixxxEffectSettings& b) { return !(a == b); }

class ParameterAutomationSnapshot;

namespace au::effects {
class MixxxEffectBase : public EffectWithSettings<MixxxEffectSettings, PerTrackEffect>
{
public:
    //! The vendor every Mixxx effect is registered under; also marks their ids.
    static const wxString Vendor;

    MixxxEffectBase(const char* mixxxId, EffectGroup group);
    ~MixxxEffectBase() override;

    const mixxxeffects::EffectInfo& Info() const { return *mInfo; }

    //! Settings key and automation id of the meta knob.
    static const char* MetaKey;
    //! Sets every parameter linked to the meta knob from `meta`, as Mixxx
    //! does when an effect loads or the knob moves with `force`.
    void ApplyMeta(MixxxEffectSettings& settings, double meta) const;
    //! The meta knob moved from `oldMeta` to `meta`: linked parameters follow,
    //! except ones set by hand until the knob reaches them (Mixxx's metaknob
    //! soft takeover).
    void MoveMeta(MixxxEffectSettings& settings, double oldMeta, double meta) const;

    // ComponentInterface
    ComponentInterfaceSymbol GetSymbol() const override;
    VendorSymbol GetVendor() const override;
    wxString GetVersion() const override;
    TranslatableString GetDescription() const override;
    ManualPageID ManualPage() const override;

    // EffectDefinitionInterface
    ::EffectType GetType() const override;
    EffectGroup GetGroup() const override { return mGroup; }
    RealtimeSince RealtimeSupport() const override;

    EffectSettings MakeSettings() const override;
    bool VisitSettings(SettingsVisitor& visitor, EffectSettings& settings) override;
    bool VisitSettings(ConstSettingsVisitor& visitor, const EffectSettings& settings) const override;
    bool SaveSettings(const EffectSettings& settings, CommandParameters& parms) const override;
    bool LoadSettings(const CommandParameters& parms, EffectSettings& settings) const override;
    OptionalMessage LoadFactoryDefaults(EffectSettings& settings) const override;

    std::shared_ptr<::EffectInstance> MakeInstance() const override;

private:
    const mixxxeffects::EffectInfo* mInfo = nullptr;
    const EffectGroup mGroup;
    std::vector<wxString> mKeys;   // settings keys, one per parameter (the Mixxx parameter ids)
};

class MixxxEffectInstance final : public PerTrackEffect::Instance, public EffectInstanceWithBlockSize
{
public:
    explicit MixxxEffectInstance(const MixxxEffectBase& effect);
    ~MixxxEffectInstance() override;

    bool ProcessInitialize(EffectSettings& settings, double sampleRate, ChannelNames chanMap) override;
    bool ProcessFinalize() noexcept override;
    size_t ProcessBlock(EffectSettings& settings, const float* const* inBlock, float* const* outBlock,
                        size_t blockLen) override;

    bool RealtimeInitialize(EffectSettings& settings, double sampleRate, size_t audioThreadBufferSize) override;
    bool RealtimeAddProcessor(EffectSettings& settings, EffectOutputs* pOutputs, unsigned numChannels,
                              float sampleRate) override;
    bool RealtimeFinalize(EffectSettings& settings) noexcept override;
    size_t RealtimeProcess(size_t group, EffectSettings& settings, const float* const* inbuf, float* const* outbuf,
                           size_t numSamples) override;

    unsigned GetAudioInCount() const override { return 2; }
    unsigned GetAudioOutCount() const override { return 2; }

    bool SupportsParameterAutomation() const noexcept override { return true; }
    std::vector<AutomationParameter> AutomationParameters() const override;
    //! 0 is the meta knob, n the n-th parameter (MixxxEffectViewModel reports index + 1).
    std::string InteractionParameterId(uint32_t id) const override;
    bool ConfigureParameterAutomation(std::shared_ptr<const ParameterAutomationSnapshot> snapshot) override;

private:
    struct Processor {
        std::unique_ptr<mixxxeffects::Processor> dsp;
        double sampleRate = 44100.0;
        std::vector<float> interleaved;
    };
    struct Automation {
        std::shared_ptr<const ParameterAutomationSnapshot> snapshot;
        std::vector<std::pair<size_t /*snapshot parameter*/, size_t /*effect parameter*/> > bindings;
        std::optional<size_t> metaBinding;   // snapshot parameter of the meta knob
    };

    size_t Process(Processor& processor, const EffectSettings& settings, const float* const* inBlock,
                   float* const* outBlock, size_t blockLen);

    const MixxxEffectBase& mEffect;
    Processor mOffline;
    std::vector<Processor> mRealtime;
    std::shared_ptr<const Automation> mAutomation;   // atomic_load / atomic_store
    std::array<std::atomic<double>, MixxxEffectSettings::kMaxParameters> mCurrentValues;
    std::atomic<double> mCurrentMeta { 0.0 };
};
}
