/*
 * GlideMix
 */
#include "mixxxeffect.h"

#include <algorithm>
#include <cmath>

#include "au3-components/EffectAutomationParameters.h"
#include "au3-components/ParameterAutomationSnapshot.h"
#include "au3-components/SettingsVisitor.h"
#include "au3-mixer/PlaybackSpeedCurve.h"
#include "log.h"

using namespace au::effects;

namespace {
// Parameter changes follow automation in steps this short (Mixxx effects ramp
// their own parameters across each buffer, so steps don't click).
constexpr size_t kAutomationStepFrames = 64;

double BeatLengthSeconds(const ParameterAutomationTimes* times)
{
    // The timeline is in bars at the project tempo; the tempo lane can play
    // it faster or slower.
    const double tempo = PlaybackSpeedCurve::ProjectTempo();
    if (tempo <= 0.0) {
        return 0.0;
    }
    double speed = 1.0;
    if (times) {
        if (const auto curve = PlaybackSpeedCurve::Live()) {
            speed = std::max(0.01, curve->SpeedAt(times->At(0)));
        }
    }
    return 60.0 / (tempo * speed);
}

//! Beat phase (0..1) at timeline time `time`, or -1 when unknown.
double BeatFraction(double time)
{
    const double tempo = PlaybackSpeedCurve::ProjectTempo();
    if (tempo <= 0.0) {
        return -1.0;
    }
    const double beats = (time - PlaybackSpeedCurve::BeatOrigin()) * tempo / 60.0;
    return beats - std::floor(beats);
}
}

// Part of every DJ effect's id ("Effect_Builtin_<vendor>_<symbol>_<path>"),
// which projects and the effect lists save: renaming it orphans the effects in
// every saved project. Renamed twice before any real project used a DJ
// effect: "Mixxx", then "Mixtape Studio", then "GlideMix" with the app's own
// name (2026-09). utils::isMixxxDjEffect() matches on it too.
const wxString MixxxEffectBase::Vendor = wxT("GlideMix");
const char* MixxxEffectBase::MetaKey = "meta";

bool operator==(const MixxxEffectSettings& a, const MixxxEffectSettings& b)
{
    return a.values == b.values && a.meta == b.meta;
}

MixxxEffectBase::MixxxEffectBase(const char* mixxxId, EffectGroup group)
    : mInfo(mixxxeffects::findEffect(mixxxId)), mGroup(group)
{
    SetLinearEffectFlag(true);
    if (mInfo) {
        for (const auto& p : mInfo->parameters) {
            mKeys.push_back(wxString::FromUTF8(p.id.c_str()));
        }
    }
}

MixxxEffectBase::~MixxxEffectBase() = default;

ComponentInterfaceSymbol MixxxEffectBase::GetSymbol() const
{
    // The internal name keeps these apart from the editor's own Echo and
    // Reverb; the DJ Effects menu shows "DJ Reverb" where a name clashes. It is
    // an id -- saved projects and the visible-effects lists store it -- so it
    // stays as it is; only what the user reads is our own name.
    const wxString name = wxString::FromUTF8(mInfo ? mInfo->name.c_str() : "Effect");
    return { wxT("Mixxx ") + name, au3::untranslatable(name) };
}

VendorSymbol MixxxEffectBase::GetVendor() const
{
    return VendorSymbol { Vendor };
}

wxString MixxxEffectBase::GetVersion() const
{
    return wxString::FromUTF8(mInfo ? mInfo->version.c_str() : "1.0");
}

TranslatableString MixxxEffectBase::GetDescription() const
{
    return au3::untranslatable(wxString::FromUTF8(mInfo ? mInfo->description.c_str() : ""));
}

ManualPageID MixxxEffectBase::ManualPage() const
{
    return {};
}

::EffectType MixxxEffectBase::GetType() const
{
    return EffectTypeProcess;
}

auto MixxxEffectBase::RealtimeSupport() const -> RealtimeSince
{
    return RealtimeSince::Always;
}

EffectSettings MixxxEffectBase::MakeSettings() const
{
    auto settings = EffectSettings::Make<MixxxEffectSettings>();
    LoadFactoryDefaults(settings);
    return settings;
}

