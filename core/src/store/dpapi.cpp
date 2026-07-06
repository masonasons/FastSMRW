#include "fastsm/store/dpapi.hpp"

// Credential-blob encryption for the on-disk config. On Windows this is DPAPI
// (per-user, machine-bound). On other platforms (Android, ...) DPAPI does not
// exist; the front end is responsible for at-rest protection of the config
// directory (Android app-private storage, optionally a Keystore-backed cipher
// supplied through the JNI layer later), so here we pass the bytes through
// unchanged. Keeping the same function surface means app_config.cpp is portable.

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wincrypt.h>

namespace fastsm::store {

std::string dpapi_protect(std::string_view plaintext) {
    DATA_BLOB in{static_cast<DWORD>(plaintext.size()),
                 reinterpret_cast<BYTE*>(const_cast<char*>(plaintext.data()))};
    DATA_BLOB out{};
    if (!CryptProtectData(&in, L"FastSMRW", nullptr, nullptr, nullptr, 0, &out))
        return {};
    std::string result(reinterpret_cast<char*>(out.pbData), out.cbData);
    LocalFree(out.pbData);
    return result;
}

std::optional<std::string> dpapi_unprotect(std::string_view ciphertext) {
    if (ciphertext.empty())
        return std::nullopt;
    DATA_BLOB in{static_cast<DWORD>(ciphertext.size()),
                 reinterpret_cast<BYTE*>(const_cast<char*>(ciphertext.data()))};
    DATA_BLOB out{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, 0, &out))
        return std::nullopt;
    std::string result(reinterpret_cast<char*>(out.pbData), out.cbData);
    LocalFree(out.pbData);
    return result;
}

} // namespace fastsm::store

#else // non-Windows: passthrough (front end owns at-rest protection)

namespace fastsm::store {

std::string dpapi_protect(std::string_view plaintext) {
    return std::string(plaintext);
}

std::optional<std::string> dpapi_unprotect(std::string_view ciphertext) {
    if (ciphertext.empty())
        return std::nullopt;
    return std::string(ciphertext);
}

} // namespace fastsm::store

#endif
