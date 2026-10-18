/*
 * GlideMix
 */
#include "metronomeclick.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace {
// Per-voice character, unchanged from the parked MixHouse DJ app. Frequencies
// are a fifth or an octave apart between beat and accent so a downbeat reads as
// PITCH, which survives a loud master better than level.
struct VoiceSpec {
    double beatHz;
    double accentHz;
    double decay;     // e-folds per second; higher = drier
    double harmonic;  // second-harmonic amount: more = harder, more plasticky
    double lengthSec;
    double noise;     // broadband attack: what makes a tick rather than a beep
};

constexpr VoiceSpec kVoices[] = {
    // Classic, tuned toward Ableton's: a tick, not a beep. Short, a fast
    // decay, and the accent an OCTAVE up.
    { 1050.0, 2100.0, 200.0, 0.5, 0.022, 0.45 },   // Classic: bright tick
    { 600.0, 900.0, 45.0, 0.55, 0.075, 0.20 },     // Wood: lower, rings on
    { 2200.0, 3000.0, 260.0, 0.15, 0.020, 0.70 },  // Click: near-pitchless
};

// Peak amplitude: near full scale, then attenuated by the level control.
constexpr float kPeak = 0.9f;
}

namespace au::playback {
std::vector<float> renderMetronomeClick(int sampleRate, bool accent, int voice, int semis, int tonalityPct, int dampPct)
{
    const VoiceSpec& v = kVoices[std::clamp(voice, 0, 2)];
    // Tuning in semitones, the way Logic tunes Klopfgeist.
    const double ratio = std::pow(2.0, semis / 12.0);
    const double hz = (accent ? v.accentHz : v.beatHz) * ratio;
    // Damp is Logic's release control: 0 rings, 1 is very dry.
    const double dampScale = 0.35 + 1.9 * (dampPct / 100.0);
    // The accent rings a little longer, as on a real block struck harder.
    const double decay = (accent ? v.decay * 0.8 : v.decay) * dampScale;
    // Tonality crossfades the voice's own noise amount toward a pure tone.
    const double noiseAmt = v.noise * (1.0 - tonalityPct / 100.0) * 2.0;
    const auto frames = static_cast<std::size_t>(v.lengthSec * sampleRate);
    std::vector<float> out(frames);
    for (std::size_t i = 0; i < frames; ++i) {
        const double t = static_cast<double>(i) / sampleRate;
        const double env = std::exp(-decay * t);
        double body = std::sin(2.0 * M_PI * hz * t)
                      + v.harmonic * std::sin(2.0 * M_PI * hz * 2.0 * t);
        // A real click is broadband at the very start: deterministic
        // pseudo-noise, decaying ~10x faster than the body so it reads as
        // attack rather than hiss.
        if (noiseAmt > 0.0) {
            const double n = std::sin(t * 12345.678) * std::cos(t * 7531.246);
            body += noiseAmt * n * std::exp(-decay * 10.0 * t);
        }
        // A couple of milliseconds of fade-in stops the transient popping.
        const double attack = std::min(1.0, t / 0.0015);
        out[i] = static_cast<float>(kPeak * env * attack * body / (1.0 + v.harmonic));
    }
    return out;
}
}
