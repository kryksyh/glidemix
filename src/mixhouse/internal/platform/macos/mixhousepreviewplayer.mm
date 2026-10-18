/*
 * MixHouse DJ 2
 */
#include "../../mixhousepreviewplayer.h"

#import <AVFoundation/AVFoundation.h>
#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>

#include <algorithm>
#include <vector>

namespace {
// kAudioObjectPropertyElementMain; spelled as its value so this builds
// against the 11.0 deployment target without the Master/Main deprecation dance.
constexpr AudioObjectPropertyElement kElementMain = 0;

AudioDeviceID defaultOutputDevice()
{
    AudioObjectPropertyAddress addr{ kAudioHardwarePropertyDefaultOutputDevice,
                                     kAudioObjectPropertyScopeGlobal, kElementMain };
    AudioDeviceID device = kAudioObjectUnknown;
    UInt32 size = sizeof(device);
    AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr, 0, nullptr, &size, &device);
    return device;
}

bool hasOutputStreams(AudioDeviceID device)
{
    AudioObjectPropertyAddress addr{ kAudioDevicePropertyStreams, kAudioObjectPropertyScopeOutput, kElementMain };
    UInt32 size = 0;
    return AudioObjectGetPropertyDataSize(device, &addr, 0, nullptr, &size) == noErr && size > 0;
}

/// Audacity stores its playback device as PortAudio's device name, which on
/// macOS is Core Audio's own device name -- so match on that.
AudioDeviceID findOutputDevice(const std::string& name)
{
    if (name.empty()) {
        return defaultOutputDevice();
    }
    AudioObjectPropertyAddress addr{ kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal, kElementMain };
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &addr, 0, nullptr, &size) != noErr) {
        return defaultOutputDevice();
    }
    std::vector<AudioDeviceID> devices(size / sizeof(AudioDeviceID));
    if (devices.empty()
        || AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr, 0, nullptr, &size, devices.data()) != noErr) {
        return defaultOutputDevice();
    }
    NSString* wanted = [NSString stringWithUTF8String:name.c_str()];
    for (AudioDeviceID device : devices) {
        if (!hasOutputStreams(device)) {
            continue;
        }
        AudioObjectPropertyAddress nameAddr{ kAudioObjectPropertyName, kAudioObjectPropertyScopeGlobal, kElementMain };
        CFStringRef deviceName = nullptr;
        UInt32 nameSize = sizeof(deviceName);
        if (AudioObjectGetPropertyData(device, &nameAddr, 0, nullptr, &nameSize, &deviceName) != noErr || !deviceName) {
            continue;
        }
        const bool match = [(__bridge NSString*)deviceName isEqualToString:wanted];
        CFRelease(deviceName);
        if (match) {
            return device;
        }
    }
    return defaultOutputDevice();
}
}

