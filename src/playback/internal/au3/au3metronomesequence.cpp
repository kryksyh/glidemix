/*
 * GlideMix
 */
#include "au3metronomesequence.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "au3-math/SampleFormat.h"
#include "au3-mixer/PlaybackSpeedCurve.h"
#include "au3-numeric-formats/ProjectTimeSignature.h"
#include "au3-project/Project.h"

using namespace au::playback;

Au3MetronomeSequence::Au3MetronomeSequence(AudacityProject& project, std::shared_ptr<IMetronome> metronome, double rate)
    : m_timeSignature(ProjectTimeSignature::Get(project)), m_metronome(std::move(metronome)), m_rate(rate)
{
    m_metronome->prepare(static_cast<int>(std::lround(rate)));
}

double Au3MetronomeSequence::GetEndTime() const
{
    // Endless: the play range, not the sequence, decides where playback stops.
    return std::numeric_limits<double>::max() / 4;
}

void Au3MetronomeSequence::GetEnvelopeValues(double* buffer, size_t bufferLen, double, bool) const
{
    std::fill(buffer, buffer + bufferLen, 1.0);
}

bool Au3MetronomeSequence::DoGet(size_t iChannel, size_t nBuffers, const samplePtr buffers[], sampleFormat format,
                                 sampleCount start, size_t len, bool backwards, fillFormat, bool, sampleCount* pNumWithinClips) const
{
    (void)iChannel;
    if (pNumWithinClips) {
        *pNumWithinClips = 0;
    }

    const IMetronome::Clicks* clicks = m_metronome->clicks();
    const double tempo = m_timeSignature.GetTempo();
    const int upper = m_timeSignature.GetUpperTimeSignature();
    const int lower = m_timeSignature.GetLowerTimeSignature();

    for (size_t b = 0; b < nBuffers; ++b) {
        std::fill_n(buffers[b], len * SAMPLE_SIZE(format), 0);
    }

    // The audio engine always mixes in float; anything else stays silent.
    if (format != floatSample || !clicks || !clicks->enabled || clicks->beat.empty() || tempo <= 0.0 || upper <= 0 || lower <= 0 || m_rate <= 0.0) {
        return true;
    }

    // ProjectTimeSignature::GetBeatDuration(): a quarter note scaled to the
    // time signature's beat unit. Bar 1 starts at time zero, as on the ruler.
    const double beatSec = (60.0 / tempo) * (4.0 / lower);
    const double samplesPerBeat = beatSec * m_rate;
    const int64_t first = start.as_long_long();
    // Tempo lane: playback resamples by the warp speed, which would pitch and
    // shorten the click. Reading the click at 1 / speed undoes both.
    const auto speedCurve = PlaybackSpeedCurve::Live();

    for (size_t i = 0; i < len; ++i) {
        const int64_t pos = backwards ? first - static_cast<int64_t>(i) - 1 : first + static_cast<int64_t>(i);
        if (pos < 0) {
            continue;
        }
        const double beatPos = pos / samplesPerBeat;
        const int64_t beat = static_cast<int64_t>(std::floor(beatPos));
        const double speed = speedCurve ? speedCurve->SpeedAt(beat * beatSec) : 1.0;
        const double offset = (beatPos - beat) * samplesPerBeat / (speed > 0.0 ? speed : 1.0);
        const bool accent = (beat % upper) == 0;
        const std::vector<float>& click = accent ? clicks->accent : clicks->beat;
        const auto index = static_cast<size_t>(offset);
        if (index + 1 >= click.size()) {
            continue;
        }
        const float frac = static_cast<float>(offset - index);
        const float sample = click[index] + (click[index + 1] - click[index]) * frac;
        // The knob reaches +24 dB, where the click passes full scale: clip it
        // here rather than hand the mixer samples beyond 1.0.
        const float value = std::clamp(sample * clicks->gain, -1.0f, 1.0f);
        for (size_t b = 0; b < nBuffers; ++b) {
            reinterpret_cast<float*>(buffers[b])[i] = value;
        }
    }
    return true;
}
