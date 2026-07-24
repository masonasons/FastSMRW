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

    bool supports_position_sync() const override { return true; }
    std::optional<std::string> home_marker() override;
    bool set_home_marker(const std::string& status_id) override;

    TimelinePage items(const TimelineSource& source, int limit,
                       const PageCursor& cursor) override;

    std::optional<Status> post(const PostDraft& draft) override;
    std::optional<Status> edit_post(const std::string& id, const PostDraft& draft) override;
    std::optional<PostSource> post_source(const std::string& id) override;
    bool boost(const Status& status) override;
    bool unboost(const Status& status) override;
    bool favorite(const Status& status) override;
    bool unfavorite(const Status& status) override;
    bool pin_post(const Status& status) override;
    bool unpin_post(const Status& status) override;
    bool delete_post(const Status& status) override;
    bool mute_conversation(const Status& status) override;
    bool unmute_conversation(const Status& status) override;
    bool bookmark(const Status& status) override;
    bool unbookmark(const Status& status) override;
    bool report(const ReportDraft& draft) override;
    std::optional<ProfileSource> profile_source() override;
    bool update_profile(const ProfileSource& profile) override;
    std::optional<Poll> vote_poll(const std::string& poll_id,
                                  const std::vector<int>& choices) override;

    std::optional<User> fetch_profile(const std::string& id) override;
    std::optional<User> lookup_user(const std::string& handle) override;
    std::optional<Status> fetch_status(const std::string& id) override;
    std::vector<User> search_accounts(const std::string& query, int limit) override;
    FullRelationResult fetch_all_relations(const std::string& id, bool following) override;
    std::optional<Relationship> relationship(const std::string& id) override;
    bool follow(const std::string& id) override;
    bool unfollow(const std::string& id) override;
    bool mute(const std::string& id) override;
    bool unmute(const std::string& id) override;
    bool block(const std::string& id) override;
    bool unblock(const std::string& id) override;
    bool authorize_follow_request(const std::string& id) override;
    bool reject_follow_request(const std::string& id) override;
    bool set_show_boosts(const std::string& id, bool show) override;

    bool follow_hashtag(const std::string& name) override;
    bool unfollow_hashtag(const std::string& name) override;
    std::vector<FollowedTag> followed_hashtags() override;
    std::vector<FollowedTag> trending_hashtags() override;

    std::vector<TimelineList> lists() override;
    std::vector<TimelineList> account_lists(const std::string& account_id) override;
    bool set_list_membership(const std::string& list_id, const std::string& account_id,
                             bool add) override;
    bool create_list(const std::string& title, const std::string& replies_policy,
                     bool exclusive) override;
    bool update_list(const std::string& id, const std::string& title,
                     const std::string& replies_policy, bool exclusive) override;
    bool delete_list(const std::string& id) override;

    bool supports_server_filters() const override { return true; }
    std::vector<ServerFilter> list_server_filters() override;
    bool create_server_filter(const ServerFilter& filter) override;
    bool update_server_filter(const ServerFilter& filter) override;
    bool delete_server_filter(const std::string& id) override;

    std::optional<StreamRequest> stream_request_for(const TimelineSource& source) const override;
    std::optional<StreamItem> parse_stream_event(const std::string& event, const std::string& data,
                                                 const TimelineSource& route) const override;

    const MastodonCredentials& credentials() const { return credentials_; }

private:
    // Issues an authenticated request; returns the parsed JSON body on 2xx.
    // `out_status` (optional) receives the HTTP status code regardless.
    bool request(const std::string& method, const std::string& url, const std::string& body,
                 const std::string& content_type, std::string& out_body, long& out_status);
    bool status_action(const std::string& status_id, const char* verb);
    bool account_action(const std::string& id, const char* verb);
    // Upload one attachment (multipart POST /api/v2/media, with alt text as
    // description) and return its media id, polling while the server processes it.
    std::optional<std::string> upload_media(const MediaUpload& a);

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
    int max_profile_fields_ = 4; // server's max profile metadata fields (default 4)
    // Set once we learn this instance predates grouped notifications (/api/v2/
    // notifications 404s) so we stop probing v2 and go straight to v1.
    bool grouped_notifs_unsupported_ = false;
};

} // namespace fastsm
