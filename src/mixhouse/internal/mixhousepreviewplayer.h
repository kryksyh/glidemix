/*
 * MixHouse DJ 2
 */
#pragma once

#include <memory>
#include <string>

namespace au::mixhouse {
/// The library's click-to-preview audio, standing in for real mixxx's
/// [PreviewDeck1] (mixer/previewdeck.h). The interaction on top of it is a
/// port of the MixHouse DJ fork's OverviewDelegate::editorEvent (click =
/// play from there, left-edge stop zone, coalesced seeks); the audio path
/// can't be, because this app has no mixxx engine to host a preview deck.
///
/// macOS: AVAudioEngine + AVAudioPlayerNode reading the file directly
/// (Core Audio decodes and resamples), on its own output stream -- fully
/// independent of Audacity's project playback, the same way mixxx's
/// preview deck is independent of its main decks. Windows: the Media
/// Engine. Linux: decoded into memory, played on its own PortAudio stream.
/// Other platforms: a stub whose load() fails, so clicking does nothing.
///
/// Main thread only.
class MixHousePreviewPlayer
{
public:
    MixHousePreviewPlayer();
    ~MixHousePreviewPlayer();

    /// Opens `utf8Path` and routes output to the device named
    /// `outputDeviceName` (Audacity's own AudioIO/PlaybackDevice setting;
    /// empty or not found = system default output). Stops anything playing.
    bool load(const std::string& utf8Path, const std::string& outputDeviceName);
    /// Starts (or restarts) playback at `fraction` (0..1) of the loaded file.
    void playFrom(double fraction);
    void stop();
    /// False after stop() and also once playback reaches the end of the file.
    bool isPlaying();
    /// Current position as a fraction (0..1) of the loaded file.
    double position();
    /// GlideMix: playback speed, keeping the pitch (1 = normal). Used to play a
    /// library song at the timeline's tempo. No effect where unsupported.
    void setRate(double rate);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
}
