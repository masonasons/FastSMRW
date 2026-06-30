#include "check.hpp"

#include <filesystem>

#include "fastsm/store/timeline_cache.hpp"
#include "fastsm/util/base64.hpp"

using namespace fastsm;

void test_base64() {
    CHECK_EQ(util::base64_encode("hello"), std::string("aGVsbG8="));
    CHECK_EQ(util::base64_encode("hi"), std::string("aGk="));
    CHECK_EQ(util::base64_decode("aGVsbG8="), std::string("hello"));
    // Round-trip with embedded NUL / high bytes.
    std::string bin;
    for (int i = 0; i < 256; ++i)
        bin.push_back(static_cast<char>(i));
    CHECK_EQ(util::base64_decode(util::base64_encode(bin)), bin);
    CHECK(util::base64_decode("not valid!*").empty());
}

void test_timeline_cache() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "fastsmrw_cache_test";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    store::TimelineCache cache(dir, 200);
    const std::string key = "mastodon:42:home";

    std::vector<TimelineItem> items;
    {
        Status s;
        s.id = "a1";
        s.text = "first";
        s.created_at = 100;
        items.push_back(TimelineItem{std::move(s)});
    }
    {
        Status s;
        s.id = "a2";
        s.text = "second";
        s.created_at = 200;
        items.push_back(TimelineItem{std::move(s)});
    }

    cache.save(key, items);
    const std::vector<TimelineItem> loaded = cache.load(key);
    CHECK_EQ(loaded.size(), size_t(2));
    CHECK_EQ(loaded[0].id(), std::string("s:a1"));
    CHECK(loaded[1].status() != nullptr);
    CHECK_EQ(loaded[1].status()->text, std::string("second"));

    cache.remove(key);
    CHECK_EQ(cache.load(key).size(), size_t(0));

    std::filesystem::remove_all(dir, ec);
}
