#pragma once

#include <optional>
#include <string>
#include <vector>

#include "fastsm/models/models.hpp"
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
    std::optional<Visibility> visibility;
    std::optional<std::string> spoiler_text;
    std::optional<std::string> quoted_status_id;
    std::optional<std::string> language;
    std::optional<PollDraft> poll;
    std::optional<std::int64_t> scheduled_at; // unix seconds (Mastodon)
};

// Editable source of an existing post (for Edit).
struct PostSource {
    std::string text;
    std::string spoiler_text;
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
};

} // namespace fastsm
