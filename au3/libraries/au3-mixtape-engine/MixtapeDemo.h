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

//! Every kDemoGapPeriodSec of output, kDemoGapSec go silent (an export also
//! gets a soft noise in the gap). Faded, so it never clicks.
constexpr double kDemoGapPeriodSec = 120.0;
constexpr double kDemoGapSec = 3.0;

//! Applies the gaps to one block. `outputFrame` counts this renderer's
//! frames since it started, so seeking does not skip a gap.
void ApplyDemoGaps(float* const* out, size_t channels, size_t frames, int64_t outputFrame, double rate,
                   bool addNoise, uint32_t& noiseState);
}
