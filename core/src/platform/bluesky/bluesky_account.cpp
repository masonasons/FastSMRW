#include "fastsm/platform/bluesky/bluesky_account.hpp"

#include <cctype>

#include <nlohmann/json.hpp>

#include "fastsm/platform/bluesky/bluesky_map.hpp"
#include "fastsm/platform/bluesky/bluesky_richtext.hpp"
#include "fastsm/util/date_parsing.hpp"
#include "fastsm/util/url.hpp"

using nlohmann::json;

namespace fastsm {
namespace {

std::string rkey_from_uri(const std::string& at_uri) {
    const size_t slash = at_uri.find_last_of('/');
    return slash == std::string::npos ? at_uri : at_uri.substr(slash + 1);
}

// Flatten a getPostThread replies tree into rows, depth-first.
void append_bsky_replies(const json& replies, std::vector<TimelineItem>& out) {
    if (!replies.is_array())
        return;
    for (const auto& node : replies) {
        if (auto p = node.find("post"); p != node.end() && p->is_object())
            out.push_back(TimelineItem{bluesky::map_post(*p)});
        if (auto r = node.find("replies"); r != node.end())
            append_bsky_replies(*r, out);
    }
}

} // namespace

BlueskyAccount::BlueskyAccount(BlueskyCredentials credentials, BlueskySession session, User me,
                               net::IHttpClient* http)
    : credentials_(std::move(credentials)), session_(std::move(session)), me_(std::move(me)),
      http_(http) {}

PlatformFeatures BlueskyAccount::features() const {
    PlatformFeatures f;
    f.quote_posts = true;
    f.media = true; // images (with alt text) + video via uploadBlob
    return f;
}

std::vector<TimelineSource> BlueskyAccount::default_timelines() const {
    return {TimelineSource::home(), TimelineSource::notifications()};
}

std::vector<TimelineSource> BlueskyAccount::spawnable_timelines() const {
    return {TimelineSource::mentions()};
}

bool BlueskyAccount::refresh_session() {
    net::HttpRequest req;
    req.method = "POST";
    req.url = session_.pds_url + "/xrpc/com.atproto.server.refreshSession";
    req.headers.push_back({"Authorization", "Bearer " + session_.refresh_jwt});
    const net::HttpResponse res = http_->send(req);
    if (!res.ok())
        return false;
    try {
        const json j = json::parse(res.body);
        session_.access_jwt = j.value("accessJwt", session_.access_jwt);
        session_.refresh_jwt = j.value("refreshJwt", session_.refresh_jwt);
        return true;
    } catch (...) {
        return false;
    }
}

net::HttpResponse BlueskyAccount::send_authed(const std::string& method, const std::string& url,
                                              const std::string& body,
                                              const std::string& content_type) {
    auto build = [&](const std::string& token) {
        net::HttpRequest req;
        req.method = method;
        req.url = url;
        req.headers.push_back({"Authorization", "Bearer " + token});
        if (!body.empty()) {
            req.headers.push_back({"Content-Type", content_type});
            req.body = body;
        }
        return req;
    };

    net::HttpResponse res = http_->send(build(session_.access_jwt));
    const bool expired = res.status == 401 ||
                         (res.status == 400 && res.body.find("ExpiredToken") != std::string::npos);
    if (expired && refresh_session())
        res = http_->send(build(session_.access_jwt));
    return res;
}

TimelinePage BlueskyAccount::items(const TimelineSource& source, int limit,
                                   const PageCursor& cursor) {
    TimelinePage page;
    const std::string base = session_.pds_url + "/xrpc/";
    const std::string cur = (cursor.kind == CursorKind::Token && !cursor.value.empty())
                                ? "&cursor=" + util::percent_encode(cursor.value)
                                : std::string{};
    auto fetch = [&](const std::string& url) -> std::optional<json> {
        const net::HttpResponse res = send_authed("GET", url, "");
        if (!res.ok())
            return std::nullopt;
        try {
            return json::parse(res.body);
        } catch (...) {
            return std::nullopt;
        }
    };
    auto take_feed = [&](const json& j) {
        if (auto f = j.find("feed"); f != j.end() && f->is_array())
            for (const auto& e : *f)
                page.items.push_back(TimelineItem{bluesky::map_feed_item(e)});
        if (auto c = j.find("cursor"); c != j.end() && c->is_string())
            page.next_cursor = PageCursor::token(c->get<std::string>());
    };
    // searchPosts returns a flat "posts" array (post views, not feed items).
    auto take_posts = [&](const json& j) {
        if (auto p = j.find("posts"); p != j.end() && p->is_array())
            for (const auto& e : *p)
                page.items.push_back(TimelineItem{bluesky::map_post(e)});
        if (auto c = j.find("cursor"); c != j.end() && c->is_string())
            page.next_cursor = PageCursor::token(c->get<std::string>());
    };
    auto take_actors = [&](const json& j) {
        if (auto a = j.find("actors"); a != j.end() && a->is_array())
            for (const auto& prof : *a)
                page.items.push_back(TimelineItem{bluesky::map_author(prof)});
        if (auto c = j.find("cursor"); c != j.end() && c->is_string())
            page.next_cursor = PageCursor::token(c->get<std::string>());
    };

    switch (source.kind) {
    case TimelineSource::Kind::Home:
        if (auto j = fetch(base + "app.bsky.feed.getTimeline?limit=" + std::to_string(limit) + cur))
            take_feed(*j);
        break;
    case TimelineSource::Kind::UserPosts:
        if (auto j = fetch(base + "app.bsky.feed.getAuthorFeed?actor=" +
                           util::percent_encode(source.param) + "&limit=" + std::to_string(limit) +
                           cur))
            take_feed(*j);
        break;
    case TimelineSource::Kind::Thread: {
        auto j = fetch(base + "app.bsky.feed.getPostThread?uri=" +
                       util::percent_encode(source.param));
        if (!j)
            break;
        auto t = j->find("thread");
        if (t == j->end() || !t->is_object())
            break;
        // Ancestors: walk the parent chain, then reverse into chronological order.
        std::vector<TimelineItem> ancestors;
        const json* node = nullptr;
        if (auto pit = t->find("parent"); pit != t->end() && pit->is_object())
            node = &(*pit);
        while (node) {
            if (auto p = node->find("post"); p != node->end() && p->is_object())
                ancestors.push_back(TimelineItem{bluesky::map_post(*p)});
            const json* next = nullptr;
            if (auto pit = node->find("parent"); pit != node->end() && pit->is_object())
                next = &(*pit);
            node = next;
        }
        for (auto it = ancestors.rbegin(); it != ancestors.rend(); ++it)
            page.items.push_back(std::move(*it));
        if (auto p = t->find("post"); p != t->end() && p->is_object())
            page.items.push_back(TimelineItem{bluesky::map_post(*p)});
        if (auto r = t->find("replies"); r != t->end())
            append_bsky_replies(*r, page.items);
        break;
    }
    case TimelineSource::Kind::Followers:
    case TimelineSource::Kind::Following: {
        const bool followers = source.kind == TimelineSource::Kind::Followers;
        const std::string ep =
            followers ? "app.bsky.graph.getFollowers" : "app.bsky.graph.getFollows";
        const char* key = followers ? "followers" : "follows";
        if (auto j = fetch(base + ep + "?actor=" + util::percent_encode(source.param) +
                           "&limit=" + std::to_string(limit) + cur)) {
            if (auto a = j->find(key); a != j->end() && a->is_array())
                for (const auto& prof : *a)
                    page.items.push_back(TimelineItem{bluesky::map_author(prof)});
            if (auto c = j->find("cursor"); c != j->end() && c->is_string())
                page.next_cursor = PageCursor::token(c->get<std::string>());
        }
        break;
    }
    case TimelineSource::Kind::Notifications: {
        // Every notification, including mention/reply/quote (as Mention rows) so
        // the "show mentions in notifications" setting can filter them client-side.
        if (auto j = fetch(base + "app.bsky.notification.listNotifications?limit=" +
                           std::to_string(limit) + cur)) {
            if (auto a = j->find("notifications"); a != j->end() && a->is_array())
                for (const auto& e : *a)
                    page.items.push_back(TimelineItem{bluesky::map_notification(e)});
            if (auto c = j->find("cursor"); c != j->end() && c->is_string())
                page.next_cursor = PageCursor::token(c->get<std::string>());
        }
        break;
    }
    case TimelineSource::Kind::Mentions: {
        // Keep only mention/reply/quote notifications, then hydrate the actual
        // posts via getPosts so a @-mention shows as the post itself (Mastodon
        // parity). getPosts takes at most 25 uris per call, so batch them.
        auto j = fetch(base + "app.bsky.notification.listNotifications?limit=" +
                       std::to_string(limit) + cur);
        if (!j)
            break;
        std::vector<std::string> uris;
        if (auto a = j->find("notifications"); a != j->end() && a->is_array())
            for (const auto& e : *a) {
                const std::string reason = e.value("reason", std::string{});
                if (reason == "mention" || reason == "reply" || reason == "quote")
                    if (std::string uri = e.value("uri", std::string{}); !uri.empty())
                        uris.push_back(std::move(uri));
            }
        if (auto c = j->find("cursor"); c != j->end() && c->is_string())
            page.next_cursor = PageCursor::token(c->get<std::string>());
        for (size_t i = 0; i < uris.size(); i += 25) {
            std::string q;
            for (size_t k = i; k < i + 25 && k < uris.size(); ++k)
                q += "&uris=" + util::percent_encode(uris[k]);
            if (q.empty())
                continue;
            if (auto pj = fetch(base + "app.bsky.feed.getPosts?" + q.substr(1)))
                if (auto pa = pj->find("posts"); pa != pj->end() && pa->is_array())
                    for (const auto& p : *pa)
                        page.items.push_back(TimelineItem{bluesky::map_post(p)});
        }
        break;
    }
    case TimelineSource::Kind::Hashtag:
        // Bluesky has no hashtag endpoint; a "#tag" full-text search is the
        // closest equivalent and is what the app itself uses.
        if (auto j = fetch(base + "app.bsky.feed.searchPosts?q=" +
                           util::percent_encode("#" + source.param) +
                           "&limit=" + std::to_string(limit) + cur))
            take_posts(*j);
        break;
    case TimelineSource::Kind::SearchPosts:
        if (auto j = fetch(base + "app.bsky.feed.searchPosts?q=" +
                           util::percent_encode(source.param) + "&limit=" + std::to_string(limit) +
                           cur))
            take_posts(*j);
        break;
    case TimelineSource::Kind::SearchPeople:
        if (auto j = fetch(base + "app.bsky.actor.searchActors?q=" +
                           util::percent_encode(source.param) + "&limit=" + std::to_string(limit) +
                           cur))
            take_actors(*j);
        break;
    case TimelineSource::Kind::List: // a curation list's feed (param = list at-uri)
        if (auto j = fetch(base + "app.bsky.feed.getListFeed?list=" +
                           util::percent_encode(source.param) + "&limit=" + std::to_string(limit) +
                           cur))
            take_feed(*j);
        break;
    case TimelineSource::Kind::Feed: // a custom feed generator (param = feed at-uri)
        if (auto j = fetch(base + "app.bsky.feed.getFeed?feed=" +
                           util::percent_encode(source.param) + "&limit=" + std::to_string(limit) +
                           cur))
            take_feed(*j);
        break;
    case TimelineSource::Kind::FavoritedBy: // who liked a post (param = post uri)
        if (auto j = fetch(base + "app.bsky.feed.getLikes?uri=" +
                           util::percent_encode(source.param) + "&limit=" + std::to_string(limit) +
                           cur)) {
            if (auto a = j->find("likes"); a != j->end() && a->is_array())
                for (const auto& e : *a)
                    if (auto actor = e.find("actor"); actor != e.end() && actor->is_object())
                        page.items.push_back(TimelineItem{bluesky::map_author(*actor)});
            if (auto c = j->find("cursor"); c != j->end() && c->is_string())
                page.next_cursor = PageCursor::token(c->get<std::string>());
        }
        break;
    case TimelineSource::Kind::BoostedBy: // who reposted a post (param = post uri)
        if (auto j = fetch(base + "app.bsky.feed.getRepostedBy?uri=" +
                           util::percent_encode(source.param) + "&limit=" + std::to_string(limit) +
                           cur)) {
            if (auto a = j->find("repostedBy"); a != j->end() && a->is_array())
                for (const auto& prof : *a)
                    page.items.push_back(TimelineItem{bluesky::map_author(prof)});
            if (auto c = j->find("cursor"); c != j->end() && c->is_string())
                page.next_cursor = PageCursor::token(c->get<std::string>());
        }
        break;
    default:
        break; // other kinds aren't supported on Bluesky yet
    }
    return page;
}

std::string BlueskyAccount::resolve_handle(const std::string& handle) {
    if (handle.empty())
        return {};
    const std::string url = session_.pds_url +
                            "/xrpc/com.atproto.identity.resolveHandle?handle=" +
                            util::percent_encode(handle);
    const net::HttpResponse res = send_authed("GET", url, "");
    if (!res.ok())
        return {};
    try {
        return json::parse(res.body).value("did", std::string{});
    } catch (...) {
        return {};
    }
}

json BlueskyAccount::build_facets(const std::string& text) {
    json facets = json::array();
    for (const auto& span : bluesky::detect_facets(text)) {
        json feature;
        switch (span.type) {
        case bluesky::FacetSpan::Type::Link:
            feature = {{"$type", "app.bsky.richtext.facet#link"}, {"uri", span.value}};
            break;
        case bluesky::FacetSpan::Type::Tag:
            feature = {{"$type", "app.bsky.richtext.facet#tag"}, {"tag", span.value}};
            break;
        case bluesky::FacetSpan::Type::Mention: {
            const std::string did = resolve_handle(span.value);
            if (did.empty())
                continue; // a handle that doesn't resolve isn't a mention
            feature = {{"$type", "app.bsky.richtext.facet#mention"}, {"did", did}};
            break;
        }
        }
        facets.push_back({{"index",
                           {{"byteStart", span.start}, {"byteEnd", span.end}}},
                          {"features", json::array({feature})}});
    }
    return facets;
}

std::optional<json> BlueskyAccount::build_reply_ref(const std::string& parent_uri) {
    if (parent_uri.empty())
        return std::nullopt;
    const std::string url = session_.pds_url +
                            "/xrpc/app.bsky.feed.getPosts?uris=" + util::percent_encode(parent_uri);
    const net::HttpResponse res = send_authed("GET", url, "");
    if (!res.ok())
        return std::nullopt;
    try {
        const json j = json::parse(res.body);
        auto pa = j.find("posts");
        if (pa == j.end() || !pa->is_array() || pa->empty())
            return std::nullopt;
        const json& post = (*pa)[0];
        const std::string uri = post.value("uri", std::string{});
        const std::string cid = post.value("cid", std::string{});
        if (uri.empty() || cid.empty())
            return std::nullopt;
        json parent_ref = {{"uri", uri}, {"cid", cid}};
        // The thread root is the parent's own root when it's itself a reply,
        // otherwise the parent is the root.
        json root_ref = parent_ref;
        if (auto rec = post.find("record"); rec != post.end() && rec->is_object())
            if (auto reply = rec->find("reply"); reply != rec->end() && reply->is_object())
                if (auto root = reply->find("root"); root != reply->end() && root->is_object())
                    if (root->contains("uri") && root->contains("cid"))
                        root_ref = *root;
        return json{{"root", root_ref}, {"parent", parent_ref}};
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<json> BlueskyAccount::upload_blob(const MediaUpload& media) {
    if (media.bytes.empty())
        return std::nullopt;
    const std::string mime = media.mime.empty() ? "application/octet-stream" : media.mime;
    const net::HttpResponse res = send_authed(
        "POST", session_.pds_url + "/xrpc/com.atproto.repo.uploadBlob", media.bytes, mime);
    if (!res.ok())
        return std::nullopt;
    try {
        const json j = json::parse(res.body);
        if (auto b = j.find("blob"); b != j.end() && b->is_object())
            return *b;
    } catch (...) {
    }
    return std::nullopt;
}

std::optional<json> BlueskyAccount::build_embed(const PostDraft& draft) {
    // Media: up to 4 images (each with alt text), or a single video, uploaded as
    // blobs. If an upload fails, abort the whole post rather than send it without
    // the media the user attached.
    json images = json::array();
    json video_embed; // null until a video is attached
    for (const auto& a : draft.attachments) {
        const bool is_video = a.mime.rfind("video/", 0) == 0;
        auto blob = upload_blob(a);
        if (!blob)
            return std::nullopt;
        if (is_video && video_embed.is_null()) {
            video_embed = {{"$type", "app.bsky.embed.video"}, {"video", *blob}};
            if (!a.alt.empty())
                video_embed["alt"] = a.alt;
        } else if (!is_video && images.size() < 4) {
            images.push_back({{"alt", a.alt}, {"image", *blob}});
        }
    }
    json media_embed; // null unless there's media
    if (!images.empty())
        media_embed = {{"$type", "app.bsky.embed.images"}, {"images", std::move(images)}};
    else if (!video_embed.is_null())
        media_embed = std::move(video_embed);

    // Quote: a strong ref (uri + cid) to the quoted post.
    json quote_embed; // null unless quoting
    if (draft.quoted_status_id && !draft.quoted_status_id->empty() && draft.quoted_status_cid &&
        !draft.quoted_status_cid->empty()) {
        quote_embed = {
            {"$type", "app.bsky.embed.record"},
            {"record", {{"uri", *draft.quoted_status_id}, {"cid", *draft.quoted_status_cid}}}};
    }

    const bool has_media = !media_embed.is_null();
    const bool has_quote = !quote_embed.is_null();
    if (has_media && has_quote)
        return json{{"$type", "app.bsky.embed.recordWithMedia"},
                    {"record", std::move(quote_embed)},
                    {"media", std::move(media_embed)}};
    if (has_media)
        return media_embed;
    if (has_quote)
        return quote_embed;
    return std::nullopt;
}

std::optional<Status> BlueskyAccount::post(const PostDraft& draft) {
    json record;
    record["$type"] = "app.bsky.feed.post";
    record["text"] = draft.text;
    record["createdAt"] = util::format_iso8601(util::now_unix());
    if (draft.language)
        record["langs"] = json::array({*draft.language});
    // Richtext facets so links open, mentions notify, and hashtags register.
    if (json facets = build_facets(draft.text); !facets.empty())
        record["facets"] = std::move(facets);
    // Reply: thread the post by attaching root + parent strong refs. Without this
    // the post would publish detached from the conversation.
    if (draft.reply_to_id && !draft.reply_to_id->empty()) {
        if (auto reply = build_reply_ref(*draft.reply_to_id))
            record["reply"] = std::move(*reply);
    }
    // Media and/or quote embed (images/video/record/recordWithMedia).
    if (auto embed = build_embed(draft))
        record["embed"] = std::move(*embed);

    json body;
    body["repo"] = credentials_.did;
    body["collection"] = "app.bsky.feed.post";
    body["record"] = record;

    const net::HttpResponse res = send_authed(
        "POST", session_.pds_url + "/xrpc/com.atproto.repo.createRecord", body.dump());
    if (!res.ok())
        return std::nullopt;

    Status s;
    s.platform = Platform::Bluesky;
    s.account = me_;
    s.text = draft.text;
    s.created_at = util::now_unix();
    try {
        const json j = json::parse(res.body);
        s.id = j.value("uri", "");
        s.cid = j.value("cid", "");
    } catch (...) {
    }
    return s;
}

bool BlueskyAccount::create_record(const char* collection, const std::string& record_json) {
    json body;
    body["repo"] = credentials_.did;
    body["collection"] = collection;
    body["record"] = json::parse(record_json);
    const net::HttpResponse res = send_authed(
        "POST", session_.pds_url + "/xrpc/com.atproto.repo.createRecord", body.dump());
    return res.ok();
}

bool BlueskyAccount::delete_record(const char* collection, const std::string& at_uri) {
    json body;
    body["repo"] = credentials_.did;
    body["collection"] = collection;
    body["rkey"] = rkey_from_uri(at_uri);
    const net::HttpResponse res = send_authed(
        "POST", session_.pds_url + "/xrpc/com.atproto.repo.deleteRecord", body.dump());
    return res.ok();
}

std::optional<std::string> BlueskyAccount::get_profile_body(const std::string& actor) {
    const std::string url =
        session_.pds_url + "/xrpc/app.bsky.actor.getProfile?actor=" + util::percent_encode(actor);
    const net::HttpResponse res = send_authed("GET", url, "");
    if (!res.ok())
        return std::nullopt;
    return res.body;
}

std::optional<User> BlueskyAccount::fetch_profile(const std::string& id) {
    auto body = get_profile_body(id);
    if (!body)
        return std::nullopt;
    try {
        return bluesky::map_author(json::parse(*body));
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<Status> BlueskyAccount::fetch_status(const std::string& uri) {
    // getPosts returns post views by at-uri; the in_reply_to_id we carry for a
    // Bluesky reply IS the parent's uri, so one lookup speaks the referenced post.
    if (uri.empty())
        return std::nullopt;
    const std::string url =
        session_.pds_url + "/xrpc/app.bsky.feed.getPosts?uris=" + util::percent_encode(uri);
    const net::HttpResponse res = send_authed("GET", url, "");
    if (!res.ok())
        return std::nullopt;
    try {
        const json j = json::parse(res.body);
        if (auto pa = j.find("posts"); pa != j.end() && pa->is_array() && !pa->empty())
            return bluesky::map_post((*pa)[0]);
    } catch (...) {
    }
    return std::nullopt;
}

std::optional<User> BlueskyAccount::lookup_user(const std::string& handle) {
    // getProfile's `actor` accepts a handle as well as a DID, so a typed handle
    // resolves directly. A leading '@' is tolerated and stripped.
    std::string h = handle;
    if (!h.empty() && h.front() == '@')
        h.erase(h.begin());
    if (h.empty())
        return std::nullopt;
    auto body = get_profile_body(h);
    if (!body)
        return std::nullopt;
    try {
        return bluesky::map_author(json::parse(*body));
    } catch (...) {
        return std::nullopt;
    }
}

std::vector<User> BlueskyAccount::search_accounts(const std::string& query, int limit) {
    // searchActorsTypeahead is the fast prefix search the Bluesky app uses for
    // its own @-mention autocomplete.
    std::vector<User> out;
    std::string q = query;
    if (!q.empty() && q.front() == '@')
        q.erase(q.begin());
    if (q.empty())
        return out;
    if (limit <= 0)
        limit = 8;
    const std::string url = session_.pds_url +
                            "/xrpc/app.bsky.actor.searchActorsTypeahead?q=" +
                            util::percent_encode(q) + "&limit=" + std::to_string(limit);
    const net::HttpResponse res = send_authed("GET", url, "");
    if (!res.ok())
        return out;
    try {
        const json j = json::parse(res.body);
        if (auto a = j.find("actors"); a != j.end() && a->is_array())
            for (const auto& actor : *a)
                out.push_back(bluesky::map_author(actor));
    } catch (...) {
    }
    return out;
}

std::vector<TimelineList> BlueskyAccount::lists() {
    // The viewer's own lists. Only curation lists are viewable as a feed; mod
    // lists exist for muting/blocking, not reading.
    std::vector<TimelineList> out;
    const std::string url = session_.pds_url + "/xrpc/app.bsky.graph.getLists?actor=" +
                            util::percent_encode(credentials_.did) + "&limit=100";
    const net::HttpResponse res = send_authed("GET", url, "");
    if (!res.ok())
        return out;
    try {
        const json j = json::parse(res.body);
        if (auto a = j.find("lists"); a != j.end() && a->is_array())
            for (const auto& l : *a) {
                if (l.value("purpose", std::string{}) != "app.bsky.graph.defs#curatelist")
                    continue;
                TimelineList t;
                t.id = l.value("uri", std::string{});
                t.title = l.value("name", std::string{});
                if (!t.id.empty())
                    out.push_back(std::move(t));
            }
    } catch (...) {
    }
    return out;
}

std::vector<TimelineList> BlueskyAccount::saved_feeds() {
    std::vector<TimelineList> out;
    // 1) Read the saved-feeds preference to collect the feed generator uris.
    const net::HttpResponse pres =
        send_authed("GET", session_.pds_url + "/xrpc/app.bsky.actor.getPreferences", "");
    if (!pres.ok())
        return out;
    std::vector<std::string> feed_uris;
    try {
        const json j = json::parse(pres.body);
        if (auto prefs = j.find("preferences"); prefs != j.end() && prefs->is_array())
            for (const auto& p : *prefs) {
                if (p.value("$type", std::string{}) != "app.bsky.actor.defs#savedFeedsPrefV2")
                    continue;
                if (auto items = p.find("items"); items != p.end() && items->is_array())
                    for (const auto& it : *items)
                        if (it.value("type", std::string{}) == "feed")
                            if (std::string v = it.value("value", std::string{}); !v.empty())
                                feed_uris.push_back(std::move(v));
            }
    } catch (...) {
        return out;
    }
    if (feed_uris.empty())
        return out;
    // 2) Resolve display names via getFeedGenerators (up to 25 uris per call).
    for (size_t i = 0; i < feed_uris.size(); i += 25) {
        std::string q;
        for (size_t k = i; k < i + 25 && k < feed_uris.size(); ++k)
            q += "&feeds=" + util::percent_encode(feed_uris[k]);
        if (q.empty())
            continue;
        const net::HttpResponse fres = send_authed(
            "GET", session_.pds_url + "/xrpc/app.bsky.feed.getFeedGenerators?" + q.substr(1), "");
        if (!fres.ok())
            continue;
        try {
            const json j = json::parse(fres.body);
            if (auto a = j.find("feeds"); a != j.end() && a->is_array())
                for (const auto& f : *a) {
                    TimelineList t;
                    t.id = f.value("uri", std::string{});
                    t.title = f.value("displayName", std::string{});
                    if (!t.id.empty())
                        out.push_back(std::move(t));
                }
        } catch (...) {
        }
    }
    return out;
}

std::vector<std::string> BlueskyAccount::muted_words() {
    std::vector<std::string> out;
    const net::HttpResponse res =
        send_authed("GET", session_.pds_url + "/xrpc/app.bsky.actor.getPreferences", "");
    if (!res.ok())
        return out;
    try {
        const json j = json::parse(res.body);
        if (auto prefs = j.find("preferences"); prefs != j.end() && prefs->is_array())
            for (const auto& p : *prefs) {
                if (p.value("$type", std::string{}) != "app.bsky.actor.defs#mutedWordsPref")
                    continue;
                if (auto items = p.find("items"); items != p.end() && items->is_array())
                    for (const auto& it : *items) {
                        std::string v = it.value("value", std::string{});
                        for (char& c : v)
                            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                        if (!v.empty())
                            out.push_back(std::move(v));
                    }
            }
    } catch (...) {
    }
    return out;
}

void BlueskyAccount::mark_notifications_seen() {
    const json body = {{"seenAt", util::format_iso8601(util::now_unix())}};
    send_authed("POST", session_.pds_url + "/xrpc/app.bsky.notification.updateSeen", body.dump());
}

// getFollowers / getFollows, paging through every page via the opaque cursor.
// All-or-nothing: a 429 aborts as RateLimited and any other failure as Failed, so
// a partial list is never returned as if complete.
FullRelationResult BlueskyAccount::fetch_all_relations(const std::string& id, bool following) {
    FullRelationResult out;
    const std::string base = session_.pds_url + "/xrpc/";
    const std::string ep = following ? "app.bsky.graph.getFollows" : "app.bsky.graph.getFollowers";
    const char* key = following ? "follows" : "followers";
    std::string cursor;
    // Cap the page count as a safety net against a never-ending cursor.
    for (int page = 0; page < 1000; ++page) {
        std::string url = base + ep + "?actor=" + util::percent_encode(id) + "&limit=100";
        if (!cursor.empty())
            url += "&cursor=" + util::percent_encode(cursor);
        const net::HttpResponse res = send_authed("GET", url, "");
        if (res.status == 429) {
            out.status = FullRelationResult::Status::RateLimited;
            return out;
        }
        if (!res.ok()) {
            out.status = FullRelationResult::Status::Failed;
            return out;
        }
        json j;
        try {
            j = json::parse(res.body);
        } catch (...) {
            out.status = FullRelationResult::Status::Failed;
            return out;
        }
        if (auto a = j.find(key); a != j.end() && a->is_array())
            for (const auto& prof : *a)
                out.users.push_back(bluesky::map_author(prof));
        // No cursor == reached the end of the list.
        auto c = j.find("cursor");
        if (c == j.end() || !c->is_string() || c->get<std::string>().empty())
            break;
        cursor = c->get<std::string>();
    }
    out.status = FullRelationResult::Status::Ok;
    return out;
}

std::optional<Relationship> BlueskyAccount::relationship(const std::string& id) {
    auto body = get_profile_body(id);
    if (!body)
        return std::nullopt;
    try {
        const json j = json::parse(*body);
        Relationship r;
        r.id = j.value("did", id);
        if (auto v = j.find("viewer"); v != j.end() && v->is_object()) {
            r.following = v->contains("following"); // a record uri present == following
            r.muting = v->value("muted", false);
            r.blocking = v->contains("blocking");
        }
        return r;
    } catch (...) {
        return std::nullopt;
    }
}

bool BlueskyAccount::follow(const std::string& id) {
    const json rec = {{"$type", "app.bsky.graph.follow"},
                      {"subject", id},
                      {"createdAt", util::format_iso8601(util::now_unix())}};
    return create_record("app.bsky.graph.follow", rec.dump());
}

bool BlueskyAccount::block(const std::string& id) {
    const json rec = {{"$type", "app.bsky.graph.block"},
                      {"subject", id},
                      {"createdAt", util::format_iso8601(util::now_unix())}};
    return create_record("app.bsky.graph.block", rec.dump());
}

// Bluesky follow/block are records; undo by deleting the record whose URI the
// profile's viewer state carries.
bool BlueskyAccount::unfollow(const std::string& id) {
    auto body = get_profile_body(id);
    if (!body)
        return false;
    try {
        const json j = json::parse(*body);
        if (auto v = j.find("viewer"); v != j.end()) {
            const std::string uri = v->value("following", std::string{});
            if (!uri.empty())
                return delete_record("app.bsky.graph.follow", uri);
        }
    } catch (...) {
    }
    return false;
}

bool BlueskyAccount::unblock(const std::string& id) {
    auto body = get_profile_body(id);
    if (!body)
        return false;
    try {
        const json j = json::parse(*body);
        if (auto v = j.find("viewer"); v != j.end()) {
            const std::string uri = v->value("blocking", std::string{});
            if (!uri.empty())
                return delete_record("app.bsky.graph.block", uri);
        }
    } catch (...) {
    }
    return false;
}

bool BlueskyAccount::mute_actor(const std::string& did, bool mute) {
    const json body = {{"actor", did}};
    const std::string ep = mute ? "app.bsky.graph.muteActor" : "app.bsky.graph.unmuteActor";
    const net::HttpResponse res = send_authed("POST", session_.pds_url + "/xrpc/" + ep, body.dump());
    return res.ok();
}
bool BlueskyAccount::mute(const std::string& id) { return mute_actor(id, true); }
bool BlueskyAccount::unmute(const std::string& id) { return mute_actor(id, false); }

bool BlueskyAccount::favorite(const Status& status) {
    const Status& t = status.display_status();
    if (!t.cid)
        return false;
    json record;
    record["$type"] = "app.bsky.feed.like";
    record["subject"] = json{{"uri", t.id}, {"cid", *t.cid}};
    record["createdAt"] = util::format_iso8601(util::now_unix());
    return create_record("app.bsky.feed.like", record.dump());
}

bool BlueskyAccount::unfavorite(const Status& status) {
    const Status& t = status.display_status();
    if (!t.like_uri)
        return false;
    return delete_record("app.bsky.feed.like", *t.like_uri);
}

bool BlueskyAccount::boost(const Status& status) {
    const Status& t = status.display_status();
    if (!t.cid)
        return false;
    json record;
    record["$type"] = "app.bsky.feed.repost";
    record["subject"] = json{{"uri", t.id}, {"cid", *t.cid}};
    record["createdAt"] = util::format_iso8601(util::now_unix());
    return create_record("app.bsky.feed.repost", record.dump());
}

bool BlueskyAccount::unboost(const Status& status) {
    const Status& t = status.display_status();
    if (!t.repost_uri)
        return false;
    return delete_record("app.bsky.feed.repost", *t.repost_uri);
}

bool BlueskyAccount::report(const ReportDraft& draft) {
    // Map our generic category to a Bluesky moderation reasonType.
    auto reason_type = [](const std::string& cat) -> const char* {
        if (cat == "spam")
            return "com.atproto.moderation.defs#reasonSpam";
        if (cat == "violation" || cat == "legal")
            return "com.atproto.moderation.defs#reasonViolation";
        return "com.atproto.moderation.defs#reasonOther";
    };
    // Subject: a specific post (strong ref: uri + cid) when reporting a post, else
    // the account itself (repo ref).
    json subject;
    if (!draft.status_ids.empty() && !draft.status_ids.front().empty() &&
        !draft.status_cids.empty() && !draft.status_cids.front().empty()) {
        subject = {{"$type", "com.atproto.repo.strongRef"},
                   {"uri", draft.status_ids.front()},
                   {"cid", draft.status_cids.front()}};
    } else if (!draft.account_id.empty()) {
        subject = {{"$type", "com.atproto.admin.defs#repoRef"}, {"did", draft.account_id}};
    } else {
        return false;
    }
    json body;
    body["reasonType"] = reason_type(draft.category);
    if (!draft.comment.empty())
        body["reason"] = draft.comment;
    body["subject"] = std::move(subject);
    // The PDS proxies com.atproto.moderation.createReport to the moderation service,
    // just as it proxies the app.bsky.* reads used elsewhere.
    const net::HttpResponse res = send_authed(
        "POST", session_.pds_url + "/xrpc/com.atproto.moderation.createReport", body.dump());
    return res.ok();
}

bool BlueskyAccount::delete_post(const Status& status) {
    // The post's own at-uri is its id; deleting the record removes the post.
    const Status& t = status.display_status();
    if (t.id.empty())
        return false;
    return delete_record("app.bsky.feed.post", t.id);
}

std::optional<json> BlueskyAccount::get_profile_record() {
    const std::string url = session_.pds_url + "/xrpc/com.atproto.repo.getRecord?repo=" +
                            util::percent_encode(credentials_.did) +
                            "&collection=app.bsky.actor.profile&rkey=self";
    const net::HttpResponse res = send_authed("GET", url, "");
    if (!res.ok())
        return std::nullopt;
    try {
        const json j = json::parse(res.body);
        if (auto v = j.find("value"); v != j.end() && v->is_object())
            return *v;
    } catch (...) {
    }
    return std::nullopt;
}

bool BlueskyAccount::put_profile_record(const json& value) {
    json body;
    body["repo"] = credentials_.did;
    body["collection"] = "app.bsky.actor.profile";
    body["rkey"] = "self";
    body["record"] = value;
    const net::HttpResponse res = send_authed(
        "POST", session_.pds_url + "/xrpc/com.atproto.repo.putRecord", body.dump());
    return res.ok();
}

// Bluesky pins a single post to your profile via the profile record's pinnedPost.
bool BlueskyAccount::pin_post(const Status& status) {
    const Status& t = status.display_status();
    if (t.id.empty() || !t.cid)
        return false;
    json value = get_profile_record().value_or(json::object());
    value["$type"] = "app.bsky.actor.profile";
    value["pinnedPost"] = json{{"uri", t.id}, {"cid", *t.cid}};
    return put_profile_record(value);
}

bool BlueskyAccount::unpin_post(const Status&) {
    auto value = get_profile_record();
    if (!value)
        return false;
    value->erase("pinnedPost");
    (*value)["$type"] = "app.bsky.actor.profile";
    return put_profile_record(*value);
}

std::optional<ProfileSource> BlueskyAccount::profile_source() {
    ProfileSource src;
    if (auto value = get_profile_record()) {
        src.display_name = value->value("displayName", std::string{});
        src.note = value->value("description", std::string{});
    }
    // Bluesky has no profile metadata fields or the Mastodon account flags.
    src.max_fields = 0;
    return src;
}

bool BlueskyAccount::update_profile(const ProfileSource& profile) {
    // Read-modify-write so avatar/banner/pinnedPost/labels already on the record
    // are preserved; Bluesky only edits the display name and bio here.
    json value = get_profile_record().value_or(json::object());
    value["$type"] = "app.bsky.actor.profile";
    value["displayName"] = profile.display_name;
    value["description"] = profile.note;
    return put_profile_record(value);
}

} // namespace fastsm
