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
    std::vector<TimelineSource> spawnable_timelines() const override;
    void load_configuration() override;

    TimelinePage items(const TimelineSource& source, int limit,
                       const PageCursor& cursor) override;

    std::optional<Status> post(const PostDraft& draft) override;
    std::optional<Status> edit_post(const std::string& id, const PostDraft& draft) override;
    std::optional<PostSource> post_source(const std::string& id) override;
    bool boost(const Status& status) override;
    bool unboost(const Status& status) override;
    bool favorite(const Status& status) override;
    bool unfavorite(const Status& status) override;

    std::optional<Relationship> relationship(const std::string& id) override;
    bool follow(const std::string& id) override;
    bool unfollow(const std::string& id) override;
    bool mute(const std::string& id) override;
    bool unmute(const std::string& id) override;
    bool block(const std::string& id) override;
    bool unblock(const std::string& id) override;
    bool set_show_boosts(const std::string& id, bool show) override;

    std::optional<StreamRequest> user_stream_request() const override;
    std::optional<StreamItem> parse_stream_event(const std::string& event,
                                                 const std::string& data) const override;

    const MastodonCredentials& credentials() const { return credentials_; }

private:
    // Issues an authenticated request; returns the parsed JSON body on 2xx.
    // `out_status` (optional) receives the HTTP status code regardless.
    bool request(const std::string& method, const std::string& url, const std::string& body,
                 const std::string& content_type, std::string& out_body, long& out_status);
    bool status_action(const std::string& status_id, const char* verb);
    bool account_action(const std::string& id, const char* verb);

    // --- Remote timelines (fetched unauthenticated from a foreign instance) ---
    // Look up a username's account id on its home instance (unauthenticated).
    std::string remote_account_id(const std::string& base, const std::string& username);
    // Search the user's own instance for a remote post's URL (resolve=true pulls
    // it in if unknown); returns the local status id, or nullopt.
    std::optional<std::string> resolve_url(const std::string& post_url);
    // The status id to act on: a remote post's local copy if it resolves, else
    // the status's own id.
    std::string action_status_id(const Status& status);

    MastodonCredentials credentials_;
    User me_;
    net::IHttpClient* http_;
    int max_chars_;
};

} // namespace fastsm