OptionalMessage MixxxEffectBase::LoadFactoryDefaults(EffectSettings& settings) const
{
    auto& s = GetSettings(settings);
    if (mInfo) {
        for (size_t i = 0; i < mInfo->parameters.size() && i < MixxxEffectSettings::kMaxParameters; ++i) {
            s.values[i] = mInfo->parameters[i].defaultValue;
        }
        // Mixxx loads an effect with its metaknob default, which sets the
        // linked parameters (EffectSlot::loadEffectInner).
        ApplyMeta(s, mInfo->metaknobDefault);
    }
    return { nullptr };
}

void MixxxEffectBase::ApplyMeta(MixxxEffectSettings& s, double meta) const
{
    s.meta = std::clamp(meta, 0.0, 1.0);
    for (size_t i = 0; mInfo && i < mInfo->parameters.size() && i < MixxxEffectSettings::kMaxParameters; ++i) {
        const auto& p = mInfo->parameters[i];
        const double normalized = p.linkedNormalized(s.meta);
        if (normalized >= 0.0) {
            s.values[i] = p.fromNormalized(normalized);
        }
    }
}

void MixxxEffectBase::MoveMeta(MixxxEffectSettings& s, double oldMeta, double meta) const
{
    // SoftTakeover::kDefaultTakeoverThreshold
    constexpr double kTakeoverThreshold = 3.0 / 128;
    s.meta = std::clamp(meta, 0.0, 1.0);
    for (size_t i = 0; mInfo && i < mInfo->parameters.size() && i < MixxxEffectSettings::kMaxParameters; ++i) {
        const auto& p = mInfo->parameters[i];
        const double target = p.linkedNormalized(s.meta);
        if (target < 0.0) {
            continue;
        }
        const double previous = p.linkedNormalized(std::clamp(oldMeta, 0.0, 1.0));
        const double current = p.toNormalized(s.values[i]);
        const bool following = std::abs(current - previous) <= kTakeoverThreshold
                               || std::abs(current - target) <= kTakeoverThreshold
                               || (current - previous) * (current - target) < 0.0;   // the knob passed it
        if (following) {
            s.values[i] = p.fromNormalized(target);
        }
    }
}

bool MixxxEffectBase::VisitSettings(SettingsVisitor& visitor, EffectSettings& settings)
{
    auto& s = GetSettings(settings);
    for (size_t i = 0; mInfo && i < mInfo->parameters.size() && i < MixxxEffectSettings::kMaxParameters; ++i) {
        const auto& p = mInfo->parameters[i];
        visitor.Define(s.values[i], mKeys[i].wc_str(), p.defaultValue, p.minimum, p.maximum, 1.0);
    }
    if (mInfo) {
        visitor.Define(s.meta, wxString(MetaKey).wc_str(), mInfo->metaknobDefault, 0.0, 1.0, 1.0);
    }
    return true;
}

bool MixxxEffectBase::VisitSettings(ConstSettingsVisitor& visitor, const EffectSettings& settings) const
{
    const auto& s = GetSettings(settings);
    for (size_t i = 0; mInfo && i < mInfo->parameters.size() && i < MixxxEffectSettings::kMaxParameters; ++i) {
        const auto& p = mInfo->parameters[i];
        visitor.Define(s.values[i], mKeys[i].wc_str(), p.defaultValue, p.minimum, p.maximum, 1.0);
    }
    if (mInfo) {
        visitor.Define(s.meta, wxString(MetaKey).wc_str(), mInfo->metaknobDefault, 0.0, 1.0, 1.0);
    }
    return true;
}

bool MixxxEffectBase::SaveSettings(const EffectSettings& settings, CommandParameters& parms) const
{
    const auto& s = GetSettings(settings);
    for (size_t i = 0; mInfo && i < mInfo->parameters.size() && i < MixxxEffectSettings::kMaxParameters; ++i) {
        parms.Write(mKeys[i], s.values[i]);
    }
    parms.Write(MetaKey, s.meta);
    return true;
}

