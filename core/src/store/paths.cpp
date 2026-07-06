#define _CRT_SECURE_NO_WARNINGS
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#endif
#include "fastsm/store/paths.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

#include <cstdlib>
#include <string>
#include <system_error>

// These resolve the desktop app's data home. On non-Windows platforms (Android,
// ...) the front end injects explicit directories via the C ABI config_json, so
// config_dir()/cache_dir() are never called there — the POSIX branch exists only
// so the shared core links, and doubles as a sane default for a future desktop
// Linux/Mac port.

namespace fastsm::store {
namespace {

std::filesystem::path ensure(std::filesystem::path p) {
    std::error_code ec;
    std::filesystem::create_directories(p, ec);
    return p;
}

#ifdef _WIN32

std::filesystem::path env_dir(const wchar_t* var, const wchar_t* fallback) {
    if (const wchar_t* value = _wgetenv(var); value && *value)
        return std::filesystem::path(value);
    return std::filesystem::path(fallback);
}

// The folder that holds the running executable.
std::filesystem::path exe_dir() {
    wchar_t buf[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
        return {};
    return std::filesystem::path(std::wstring(buf, n)).parent_path();
}

// Portable mode (matches the original FastSM): if a "userdata" folder sits next
// to the executable, that folder IS the data home — config.json, cache,
// soundpacks, keymaps, positions all live directly inside it (no FastSMRW
// subfolder). Returns an empty path when not in portable mode.
std::filesystem::path portable_dir() {
    const std::filesystem::path dir = exe_dir();
    if (dir.empty())
        return {};
    std::error_code ec;
    const std::filesystem::path ud = dir / L"userdata";
    if (std::filesystem::is_directory(ud, ec))
        return ud;
    return {};
}

#else // POSIX fallback

std::filesystem::path env_path(const char* var) {
    if (const char* value = std::getenv(var); value && *value)
        return std::filesystem::path(value);
    return {};
}

#endif

} // namespace

std::filesystem::path config_dir() {
#ifdef _WIN32
    if (std::filesystem::path p = portable_dir(); !p.empty())
        return ensure(std::move(p));
    return ensure(env_dir(L"APPDATA", L".") / L"FastSMRW");
#else
    if (std::filesystem::path xdg = env_path("XDG_CONFIG_HOME"); !xdg.empty())
        return ensure(xdg / "FastSMRW");
    if (std::filesystem::path home = env_path("HOME"); !home.empty())
        return ensure(home / ".config" / "FastSMRW");
    return ensure(std::filesystem::path(".") / "FastSMRW");
#endif
}

std::filesystem::path cache_dir() {
    // Kept inside the single FastSM data folder (alongside config), not a
    // separate %LOCALAPPDATA% location.
    return ensure(config_dir() / L"cache");
}

} // namespace fastsm::store
