#define _CRT_SECURE_NO_WARNINGS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "fastsm/store/paths.hpp"

#include <windows.h>

#include <cstdlib>
#include <string>
#include <system_error>

namespace fastsm::store {
namespace {

std::filesystem::path env_dir(const wchar_t* var, const wchar_t* fallback) {
    if (const wchar_t* value = _wgetenv(var); value && *value)
        return std::filesystem::path(value);
    return std::filesystem::path(fallback);
}

std::filesystem::path ensure(std::filesystem::path p) {
    std::error_code ec;
    std::filesystem::create_directories(p, ec);
    return p;
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

} // namespace

std::filesystem::path config_dir() {
    if (std::filesystem::path p = portable_dir(); !p.empty())
        return ensure(std::move(p));
    return ensure(env_dir(L"APPDATA", L".") / L"FastSMRW");
}

std::filesystem::path cache_dir() {
    // Kept inside the single FastSM data folder (alongside config), not a
    // separate %LOCALAPPDATA% location.
    return ensure(config_dir() / L"cache");
}

} // namespace fastsm::store
