/*
 * GlideMix -- see glidemixdjeffects.h.
 */
#include "effects/backends/builtin/glidemixdjeffects.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr double kMaxSeconds = 4.0;   // longest delay / loop / slowed read
constexpr double kPi = 3.14159265358979323846;

EffectManifestParameterPointer addParameter(EffectManifestPointer manifest, const char* id, const QString& name,
                                            const QString& description, double minimum, double def, double maximum,
                                            EffectManifestParameter::UnitsHint units
                                            = EffectManifestParameter::UnitsHint::Unknown,
                                            EffectManifestParameter::ValueScaler scaler
                                            = EffectManifestParameter::ValueScaler::Linear)
{
    EffectManifestParameterPointer p = manifest->addParameter();
    p->setId(id);
    p->setName(name);
    p->setShortName(name);
    p->setDescription(description);
    p->setValueScaler(scaler);
    p->setUnitsHint(units);
    p->setRange(minimum, def, maximum);
    return p;
}

EffectManifestPointer baseManifest(const QString& id, const QString& name, const QString& description)
{
    EffectManifestPointer m(new EffectManifest());
    m->setAddDryToWet(false);
    m->setEffectRampsFromDry(false);
    m->setId(id);
    m->setName(name);
    m->setShortName(name);
    m->setAuthor("GlideMix");
    m->setVersion("1.0");
    m->setDescription(description);
    return m;
}

double beatSeconds(const GroupFeatureState& gf)
{
    return gf.beat_length.has_value() && gf.beat_length->seconds > 0.0 ? gf.beat_length->seconds : 0.5;
}

//! The nearest of the musical lengths to `beats`.
double snapBeats(double beats, std::initializer_list<double> choices)
{
    double best = *choices.begin();
    for (double c : choices) {
        if (std::abs(std::log(c / std::max(beats, 1e-6))) < std::abs(std::log(best / std::max(beats, 1e-6)))) {
            best = c;
        }
    }
    return best;
}

//! Beat position (in beats, any integer part) of frame `i` of the buffer.
double beatPositionAt(const GroupFeatureState& gf, SINT frames, SINT i, double sampleRate, double fallbackCounter)
{
    const double beatFrames = beatSeconds(gf) * sampleRate;
    if (gf.beat_fraction_buffer_end.has_value()) {
        return *gf.beat_fraction_buffer_end - static_cast<double>(frames - 1 - i) / beatFrames;
    }
    return (fallbackCounter + i) / beatFrames;
}
}

GlideMixDelayState::GlideMixDelayState(const mixxx::EngineParameters& p)
    : EffectState(p)
{
    sampleRate = p.sampleRate();
    const size_t frames = static_cast<size_t>(kMaxSeconds * sampleRate) + 1;
    left.assign(frames, 0.0f);
    right.assign(frames, 0.0f);
}

// ---- Ping Pong ---------------------------------------------------------

QString GlideMixPingPongEffect::getId()
{
    return QStringLiteral("org.glidemix.effects.pingpong");
}

EffectManifestPointer GlideMixPingPongEffect::getManifest()
{
    auto m = baseManifest(getId(), QObject::tr("Ping Pong"),
                          QObject::tr("An echo that bounces between left and right in time with the beat."));
    addParameter(m, "delay_time", QObject::tr("Time"), QObject::tr("Delay in beats"), 0.125, 0.5, 2.0,
                 EffectManifestParameter::UnitsHint::Beats);
    addParameter(m, "feedback_amount", QObject::tr("Feedback"), QObject::tr("How long the echo keeps bouncing"), 0.0,
                 0.55, 0.9);
    addParameter(m, "send_amount", QObject::tr("Send"), QObject::tr("How much of the track goes into the echo"), 0.0,
                 0.7, 1.0);
    return m;
}

void GlideMixPingPongEffect::loadEngineEffectParameters(const QMap<QString, EngineEffectParameterPointer>& parameters)
{
    m_time = parameters.value("delay_time");
    m_feedback = parameters.value("feedback_amount");
    m_send = parameters.value("send_amount");
}

void GlideMixPingPongEffect::processChannel(GlideMixDelayState* s, const CSAMPLE* in, CSAMPLE* out,
                                            const mixxx::EngineParameters& p, const EffectEnableState,
                                            const GroupFeatureState& gf)
{
    const SINT frames = p.framesPerBuffer();
    const size_t size = s->left.size();
    const double beats = snapBeats(m_time->value(), { 0.125, 0.25, 1.0 / 3.0, 0.5, 0.75, 1.0, 2.0 });
    const size_t delay = std::clamp<size_t>(static_cast<size_t>(beats * beatSeconds(gf) * s->sampleRate), 1, size - 1);
    const float fb = static_cast<float>(m_feedback->value());
    const float send = static_cast<float>(m_send->value());
    for (SINT i = 0; i < frames; ++i) {
        const size_t w = s->write % size;
        const size_t r = (s->write + size - delay) % size;
        const float dL = s->left[r];
        const float dR = s->right[r];
        const float inL = in[2 * i];
        const float inR = in[2 * i + 1];
        // Soft-limited in the loop, so full feedback rings on without piling up.
        s->left[w] = std::tanh(0.5f * (inL + inR) * send + fb * dR);   // into the left ...
        s->right[w] = std::tanh(fb * dL);                               // ... and bounced right
        out[2 * i] = inL + 0.7f * dL;
        out[2 * i + 1] = inR + 0.7f * dR;
        ++s->write;
    }
}

