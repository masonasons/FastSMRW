#include "check.hpp"

#include <string>

#include "fastsm/net/http_client.hpp"
#include "fastsm/platform/mastodon/mastodon_account.hpp"

using namespace fastsm;

namespace {
// Returns the focused status for /statuses/<id> and a context (1 ancestor + 1
// descendant) for /statuses/<id>/context.
struct FakeThreadHttp : net::IHttpClient {
    net::HttpResponse send(const net::HttpRequest& req) override {
        net::HttpResponse res;
        res.status = 200;
        if (req.url.find("/context") != std::string::npos) {
            res.body =
                R"({"ancestors":[{"id":"1","content":"<p>root</p>","account":{"id":"a","acct":"alice"},"created_at":"2024-01-01T00:00:00.000Z"}],)"
                R"("descendants":[{"id":"3","content":"<p>reply</p>","account":{"id":"c","acct":"carol"},"created_at":"2024-01-01T02:00:00.000Z"}]})";
        } else {
            res.body =
                R"({"id":"2","content":"<p>focused</p>","account":{"id":"b","acct":"bob"},"created_at":"2024-01-01T01:00:00.000Z"})";
        }
        return res;
    }
};
} // namespace

namespace {
// Serves /api/v2/instance with a configuration block reporting a 1000-char limit.
struct FakeInstanceHttp : net::IHttpClient {
    bool saw_v2 = false;
    net::HttpResponse send(const net::HttpRequest& req) override {
        net::HttpResponse res;
        if (req.url.find("/api/v2/instance") != std::string::npos) {
            saw_v2 = true;
            res.status = 200;
            res.body = R"({"configuration":{"statuses":{"max_characters":1000}}})";
        } else {
            res.status = 404;
        }
        return res;
    }
};
} // namespace

void test_mastodon_instance_max_chars() {
    FakeInstanceHttp http;
    MastodonCredentials cred;
    cred.instance_url = "https://example.social";
    cred.access_token = "tok";
    User me;
    me.id = "me";
    MastodonAccount account(cred, me, &http);

    CHECK_EQ(account.max_chars(), 500); // default before loading
    account.load_configuration();
    CHECK(http.saw_v2);
    CHECK_EQ(account.max_chars(), 1000); // now the instance's real limit
}

namespace {
// Serves a user's normal statuses feed, plus a separate ?pinned=true response.
// The pinned post (id 10, old) also appears in the normal feed to exercise dedup.
struct FakeUserPostsHttp : net::IHttpClient {
    net::HttpResponse send(const net::HttpRequest& req) override {
        net::HttpResponse res;
        res.status = 200;
        if (req.url.find("pinned=true") != std::string::npos) {
            res.body =
                R"([{"id":"10","content":"<p>pinned</p>","account":{"id":"u1","acct":"bob"},"created_at":"2020-01-01T00:00:00.000Z"}])";
        } else {
            res.body =
                R"([{"id":"20","content":"<p>new</p>","account":{"id":"u1","acct":"bob"},"created_at":"2024-03-01T00:00:00.000Z"},)"
                R"({"id":"15","content":"<p>mid</p>","account":{"id":"u1","acct":"bob"},"created_at":"2024-02-01T00:00:00.000Z"},)"
                R"({"id":"10","content":"<p>pinned-dup</p>","account":{"id":"u1","acct":"bob"},"created_at":"2020-01-01T00:00:00.000Z"}])";
        }
        return res;
    }
};
} // namespace

