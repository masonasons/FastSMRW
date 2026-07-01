#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace fastsm {

// One keyword within a server-side filter (Mastodon /api/v2/filters).
struct ServerFilterKeyword {
    std::string id;         // server keyword id (empty for a not-yet-created keyword)
    std::string keyword;    // the text to match
    bool whole_word = true; // match on word boundaries only
};

// A Mastodon server-side filter. The server evaluates these and tags matching
// statuses with a `filtered` array; the client only manages them (CRUD) and
// honors the hide/warn action it sees on statuses.
struct ServerFilter {
    std::string id;
    std::string title;
    std::vector<std::string> context; // "home","notifications","public","thread","account"
    std::string action = "warn";      // "warn" | "hide"
    std::optional<std::int64_t> expires_at; // unix seconds; nullopt = never (read side)
    int expires_in = 0; // seconds from now to set on create/update; 0 = never (write side)
    std::vector<ServerFilterKeyword> keywords;
};

} // namespace fastsm
