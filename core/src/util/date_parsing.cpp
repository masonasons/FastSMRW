#include "fastsm/util/date_parsing.hpp"

#include <chrono>
#include <cstdio>

namespace fastsm::util {
namespace {

// Days since 1970-01-01 for a proleptic Gregorian date (Howard Hinnant's
// algorithm). Valid for any reasonable year.
std::int64_t days_from_civil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153u * (m > 2 ? m - 3u : m + 9u) + 2u) / 5u + d - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

// Inverse of days_from_civil: (year, month, day) from days since the epoch.
void civil_from_days(std::int64_t z, int& y, unsigned& m, unsigned& d) {
    z += 719468;
    const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int yy = static_cast<int>(yoe) + static_cast<int>(era) * 400;
    const unsigned doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
    const unsigned mp = (5u * doy + 2u) / 153u;
    d = doy - (153u * mp + 2u) / 5u + 1u;
    m = mp < 10u ? mp + 3u : mp - 9u;
    y = yy + (m <= 2 ? 1 : 0);
}

} // namespace

std::optional<std::int64_t> parse_iso8601(std::string_view s) {
    if (s.size() < 19)
        return std::nullopt;

    auto digits = [&](size_t pos, int len) -> int {
        int v = 0;
        for (int k = 0; k < len; ++k) {
            const char c = s[pos + static_cast<size_t>(k)];
            if (c < '0' || c > '9')
                return -1;
            v = v * 10 + (c - '0');
        }
        return v;
    };

    if (s[4] != '-' || s[7] != '-' || (s[10] != 'T' && s[10] != 't' && s[10] != ' ') ||
        s[13] != ':' || s[16] != ':')
        return std::nullopt;

    const int year = digits(0, 4);
    const int month = digits(5, 2);
    const int day = digits(8, 2);
    const int hour = digits(11, 2);
    const int minute = digits(14, 2);
    const int second = digits(17, 2);
    if (year < 0 || month < 1 || month > 12 || day < 1 || day > 31 || hour < 0 || hour > 23 ||
        minute < 0 || minute > 59 || second < 0 || second > 60)
        return std::nullopt;

    size_t pos = 19;
    if (pos < s.size() && s[pos] == '.') {
        ++pos;
        while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9')
            ++pos;
    }

    int offset_seconds = 0;
    if (pos < s.size()) {
        const char z = s[pos];
        if (z == 'Z' || z == 'z') {
            offset_seconds = 0;
        } else if ((z == '+' || z == '-') && pos + 3 <= s.size()) {
            const int sign = (z == '+') ? 1 : -1;
            const int oh = digits(pos + 1, 2);
            int om = 0;
            if (pos + 6 <= s.size() && s[pos + 3] == ':')
                om = digits(pos + 4, 2);
            else if (pos + 5 <= s.size())
                om = digits(pos + 3, 2);
            if (oh >= 0 && om >= 0)
                offset_seconds = sign * (oh * 3600 + om * 60);
        }
    }

    const std::int64_t days =
        days_from_civil(year, static_cast<unsigned>(month), static_cast<unsigned>(day));
    return days * 86400 + hour * 3600 + minute * 60 + second - offset_seconds;
}

std::string format_iso8601(std::int64_t unix_seconds) {
    std::int64_t days = unix_seconds / 86400;
    std::int64_t rem = unix_seconds % 86400;
    if (rem < 0) {
        rem += 86400;
        --days;
    }
    int y = 0;
    unsigned m = 0, d = 0;
    civil_from_days(days, y, m, d);
    const int hour = static_cast<int>(rem / 3600);
    const int minute = static_cast<int>((rem % 3600) / 60);
    const int second = static_cast<int>(rem % 60);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02u-%02uT%02d:%02d:%02dZ", y, m, d, hour, minute,
                  second);
    return buf;
}

std::int64_t now_unix() {
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

} // namespace fastsm::util
