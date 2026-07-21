#include "fastsm/platform/bluesky/bluesky_richtext.hpp"

#include <cctype>

namespace fastsm::bluesky {
namespace {

bool is_ws(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

// A facet may start only at the beginning, after whitespace, or after an opening
// bracket/quote — so "email@x.com" or "a#b" mid-word don't falsely match. A
// preceding non-ASCII (multibyte) byte is >= 0x80 and is treated as "not a
// boundary", which is correct: we don't want to start a token inside a word.
bool boundary_before(const std::string& t, std::size_t i) {
    if (i == 0)
        return true;
    unsigned char p = static_cast<unsigned char>(t[i - 1]);
    return is_ws(p) || p == '(' || p == '[' || p == '{' || p == '<' || p == '"' || p == '\'';
}

bool starts_with_ci(const std::string& t, std::size_t i, const char* lit) {
    for (std::size_t k = 0; lit[k]; ++k) {
        if (i + k >= t.size())
            return false;
        if (std::tolower(static_cast<unsigned char>(t[i + k])) != lit[k])
            return false;
    }
    return true;
}

// Trim characters that commonly trail a URL/tag but aren't part of it. A closing
// ')' is only trimmed when the URL has no matching '(' (Wikipedia-style links).
std::size_t trim_url_end(const std::string& t, std::size_t start, std::size_t end) {
    while (end > start) {
        char c = t[end - 1];
        if (c == '.' || c == ',' || c == ';' || c == ':' || c == '!' || c == '?' || c == '"' ||
            c == '\'' || c == '>' || c == ']' || c == '}') {
            --end;
            continue;
        }
        if (c == ')') {
            bool has_open = false;
            for (std::size_t k = start; k < end - 1; ++k)
                if (t[k] == '(')
                    has_open = true;
            if (!has_open) {
                --end;
                continue;
            }
        }
        break;
    }
    return end;
}

bool is_handle_char(unsigned char c) {
    return std::isalnum(c) || c == '.' || c == '-';
}

} // namespace

std::vector<FacetSpan> detect_facets(const std::string& t) {
    std::vector<FacetSpan> out;
    const std::size_t n = t.size();
    std::size_t i = 0;
    while (i < n) {
        const unsigned char c = static_cast<unsigned char>(t[i]);

        // Links: http(s):// URLs, plus bare "www." (scheme prepended).
        if ((c == 'h' || c == 'H' || c == 'w' || c == 'W') && boundary_before(t, i)) {
            std::string prefix;
            bool is_url = false;
            if (starts_with_ci(t, i, "https://") || starts_with_ci(t, i, "http://")) {
                is_url = true;
            } else if (starts_with_ci(t, i, "www.")) {
                is_url = true;
                prefix = "https://";
            }
            if (is_url) {
                std::size_t j = i;
                while (j < n && !is_ws(static_cast<unsigned char>(t[j])))
                    ++j;
                const std::size_t end = trim_url_end(t, i, j);
                // A bare "www." with nothing after the dot isn't a link.
                if (end - i > (prefix.empty() ? 8u : 4u)) {
                    out.push_back({i, end, FacetSpan::Type::Link, prefix + t.substr(i, end - i)});
                    i = end;
                    continue;
                }
            }
        }

        // Mentions: @handle, where the handle is a domain (contains a dot).
        if (c == '@' && boundary_before(t, i)) {
            std::size_t j = i + 1;
            while (j < n && is_handle_char(static_cast<unsigned char>(t[j])))
                ++j;
            std::size_t end = j;
            while (end > i + 1 && (t[end - 1] == '.' || t[end - 1] == '-'))
                --end; // trailing dots/hyphens aren't part of the handle
            const std::string handle = t.substr(i + 1, end - (i + 1));
            const bool has_dot = handle.find('.') != std::string::npos;
            if (handle.size() >= 3 && has_dot) {
                out.push_back({i, end, FacetSpan::Type::Mention, handle});
                i = end;
                continue;
            }
        }

        // Hashtags: #tag (not starting with a digit, not purely numeric).
        if (c == '#' && boundary_before(t, i)) {
            std::size_t j = i + 1;
            while (j < n && !is_ws(static_cast<unsigned char>(t[j])) && t[j] != '#')
                ++j;
            const std::size_t end = trim_url_end(t, i + 1, j);
            const std::string tag = t.substr(i + 1, end - (i + 1));
            const bool first_digit = !tag.empty() && std::isdigit(static_cast<unsigned char>(tag[0]));
            if (!tag.empty() && tag.size() <= 64 && !first_digit) {
                out.push_back({i, end, FacetSpan::Type::Tag, tag});
                i = end;
                continue;
            }
        }

        ++i;
    }
    return out;
}

} // namespace fastsm::bluesky
