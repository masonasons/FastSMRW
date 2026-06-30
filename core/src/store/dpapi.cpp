#include "fastsm/store/dpapi.hpp"

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
