#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace fastsm {

// A link preview card.
struct Card {
    std::string url;
    std::string title;
    std::string description;
    std::string image_url;
};

// A poll attached to a status (Mastodon).
struct Poll {
    struct Option {
        std::string title;
        int votes_count = 0;
    };

    std::string id;
    std::int64_t expires_at = 0; // unix seconds; 0 = none
    bool expired = false;
    bool multiple = false;
    int votes_count = 0;
    bool voted = false;
    std::vector<Option> options;
};

} // namespace fastsm