bool MixxxEffectBase::LoadSettings(const CommandParameters& parms, EffectSettings& settings) const
{
    auto& s = GetSettings(settings);
    if (!mInfo) {
        return true;
    }
    // A saved meta knob sets the linked parameters the settings don't name
    // themselves (a chain preset names them all; "keep metaknob position"
    // hands over only the knob).
    double meta = mInfo->metaknobDefault;
    parms.Read(MetaKey, &meta, mInfo->metaknobDefault);
    ApplyMeta(s, meta);
    for (size_t i = 0; i < mInfo->parameters.size() && i < MixxxEffectSettings::kMaxParameters; ++i) {
        const auto& p = mInfo->parameters[i];
        double value = 0.0;
        if (parms.Read(mKeys[i], &value)) {
            s.values[i] = std::clamp(value, p.minimum, p.maximum);
        } else if (p.linkedNormalized(s.meta) < 0.0) {
            s.values[i] = p.defaultValue;
        }
    }
    return true;
}

std::shared_ptr<::EffectInstance> MixxxEffectBase::MakeInstance() const
{
    return std::make_shared<MixxxEffectInstance>(*this);
}

MixxxEffectInstance::MixxxEffectInstance(const MixxxEffectBase& effect)
    : PerTrackEffect::Instance(effect), mEffect(effect)
{
    const auto& params = effect.Info().parameters;
    for (size_t i = 0; i < mCurrentValues.size(); ++i) {
        mCurrentValues[i].store(i < params.size() ? params[i].defaultValue : 0.0, std::memory_order_relaxed);
    }
    mCurrentMeta.store(effect.Info().metaknobDefault, std::memory_order_relaxed);
}

MixxxEffectInstance::~MixxxEffectInstance() = default;

bool MixxxEffectInstance::ProcessInitialize(EffectSettings&, double sampleRate, ChannelNames)
{
    mOffline.dsp = mixxxeffects::createProcessor(mEffect.Info().id);
    mOffline.sampleRate = sampleRate;
    return bool(mOffline.dsp);
}

bool MixxxEffectInstance::ProcessFinalize() noexcept
{
    mOffline.dsp.reset();
    return true;
}

size_t MixxxEffectInstance::ProcessBlock(EffectSettings& settings, const float* const* inBlock, float* const* outBlock,
                                         size_t blockLen)
{
    return Process(mOffline, settings, inBlock, outBlock, blockLen);
}

bool MixxxEffectInstance::RealtimeInitialize(EffectSettings&, double, size_t)
{
    SetBlockSize(512);
    mRealtime.clear();
    return true;
}

bool MixxxEffectInstance::RealtimeAddProcessor(EffectSettings&, EffectOutputs*, unsigned, float sampleRate)
{
    Processor processor;
    processor.dsp = mixxxeffects::createProcessor(mEffect.Info().id);
    processor.sampleRate = sampleRate;
    if (!processor.dsp) {
        return false;
    }
    mRealtime.push_back(std::move(processor));
    return true;
}

bool MixxxEffectInstance::RealtimeFinalize(EffectSettings&) noexcept
{
    mRealtime.clear();
    return true;
}

size_t MixxxEffectInstance::RealtimeProcess(size_t group, EffectSettings& settings, const float* const* inbuf,
                                            float* const* outbuf, size_t numSamples)
{
    if (group >= mRealtime.size()) {
        return 0;
    }
    return Process(mRealtime[group], settings, inbuf, outbuf, numSamples);
}

