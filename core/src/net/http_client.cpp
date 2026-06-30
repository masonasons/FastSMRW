#include "fastsm/net/http_client.hpp"

#include <algorithm>
#include <cctype>

namespace fastsm::net {

namespace {
bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}
} // namespace

std::optional<std::string> HttpResponse::header(std::string_view name) const {
    for (const auto& [key, value] : headers) {
        if (iequals(key, name))
            return value;
    }
    return std::nullopt;
}

} // namespace fastsm::net
