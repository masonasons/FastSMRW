#include "fastsm/platform/bluesky/bluesky_map.hpp"

#include <memory>
#include <string>

#include "fastsm/util/date_parsing.hpp"

using nlohmann::json;

namespace fastsm::bluesky {
namespace {

std::string str(const json& j, const char* key) {
    auto it = j.find(key);
    if (it != j.end() && it->is_string())
        return it->get<std::string>();
    return {};
}

const json* obj(const json& j, const char* key) {
    auto it = j.find(key);
    if (it != j.end() && it->is_object())
        return &*it;
    return nullptr;
}

// Pull image attachments out of an embed view.
void map_embed_media(const json& embed, Status& s) {
    const std::string type = str(embed, "$type");
    if (type == "app.bsky.embed.images#view") {
        if (auto it = embed.find("images"); it != embed.end() && it->is_array()) {
            for (const auto& img : *it) {
                MediaAttachment m;
                m.type = MediaAttachment::Kind::Image;
                m.url = str(img, "fullsize");
                m.preview_url = str(img, "thumb");
                m.description = str(img, "alt");
                s.media_attachments.push_back(std::move(m));
            }
        }
    }
}

Notification::Kind notif_kind(const std::string& reason) {
    if (reason == "like")
        return Notification::Kind::Favourite;
    if (reason == "repost")
        return Notification::Kind::Reblog;
    if (reason == "follow")
        return Notification::Kind::Follow;
    // Bluesky splits an @-mention into mention/reply/quote; all read as "mention".
    if (reason == "mention" || reason == "reply" || reason == "quote")
        return Notification::Kind::Mention;
    return Notification::Kind::Unknown;
}

} // namespace

User map_author(const json& j) {
    User u;
    u.platform = Platform::Bluesky;
    u.id = str(j, "did");
    u.acct = str(j, "handle");
    u.username = u.acct;
    u.display_name = str(j, "displayName");
    u.note = str(j, "description");
    u.avatar_url = str(j, "avatar");
    u.followers_count = j.value("followersCount", 0);
    u.following_count = j.value("followsCount", 0);
    u.statuses_count = j.value("postsCount", 0);
    u.url = "https://bsky.app/profile/" + u.acct;
    return u;
}

Status map_post(const json& post) {
    Status s;
    s.platform = Platform::Bluesky;
    s.id = str(post, "uri");
    s.cid = str(post, "cid");
    if (const json* author = obj(post, "author"))
        s.account = map_author(*author);

    if (const json* record = obj(post, "record")) {
        s.text = str(*record, "text");
        s.created_at = util::parse_iso8601(str(*record, "createdAt")).value_or(0);
        if (const json* reply = obj(*record, "reply")) {
            if (const json* parent = obj(*reply, "parent"))
                if (std::string uri = str(*parent, "uri"); !uri.empty())
                    s.in_reply_to_id = uri;
        }
    }
    if (s.created_at == 0)
        s.created_at = util::parse_iso8601(str(post, "indexedAt")).value_or(0);

    s.favourites_count = post.value("likeCount", 0);
    s.boosts_count = post.value("repostCount", 0);
    s.replies_count = post.value("replyCount", 0);

    if (const json* viewer = obj(post, "viewer")) {
        if (std::string like = str(*viewer, "like"); !like.empty()) {
            s.favourited = true;
            s.like_uri = like;
        }
        if (std::string repost = str(*viewer, "repost"); !repost.empty()) {
            s.boosted = true;
            s.repost_uri = repost;
        }
    }

    if (const json* embed = obj(post, "embed")) {
        map_embed_media(*embed, s);
        // Quote post (record embed): app.bsky.embed.record#view
        if (str(*embed, "$type") == "app.bsky.embed.record#view") {
            if (const json* rec = obj(*embed, "record")) {
                Status q;
                q.platform = Platform::Bluesky;
                q.id = str(*rec, "uri");
                q.cid = str(*rec, "cid");
                if (const json* qa = obj(*rec, "author"))
                    q.account = map_author(*qa);
                if (const json* val = obj(*rec, "value"))
                    q.text = str(*val, "text");
                s.quote = std::make_shared<Status>(std::move(q));
            }
        }
    }
    return s;
}

Notification map_notification(const json& j) {
    Notification n;
    n.platform = Platform::Bluesky;
    n.id = str(j, "uri");
    n.type = notif_kind(str(j, "reason"));
    if (const json* author = obj(j, "author"))
        n.account = map_author(*author);
    n.created_at = util::parse_iso8601(str(j, "indexedAt")).value_or(0);
    // A mention/reply/quote notification's own record is the incoming post; build
    // a light Status so the row can read its text (matching the Mastodon side).
    if (n.type == Notification::Kind::Mention) {
        Status s;
        s.platform = Platform::Bluesky;
        s.id = str(j, "uri");
        s.cid = str(j, "cid");
        s.account = n.account;
        if (const json* record = obj(j, "record")) {
            s.text = str(*record, "text");
            s.created_at = util::parse_iso8601(str(*record, "createdAt")).value_or(n.created_at);
        }
        if (s.created_at == 0)
            s.created_at = n.created_at;
        n.status = std::make_shared<Status>(std::move(s));
    }
    return n;
}

Status map_feed_item(const json& item) {
    const json* post = obj(item, "post");
    Status inner = post ? map_post(*post) : Status{};

    // A repost "reason" turns the row into a boost authored by the reposter.
    if (const json* reason = obj(item, "reason")) {
        if (str(*reason, "$type") == "app.bsky.feed.defs#reasonRepost") {
            Status boost;
            boost.platform = Platform::Bluesky;
            boost.id = inner.id; // the underlying post identifies the row
            boost.created_at = util::parse_iso8601(str(*reason, "indexedAt")).value_or(inner.created_at);
            if (const json* by = obj(*reason, "by"))
                boost.account = map_author(*by);
            boost.reblog = std::make_shared<Status>(std::move(inner));
            return boost;
        }
    }
    return inner;
}

} // namespace fastsm::bluesky