size_t MixxxEffectInstance::Process(Processor& processor, const EffectSettings& settings, const float* const* inBlock,
                                    float* const* outBlock, size_t blockLen)
{
    if (!processor.dsp) {
        return 0;
    }
    const auto& info = mEffect.Info();
    const auto& s = MixxxEffectBase::GetSettings(settings);
    const size_t parameterCount = std::min(info.parameters.size(), MixxxEffectSettings::kMaxParameters);
    const auto* times = GetParameterAutomationTimes();
    const auto automation = std::atomic_load_explicit(&mAutomation, std::memory_order_acquire);
    const bool automated = times && automation && automation->snapshot
                           && (!automation->bindings.empty() || automation->metaBinding);

    processor.interleaved.resize(blockLen * 2);
    for (size_t i = 0; i < blockLen; ++i) {
        processor.interleaved[2 * i] = inBlock[0][i];
        processor.interleaved[2 * i + 1] = inBlock[1] ? inBlock[1][i] : inBlock[0][i];
    }

    const double beatLength = BeatLengthSeconds(times);
    const size_t step = automated ? kAutomationStepFrames : blockLen;
    for (size_t offset = 0; offset < blockLen; offset += step) {
        const size_t frames = std::min(step, blockLen - offset);
        std::array<double, MixxxEffectSettings::kMaxParameters> values = s.values;
        double meta = s.meta;
        if (automated) {
            const double time = times->At(offset);
            // An automated meta knob moves its linked parameters; a
            // parameter's own automation line still wins.
            if (automation->metaBinding) {
                const auto value = automation->snapshot->Evaluate(*automation->metaBinding, time);
                if (value.status == ParameterAutomationSnapshot::Status::Active) {
                    meta = std::clamp(value.normalized, 0.0, 1.0);
                    for (size_t i = 0; i < parameterCount; ++i) {
                        const double linked = info.parameters[i].linkedNormalized(meta);
                        if (linked >= 0.0) {
                            values[i] = info.parameters[i].fromNormalized(linked);
                        }
                    }
                }
            }
            for (const auto& [snapshotIndex, parameterIndex] : automation->bindings) {
                const auto value = automation->snapshot->Evaluate(snapshotIndex, time);
                if (value.status == ParameterAutomationSnapshot::Status::Active) {
                    values[parameterIndex] = info.parameters[parameterIndex].fromNormalized(value.normalized);
                }
            }
        }
        for (size_t i = 0; i < parameterCount; ++i) {
            processor.dsp->setParameter(i, values[i]);
            mCurrentValues[i].store(values[i], std::memory_order_relaxed);
        }
        mCurrentMeta.store(meta, std::memory_order_relaxed);
        const double beatFraction = times ? BeatFraction(times->At(offset + frames - 1) + 1.0 / processor.sampleRate)
                                    : -1.0;
        processor.dsp->process(processor.interleaved.data() + 2 * offset, processor.interleaved.data() + 2 * offset,
                               frames, processor.sampleRate, beatLength, beatFraction);
    }

    for (size_t i = 0; i < blockLen; ++i) {
        outBlock[0][i] = processor.interleaved[2 * i];
        if (outBlock[1]) {
            outBlock[1][i] = processor.interleaved[2 * i + 1];
        }
    }
    return blockLen;
}

std::vector<EffectInstance::AutomationParameter> MixxxEffectInstance::AutomationParameters() const
{
    std::vector<AutomationParameter> result;
    const auto& params = mEffect.Info().parameters;
    for (size_t i = 0; i < params.size() && i < MixxxEffectSettings::kMaxParameters; ++i) {
        const auto& p = params[i];
        result.push_back({ p.id, p.name, p.toNormalized(mCurrentValues[i].load(std::memory_order_relaxed)) });
    }
    // The meta knob first, as Mixxx shows it.
    result.insert(result.begin(), { MixxxEffectBase::MetaKey, "Meta", mCurrentMeta.load(std::memory_order_relaxed) });
    return result;
}

std::string MixxxEffectInstance::InteractionParameterId(uint32_t id) const
{
    const auto& params = mEffect.Info().parameters;
    if (id == 0) {
        return MixxxEffectBase::MetaKey;
    }
    return id <= params.size() ? params[id - 1].id : std::to_string(id);
}

bool MixxxEffectInstance::ConfigureParameterAutomation(std::shared_ptr<const ParameterAutomationSnapshot> snapshot)
{
    auto automation = std::make_shared<Automation>();
    if (snapshot) {
        const auto& params = mEffect.Info().parameters;
        for (size_t i = 0; i < snapshot->ParameterCount(); ++i) {
            const auto& id = snapshot->ParameterId(i);
            if (id == MixxxEffectBase::MetaKey) {
                automation->metaBinding = i;
                continue;
            }
            const auto it = std::find_if(params.begin(), params.end(), [&](const auto& p) { return p.id == id; });
            if (it == params.end()) {
                LOGW() << "DJ effect " << mEffect.Info().name << " has no parameter " << id << " for its automation";
                return false;
            }
            automation->bindings.emplace_back(i, static_cast<size_t>(it - params.begin()));
        }
        automation->snapshot = std::move(snapshot);
    }
    std::atomic_store_explicit(&mAutomation, std::shared_ptr<const Automation>(std::move(automation)),
                               std::memory_order_release);
    return true;
}
