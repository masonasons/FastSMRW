#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace fastsm::util {

// Parse an ISO-8601 / RFC-3339 timestamp (as emitted by Mastodon and the AT
// Protocol) to Unix seconds. Handles optional fractional seconds and a 'Z' or
// +/-HH:MM(:?) offset. Returns nullopt if the value is not a valid timestamp.
std::optional<std::int64_t> parse_iso8601(std::string_view value);

// Format Unix seconds as an ISO-8601 UTC timestamp "YYYY-MM-DDTHH:MM:SSZ".
std::string format_iso8601(std::int64_t unix_seconds);

// Current time in Unix seconds.
std::int64_t now_unix();

} // namespace fastsm::util
