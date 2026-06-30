#pragma once

#include <optional>
#include <string>
#include <vector>

#include "fastsm/net/http_client.hpp"
#include "fastsm/platform/mastodon/mastodon_credentials.hpp"
#include "fastsm/platform/social_account.hpp"

namespace fastsm {

class MastodonAccount : public SocialAccount {
public:
    MastodonAccount(MastodonCredentials credentials, User me, net::IHttpClient* http,
                    int max_chars = 500);

    Platform platform() const override { return Platform::Mastodon; }
    const User& me() const override { return me_; }
    int max_chars() const override { return max_chars_; }
    std::string account_key() const override { return "mastodon:" + me_.id; }
    PlatformFeatures features() const override;
    std::vector<TimelineSource> default_timelines() const override;

    TimelinePage items(const TimelineSource& source, int limit,
                       const PageCursor& cursor) override;

    std::optional<Status> post(const PostDraft& draft) override;
    bool boost(const Status& status) override;
    bool unboost(const Status& status) override;
    bool favorite(const Status& status) override;
    bool unfavorite(const Status& status) override;

    const MastodonCredentials& credentials() const { return credentials_; }

private:
    // Issues an authenticated request; returns the parsed JSON body on 2xx.
    // `out_status` (optional) receives the HTTP status code regardless.
    bool request(const std::string& method, const std::string& url, const std::string& body,
                 const std::string& content_type, std::string& out_body, long& out_status);
    bool status_action(const std::string& status_id, const char* verb);

    MastodonCredentials credentials_;
    User me_;
    net::IHttpClient* http_;
    int max_chars_;
};

} // namespace fastsm
