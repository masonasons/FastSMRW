#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fastsm::util {

// Percent-encode per RFC 3986 (unreserved A-Za-z0-9-_.~ kept verbatim).
std::string percent_encode(std::string_view s);

// The bare host of a URL: "https://mastodon.social/" -> "mastodon.social". Drops the
// scheme, any userinfo, the port and the path, and lowercases what's left, so a server
// can be shown to the user without the plumbing around it. Returns the input trimmed of
// a scheme if it doesn't parse as a URL (a bare "mastodon.social" passes through).
std::string url_host(std::string_view url);

// Build an application/x-www-form-urlencoded body / query string from pairs,
// preserving order and allowing duplicate keys (e.g. poll[options][]).
std::string form_encode(const std::vector<std::pair<std::string, std::string>>& params);

} // namespace fastsm::util
