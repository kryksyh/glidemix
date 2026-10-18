/*
 * GlideMix
 */
#pragma once

#include <vector>

#include "framework/global/async/notification.h"
#include "framework/global/modularity/imoduleinterface.h"

namespace au::playback {
/// The metronome, ported from the parked MixHouse DJ app: a synthesised
/// click (three voices, Logic-style tuning) that plays on the project's beat
/// grid, accenting beat 1 of every bar.
class IMetronome : MODULE_GLOBAL_INTERFACE
{
    INTERFACE_ID(IMetronome)

public:
    enum class Voice {
        Classic = 0,  // bright electronic tick
        Wood = 1,     // woodblock: lower, more body, longer decay
        Click = 2,    // dry transient, almost no pitch
    };

    /// One rendered pair of clicks for the current settings and a sample rate.
    struct Clicks {
        int sampleRate = 0;
        std::vector<float> beat;
        std::vector<float> accent;
        float gain = 1.0f;
        bool enabled = false;
    };

    virtual ~IMetronome() = default;

    virtual bool enabled() const = 0;
    virtual void setEnabled(bool enabled) = 0;

    /// Click level 0..1, the parked app's knob: linear across -24..+24 dB.
    virtual double level() const = 0;
    virtual void setLevel(double level) = 0;

    virtual Voice voice() const = 0;
    virtual void setVoice(Voice voice) = 0;

    /// Semitones, -24..+24.
    virtual int pitch() const = 0;
    /// 0 = pure click, 1 = pure tone.
    virtual double tonality() const = 0;
    /// 0 = long ring, 1 = very dry.
    virtual double damp() const = 0;
    virtual void setTuning(int pitch, double tonality, double damp) = 0;

    virtual muse::async::Notification settingsChanged() const = 0;

    /// Renders the clicks for a sample rate. Main thread, before playback starts.
    virtual void prepare(int sampleRate) = 0;

    /// The clicks for the current settings, rendered at the rate last passed to
    /// prepare(), or null before that. Safe on the audio thread: lock-free, and a
    /// returned pointer stays valid for the metronome's lifetime.
    virtual const Clicks* clicks() const = 0;
};
}
