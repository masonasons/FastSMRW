#include "check.hpp"

#include <string>

#include "fastsm/net/http_client.hpp"
#include "fastsm/update/update_checker.hpp"

using namespace fastsm;

namespace {

// Serves canned GitHub API responses: a rolling "latest" release built from
// commit deadbeef, and a list with a v0.0.4 release.
struct FakeGitHub : net::IHttpClient {
    net::HttpResponse send(const net::HttpRequest& req) override {
        net::HttpResponse res;
        res.status = 200;
        if (req.url.find("/releases/tags/latest") != std::string::npos) {
            res.body = R"({
                "tag_name": "latest",
                "target_commitish": "deadbeef1234567890",
                "body": "Automated build of main (deadbeef1234).",
                "assets": [{"name":"FastSMRW.zip","browser_download_url":"https://x/latest/FastSMRW.zip"}]
            })";
        } else if (req.url.find("/releases") != std::string::npos) {
            res.body = R"([
                {"tag_name":"latest","prerelease":false,"draft":false,"body":"rolling",
                 "assets":[{"name":"FastSMRW.zip","browser_download_url":"https://x/latest/FastSMRW.zip"}]},
                {"tag_name":"v0.0.4","prerelease":false,"draft":false,"body":"notes for 0.0.4",
                 "assets":[{"name":"FastSMRW.zip","browser_download_url":"https://x/v0.0.4/FastSMRW.zip"}]},
                {"tag_name":"v0.0.2","prerelease":false,"draft":false,"body":"old",
                 "assets":[{"name":"FastSMRW.zip","browser_download_url":"https://x/v0.0.2/FastSMRW.zip"}]}
            ])";
        }
        return res;
    }
};

} // namespace

void test_update_version_compare() {
    using update::compare_versions;
    CHECK(compare_versions("0.0.3", "0.0.4") < 0);
    CHECK(compare_versions("0.0.4", "0.0.3") > 0);
    CHECK(compare_versions("0.0.3", "0.0.3") == 0);
    CHECK(compare_versions("1.2", "1.2.0") == 0);   // missing components are zero
    CHECK(compare_versions("0.10.0", "0.9.0") > 0); // numeric, not lexical
    CHECK(compare_versions("v1.0.0", "0.9.9") > 0);  // leading 'v' ignored
    CHECK(compare_versions("0.0.4-beta", "0.0.4") == 0); // suffix ignored
}

void test_update_stable_branch() {
    FakeGitHub http;
    // Running 0.0.3, newest stable is v0.0.4 -> update available.
    update::UpdateInfo up =
        update::check_for_update(http, "stable", "0.0.3", "abc1234", "owner/repo");
    CHECK(up.error.empty());
    CHECK(up.available);
    CHECK_EQ(up.version, std::string("0.0.4"));
    CHECK_EQ(up.download_url, std::string("https://x/v0.0.4/FastSMRW.zip"));

    // Already on 0.0.4 -> not available.
    update::UpdateInfo cur =
        update::check_for_update(http, "stable", "0.0.4", "abc1234", "owner/repo");
    CHECK(cur.error.empty());
    CHECK(!cur.available);
}

void test_update_latest_branch() {
    FakeGitHub http;
    // Our build commit differs from the release's deadbeef -> update available.
    update::UpdateInfo up =
        update::check_for_update(http, "latest", "0.0.3", "abc1234", "owner/repo");
    CHECK(up.error.empty());
    CHECK(up.available);
    CHECK_EQ(up.download_url, std::string("https://x/latest/FastSMRW.zip"));

    // Same commit -> not available.
    update::UpdateInfo same =
        update::check_for_update(http, "latest", "0.0.3", "deadbee", "owner/repo");
    CHECK(same.error.empty());
    CHECK(!same.available);

    // No embedded commit (local build) -> treated as current, not available.
    update::UpdateInfo local =
        update::check_for_update(http, "latest", "0.0.3", "", "owner/repo");
    CHECK(!local.available);
}
