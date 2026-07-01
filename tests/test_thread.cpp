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
