/*
 * GlideMix: remember which third-party plug-in crashed the app.
 * See PluginCrashGuard.h.
 */
#include "PluginCrashGuard.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <sstream>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <csignal>
#else
#include <csignal>
#include <fcntl.h>
#include <unistd.h>
#include <climits>
#endif

namespace fs = std::filesystem;

namespace PluginCrashGuard {
namespace {
constexpr const char* kSuffix = ".loading";
constexpr const char* kCrashFileName = "crashed.loading";

// Plug-in the current thread is in (innermost CallScope), or null.
thread_local const char* tCurrent = nullptr;
// The crash handler wrote the record from this thread. If the scope then
// ends normally, the plug-in handled its own fault: take the record back.
thread_local bool tRecorded = false;

std::atomic<bool> sInstalled { false };
fs::path sDir;
fs::path sCrashFile;

std::mutex sMarkedMutex;
std::map<std::string, int> sMarkedCounts;

#ifdef _WIN32
wchar_t sCrashFileW[MAX_PATH * 4] = {};
#else
char sCrashFileC[PATH_MAX] = {};
#endif

fs::path FromUtf8(const std::string& s)
{
    return fs::path(std::u8string(s.begin(), s.end()));
}

std::string ToUtf8(const fs::path& p)
{
    const std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
}

fs::path MarkedFilePath(const std::string& id)
{
    // ids hold characters that are not valid in file names; the name is a
    // hash, the file holds the id (same layout as muse's load guard)
    std::ostringstream name;
    name << "glidemix-" << std::hex << std::hash<std::string> {}(id) << kSuffix;
    return sDir / name.str();
}

void WriteTextFile(const fs::path& path, const std::string& content)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << content;
}

void RemoveFile(const fs::path& path) noexcept
{
    std::error_code ec;
    fs::remove(path, ec);
}

// --- crash handlers: async-signal-safe only below this line ---

#ifdef _WIN32
void WriteCrashRecord() noexcept
{
    const char* id = tCurrent;
    if (!id || !sInstalled.load(std::memory_order_relaxed)) {
        return;
    }
    HANDLE h = CreateFileW(sCrashFileW, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return;
    }
    DWORD written = 0;
    ::WriteFile(h, id, static_cast<DWORD>(std::strlen(id)), &written, nullptr);
    CloseHandle(h);
    tRecorded = true;
}

LONG CALLBACK OnException(EXCEPTION_POINTERS* info)
{
    switch (info->ExceptionRecord->ExceptionCode) {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_PRIV_INSTRUCTION:
    case EXCEPTION_IN_PAGE_ERROR:
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
    case EXCEPTION_DATATYPE_MISALIGNMENT:
        // First chance: the plug-in may still catch it itself. Then its
        // CallScope ends normally and the record is taken back.
        WriteCrashRecord();
        break;
    default:
        // C++ exceptions, debugger events, stack overflow (too little stack
        // left to write a file): not ours to judge.
        break;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

using SignalHandler = void (*)(int);
SignalHandler sPrevAbort = SIG_DFL;

void OnAbort(int sig)
{
    WriteCrashRecord();
    if (sPrevAbort != SIG_DFL && sPrevAbort != SIG_IGN && sPrevAbort != SIG_ERR) {
        sPrevAbort(sig);
    }
}

void InstallHandlers()
{
    AddVectoredExceptionHandler(1, &OnException);
    sPrevAbort = std::signal(SIGABRT, &OnAbort);
}

#else

constexpr int kSignals[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT };
constexpr size_t kSignalCount = sizeof(kSignals) / sizeof(kSignals[0]);
struct sigaction sPrevActions[kSignalCount];

void WriteCrashRecord() noexcept
{
    const char* id = tCurrent;
    if (!id || !sInstalled.load(std::memory_order_relaxed)) {
        return;
    }
    const int fd = ::open(sCrashFileC, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        return;
    }
    const size_t len = std::strlen(id);
    ssize_t ignored = ::write(fd, id, len);
    (void)ignored;
    ::close(fd);
    tRecorded = true;
}

void OnSignal(int sig, siginfo_t* info, void*)
{
    WriteCrashRecord();

    // Put back whatever handled this signal before us (the app's crash
    // logger, or the default), then let it run.
    for (size_t i = 0; i < kSignalCount; ++i) {
        if (kSignals[i] == sig) {
            ::sigaction(sig, &sPrevActions[i], nullptr);
            break;
        }
    }
    // A fault re-executes the faulting instruction when we return and lands
    // in the restored handler. A sent signal (abort(), kill) must be raised
    // again.
    if (info == nullptr || info->si_code <= 0) {
        ::raise(sig);
    }
}

void InstallHandlers()
{
    for (size_t i = 0; i < kSignalCount; ++i) {
        struct sigaction action;
        std::memset(&action, 0, sizeof(action));
        action.sa_sigaction = &OnSignal;
        action.sa_flags = SA_SIGINFO;
        sigemptyset(&action.sa_mask);
        ::sigaction(kSignals[i], &action, &sPrevActions[i]);
    }
}
#endif
}

void Install(const std::string& recordsDirUtf8)
{
    if (sInstalled.load() || recordsDirUtf8.empty()) {
        return;
    }

    sDir = FromUtf8(recordsDirUtf8);
    std::error_code ec;
    fs::create_directories(sDir, ec);
    sCrashFile = sDir / kCrashFileName;

#ifdef _WIN32
    const std::wstring w = sCrashFile.wstring();
    if (w.size() >= sizeof(sCrashFileW) / sizeof(sCrashFileW[0])) {
        return;
    }
    std::copy(w.begin(), w.end(), sCrashFileW);
    sCrashFileW[w.size()] = L'\0';
#else
    const std::string c = ToUtf8(sCrashFile);
    if (c.size() >= sizeof(sCrashFileC)) {
        return;
    }
    std::memcpy(sCrashFileC, c.c_str(), c.size() + 1);
#endif

    InstallHandlers();
    sInstalled = true;
}

bool IsInstalled() noexcept
{
    return sInstalled.load();
}

bool IsThirdPartyPluginId(const char* pluginId) noexcept
{
    if (!pluginId) {
        return false;
    }
    // "Effect_<family>_<vendor>_<name>_<path>" (PluginManager / effectsutils)
    static constexpr const char* kPrefixes[] = {
        "Effect_VST3_", "Effect_VST_", "Effect_AudioUnit_", "Effect_LV2_", "Effect_LADSPA_",
    };
    for (const char* prefix : kPrefixes) {
        if (std::strncmp(pluginId, prefix, std::strlen(prefix)) == 0) {
            return true;
        }
    }
    return false;
}

std::vector<std::string> ReadRecords(const std::string& recordsDirUtf8)
{
    std::vector<std::string> ids;
    std::error_code ec;
    const fs::path dir = FromUtf8(recordsDirUtf8);
    if (!fs::is_directory(dir, ec)) {
        return ids;
    }
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec) || it->path().extension() != kSuffix) {
            continue;
        }
        std::ifstream in(it->path(), std::ios::binary);
        std::string id((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (!id.empty() && std::find(ids.begin(), ids.end(), id) == ids.end()) {
            ids.push_back(std::move(id));
        }
    }
    return ids;
}

CallScope::CallScope(const char* pluginId) noexcept
    : mPrevious(tCurrent)
{
    tCurrent = IsThirdPartyPluginId(pluginId) ? pluginId : mPrevious;
}

CallScope::~CallScope()
{
    tCurrent = mPrevious;
    if (tRecorded) {
        // We got here, so the fault was handled after all.
        tRecorded = false;
        RemoveFile(sCrashFile);
    }
}

MarkedScope::MarkedScope(std::string pluginId)
    : mId(std::move(pluginId))
    , mCall(mId.c_str())
{
    if (!sInstalled.load() || !IsThirdPartyPluginId(mId.c_str())) {
        return;
    }
    std::lock_guard lock(sMarkedMutex);
    if (++sMarkedCounts[mId] == 1) {
        std::error_code ec;
        fs::create_directories(sDir, ec);
        WriteTextFile(MarkedFilePath(mId), mId);
    }
}

MarkedScope::~MarkedScope()
{
    if (!sInstalled.load() || !IsThirdPartyPluginId(mId.c_str())) {
        return;
    }
    std::lock_guard lock(sMarkedMutex);
    auto it = sMarkedCounts.find(mId);
    if (it == sMarkedCounts.end()) {
        return;
    }
    if (--it->second <= 0) {
        sMarkedCounts.erase(it);
        RemoveFile(MarkedFilePath(mId));
    }
}
}
