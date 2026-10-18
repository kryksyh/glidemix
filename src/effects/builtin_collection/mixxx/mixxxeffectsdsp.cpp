/*
 * GlideMix
 */
#include "mixxxeffectsdsp.h"

#include <algorithm>
#include <cmath>
#include <functional>

#include <atomic>
#include <map>
#include <mutex>

#include "control/mixtapecontrols.h"
#include "effects/backends/builtin/autopaneffect.h"
#include "effects/backends/builtin/balanceeffect.h"
#include "effects/backends/builtin/bessel4lvmixeqeffect.h"
#include "effects/backends/builtin/bessel8lvmixeqeffect.h"
#include "effects/backends/builtin/biquadfullkilleqeffect.h"
#include "effects/backends/builtin/bitcrushereffect.h"
#include "effects/backends/builtin/compressoreffect.h"
#include "effects/backends/builtin/distortioneffect.h"
#include "effects/backends/builtin/echoeffect.h"
#include "effects/backends/builtin/filtereffect.h"
#include "effects/backends/builtin/flangereffect.h"
#include "effects/backends/builtin/glitcheffect.h"
#include "effects/backends/builtin/glidemixdjeffects.h"
#include "effects/backends/builtin/graphiceqeffect.h"
#include "effects/backends/builtin/linkwitzriley8eqeffect.h"
#include "effects/backends/builtin/loudnesscontoureffect.h"
#include "effects/backends/builtin/metronomeeffect.h"
#include "effects/backends/builtin/moogladder4filtereffect.h"
#include "effects/backends/builtin/parametriceqeffect.h"
#include "effects/backends/builtin/phasereffect.h"
#ifdef MIXTAPE_HAS_RUBBERBAND
#include "effects/backends/builtin/pitchshifteffect.h"
#endif
#include "effects/backends/builtin/reverbeffect.h"
#include "effects/backends/builtin/threebandbiquadeqeffect.h"
#include "effects/backends/builtin/tremoloeffect.h"
#include "effects/backends/builtin/whitenoiseeffect.h"
#include "effects/backends/effectmanifest.h"
#include "engine/effects/engineeffectparameter.h"

namespace mixxxeffects {
namespace {
// Mixxx's engine hands effects at most this many frames per callback; the
// states' work buffers are sized for it.
constexpr size_t kMaxFramesPerBuffer = 1024;

struct Entry {
    EffectManifestPointer manifest;
    std::function<std::unique_ptr<EffectProcessor>()> create;
};

template<typename T>
Entry entry()
{
    return { T::getManifest(), []() { return std::unique_ptr<EffectProcessor>(new T()); } };
}

const std::vector<Entry>& registry()
{
    // Mixxx's BuiltInBackend order.
    static const std::vector<Entry> entries = {
        entry<Bessel4LVMixEQEffect>(),
        entry<Bessel8LVMixEQEffect>(),
        entry<LinkwitzRiley8EQEffect>(),
        entry<ThreeBandBiquadEQEffect>(),
        entry<BiquadFullKillEQEffect>(),
        entry<GraphicEQEffect>(),
        entry<ParametricEQEffect>(),
        entry<LoudnessContourEffect>(),
        entry<FilterEffect>(),
        entry<MoogLadder4FilterEffect>(),
        entry<BitCrusherEffect>(),
        entry<WhiteNoiseEffect>(),
        entry<BalanceEffect>(),
        entry<FlangerEffect>(),
        entry<EchoEffect>(),
        entry<AutoPanEffect>(),
        entry<ReverbEffect>(),
        entry<PhaserEffect>(),
        entry<MetronomeEffect>(),
        entry<TremoloEffect>(),
#ifdef MIXTAPE_HAS_RUBBERBAND
        entry<PitchShiftEffect>(),
#endif
        entry<DistortionEffect>(),
        entry<GlitchEffect>(),
        entry<CompressorEffect>(),
        // GlideMix's own mixer beat effects.
        entry<GlideMixPingPongEffect>(),
        entry<GlideMixDubEchoEffect>(),
        entry<GlideMixRollEffect>(),
        entry<GlideMixTransEffect>(),
        entry<GlideMixSweepEffect>(),
        entry<GlideMixTapeStopEffect>(),
    };
    return entries;
}

Scale toScale(EffectManifestParameter::ValueScaler scaler)
{
    switch (scaler) {
    case EffectManifestParameter::ValueScaler::Logarithmic: return Scale::Logarithmic;
    case EffectManifestParameter::ValueScaler::LogarithmicInverse: return Scale::LogarithmicInverse;
    case EffectManifestParameter::ValueScaler::Integral: return Scale::Integral;
    case EffectManifestParameter::ValueScaler::Toggle: return Scale::Toggle;
    default: return Scale::Linear;
    }
}

Link toLink(EffectManifestParameter::LinkType type)
{
    switch (type) {
    case EffectManifestParameter::LinkType::Linked: return Link::Linked;
    case EffectManifestParameter::LinkType::LinkedLeft: return Link::LinkedLeft;
    case EffectManifestParameter::LinkType::LinkedRight: return Link::LinkedRight;
    case EffectManifestParameter::LinkType::LinkedLeftRight: return Link::LinkedLeftRight;
    default: return Link::None;
    }
}

Units toUnits(EffectManifestParameter::UnitsHint hint)
{
    switch (hint) {
    case EffectManifestParameter::UnitsHint::Hertz: return Units::Hertz;
    case EffectManifestParameter::UnitsHint::SampleRate: return Units::SampleRate;
    case EffectManifestParameter::UnitsHint::Beats: return Units::Beats;
    case EffectManifestParameter::UnitsHint::Millisecond: return Units::Millisecond;
    case EffectManifestParameter::UnitsHint::Semitones: return Units::Semitones;
    case EffectManifestParameter::UnitsHint::Coefficient: return Units::Coefficient;
    case EffectManifestParameter::UnitsHint::Decibel: return Units::Decibel;
    case EffectManifestParameter::UnitsHint::Time: return Units::Time;
    default: return Units::Unknown;
    }
}

class ProcessorImpl final : public Processor
{
public:
    ProcessorImpl(const Entry& e, const EffectInfo& info)
        : m_info(info), m_processor(e.create())
    {
        QMap<QString, EngineEffectParameterPointer> parameters;
        for (const auto& p : e.manifest->parameters()) {
            auto parameter = std::make_shared<EngineEffectParameter>(p);
            parameters.insert(p->id(), parameter);
            m_parameters.push_back(parameter);
        }
        m_processor->loadEngineEffectParameters(parameters);
        m_dry.resize(kMaxFramesPerBuffer * 2);
    }

