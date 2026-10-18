/*
 * GlideMix
 *
 * Mixxx's built-in effects (GPL, from Mixxx 2.6's src/effects/backends/builtin)
 * behind a plain C++ interface: what each effect's parameters are, and a
 * processor that runs its DSP. This header includes nothing from Mixxx or
 * Audacity, so the two never meet in one translation unit.
 */
#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace mixxxeffects {
enum class Scale { Linear, Logarithmic, LogarithmicInverse, Integral, Toggle };
//! How a parameter follows the effect's meta knob (Mixxx's LinkType).
enum class Link { None, Linked, LinkedLeft, LinkedRight, LinkedLeftRight };
enum class Units { Unknown, Hertz, SampleRate, Beats, Millisecond, Semitones, Coefficient, Decibel, Time };

struct ParameterInfo {
    std::string id;
    std::string name;
    std::string shortName;
    std::string description;
    double minimum = 0.0;
    double defaultValue = 0.0;
    double maximum = 1.0;
    Scale scale = Scale::Linear;
    Units units = Units::Unknown;
    Link link = Link::None;
    bool linkInverted = false;
    double neutralPoint = 0.0;   ///< knob position of the "no effect" value

    //! Mixxx's knob mapping (ValueScaler): value <-> 0..1.
    double toNormalized(double value) const;
    double fromNormalized(double normalized) const;
    //! Knob position (0..1) the meta knob at `meta` puts this parameter at,
    //! or < 0 when it isn't linked (EffectKnobParameterSlot::
    //! onEffectMetaParameterChanged).
    double linkedNormalized(double meta) const;
};

struct EffectInfo {
    std::string id;           ///< e.g. "org.mixxx.effects.echo"
    std::string name;
    std::string description;
    std::string author;
    std::string version;
    bool addDryToWet = false; ///< Mixxx adds the dry signal to this effect's output
    double metaknobDefault = 0.0;
    std::vector<ParameterInfo> parameters;
};

const std::vector<EffectInfo>& effects();
const EffectInfo* findEffect(const std::string& id);

class Processor
{
public:
    virtual ~Processor() = default;
    virtual void setParameter(size_t index, double value) = 0;
    //! Interleaved stereo in and out (they may be the same buffer).
    //! `beatLengthSeconds` > 0 makes tempo-synced parameters count beats,
    //! as Mixxx does when the deck has a beat grid. `beatFractionAtEnd`
    //! (0..1, or < 0 when unknown) is the beat phase after the last frame;
    //! Tremolo and Metronome lock to it.
    virtual void process(const float* interleavedIn, float* interleavedOut, size_t frames, double sampleRate,
                         double beatLengthSeconds, double beatFractionAtEnd) = 0;
    virtual void reset() = 0;
};

std::unique_ptr<Processor> createProcessor(const std::string& id);
}