void test_mastodon_user_pinned_posts() {
    FakeUserPostsHttp http;
    MastodonCredentials cred;
    cred.instance_url = "https://example.social";
    cred.access_token = "tok";
    User me;
    me.id = "me";
    MastodonAccount account(cred, me, &http);

    // First page: the pinned post floats to the top and is flagged; its duplicate
    // copy in the normal feed is dropped (the pinned one wins); the rest keep order.
    const TimelinePage page =
        account.items(TimelineSource::user_posts("u1", "@bob"), 40, PageCursor::start());
    CHECK_EQ(page.items.size(), size_t(3));
    if (page.items.size() == 3) {
        CHECK_EQ(page.items[0].id(), std::string("s:10"));
        CHECK(page.items[0].is_pinned());
        CHECK_EQ(page.items[1].id(), std::string("s:20"));
        CHECK(!page.items[1].is_pinned());
        CHECK_EQ(page.items[2].id(), std::string("s:15"));
    }

    // Paging older (a max_id cursor) must NOT prepend pinned posts again.
    const TimelinePage older =
        account.items(TimelineSource::user_posts("u1", "@bob"), 40, PageCursor::max_id("20"));
    CHECK(!older.items.empty());
    if (!older.items.empty()) {
        CHECK_EQ(older.items[0].id(), std::string("s:20"));
        CHECK(!older.items[0].is_pinned());
    }
}

namespace {
// A thread whose focused post (id 2) links in its text to another fediverse post
// (id 9). Serves: the search that resolves the link URL to id 9, and id 9's own
// status + context (ancestor 8). Exercises thread link-folding.
struct FakeFoldHttp : net::IHttpClient {
    net::HttpResponse send(const net::HttpRequest& req) override {
        net::HttpResponse res;
        res.status = 200;
        if (req.url.find("/api/v2/search") != std::string::npos) {
            res.body =
                R"({"statuses":[{"id":"9","content":"<p>linked</p>","account":{"id":"d","acct":"dana"},"created_at":"2024-01-01T03:00:00.000Z"}]})";
        } else if (req.url.find("/context") != std::string::npos) {
            if (req.url.find("/statuses/9/context") != std::string::npos)
                res.body = R"({"ancestors":[{"id":"8","content":"<p>linked-root</p>","account":{"id":"e","acct":"eve"},"created_at":"2024-01-01T02:30:00.000Z"}],"descendants":[]})";
            else if (req.url.find("/statuses/7/context") != std::string::npos)
                res.body = R"({"ancestors":[],"descendants":[]})";
            else
                res.body =
                    R"({"ancestors":[{"id":"1","content":"<p>root</p>","account":{"id":"a","acct":"alice"},"created_at":"2024-01-01T00:00:00.000Z"}],)"
                    R"("descendants":[{"id":"3","content":"<p>reply</p>","account":{"id":"c","acct":"carol"},"created_at":"2024-01-01T02:00:00.000Z"}]})";
        } else if (req.url.find("/statuses/9") != std::string::npos) {
            res.body =
                R"({"id":"9","content":"<p>linked</p>","account":{"id":"d","acct":"dana"},"created_at":"2024-01-01T03:00:00.000Z"})";
        } else if (req.url.find("/statuses/7") != std::string::npos) {
            res.body =
                R"({"id":"7","content":"<p>quoted</p>","account":{"id":"f","acct":"frank"},"created_at":"2024-01-01T00:30:00.000Z"})";
        } else {
            // The focused post quotes id 7 and links to id 9 in its text.
            res.body =
                R"({"id":"2","content":"<p>see <a href=\"https://example.social/@dana/9\">this</a></p>",)"
                R"("account":{"id":"b","acct":"bob"},"created_at":"2024-01-01T01:00:00.000Z",)"
                R"("quote":{"id":"7","content":"<p>quoted</p>","account":{"id":"f","acct":"frank"},"created_at":"2024-01-01T00:30:00.000Z"}})";
        }
        return res;
    }
};
} // namespace

