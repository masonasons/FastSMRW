#pragma once

#include <string>
#include <vector>

// Bluesky richtext facet detection. AT Protocol posts carry plain `text` plus a
// `facets` array that annotates byte ranges as links, @-mentions, or #hashtags.
// Without facets, links aren't tappable, mentions don't notify/resolve, and tags
// don't register — so the composer must build them from the typed text.
//
// Detection is a pure, unit-testable function over UTF-8 text. Byte offsets are
// indices into the UTF-8 std::string, which is exactly what AT Proto facets use.
// Mention spans still need a network round-trip (handle -> DID) before they can
// become a facet; that resolution lives in BlueskyAccount, not here.
namespace fastsm::bluesky {

struct FacetSpan {
    enum class Type { Link, Mention, Tag };
    std::size_t start = 0; // byte offset, inclusive
    std::size_t end = 0;   // byte offset, exclusive
    Type type = Type::Link;
    // Link: the full URL (with an https:// scheme added for bare "www." matches).
    // Mention: the handle without the leading '@'. Tag: the tag without '#'.
    std::string value;
};

// Detect links, @-mentions, and #hashtags in `utf8_text`, in order of appearance.
std::vector<FacetSpan> detect_facets(const std::string& utf8_text);

} // namespace fastsm::bluesky
