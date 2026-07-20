#pragma once

#include <optional>
#include <string>
#include <vector>

#include "fastsm/models/models.hpp"
#include "fastsm/net/http_client.hpp"
#include "fastsm/timeline/timeline_source.hpp"

// SocialAccount unifies Mastodon and Bluesky behind one interface. M1 covers the
// home-timeline vertical slice: paged fetch, post, and boost/favorite toggles.
// (Remote-post resolution, user actions, push, etc. arrive in later milestones.)

namespace fastsm {

// Capability flags so the UI can adapt per platform.
struct PlatformFeatures {
    bool visibility = false;      // post visibility levels (Mastodon)
    bool content_warning = false; // spoiler text
    bool quote_posts = false;
    bool polls = false;
    bool editing = false;
    bool scheduling = false;
    bool hide_boosts = false; // can hide/show a followed account's boosts (Mastodon)
    bool media = false;       // attach media (with alt text) to posts
    bool follow_hashtags = false; // follow/unfollow hashtags (Mastodon)
    bool mute_conversations = false; // mute/unmute a thread's notifications (Mastodon)
    bool bookmarks = false;          // save/unsave a post to your bookmarks (Mastodon)
};

// A moderation report. account_id is required; status_ids optionally names specific
// posts. category is "spam" | "violation" | "legal" | "other"; forward sends the
// report on to the user's remote instance (Mastodon). rule_ids apply only when
// category == "violation".
struct ReportDraft {
    std::string account_id;
    std::vector<std::string> status_ids;
    std::string comment;
    std::string category = "other";
    bool forward = false;
    std::vector<std::string> rule_ids;
};

// One profile metadata row ("Website" / "https://…"). Mastodon allows up to 4.
struct ProfileField {
    std::string name;
    std::string value;
};

// The editable source of your own profile (raw, not HTML) for the profile editor.
struct ProfileSource {
    std::string display_name;
    std::string note; // bio, plain-text source
    std::vector<ProfileField> fields; // metadata rows (up to 4)
    bool locked = false;        // require follow requests to follow you
    bool bot = false;           // this account is automated
    bool discoverable = false;  // list this account in the profile directory
    std::string privacy = "public"; // default posting visibility (source[privacy])
    bool sensitive = false;     // mark your media as sensitive by default
    int max_fields = 4;         // how many metadata fields the server allows
};

// A hashtag the viewer follows (Mastodon /api/v1/followed_tags): its name (no
// '#'), canonical URL, and whether the viewer currently follows it.
struct FollowedTag {
    std::string name;
    std::string url;
    bool following = true;
};

// A media file to attach to a new post: the raw bytes plus a filename, MIME
// type, and alt-text description. The platform uploads/embeds it at post time.
struct MediaUpload {
    std::string filename; // e.g. "photo.jpg"
    std::string mime;     // e.g. "image/jpeg"
    std::string bytes;    // raw file contents
    std::string alt;      // alt-text description (may be empty)
};

// The viewer's relationship to another account (Mastodon).
struct Relationship {
    std::string id; // the other account's id
    bool following = false;
    bool followed_by = false;
    bool muting = false;
    bool blocking = false;
    bool requested = false;        // follow request pending (locked accounts)
    bool showing_reblogs = true;   // false == this account's boosts are hidden
};

// Outcome of fetching a COMPLETE relation list (all followers / all following).
// Only Ok means `users` is the full, trustworthy list; on RateLimited or Failed
// the list is partial and MUST NOT be presented (used by User Analysis, which
// must never show a false result). RateLimited is called out separately so the
// UI can tell the user to try again later rather than reporting a generic error.
struct FullRelationResult {
    enum class Status { Ok, RateLimited, Failed };
    Status status = Status::Failed;
    std::vector<User> users;
};

// Pagination cursor. Mastodon pages by max_id; Bluesky by an opaque token.
enum class CursorKind { Start, MaxID, Token };

struct PageCursor {
    CursorKind kind = CursorKind::Start;
    std::string value;

    bool is_start() const { return kind == CursorKind::Start; }

