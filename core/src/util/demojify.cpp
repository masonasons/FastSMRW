#include "fastsm/util/demojify.hpp"

#include <cstdint>
#include <regex>
#include <vector>

namespace fastsm::util {
namespace {

// Decode one UTF-8 code point starting at s[i]; advances i past it. Invalid
// bytes are passed through as a single code point so we never lose data.
std::uint32_t next_code_point(const std::string& s, size_t& i) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    auto cont = [&](size_t k) {
        return i + k < s.size() && (static_cast<unsigned char>(s[i + k]) & 0xC0) == 0x80;
    };
    if (c < 0x80) {
        ++i;
        return c;
    }
    if ((c & 0xE0) == 0xC0 && cont(1)) {
        std::uint32_t cp = (c & 0x1F) << 6 | (static_cast<unsigned char>(s[i + 1]) & 0x3F);
        i += 2;
        return cp;
    }
    if ((c & 0xF0) == 0xE0 && cont(1) && cont(2)) {
        std::uint32_t cp = (c & 0x0F) << 12 | (static_cast<unsigned char>(s[i + 1]) & 0x3F) << 6 |
                           (static_cast<unsigned char>(s[i + 2]) & 0x3F);
        i += 3;
        return cp;
    }
    if ((c & 0xF8) == 0xF0 && cont(1) && cont(2) && cont(3)) {
        std::uint32_t cp = (c & 0x07) << 18 | (static_cast<unsigned char>(s[i + 1]) & 0x3F) << 12 |
                           (static_cast<unsigned char>(s[i + 2]) & 0x3F) << 6 |
                           (static_cast<unsigned char>(s[i + 3]) & 0x3F);
        i += 4;
        return cp;
    }
    ++i; // lone/invalid byte
    return c;
}

void append_code_point(std::string& out, std::uint32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

// Emoji code-point ranges, matching the desktop FastSM's emoji regex. Kept
// deliberately close to that set so both clients strip the same characters.
bool is_emoji_code_point(std::uint32_t c) {
    return (c >= 0x1F300 && c <= 0x1F9FF) || // symbols & pictographs, emoticons, ...
           (c >= 0x1FA00 && c <= 0x1FAFF) || // extended symbols
           (c >= 0x2600 && c <= 0x27BF) ||   // misc symbols, dingbats
           (c >= 0x1F680 && c <= 0x1F6FF) || // transport & map
           (c >= 0x1F1E0 && c <= 0x1F1FF) || // regional indicators (flags)
           (c >= 0x2300 && c <= 0x23FF) ||   // misc technical
           (c >= 0x2B50 && c <= 0x2B55) ||   // stars
           (c >= 0xFE00 && c <= 0xFE0F) ||   // variation selectors
           c == 0x200D ||                    // zero-width joiner
           c == 0x3030 || c == 0x25AA || c == 0x25AB || c == 0x25B6 || c == 0x25C0 ||
           (c >= 0x25FB && c <= 0x25FE);
}

} // namespace

std::string collapse_spaces(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    bool in_space = false;
    for (char c : s) {
        const bool ws = (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v');
        if (ws) {
            in_space = true;
            continue;
        }
        if (in_space && !out.empty())
            out.push_back(' ');
        in_space = false;
        out.push_back(c);
    }
    return out;
}

std::string strip_custom_emoji(const std::string& utf8) {
    if (utf8.find(':') == std::string::npos)
        return utf8;
    static const std::regex re(":[A-Za-z0-9_]+:");
    return collapse_spaces(std::regex_replace(utf8, re, ""));
}

std::string strip_unicode_emoji(const std::string& utf8) {
    std::string out;
    out.reserve(utf8.size());
    bool removed = false;
    for (size_t i = 0; i < utf8.size();) {
        const size_t start = i;
        const std::uint32_t cp = next_code_point(utf8, i);
        if (is_emoji_code_point(cp)) {
            removed = true;
            continue;
        }
        out.append(utf8, start, i - start);
    }
    return removed ? collapse_spaces(out) : utf8;
}

std::string truncate_leading_mentions(const std::string& utf8, int max) {
    if (max <= 0)
        return utf8;
    // A leading run of one or more "@handle " tokens at the very start.
    static const std::regex run_re(R"(^((?:@[A-Za-z0-9_]+(?:@[A-Za-z0-9_.\-]+)?\s+)+))");
    std::smatch m;
    if (!std::regex_search(utf8, m, run_re))
        return utf8;
    const std::string run = m[1].str();
    static const std::regex handle_re(R"(@[A-Za-z0-9_]+(?:@[A-Za-z0-9_.\-]+)?)");
    std::vector<std::string> handles;
    for (auto it = std::sregex_iterator(run.begin(), run.end(), handle_re), end = std::sregex_iterator();
         it != end; ++it)
        handles.push_back(it->str());
    if (static_cast<int>(handles.size()) <= max)
        return utf8;

    std::string kept;
    for (int i = 0; i < max; ++i) {
        if (i)
            kept += " ";
        kept += handles[static_cast<size_t>(i)];
    }
    const int others = static_cast<int>(handles.size()) - max;
    std::string out = kept + " and " + std::to_string(others) + (others == 1 ? " other" : " others");
    const std::string rest = utf8.substr(m[1].length());
    if (!rest.empty())
        out += " " + rest;
    return out;
}

} // namespace fastsm::util
