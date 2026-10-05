#include <cctype>
#include "fastsm/util/url.hpp"

namespace fastsm::util {

std::string percent_encode(std::string_view s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 0x0F]);
        }
    }
    return out;
}

std::string form_encode(const std::vector<std::pair<std::string, std::string>>& params) {
    std::string out;
    bool first = true;
    for (const auto& [key, value] : params) {
        if (!first)
            out.push_back('&');
        first = false;
        out += percent_encode(key);
        out.push_back('=');
        out += percent_encode(value);
    }
    return out;
}

std::string url_host(std::string_view url) {
    // Scheme, if any.
    if (const auto scheme = url.find("://"); scheme != std::string_view::npos)
        url.remove_prefix(scheme + 3);
    // Anything after the authority.
    for (const char* delim : {"/", "?", "#"})
        if (const auto at = url.find(delim); at != std::string_view::npos)
            url = url.substr(0, at);
    // user:pass@ prefix.
    if (const auto at = url.rfind('@'); at != std::string_view::npos)
        url.remove_prefix(at + 1);
    // :port suffix. Guarded against an IPv6 literal, where the colons are inside [].
    if (url.find(']') == std::string_view::npos)
        if (const auto colon = url.rfind(':'); colon != std::string_view::npos)
            url = url.substr(0, colon);
    std::string host(url);
    for (char& c : host)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return host;
}

} // namespace fastsm::util
