#pragma once

#include <functional>
#include <string>

#include "fastsm/models/user.hpp"
#include "fastsm/net/http_client.hpp"
#include "fastsm/platform/mastodon/mastodon_credentials.hpp"

namespace fastsm {

struct MastodonLoginResult {
    bool ok = false;
    std::string error;
    MastodonCredentials credentials;
    User me;
};

// Drives the Mastodon OAuth flow on Windows using a 127.0.0.1 loopback redirect
// (no custom URL scheme). The client/source is registered as "FastSMRW".
//
// login() blocks (it runs a one-shot local listener waiting for the browser
// redirect), so call it from the worker thread. `open_browser` is invoked with
// the authorize URL; the front end should ShellExecute it.
class MastodonAuth {
public:
    explicit MastodonAuth(net::IHttpClient* http);

    MastodonLoginResult login(const std::string& instance_input,
                              const std::function<void(const std::string& url)>& open_browser);

    static std::string normalize_instance(std::string input);

private:
    net::IHttpClient* http_;
};

} // namespace fastsm