    void setParameter(size_t index, double value) override
    {
        if (index < m_parameters.size()) {
            const auto& p = m_info.parameters[index];
            m_parameters[index]->setValue(std::clamp(value, p.minimum, p.maximum));
        }
    }

    void process(const float* in, float* out, size_t frames, double sampleRate, double beatLengthSeconds,
                 double beatFractionAtEnd) override
    {
        GroupFeatureState features;
        if (beatLengthSeconds > 0.0) {
            features.beat_length = GroupFeatureBeatLength { beatLengthSeconds, 1.0 };
            if (beatFractionAtEnd >= 0.0 && frames > 0) {
                // Mixxx gives the fraction at the end of each engine buffer;
                // step it back for the chunks before the last one.
                const double beatFrames = beatLengthSeconds * sampleRate;
                double fraction = beatFractionAtEnd - double(frames - std::min(frames, kMaxFramesPerBuffer)) / beatFrames;
                features.beat_fraction_buffer_end = fraction - std::floor(fraction);
            }
        }
        while (frames > 0) {
            const size_t n = std::min(frames, kMaxFramesPerBuffer);
            const mixxx::EngineParameters engine(mixxx::audio::SampleRate(sampleRate), static_cast<SINT>(n));
            // Mixxx effects need input and output to be different buffers.
            std::copy(in, in + n * 2, m_dry.begin());
            m_processor->process(m_dry.data(), out, engine, static_cast<SINT>(kMaxFramesPerBuffer),
                                 m_started ? EffectEnableState::Enabled : EffectEnableState::Enabling, features);
            m_started = true;
            if (m_info.addDryToWet) {
                for (size_t i = 0; i < n * 2; ++i) {
                    out[i] += m_dry[i];
                }
            }
            in += n * 2;
            out += n * 2;
            frames -= n;
            if (features.beat_fraction_buffer_end && frames > 0) {
                const double fraction = *features.beat_fraction_buffer_end
                                        + double(std::min(frames, kMaxFramesPerBuffer)) / (beatLengthSeconds * sampleRate);
                features.beat_fraction_buffer_end = fraction - std::floor(fraction);
            }
        }
    }

