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
