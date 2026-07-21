#include "check.hpp"

#include <string>

#include "fastsm/platform/bluesky/bluesky_richtext.hpp"

using namespace fastsm::bluesky;
using Type = FacetSpan::Type;

// Byte offsets in a facet must index the UTF-8 text exactly, so the server can
// slice the annotated substring back out.
static std::string slice(const std::string& text, const FacetSpan& f) {
    return text.substr(f.start, f.end - f.start);
}

void test_bluesky_facets_link() {
    const std::string t = "see https://example.com/path for more";
    auto f = detect_facets(t);
    CHECK_EQ(f.size(), static_cast<size_t>(1));
    CHECK(f[0].type == Type::Link);
    CHECK_EQ(f[0].value, std::string("https://example.com/path"));
    CHECK_EQ(slice(t, f[0]), std::string("https://example.com/path"));
}

void test_bluesky_facets_link_trailing_punct() {
    // A trailing period/paren isn't part of the URL.
    const std::string t = "(link: https://example.com).";
    auto f = detect_facets(t);
    CHECK_EQ(f.size(), static_cast<size_t>(1));
    CHECK_EQ(f[0].value, std::string("https://example.com"));
    CHECK_EQ(slice(t, f[0]), std::string("https://example.com"));
}

void test_bluesky_facets_bare_www() {
    const std::string t = "go to www.bsky.app now";
    auto f = detect_facets(t);
    CHECK_EQ(f.size(), static_cast<size_t>(1));
    CHECK(f[0].type == Type::Link);
    CHECK_EQ(f[0].value, std::string("https://www.bsky.app")); // scheme prepended
    CHECK_EQ(slice(t, f[0]), std::string("www.bsky.app"));      // but the span is the visible text
}

void test_bluesky_facets_mention() {
    const std::string t = "hi @alice.bsky.social welcome";
    auto f = detect_facets(t);
    CHECK_EQ(f.size(), static_cast<size_t>(1));
    CHECK(f[0].type == Type::Mention);
    CHECK_EQ(f[0].value, std::string("alice.bsky.social")); // handle without '@'
    CHECK_EQ(slice(t, f[0]), std::string("@alice.bsky.social"));
}

void test_bluesky_facets_mention_needs_domain() {
    // A bare "@word" with no dot isn't a handle (and email local parts don't match
    // because they're not at a boundary).
    CHECK(detect_facets("@nope hello").empty());
    CHECK(detect_facets("mail me at bob@x.com").empty()); // '@' is mid-word
}

void test_bluesky_facets_tag() {
    const std::string t = "a #Blind post #a11y!";
    auto f = detect_facets(t);
    CHECK_EQ(f.size(), static_cast<size_t>(2));
    CHECK(f[0].type == Type::Tag);
    CHECK_EQ(f[0].value, std::string("Blind"));
    CHECK_EQ(f[1].value, std::string("a11y")); // trailing '!' trimmed
    // A purely numeric tag is rejected.
    CHECK(detect_facets("#123").empty());
}

void test_bluesky_facets_utf8_offsets() {
    // A multibyte prefix must not corrupt later byte offsets.
    const std::string t = "caf\xC3\xA9 #tag"; // "café #tag"
    auto f = detect_facets(t);
    CHECK_EQ(f.size(), static_cast<size_t>(1));
    CHECK(f[0].type == Type::Tag);
    CHECK_EQ(slice(t, f[0]), std::string("#tag"));
}

void test_bluesky_facets_mixed() {
    const std::string t = "@a.com see https://x.io #done";
    auto f = detect_facets(t);
    CHECK_EQ(f.size(), static_cast<size_t>(3));
    CHECK(f[0].type == Type::Mention);
    CHECK(f[1].type == Type::Link);
    CHECK(f[2].type == Type::Tag);
}
