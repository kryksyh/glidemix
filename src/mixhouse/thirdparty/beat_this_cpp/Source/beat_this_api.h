#pragma once

#include <vector>
#include <string>
#include <memory>
#include <utility>
#include <functional>

namespace BeatThis {

// GlideMix patch: runs one piece of the spectrogram (frames x 128 mel bins,
// frames <= 1500) through the network somewhere else -- Core ML on the Mac
// -- and writes `frames` beat and downbeat logits. Returns false to fall
// back to ONNX Runtime. Set once, before the first analysis.
using ChunkRunner = std::function<bool(const float* spect, int frames, int bins, float* beat, float* downbeat)>;
void setChunkRunner(ChunkRunner runner);
bool hasChunkRunner();

struct BeatResult {
    std::vector<float> beats;
    std::vector<float> downbeats;
    std::vector<int> beat_counts; // Beat numbers for each beat (1 = downbeat, 2,3,4... = other beats)
};

class BeatThis {
public:
    explicit BeatThis(const std::string& onnx_model_path, bool use_dbn = false);
    ~BeatThis();

    // Move semantics
    BeatThis(BeatThis&&) = default;
    BeatThis& operator=(BeatThis&&) = default;

    // Delete copy semantics
    BeatThis(const BeatThis&) = delete;
    BeatThis& operator=(const BeatThis&) = delete;

    // Process audio and get beat/downbeat times as vector data
    BeatResult process_audio(
        const std::vector<float>& audio_data,
        int samplerate,
        int channels = 1
    );

    // Overload for raw pointer interface
    BeatResult process_audio(
        const float* audio_data,
        size_t num_samples,
        int samplerate,
        int channels = 1
    );


private:
    class Impl;
    std::unique_ptr<Impl> pImpl;
};

} // namespace BeatThis