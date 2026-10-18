/*
 * GlideMix
 */
#pragma once

#include <cstddef>
#include <vector>

struct lame_global_struct;

namespace au::broadcast {
/// The stream's MP3 encoder: LAME, the same one the app exports MP3 with.
/// Takes interleaved stereo float blocks straight from the engine.
class Mp3Encoder
{
public:
    Mp3Encoder() = default;
    ~Mp3Encoder();

    Mp3Encoder(const Mp3Encoder&) = delete;
    Mp3Encoder& operator=(const Mp3Encoder&) = delete;

    /// `channels` is 1 or 2; anything else is refused. Returns false when LAME
    /// will not take the settings (an unsupported sample rate, say).
    bool open(double sampleRate, int channels, int bitrateKbps);
    void close();
    bool isOpen() const { return m_lame != nullptr; }

    /// Encodes one block; appends to `out`. Returns false on an encoder error.
    bool encode(const float* interleaved, size_t frames, std::vector<unsigned char>& out);
    /// The last partial frame, at the end of a stream.
    void flush(std::vector<unsigned char>& out);

private:
    lame_global_struct* m_lame = nullptr;
    int m_channels = 2;
    std::vector<float> m_left;
    std::vector<float> m_right;
};
}
