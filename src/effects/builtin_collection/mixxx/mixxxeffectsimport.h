/*
 * GlideMix
 */
#pragma once

namespace au::effects {
class IEffectsConfiguration;

//! Takes over Mixxx's Visible / Hidden effects lists (effects.xml) for the
//! DJ Effects menu, once. Later changes are made in Preferences > Effects.
void importMixxxEffectListsOnce(IEffectsConfiguration& configuration);

//! Keeps the DJ EQ effects' crossovers on the Preferences values (taken over
//! from Mixxx's [Mixer Profile] once).
void connectEqFrequencies(IEffectsConfiguration& configuration);
}