// ---- Roll ----------------------------------------------------------------

QString GlideMixRollEffect::getId()
{
    return QStringLiteral("org.glidemix.effects.roll");
}

EffectManifestPointer GlideMixRollEffect::getManifest()
{
    auto m = baseManifest(getId(), QObject::tr("Roll"),
                          QObject::tr("Loops a slice of the beat while Roll is on, the classic beat roll."));
    addParameter(m, "length", QObject::tr("Length"), QObject::tr("Slice length in beats: 1/16 to 1"), 0.0625, 0.25,
                 1.0, EffectManifestParameter::UnitsHint::Beats);
    addParameter(m, "roll", QObject::tr("Roll"), QObject::tr("On: loop the slice. Off: play on."), 0.0, 0.0, 1.0,
                 EffectManifestParameter::UnitsHint::Unknown, EffectManifestParameter::ValueScaler::Toggle);
    return m;
}

void GlideMixRollEffect::loadEngineEffectParameters(const QMap<QString, EngineEffectParameterPointer>& parameters)
{
    m_length = parameters.value("length");
    m_roll = parameters.value("roll");
}

void GlideMixRollEffect::processChannel(GlideMixDelayState* s, const CSAMPLE* in, CSAMPLE* out,
                                        const mixxx::EngineParameters& p, const EffectEnableState,
                                        const GroupFeatureState& gf)
{
    const SINT frames = p.framesPerBuffer();
    const size_t size = s->left.size();
    const bool on = m_roll->value() >= 0.5;
    if (on && !s->active) {
        const double beats = snapBeats(m_length->value(), { 0.0625, 0.125, 0.25, 0.5, 1.0 });
        s->loopLength = std::clamp<size_t>(static_cast<size_t>(beats * beatSeconds(gf) * s->sampleRate), 64, size - 1);
        s->loopStart = s->write % size;
        s->loopPos = 0;
        s->recorded = 0;
        s->active = true;
    } else if (!on) {
        s->active = false;
    }
    constexpr size_t kFade = 96;   // ~2 ms at each end of the slice, so the loop never clicks
    for (SINT i = 0; i < frames; ++i) {
        const float inL = in[2 * i];
        const float inR = in[2 * i + 1];
        if (!s->active || s->recorded < s->loopLength) {
            const size_t w = s->write % size;
            s->left[w] = inL;
            s->right[w] = inR;
            ++s->write;
            if (s->active) {
                ++s->recorded;
            }
            out[2 * i] = inL;
            out[2 * i + 1] = inR;
            continue;
        }
        const size_t r = (s->loopStart + s->loopPos) % size;
        const size_t fromEnd = s->loopLength - s->loopPos;
        const float fade = static_cast<float>(std::min<size_t>({ kFade, s->loopPos + 1, fromEnd })) / kFade;
        out[2 * i] = s->left[r] * fade;
        out[2 * i + 1] = s->right[r] * fade;
        s->loopPos = (s->loopPos + 1) % s->loopLength;
    }
}

// ---- Trans (beat gate) ---------------------------------------------------

QString GlideMixTransEffect::getId()
{
    return QStringLiteral("org.glidemix.effects.trans");
}

EffectManifestPointer GlideMixTransEffect::getManifest()
{
    auto m = baseManifest(getId(), QObject::tr("Trans"),
                          QObject::tr("A beat gate: cuts the sound on and off in time with the beat."));
    addParameter(m, "rate", QObject::tr("Rate"), QObject::tr("Gate length in beats: 1/8 to 2"), 0.125, 0.25, 2.0,
                 EffectManifestParameter::UnitsHint::Beats);
    addParameter(m, "depth", QObject::tr("Depth"), QObject::tr("How far the sound is cut"), 0.0, 1.0, 1.0);
    return m;
}

void GlideMixTransEffect::loadEngineEffectParameters(const QMap<QString, EngineEffectParameterPointer>& parameters)
{
    m_rate = parameters.value("rate");
    m_depth = parameters.value("depth");
}

