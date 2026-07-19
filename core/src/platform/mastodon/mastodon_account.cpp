#include "fastsm/platform/mastodon/mastodon_account.hpp"

#include <algorithm>
#include <chrono>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include <nlohmann/json.hpp>

#include "fastsm/platform/mastodon/mastodon_map.hpp"
#include "fastsm/presentation/status_presenter.hpp"
#include "fastsm/util/date_parsing.hpp"
#include "fastsm/util/log.hpp"
#include "fastsm/util/url.hpp"

using nlohmann::json;

namespace fastsm {
namespace {

// Mastodon paginates some endpoints (bookmarks/favourites, ...) via the Link
// header: Link: <https://host/api/v1/bookmarks?max_id=109>; rel="next", <...>.
// Return the rel="next" link's max_id, if present.
std::optional<std::string> parse_next_max_id(const std::string& link) {
    size_t pos = 0;
    while (true) {
        const size_t lt = link.find('<', pos);
        if (lt == std::string::npos)
            break;
        const size_t gt = link.find('>', lt);
        if (gt == std::string::npos)
            break;
        const std::string url = link.substr(lt + 1, gt - lt - 1);
        const size_t comma = link.find(',', gt);
        const std::string params =
            link.substr(gt, (comma == std::string::npos ? link.size() : comma) - gt);
        if (params.find("rel=\"next\"") != std::string::npos) {
            if (const size_t m = url.find("max_id="); m != std::string::npos) {
                const size_t start = m + 7;
                const size_t end = url.find('&', start);
                return url.substr(start, (end == std::string::npos ? url.size() : end) - start);
            }
        }
        if (comma == std::string::npos)
            break;
        pos = comma + 1;
    }
    return std::nullopt;
}

} // namespace

MastodonAccount::MastodonAccount(MastodonCredentials credentials, User me, net::IHttpClient* http,
                                 int max_chars)
    : credentials_(std::move(credentials)), me_(std::move(me)), http_(http),
      max_chars_(max_chars) {}

PlatformFeatures MastodonAccount::features() const {
    PlatformFeatures f;
    f.visibility = true;
    f.content_warning = true;
    f.quote_posts = true; // Mastodon 4.4+ (older servers just ignore the param)
    f.polls = true;
    f.editing = true;
    f.scheduling = true;
    f.hide_boosts = true;
    f.media = true;
    f.follow_hashtags = true;
    f.mute_conversations = true;
    return f;
}

std::vector<TimelineSource> MastodonAccount::default_timelines() const {
    return {TimelineSource::home(), TimelineSource::notifications()};
}

std::vector<TimelineSource> MastodonAccount::spawnable_timelines() const {
    return {TimelineSource::local(),     TimelineSource::federated(),
            TimelineSource::mentions(),  TimelineSource::bookmarks(),
            TimelineSource::favorites(), TimelineSource::trends(),
            TimelineSource::conversations()};
}

void MastodonAccount::load_configuration() {
    // Pull the instance's real maximum post length. Mastodon 4 exposes it at
    // /api/v2/instance (configuration.statuses.max_characters); older servers at
    // /api/v1/instance. Leave the default (500) if neither reports it.
    for (const char* path : {"/api/v2/instance", "/api/v1/instance"}) {
        std::string body;
        long status = 0;
        if (!request("GET", credentials_.instance_url + path, "", "", body, status))
            continue;
        const json j = json::parse(body, nullptr, false);
        if (j.is_discarded())
            continue;
        const int max =
            j.value("configuration", json::object()).value("statuses", json::object()).value("max_characters", 0);
        if (max > 0) {
            max_chars_ = max;
            return;
        }
    }
}

bool MastodonAccount::request(const std::string& method, const std::string& url,
                              const std::string& body, const std::string& content_type,
                              std::string& out_body, long& out_status) {
    net::HttpRequest req;
    req.method = method;
    req.url = url;
    req.headers.push_back({"Authorization", "Bearer " + credentials_.access_token});
    if (!body.empty()) {
        req.headers.push_back({"Content-Type", content_type});
        req.body = body;
    }
    const net::HttpResponse res = http_->send(req);
    out_status = res.status;
    out_body = res.body;
    return res.ok();
}

TimelinePage MastodonAccount::items(const TimelineSource& source, int limit,
                                    const PageCursor& cursor) {
    TimelinePage page;

    // A thread: the focused status plus its ancestors and descendants, in
    // conversation order. Fetched whole (not paginated).
    if (source.kind == TimelineSource::Kind::Thread) {
        // The focused status plus its ancestors and descendants (the local
        // instance's view of the conversation), in order. Matches the Mac.
        std::unordered_set<std::string> present; // ids already in the thread (dedup)

        // Fetch one status + its /context and append the new rows in
        // ancestors -> focused -> descendants order. Returns the focused status
        // (so the caller can inspect its links), or an empty Status on failure.
        auto append_thread = [&](const std::string& status_id) -> Status {
            const std::string base = credentials_.instance_url + "/api/v1/statuses/" + status_id;
            std::string body;
            long st = 0;
            Status focus;
            if (request("GET", base, "", "", body, st)) {
                try {
                    focus = mastodon::map_status(json::parse(body));
                } catch (...) {
                }
            }
            auto add = [&](Status s) {
                if (s.id.empty() || !present.insert(s.id).second)
                    return; // empty or already folded in
                page.items.push_back(TimelineItem{std::move(s)});
            };
            std::string cbody;
            if (request("GET", base + "/context", "", "", cbody, st)) {
                try {
                    json ctx = json::parse(cbody);
                    if (auto a = ctx.find("ancestors"); a != ctx.end() && a->is_array())
                        for (const auto& s : *a)
                            add(mastodon::map_status(s));
                    add(focus);
                    if (auto d = ctx.find("descendants"); d != ctx.end() && d->is_array())
                        for (const auto& s : *d)
                            add(mastodon::map_status(s));
                } catch (...) {
                }
            } else {
                add(focus); // no context available; still show the post itself
            }
            return focus;
        };

        const Status root = append_thread(source.param);

        // Fold in posts the focused post references — its native quote and any
        // fediverse post it links to in text — merging each one's conversation
        // into this thread (deduped). Bounded so a reference-heavy post can't fan
        // out into many network calls.
        int folded = 0;
        auto fold = [&](const std::string& id) {
            if (id.empty() || present.count(id) || folded >= 4)
                return;
            append_thread(id);
            ++folded;
        };
        if (root.quote) { // the quoted post's conversation
            std::string qid = root.quote->id;
            if (root.quote->instance_url && !root.quote->url.empty())
                if (auto local = resolve_url(root.quote->url))
                    qid = *local; // a remote quote -> its local copy
            fold(qid);
        }
        int attempts = 0;
        for (const auto& url : present::post_text_link_urls(root)) {
            if (attempts >= 4 || folded >= 4)
                break;
            ++attempts;
            if (const std::optional<std::string> local = resolve_url(url))
                fold(*local); // a fediverse post on this instance -> fold it in
        }
        return page;
    }

    // Search (posts or people) uses /api/v2/search, which returns an object with
    // accounts/statuses arrays rather than a plain list. Top results only.
    if (source.kind == TimelineSource::Kind::SearchPosts ||
        source.kind == TimelineSource::Kind::SearchPeople) {
        const bool people = source.kind == TimelineSource::Kind::SearchPeople;
        const std::string url = credentials_.instance_url + "/api/v2/search?q=" +
                                util::percent_encode(source.param) +
                                "&type=" + (people ? "accounts" : "statuses") +
                                "&limit=" + std::to_string(limit) + "&resolve=true";
        net::HttpRequest req;
        req.method = "GET";
        req.url = url;
        req.headers.push_back({"Authorization", "Bearer " + credentials_.access_token});
        const net::HttpResponse res = http_->send(req);
        if (!res.ok())
            return page;
        try {
            json j = json::parse(res.body);
            if (people)
                for (const auto& a : j.value("accounts", json::array()))
                    page.items.push_back(TimelineItem{mastodon::map_user(a)});
            else
                for (const auto& s : j.value("statuses", json::array()))
                    page.items.push_back(TimelineItem{mastodon::map_status(s)});
        } catch (...) {
        }
        return page;
    }

    // Trending posts: /api/v1/trends/statuses returns a plain array ordered by
    // trend score. It paginates by offset, but a single top page is plenty for a
    // "what's hot right now" tab, so (like Search) we return the top results only.
    if (source.kind == TimelineSource::Kind::Trends) {
        const std::string url = credentials_.instance_url +
                                "/api/v1/trends/statuses?limit=" + std::to_string(limit);
        net::HttpRequest req;
        req.method = "GET";
        req.url = url;
        req.headers.push_back({"Authorization", "Bearer " + credentials_.access_token});
        const net::HttpResponse res = http_->send(req);
        if (!res.ok())
            return page;
        try {
            const json j = json::parse(res.body);
            if (j.is_array())
                for (const auto& s : j)
                    page.items.push_back(TimelineItem{mastodon::map_status(s)});
        } catch (...) {
        }
        return page;
    }

    // Remote timelines: fetched unauthenticated from a foreign instance, then
    // tagged with that instance so interactions resolve to a local copy first.
    if (source.kind == TimelineSource::Kind::RemoteLocal ||
        source.kind == TimelineSource::Kind::RemoteUser) {
        const bool user = source.kind == TimelineSource::Kind::RemoteUser;
        std::string domain = source.param;
        std::string username;
        if (user) {
            const size_t at = source.param.find('@');
            if (at == std::string::npos || at == 0)
                return page; // malformed "user@instance"
            username = source.param.substr(0, at);
            domain = source.param.substr(at + 1);
        }
        if (domain.empty())
            return page;
        const std::string base = "https://" + domain;

        std::string rpath;
        if (user) {
            const std::string acct_id = remote_account_id(base, username);
            if (acct_id.empty())
                return page; // couldn't find the user on that instance
            rpath = "/api/v1/accounts/" + acct_id + "/statuses";
        } else {
            rpath = "/api/v1/timelines/public";
        }
        std::string rurl = base + rpath + "?limit=" + std::to_string(limit);
        if (!user)
            rurl += "&local=true";
        if (cursor.kind == CursorKind::MaxID)
            rurl += "&max_id=" + cursor.value;

        net::HttpRequest req;
        req.method = "GET";
        req.url = rurl; // unauthenticated: no Authorization header
        const net::HttpResponse res = http_->send(req);
        if (!res.ok())
            return page;
        json j;
        try {
            j = json::parse(res.body);
        } catch (...) {
            return page;
        }
        if (!j.is_array())
            return page;
        std::string last_id;
        for (const auto& entry : j) {
            Status s = mastodon::map_status(entry);
            last_id = s.id;
            mastodon::mark_remote(s, base, domain);
            page.items.push_back(TimelineItem{std::move(s)});
        }
        if (!last_id.empty())
            page.next_cursor = PageCursor::max_id(last_id);
        return page;
    }

    // Grouped notifications (Mastodon 4.3+): one row per group so favourites/boosts
    // collapse into "A and N others …". Mentions stays on v1 below (each mention is
    // its own post row and shouldn't be grouped). Falls through to v1 if the instance
    // is too old or the response isn't the grouped shape.
    if (source.kind == TimelineSource::Kind::Notifications && !grouped_notifs_unsupported_) {
        std::string url =
            credentials_.instance_url + "/api/v2/notifications?limit=" + std::to_string(limit);
        if (cursor.kind == CursorKind::MaxID)
            url += "&max_id=" + cursor.value;
        net::HttpRequest req;
        req.method = "GET";
        req.url = url;
        req.headers.push_back({"Authorization", "Bearer " + credentials_.access_token});
        const net::HttpResponse res = http_->send(req);
        if (res.status == 404) {
            grouped_notifs_unsupported_ = true; // pre-4.3 instance: use v1 from now on
        } else if (res.ok()) {
            try {
                const json j = json::parse(res.body);
                if (j.is_object() && j.contains("notification_groups")) {
                    // Side-loaded accounts/statuses, indexed by id for the group mapper.
                    std::unordered_map<std::string, const json*> accounts, statuses;
                    if (auto a = j.find("accounts"); a != j.end() && a->is_array())
                        for (const auto& e : *a)
                            accounts[e.value("id", std::string{})] = &e;
                    if (auto s = j.find("statuses"); s != j.end() && s->is_array())
                        for (const auto& e : *s)
                            statuses[e.value("id", std::string{})] = &e;
                    std::string page_min; // oldest notification id on this page (a string)
                    for (const auto& g : j["notification_groups"]) {
                        if (auto it = g.find("page_min_id"); it != g.end() && it->is_string())
                            page_min = it->get<std::string>();
                        page.items.push_back(
                            TimelineItem{mastodon::map_notification_group(g, accounts, statuses)});
                    }
                    // Page by the Link header's rel="next" max_id; fall back to the
                    // oldest notification id on this page (NOT the group_key row id).
                    if (auto link = res.header("Link"))
                        if (auto next = parse_next_max_id(*link))
                            page.next_cursor = PageCursor::max_id(*next);
                    if (!page.next_cursor && !page_min.empty())
                        page.next_cursor = PageCursor::max_id(page_min);
                    return page;
                }
            } catch (...) {
            }
        }
        // Otherwise fall through to the v1 notifications path below (best effort).
    }

    // Direct-message conversations: one row per conversation, showing its latest
    // message. Each row's status carries the conversation id so the feed stays one
    // row per conversation as new messages arrive (fetch + streaming both dedupe on it).
    if (source.kind == TimelineSource::Kind::Conversations) {
        std::string url =
            credentials_.instance_url + "/api/v1/conversations?limit=" + std::to_string(limit);
        if (cursor.kind == CursorKind::MaxID)
            url += "&max_id=" + cursor.value;
        net::HttpRequest req;
        req.method = "GET";
        req.url = url;
        req.headers.push_back({"Authorization", "Bearer " + credentials_.access_token});
        const net::HttpResponse res = http_->send(req);
        if (!res.ok())
            return page;
        try {
            const json j = json::parse(res.body);
            if (j.is_array()) {
                std::string last_id;
                for (const auto& conv : j) {
                    last_id = conv.value("id", std::string{});
                    auto ls = conv.find("last_status");
                    if (ls == conv.end() || !ls->is_object())
                        continue; // an empty conversation has no message to show
                    Status s = mastodon::map_status(*ls);
                    s.conversation_id = last_id;
                    s.conversation_unread = conv.value("unread", false);
                    page.items.push_back(TimelineItem{std::move(s)});
                }
                if (auto link = res.header("Link"))
                    if (auto next = parse_next_max_id(*link))
                        page.next_cursor = PageCursor::max_id(*next);
                if (!page.next_cursor && !last_id.empty())
                    page.next_cursor = PageCursor::max_id(last_id); // conversations page by their id
            }
        } catch (...) {
        }
        return page;
    }

    std::string path;
    std::string extra;
    switch (source.kind) {
    case TimelineSource::Kind::Home:
        path = "/api/v1/timelines/home";
        break;
    case TimelineSource::Kind::Notifications:
        path = "/api/v1/notifications";
        break;
    case TimelineSource::Kind::Local:
    case TimelineSource::Kind::Federated:
        path = "/api/v1/timelines/public";
        break;
    case TimelineSource::Kind::Mentions:
        path = "/api/v1/notifications";
        extra = "&types[]=mention"; // only @-mentions
        break;
    case TimelineSource::Kind::Bookmarks:
        path = "/api/v1/bookmarks";
        break;
    case TimelineSource::Kind::Favorites:
        path = "/api/v1/favourites";
        break;
    case TimelineSource::Kind::UserPosts:
        path = "/api/v1/accounts/" + source.param + "/statuses";
        break;
    case TimelineSource::Kind::Followers:
        path = "/api/v1/accounts/" + source.param + "/followers";
        break;
    case TimelineSource::Kind::Following:
        path = "/api/v1/accounts/" + source.param + "/following";
        break;
    case TimelineSource::Kind::FavoritedBy:
        path = "/api/v1/statuses/" + source.param + "/favourited_by"; // rows are users
        break;
    case TimelineSource::Kind::BoostedBy:
        path = "/api/v1/statuses/" + source.param + "/reblogged_by"; // rows are users
        break;
    case TimelineSource::Kind::Hashtag:
        path = "/api/v1/timelines/tag/" + util::percent_encode(source.param);
        break;
    case TimelineSource::Kind::List:
        path = "/api/v1/timelines/list/" + util::percent_encode(source.param);
        break;
    case TimelineSource::Kind::Mutes:
        path = "/api/v1/mutes"; // rows are accounts; paginates via the Link header
        break;
    case TimelineSource::Kind::Blocks:
        path = "/api/v1/blocks";
        break;
    case TimelineSource::Kind::FollowRequests:
        path = "/api/v1/follow_requests"; // rows are accounts; paginates via the Link header
        break;
    case TimelineSource::Kind::Thread:
    case TimelineSource::Kind::SearchPosts:
    case TimelineSource::Kind::SearchPeople:
    case TimelineSource::Kind::RemoteLocal:
    case TimelineSource::Kind::RemoteUser:
    case TimelineSource::Kind::Trends:
    case TimelineSource::Kind::Conversations:
        break; // handled above (early return)
    }

    std::string url = credentials_.instance_url + path + "?limit=" + std::to_string(limit) + extra;
    if (source.kind == TimelineSource::Kind::Local)
        url += "&local=true";
    if (cursor.kind == CursorKind::MaxID)
        url += "&max_id=" + cursor.value;

    net::HttpRequest req;
    req.method = "GET";
    req.url = url;
    req.headers.push_back({"Authorization", "Bearer " + credentials_.access_token});
    const net::HttpResponse res = http_->send(req);
    if (!res.ok())
        return page;

    json j;
    try {
        j = json::parse(res.body);
    } catch (...) {
        return page;
    }
    if (!j.is_array())
        return page;

    const bool notif_feed = source.is_notification_timeline(); // Notifications or Mentions
    const bool mentions = source.kind == TimelineSource::Kind::Mentions;
    const bool user_list = source.is_user_list();
    std::string last_id;
    for (const auto& entry : j) {
        if (user_list) {
            User u = mastodon::map_user(entry);
            last_id = u.id;
            page.items.push_back(TimelineItem{std::move(u)});
        } else if (notif_feed) {
            Notification n = mastodon::map_notification(entry);
            last_id = n.id; // /notifications paginates by notification id
            if (mentions) {
                // Show a @-mention as the post itself, not a "X mentioned you" row.
                if (n.status)
                    page.items.push_back(TimelineItem{*n.status});
            } else {
                page.items.push_back(TimelineItem{std::move(n)});
            }
        } else {
            Status s = mastodon::map_status(entry);
            last_id = s.id;
            page.items.push_back(TimelineItem{std::move(s)});
        }
    }
    // Prefer the Link header's rel="next" max_id (required for bookmarks/
    // favourites, which don't paginate by status id); fall back to the last id.
    if (auto link = res.header("Link")) {
        if (auto next = parse_next_max_id(*link))
            page.next_cursor = PageCursor::max_id(*next);
    }
    // Followers/following paginate only via the Link header (the cursor is a
    // relationship id, not an account id), so don't fall back to the last id.
    if (!page.next_cursor && !last_id.empty() && !user_list)
        page.next_cursor = PageCursor::max_id(last_id);

    // User timelines: float the account's pinned posts to the top, but only on the
    // first page (never while paging older). Mastodon returns them from a separate
    // ?pinned=true request; mark them, drop any copy that also shows up in the
    // normal feed (the pinned one wins), and prepend them. The next cursor stays
    // derived from the normal feed above, so an old pin never poisons pagination.
    if (source.kind == TimelineSource::Kind::UserPosts && cursor.kind != CursorKind::MaxID) {
        net::HttpRequest preq;
        preq.method = "GET";
        preq.url = credentials_.instance_url + path + "?pinned=true&limit=" + std::to_string(limit);
        preq.headers.push_back({"Authorization", "Bearer " + credentials_.access_token});
        const net::HttpResponse pres = http_->send(preq);
        if (pres.ok()) {
            try {
                const json pj = json::parse(pres.body);
                if (pj.is_array() && !pj.empty()) {
                    std::unordered_set<std::string> pinned_ids;
                    std::vector<TimelineItem> pinned;
                    for (const auto& entry : pj) {
                        Status s = mastodon::map_status(entry);
                        s.pinned = true;
                        pinned_ids.insert(s.id);
                        pinned.push_back(TimelineItem{std::move(s)});
                    }
                    page.items.erase(
                        std::remove_if(page.items.begin(), page.items.end(),
                                       [&](const TimelineItem& it) {
                                           return pinned_ids.count(it.pagination_id()) > 0;
                                       }),
                        page.items.end());
                    page.items.insert(page.items.begin(), std::make_move_iterator(pinned.begin()),
                                      std::make_move_iterator(pinned.end()));
                }
            } catch (...) {
            }
        }
    }
    return page;
}

std::optional<std::string> MastodonAccount::upload_media(const MediaUpload& a) {
    if (a.bytes.empty())
        return std::nullopt;
    // Build a multipart/form-data body: the alt text as "description", then the file.
    const std::string boundary = "----FastSMRWFormBoundary8x3Kq9Zt";
    std::string b;
    if (!a.alt.empty()) {
        b += "--" + boundary + "\r\n";
        b += "Content-Disposition: form-data; name=\"description\"\r\n\r\n";
        b += a.alt + "\r\n";
    }
    b += "--" + boundary + "\r\n";
    b += "Content-Disposition: form-data; name=\"file\"; filename=\"" +
         (a.filename.empty() ? std::string("upload") : a.filename) + "\"\r\n";
    b += "Content-Type: " + (a.mime.empty() ? std::string("application/octet-stream") : a.mime) +
         "\r\n\r\n";
    b += a.bytes;
    b += "\r\n--" + boundary + "--\r\n";

    const std::string ct = "multipart/form-data; boundary=" + boundary;
    std::string body;
    long status = 0;
    if (!request("POST", credentials_.instance_url + "/api/v2/media", b, ct, body, status)) {
        log::write("upload_media: POST /api/v2/media failed, status=" + std::to_string(status) +
                   " file=" + a.filename);
        return std::nullopt;
    }
    std::string id;
    try {
        id = json::parse(body).value("id", std::string{});
    } catch (...) {
        return std::nullopt;
    }
    if (id.empty())
        return std::nullopt;
    // 202 = accepted but still processing (video / large image). Mastodon rejects a
    // status that attaches media that hasn't finished processing, so wait until the
    // server reports it ready (GET returns 200; 206 while still processing). Cap the
    // wait so a stuck upload can't block forever; if it never finishes, treat the
    // upload as failed rather than attaching an id the status POST would reject.
    if (status == 202) {
        const std::string purl = credentials_.instance_url + "/api/v1/media/" + id;
        bool ready = false;
        for (int i = 0; i < 120; ++i) { // up to ~2 min for slow video transcodes
            std::this_thread::sleep_for(std::chrono::seconds(1));
            std::string pb;
            long ps = 0;
            if (request("GET", purl, "", "", pb, ps) && ps == 200) {
                ready = true;
                break;
            }
        }
        if (!ready) {
            log::write("upload_media: media " + id + " still processing after wait; giving up");
            return std::nullopt;
        }
    }
    return id;
}

std::optional<Status> MastodonAccount::post(const PostDraft& draft) {
    std::vector<std::pair<std::string, std::string>> params;
    params.push_back({"status", draft.text});
    // Upload any attachments first. If one fails (network error, or the server
    // never finished processing it), abort the whole post rather than silently
    // sending it without the media the user attached.
    for (const auto& a : draft.attachments) {
        auto id = upload_media(a);
        if (!id)
            return std::nullopt;
        params.push_back({"media_ids[]", *id});
    }
    if (draft.reply_to_id) {
        // Replying to a remote post: resolve its URL to a local id first, so the
        // reply threads correctly on the user's own instance.
        std::string reply_id = *draft.reply_to_id;
        if (draft.reply_to_url && !draft.reply_to_url->empty())
            if (auto local = resolve_url(*draft.reply_to_url))
                reply_id = *local;
        params.push_back({"in_reply_to_id", reply_id});
    }
    if (draft.quoted_status_id) {
        // Mastodon 4.4 quote posts: POST /statuses with quoted_status_id. A remote
        // target must be resolved to its local copy first (like a reply).
        std::string qid = *draft.quoted_status_id;
        if (draft.quoted_status_url && !draft.quoted_status_url->empty())
            if (auto local = resolve_url(*draft.quoted_status_url))
                qid = *local;
        if (!qid.empty())
            params.push_back({"quoted_status_id", qid});
    }
    if (draft.visibility)
        params.push_back({"visibility", visibility_tag(*draft.visibility)});
    if (draft.spoiler_text && !draft.spoiler_text->empty())
        params.push_back({"spoiler_text", *draft.spoiler_text});
    if (draft.language)
        params.push_back({"language", *draft.language});
    if (draft.poll && draft.poll->options.size() >= 2) {
        for (const auto& opt : draft.poll->options)
            params.push_back({"poll[options][]", opt});
        params.push_back({"poll[expires_in]", std::to_string(draft.poll->expires_in_seconds)});
        params.push_back({"poll[multiple]", draft.poll->multiple ? "true" : "false"});
    }
    if (draft.scheduled_at)
        params.push_back({"scheduled_at", util::format_iso8601(*draft.scheduled_at)});

    const std::string url = credentials_.instance_url + "/api/v1/statuses";
    std::string body;
    long status = 0;
    if (!request("POST", url, util::form_encode(params), "application/x-www-form-urlencoded", body,
                 status))
        return std::nullopt;
    try {
        // A scheduled post returns a ScheduledStatus (no "content"); nothing to
        // insert into the timeline.
        json j = json::parse(body);
        if (draft.scheduled_at)
            return std::nullopt;
        return mastodon::map_status(j);
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<Status> MastodonAccount::edit_post(const std::string& id, const PostDraft& draft) {
    std::vector<std::pair<std::string, std::string>> params;
    params.push_back({"status", draft.text});
    if (draft.spoiler_text)
        params.push_back({"spoiler_text", *draft.spoiler_text});
    if (draft.language)
        params.push_back({"language", *draft.language});

    const std::string url = credentials_.instance_url + "/api/v1/statuses/" + id;
    std::string body;
    long status = 0;
    if (!request("PUT", url, util::form_encode(params), "application/x-www-form-urlencoded", body,
                 status))
        return std::nullopt;
    try {
        return mastodon::map_status(json::parse(body));
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<PostSource> MastodonAccount::post_source(const std::string& id) {
    const std::string url = credentials_.instance_url + "/api/v1/statuses/" + id + "/source";
    std::string body;
    long status = 0;
    if (!request("GET", url, "", "", body, status))
        return std::nullopt;
    try {
        const json j = json::parse(body);
        PostSource src;
        src.text = j.value("text", "");
        src.spoiler_text = j.value("spoiler_text", "");
        return src;
    } catch (...) {
        return std::nullopt;
    }
}

bool MastodonAccount::status_action(const std::string& status_id, const char* verb) {
    const std::string url =
        credentials_.instance_url + "/api/v1/statuses/" + status_id + "/" + verb;
    std::string body;
    long status = 0;
    return request("POST", url, "", "", body, status);
}

bool MastodonAccount::delete_post(const Status& status) {
    const std::string url = credentials_.instance_url + "/api/v1/statuses/" + status.id;
    std::string body;
    long http_status = 0;
    return request("DELETE", url, "", "", body, http_status);
}

std::string MastodonAccount::remote_account_id(const std::string& base,
                                               const std::string& username) {
    net::HttpRequest req;
    req.method = "GET";
    req.url = base + "/api/v1/accounts/lookup?acct=" + util::percent_encode(username);
    net::HttpResponse res = http_->send(req);
    if (res.ok()) {
        try {
            if (std::string id = json::parse(res.body).value("id", std::string()); !id.empty())
                return id;
        } catch (...) {
        }
    }
    // Older servers lack /lookup; fall back to account search.
    req.url = base + "/api/v1/accounts/search?q=" + util::percent_encode(username) + "&limit=1";
    res = http_->send(req);
    if (res.ok()) {
        try {
            const json arr = json::parse(res.body);
            if (arr.is_array() && !arr.empty())
                return arr[0].value("id", std::string());
        } catch (...) {
        }
    }
    return {};
}

std::optional<std::string> MastodonAccount::resolve_url(const std::string& post_url) {
    if (post_url.empty())
        return std::nullopt;
    const std::string url = credentials_.instance_url + "/api/v2/search?q=" +
                            util::percent_encode(post_url) +
                            "&type=statuses&resolve=true&limit=1";
    std::string body;
    long st = 0;
    if (!request("GET", url, "", "", body, st))
        return std::nullopt;
    try {
        const json j = json::parse(body);
        const json arr = j.value("statuses", json::array());
        if (arr.is_array() && !arr.empty())
            if (std::string id = arr[0].value("id", std::string()); !id.empty())
                return id;
    } catch (...) {
    }
    return std::nullopt;
}

std::string MastodonAccount::action_status_id(const Status& status) {
    const Status& d = status.display_status();
    if (d.instance_url && !d.url.empty())
        if (auto local = resolve_url(d.url))
            return *local;
    return d.id;
}

bool MastodonAccount::boost(const Status& status) {
    return status_action(action_status_id(status), "reblog");
}
bool MastodonAccount::unboost(const Status& status) {
    return status_action(action_status_id(status), "unreblog");
}
bool MastodonAccount::favorite(const Status& status) {
    return status_action(action_status_id(status), "favourite");
}
bool MastodonAccount::unfavorite(const Status& status) {
    return status_action(action_status_id(status), "unfavourite");
}
std::optional<Poll> MastodonAccount::vote_poll(const std::string& poll_id,
                                               const std::vector<int>& choices) {
    if (poll_id.empty() || choices.empty())
        return std::nullopt;
    std::vector<std::pair<std::string, std::string>> params;
    for (int c : choices)
        params.push_back({"choices[]", std::to_string(c)});
    const std::string url = credentials_.instance_url + "/api/v1/polls/" + poll_id + "/votes";
    std::string body;
    long status = 0;
    if (!request("POST", url, util::form_encode(params), "application/x-www-form-urlencoded", body,
                 status))
        return std::nullopt;
    try {
        return mastodon::map_poll(json::parse(body));
    } catch (...) {
        return std::nullopt;
    }
}

bool MastodonAccount::pin_post(const Status& status) {
    return status_action(action_status_id(status), "pin");
}
bool MastodonAccount::unpin_post(const Status& status) {
    return status_action(action_status_id(status), "unpin");
}
bool MastodonAccount::mute_conversation(const Status& status) {
    return status_action(action_status_id(status), "mute");
}
bool MastodonAccount::unmute_conversation(const Status& status) {
    return status_action(action_status_id(status), "unmute");
}

std::optional<User> MastodonAccount::fetch_profile(const std::string& id) {
    // GET /api/v1/accounts/:id — enrich a sparse row (e.g. a mention, which
    // carries no bio or counts) with the full account so the profile dialog
    // shows the bio and follower/following/post counts.
    if (id.empty())
        return std::nullopt;
    const std::string url = credentials_.instance_url + "/api/v1/accounts/" + id;
    std::string body;
    long status = 0;
    if (!request("GET", url, "", "", body, status))
        return std::nullopt;
    try {
        json j = json::parse(body);
        if (!j.is_object())
            return std::nullopt;
        return mastodon::map_user(j);
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<Status> MastodonAccount::fetch_status(const std::string& id) {
    // GET /api/v1/statuses/:id — used to speak a reply's parent that isn't loaded.
    if (id.empty())
        return std::nullopt;
    const std::string url = credentials_.instance_url + "/api/v1/statuses/" + id;
    std::string body;
    long status = 0;
    if (!request("GET", url, "", "", body, status))
        return std::nullopt;
    try {
        json j = json::parse(body);
        if (!j.is_object())
            return std::nullopt;
        return mastodon::map_status(j);
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<User> MastodonAccount::lookup_user(const std::string& handle) {
    // Resolve a typed handle to a full account. /accounts/lookup takes the acct
    // form (user@domain, or just user for a local account); a leading '@' is
    // stripped. Falls back to a resolving search so a handle from an instance this
    // server hasn't seen yet still gets fetched.
    std::string h = handle;
    if (!h.empty() && h.front() == '@')
        h.erase(h.begin());
    if (h.empty())
        return std::nullopt;
    std::string body;
    long status = 0;
    const std::string lurl =
        credentials_.instance_url + "/api/v1/accounts/lookup?acct=" + util::percent_encode(h);
    if (request("GET", lurl, "", "", body, status)) {
        try {
            json j = json::parse(body);
            if (j.is_object() && j.contains("id"))
                return mastodon::map_user(j);
        } catch (...) {
        }
    }
    const std::string surl = credentials_.instance_url + "/api/v1/accounts/search?q=" +
                             util::percent_encode(h) + "&limit=1&resolve=true";
    if (request("GET", surl, "", "", body, status)) {
        try {
            json arr = json::parse(body);
            if (arr.is_array() && !arr.empty() && arr[0].is_object() && arr[0].contains("id"))
                return mastodon::map_user(arr[0]);
        } catch (...) {
        }
    }
    return std::nullopt;
}

std::vector<User> MastodonAccount::search_accounts(const std::string& query, int limit) {
    // Typeahead: /accounts/search with resolve=false stays fast (no remote
    // WebFinger round-trips) and returns accounts this server already knows.
    std::vector<User> out;
    std::string q = query;
    if (!q.empty() && q.front() == '@')
        q.erase(q.begin());
    if (q.empty())
        return out;
    if (limit <= 0)
        limit = 8;
    const std::string url = credentials_.instance_url + "/api/v1/accounts/search?q=" +
                            util::percent_encode(q) + "&limit=" + std::to_string(limit) +
                            "&resolve=false";
    std::string body;
    long status = 0;
    if (!request("GET", url, "", "", body, status))
        return out;
    try {
        json arr = json::parse(body);
        if (arr.is_array())
            for (const auto& a : arr)
                if (a.is_object() && a.contains("id"))
                    out.push_back(mastodon::map_user(a));
    } catch (...) {
    }
    return out;
}

// GET /api/v1/accounts/:id/{followers,following}, paging through every page via
// the Link header's rel="next" (the cursor is a relationship-internal id, not an
// account id, so it only appears in the header). All-or-nothing: any 429 aborts
// as RateLimited and any other failure as Failed, so a partial list is never
// handed back as if it were complete.
FullRelationResult MastodonAccount::fetch_all_relations(const std::string& id, bool following) {
    FullRelationResult out;
    const std::string path = "/api/v1/accounts/" + id + (following ? "/following" : "/followers");
    std::string url = credentials_.instance_url + path + "?limit=80";
    // Cap the page count as a safety net against a server that never stops paging.
    for (int page = 0; page < 1000; ++page) {
        net::HttpRequest req;
        req.method = "GET";
        req.url = url;
        req.headers.push_back({"Authorization", "Bearer " + credentials_.access_token});
        const net::HttpResponse res = http_->send(req);
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
        if (!j.is_array()) {
            out.status = FullRelationResult::Status::Failed;
            return out;
        }
        for (const auto& entry : j)
            out.users.push_back(mastodon::map_user(entry));
        // rel="next" absent == reached the end of the list.
        std::optional<std::string> next;
        if (auto link = res.header("Link"))
            next = parse_next_max_id(*link);
        if (!next)
            break;
        url = credentials_.instance_url + path + "?limit=80&max_id=" + *next;
    }
    out.status = FullRelationResult::Status::Ok;
    return out;
}

std::optional<Relationship> MastodonAccount::relationship(const std::string& id) {
    const std::string url = credentials_.instance_url + "/api/v1/accounts/relationships?id[]=" + id;
    std::string body;
    long status = 0;
    if (!request("GET", url, "", "", body, status))
        return std::nullopt;
    try {
        json arr = json::parse(body);
        if (!arr.is_array() || arr.empty())
            return std::nullopt;
        const json& j = arr[0];
        Relationship r;
        r.id = j.value("id", std::string{});
        r.following = j.value("following", false);
        r.followed_by = j.value("followed_by", false);
        r.muting = j.value("muting", false);
        r.blocking = j.value("blocking", false);
        r.requested = j.value("requested", false);
        r.showing_reblogs = j.value("showing_reblogs", true);
        return r;
    } catch (...) {
        return std::nullopt;
    }
}

// POST /api/v1/accounts/:id/{follow,unfollow,mute,unmute,block,unblock}.
bool MastodonAccount::account_action(const std::string& id, const char* verb) {
    const std::string url = credentials_.instance_url + "/api/v1/accounts/" + id + "/" + verb;
    std::string body;
    long status = 0;
    return request("POST", url, "", "", body, status);
}
bool MastodonAccount::follow(const std::string& id) { return account_action(id, "follow"); }
bool MastodonAccount::unfollow(const std::string& id) { return account_action(id, "unfollow"); }
bool MastodonAccount::mute(const std::string& id) { return account_action(id, "mute"); }
bool MastodonAccount::unmute(const std::string& id) { return account_action(id, "unmute"); }
bool MastodonAccount::block(const std::string& id) { return account_action(id, "block"); }
bool MastodonAccount::unblock(const std::string& id) { return account_action(id, "unblock"); }
bool MastodonAccount::authorize_follow_request(const std::string& id) {
    // POST /api/v1/follow_requests/:id/authorize (id is the requesting account id).
    const std::string url =
        credentials_.instance_url + "/api/v1/follow_requests/" + id + "/authorize";
    std::string body;
    long status = 0;
    return request("POST", url, "", "", body, status);
}
bool MastodonAccount::reject_follow_request(const std::string& id) {
    const std::string url =
        credentials_.instance_url + "/api/v1/follow_requests/" + id + "/reject";
    std::string body;
    long status = 0;
    return request("POST", url, "", "", body, status);
}
bool MastodonAccount::set_show_boosts(const std::string& id, bool show) {
    // Re-follow with the reblogs flag to show/hide this account's boosts.
    const std::string url = credentials_.instance_url + "/api/v1/accounts/" + id + "/follow";
    std::string body;
    long status = 0;
    return request("POST", url, std::string("reblogs=") + (show ? "true" : "false"),
                   "application/x-www-form-urlencoded", body, status);
}

// --- Server-side filters (Mastodon /api/v2/filters) ---

namespace {

ServerFilter parse_server_filter(const json& j) {
    ServerFilter f;
    f.id = j.value("id", std::string{});
    f.title = j.value("title", std::string{});
    f.action = j.value("filter_action", std::string("warn"));
    if (auto it = j.find("context"); it != j.end() && it->is_array())
        for (const auto& c : *it)
            if (c.is_string())
                f.context.push_back(c.get<std::string>());
    if (auto it = j.find("expires_at"); it != j.end() && it->is_string())
        f.expires_at = util::parse_iso8601(it->get<std::string>());
    if (auto it = j.find("keywords"); it != j.end() && it->is_array())
        for (const auto& k : *it) {
            ServerFilterKeyword kw;
            kw.id = k.value("id", std::string{});
            kw.keyword = k.value("keyword", std::string{});
            kw.whole_word = k.value("whole_word", true);
            f.keywords.push_back(std::move(kw));
        }
    return f;
}

// Build the shared title/context/action/expiry params for create & update.
void append_filter_fields(std::vector<std::pair<std::string, std::string>>& p,
                          const ServerFilter& f) {
    p.push_back({"title", f.title});
    p.push_back({"filter_action", f.action});
    for (const auto& c : f.context)
        p.push_back({"context[]", c});
    // Only send expires_in when setting a window. Sending it empty risks being
    // read as 0 (immediately expired), so "never" simply omits it.
    if (f.expires_in > 0)
        p.push_back({"expires_in", std::to_string(f.expires_in)});
}

} // namespace

std::vector<ServerFilter> MastodonAccount::list_server_filters() {
    const std::string url = credentials_.instance_url + "/api/v2/filters";
    std::string body;
    long status = 0;
    if (!request("GET", url, "", "", body, status))
        return {};
    std::vector<ServerFilter> out;
    try {
        const json arr = json::parse(body);
        if (arr.is_array())
            for (const auto& j : arr)
                out.push_back(parse_server_filter(j));
    } catch (...) {
    }
    return out;
}

namespace {
// A hashtag name for a URL path: drop a leading '#' and percent-encode.
std::string tag_path(const std::string& name) {
    std::string n = name;
    if (!n.empty() && n.front() == '#')
        n.erase(n.begin());
    return util::percent_encode(n);
}
} // namespace

bool MastodonAccount::follow_hashtag(const std::string& name) {
    const std::string url = credentials_.instance_url + "/api/v1/tags/" + tag_path(name) + "/follow";
    std::string body;
    long status = 0;
    return request("POST", url, "", "", body, status);
}

bool MastodonAccount::unfollow_hashtag(const std::string& name) {
    const std::string url = credentials_.instance_url + "/api/v1/tags/" + tag_path(name) + "/unfollow";
    std::string body;
    long status = 0;
    return request("POST", url, "", "", body, status);
}

std::vector<FollowedTag> MastodonAccount::followed_hashtags() {
    // GET /api/v1/followed_tags. One page (up to 200) is plenty for a manager UI.
    const std::string url = credentials_.instance_url + "/api/v1/followed_tags?limit=200";
    std::string body;
    long status = 0;
    if (!request("GET", url, "", "", body, status))
        return {};
    std::vector<FollowedTag> out;
    try {
        const json arr = json::parse(body);
        if (arr.is_array())
            for (const auto& j : arr) {
                FollowedTag t;
                t.name = j.value("name", std::string{});
                t.url = j.value("url", std::string{});
                t.following = true;
                if (!t.name.empty())
                    out.push_back(std::move(t));
            }
    } catch (...) {
    }
    return out;
}

std::vector<FollowedTag> MastodonAccount::trending_hashtags() {
    // GET /api/v1/trends/tags — the instance's currently trending hashtags. Each
    // entry carries `following` (true if the viewer already follows it). One page
    // is plenty for a picker.
    const std::string url = credentials_.instance_url + "/api/v1/trends/tags?limit=20";
    std::string body;
    long status = 0;
    if (!request("GET", url, "", "", body, status))
        return {};
    std::vector<FollowedTag> out;
    try {
        const json arr = json::parse(body);
        if (arr.is_array())
            for (const auto& j : arr) {
                FollowedTag t;
                t.name = j.value("name", std::string{});
                t.url = j.value("url", std::string{});
                t.following = j.value("following", false);
                if (!t.name.empty())
                    out.push_back(std::move(t));
            }
    } catch (...) {
    }
    return out;
}

std::vector<TimelineList> MastodonAccount::lists() {
    const std::string url = credentials_.instance_url + "/api/v1/lists";
    std::string body;
    long status = 0;
    if (!request("GET", url, "", "", body, status))
        return {};
    std::vector<TimelineList> out;
    try {
        const json arr = json::parse(body);
        if (arr.is_array())
            for (const auto& j : arr)
                out.push_back({j.value("id", std::string{}), j.value("title", std::string{}),
                               j.value("replies_policy", std::string("list")),
                               j.value("exclusive", false)});
    } catch (...) {
    }
    return out;
}

std::vector<TimelineList> MastodonAccount::account_lists(const std::string& account_id) {
    const std::string url = credentials_.instance_url + "/api/v1/accounts/" +
                            util::percent_encode(account_id) + "/lists";
    std::string body;
    long status = 0;
    if (!request("GET", url, "", "", body, status))
        return {};
    std::vector<TimelineList> out;
    try {
        const json arr = json::parse(body);
        if (arr.is_array())
            for (const auto& j : arr)
                out.push_back({j.value("id", std::string{}), j.value("title", std::string{})});
    } catch (...) {
    }
    return out;
}

bool MastodonAccount::set_list_membership(const std::string& list_id,
                                          const std::string& account_id, bool add) {
    // account_ids[] goes in the query string so this works whether or not the
    // HTTP client sends a body on DELETE (removal). Mastodon accepts either.
    const std::string url = credentials_.instance_url + "/api/v1/lists/" +
                            util::percent_encode(list_id) +
                            "/accounts?account_ids[]=" + util::percent_encode(account_id);
    std::string body;
    long status = 0;
    return request(add ? "POST" : "DELETE", url, "", "", body, status);
}

bool MastodonAccount::create_list(const std::string& title, const std::string& replies_policy,
                                  bool exclusive) {
    const std::string url = credentials_.instance_url + "/api/v1/lists";
    const std::string form = util::form_encode({{"title", title},
                                                {"replies_policy", replies_policy},
                                                {"exclusive", exclusive ? "true" : "false"}});
    std::string body;
    long status = 0;
    return request("POST", url, form, "application/x-www-form-urlencoded", body, status);
}

bool MastodonAccount::update_list(const std::string& id, const std::string& title,
                                  const std::string& replies_policy, bool exclusive) {
    const std::string url = credentials_.instance_url + "/api/v1/lists/" + util::percent_encode(id);
    const std::string form = util::form_encode({{"title", title},
                                                {"replies_policy", replies_policy},
                                                {"exclusive", exclusive ? "true" : "false"}});
    std::string body;
    long status = 0;
    return request("PUT", url, form, "application/x-www-form-urlencoded", body, status);
}

bool MastodonAccount::delete_list(const std::string& id) {
    const std::string url = credentials_.instance_url + "/api/v1/lists/" + util::percent_encode(id);
    std::string body;
    long status = 0;
    return request("DELETE", url, "", "", body, status);
}

bool MastodonAccount::create_server_filter(const ServerFilter& filter) {
    std::vector<std::pair<std::string, std::string>> p;
    append_filter_fields(p, filter);
    int i = 0;
    for (const auto& kw : filter.keywords) {
        const std::string base = "keywords_attributes[" + std::to_string(i++) + "]";
        p.push_back({base + "[keyword]", kw.keyword});
        p.push_back({base + "[whole_word]", kw.whole_word ? "true" : "false"});
    }
    const std::string url = credentials_.instance_url + "/api/v2/filters";
    std::string body;
    long status = 0;
    return request("POST", url, util::form_encode(p), "application/x-www-form-urlencoded", body,
                   status);
}

bool MastodonAccount::update_server_filter(const ServerFilter& filter) {
    // Replace the keyword set wholesale (like FastSM): destroy every existing
    // keyword, then add the current ones fresh. Fetch the live filter to learn
    // the existing keyword ids.
    const std::string base_url = credentials_.instance_url + "/api/v2/filters/" + filter.id;
    std::vector<std::pair<std::string, std::string>> p;
    append_filter_fields(p, filter);
    int i = 0;
    {
        std::string body;
        long status = 0;
        if (request("GET", base_url, "", "", body, status)) {
            try {
                const ServerFilter live = parse_server_filter(json::parse(body));
                for (const auto& kw : live.keywords) {
                    if (kw.id.empty())
                        continue;
                    const std::string b = "keywords_attributes[" + std::to_string(i++) + "]";
                    p.push_back({b + "[id]", kw.id});
                    p.push_back({b + "[_destroy]", "true"});
                }
            } catch (...) {
            }
        }
    }
    for (const auto& kw : filter.keywords) {
        const std::string b = "keywords_attributes[" + std::to_string(i++) + "]";
        p.push_back({b + "[keyword]", kw.keyword});
        p.push_back({b + "[whole_word]", kw.whole_word ? "true" : "false"});
    }
    std::string body;
    long status = 0;
    return request("PUT", base_url, util::form_encode(p), "application/x-www-form-urlencoded", body,
                   status);
}

bool MastodonAccount::delete_server_filter(const std::string& id) {
    const std::string url = credentials_.instance_url + "/api/v2/filters/" + id;
    std::string body;
    long status = 0;
    return request("DELETE", url, "", "", body, status);
}

std::optional<StreamRequest> MastodonAccount::stream_request_for(const TimelineSource& source) const {
    // Map a timeline to its Mastodon SSE endpoint. Home + Notifications ride the
    // one `user` stream; the public/hashtag/list feeds each have their own.
    std::string path;
    switch (source.kind) {
    case TimelineSource::Kind::Home:
    case TimelineSource::Kind::Notifications:
    case TimelineSource::Kind::Conversations: // `conversation` events ride the user stream
        path = "user";
        break;
    case TimelineSource::Kind::Local:
        path = "public/local";
        break;
    case TimelineSource::Kind::Federated:
        path = "public";
        break;
    case TimelineSource::Kind::Hashtag:
        if (source.param.empty())
            return std::nullopt;
        path = "hashtag?tag=" + util::percent_encode(source.param);
        break;
    case TimelineSource::Kind::List:
        if (source.param.empty())
            return std::nullopt;
        path = "list?list=" + util::percent_encode(source.param);
        break;
    default:
        return std::nullopt; // bookmarks/favorites/user lists/searches/remote don't stream
    }
    StreamRequest r;
    r.url = credentials_.instance_url + "/api/v1/streaming/" + path;
    r.headers = {{"Authorization", "Bearer " + credentials_.access_token}};
    return r;
}

std::optional<StreamItem> MastodonAccount::parse_stream_event(const std::string& event,
                                                              const std::string& data,
                                                              const TimelineSource& route) const {
    try {
        if (event == "update") {
            // A new status feeds the timeline this stream is for (home, local,
            // federated, a hashtag, a list...).
            return StreamItem{StreamItem::Op::Add,
                              TimelineItem{mastodon::map_status(json::parse(data))}, route, {}};
        }
        if (event == "status.update") {
            // An edited post: replace the existing row in place wherever it appears.
            return StreamItem{StreamItem::Op::Update,
                              TimelineItem{mastodon::map_status(json::parse(data))}, route, {}};
        }
        if (event == "delete") {
            // The payload is just the deleted status id; drop it from every tab.
            return StreamItem{StreamItem::Op::Delete, TimelineItem{}, route, data};
        }
        if (event == "notification") {
            // Notifications only arrive on the user stream and always feed Notifications.
            return StreamItem{StreamItem::Op::Add,
                              TimelineItem{mastodon::map_notification(json::parse(data))},
                              TimelineSource::notifications(), {}};
        }
        if (event == "conversation") {
            // A DM conversation gained a message: its latest message, tagged with the
            // conversation id so the Conversations feed replaces that one row.
            const json conv = json::parse(data);
            auto ls = conv.find("last_status");
            if (ls == conv.end() || !ls->is_object())
                return std::nullopt; // nothing to show
            Status s = mastodon::map_status(*ls);
            s.conversation_id = conv.value("id", std::string{});
            s.conversation_unread = conv.value("unread", false);
            return StreamItem{StreamItem::Op::Add, TimelineItem{std::move(s)},
                              TimelineSource::conversations(), {}};
        }
    } catch (...) {
        // Malformed payload — ignore this event.
    }
    return std::nullopt; // filters_changed / keep-alives are ignored
}

} // namespace fastsm
