/*
 * GlideMix
 */
#include "mixhousetransitionspec.h"

#include <algorithm>
#include <cmath>

namespace au::mixhouse {
namespace {
constexpr double kSpecHalfPi = 1.5707963267948966;
// Steps of the sampled equal-power curve; the clip line interpolates between them.
constexpr int kSpecCurveSteps = 8;
// A swap is not instant: this short ramp keeps it click-free (0.1 beat is about
// 45 ms at 130 BPM, quicker than the ear hears a change as a fade, slower than a click).
constexpr double kSpecSwapRampBeats = 0.1;
// At or below this a value counts as silence for the volume line (-50 dB).
constexpr double kSpecSilentAmplitude = 1e-3;

struct SpecPoint {
    double offset = 0.0;
    double value = 1.0;
};

double specDb(double amplitude)
{
    return amplitude <= kSpecSilentAmplitude ? -50.0 : 20.0 * std::log10(amplitude);
}

// Two points at the same beat would ask the clip line for a vertical step; keep
// the later one (the value the line ends up at).
void specAdd(std::vector<SpecPoint>& line, double offset, double value)
{
    if (!line.empty() && std::abs(line.back().offset - offset) < 1e-9) {
        line.back().value = value;
        return;
    }
    line.push_back({ offset, value });
}

// One track's line for one lane. `outgoing` picks which of the two tracks it is.
std::vector<SpecPoint> specLine(const MixHouseTransitionLane& lane, double length, bool outgoing)
{
    std::vector<SpecPoint> line;
    const double strength = std::clamp(lane.strength, 0.0, 1.0);
    const auto fade = [&](bool fallingLine) {
        for (int i = 0; i <= kSpecCurveSteps; ++i) {
            const double u = double(i) / kSpecCurveSteps;
            // Equal power: the two lines' squares sum to 1 at full strength.
            const double shape = fallingLine ? std::cos(u * kSpecHalfPi) : std::sin(u * kSpecHalfPi);
            specAdd(line, u * length, 1.0 - strength * (1.0 - shape));
        }
    };
    switch (lane.curve) {
    case MixHouseCurveKind::None:
        break;
    case MixHouseCurveKind::Crossfade:
        fade(outgoing);
        break;
    case MixHouseCurveKind::FadeIn:
        if (!outgoing) {
            fade(false);
        }
        break;
    case MixHouseCurveKind::FadeOut:
        if (outgoing) {
            fade(true);
        }
        break;
    case MixHouseCurveKind::Swap: {
        const double crossing = std::clamp(lane.crossing, 0.0, 1.0) * length;
        const double heldBack = std::max(0.0, lane.leaveOutBeats);
        const double low = 1.0 - strength;
        if (outgoing) {
            specAdd(line, 0.0, 1.0);
            specAdd(line, crossing, 1.0);
            specAdd(line, crossing + kSpecSwapRampBeats, low);
        } else {
            specAdd(line, 0.0, low);
            specAdd(line, crossing + heldBack, low);
            specAdd(line, crossing + heldBack + kSpecSwapRampBeats, 1.0);
        }
        break;
    }
    }
    return line;
}

MixHouseTransitionSpec makeSpec(const char* name, double lengthBeats, MixHouseCurveKind volume, MixHouseCurveKind bass,
                                double crossing, double leaveOutBeats)
{
    MixHouseTransitionSpec spec;
    spec.name = name;
    spec.lengthBeats = lengthBeats;
    spec.volume.curve = volume;
    spec.bass.curve = bass;
    spec.bass.crossing = crossing;
    spec.bass.leaveOutBeats = leaveOutBeats;
    spec.builtIn = true;
    return spec;
}
}

namespace {
// Knob positions (0..1) of the DJ effects the transition lanes play. The
// filter knobs are Mixxx's log pots: 0.2 is about 350 Hz, 0.5 about 2 kHz.
constexpr const char* kFilterEffect = "org.mixxx.effects.filter";
constexpr const char* kNoiseEffect = "org.mixxx.effects.whitenoise";
constexpr const char* kEchoEffect = "org.mixxx.effects.echo";
constexpr const char* kReverbEffect = "org.mixxx.effects.reverb";
constexpr double kLowPassClosed = 0.2;
constexpr double kHighPassClosed = 0.5;
constexpr double kNoiseLevel = 0.55;       // about 28% noise at the top of a riser
constexpr double kNoiseDownsweepBeats = 8.0;
constexpr double kEffectOutBeats = 4.0;    // the last bar goes into the echo / reverb
constexpr double kCutBeats = 0.0625;       // a 1/16 beat step, quick but click-free

void addEffectLines(const MixHouseTransitionSpec& spec, std::vector<MixHouseEffectLine>& lines)
{
    const double length = std::max(1.0, spec.lengthBeats);
    const auto line = [&](const char* effect, const char* parameter, bool outgoing,
                          std::vector<MixHouseBassMarker> markers) {
        lines.push_back({ effect, parameter, outgoing, std::move(markers) });
    };
    switch (spec.filter) {
    case MixHouseFilterSweep::None:
        break;
    case MixHouseFilterSweep::OutHighPass:
        line(kFilterEffect, "hpf", true, { { 0.0, 0.0 }, { length, kHighPassClosed } });
        break;
    case MixHouseFilterSweep::OutLowPass:
        line(kFilterEffect, "lpf", true, { { 0.0, 1.0 }, { length, kLowPassClosed } });
        break;
    case MixHouseFilterSweep::InHighPass:
        line(kFilterEffect, "hpf", false, { { 0.0, kHighPassClosed }, { length, 0.0 } });
        break;
    case MixHouseFilterSweep::InLowPass:
        line(kFilterEffect, "lpf", false, { { 0.0, kLowPassClosed }, { length, 1.0 } });
        break;
    }
    switch (spec.noise) {
    case MixHouseNoiseSweep::None:
        break;
    case MixHouseNoiseSweep::Riser:
        // The noise is made inside the effect, so it must stop with the track.
        line(kNoiseEffect, "dry_wet", true, { { 0.0, 0.0 }, { length, kNoiseLevel }, { length + kCutBeats, 0.0 } });
        break;
    case MixHouseNoiseSweep::Downsweep:
        line(kNoiseEffect, "dry_wet", false, { { length - kCutBeats, 0.0 }, { length, kNoiseLevel },
                   { length + kNoiseDownsweepBeats, 0.0 } });
        break;
    }
    // Send up over the last bar; after it the send closes and the echo or
    // reverb already fed rings out on its own.
    const double sendFrom = std::max(0.0, length - kEffectOutBeats);
    switch (spec.effectOut) {
    case MixHouseEffectOut::None:
        break;
    case MixHouseEffectOut::Echo:
        line(kEchoEffect, "send_amount", true, { { sendFrom, 0.0 }, { length, 1.0 }, { length + kCutBeats, 0.0 } });
        break;
    case MixHouseEffectOut::Reverb:
        line(kReverbEffect, "send_amount", true, { { sendFrom, 0.0 }, { length, 1.0 }, { length + kCutBeats, 0.0 } });
        break;
    }
}
}

static MixHouseTransitionSpec withEffects(MixHouseTransitionSpec spec, MixHouseFilterSweep filter,
                                   MixHouseNoiseSweep noise = MixHouseNoiseSweep::None,
                                   MixHouseEffectOut effectOut = MixHouseEffectOut::None)
{
    spec.filter = filter;
    spec.noise = noise;
    spec.effectOut = effectOut;
    return spec;
}

//! A DJ's three-band blend: the highs cross over the whole length, the bass
//! swaps in the middle and the mids a phrase later.
static MixHouseTransitionSpec withEq(MixHouseTransitionSpec spec)
{
    spec.high.curve = MixHouseCurveKind::Crossfade;
    spec.mid.curve = MixHouseCurveKind::Swap;
    spec.mid.crossing = 0.75;
    return spec;
}

MixHouseTransitionPreset presetFromSpec(const MixHouseTransitionSpec& spec)
{
    MixHouseTransitionPreset preset;
    preset.name = spec.name;
    preset.beatmix = true;   // offsets are in beats
    const double length = std::max(1.0, spec.lengthBeats);
    for (const SpecPoint& p : specLine(spec.volume, length, true)) {
        preset.outro.push_back({ p.offset, specDb(p.value) });
    }
    for (const SpecPoint& p : specLine(spec.volume, length, false)) {
        preset.intro.push_back({ p.offset, specDb(p.value) });
    }
    for (const SpecPoint& p : specLine(spec.bass, length, true)) {
        preset.bassOutro.push_back({ p.offset, p.value });
    }
    for (const SpecPoint& p : specLine(spec.bass, length, false)) {
        preset.bassIntro.push_back({ p.offset, p.value });
    }
    for (const SpecPoint& p : specLine(spec.mid, length, true)) {
        preset.midOutro.push_back({ p.offset, p.value });
    }
    for (const SpecPoint& p : specLine(spec.mid, length, false)) {
        preset.midIntro.push_back({ p.offset, p.value });
    }
    for (const SpecPoint& p : specLine(spec.high, length, true)) {
        preset.highOutro.push_back({ p.offset, p.value });
    }
    for (const SpecPoint& p : specLine(spec.high, length, false)) {
        preset.highIntro.push_back({ p.offset, p.value });
    }
    addEffectLines(spec, preset.effects);
    return preset;
}

const std::vector<MixHouseTransitionSpec>& builtInTransitionSpecs()
{
    using K = MixHouseCurveKind;
    // The first is what Mix Now uses by default: two bars, volumes crossing
    // over the whole length, and the bass handed over on beat 4, right in the
    // middle, when both tracks are at -3 dB and the kick can change hands.
    static const std::vector<MixHouseTransitionSpec> kSpecs = {
        makeSpec(kDefaultMixNowTransition, 8, K::Crossfade, K::Swap, 0.5, 0),
        makeSpec("Quick crossfade + bass swap, 2 beats out", 8, K::Crossfade, K::Swap, 0.5, 2),
        makeSpec("Bass swap, 8 bars", 32, K::Crossfade, K::Swap, 0.5, 0),
        makeSpec("Bass swap, 16 bars", 64, K::Crossfade, K::Swap, 0.5, 0),
        withEffects(makeSpec("Filter out, 8 bars", 32, K::Crossfade, K::Swap, 0.5, 0),
                    MixHouseFilterSweep::OutHighPass),
        withEffects(makeSpec("Filter in, 8 bars", 32, K::Crossfade, K::Swap, 0.5, 0),
                    MixHouseFilterSweep::InHighPass),
        withEffects(makeSpec("Muffled out, 8 bars", 32, K::Crossfade, K::Swap, 0.5, 0),
                    MixHouseFilterSweep::OutLowPass),
        withEffects(makeSpec("Riser + filter, 8 bars", 32, K::Crossfade, K::Swap, 0.5, 0),
                    MixHouseFilterSweep::OutHighPass, MixHouseNoiseSweep::Riser),
        withEffects(makeSpec("Echo out, 4 bars", 16, K::FadeOut, K::None, 0.5, 0),
                    MixHouseFilterSweep::None, MixHouseNoiseSweep::None, MixHouseEffectOut::Echo),
        withEffects(makeSpec("Reverb out, 4 bars", 16, K::FadeOut, K::None, 0.5, 0),
                    MixHouseFilterSweep::None, MixHouseNoiseSweep::None, MixHouseEffectOut::Reverb),
        withEffects(makeSpec("Drop + downsweep", 8, K::Crossfade, K::Swap, 0.5, 0),
                    MixHouseFilterSweep::None, MixHouseNoiseSweep::Downsweep),
        withEq(makeSpec("Three-band swap, 8 bars", 32, K::Crossfade, K::Swap, 0.5, 0)),
    };
    return kSpecs;
}

const std::vector<MixHouseTransitionEffectParameter>& transitionEffectParameters()
{
    static const std::vector<MixHouseTransitionEffectParameter> kParameters = {
        { kFilterEffect, "hpf", 0.0 },
        { kFilterEffect, "lpf", 1.0 },
        { kNoiseEffect, "dry_wet", 0.0 },
        { kEchoEffect, "send_amount", 0.0 },
        { kReverbEffect, "send_amount", 0.0 },
        { "org.glidemix.effects.tapestop", "speed", 1.0 },
        { "org.glidemix.effects.roll", "roll", 0.0 },
    };
    return kParameters;
}

std::string validateTransitionSpec(const MixHouseTransitionSpec& spec)
{
    if (spec.name.empty()) {
        return "The transition needs a name.";
    }
    if (spec.name.size() > 60) {
        return "The name is too long (60 characters at most).";
    }
    if (!(spec.lengthBeats >= 1.0 && spec.lengthBeats <= 256.0)) {
        return "The length must be between 1 and 256 beats.";
    }
    for (const MixHouseTransitionLane* lane : { &spec.volume, &spec.bass, &spec.mid, &spec.high }) {
        if (!(lane->strength >= 0.0 && lane->strength <= 1.0)) {
            return "Strength must be between 0 and 100%.";
        }
        if (!(lane->crossing >= 0.0 && lane->crossing <= 1.0)) {
            return "The crossing point must be within the transition.";
        }
        if (!(lane->leaveOutBeats >= 0.0 && lane->leaveOutBeats <= 16.0)) {
            return "Leaving out more than 16 beats is not supported.";
        }
    }
    if (spec.volume.curve == MixHouseCurveKind::None && spec.bass.curve == MixHouseCurveKind::None
        && spec.mid.curve == MixHouseCurveKind::None && spec.high.curve == MixHouseCurveKind::None
        && spec.filter == MixHouseFilterSweep::None && spec.noise == MixHouseNoiseSweep::None
        && spec.effectOut == MixHouseEffectOut::None) {
        return "Set at least one lane to do something.";
    }
    return {};
}
}
