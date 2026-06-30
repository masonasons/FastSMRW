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

} // namespace fastsm::util
