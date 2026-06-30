#include "check.hpp"

#include "fastsm/auth/mastodon_auth.hpp"

using namespace fastsm;

void test_instance_normalization() {
    CHECK_EQ(MastodonAuth::normalize_instance("mastodon.social"),
             std::string("https://mastodon.social"));
    CHECK_EQ(MastodonAuth::normalize_instance("https://mastodon.social/"),
             std::string("https://mastodon.social"));
    CHECK_EQ(MastodonAuth::normalize_instance("  http://example.com/some/path "),
             std::string("https://example.com"));
    CHECK_EQ(MastodonAuth::normalize_instance(""), std::string(""));
}
