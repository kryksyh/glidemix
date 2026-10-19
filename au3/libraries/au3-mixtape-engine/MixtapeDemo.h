/*
 * GlideMix: the demo version's limits (no licence entered). Set by the
 * licence service; read by the renderer and the export.
 */
#pragma once

#include <cstddef>
#include <cstdint>

namespace mixtape {
void SetDemoMode(bool demo);
bool DemoMode();

//! Seconds of mix a demo export may contain.
constexpr double kDemoExportLimitSec = 20.0 * 60.0;

//! A female voice says "Glide Mix demo" over the music, first after
//! kDemoVoiceFirstSec and then every kDemoVoicePeriodSec, on every output
//! (play, perform, broadcast, export): a clean output could be recorded.
//! The music ducks under it, so it is always heard and never clips.
constexpr double kDemoVoiceFirstSec = 30.0;
constexpr double kDemoVoicePeriodSec = 120.0;

//! Seconds the voice lasts.
double DemoVoiceSeconds();

//! Mixes the voice into one block. `outputFrame` counts this renderer's
//! frames since it started, so seeking does not skip a tag.
void ApplyDemoVoice(float* const* out, size_t channels, size_t frames, int64_t outputFrame, double rate);
}
