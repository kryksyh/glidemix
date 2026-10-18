/*
 * GlideMix
 *
 * Every Mixxx built-in effect GlideMix registers, in Mixxx's
 * BuiltInBackend order: class, Mixxx id, Mixxx name, effect group.
 * Include with MIXTAPE_MIXXX_EFFECT(ClassName, MixxxId, Name, Group) defined.
 */
MIXTAPE_MIXXX_EFFECT(MixxxBessel4LVMixEQEffect, "org.mixxx.effects.bessel4lvmixeq", "Bessel4 LV-Mix Isolator", EqAndFilters)
MIXTAPE_MIXXX_EFFECT(MixxxBessel8LVMixEQEffect, "org.mixxx.effects.bessel8lvmixeq", "Bessel8 LV-Mix Isolator", EqAndFilters)
MIXTAPE_MIXXX_EFFECT(MixxxLinkwitzRiley8EQEffect, "org.mixxx.effects.linkwitzrileyeq", "LinkwitzRiley8 Isolator", EqAndFilters)
MIXTAPE_MIXXX_EFFECT(MixxxThreeBandBiquadEQEffect, "org.mixxx.effects.threebandbiquadeq", "Biquad Equalizer", EqAndFilters)
MIXTAPE_MIXXX_EFFECT(MixxxBiquadFullKillEQEffect, "org.mixxx.effects.biquadfullkilleq", "Biquad Full Kill Equalizer", EqAndFilters)
MIXTAPE_MIXXX_EFFECT(MixxxGraphicEQEffect, "org.mixxx.effects.graphiceq", "Graphic Equalizer", EqAndFilters)
MIXTAPE_MIXXX_EFFECT(MixxxParametricEQEffect, "org.mixxx.effects.parametriceq", "Parametric Equalizer", EqAndFilters)
MIXTAPE_MIXXX_EFFECT(MixxxLoudnessContourEffect, "org.mixxx.effects.loudnesscontour", "Loudness Contour", EqAndFilters)
MIXTAPE_MIXXX_EFFECT(MixxxFilterEffect, "org.mixxx.effects.filter", "Filter", EqAndFilters)
MIXTAPE_MIXXX_EFFECT(MixxxMoogLadder4FilterEffect, "org.mixxx.effects.moogladder4filter", "Moog Ladder 4 Filter", EqAndFilters)
MIXTAPE_MIXXX_EFFECT(MixxxBitCrusherEffect, "org.mixxx.effects.bitcrusher", "Bitcrusher", DistortionAndModulation)
MIXTAPE_MIXXX_EFFECT(MixxxWhiteNoiseEffect, "org.mixxx.effects.whitenoise", "White Noise", Special)
MIXTAPE_MIXXX_EFFECT(MixxxBalanceEffect, "org.mixxx.effects.balance", "Stereo Balance", Special)
MIXTAPE_MIXXX_EFFECT(MixxxFlangerEffect, "org.mixxx.effects.flanger", "Flanger", DistortionAndModulation)
MIXTAPE_MIXXX_EFFECT(MixxxEchoEffect, "org.mixxx.effects.echo", "Echo", DelayAndReverb)
MIXTAPE_MIXXX_EFFECT(MixxxAutoPanEffect, "org.mixxx.effects.autopan", "Autopan", DistortionAndModulation)
MIXTAPE_MIXXX_EFFECT(MixxxReverbEffect, "org.mixxx.effects.reverb", "Reverb", DelayAndReverb)
MIXTAPE_MIXXX_EFFECT(MixxxPhaserEffect, "org.mixxx.effects.phaser", "Phaser", DistortionAndModulation)
MIXTAPE_MIXXX_EFFECT(MixxxMetronomeEffect, "org.mixxx.effects.metronome", "Metronome", Special)
MIXTAPE_MIXXX_EFFECT(MixxxTremoloEffect, "org.mixxx.effects.tremolo", "Tremolo", DistortionAndModulation)
#ifdef MIXTAPE_HAS_RUBBERBAND
MIXTAPE_MIXXX_EFFECT(MixxxPitchShiftEffect, "org.mixxx.effects.pitchshift", "Pitch Shift", PitchAndTempo)
#endif
MIXTAPE_MIXXX_EFFECT(MixxxDistortionEffect, "org.mixxx.effects.distortion", "Distortion", DistortionAndModulation)
MIXTAPE_MIXXX_EFFECT(MixxxGlitchEffect, "org.mixxx.effects.glitch", "Glitch", DistortionAndModulation)
MIXTAPE_MIXXX_EFFECT(MixxxCompressorEffect, "org.mixxx.effects.compressor", "Compressor", VolumeAndCompression)
MIXTAPE_MIXXX_EFFECT(GlideMixPingPongEffectClass, "org.glidemix.effects.pingpong", "Ping Pong", DelayAndReverb)
MIXTAPE_MIXXX_EFFECT(GlideMixDubEchoEffectClass, "org.glidemix.effects.dubecho", "Dub Echo", DelayAndReverb)
MIXTAPE_MIXXX_EFFECT(GlideMixRollEffectClass, "org.glidemix.effects.roll", "Roll", Special)
MIXTAPE_MIXXX_EFFECT(GlideMixTransEffectClass, "org.glidemix.effects.trans", "Trans", DistortionAndModulation)
MIXTAPE_MIXXX_EFFECT(GlideMixSweepEffectClass, "org.glidemix.effects.sweep", "Sweep", EqAndFilters)
MIXTAPE_MIXXX_EFFECT(GlideMixTapeStopEffectClass, "org.glidemix.effects.tapestop", "Tape Stop", PitchAndTempo)