void test_mastodon_thread_folding() {
    FakeFoldHttp http;
    MastodonCredentials cred;
    cred.instance_url = "https://example.social";
    cred.access_token = "tok";
    User me;
    me.id = "me";
    MastodonAccount account(cred, me, &http);

    const TimelinePage page = account.items(TimelineSource::thread("2"), 40, PageCursor::start());

    // The main thread (1,2,3), then the quoted post (7, folded first), then the
    // linked post's conversation (8,9). Deduped, appended after the descendants.
    CHECK_EQ(page.items.size(), size_t(6));
    if (page.items.size() == 6) {
        CHECK_EQ(page.items[0].id(), std::string("s:1"));
        CHECK_EQ(page.items[1].id(), std::string("s:2"));
        CHECK_EQ(page.items[2].id(), std::string("s:3"));
        CHECK_EQ(page.items[3].id(), std::string("s:7")); // the quoted post
        CHECK_EQ(page.items[4].id(), std::string("s:8")); // linked post's ancestor
        CHECK_EQ(page.items[5].id(), std::string("s:9")); // the linked post itself
    }
}

void test_mastodon_thread_fetch() {
    FakeThreadHttp http;
    MastodonCredentials cred;
    cred.instance_url = "https://example.social";
    cred.access_token = "tok";
    User me;
    me.id = "me";
    MastodonAccount account(cred, me, &http);

    const TimelinePage page = account.items(TimelineSource::thread("2"), 40, PageCursor::start());

    // ancestors + focused + descendants, in conversation order. (TimelineItem::id
    // is kind-prefixed, e.g. "s:" for a status.)
    CHECK_EQ(page.items.size(), size_t(3));
    if (page.items.size() == 3) {
        CHECK_EQ(page.items[0].id(), std::string("s:1"));
        CHECK_EQ(page.items[1].id(), std::string("s:2"));
        CHECK_EQ(page.items[2].id(), std::string("s:3"));
    }
}

namespace {
// Serves /api/v2/notifications with one follow_request group (status-less, as the
// real API sends it) and one grouped favourite, with the side-loaded accounts.
struct FakeGroupedNotifsHttp : net::IHttpClient {
    net::HttpResponse send(const net::HttpRequest& req) override {
        net::HttpResponse res;
        res.status = 200;
        if (req.url.find("/api/v2/notifications") == std::string::npos)
            return res; // anything else: empty 200 (shape mismatch -> would fall to v1)
        res.body = R"({
          "accounts":[
            {"id":"900","acct":"carol","username":"carol","display_name":"Carol"},
            {"id":"300","acct":"alice","username":"alice","display_name":"Alice"}],
          "statuses":[
            {"id":"100","content":"<p>hi</p>","account":{"id":"1","acct":"me"},
             "created_at":"2024-06-28T11:00:00.000Z"}],
          "notification_groups":[
            {"group_key":"ungrouped-7001","notifications_count":1,"type":"follow_request",
             "most_recent_notification_id":7001,"page_min_id":"7001","page_max_id":"7001",
             "latest_page_notification_at":"2024-06-28T12:20:00.000Z",
             "sample_account_ids":["900"]},
            {"group_key":"favourite-100","notifications_count":3,"type":"favourite",
             "most_recent_notification_id":7000,"page_min_id":"6990","page_max_id":"7000",
             "latest_page_notification_at":"2024-06-28T12:10:00.000Z",
             "sample_account_ids":["300"],"status_id":"100"}]
        })";
        return res;
    }
};
} // namespace

// A follow-request notification fetched through the grouped (v2) path must keep
// its kind and its requester's account, or the UI can't offer Accept/Reject on it.
void test_mastodon_grouped_follow_request_fetch() {
    FakeGroupedNotifsHttp http;
    MastodonCredentials cred;
    cred.instance_url = "https://example.social";
    cred.access_token = "tok";
    User me;
    me.id = "me";
    MastodonAccount account(cred, me, &http);

    const TimelinePage page =
        account.items(TimelineSource::notifications(), 40, PageCursor::start());
    CHECK_EQ(page.items.size(), size_t(2));
    if (page.items.size() != 2)
        return;
    const Notification* fr = page.items[0].notification();
    CHECK(fr != nullptr);
    if (fr) {
        CHECK(fr->type == Notification::Kind::FollowRequest);
        CHECK_EQ(fr->account.id, std::string("900")); // resolved from the side-load
        CHECK_EQ(fr->account.acct, std::string("carol"));
    }
    const Notification* fav = page.items[1].notification();
    CHECK(fav != nullptr);
    if (fav)
        CHECK(fav->type == Notification::Kind::Favourite);
}