namespace au::mixhouse {
struct MixHousePreviewPlayer::Impl {
    AVAudioEngine* engine = nil;
    AVAudioPlayerNode* player = nil;
    AVAudioFile* file = nil;
    AVAudioFormat* connectedFormat = nil;
    AudioDeviceID device = kAudioObjectUnknown;
    AVAudioFramePosition startFrame = 0;
    double lastPosition = 0.0;
    bool playing = false;
};

MixHousePreviewPlayer::MixHousePreviewPlayer()
    : m_impl(std::make_unique<Impl>())
{
}

MixHousePreviewPlayer::~MixHousePreviewPlayer()
{
    stop();
}

bool MixHousePreviewPlayer::load(const std::string& utf8Path, const std::string& outputDeviceName)
{
    Impl& d = *m_impl;
    stop();

    NSError* error = nil;
    NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:utf8Path.c_str()]];
    AVAudioFile* file = [[AVAudioFile alloc] initForReading:url error:&error];
    if (!file || file.length <= 0) {
        NSLog(@"GlideMix preview: cannot open %@: %@", url.path, error);
        return false;
    }

    if (!d.engine) {
        d.engine = [[AVAudioEngine alloc] init];
        d.player = [[AVAudioPlayerNode alloc] init];
        [d.engine attachNode:d.player];
    }

    // The output unit's device can only change while the engine is stopped
    // (stop() above guarantees that).
    const AudioDeviceID device = findOutputDevice(outputDeviceName);
    if (device != kAudioObjectUnknown && device != d.device) {
        AudioUnit outputUnit = d.engine.outputNode.audioUnit;
        AudioDeviceID value = device;
        if (outputUnit && AudioUnitSetProperty(outputUnit, kAudioOutputUnitProperty_CurrentDevice,
                                               kAudioUnitScope_Global, 0, &value, sizeof(value)) == noErr) {
            d.device = device;
        }
    }

    // Player -> main mixer in the file's own format; the mixer converts to the
    // output device's rate, so 44.1k files play correctly on a 48k interface.
    AVAudioFormat* format = file.processingFormat;
    if (!d.connectedFormat || ![d.connectedFormat isEqual:format]) {
        [d.engine disconnectNodeOutput:d.player];
        [d.engine connect:d.player to:d.engine.mainMixerNode format:format];
        d.connectedFormat = format;
    }

    d.file = file;
    d.startFrame = 0;
    d.lastPosition = 0.0;
    return true;
}

void MixHousePreviewPlayer::playFrom(double fraction)
{
    Impl& d = *m_impl;
    if (!d.file) {
        return;
    }
    const AVAudioFramePosition length = d.file.length;
    const AVAudioFramePosition frame = std::clamp<AVAudioFramePosition>(
        static_cast<AVAudioFramePosition>(std::clamp(fraction, 0.0, 1.0) * static_cast<double>(length)),
        0, length - 1);

    [d.player stop];
    [d.player scheduleSegment:d.file
                startingFrame:frame
                   frameCount:static_cast<AVAudioFrameCount>(length - frame)
                       atTime:nil
            completionHandler:nil];
    if (!d.engine.isRunning) {
        NSError* error = nil;
        [d.engine prepare];
        if (![d.engine startAndReturnError:&error]) {
            NSLog(@"GlideMix preview: engine start failed: %@", error);
            d.playing = false;
            return;
        }
    }
    [d.player play];
    d.startFrame = frame;
    d.lastPosition = static_cast<double>(frame) / static_cast<double>(length);
    d.playing = true;
}

void MixHousePreviewPlayer::stop()
{
    Impl& d = *m_impl;
    if (d.playing) {
        d.lastPosition = position();
    }
    d.playing = false;
    if (d.player) {
        [d.player stop];
    }
    // Stopping the engine (not just the node) releases the output device
    // while nothing is previewing.
    if (d.engine && d.engine.isRunning) {
        [d.engine stop];
    }
}

bool MixHousePreviewPlayer::isPlaying()
{
    if (m_impl->playing) {
        position();   // detects the end of the file
    }
    return m_impl->playing;
}

double MixHousePreviewPlayer::position()
{
    Impl& d = *m_impl;
    if (!d.file || !d.playing) {
        return d.lastPosition;
    }
    const double length = static_cast<double>(d.file.length);
    AVAudioTime* nodeTime = d.player.lastRenderTime;
    AVAudioTime* playerTime = nodeTime ? [d.player playerTimeForNodeTime:nodeTime] : nil;
    if (!playerTime || !playerTime.sampleTimeValid) {
        return d.lastPosition;
    }
    // playerTime counts from this play() call, in the connection's (the
    // file's) sample rate -- the same frame units as startFrame.
    const double frames = static_cast<double>(d.startFrame) + static_cast<double>(std::max<AVAudioFramePosition>(0, playerTime.sampleTime));
    if (frames >= length) {
        d.lastPosition = 1.0;
        d.playing = false;
        [d.player stop];
        [d.engine stop];
        return 1.0;
    }
    d.lastPosition = frames / length;
    return d.lastPosition;
}
}
