#include "fastsm/util/quote_text.hpp"

#include <regex>

namespace fastsm::util {
namespace {

std::string trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos)
        return {};
    const size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

} // namespace

std::string strip_quote_url(const std::string& text, const std::string& quoted_url) {
    std::string result = text;

    // Leading "RE:/QT: <url>" reference.
    static const std::regex lead(R"(^(RE|QT):\s*https?://\S+\s*)", std::regex::icase);
    result = std::regex_replace(result, lead, "");

    // The exact quoted URL appended at the end.
    if (!quoted_url.empty()) {
        const std::string trimmed = trim(result);
        if (trimmed.size() >= quoted_url.size() &&
            trimmed.compare(trimmed.size() - quoted_url.size(), quoted_url.size(), quoted_url) == 0)
            result = trimmed.substr(0, trimmed.size() - quoted_url.size());
    }

    // Any trailing Mastodon-style status URL (https://instance/@user/123).
    static const std::regex trail(R"(\s*https?://[^\s]+/@[^\s]+/\d+\s*$)");
    result = std::regex_replace(result, trail, "");

    return trim(result);
}

} // namespace fastsm::util
