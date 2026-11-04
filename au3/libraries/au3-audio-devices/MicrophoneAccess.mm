/*
 * GlideMix
 */
#include "MicrophoneAccess.h"

#import <AVFoundation/AVFoundation.h>
#import <CoreAudio/CoreAudio.h>

#include <string>
#include <vector>

bool MicrophoneAccessGranted()
{
    return [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio] == AVAuthorizationStatusAuthorized;
}

bool OutputDeviceRateKnown(const char* deviceName, double rate, bool* supported)
{
    AudioObjectPropertyAddress address { kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal,
                                         kAudioObjectPropertyElementMain };
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &address, 0, nullptr, &size) != noErr) {
        return false;
    }
    std::vector<AudioDeviceID> devices(size / sizeof(AudioDeviceID));
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, nullptr, &size, devices.data()) != noErr) {
        return false;
    }
    for (const AudioDeviceID device : devices) {
        address = { kAudioObjectPropertyName, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
        CFStringRef name = nullptr;
        size = sizeof(name);
        if (AudioObjectGetPropertyData(device, &address, 0, nullptr, &size, &name) != noErr || !name) {
            continue;
        }
        char utf8[512] = {};
        const bool same = CFStringGetCString(name, utf8, sizeof(utf8), kCFStringEncodingUTF8)
                          && std::string(utf8) == deviceName;
        CFRelease(name);
        if (!same) {
            continue;
        }
        address = { kAudioDevicePropertyAvailableNominalSampleRates, kAudioObjectPropertyScopeOutput,
                    kAudioObjectPropertyElementMain };
        if (AudioObjectGetPropertyDataSize(device, &address, 0, nullptr, &size) != noErr || size == 0) {
            return false;
        }
        std::vector<AudioValueRange> ranges(size / sizeof(AudioValueRange));
        if (AudioObjectGetPropertyData(device, &address, 0, nullptr, &size, ranges.data()) != noErr) {
            return false;
        }
        *supported = false;
        for (const AudioValueRange& range : ranges) {
            *supported = *supported || (rate >= range.mMinimum && rate <= range.mMaximum);
        }
        return true;
    }
    return false;
}
