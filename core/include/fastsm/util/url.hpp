#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fastsm::util {

// Percent-encode per RFC 3986 (unreserved A-Za-z0-9-_.~ kept verbatim).
std::string percent_encode(std::string_view s);

// Build an application/x-www-form-urlencoded body / query string from pairs,
// preserving order and allowing duplicate keys (e.g. poll[options][]).
std::string form_encode(const std::vector<std::pair<std::string, std::string>>& params);

} // namespace fastsm::util
