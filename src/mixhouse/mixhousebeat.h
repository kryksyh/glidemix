/*
 * GlideMix Samples B8: Beat. Up to 8 rows of the user's own one-shots on a
 * 16-steps-per-bar grid, rendered to ONE seamless audio loop (Chris,
 * 2026-11-12: "one audio loop"), which lands on a song's sample line like any
 * loop and follows the song tempo. Rendering is ours: decode each one-shot
 * once, place a copy on every hit at its level, a new hit on the same row
 * cuts the one before (like a drum machine), and anything ringing past the
 * end wraps to the start so the loop has no seam.
 */
#pragma once

#include <QString>
#include <vector>

namespace au::mixhouse {
struct BeatRow {
    QString path;              ///< the one-shot; empty = an empty row
    bool mute = false;
    bool solo = false;
    double gain = 0.78;        ///< row volume, 0..1 (FL's sampler default, -5.2 dB headroom)
    double pan = 0.0;          ///< -1 left .. 1 right
    bool cutSelf = true;       ///< a new hit stops the one still ringing (FL's "Cut itself")
    double swingMix = 1.0;     ///< 0..1: how much of the pattern's swing this row takes (FL's "Swing mix")
    QString role;              ///< Beat starter: kick, clap, snare, hat, openhat, perc (the dice picks the same kind)
    bool locked = false;       ///< the dice leaves this row's sample alone
    int length = 0;            ///< steps before the row repeats (polymeter); 0 = the whole pattern
    int choke = 0;             ///< choke group 1..4: a hit stops the other rows of its group still ringing; 0 = none
    std::vector<int> steps;    ///< per 1/16 step: 0 off, else on
    // The Graph editor's values, one per step (FL's graph: velocity, pitch,
    // pan, shift, repeat). Missing entries take the defaults below.
    std::vector<double> velocity;   ///< 0..1, default 0.78 (Shift-click: 1.0 accent, 0.5 soft)
    std::vector<double> pitch;      ///< semitones -24..24, played like a sampler (faster = higher)
    std::vector<double> stepPan;    ///< -1..1, added to the row's pan
    std::vector<double> shift;      ///< 0..1 of a step: the hit starts that much late
    std::vector<int> repeat;        ///< 1..4 hits inside the step (hat rolls)
    std::vector<double> gate;       ///< length 0.05..1 of the one-shot (1 = all of it)
    std::vector<double> reverse;    ///< 0 or 1: the hit plays backwards
    std::vector<double> drive;      ///< distortion 0..1
    std::vector<double> filter;     ///< low-pass 0..1 (1 = open)
    std::vector<double> reverb;     ///< reverb send 0..1
    double attack = 0.0;       ///< seconds of fade-in on every hit (Serato's Attack)
    double release = 0.0;      ///< seconds of fade-out at the end of every hit (Release)
    int keyShift = 0;          ///< semitones added to every step's pitch (Key shift)
};

constexpr double kBeatDefaultVelocity = 0.78;

/// The pattern's swing as the MPC percentage people know (50 = straight .. 75).
inline double beatSwingPercent(double swing) { return 50.0 + 25.0 * swing; }

/// A step's graph value, or its default when the step has none.
double beatStepValue(const std::vector<double>& values, int step, double fallback);
int beatStepRepeat(const BeatRow& row, int step);

struct BeatPattern {
    int bars = 1;              ///< 1, 2, 4, 8 or 16
    int stepsPerBar = 16;      ///< the grid: 16 (1/16), 32 (x2, 1/32) or 24 (1/16 triplets)
    double swing = 0.0;        ///< 0..1 = MPC swing 50 %..75 %: every second 1/16 comes late, up to half a step
                               ///< (50 % straight, ~54-58 % loose, ~66 % triplet feel, 75 % the most)
    std::vector<BeatRow> rows;
};

constexpr int kBeatStepsPerBar = 16;
constexpr int kBeatMaxRows = 8;


/// Renders `pattern` at `bpm` to a 24-bit stereo WAV at 44.1 kHz, exactly
/// `bars` bars long. False (and `error` set) when nothing could be rendered.
bool renderBeat(const BeatPattern& pattern, double bpm, const QString& outPath, QString& error);

/// The pattern of a placed beat lives next to its WAV, same name, ".glidebeat"
/// (JSON). "Edit beat" on the clip opens it again.
QString beatPatternPathFor(const QString& wavPath);
bool saveBeatPattern(const BeatPattern& pattern, double bpm, const QString& path);
bool loadBeatPattern(const QString& path, BeatPattern& pattern);

/// "Send to Beat" from a clip's right-click: the sound waits here until the
/// Beat window (open now or opened next) adds it as a row.
void queueBeatSample(const QString& path);
std::vector<QString> takeQueuedBeatSamples();

/// `hits` hits spread as evenly as possible over `steps` steps (Euclidean),
/// the first on step 0.
std::vector<bool> evenRhythm(int hits, int steps);
}
