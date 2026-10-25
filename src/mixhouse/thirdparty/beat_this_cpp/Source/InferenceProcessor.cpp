#include "InferenceProcessor.h"
#include "beat_this_api.h"
#include <algorithm>
#include <atomic>
#include <future>
#include <numeric>
#include <thread>

namespace {
BeatThis::ChunkRunner& chunkRunner()
{
    static BeatThis::ChunkRunner runner;
    return runner;
}
// Songs in the network right now: the pieces of one song run side by side
// on the cores the other songs leave free.
std::atomic<int> s_activeSongs { 0 };
}

namespace BeatThis {
void setChunkRunner(ChunkRunner runner) { chunkRunner() = std::move(runner); }
bool hasChunkRunner() { return static_cast<bool>(chunkRunner()); }
}


InferenceProcessor::InferenceProcessor(Ort::Session& session, Ort::Env& env)
    : session_(session), env_(env) {}

// Helper to run ONNX inference on a single chunk
std::pair<std::vector<float>, std::vector<float>> InferenceProcessor::run_onnx_inference(
    const std::vector<std::vector<float>>& chunk_spect
) {
    // Prepare ONNX Input Tensor
    size_t input_tensor_size = chunk_spect.size() * chunk_spect[0].size();
    std::vector<float> input_tensor_values(input_tensor_size);
    // Flatten the spectrogram
    for(size_t i=0; i < chunk_spect.size(); ++i) {
        memcpy(input_tensor_values.data() + i * chunk_spect[0].size(), chunk_spect[i].data(), chunk_spect[0].size() * sizeof(float));
    }
    
    std::vector<int64_t> input_shape = {1, (int64_t)chunk_spect.size(), (int64_t)chunk_spect[0].size()};

    Ort::MemoryInfo memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    Ort::Value input_tensor = Ort::Value::CreateTensor<float>(memory_info, input_tensor_values.data(), input_tensor_size, input_shape.data(), input_shape.size());

    const char* input_names[] = {"input_spectrogram"};
    const char* output_names[] = {"beat", "downbeat"};
    
    auto output_tensors = session_.Run(Ort::RunOptions{nullptr}, input_names, &input_tensor, 1, output_names, 2);

    float* beat_output_data = output_tensors[0].GetTensorMutableData<float>();
    float* downbeat_output_data = output_tensors[1].GetTensorMutableData<float>();

    auto beat_shape = output_tensors[0].GetTensorTypeAndShapeInfo().GetShape();
    size_t beat_output_size = beat_shape[0] * beat_shape[1]; // Assuming batch_size=1

    std::vector<float> beat_logits(beat_output_data, beat_output_data + beat_output_size);
    std::vector<float> downbeat_logits(downbeat_output_data, downbeat_output_data + beat_output_size);


    return {beat_logits, downbeat_logits};
}


// Helper function: zeropad (from Python's inference.py)
std::vector<std::vector<float>> zeropad(
    const std::vector<std::vector<float>>& spect,
    int left,
    int right
) {
    if (left == 0 && right == 0) {
        return spect;
    } else {
        int original_rows = spect.size();
        int original_cols = spect[0].size();
        std::vector<std::vector<float>> padded_spect(original_rows + left + right, std::vector<float>(original_cols, 0.0f));
        for (int i = 0; i < original_rows; ++i) {
            for (int j = 0; j < original_cols; ++j) {
                padded_spect[i + left][j] = spect[i][j];
            }
        }
        return padded_spect;
    }
}

// Helper function: split_piece (from Python's inference.py)
std::vector<std::vector<std::vector<float>>> InferenceProcessor::split_piece(
    const std::vector<std::vector<float>>& spect,
    int chunk_size,
    int border_size
) {
    std::vector<std::vector<std::vector<float>>> chunks;
    std::vector<int> starts;

    int len_spect = spect.size();
    
    // generate the start and end indices
    for (int start = -border_size; start < len_spect - border_size; start += (chunk_size - 2 * border_size)) {
        starts.push_back(start);
    }

    // If avoid_short_end is true (which it is in Python), adjust the last start
    if (len_spect > chunk_size - 2 * border_size) {
        starts.back() = len_spect - (chunk_size - border_size);
    }

    // generate the chunks
    for (int start : starts) {
        int actual_start = std::max(0, start);
        int actual_end = std::min(start + chunk_size, len_spect);
        
        std::vector<std::vector<float>> current_chunk;
        if (actual_end > actual_start) {
            current_chunk.assign(spect.begin() + actual_start, spect.begin() + actual_end);
        }

        int left_pad = std::max(0, -start);
        int right_pad = std::max(0, std::min(border_size, start + chunk_size - len_spect));
        
        chunks.push_back(zeropad(current_chunk, left_pad, right_pad));
    }
    return chunks;
}

