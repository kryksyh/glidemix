/*
 * GlideMix: remember which third-party plug-in crashed the app.
 *
 * Plug-in code runs in our process. When it crashes, the app goes with it,
 * and on the next start we want to know which plug-in it was so it can be
 * turned off instead of crashing again.
 *
 * Two records, both written into the folder muse's IAudioPluginsLoadGuard
 * already reads at start-up (`<app data>/audio_plugins_loading`, one
 * `*.loading` file per plug-in, the file holds the plug-in id):
 *
 * - MarkedScope: a file that exists while a rare, risky call runs (load,
 *   instantiate, open the window). If the app dies inside, the file stays.
 *   Works even when no crash handler runs.
 * - CallScope: no I/O, just a thread-local pointer. Cheap enough for the
 *   audio thread and timers. If the app gets a fatal signal (macOS/Linux) or
 *   an access violation (Windows) while a CallScope is open on the crashing
 *   thread, the handler writes `crashed.loading` with that plug-in's id.
 *
 * Only third-party plug-ins are recorded (VST3, VST, Audio Unit, LV2,
 * LADSPA ids). A scope for a built-in effect does nothing: a bug in our own
 * code must not switch off one of our effects.
 *
 * Install() is called once at start-up, in the main app only (not in the
 * plug-in validation sub-process, which is allowed to crash).
 */
#pragma once

#include <string>
#include <vector>

namespace PluginCrashGuard {
//! UTF-8 path of the folder for the `*.loading` records; created if missing.
//! Installs the fatal-signal / exception handlers. Call once, main thread.
UTILITY_API void Install(const std::string& recordsDirUtf8);

//! True once Install() ran.
UTILITY_API bool IsInstalled() noexcept;

//! A VST3, VST, Audio Unit, LV2 or LADSPA effect id ("Effect_VST3_...").
UTILITY_API bool IsThirdPartyPluginId(const char* pluginId) noexcept;

//! The plug-in ids recorded by a previous run that did not end normally.
//! Read-only; muse's load guard clears the folder after marking them broken.
UTILITY_API std::vector<std::string> ReadRecords(const std::string& recordsDirUtf8);

//! Cheap scope: tells the crash handler which plug-in this thread is in.
//! `pluginId` must stay valid (and unchanged) for the life of the scope.
//! Nests; the innermost scope wins. Safe on the audio thread.
class UTILITY_API CallScope final
{
public:
    explicit CallScope(const char* pluginId) noexcept;
    ~CallScope();

    CallScope(const CallScope&) = delete;
    CallScope& operator=(const CallScope&) = delete;

private:
    const char* mPrevious;
};

//! CallScope plus a record file on disk for the duration of the scope.
//! For rare calls only (it writes and deletes a small file): loading,
//! instantiating, opening or closing a plug-in window. Not the audio thread.
class UTILITY_API MarkedScope final
{
public:
    explicit MarkedScope(std::string pluginId);
    ~MarkedScope();

    MarkedScope(const MarkedScope&) = delete;
    MarkedScope& operator=(const MarkedScope&) = delete;

private:
    std::string mId;
    CallScope mCall;
};
}