    static PageCursor start() { return {}; }
    static PageCursor max_id(std::string v) { return {CursorKind::MaxID, std::move(v)}; }
    static PageCursor token(std::string v) { return {CursorKind::Token, std::move(v)}; }
};

struct TimelinePage {
    std::vector<TimelineItem> items;
    std::optional<PageCursor> next_cursor; // nullopt = end of timeline
};

// A poll to attach to a new post (Mastodon).
struct PollDraft {
    std::vector<std::string> options;
    bool multiple = false;
    int expires_in_seconds = 86400;
};

// Input for composing a post.
struct PostDraft {
    std::string text;
    std::optional<std::string> reply_to_id;
    // When replying to a post fetched from a remote instance, its canonical URL,
    // so the platform can resolve it to a local copy before posting the reply.
    std::optional<std::string> reply_to_url;
    std::optional<Visibility> visibility;
    std::optional<std::string> spoiler_text;
    std::optional<std::string> quoted_status_id;
    std::optional<std::string> quoted_status_cid; // Bluesky: the quoted post's cid (with the uri)
    std::optional<std::string> quoted_status_url; // remote Mastodon quote -> resolve to a local id
    std::optional<std::string> language;
    std::optional<PollDraft> poll;
    std::optional<std::int64_t> scheduled_at; // unix seconds (Mastodon)
    std::vector<MediaUpload> attachments;     // media to upload + attach (with alt text)
};

// Editable source of an existing post (for Edit).
struct PostSource {
    std::string text;
    std::string spoiler_text;
};

// A user's timeline list (Mastodon /api/v1/lists): id + display title, plus the
// list's settings (used by the list manager; ignored elsewhere).
struct TimelineList {
    std::string id;
    std::string title;
    std::string replies_policy = "list"; // "none" | "list" | "followed"
    bool exclusive = false;              // hide members from the home timeline
};

// A long-lived request to open for real-time streaming (e.g. Mastodon SSE).
struct StreamRequest {
    std::string url;
    net::Headers headers;
};

// One parsed streaming event. `op` says what to do with it: add a new row,
// replace an edited post in place, or remove a deleted post.
struct StreamItem {
    enum class Op { Add, Update, Delete };
    Op op = Op::Add;
    TimelineItem item;      // the post/notification (Add and Update)
    TimelineSource target;  // kind + param, so hashtag/list route to the right tab (Add)
    std::string removed_id; // the deleted status id (Delete)
};

class SocialAccount {
public:
    virtual ~SocialAccount() = default;

    virtual Platform platform() const = 0;
    virtual const User& me() const = 0;
    virtual int max_chars() const = 0;
    virtual std::string account_key() const = 0; // "mastodon:<id>" / "bluesky:<did>"
    virtual PlatformFeatures features() const = 0;
    virtual std::vector<TimelineSource> default_timelines() const = 0;
    // Timelines the user can open via the New Timeline dialog (Ctrl+T).
    virtual std::vector<TimelineSource> spawnable_timelines() const { return {}; }

    // Refresh server-derived configuration (e.g. the instance character limit).
    // Runs synchronously on the worker thread; default is a no-op for platforms
    // with a fixed limit (Bluesky).
    virtual void load_configuration() {}

    // Largest page the platform's API accepts per call (Mastodon 40, Bluesky 100).
    // The controller uses this so each fetch pulls as much as the server allows.
    virtual int max_page_size() const { return 40; }

    // Fetch one page. Implementations run synchronously on the worker thread.
    virtual TimelinePage items(const TimelineSource& source, int limit,
                               const PageCursor& cursor) = 0;

    // Create a post; returns the created status on success.
    virtual std::optional<Status> post(const PostDraft& draft) = 0;

    // Edit an existing post (text/CW/language). Returns the updated status, or
    // nullopt if unsupported (Bluesky) or on failure.
    virtual std::optional<Status> edit_post(const std::string&, const PostDraft&) { return std::nullopt; }
    // Fetch the editable source (raw text + CW) of a post, or nullopt.
    virtual std::optional<PostSource> post_source(const std::string&) { return std::nullopt; }