namespace {
// Answers every call with an empty page and whatever rate-limit headers are set.
struct FakeRateLimitHttp : net::IHttpClient {
    net::Headers headers;
    long status = 200;
    net::HttpResponse send(const net::HttpRequest&) override {
        net::HttpResponse res;
        res.status = status;
        res.body = "[]";
        res.headers = headers;
        return res;
    }
};
} // namespace

void test_mastodon_rate_limit_pauses_refresh() {
    FakeRateLimitHttp http;
    MastodonCredentials cred;
    cred.instance_url = "https://example.social";
    cred.access_token = "tok";
    User me;
    me.id = "me";
    MastodonAccount account(cred, me, &http);

    // Plenty of budget left: background refresh keeps running.
    http.headers = {{"X-RateLimit-Remaining", "250"}, {"X-RateLimit-Reset", "2999-01-01T00:00:00.000Z"}};
    account.items(TimelineSource::home(), 40, {});
    CHECK(account.background_refresh_allowed());

    // Nearly spent: paused until the server's reset time.
    http.headers = {{"X-RateLimit-Remaining", "20"}, {"X-RateLimit-Reset", "2999-01-01T00:00:00.000Z"}};
    account.items(TimelineSource::home(), 40, {});
    CHECK(!account.background_refresh_allowed());

    // Once the reset time has passed, refresh resumes.
    MastodonAccount fresh(cred, me, &http);
    http.headers = {{"X-RateLimit-Remaining", "0"}, {"X-RateLimit-Reset", "2000-01-01T00:00:00.000Z"}};
    fresh.items(TimelineSource::home(), 40, {});
    CHECK(fresh.background_refresh_allowed());

    // A bare 429 (no headers) pauses too.
    MastodonAccount limited(cred, me, &http);
    http.headers = {};
    http.status = 429;
    limited.items(TimelineSource::home(), 40, {});
    CHECK(!limited.background_refresh_allowed());
}

// Issue 37: the account remembers when its own server last answered and last
// failed, for "Speak refresh status".
void test_mastodon_tracks_server_replies() {
    FakeRateLimitHttp http;
    MastodonCredentials cred;
    cred.instance_url = "https://example.social";
    cred.access_token = "tok";
    User me;
    me.id = "me";
    MastodonAccount account(cred, me, &http);
    CHECK_EQ(account.last_answer(), std::int64_t{0});
    CHECK_EQ(account.last_failure(), std::int64_t{0});

    // A 404 still means the server is there.
    http.status = 404;
    account.items(TimelineSource::home(), 40, {});
    CHECK(account.last_answer() > 0);
    CHECK_EQ(account.last_failure(), std::int64_t{0});

    // No reply at all, or a 502 from the proxy in front of a dead instance.
    MastodonAccount down(cred, me, &http);
    http.status = 0;
    down.items(TimelineSource::home(), 40, {});
    CHECK(down.last_failure() > 0);
    CHECK_EQ(down.last_answer(), std::int64_t{0});
    MastodonAccount proxied(cred, me, &http);
    http.status = 502;
    proxied.items(TimelineSource::home(), 40, {});
    CHECK(proxied.last_failure() > 0);
    CHECK_EQ(proxied.last_answer(), std::int64_t{0});
}

namespace {
// Serves a remote instance's local timeline (one post by a local author that
// mentions another local user), and answers our own instance's search with
// its copy of that post.
struct FakeRemoteHttp : net::IHttpClient {
    std::string last_search;
    net::HttpResponse send(const net::HttpRequest& req) override {
        net::HttpResponse res;
        res.status = 200;
        if (req.url.find("https://remote.example/api/v1/timelines/public") == 0) {
            res.body =
                R"([{"id":"999","content":"<p>hi @carol</p>","url":"https://remote.example/@bob/999",)"
                R"("account":{"id":"77","acct":"bob","username":"bob"},)"
                R"("mentions":[{"id":"78","acct":"carol","username":"carol","url":"https://remote.example/@carol"}],)"
                R"("created_at":"2024-01-01T00:00:00.000Z"}])";
        } else if (req.url.find("/api/v2/search") != std::string::npos) {
            last_search = req.url;
            res.body = R"({"statuses":[{"id":"123"}]})";
        } else {
            res.status = 404;
        }
        return res;
    }
};
} // namespace

