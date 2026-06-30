#pragma once

#include <optional>
#include <string>
#include <vector>

#include "fastsm/net/http_client.hpp"
#include "fastsm/platform/bluesky/bluesky_credentials.hpp"
#include "fastsm/platform/social_account.hpp"

namespace fastsm {

class BlueskyAccount : public SocialAccount {
public:
    BlueskyAccount(BlueskyCredentials credentials, BlueskySession session, User me,
                   net::IHttpClient* http);

    Platform platform() const override { return Platform::Bluesky; }
    const User& me() const override { return me_; }
    int max_chars() const override { return 300; }
    std::string account_key() const override { return "bluesky:" + credentials_.did; }
    PlatformFeatures features() const override;
    std::vector<TimelineSource> default_timelines() const override;

    TimelinePage items(const TimelineSource& source, int limit,
                       const PageCursor& cursor) override;

    std::optional<Status> post(const PostDraft& draft) override;
    bool boost(const Status& status) override;
    bool unboost(const Status& status) override;
    bool favorite(const Status& status) override;
    bool unfavorite(const Status& status) override;

    const BlueskyCredentials& credentials() const { return credentials_; }

private:
    // Sends with the access JWT; on an expired-token response, refreshes once
    // and retries.
    net::HttpResponse send_authed(const std::string& method, const std::string& url,
                                  const std::string& json_body);
    bool refresh_session();
    bool create_record(const char* collection, const std::string& record_json);
    bool delete_record(const char* collection, const std::string& at_uri);

    BlueskyCredentials credentials_;
    BlueskySession session_;
    User me_;
    net::IHttpClient* http_;
};

} // namespace fastsm