    void reset() override
    {
        m_processor->reset();
        m_started = false;
    }

private:
    const EffectInfo& m_info;
    std::unique_ptr<EffectProcessor> m_processor;
    std::vector<EngineEffectParameterPointer> m_parameters;
    std::vector<float> m_dry;
    bool m_started = false;
};
} // namespace

namespace {
double db2ratio(double db) { return std::pow(10.0, db / 20.0); }
double ratio2db(double ratio) { return std::log10(ratio) * 20.0; }

// Mixxx's ControlLogPotmeterBehavior (control/controlbehavior.cpp).
struct LogPot {
    double minimum, maximum, minDb;
    double valueToParameter(double value) const
    {
        const double range = maximum - minimum;
        if (range == 0.0) {
            return 0.0;
        }
        value = std::clamp(value, minimum, maximum);
        const double minOffset = db2ratio(minDb);
        const double linParameter = (value - minimum) / range;
        const double dbParameter = ratio2db(linParameter + minOffset * (1 - linParameter));
        return 1 - (dbParameter / minDb);
    }
    double parameterToValue(double parameter) const
    {
        const double minOffset = db2ratio(minDb);
        const double dbParameter = (1 - parameter) * minDb;
        const double linParameter = (db2ratio(dbParameter) - minOffset) / (1 - minOffset);
        return minimum + (linParameter * (maximum - minimum));
    }
};

// Mixxx's ControlAudioTaperPotBehavior (control/controlbehavior.cpp).
struct AudioTaperPot {
    double minDb, maxDb, neutral;
    double maxValue() const { return db2ratio(maxDb); }
    double valueToParameter(double value) const
    {
        const double offset = db2ratio(minDb);
        if (value <= 0.0) {
            return 0.0;
        } else if (value < 1.0) {
            const double overlay = offset * (1 - value);
            return minDb != 0 ? (ratio2db(value + overlay) - minDb) / minDb * neutral * -1 : value * neutral;
        } else if (value == 1.0) {
            return neutral;
        } else if (value < maxValue()) {
            return (ratio2db(value) / maxDb * (1 - neutral)) + neutral;
        }
        return 1.0;
    }
    double parameterToValue(double parameter) const
    {
        const double offset = db2ratio(minDb);
        if (parameter <= 0.0) {
            return 0.0;
        } else if (parameter < neutral) {
            if (minDb != 0) {
                const double db = (parameter * minDb / (neutral * -1)) + minDb;
                return (db2ratio(db) - offset) / (1 - offset);
            }
            return parameter / neutral;
        } else if (parameter == neutral) {
            return 1.0;
        } else if (parameter < 1.0) {
            return db2ratio((parameter - neutral) * maxDb / (1 - neutral));
        }
        return maxValue();
    }
};
}

// ControlEffectKnob::setBehaviour picks the knob curve from the scaler and range.
double ParameterInfo::toNormalized(double value) const
{
    if (maximum <= minimum) {
        return 0.0;
    }
    value = std::clamp(value, minimum, maximum);
    double parameter = 0.0;
    switch (scale) {
    case Scale::Toggle:
        return value > 0.0 ? 1.0 : 0.0;
    case Scale::Logarithmic:
        if (minimum == 0.0 && maximum == 1.0) {
            parameter = AudioTaperPot { -20, 0, 1 }.valueToParameter(value);
        } else if (minimum == 0.0 && maximum > 1.0) {
            parameter = AudioTaperPot { -12, ratio2db(maximum), 0.5 }.valueToParameter(value);
        } else {
            parameter = LogPot { minimum, maximum, -40 }.valueToParameter(value);
        }
        break;
    case Scale::LogarithmicInverse:
        parameter = 1 - LogPot { minimum, maximum, -40 }.valueToParameter(value);
        break;
    default:
        parameter = (value - minimum) / (maximum - minimum);
        break;
    }
    return std::clamp(parameter, 0.0, 1.0);
}

double ParameterInfo::linkedNormalized(double parameter) const
{
    // Mixxx's EffectKnobParameterSlot::onEffectMetaParameterChanged.
    if (parameter < 0.0 || parameter > 1.0) {
        return -1.0;
    }
    double neutral = neutralPoint;
    switch (link) {
    case Link::Linked:
        if (neutral > 0.0 && neutral < 1.0) {
            if (linkInverted) {
                neutral = 1.0 - neutral;   // the neutral position must stick where it is
            }
            // Knob is already a split knob: match to center position of meta knob
            if (parameter <= 0.5) {
                parameter /= 0.5;
                parameter *= neutral;
            } else {
                parameter -= 0.5;
                parameter /= 0.5;
                parameter *= 1 - neutral;
                parameter += neutral;
            }
        }
        break;
    case Link::LinkedLeft:
        parameter = parameter >= 0.5 ? 1.0 : parameter * 2;
        break;
    case Link::LinkedRight:
        parameter = parameter >= 0.5 ? (parameter - 0.5) * 2 : 0.0;
        break;
    case Link::LinkedLeftRight:
        parameter = parameter >= 0.5 ? (parameter - 0.5) * 2 : 1.0 - parameter * 2;
        break;
    case Link::None:
    default:
        return -1.0;
    }
    return linkInverted ? 1.0 - parameter : parameter;
}

double ParameterInfo::fromNormalized(double normalized) const
{
    normalized = std::clamp(normalized, 0.0, 1.0);
    double value = minimum;
    switch (scale) {
    case Scale::Toggle:
        return normalized >= 0.5 ? maximum : minimum;
    case Scale::Logarithmic:
        if (minimum == 0.0 && maximum == 1.0) {
            value = AudioTaperPot { -20, 0, 1 }.parameterToValue(normalized);
        } else if (minimum == 0.0 && maximum > 1.0) {
            value = AudioTaperPot { -12, ratio2db(maximum), 0.5 }.parameterToValue(normalized);
        } else {
            value = LogPot { minimum, maximum, -40 }.parameterToValue(normalized);
        }
        break;
    case Scale::LogarithmicInverse:
        value = LogPot { minimum, maximum, -40 }.parameterToValue(1 - normalized);
        break;
    case Scale::Integral:
        value = std::round(minimum + normalized * (maximum - minimum));
        break;
    default:
        value = minimum + normalized * (maximum - minimum);
        break;
    }
    return std::clamp(value, minimum, maximum);
}

const std::vector<EffectInfo>& effects()
{
    static const std::vector<EffectInfo> infos = [] {
        std::vector<EffectInfo> result;
        for (const Entry& e : registry()) {
            EffectInfo info;
            info.id = e.manifest->id().toStdString();
            info.name = e.manifest->name().toStdString();
            info.description = e.manifest->description().toStdString();
            info.author = e.manifest->author().toStdString();
            info.version = e.manifest->version().toStdString();
            info.addDryToWet = e.manifest->addDryToWet();
            info.metaknobDefault = e.manifest->metaknobDefault();
            for (const auto& p : e.manifest->parameters()) {
                ParameterInfo pi;
                pi.id = p->id().toStdString();
                pi.name = p->name().toStdString();
                pi.shortName = p->shortName().toStdString();
                pi.description = p->description().toStdString();
                pi.minimum = p->getMinimum();
                pi.defaultValue = p->getDefault();
                pi.maximum = p->getMaximum();
                pi.scale = toScale(p->valueScaler());
                pi.units = toUnits(p->unitsHint());
                pi.link = toLink(p->defaultLinkType());
                pi.linkInverted = p->defaultLinkInversion() == EffectManifestParameter::LinkInversion::Inverted;
                pi.neutralPoint = p->neutralPointOnScale();
                info.parameters.push_back(std::move(pi));
            }
            result.push_back(std::move(info));
        }
        return result;
    }();
    return infos;
}

const EffectInfo* findEffect(const std::string& id)
{
    for (const auto& info : effects()) {
        if (info.id == id) {
            return &info;
        }
    }
    return nullptr;
}

std::unique_ptr<Processor> createProcessor(const std::string& id)
{
    const auto& entries = registry();
    const auto& infos = effects();
    for (size_t i = 0; i < entries.size(); ++i) {
        if (infos[i].id == id) {
            return std::make_unique<ProcessorImpl>(entries[i], infos[i]);
        }
    }
    return nullptr;
}
}

namespace mixtapecontrols {
namespace {
struct Table {
    std::mutex mutex;
    // std::map nodes never move, so handed-out pointers stay valid.
    std::map<std::pair<std::string, std::string>, std::atomic<double> > values;
};

Table& table()
{
    static Table* t = [] {
        auto* created = new Table();
        // Mixxx's Mixer preferences defaults: the EQ crossovers.
        created->values[{ "[Mixer Profile]", "LoEQFrequency" }].store(250.0);
        created->values[{ "[Mixer Profile]", "HiEQFrequency" }].store(2500.0);
        return created;
    }();
    return *t;
}
}

std::atomic<double>* control(const std::string& group, const std::string& item)
{
    Table& t = table();
    std::lock_guard<std::mutex> lock(t.mutex);
    return &t.values[{ group, item }];
}

void set(const std::string& group, const std::string& item, double value)
{
    control(group, item)->store(value, std::memory_order_relaxed);
}
}
