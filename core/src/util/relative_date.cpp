#include "fastsm/util/relative_date.hpp"

namespace fastsm::util {
namespace {

constexpr std::int64_t kMinute = 60;
constexpr std::int64_t kHour = 3600;
constexpr std::int64_t kDay = 86400;
constexpr std::int64_t kWeek = 7 * kDay;
constexpr std::int64_t kMonth = 30 * kDay;
constexpr std::int64_t kYear = 365 * kDay;

} // namespace

std::string relative_compact(std::int64_t when, std::int64_t now) {
    std::int64_t d = now - when;
    if (d < 0)
        d = 0;
    if (d < kMinute)
        return "now";
    if (d < kHour)
        return std::to_string(d / kMinute) + "m";
    if (d < kDay)
        return std::to_string(d / kHour) + "h";
    if (d < kWeek)
        return std::to_string(d / kDay) + "d";
    if (d < kYear)
        return std::to_string(d / kWeek) + "w";
    return std::to_string(d / kYear) + "y";
}

std::string relative_spoken(std::int64_t when, std::int64_t now) {
    std::int64_t d = now - when;
    if (d < 0)
        d = 0;
    auto phrase = [](std::int64_t value, const char* unit) {
        std::string r = std::to_string(value) + " " + unit;
        if (value != 1)
            r += "s";
        return r + " ago";
    };
    if (d < 5)
        return "just now";
    if (d < kMinute)
        return phrase(d, "second");
    if (d < kHour)
        return phrase(d / kMinute, "minute");
    if (d < kDay)
        return phrase(d / kHour, "hour");
    if (d < kWeek)
        return phrase(d / kDay, "day");
    if (d < kMonth)
        return phrase(d / kWeek, "week");
    if (d < kYear)
        return phrase(d / kMonth, "month");
    return phrase(d / kYear, "year");
}

} // namespace fastsm::util
