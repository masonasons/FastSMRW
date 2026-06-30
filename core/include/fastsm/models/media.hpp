#pragma once

#include <string>

namespace fastsm {

// A media attachment on a status.
struct MediaAttachment {
    enum class Kind {
        Image,
        Video,
        Audio,
        Gifv,
        Unknown,
    };

    std::string id;
    Kind type = Kind::Unknown;
    std::string url;
    std::string preview_url;
    std::string description; // alt text
};

// A user mentioned in a status.
struct Mention {
    std::string id;
    std::string acct;
    std::string username;
    std::string url;
};

} // namespace fastsm