void GlideMixTransEffect::processChannel(GlideMixDelayState* s, const CSAMPLE* in, CSAMPLE* out,
                                         const mixxx::EngineParameters& p, const EffectEnableState,
                                         const GroupFeatureState& gf)
{
    const SINT frames = p.framesPerBuffer();
    const double rate = snapBeats(m_rate->value(), { 0.125, 0.25, 0.5, 1.0, 2.0 });
    const double depth = m_depth->value();
    const double smooth = 1.0 - std::exp(-1.0 / (0.002 * s->sampleRate));   // 2 ms edges
    for (SINT i = 0; i < frames; ++i) {
        const double pos = beatPositionAt(gf, frames, i, s->sampleRate, static_cast<double>(s->write)) / rate;
        const bool open = pos - std::floor(pos) < 0.5;
        s->gate += ((open ? 1.0 : 1.0 - depth) - s->gate) * smooth;
        out[2 * i] = static_cast<float>(in[2 * i] * s->gate);
        out[2 * i + 1] = static_cast<float>(in[2 * i + 1] * s->gate);
    }
    s->write += static_cast<size_t>(frames);
}

// ---- Dub Echo --------------------------------------------------------------

QString GlideMixDubEchoEffect::getId()
{
    return QStringLiteral("org.glidemix.effects.dubecho");
}

EffectManifestPointer GlideMixDubEchoEffect::getManifest()
{
    auto m = baseManifest(getId(), QObject::tr("Dub Echo"),
                          QObject::tr("An echo whose repeats turn darker and dirtier as they fade, dub style."));
    addParameter(m, "delay_time", QObject::tr("Time"), QObject::tr("Delay in beats"), 0.125, 0.75, 2.0,
                 EffectManifestParameter::UnitsHint::Beats);
    addParameter(m, "feedback_amount", QObject::tr("Feedback"), QObject::tr("How long the repeats go on"), 0.0, 0.65,
                 0.97);
    addParameter(m, "send_amount", QObject::tr("Send"), QObject::tr("How much of the track goes into the echo"), 0.0,
                 0.7, 1.0);
    return m;
}

void GlideMixDubEchoEffect::loadEngineEffectParameters(const QMap<QString, EngineEffectParameterPointer>& parameters)
{
    m_time = parameters.value("delay_time");
    m_feedback = parameters.value("feedback_amount");
    m_send = parameters.value("send_amount");
}

void GlideMixDubEchoEffect::processChannel(GlideMixDelayState* s, const CSAMPLE* in, CSAMPLE* out,
                                           const mixxx::EngineParameters& p, const EffectEnableState,
                                           const GroupFeatureState& gf)
{
    const SINT frames = p.framesPerBuffer();
    const size_t size = s->left.size();
    const double beats = snapBeats(m_time->value(), { 0.125, 0.25, 1.0 / 3.0, 0.5, 0.75, 1.0, 1.5, 2.0 });
    const size_t delay = std::clamp<size_t>(static_cast<size_t>(beats * beatSeconds(gf) * s->sampleRate), 1, size - 1);
    const double fb = m_feedback->value();
    const double send = m_send->value();
    const double lpCoef = 1.0 - std::exp(-2.0 * kPi * 2200.0 / s->sampleRate);   // each repeat a little darker
    const double hpCoef = std::exp(-2.0 * kPi * 180.0 / s->sampleRate);          // and thinner at the bottom
    for (SINT i = 0; i < frames; ++i) {
        const size_t w = s->write % size;
        const size_t r = (s->write + size - delay) % size;
        for (int c = 0; c < 2; ++c) {
            std::vector<float>& line = c == 0 ? s->left : s->right;
            const double d = line[r];
            s->lp[c] += (d - s->lp[c]) * lpCoef;
            const double high = hpCoef * (s->hp[c] + s->lp[c] - s->hpIn[c]);
            s->hpIn[c] = s->lp[c];
            s->hp[c] = high;
            const double wet = std::tanh(1.3 * high) / 1.3;   // tape-like softness
            line[w] = static_cast<float>(in[2 * i + c] * send + fb * wet);
            out[2 * i + c] = static_cast<float>(in[2 * i + c] + d);
        }
        ++s->write;
    }
}

// ---- Sweep -------------------------------------------------------------------

QString GlideMixSweepEffect::getId()
{
    return QStringLiteral("org.glidemix.effects.sweep");
}

EffectManifestPointer GlideMixSweepEffect::getManifest()
{
    auto m = baseManifest(getId(), QObject::tr("Sweep"),
                          QObject::tr("A band-pass filter swept up and down in time with the beat."));
    addParameter(m, "rate", QObject::tr("Rate"), QObject::tr("One sweep up and down, in beats: 1/2 to 8"), 0.5, 2.0,
                 8.0, EffectManifestParameter::UnitsHint::Beats);
    addParameter(m, "depth", QObject::tr("Depth"), QObject::tr("How much of the sound goes through the sweep"), 0.0,
                 1.0, 1.0);
    return m;
}

