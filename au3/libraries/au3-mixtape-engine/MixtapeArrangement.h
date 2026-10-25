/*
 * GlideMix -- playback engine
 *
 * An immutable copy of what the engine plays. The main thread builds a new
 * Arrangement after every edit and hands it over; the render thread picks it
 * up at the next block. Nothing here points at mutable project data: sample
 * blocks are immutable and shared, envelopes are copied. That is what lets
 * edits happen while the music keeps playing.
 */
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

class Envelope;
class SampleBlock;
class TrackList;
class WaveClip;
class WaveTrack;

namespace mixtape {
//! One stored block of a clip channel, `start` samples into the sequence.
struct SourceBlock {
    int64_t start = 0;
    std::shared_ptr<SampleBlock> block;
};

struct SourceChannel {
    std::vector<SourceBlock> blocks;
    int64_t numSamples = 0;
};

//! Keeps the decoded samples of the blocks last read alive. A sample block
//! caches its decoded floats only while someone holds them (a weak_ptr), so
//! reading without this decoded the whole block from the project database
//! again for every 256 frames.
struct SourceCursor {
    size_t index = static_cast<size_t>(-1);
    std::shared_ptr<std::vector<float> > view;
    size_t nextIndex = static_cast<size_t>(-1);
    std::shared_ptr<std::vector<float> > nextView;
};

//! Zero-fills outside the channel. Render-thread safe: reads immutable blocks.
//! `cursor` (one per voice and channel) makes consecutive reads cheap.
void ReadSource(const SourceChannel& channel, int64_t position, float* out, size_t count,
                SourceCursor* cursor = nullptr);

struct ClipSnapshot {
    int64_t clipId = 0;
    double sampleRate = 44100.0;
    std::vector<SourceChannel> channels;
    //! Timeline second where source sample 0 plays.
    double sequenceStart = 0.0;
    //! The audible part on the timeline.
    double playStart = 0.0;
    double playEnd = 0.0;
    //! Timeline seconds per source second.
    double stretchRatio = 1.0;
    int centShift = 0;
    //! The clip's volume line (absolute timeline times); may be null.
    std::shared_ptr<const Envelope> envelope;
    //! The clip's bass line (absolute timeline times, 0..1: 1 leaves the bass
    //! alone, 0 removes it). Null for clips without one, which then skip the
    //! bass stage entirely and play bit for bit as before.
    std::shared_ptr<const Envelope> lowBand;
    std::shared_ptr<const Envelope> midBand;    // GlideMix: the transition EQ's mids and highs
    std::shared_ptr<const Envelope> highBand;

    //! Timeline time of a source sample and back.
    double TimeOfSource(double sample) const { return sequenceStart + sample / sampleRate * stretchRatio; }
    double SourceAt(double time) const { return (time - sequenceStart) / stretchRatio * sampleRate; }

    //! Same audio at the same place: a voice playing the old one can go on.
    bool SameAudio(const ClipSnapshot& other) const;
};

struct TrackSnapshot {
    int64_t trackId = 0;
    float volume = 1.0f;
    float pan = 0.0f;
    bool mute = false;
    bool solo = false;
    std::vector<ClipSnapshot> clips;
    //! Keeps the track (its effect list) alive while the engine may still
    //! process it, even if it is deleted meanwhile. Null in tests.
    std::shared_ptr<const WaveTrack> track;
    //! Changes when clips move or automation curves change: automation needs rebuilding.
    uint64_t automationSignature = 0;
    //! Frames the track's effects delay it by (their reported latency).
    size_t latencyFrames = 0;

    float ChannelGain(size_t channel) const;
};

struct Arrangement {
    std::vector<TrackSnapshot> tracks;
    uint64_t generation = 0;
    bool anySolo = false;
    //! GlideMix: the master effects' latency (a look-ahead limiter ...), in
    //! output frames; the whole mix comes out that much later.
    size_t masterLatencyFrames = 0;
};

ClipSnapshot SnapshotClip(const WaveClip& clip);
TrackSnapshot SnapshotTrack(const WaveTrack& track);
std::shared_ptr<Arrangement> SnapshotArrangement(const TrackList& tracks, uint64_t generation);
}
