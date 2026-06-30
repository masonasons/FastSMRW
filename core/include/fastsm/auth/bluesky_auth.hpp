#pragma once

#include <string>

#include "fastsm/models/user.hpp"
#include "fastsm/net/http_client.hpp"
#include "fastsm/platform/bluesky/bluesky_credentials.hpp"

namespace fastsm {

struct BlueskyLoginResult {
    bool ok = false;
    std::string error;
    BlueskyCredentials credentials;
    BlueskySession session;
    User me;
};

// Bluesky sign-in via handle + app password (com.atproto.server.createSession).
// Blocking; call from the worker thread.
class BlueskyAuth {
public:
    explicit BlueskyAuth(net::IHttpClient* http);

    BlueskyLoginResult login(const std::string& service_input, const std::string& identifier,
                             const std::string& app_password);

private:
    net::IHttpClient* http_;
};

} // namespace fastsm