void GlideMixSweepEffect::loadEngineEffectParameters(const QMap<QString, EngineEffectParameterPointer>& parameters)
{
    m_rate = parameters.value("rate");
    m_depth = parameters.value("depth");
}

void GlideMixSweepEffect::processChannel(GlideMixDelayState* s, const CSAMPLE* in, CSAMPLE* out,
                                         const mixxx::EngineParameters& p, const EffectEnableState,
                                         const GroupFeatureState& gf)
{
    const SINT frames = p.framesPerBuffer();
    const double rate = snapBeats(m_rate->value(), { 0.5, 1.0, 2.0, 4.0, 8.0 });
    const double depth = m_depth->value();
    constexpr double kQ = 0.6;   // damping: a clear but not whistling peak
    for (SINT i = 0; i < frames; ++i) {
        const double pos = beatPositionAt(gf, frames, i, s->sampleRate, static_cast<double>(s->write)) / rate;
        const double lfo = 0.5 - 0.5 * std::cos(2.0 * kPi * (pos - std::floor(pos)));
        const double cutoff = 250.0 * std::pow(6000.0 / 250.0, lfo);
        const double f = 2.0 * std::sin(kPi * std::min(cutoff, s->sampleRate * 0.2) / s->sampleRate);
        for (int c = 0; c < 2; ++c) {
            const double x = in[2 * i + c];
            // Chamberlin state-variable filter: bp1 = low, bp2 = band.
            s->bp1[c] += f * s->bp2[c];
            const double high = x - s->bp1[c] - kQ * s->bp2[c];
            s->bp2[c] += f * high;
            out[2 * i + c] = static_cast<float>(x * (1.0 - depth) + s->bp2[c] * depth * 1.4);
        }
    }
    s->write += static_cast<size_t>(frames);
}

// ---- Tape Stop -----------------------------------------------------------------

QString GlideMixTapeStopEffect::getId()
{
    return QStringLiteral("org.glidemix.effects.tapestop");
}

EffectManifestPointer GlideMixTapeStopEffect::getManifest()
{
    auto m = baseManifest(getId(), QObject::tr("Tape Stop"),
                          QObject::tr("Slows the track down to a stop. Drawn fast it is a vinyl brake, slow a tape "
                                      "stop; back to full speed it plays on from where it is."));
    addParameter(m, "speed", QObject::tr("Speed"), QObject::tr("1 = normal, 0 = stopped"), 0.0, 1.0, 1.0);
    return m;
}

void GlideMixTapeStopEffect::loadEngineEffectParameters(const QMap<QString, EngineEffectParameterPointer>& parameters)
{
    m_speed = parameters.value("speed");
}

void GlideMixTapeStopEffect::processChannel(GlideMixDelayState* s, const CSAMPLE* in, CSAMPLE* out,
                                            const mixxx::EngineParameters& p, const EffectEnableState,
                                            const GroupFeatureState&)
{
    const SINT frames = p.framesPerBuffer();
    const size_t size = s->left.size();
    const double target = std::clamp(m_speed->value(), 0.0, 1.0);
    if (target >= 0.999 && s->active && s->speed >= 0.999) {
        s->active = false;   // back to the live signal
    }
    if (target < 0.999 && !s->active) {
        s->active = true;
        s->readPos = static_cast<double>(s->write);
        s->speed = 1.0;
    }
    const double glide = 1.0 - std::exp(-1.0 / (0.01 * s->sampleRate));   // speed follows in ~10 ms
    for (SINT i = 0; i < frames; ++i) {
        const size_t w = s->write % size;
        s->left[w] = in[2 * i];
        s->right[w] = in[2 * i + 1];
        ++s->write;
        if (!s->active) {
            out[2 * i] = in[2 * i];
            out[2 * i + 1] = in[2 * i + 1];
            continue;
        }
        s->speed += (target - s->speed) * glide;
        // Never read what hasn't been written, nor what has been overwritten.
        const double newest = static_cast<double>(s->write - 1);
        const double oldest = newest - static_cast<double>(size - 2);
        s->readPos = std::clamp(s->readPos, oldest, newest);
        const size_t a = static_cast<size_t>(s->readPos);
        const double t = s->readPos - static_cast<double>(a);
        const size_t ia = a % size;
        const size_t ib = (a + 1) % size;
        // Quieter as it stops, as a record does.
        const float level = static_cast<float>(std::min(1.0, s->speed * 3.0));
        out[2 * i] = static_cast<float>((1.0 - t) * s->left[ia] + t * s->left[ib]) * level;
        out[2 * i + 1] = static_cast<float>((1.0 - t) * s->right[ia] + t * s->right[ib]) * level;
        s->readPos += s->speed;
    }
}