// Issue 45: a remote instance's rows carry that server's ids. Their handles are
// qualified so they can be looked up, and a post resolves to our copy of it.
void test_mastodon_remote_rows_resolve_locally() {
    FakeRemoteHttp http;
    MastodonCredentials cred;
    cred.instance_url = "https://example.social";
    cred.access_token = "tok";
    User me;
    me.id = "me";
    MastodonAccount account(cred, me, &http);

    const TimelinePage page = account.items(TimelineSource::remote_local("remote.example"), 40, {});
    CHECK_EQ(page.items.size(), static_cast<size_t>(1));
    if (page.items.empty())
        return;
    const Status* s = page.items.front().status();
    CHECK(s != nullptr);
    if (!s)
        return;
    CHECK(s->instance_url.has_value());
    CHECK_EQ(s->account.acct, std::string("bob@remote.example"));
    CHECK_EQ(s->mentions.size(), static_cast<size_t>(1));
    if (!s->mentions.empty())
        CHECK_EQ(s->mentions.front().acct, std::string("carol@remote.example"));

    // The remote post resolves to our instance's copy, found by its URL.
    const auto local = account.local_status_id(*s);
    CHECK(local.has_value());
    CHECK_EQ(local.value_or(""), std::string("123"));
    CHECK(http.last_search.find("example.social/api/v2/search") != std::string::npos);

    // An ordinary post is its own id, with no lookup.
    Status plain;
    plain.id = "55";
    http.last_search.clear();
    CHECK_EQ(account.local_status_id(plain).value_or(""), std::string("55"));
    CHECK(http.last_search.empty());
}

namespace {
// Answers POST /statuses with whatever status/body is set (a 422 refusal, a
// scheduled-status reply...).
struct FakePostHttp : net::IHttpClient {
    long status = 200;
    std::string body;
    net::HttpResponse send(const net::HttpRequest&) override {
        net::HttpResponse res;
        res.status = status;
        res.body = body;
        return res;
    }
};
} // namespace

// Issue 42: a refused post keeps the server's reason for the reopened composer,
// and a scheduled post counts as sent.
void test_mastodon_post_failure_reason() {
    FakePostHttp http;
    MastodonCredentials cred;
    cred.instance_url = "https://example.social";
    cred.access_token = "tok";
    User me;
    me.id = "me";
    MastodonAccount account(cred, me, &http);

    PostDraft draft;
    draft.text = "far too long";
    http.status = 422;
    http.body = R"({"error":"Validation failed: Text character limit of 500 exceeded"})";
    CHECK(!account.post(draft).has_value());
    auto err = account.last_request_error();
    CHECK_EQ(err.status, 422L);
    CHECK_EQ(err.message, std::string("Validation failed: Text character limit of 500 exceeded"));

    // No reply at all.
    http.status = 0;
    http.body.clear();
    CHECK(!account.post(draft).has_value());
    err = account.last_request_error();
    CHECK_EQ(err.status, 0L);
    CHECK(err.message.empty());

    // Scheduled: the server answers with a ScheduledStatus; that's a success with
    // nothing to show in a timeline (used to be reported as a failure).
    http.status = 200;
    http.body = R"({"id":"5","scheduled_at":"2999-01-01T00:00:00.000Z","params":{"text":"later"}})";
    draft.scheduled_at = 32503680000;
    const auto scheduled = account.post(draft);
    CHECK(scheduled.has_value());
    CHECK(scheduled && scheduled->id.empty());
}
