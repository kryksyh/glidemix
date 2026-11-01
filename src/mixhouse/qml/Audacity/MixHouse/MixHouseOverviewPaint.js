.pragma library

// GlideMix: the waveform overview's columns, for the library table and the
// Project tab. buckets come from overviewBuckets(), one per physical pixel.
function drawBuckets(ctx, width, height, buckets, dpr, overviewType) {
    var lowColor = Qt.rgba(1, 0, 0, 1)      // #FF0000
    var midColor = Qt.rgba(0, 1, 0, 1)      // #00FF00
    var highColor = Qt.rgba(0, 0, 1, 1)     // #0000FF

    // Real mixxx peak-normalizes the WHOLE track's
    // overview to fill the available height
    // (waveformoverviewrenderer.cpp's own render():
    // finds the loudest signal_all sample across the
    // entire track, then crops/rescales so THAT
    // sample reaches 100% height) -- unconditional,
    // not gated behind any Preferences gain setting
    // (confirmed: that gain/normalize radio only
    // ever reaches the deck's live WOverview widget,
    // never the library table's own overview column,
    // per real mixxx's own overviewcache.cpp). Max-
    // pooling into buckets already preserves the
    // track's true peak byte value (every bucket is
    // itself a max over its window), so finding it
    // here from the resampled buckets is exact, not
    // an approximation of finding it from the raw
    // samples. Shared by all three types below.
    //
    // Real rekordbox's own preview thumbnail, unlike
    // real mixxx's, does NOT let the loudest point
    // reach 100% of the cell height -- called
    // this "clipped" against a real rekordbox
    // screenshot twice now, first at 0.8 then again
    // at 0.6. the reference library is almost entirely
    // heavily-compressed/loud EDM mashups, where
    // signal_all sits near true peak for long
    // stretches -- meaning even a real, correct
    // per-track peak-normalize will legitimately
    // paint mostly-flat-topped bars for THESE
    // specific tracks, which is what likely still
    // reads as "clipped" rather than a shape/margin
    // problem as such. That real cause turned out to
    // be AmplitudeEnvelopeCollector's own peak-vs-RMS
    // choice (see mixhouseanalyzer.cpp), fixed there
    // directly -- once real per-bucket variation
    // existed, measured the real proportions
    // directly against a real rekordbox screenshot
    // (~3mm ours vs ~5mm real rekordbox, on screen),
    // a plain ~1.67x, so this is a pure visual-scale
    // constant now, not a proxy for the data problem
    // 0.6/0.45 were chasing.
    var kRekordboxHeadroom = 0.75
    var peak = 0
    for (var p = 0; p < buckets.length; ++p) {
        if (buckets[p].all > peak) {
            peak = buckets[p].all
        }
    }
    // GlideMix: our own analysis drawn at the height rekordbox's
    // previews show, so both kinds of row look alike.
    var heightScale = peak > 0 ? (255 / peak) * kRekordboxHeadroom * 0.8 : 1
    // Buckets sourced from rekordbox carry its own already-
    // normalised heights, so they are drawn as stored (only the
    // display headroom applies) rather than peak-normalised again.
    var fromRekordbox = buckets.length > 0 && buckets[0].hasColor === true
    if (fromRekordbox) {
        heightScale = 1.0   // heights are already a fraction of rekordbox's own row
    }

    var colW = 1 / dpr
    for (var i = 0; i < buckets.length; ++i) {
        var b = buckets[i]
        var x = i * colW
        // Snapped to whole physical pixels so edges
        // stay hard instead of antialiasing into
        // half-lit rows.
        var barPx = Math.max(1, Math.round(Math.min(height, (b.all * heightScale / 255) * height) * dpr))
        var barHeight = barPx / dpr
        var top = Math.round((height * dpr - barPx) / 2) / dpr

        if (overviewType === 3) {
            // White (MixHouse's own default, see
            // MixHouseWaveformOverviewType): plain
            // #FFFFFF, no band coloring at all.
            ctx.fillStyle = "#FFFFFF"
            ctx.fillRect(x, top, colW, barHeight)
        } else if (overviewType === 0) {
            // Filtered (real mixxx: drawWaveformPartLMH)
            // -- three separate flat-colored traces,
            // one per band, NO color blending at all.
            // Real mixxx draws each as its own full-
            // height line stroke; adapted here to
            // three semi-transparent overlapping bars
            // (each sized by that band's OWN
            // amplitude, not signal_all) since this
            // renderer draws filled 1px columns, not
            // line strokes -- same "three independent
            // traces, flat colors" look, not a pixel-
            // exact port of mixxx's own line-drawing.
            var lowH = Math.max(1, (b.low / 255) * height)
            var midH = Math.max(1, (b.mid / 255) * height)
            var highH = Math.max(1, (b.high / 255) * height)
            ctx.fillStyle = Qt.rgba(lowColor.r, lowColor.g, lowColor.b, 0.6)
            ctx.fillRect(x, (height - lowH) / 2, colW, lowH)
            ctx.fillStyle = Qt.rgba(midColor.r, midColor.g, midColor.b, 0.6)
            ctx.fillRect(x, (height - midH) / 2, colW, midH)
            ctx.fillStyle = Qt.rgba(highColor.r, highColor.g, highColor.b, 0.6)
            ctx.fillRect(x, (height - highH) / 2, colW, highH)
        } else if (overviewType === 1) {
            // HSV (real mixxx: drawWaveformPartHSV) --
            // fixed hue taken from the low band's OWN
            // color (lowColor.hsvHue), not a hardcoded
            // red -- this used to silently ignore
            // lowColor entirely, which would have kept
            // rendering red here even after switching
            // to rekordbox's blue/orange/white scheme
            // above. Mid contributes nothing to color
            // (real mixxx's own comment says so
            // explicitly), low/high energy modulate
            // saturation/value.
            var total = (b.low + b.mid + b.high) * 1.2
            var lo = total > 0 ? b.low / total : 0
            var hi = total > 0 ? b.high / total : 0
            ctx.fillStyle = Qt.hsva(lowColor.hsvHue, 1.0 - hi, 1.0 - lo, 1.0)
            ctx.fillRect(x, top, colW, barHeight)
        } else {
            // Rekordbox's own real "RGB" waveform mode
            // -- confirmed in testing this directly against
            // a real rekordbox screenshot of these
            // SAME tracks side by side (the "3Band"
            // layered-trace version tried first was
            // the wrong mode).
            //
            // An earlier version of this normalized so
            // the loudest of the three channels always
            // hit full brightness (divide-by-max) --
            // testing caught that it looked "washed and
            // not so crisp" compared to real
            // rekordbox. Real cause: normal music has
            // SOME energy in all three bands almost
            // always, so divide-by-max was forcing at
            // least one channel to 1.0 on nearly every
            // column, biasing everything toward pale/
            // white rather than a saturated hue. This
            // is also numerically why the ORIGINAL
            // (pre-today) mixxx-quirk rainbow had the
            // exact same washed quality -- with pure
            // red/green/blue band colors the quirk's
            // "paint low/mid/high straight into R/G/B,
            // divide by max(low,mid,high)" and this
            // formula's weighted-then-normalized
            // result are numerically identical, so
            // this was never actually a regression
            // introduced today.
            //
            // Dropping the normalization (dividing by
            // each column's own max) helped, but real
            // tracks -- especially dense, loud modern
            // masters like these -- usually have
            // meaningful energy in ALL three bands at
            // once. When R, G and B are all similarly
            // sized, the result is grayish/pale NO
            // MATTER how it's scaled -- normalized or
            // not -- because that's just what R=G=B
            // looks like. Confirmed in testing:
            // directly, track by track, against real
            // rekordbox: still washed out.
            //
            // Real fix: separate HUE from SATURATION.
            // Blend the weighted low/mid/high mix as
            // before to get a color whose HUE reflects
            // which band relatively dominates, but
            // then force saturation up to a vivid
            // floor and drive brightness from the
            // column's own overall level (b.all) --
            // exactly separating "what hue is this"
            // from "how loud is this", instead of
            // letting three similarly-sized channel
            // values wash each other out. This is a
            // real, standard technique (this file's
            // own existing HSV type above does the
            // same hue/saturation separation, just
            // with a fixed hue and only low/high
            // feeding it) -- applied here to the
            // blended hue instead of a fixed one.
            if (b.hasColor === true) {
                if (b.all === 0) {
                    continue   // leading silence: nothing drawn
                }
                // The colour rekordbox's preview shows for this column
                // (see MixHouseRekordboxPhrases::waveformOverview).
                var packed = b.color
                ctx.fillStyle = Qt.rgba(((packed >> 16) & 255) / 255,
                                        ((packed >> 8) & 255) / 255,
                                        (packed & 255) / 255, 1.0)
                ctx.fillRect(x, top, colW, barHeight)
                continue
            }
            // GlideMix: weighted and softened like rekordbox's own
            // preview. Dance music is bass-heavy, so an even blend
            // came out red nearly everywhere (and neon at full
            // saturation); mids and highs are lifted so the
            // sections still read, and the palette is pastel.
            var wLow = b.low, wMid = b.mid * 1.4, wHigh = b.high * 2.0
            var mixR = wLow * lowColor.r + wMid * midColor.r + wHigh * highColor.r
            var mixG = wLow * lowColor.g + wMid * midColor.g + wHigh * highColor.g
            var mixB = wLow * lowColor.b + wMid * midColor.b + wHigh * highColor.b
            var mixMax = Math.max(mixR, mixG, mixB)
            var hue = mixMax > 0 ? Qt.rgba(mixR / mixMax, mixG / mixMax, mixB / mixMax, 1.0).hsvHue : 0
            // Brightness stays at full: the bar HEIGHT above already
            // carries the level (peak-normalized with the rekordbox
            // headroom), so dimming the colour by b.all as well counted
            // loudness twice -- quiet passages went to near-black and the
            // whole column read dark and muddy. The beatgrid editor, which
            // confirmed in testing looks right, draws its bands at full strength
            // for exactly this reason (mixhousegrideditorwave.cpp's kLow/
            // kMid/kHigh are flat opaque colours) and lets height alone
            // show level. Same rule here, so the two views agree.
            ctx.fillStyle = Qt.hsva(hue, 0.5, 0.88, 1.0)
            ctx.fillRect(x, top, colW, barHeight)
        }
    }

}
