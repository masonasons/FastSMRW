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

// Result of begin_login: the registered app's credentials (no access token yet)
// plus the authorize URL to send the user to, and the CSRF state.
struct MastodonBeginResult {
    bool ok = false;
    std::string error;
    MastodonCredentials credentials; // instance_url + client_id/secret
    std::string authorize_url;
    std::string state;
};

// Drives the Mastodon OAuth flow. The flow is two portable steps:
//   begin_login()  — register the app for a redirect URI, build the authorize
//                    URL (send the user there).
//   finish_login() — exchange the returned code for a token and verify.
// Front ends that own their redirect handling (e.g. Android's Custom Tab +
// fastsm://oauth deep link) call these directly. On Windows, login() wraps them
// around a 127.0.0.1 loopback listener and blocks for the redirect (worker
// thread); `open_browser` is invoked with the authorize URL to ShellExecute.
class MastodonAuth {
public:
    explicit MastodonAuth(net::IHttpClient* http);

    MastodonBeginResult begin_login(const std::string& instance_input,
                                    const std::string& redirect_uri);
    MastodonLoginResult finish_login(const MastodonCredentials& credentials,
                                     const std::string& code, const std::string& redirect_uri);

    MastodonLoginResult login(const std::string& instance_input,
                              const std::function<void(const std::string& url)>& open_browser);

    static std::string normalize_instance(std::string input);

private:
    net::IHttpClient* http_;
};

} // namespace fastsm
