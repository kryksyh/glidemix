/*
 * GlideMix -- playback engine
 */
#include "MixtapeArrangement.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "au3-mixer/Envelope.h"
#include "au3-track/Track.h"
#include "au3-wave-track/Sequence.h"
#include "au3-wave-track/SampleBlock.h"
#include "au3-wave-track/WaveClip.h"
#include "au3-wave-track/WaveTrack.h"

namespace mixtape {
void ReadSource(const SourceChannel& channel, int64_t position, float* out, size_t count, SourceCursor* cursor)
{
    size_t done = 0;
    if (position < 0) {
        const size_t silence = static_cast<size_t>(std::min<int64_t>(count, -position));
        std::fill_n(out, silence, 0.0f);
        done = silence;
        position += static_cast<int64_t>(silence);
    }
    const auto& blocks = channel.blocks;
    // The block holding `position`: the last one starting at or before it.
    auto it = std::upper_bound(blocks.begin(), blocks.end(), position,
                               [](int64_t pos, const SourceBlock& b) { return pos < b.start; });
    size_t index = static_cast<size_t>(it - blocks.begin());
    if (index == 0) {
        std::fill(out + done, out + count, 0.0f);
        return;
    }
    --index;
    while (done < count && position < channel.numSamples && index < blocks.size()) {
        const SourceBlock& block = blocks[index];
        std::shared_ptr<std::vector<float> > view;
        if (cursor && cursor->index == index && cursor->view) {
            view = cursor->view;
        } else if (cursor && cursor->nextIndex == index && cursor->nextView) {
            view = cursor->nextView;
        } else {
            view = block.block ? block.block->GetFloatSampleView(false) : nullptr;
        }
        if (cursor && view) {
            // The block being played and the one just before it (a read
            // straddling the boundary needs both).
            if (cursor->index != index) {
                cursor->nextIndex = cursor->index;
                cursor->nextView = std::move(cursor->view);
                cursor->index = index;
                cursor->view = view;
            }
        }
        const int64_t offset = position - block.start;
        const int64_t blockSize = view ? static_cast<int64_t>(view->size()) : 0;
        if (offset < 0 || offset >= blockSize) {
            ++index;
            continue;
        }
        const size_t n = static_cast<size_t>(std::min<int64_t>(count - done, blockSize - offset));
        std::memcpy(out + done, view->data() + offset, n * sizeof(float));
        done += n;
        position += static_cast<int64_t>(n);
        ++index;
    }
    std::fill(out + done, out + count, 0.0f);
}

bool ClipSnapshot::SameAudio(const ClipSnapshot& other) const
{
    if (clipId != other.clipId || sampleRate != other.sampleRate || sequenceStart != other.sequenceStart
        || stretchRatio != other.stretchRatio || centShift != other.centShift
        || channels.size() != other.channels.size()) {
        return false;
    }
    for (size_t c = 0; c < channels.size(); ++c) {
        const auto& a = channels[c];
        const auto& b = other.channels[c];
        if (a.numSamples != b.numSamples || a.blocks.size() != b.blocks.size()) {
            return false;
        }
        for (size_t i = 0; i < a.blocks.size(); ++i) {
            if (a.blocks[i].block != b.blocks[i].block || a.blocks[i].start != b.blocks[i].start) {
                return false;
            }
        }
    }
    return true;
}

float TrackSnapshot::ChannelGain(size_t channel) const
{
    // WaveTrack::GetChannelVolume
    float left = 1.0f;
    float right = 1.0f;
    if (pan < 0) {
        right = pan + 1.0f;
    } else if (pan > 0) {
        left = 1.0f - pan;
    }
    return volume * ((channel % 2) == 0 ? left : right);
}

ClipSnapshot SnapshotClip(const WaveClip& clip)
{
    ClipSnapshot snap;
    snap.clipId = clip.GetId();
    snap.sampleRate = clip.GetRate();
    snap.sequenceStart = clip.GetSequenceStartTime();
    snap.playStart = clip.GetPlayStartTime();
    snap.playEnd = clip.GetPlayEndTime();
    snap.stretchRatio = clip.GetStretchRatio();
    snap.centShift = clip.GetCentShift();
    snap.envelope = std::make_shared<const Envelope>(clip.GetEnvelope());
    for (const auto& curve : clip.GetParameterAutomation().Curves()) {
        if (!curve.enabled || curve.effectInstanceId != clipautomation::kMixtapeBuiltinInstanceId
            || curve.envelope.GetNumberOfPoints() == 0) {
            continue;
        }
        if (curve.parameterId == clipautomation::kMixtapeLowBandParameter) {
            snap.lowBand = std::make_shared<const Envelope>(curve.envelope);
        } else if (curve.parameterId == clipautomation::kMixtapeMidBandParameter) {
            snap.midBand = std::make_shared<const Envelope>(curve.envelope);
        } else if (curve.parameterId == clipautomation::kMixtapeHighBandParameter) {
            snap.highBand = std::make_shared<const Envelope>(curve.envelope);
        }
    }
    for (size_t ii = 0; ii < clip.NChannels(); ++ii) {
        SourceChannel channel;
        if (const Sequence* sequence = clip.GetSequence(ii)) {
            channel.numSamples = sequence->GetNumSamples().as_long_long();
            for (const SeqBlock& block : sequence->GetBlockArray()) {
                channel.blocks.push_back({ block.start.as_long_long(), block.sb });
            }
        }
        snap.channels.push_back(std::move(channel));
    }
    return snap;
}

TrackSnapshot SnapshotTrack(const WaveTrack& track)
{
    TrackSnapshot snap;
    snap.trackId = track.GetId().raw();
    snap.volume = track.GetVolume();
    snap.pan = track.GetPan();
    snap.mute = track.GetMute();
    snap.solo = track.GetSolo();
    snap.track = track.SharedPointer<const WaveTrack>();
    uint64_t signature = 1469598103934665603ull;
    const auto mix = [&signature](uint64_t value) {
        signature = (signature ^ value) * 1099511628211ull;
    };
    for (const auto& clip : track.Intervals()) {
        if (clip->GetPlayEndTime() > clip->GetPlayStartTime()) {
            snap.clips.push_back(SnapshotClip(*clip));
        }
        mix(static_cast<uint64_t>(clip->GetId()));
        mix(static_cast<uint64_t>(std::llround(clip->GetPlayStartTime() * 1e6)));
        mix(static_cast<uint64_t>(std::llround(clip->GetPlayEndTime() * 1e6)));
        mix(static_cast<uint64_t>(std::llround(clip->GetStretchRatio() * 1e9)));
        for (const auto& curve : clip->GetParameterAutomation().Curves()) {
            mix(curve.envelope.GetNumberOfPoints());
            mix(curve.bezier.segments.size());
            mix(curve.enabled ? 1 : 2);
        }
    }
    snap.automationSignature = signature;
    return snap;
}

std::shared_ptr<Arrangement> SnapshotArrangement(const TrackList& tracks, uint64_t generation)
{
    auto arrangement = std::make_shared<Arrangement>();
    arrangement->generation = generation;
    for (const WaveTrack* track : tracks.Any<const WaveTrack>()) {
        arrangement->tracks.push_back(SnapshotTrack(*track));
        arrangement->anySolo |= arrangement->tracks.back().solo;
    }
    return arrangement;
}
}
