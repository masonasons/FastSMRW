#include "check.hpp"

#include "fastsm/util/date_parsing.hpp"
#include "fastsm/util/demojify.hpp"
#include "fastsm/util/html_stripper.hpp"
#include "fastsm/util/relative_date.hpp"

using namespace fastsm::util;

void test_html_stripping() {
    CHECK_EQ(strip_html("<p>hello &amp; <a href=\"x\">world</a></p>"),
             std::string("hello & world"));
    // Block tags become spaces, runs collapse, ends trim.
    CHECK_EQ(strip_html("<p>a</p><p>b</p>"), std::string("a b"));
    CHECK_EQ(strip_html("  <div>  spaced  </div>  "), std::string("spaced"));
    CHECK_EQ(strip_html("line<br>break"), std::string("line break"));
    // Unknown entity passes through unchanged.
    CHECK_EQ(strip_html("a &bogus; b"), std::string("a &bogus; b"));
}

void test_entity_decoding() {
    CHECK_EQ(decode_entities("&#65;&#x42;&#67;"), std::string("ABC")); // A B C
    CHECK_EQ(decode_entities("&lt;tag&gt;"), std::string("<tag>"));
    CHECK_EQ(decode_entities("caf&#233;"), std::string("caf\xC3\xA9")); // é in UTF-8
    CHECK_EQ(decode_entities("plain"), std::string("plain"));
}

void test_date_parsing() {
    CHECK(parse_iso8601("1970-01-01T00:00:00Z").value() == 0);
    CHECK(parse_iso8601("2000-01-01T00:00:00Z").value() == 946684800);

    const auto z = parse_iso8601("2024-06-28T12:00:00Z");
    const auto frac = parse_iso8601("2024-06-28T12:00:00.123Z");
    const auto plus2 = parse_iso8601("2024-06-28T12:00:00+02:00");
    CHECK(z.has_value() && frac.has_value() && plus2.has_value());
    CHECK_EQ(frac.value(), z.value());            // fractional seconds ignored
    CHECK_EQ(plus2.value(), z.value() - 2 * 3600); // +02:00 is earlier in UTC

    CHECK(!parse_iso8601("not a date").has_value());
    CHECK(!parse_iso8601("2024/06/28 12:00:00").has_value());
}

void test_relative_dates() {
    const std::int64_t now = 1'000'000'000;
    CHECK_EQ(relative_compact(now - 30, now), std::string("now"));
    CHECK_EQ(relative_compact(now - 300, now), std::string("5m"));
    CHECK_EQ(relative_compact(now - 2 * 3600, now), std::string("2h"));
    CHECK_EQ(relative_compact(now - 3 * 86400, now), std::string("3d"));

    CHECK_EQ(relative_spoken(now - 1, now), std::string("just now"));
    CHECK_EQ(relative_spoken(now - 3600, now), std::string("1 hour ago"));
    CHECK_EQ(relative_spoken(now - 2 * 3600, now), std::string("2 hours ago"));
    CHECK_EQ(relative_spoken(now - 300, now), std::string("5 minutes ago"));
}

void test_demojify() {
    // Custom :shortcode: emoji removed, surrounding spaces collapsed.
    CHECK_EQ(strip_custom_emoji("hi :wave: there"), std::string("hi there"));
    CHECK_EQ(strip_custom_emoji("no shortcodes here"), std::string("no shortcodes here"));
    // A colon that isn't a shortcode is left alone.
    CHECK_EQ(strip_custom_emoji("time 12:30 now"), std::string("time 12:30 now"));

    // Unicode emoji removed; accented letters and ASCII kept.
    CHECK_EQ(strip_unicode_emoji("caf\xC3\xA9 \xF0\x9F\x98\x80 time"),
             std::string("caf\xC3\xA9 time"));                          // "café 😀 time"
    CHECK_EQ(strip_unicode_emoji("plain text"), std::string("plain text"));
    // Waving hand + ZWJ sequences fully stripped.
    CHECK_EQ(strip_unicode_emoji("hello \xF0\x9F\x91\x8B"), std::string("hello"));

    CHECK_EQ(collapse_spaces("  a   b  c "), std::string("a b c"));
}

void test_truncate_mentions() {
    // Fewer handles than the max: unchanged.
    CHECK_EQ(truncate_leading_mentions("@a @b hi", 2), std::string("@a @b hi"));
    // 0 disables.
    CHECK_EQ(truncate_leading_mentions("@a @b @c @d hi", 0), std::string("@a @b @c @d hi"));
    // Keep first N, collapse the rest.
    CHECK_EQ(truncate_leading_mentions("@a @b @c @d hi", 2),
             std::string("@a @b and 2 others hi"));
    CHECK_EQ(truncate_leading_mentions("@a @b @c hi", 2), std::string("@a @b and 1 other hi"));
    // Handles with a domain part; collapsed handles are replaced, not kept.
    CHECK_EQ(truncate_leading_mentions("@a@x.social @b @c body", 1),
             std::string("@a@x.social and 2 others body"));
    // No leading mentions: unchanged.
    CHECK_EQ(truncate_leading_mentions("just text", 1), std::string("just text"));
}
