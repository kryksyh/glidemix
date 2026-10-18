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
