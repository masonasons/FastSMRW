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
    std::vector<TimelineSource> spawnable_timelines() const override;

    int max_page_size() const override { return 100; } // Bluesky API cap per call

    TimelinePage items(const TimelineSource& source, int limit,
                       const PageCursor& cursor) override;

    std::optional<Status> post(const PostDraft& draft) override;
    bool boost(const Status& status) override;
    bool unboost(const Status& status) override;
    bool favorite(const Status& status) override;
    bool unfavorite(const Status& status) override;
    bool delete_post(const Status& status) override;

    std::optional<User> fetch_profile(const std::string& id) override;
    std::optional<User> lookup_user(const std::string& handle) override;
    std::optional<Status> fetch_status(const std::string& uri) override;
    std::vector<User> search_accounts(const std::string& query, int limit) override;
    FullRelationResult fetch_all_relations(const std::string& id, bool following) override;
    std::optional<Relationship> relationship(const std::string& id) override;
    bool follow(const std::string& id) override;
    bool unfollow(const std::string& id) override;
    bool mute(const std::string& id) override;
    bool unmute(const std::string& id) override;
    bool block(const std::string& id) override;
    bool unblock(const std::string& id) override;

    const BlueskyCredentials& credentials() const { return credentials_; }

private:
    // Sends with the access JWT; on an expired-token response, refreshes once
    // and retries.
    net::HttpResponse send_authed(const std::string& method, const std::string& url,
                                  const std::string& json_body);
    bool refresh_session();
    bool create_record(const char* collection, const std::string& record_json);
    bool delete_record(const char* collection, const std::string& at_uri);
    // Raw app.bsky.actor.getProfile response body for an actor (did or handle).
    std::optional<std::string> get_profile_body(const std::string& actor);
    bool mute_actor(const std::string& did, bool mute);

    BlueskyCredentials credentials_;
    BlueskySession session_;
    User me_;
    net::IHttpClient* http_;
};

} // namespace fastsm
