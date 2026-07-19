#include "fastsm/platform/mastodon/mastodon_map.hpp"

#include <memory>
#include <string>
#include <unordered_map>

#include "fastsm/util/date_parsing.hpp"
#include "fastsm/util/html_stripper.hpp"
#include "fastsm/util/quote_text.hpp"

using nlohmann::json;

namespace fastsm::mastodon {
namespace {

std::string str(const json& j, const char* key) {
    auto it = j.find(key);
    if (it != j.end() && it->is_string())
        return it->get<std::string>();
    return {};
}

std::int64_t date(const json& j, const char* key) {
    auto it = j.find(key);
    if (it != j.end() && it->is_string())
        return util::parse_iso8601(it->get<std::string>()).value_or(0);
    return 0;
}

// Null-safe number/bool readers. nlohmann's json::value(key, default) throws
// type_error.302 when the key is present but null — and some fediverse servers
// (Pleroma, Akkoma, GoToSocial, Sharkey, …) return null where Mastodon returns a
// count or flag. Check the type first, like str()/date() above, so a single odd
// account can't throw out of the mapper and crash the client.
int num(const json& j, const char* key, int fallback = 0) {
    auto it = j.find(key);
    return (it != j.end() && it->is_number()) ? it->get<int>() : fallback;
}

bool boolean(const json& j, const char* key, bool fallback = false) {
    auto it = j.find(key);
    return (it != j.end() && it->is_boolean()) ? it->get<bool>() : fallback;
}

MediaAttachment map_media(const json& j) {
    MediaAttachment m;
    m.id = str(j, "id");
    const std::string type = str(j, "type");
    if (type == "image")
        m.type = MediaAttachment::Kind::Image;
    else if (type == "video")
        m.type = MediaAttachment::Kind::Video;
    else if (type == "gifv")
        m.type = MediaAttachment::Kind::Gifv;
    else if (type == "audio")
        m.type = MediaAttachment::Kind::Audio;
    else
        m.type = MediaAttachment::Kind::Unknown;
    m.url = str(j, "url");
    m.preview_url = str(j, "preview_url");
    m.description = str(j, "description");
    return m;
}

Notification::Kind notif_kind(const std::string& t) {
    if (t == "follow")
        return Notification::Kind::Follow;
    if (t == "follow_request")
        return Notification::Kind::FollowRequest;
    if (t == "favourite")
        return Notification::Kind::Favourite;
    if (t == "reblog")
        return Notification::Kind::Reblog;
    if (t == "mention")
        return Notification::Kind::Mention;
    if (t == "poll")
        return Notification::Kind::Poll;
    if (t == "status")
        return Notification::Kind::Status;
    if (t == "update")
        return Notification::Kind::Update;
    return Notification::Kind::Unknown;
}

} // namespace

Poll map_poll(const json& j) {
    Poll p;
    p.id = str(j, "id");
    p.expires_at = date(j, "expires_at");
    p.expired = boolean(j, "expired");
    p.multiple = boolean(j, "multiple");
    p.votes_count = num(j, "votes_count");
    p.voters_count = num(j, "voters_count");
    p.voted = boolean(j, "voted");
    if (auto it = j.find("own_votes"); it != j.end() && it->is_array())
        for (const auto& v : *it)
            if (v.is_number_integer())
                p.own_votes.push_back(v.get<int>());
    if (auto it = j.find("options"); it != j.end() && it->is_array()) {
        for (const auto& o : *it)
            p.options.push_back({str(o, "title"), num(o, "votes_count")});
    }
    return p;
}

User map_user(const json& j) {
    User u;
    u.platform = Platform::Mastodon;
    u.id = str(j, "id");
    u.acct = str(j, "acct");
    u.username = str(j, "username");
    u.display_name = str(j, "display_name");
    u.note = str(j, "note");
    u.avatar_url = str(j, "avatar");
    u.header_url = str(j, "header");
    u.url = str(j, "url");
    u.followers_count = num(j, "followers_count");
    u.following_count = num(j, "following_count");
    u.statuses_count = num(j, "statuses_count");
    u.created_at = date(j, "created_at");
    u.bot = boolean(j, "bot");
    u.locked = boolean(j, "locked");
    return u;
}

Status map_status(const json& j) {
    Status s;
    s.platform = Platform::Mastodon;
    s.id = str(j, "id");
    s.url = str(j, "url");
    if (auto it = j.find("account"); it != j.end() && it->is_object())
        s.account = map_user(*it);
    s.content = str(j, "content");
    s.text = util::strip_html(s.content);
    s.created_at = date(j, "created_at");
    s.favourites_count = num(j, "favourites_count");
    s.boosts_count = num(j, "reblogs_count");
    s.replies_count = num(j, "replies_count");
    s.favourited = boolean(j, "favourited");
    s.boosted = boolean(j, "reblogged");
    s.muted = boolean(j, "muted"); // conversation muted for the viewer
    s.pinned = boolean(j, "pinned");

    if (std::string v = str(j, "in_reply_to_id"); !v.empty())
        s.in_reply_to_id = v;
    if (std::string v = str(j, "in_reply_to_account_id"); !v.empty())
        s.in_reply_to_account_id = v;
    if (std::string v = str(j, "spoiler_text"); !v.empty())
        s.spoiler_text = v;
    if (std::string v = str(j, "visibility"); !v.empty())
        s.visibility = visibility_from_tag(v);

    if (auto it = j.find("application"); it != j.end() && it->is_object()) {
        if (std::string name = str(*it, "name"); !name.empty())
            s.application_name = name;
    }
    // Server-side filters that matched this status (Mastodon /api/v2/filters).
    if (auto it = j.find("filtered"); it != j.end() && it->is_array()) {
        for (const auto& fr : *it) {
            auto f = fr.find("filter");
            if (f == fr.end() || !f->is_object())
                continue;
            StatusFilterMatch m;
            m.title = str(*f, "title");
            m.hide = str(*f, "filter_action") == "hide";
            s.filtered.push_back(std::move(m));
        }
    }
    if (auto it = j.find("media_attachments"); it != j.end() && it->is_array()) {
        for (const auto& m : *it)
            s.media_attachments.push_back(map_media(m));
    }
    if (auto it = j.find("mentions"); it != j.end() && it->is_array()) {
        for (const auto& m : *it)
            s.mentions.push_back({str(m, "id"), str(m, "acct"), str(m, "username"), str(m, "url")});
    }
    if (auto it = j.find("tags"); it != j.end() && it->is_array()) {
        for (const auto& t : *it)
            if (std::string name = str(t, "name"); !name.empty())
                s.tags.push_back(std::move(name)); // hashtag names (server returns them without '#')
    }
    if (auto it = j.find("card"); it != j.end() && it->is_object()) {
        Card c;
        c.url = str(*it, "url");
        c.title = str(*it, "title");
        c.description = str(*it, "description");
        c.image_url = str(*it, "image");
        s.card = c;
    }
    if (auto it = j.find("poll"); it != j.end() && it->is_object())
        s.poll = map_poll(*it);
    if (auto it = j.find("reblog"); it != j.end() && it->is_object())
        s.reblog = std::make_shared<Status>(map_status(*it));
    // Quote posts. Mastodon 4.4 wraps the quote as { state, quoted_status: Status }
    // (only "accepted" quotes carry a populated quoted_status). Some servers embed
    // the quoted Status object directly under "quote". Accept both, and a
    // top-level "quoted_status" as a last resort.
    if (auto it = j.find("quote"); it != j.end() && it->is_object()) {
        const json& q = *it;
        if (auto qs = q.find("quoted_status"); qs != q.end() && qs->is_object())
            s.quote = std::make_shared<Status>(map_status(*qs));
        else if (q.contains("id") && q.contains("account")) // Status embedded directly
            s.quote = std::make_shared<Status>(map_status(q));
    } else if (auto it = j.find("quoted_status"); it != j.end() && it->is_object()) {
        s.quote = std::make_shared<Status>(map_status(*it));
    }
    // The quote is presented on its own, so drop its URL from this post's text.
    if (s.quote)
        s.text = util::strip_quote_url(s.text, s.quote->url);
    return s;
}

void mark_remote(Status& s, const std::string& base, const std::string& domain) {
    s.instance_url = base;
    // On a remote instance's own feed, its local authors' handles come back
    // bare (no domain); qualify them so they read as "user@domain".
    if (!s.account.acct.empty() && s.account.acct.find('@') == std::string::npos && !domain.empty())
        s.account.acct += "@" + domain;
    // Some servers omit `url`; synthesize the canonical one so interactions can
    // resolve the post on the user's own instance.
    if (s.url.empty() && !s.account.username.empty() && !s.id.empty())
        s.url = base + "/@" + s.account.username + "/" + s.id;
    if (s.reblog)
        mark_remote(*s.reblog, base, domain); // the boosted post is the action target
}

Notification map_notification(const json& j) {
    Notification n;
    n.platform = Platform::Mastodon;
    n.id = str(j, "id");
    n.type = notif_kind(str(j, "type"));
    if (auto it = j.find("account"); it != j.end() && it->is_object())
        n.account = map_user(*it);
    n.created_at = date(j, "created_at");
    if (auto it = j.find("status"); it != j.end() && it->is_object())
        n.status = std::make_shared<Status>(map_status(*it));
    n.group_key = str(j, "group_key"); // 4.3+: lets a streamed one merge into its group
    return n;
}

Notification map_notification_group(const json& group,
                                    const std::unordered_map<std::string, const json*>& accounts,
                                    const std::unordered_map<std::string, const json*>& statuses) {
    Notification n;
    n.platform = Platform::Mastodon;
    n.group_key = str(group, "group_key");
    // The group_key is a stable, unique string per group — use it as the row id.
    // (most_recent_notification_id is a JSON *number* and changes as actors arrive,
    // so it makes a poor id; pagination uses the Link header / page_min_id instead.)
    n.id = n.group_key;
    n.type = notif_kind(str(group, "type"));
    n.notifications_count = num(group, "notifications_count", 1);
    n.created_at = date(group, "latest_page_notification_at");
    // sample_account_ids are newest-first; the first is the actor we name ("A").
    if (auto it = group.find("sample_account_ids"); it != group.end() && it->is_array() &&
                                                    !it->empty()) {
        if (auto a = accounts.find(it->front().get<std::string>()); a != accounts.end())
            n.account = map_user(*a->second);
    }
    if (auto it = group.find("status_id"); it != group.end() && it->is_string()) {
        if (auto s = statuses.find(it->get<std::string>()); s != statuses.end())
            n.status = std::make_shared<Status>(map_status(*s->second));
    }
    if (n.created_at == 0 && n.status) // no page timestamp -> at least not epoch 1970
        n.created_at = n.status->display_status().created_at;
    return n;
}

} // namespace fastsm::mastodon