// Helper function: aggregate_prediction (from Python's inference.py)
std::pair<std::vector<float>, std::vector<float>> InferenceProcessor::aggregate_prediction(
    const std::vector<std::pair<std::vector<float>, std::vector<float>>>& pred_chunks,
    const std::vector<int>& starts,
    int full_size,
    int chunk_size,
    int border_size
) {
    std::vector<float> piece_prediction_beat(full_size, -1000.0f);
    std::vector<float> piece_prediction_downbeat(full_size, -1000.0f);

    // Python's overlap_mode is "keep_first", which means processing in reverse order
    for (int i = pred_chunks.size() - 1; i >= 0; --i) {
        const auto& pchunk = pred_chunks[i];
        int start = starts[i];

        // Cut the predictions to discard the border
        const std::vector<float>& beat_chunk = pchunk.first;
        const std::vector<float>& downbeat_chunk = pchunk.second;

        // Ensure the chunk is large enough to cut borders
        if (beat_chunk.size() < 2 * border_size) {
            // Handle cases where chunk is too small for borders, e.g., very short audio
            // For simplicity, we'll just copy the whole chunk if it's too small
            // A more robust solution might involve different padding/slicing logic
            for (size_t j = 0; j < beat_chunk.size(); ++j) {
                int target_idx = start + j;
                if (target_idx >= 0 && target_idx < full_size) {
                    piece_prediction_beat[target_idx] = beat_chunk[j];
                    piece_prediction_downbeat[target_idx] = downbeat_chunk[j];
                }
            }
        } else {
            for (int j = border_size; j < beat_chunk.size() - border_size; ++j) {
                int target_idx = start + j;
                if (target_idx >= 0 && target_idx < full_size) {
                    piece_prediction_beat[target_idx] = beat_chunk[j];
                    piece_prediction_downbeat[target_idx] = downbeat_chunk[j];
                }
            }
        }
    }
    return {piece_prediction_beat, piece_prediction_downbeat};
}

// Main processing method
std::pair<std::vector<float>, std::vector<float>> InferenceProcessor::process_spectrogram(
    const std::vector<std::vector<float>>& spectrogram
) {
    std::vector<std::vector<std::vector<float>>> chunks = split_piece(spectrogram, chunk_size, border_size);

    std::vector<std::pair<std::vector<float>, std::vector<float>>> pred_chunks;
    std::vector<int> starts;

    // Re-calculate starts for aggregation, as split_piece doesn't return them directly
    // This is a simplified re-calculation based on the Python logic
    int len_spect = spectrogram.size();
    for (int start = -border_size; start < len_spect - border_size; start += (chunk_size - 2 * border_size)) {
        starts.push_back(start);
    }
    if (len_spect > chunk_size - 2 * border_size) {
        starts.back() = len_spect - (chunk_size - border_size);
    }

    // GlideMix patch: Core ML when installed (see setChunkRunner), else
    // ONNX Runtime with the pieces side by side on the free cores.
    const auto runOne = [this](const std::vector<std::vector<float>>& chunk) {
        if (const auto& runner = chunkRunner(); runner && !chunk.empty()) {
            const int frames = static_cast<int>(chunk.size());
            const int bins = static_cast<int>(chunk[0].size());
            std::vector<float> flat(static_cast<size_t>(frames) * bins);
            for (int i = 0; i < frames; ++i) {
                std::copy(chunk[i].begin(), chunk[i].end(), flat.begin() + static_cast<size_t>(i) * bins);
            }
            std::vector<float> beat(frames), downbeat(frames);
            if (runner(flat.data(), frames, bins, beat.data(), downbeat.data())) {
                return std::make_pair(std::move(beat), std::move(downbeat));
            }
        }
        return run_onnx_inference(chunk);
    };
    const int active = ++s_activeSongs;
    const int cores = static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
    const int parallel = BeatThis::hasChunkRunner() ? 1 : std::clamp(cores / std::max(1, active), 1, static_cast<int>(chunks.size()));
    pred_chunks.resize(chunks.size());
    if (parallel <= 1) {
        for (size_t i = 0; i < chunks.size(); ++i) {
            pred_chunks[i] = runOne(chunks[i]);
        }
    } else {
        std::atomic<size_t> next { 0 };
        std::vector<std::future<void>> workers;
        for (int w = 0; w < parallel; ++w) {
            workers.push_back(std::async(std::launch::async, [&]() {
                for (size_t i = next++; i < chunks.size(); i = next++) {
                    pred_chunks[i] = runOne(chunks[i]);
                }
            }));
        }
        for (auto& w : workers) {
            w.get();
        }
    }
    --s_activeSongs;

    return aggregate_prediction(pred_chunks, starts, spectrogram.size(), chunk_size, border_size);
}