    // Interaction toggles. Take the full status so platforms can extract what
    // they need (Mastodon: id; Bluesky: at-uri + cid + viewer record uris).
    virtual bool boost(const Status& status) = 0;
    virtual bool unboost(const Status& status) = 0;
    virtual bool favorite(const Status& status) = 0;
    virtual bool unfavorite(const Status& status) = 0;
    // Pin / unpin one of YOUR OWN posts to your profile. Optional: platforms that
    // don't support it (Bluesky) inherit these no-op stubs.
    virtual bool pin_post(const Status&) { return false; }
    virtual bool unpin_post(const Status&) { return false; }
    // Delete one of YOUR OWN posts. Returns success.
    virtual bool delete_post(const Status&) { return false; }
    // Mute / unmute a conversation (stop / resume notifications about a thread).
    // Optional: unsupported platforms (Bluesky) inherit these no-op stubs.
    virtual bool mute_conversation(const Status&) { return false; }
    virtual bool unmute_conversation(const Status&) { return false; }
    // Save / unsave a post to your bookmarks. Optional: platforms that don't
    // support it (Bluesky) inherit these no-op stubs.
    virtual bool bookmark(const Status&) { return false; }
    virtual bool unbookmark(const Status&) { return false; }
    // Report an account (and optionally specific posts) to the server's moderators.
    // category is "spam" | "violation" | "legal" | "other"; forward sends it on to
    // the user's remote instance. Optional: unsupported platforms inherit the stub.
    virtual bool report(const ReportDraft&) { return false; }
    // Fetch the editable source of your own profile (display name + raw bio), or
    // nullopt if unsupported. Update your display name + bio; returns success.
    virtual std::optional<ProfileSource> profile_source() { return std::nullopt; }
    virtual bool update_profile(const ProfileSource& /*profile*/) { return false; }
    // Vote on a poll (choices = selected option indexes). Returns the updated poll,
    // or nullopt on failure / if unsupported.
    virtual std::optional<Poll> vote_poll(const std::string&, const std::vector<int>&) {
        return std::nullopt;
    }

    // --- User relationship actions (optional; Mastodon + Bluesky) ---
    // Fetch a fuller profile for an account (counts, bio) when the row's User is
    // sparse, or nullopt to use the User already in hand (Mastodon).
    virtual std::optional<User> fetch_profile(const std::string&) { return std::nullopt; }
    // Resolve a typed handle (e.g. "alice@example.com" or "alice.bsky.social", a
    // leading '@' is tolerated) to a User, or nullopt if it can't be found. Runs
    // synchronously on the worker thread. Lets the user look up someone by handle
    // even when they aren't in any open timeline.
    virtual std::optional<User> lookup_user(const std::string&) { return std::nullopt; }
    // Fetch a single status by id (Mastodon: status id; Bluesky: post uri), for
    // speaking a reply's parent that isn't loaded in any open timeline. Runs on
    // the worker thread. nullopt if unsupported or not found.
    virtual std::optional<Status> fetch_status(const std::string&) { return std::nullopt; }
    // Typeahead search for accounts whose handle/display name matches a partial
    // query, for @-mention autocomplete in the composer. Returns up to `limit`
    // users (best-match first), empty if none/unsupported. Runs synchronously on
    // the worker thread.
    virtual std::vector<User> search_accounts(const std::string& /*query*/, int /*limit*/) {
        return {};
    }
    // Fetch the COMPLETE followers (following=false) or following (following=true)
    // list for account `id`, paging through EVERY page. Runs synchronously on the
    // worker thread. Returns Ok only if every page was retrieved; RateLimited if a
    // 429 stopped it partway; Failed on any other error. On non-Ok the users are
    // partial and must be discarded, not shown. Default is Failed (unsupported).
    virtual FullRelationResult fetch_all_relations(const std::string& /*id*/, bool /*following*/) {
        return {};
    }
    // The viewer's relationship to an account, or nullopt if unsupported/failed.
    virtual std::optional<Relationship> relationship(const std::string&) { return std::nullopt; }
    virtual bool follow(const std::string&) { return false; }
    virtual bool unfollow(const std::string&) { return false; }
    virtual bool mute(const std::string&) { return false; }
    virtual bool unmute(const std::string&) { return false; }
    virtual bool block(const std::string&) { return false; }
    virtual bool unblock(const std::string&) { return false; }
    // Accept / reject a pending follow request (id = the requesting account id).
    virtual bool authorize_follow_request(const std::string&) { return false; }
    virtual bool reject_follow_request(const std::string&) { return false; }
    // Show or hide a followed account's boosts in the home timeline.
    virtual bool set_show_boosts(const std::string&, bool) { return false; }

