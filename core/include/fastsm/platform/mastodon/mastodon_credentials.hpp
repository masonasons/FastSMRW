#pragma once

#include <string>

namespace fastsm {

// Persisted credentials for a Mastodon account. The access token is the secret;
// the client id/secret come from app registration (client_name=FastSMRW).
struct MastodonCredentials {
    std::string instance_url; // normalized, e.g. "https://mastodon.social"
    std::string client_id;
    std::string client_secret;
    std::string access_token;
};

} // namespace fastsm
