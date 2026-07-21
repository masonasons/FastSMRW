#pragma once

#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

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
    bool pin_post(const Status& status) override;
    bool unpin_post(const Status& status) override;
    bool report(const ReportDraft& draft) override;
    std::optional<ProfileSource> profile_source() override;
    bool update_profile(const ProfileSource& profile) override;

    std::optional<User> fetch_profile(const std::string& id) override;
    std::optional<User> lookup_user(const std::string& handle) override;
    std::optional<Status> fetch_status(const std::string& uri) override;
    std::vector<User> search_accounts(const std::string& query, int limit) override;
    std::vector<TimelineList> lists() override;
    std::vector<TimelineList> saved_feeds() override;
    std::vector<std::string> muted_words() override;
    void mark_notifications_seen() override;
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
    // and retries. `content_type` defaults to JSON; blob uploads pass the file's
    // MIME so the raw `body` bytes are sent verbatim.
    net::HttpResponse send_authed(const std::string& method, const std::string& url,
                                  const std::string& body,
                                  const std::string& content_type = "application/json");
    // Upload a media file to the repo; returns the resulting `blob` ref object to
    // embed in a post, or nullopt on failure.
    std::optional<nlohmann::json> upload_blob(const MediaUpload& media);
    // Build a post's `embed` from attachments (images/video) and/or a quoted post,
    // choosing images/video/record/recordWithMedia as appropriate. Null if none.
    std::optional<nlohmann::json> build_embed(const PostDraft& draft);
    bool refresh_session();
    bool create_record(const char* collection, const std::string& record_json);
    bool delete_record(const char* collection, const std::string& at_uri);
    // Raw app.bsky.actor.getProfile response body for an actor (did or handle).
    std::optional<std::string> get_profile_body(const std::string& actor);
    bool mute_actor(const std::string& did, bool mute);
    // Resolve a handle (no leading '@') to a DID, or "" if it can't be resolved.
    std::string resolve_handle(const std::string& handle);
    // Build a reply record's {root, parent} strong-ref block for a parent at-uri,
    // walking to the thread root, or nullopt if the parent can't be fetched.
    std::optional<nlohmann::json> build_reply_ref(const std::string& parent_uri);
    // Build the richtext `facets` array (links/mentions/tags) for post text, or an
    // empty array if there are none. Resolves mention handles to DIDs.
    nlohmann::json build_facets(const std::string& text);
    // Read / write the app.bsky.actor.profile record (rkey "self"). get returns the
    // record's `value` object (or nullopt); put replaces it (read-modify-write so
    // avatar/banner/pinned survive edits).
    std::optional<nlohmann::json> get_profile_record();
    bool put_profile_record(const nlohmann::json& value);

    BlueskyCredentials credentials_;
    BlueskySession session_;
    User me_;
    net::IHttpClient* http_;
};

} // namespace fastsm
