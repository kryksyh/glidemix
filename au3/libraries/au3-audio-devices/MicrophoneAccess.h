/*
 * GlideMix
 *
 * Whether the app may use the microphone. On macOS, opening an input device
 * before the user has been asked brings up the permission prompt; the device
 * scans at startup check this first, so the prompt comes with the first
 * recording instead of every launch.
 */
#pragma once

//! False on macOS until the user has allowed the microphone; always true elsewhere.
AUDIO_DEVICES_API bool MicrophoneAccessGranted();

#ifdef __APPLE__
//! Whether the named output device runs at `rate`, from what it reports, without
//! opening it: opening any device can bring up the same prompt. False if it isn't found.
AUDIO_DEVICES_API bool OutputDeviceRateKnown(const char* deviceName, double rate, bool* supported);
#endif
