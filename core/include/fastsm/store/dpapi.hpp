#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace fastsm::store {

// Per-user encryption via the Windows Data Protection API. Used to protect
// stored credentials at rest. Returns raw (binary) bytes.
std::string dpapi_protect(std::string_view plaintext);
std::optional<std::string> dpapi_unprotect(std::string_view ciphertext);

} // namespace fastsm::store
