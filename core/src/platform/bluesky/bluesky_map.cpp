#include "fastsm/platform/bluesky/bluesky_map.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

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

// Map a media embed *view* (images / video / external link card) onto a status.
// recordWithMedia's media is one of these too, so this is shared.
void map_media_view(const json& view, Status& s) {
    const std::string type = str(view, "$type");
    if (type == "app.bsky.embed.images#view") {
        if (auto it = view.find("images"); it != view.end() && it->is_array()) {
            for (const auto& img : *it) {
                MediaAttachment m;
                m.type = MediaAttachment::Kind::Image;
                m.url = str(img, "fullsize");
                m.preview_url = str(img, "thumb");
                m.description = str(img, "alt");
                s.media_attachments.push_back(std::move(m));
            }
        }
    } else if (type == "app.bsky.embed.video#view") {
        MediaAttachment m;
        m.type = MediaAttachment::Kind::Video;
        m.url = str(view, "playlist"); // HLS manifest
        m.preview_url = str(view, "thumbnail");
        m.description = str(view, "alt");
        s.media_attachments.push_back(std::move(m));
    } else if (type == "app.bsky.embed.external#view") {
        // A link preview card (also how Bluesky surfaces GIFs, via Tenor).
        if (const json* ext = obj(view, "external")) {
            Card c;
            c.url = str(*ext, "uri");
            c.title = str(*ext, "title");
            c.description = str(*ext, "description");
            c.image_url = str(*ext, "thumb");
            if (!c.url.empty())
                s.card = std::move(c);
        }
    }
}

// Map an embedded quoted post (app.bsky.embed.record#viewRecord) to a light
// Status: enough to speak the author and text.
std::shared_ptr<Status> map_quote_record(const json& rec) {
    if (str(rec, "uri").empty())
        return nullptr; // viewNotFound / viewBlocked carry no post
    Status q;
    q.platform = Platform::Bluesky;
    q.id = str(rec, "uri");
    q.cid = str(rec, "cid");
    if (const json* qa = obj(rec, "author"))
        q.account = map_author(*qa);
    if (const json* val = obj(rec, "value"))
        q.text = str(*val, "text");
    return std::make_shared<Status>(std::move(q));
}

// Populate mentions/tags from a post record's richtext `facets` (byte-range
// annotations over the plain text). Mention facets carry the DID; the handle is
// the sliced display text ("@handle"). Tag facets carry the bare tag.
void map_facets(const json& record, Status& s) {
    auto fit = record.find("facets");
    if (fit == record.end() || !fit->is_array())
        return;
    for (const auto& f : *fit) {
        const json* index = obj(f, "index");
        const auto feats = f.find("features");
        if (!index || feats == f.end() || !feats->is_array())
            continue;
        const std::size_t bs = index->value("byteStart", 0);
        const std::size_t be = index->value("byteEnd", 0);
        std::string slice;
        if (be > bs && be <= s.text.size())
            slice = s.text.substr(bs, be - bs);
        for (const auto& feat : *feats) {
            const std::string type = str(feat, "$type");
            if (type == "app.bsky.richtext.facet#mention") {
                Mention m;
                m.id = str(feat, "did");
                m.acct = slice.empty() ? std::string{} : slice;
                if (!m.acct.empty() && m.acct.front() == '@')
                    m.acct.erase(m.acct.begin());
                m.username = m.acct;
                m.url = "https://bsky.app/profile/" + (m.acct.empty() ? m.id : m.acct);
                if (!m.id.empty() || !m.acct.empty())
                    s.mentions.push_back(std::move(m));
            } else if (type == "app.bsky.richtext.facet#tag") {
                std::string tag = str(feat, "tag");
                if (tag.empty() && slice.size() > 1 && slice.front() == '#')
                    tag = slice.substr(1);
                if (!tag.empty())
                    s.tags.push_back(std::move(tag));
            }
        }
    }
}

// Moderation / self labels that warrant a content warning for a blind user,
// mirroring the media blur the visual app applies to sensitive content.
bool is_sensitive_label(const std::string& v) {
    return v == "porn" || v == "sexual" || v == "nudity" || v == "graphic-media" || v == "gore" ||
           v == "corpse" || v == "self-harm" || v == "sexual-figurative";
}

// Surface a post's sensitive labels (moderator labels on the post view + the
// author's self-labels in the record) as a content warning, so it gets the same
// hide/warn treatment as a Mastodon CW. Leaves an existing CW untouched.
void apply_labels(const json& post, const json* record, Status& s) {
    std::vector<std::string> sensitive;
    auto scan = [&](const json& arr) {
        if (!arr.is_array())
            return;
        for (const auto& l : arr) {
            const std::string v = str(l, "val");
            if (is_sensitive_label(v) &&
                std::find(sensitive.begin(), sensitive.end(), v) == sensitive.end())
                sensitive.push_back(v);
        }
    };
    if (auto it = post.find("labels"); it != post.end())
        scan(*it);
    if (record)
        if (const json* self = obj(*record, "labels"))
            if (auto vals = self->find("values"); vals != self->end())
                scan(*vals);
    if (!sensitive.empty() && (!s.spoiler_text || s.spoiler_text->empty())) {
        std::string joined;
        for (size_t i = 0; i < sensitive.size(); ++i)
            joined += (i ? ", " : "") + sensitive[i];
        s.spoiler_text = "Sensitive: " + joined;
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
        map_facets(*record, s);
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
        const std::string type = str(*embed, "$type");
        if (type == "app.bsky.embed.record#view") {
            // A pure quote post.
            if (const json* rec = obj(*embed, "record"))
                s.quote = map_quote_record(*rec);
        } else if (type == "app.bsky.embed.recordWithMedia#view") {
            // Quote + media together.
            if (const json* media = obj(*embed, "media"))
                map_media_view(*media, s);
            if (const json* wrap = obj(*embed, "record"))
                if (const json* rec = obj(*wrap, "record"))
                    s.quote = map_quote_record(*rec);
        } else {
            // Images / video / external card.
            map_media_view(*embed, s);
        }
    }
    apply_labels(post, obj(post, "record"), s);
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

    // The feed carries the reply parent as a full post view (with its author),
    // so a reply row can name who it's replying to. The bare record only has
    // the parent URI, so this is the one place the handle is available.
    if (const json* reply = obj(item, "reply"))
        if (const json* parent = obj(*reply, "parent"))
            if (const json* author = obj(*parent, "author"))
                if (std::string h = str(*author, "handle"); !h.empty())
                    inner.reply_to_handle = h;

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
