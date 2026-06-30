#include "fastsm/store/paths.hpp"

#include <cstdlib>
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

} // namespace

std::filesystem::path config_dir() {
    return ensure(env_dir(L"APPDATA", L".") / L"FastSMRW");
}

std::filesystem::path cache_dir() {
    return ensure(env_dir(L"LOCALAPPDATA", L".") / L"FastSMRW" / L"timelines");
}

} // namespace fastsm::store
