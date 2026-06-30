#pragma once

#include <string>

namespace fastsm {

// Persisted Bluesky credentials. The app password is the secret; the session
// (JWTs) is re-established from these at launch.
struct BlueskyCredentials {
    std::string service_url; // entryway, e.g. "https://bsky.social"
    std::string identifier;  // handle or email used to sign in
    std::string app_password;
    std::string did;
    std::string handle;
};

// Runtime session, not persisted. pds_url is where app.bsky.* / com.atproto.*
// calls are sent (the user's PDS, which proxies reads to the AppView).
struct BlueskySession {
    std::string access_jwt;
    std::string refresh_jwt;
    std::string did;
    std::string handle;
    std::string pds_url;
};

} // namespace fastsm
