/*
 * GlideMix
 */
#include "metronome.h"

#include <algorithm>
#include <cmath>

#include "framework/global/settings.h"

#include "metronomeclick.h"

using namespace au::playback;

static const std::string moduleName("playback");

static const muse::Settings::Key METRONOME_ENABLED(moduleName, "metronome/enabled");
static const muse::Settings::Key METRONOME_LEVEL(moduleName, "metronome/level");
static const muse::Settings::Key METRONOME_VOICE(moduleName, "metronome/voice");
static const muse::Settings::Key METRONOME_PITCH(moduleName, "metronome/pitch");
static const muse::Settings::Key METRONOME_TONALITY(moduleName, "metronome/tonality");
static const muse::Settings::Key METRONOME_DAMP(moduleName, "metronome/damp");

void Metronome::init()
{
    // Defaults are the parked app's: off, knob at 0.85, Classic, untuned,
    // tonality and damp centred.
    muse::settings()->setDefaultValue(METRONOME_ENABLED, muse::Val(false));
    muse::settings()->setDefaultValue(METRONOME_LEVEL, muse::Val(0.85));
    muse::settings()->setDefaultValue(METRONOME_VOICE, muse::Val(static_cast<int>(Voice::Classic)));
    muse::settings()->setDefaultValue(METRONOME_PITCH, muse::Val(0));
    muse::settings()->setDefaultValue(METRONOME_TONALITY, muse::Val(0.5));
    muse::settings()->setDefaultValue(METRONOME_DAMP, muse::Val(0.5));
}

bool Metronome::enabled() const
{
    return muse::settings()->value(METRONOME_ENABLED).toBool();
}

void Metronome::setEnabled(bool enabled)
{
    if (enabled == this->enabled()) {
        return;
    }
    muse::settings()->setSharedValue(METRONOME_ENABLED, muse::Val(enabled));
    publish();
}

double Metronome::level() const
{
    return muse::settings()->value(METRONOME_LEVEL).toDouble();
}

void Metronome::setLevel(double level)
{
    muse::settings()->setSharedValue(METRONOME_LEVEL, muse::Val(std::clamp(level, 0.0, 1.0)));
    publish();
}

IMetronome::Voice Metronome::voice() const
{
    return static_cast<Voice>(std::clamp(muse::settings()->value(METRONOME_VOICE).toInt(), 0, 2));
}

void Metronome::setVoice(Voice voice)
{
    muse::settings()->setSharedValue(METRONOME_VOICE, muse::Val(static_cast<int>(voice)));
    publish();
}

int Metronome::pitch() const
{
    return std::clamp(muse::settings()->value(METRONOME_PITCH).toInt(), -24, 24);
}

double Metronome::tonality() const
{
    return std::clamp(muse::settings()->value(METRONOME_TONALITY).toDouble(), 0.0, 1.0);
}

double Metronome::damp() const
{
    return std::clamp(muse::settings()->value(METRONOME_DAMP).toDouble(), 0.0, 1.0);
}

void Metronome::setTuning(int pitch, double tonality, double damp)
{
    muse::settings()->setSharedValue(METRONOME_PITCH, muse::Val(std::clamp(pitch, -24, 24)));
    muse::settings()->setSharedValue(METRONOME_TONALITY, muse::Val(std::clamp(tonality, 0.0, 1.0)));
    muse::settings()->setSharedValue(METRONOME_DAMP, muse::Val(std::clamp(damp, 0.0, 1.0)));
    publish();
}

muse::async::Notification Metronome::settingsChanged() const
{
    return m_settingsChanged;
}

void Metronome::prepare(int sampleRate)
{
    if (sampleRate <= 0) {
        return;
    }
    if (sampleRate != m_sampleRate || !m_current.load()) {
        m_sampleRate = sampleRate;
        publish();
    }
}

const IMetronome::Clicks* Metronome::clicks() const
{
    return m_current.load(std::memory_order_acquire);
}

void Metronome::publish()
{
    if (m_sampleRate > 0) {
        auto set = std::make_unique<Clicks>();
        set->sampleRate = m_sampleRate;
        const int voiceIndex = static_cast<int>(voice());
        const int tonalityPct = static_cast<int>(std::lround(tonality() * 100));
        const int dampPct = static_cast<int>(std::lround(damp() * 100));
        set->beat = renderMetronomeClick(m_sampleRate, false, voiceIndex, pitch(), tonalityPct, dampPct);
        set->accent = renderMetronomeClick(m_sampleRate, true, voiceIndex, pitch(), tonalityPct, dampPct);
        // The parked app's knob drives Mixxx's metronome gain, a -24..+24 dB
        // range with the knob as its linked parameter.
        set->gain = static_cast<float>(std::pow(10.0, (-24.0 + 48.0 * level()) / 20.0));
        set->enabled = enabled();
        m_current.store(set.get(), std::memory_order_release);
        m_published.push_back(std::move(set));
    }
    m_settingsChanged.notify();
}
