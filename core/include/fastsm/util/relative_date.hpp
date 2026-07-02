#pragma once

#include <cstdint>
#include <string>

namespace fastsm::util {

// Compact relative label for visual display: "now", "5m", "2h", "3d", "2w",
// "1y". `when` and `now` are Unix seconds.
std::string relative_compact(std::int64_t when, std::int64_t now);

// Spoken relative label for screen readers: "5 minutes ago", "1 hour ago".
std::string relative_spoken(std::int64_t when, std::int64_t now);

// Absolute clock time in local time: "3:45 PM", with ", Month D, YYYY" appended
// at the end only when `when` falls on a different local calendar day than `now`.
std::string absolute_time(std::int64_t when, std::int64_t now);

} // namespace fastsm::util
