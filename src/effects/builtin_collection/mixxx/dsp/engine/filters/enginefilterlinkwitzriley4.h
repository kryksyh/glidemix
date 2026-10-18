#pragma once

#include "audio/types.h"
#include "engine/filters/enginefilteriir.h"

class EngineFilterLinkwitzRiley4Low : public EngineFilterIIR<4, IIR_LP> {
  public:
    EngineFilterLinkwitzRiley4Low(mixxx::audio::SampleRate sampleRate, double freqCorner1);
    void setFrequencyCorners(mixxx::audio::SampleRate sampleRate, double freqCorner1);
};


class EngineFilterLinkwitzRiley4High : public EngineFilterIIR<4, IIR_HP> {
  public:
    EngineFilterLinkwitzRiley4High(mixxx::audio::SampleRate sampleRate, double freqCorner1);
    void setFrequencyCorners(mixxx::audio::SampleRate sampleRate, double freqCorner1);
};
