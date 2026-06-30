#include "fastsm/util/html_stripper.hpp"

#include <array>
#include <cctype>

namespace fastsm::util {
namespace {

void append_utf8(std::string& out, unsigned cp) {
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
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

// Returns the code point for a named entity, or 0 if unknown.
unsigned named_entity(std::string_view name) {
    struct Entry {
        std::string_view name;
        unsigned cp;
    };
    static constexpr std::array<Entry, 14> kEntities{{
        {"amp", '&'},     {"lt", '<'},      {"gt", '>'},      {"quot", '"'},
        {"apos", '\''},   {"nbsp", 0x20},   {"hellip", 0x2026}, {"mdash", 0x2014},
        {"ndash", 0x2013}, {"copy", 0x00A9}, {"reg", 0x00AE},  {"trade", 0x2122},
        {"deg", 0x00B0},  {"middot", 0x00B7},
    }};
    for (const auto& e : kEntities) {
        if (e.name == name)
            return e.cp;
    }
    return 0;
}

bool is_break_tag(std::string_view name) {
    static constexpr std::array<std::string_view, 19> kTags{
        {"br", "p", "div", "li", "ul", "ol", "blockquote", "tr", "pre", "section",
         "article", "h1", "h2", "h3", "h4", "h5", "h6", "hr", "table"}};
    for (auto t : kTags) {
        if (t == name)
            return true;
    }
    return false;
}

std::string collapse_whitespace(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    bool pending_space = false;
    for (char c : s) {
        const bool ws = c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
        if (ws) {
            if (!out.empty())
                pending_space = true;
        } else {
            if (pending_space) {
                out.push_back(' ');
                pending_space = false;
            }
            out.push_back(c);
        }
    }
    return out;
}

} // namespace

std::string decode_entities(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    size_t i = 0;
    const size_t n = s.size();
    while (i < n) {
        if (s[i] != '&') {
            out.push_back(s[i]);
            ++i;
            continue;
        }
        const size_t semi = s.find(';', i + 1);
        if (semi != std::string_view::npos && semi - i <= 12) {
            const std::string_view ent = s.substr(i + 1, semi - (i + 1));
            bool handled = false;
            if (!ent.empty() && ent[0] == '#') {
                unsigned cp = 0;
                bool ok = false;
                if (ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X')) {
                    ok = ent.size() > 2;
                    for (size_t k = 2; k < ent.size() && ok; ++k) {
                        const char c = ent[k];
                        cp *= 16;
                        if (c >= '0' && c <= '9')
                            cp += static_cast<unsigned>(c - '0');
                        else if (c >= 'a' && c <= 'f')
                            cp += static_cast<unsigned>(c - 'a' + 10);
                        else if (c >= 'A' && c <= 'F')
                            cp += static_cast<unsigned>(c - 'A' + 10);
                        else
                            ok = false;
                    }
                } else {
                    ok = ent.size() > 1;
                    for (size_t k = 1; k < ent.size() && ok; ++k) {
                        const char c = ent[k];
                        if (c >= '0' && c <= '9')
                            cp = cp * 10 + static_cast<unsigned>(c - '0');
                        else
                            ok = false;
                    }
                }
                if (ok && cp != 0) {
                    append_utf8(out, cp);
                    handled = true;
                }
            } else if (unsigned cp = named_entity(ent)) {
                append_utf8(out, cp);
                handled = true;
            }
            if (handled) {
                i = semi + 1;
                continue;
            }
        }
        out.push_back('&');
        ++i;
    }
    return out;
}

std::string strip_html(std::string_view html) {
    std::string out;
    out.reserve(html.size());
    size_t i = 0;
    const size_t n = html.size();
    while (i < n) {
        if (html[i] != '<') {
            out.push_back(html[i]);
            ++i;
            continue;
        }
        const size_t end = html.find('>', i);
        if (end == std::string_view::npos)
            break; // unterminated tag; drop the remainder
        size_t p = i + 1;
        if (p < end && html[p] == '/')
            ++p;
        const size_t name_start = p;
        while (p < end && std::isalnum(static_cast<unsigned char>(html[p])))
            ++p;
        std::string name;
        name.reserve(p - name_start);
        for (size_t k = name_start; k < p; ++k)
            name.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(html[k]))));
        if (is_break_tag(name))
            out.push_back(' ');
        i = end + 1;
    }
    return collapse_whitespace(decode_entities(out));
}

} // namespace fastsm::util
