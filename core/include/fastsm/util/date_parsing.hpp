#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace fastsm::util {

// Parse an ISO-8601 / RFC-3339 timestamp (as emitted by Mastodon and the AT
// Protocol) to Unix seconds. Handles optional fractional seconds and a 'Z' or
// +/-HH:MM(:?) offset. Returns nullopt if the value is not a valid timestamp.
std::optional<std::int64_t> parse_iso8601(std::string_view value);

} // namespace fastsm::util