    // --- Followed hashtags (optional; Mastodon /api/v1/tags, /followed_tags) ---
    // Follow / unfollow a hashtag by name (no '#'). Return success.
    virtual bool follow_hashtag(const std::string&) { return false; }
    virtual bool unfollow_hashtag(const std::string&) { return false; }
    // The hashtags the viewer currently follows. Runs synchronously on the worker
    // thread. Empty for platforms without hashtag following (Bluesky).
    virtual std::vector<FollowedTag> followed_hashtags() { return {}; }
    // The instance's currently trending hashtags (Mastodon /api/v1/trends/tags).
    // Each carries whether the viewer already follows it. Runs synchronously on
    // the worker thread. Empty for platforms without trends (Bluesky).
    virtual std::vector<FollowedTag> trending_hashtags() { return {}; }

    // --- Server-side keyword filters (optional; Mastodon /api/v2/filters) ---
    // Whether this platform exposes managed server filters at all (Mastodon yes,
    // Bluesky no). The UI shows an "only for Mastodon" notice when false.
    virtual bool supports_server_filters() const { return false; }
    // CRUD; all run synchronously on the worker thread. list returns the current
    // set; create/update/delete return success.
    virtual std::vector<ServerFilter> list_server_filters() { return {}; }
    virtual bool create_server_filter(const ServerFilter&) { return false; }
    virtual bool update_server_filter(const ServerFilter&) { return false; }
    virtual bool delete_server_filter(const std::string&) { return false; }

    // --- Timeline lists (optional; Mastodon /api/v1/lists) ---
    // Fetch the account's lists so the New Timeline dialog can offer them. Runs
    // synchronously on the worker thread. Empty for platforms without lists.
    virtual std::vector<TimelineList> lists() { return {}; }
    // The lists a given account is a member of (Mastodon /api/v1/accounts/:id/lists).
    virtual std::vector<TimelineList> account_lists(const std::string&) { return {}; }
    // Add or remove an account from one of the viewer's lists. (Mastodon only
    // permits adding accounts you follow.) Returns success.
    virtual bool set_list_membership(const std::string&, const std::string&, bool) { return false; }
    // List management (Mastodon): create/update/delete. All return success.
    // `replies_policy` is "none" | "list" | "followed"; `exclusive` hides members
    // from the home timeline.
    virtual bool create_list(const std::string& /*title*/, const std::string& /*replies_policy*/,
                             bool /*exclusive*/) {
        return false;
    }
    virtual bool update_list(const std::string& /*id*/, const std::string& /*title*/,
                             const std::string& /*replies_policy*/, bool /*exclusive*/) {
        return false;
    }
    virtual bool delete_list(const std::string&) { return false; }

    // --- Real-time streaming (optional; Mastodon SSE user stream) ---

    // The long-lived SSE request to open so `source` streams in real time, or
    // nullopt if that source can't stream on this platform. Home and Notifications
    // share one connection (the user stream); Local/Federated/Hashtag/List each
    // have their own endpoint.
    virtual std::optional<StreamRequest> stream_request_for(const TimelineSource& source) const {
        (void)source;
        return std::nullopt;
    }
    // Parse one streaming event into a timeline item and the timeline it feeds, or
    // nullopt for events we ignore (delete, keep-alives, filters, ...). `route` is
    // the source whose "update" events this stream carries.
    virtual std::optional<StreamItem> parse_stream_event(const std::string& event,
                                                         const std::string& data,
                                                         const TimelineSource& route) const {
        (void)event;
        (void)data;
        (void)route;
        return std::nullopt;
    }
};

} // namespace fastsm
