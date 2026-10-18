/*
 * GlideMix
 */
#include "mp3encoder.h"

#include <lame/lame.h>

#include "log.h"

using namespace au::broadcast;

Mp3Encoder::~Mp3Encoder()
{
    close();
}

bool Mp3Encoder::open(double sampleRate, int channels, int bitrateKbps)
{
    close();
    if (channels < 1 || channels > 2 || sampleRate <= 0.0) {
        return false;
    }

    m_lame = lame_init();
    if (!m_lame) {
        return false;
    }
    m_channels = channels;
    lame_set_in_samplerate(m_lame, static_cast<int>(sampleRate));
    lame_set_out_samplerate(m_lame, static_cast<int>(sampleRate));
    lame_set_num_channels(m_lame, channels);
    lame_set_mode(m_lame, channels == 1 ? MONO : JOINT_STEREO);
    lame_set_brate(m_lame, bitrateKbps);
    lame_set_VBR(m_lame, vbr_off);
    // A listener joining mid-stream has no header to read, so every frame has
    // to stand on its own: no bit reservoir games, no Xing header.
    lame_set_bWriteVbrTag(m_lame, 0);
    lame_set_quality(m_lame, 3);
    if (lame_init_params(m_lame) < 0) {
        LOGE() << "LAME would not take " << sampleRate << " Hz, " << channels << " ch, " << bitrateKbps << " kbps";
        close();
        return false;
    }
    return true;
}

void Mp3Encoder::close()
{
    if (m_lame) {
        lame_close(m_lame);
        m_lame = nullptr;
    }
    m_left.clear();
    m_right.clear();
}

bool Mp3Encoder::encode(const float* interleaved, size_t frames, std::vector<unsigned char>& out)
{
    if (!m_lame || frames == 0) {
        return m_lame != nullptr;
    }

    // LAME's float entry point wants the samples full scale, one array per
    // channel; the engine hands them over interleaved and normalised to 1.
    m_left.resize(frames);
    m_right.resize(frames);
    for (size_t i = 0; i < frames; ++i) {
        m_left[i] = interleaved[i * m_channels] * 32768.0f;
        m_right[i] = m_channels == 2 ? interleaved[i * m_channels + 1] * 32768.0f : m_left[i];
    }

    // LAME's own worst case for the output buffer.
    const size_t capacity = static_cast<size_t>(1.25 * frames) + 7200;
    const size_t at = out.size();
    out.resize(at + capacity);
    const int written = lame_encode_buffer_float(m_lame, m_left.data(), m_right.data(),
                                                 static_cast<int>(frames), out.data() + at,
                                                 static_cast<int>(capacity));
    if (written < 0) {
        out.resize(at);
        LOGE() << "LAME encode error " << written;
        return false;
    }
    out.resize(at + static_cast<size_t>(written));
    return true;
}

void Mp3Encoder::flush(std::vector<unsigned char>& out)
{
    if (!m_lame) {
        return;
    }
    const size_t at = out.size();
    out.resize(at + 7200);
    const int written = lame_encode_flush(m_lame, out.data() + at, 7200);
    out.resize(at + (written > 0 ? static_cast<size_t>(written) : 0));
}
