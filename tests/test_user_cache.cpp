#include "check.hpp"

#include "fastsm/models/user_cache.hpp"

using namespace fastsm;

namespace {

User person(const std::string& id, const std::string& acct, const std::string& display = {}) {
    User u;
    u.id = id;
    u.acct = acct;
    u.username = acct.substr(0, acct.find('@'));
    u.display_name = display;
    return u;
}

Status post_by(const User& author) {
    Status s;
    s.id = "s" + author.id;
    s.account = author;
    return s;
}

} // namespace

void test_user_cache_recency() {
    UserCache cache;
    cache.add(person("1", "alice"));
    cache.add(person("2", "bob"));
    CHECK_EQ(cache.size(), size_t{2});
    // Newest first, so whoever you just saw is the first suggestion.
    CHECK_EQ(cache.all()[0].acct, std::string("bob"));

    // Seeing someone again moves them up rather than duplicating them.
    cache.add(person("1", "alice"));
    CHECK_EQ(cache.size(), size_t{2});
    CHECK_EQ(cache.all()[0].acct, std::string("alice"));

    // A re-add refreshes the record, so a changed display name is picked up.
    cache.add(person("1", "alice", "Alice Renamed"));
    CHECK_EQ(cache.size(), size_t{2});
    CHECK_EQ(cache.all()[0].display_name, std::string("Alice Renamed"));

    // A row with no id is not a person: half-filled rows must not pollute the cache.
    cache.add(person("", "ghost"));
    CHECK_EQ(cache.size(), size_t{2});
}

void test_user_cache_is_capped() {
    UserCache cache;
    for (size_t i = 0; i < UserCache::kMaxUsers + 50; ++i)
        cache.add(person(std::to_string(i), "u" + std::to_string(i)));
    CHECK_EQ(cache.size(), UserCache::kMaxUsers);
    // The cap drops the oldest, not the newest: the most recent must survive.
    CHECK_EQ(cache.all().front().id, std::to_string(UserCache::kMaxUsers + 49));
}

void test_user_cache_matching() {
    UserCache cache;
    cache.add(person("1", "alice@example.social", "Alice Example"));
    cache.add(person("2", "bob@other.social", "Bobby Tables"));
    cache.add(person("3", "carol", "Carol Local"));

    // The local part matches, which is what people actually type.
    CHECK_EQ(cache.starting_with("ali", 10).size(), size_t{1});
    CHECK_EQ(cache.starting_with("ali", 10)[0].id, std::string("1"));
    // So does the full handle with its domain.
    CHECK_EQ(cache.starting_with("alice@ex", 10).size(), size_t{1});
    // And the display name, which is often all you remember.
    CHECK_EQ(cache.starting_with("Bobby", 10).size(), size_t{1});
    CHECK_EQ(cache.starting_with("Bobby", 10)[0].id, std::string("2"));
    // Case-insensitively, both ways round.
    CHECK_EQ(cache.starting_with("CAROL", 10).size(), size_t{1});
    CHECK_EQ(cache.starting_with("carol l", 10).size(), size_t{1}); // display name

    // A prefix, not a substring: "ample" is inside "example" but nobody types that
    // expecting a match, and substring matching makes the list noise.
    CHECK_EQ(cache.starting_with("ample", 10).size(), size_t{0});
    // An empty prefix matches nobody rather than listing the whole cache.
    CHECK_EQ(cache.starting_with("", 10).size(), size_t{0});
    CHECK_EQ(cache.starting_with("ali", 0).size(), size_t{0});
    // The limit is honoured.
    CHECK_EQ(cache.starting_with("", 10).size(), size_t{0});
    CHECK_EQ(cache.starting_with("a", 10).size(), size_t{1});
}

void test_user_cache_harvests_rows() {
    UserCache cache;
    const User author = person("10", "author");
    const User booster = person("11", "booster");
    const User quoted = person("12", "quoted");

    // A plain post: just its author.
    cache.add_from(TimelineItem{post_by(author)});
    CHECK_EQ(cache.size(), size_t{1});

    // A boost carries two people, and BOTH matter: the one being read and the one
    // who boosted it. Only taking the wrapper would miss the actual author.
    Status boost = post_by(booster);
    boost.reblog = std::make_shared<Status>(post_by(author));
    cache.add_from(TimelineItem{boost});
    CHECK_EQ(cache.size(), size_t{2});
    CHECK_EQ(cache.all()[0].id, std::string("10")); // the author, added last

    // A quote adds the quoted author too.
    Status quoting = post_by(person("13", "quoter"));
    quoting.quote = std::make_shared<Status>(post_by(quoted));
    cache.add_from(TimelineItem{quoting});
    CHECK_EQ(cache.starting_with("quoted", 5).size(), size_t{1});
    CHECK_EQ(cache.starting_with("quoter", 5).size(), size_t{1});

    // A notification: the actor, and the author of the post it refers to.
    Notification n;
    n.id = "n1";
    n.account = person("20", "actor");
    n.status = std::make_shared<Status>(post_by(person("21", "subject")));
    cache.add_from(TimelineItem{n});
    CHECK_EQ(cache.starting_with("actor", 5).size(), size_t{1});
    CHECK_EQ(cache.starting_with("subject", 5).size(), size_t{1});

    // A user row (a followers list, say) is a person in its own right.
    cache.add_from(TimelineItem{person("30", "listed")});
    CHECK_EQ(cache.starting_with("listed", 5).size(), size_t{1});
}
