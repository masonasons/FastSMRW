#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "fastsm/models/timeline_item.hpp"

// Compact little-endian binary encoding of timeline rows for the on-disk cache.
// Far smaller and cheaper to (de)serialize than JSON, and it never builds an
// intermediate DOM, so it's easy on RAM. Format is versioned; unknown/truncated
// data decodes to an empty vector (treated as a cache miss).
namespace fastsm::store {

// A decoded cache: the rows, plus the scrollback cursor (the "bottom gap" used to
// load older posts) and whether the rows were truncated to the cache cap (in
// which case the cursor points below the full, un-cached backlog). cursor_kind:
// 0 = none, 1 = max_id, 2 = token. (A reserved gap list is written for forward
// compat but not yet populated.)
// A "gap": after the row with after_id there are unloaded posts; the cursor
// (kind 1 = max_id, 2 = token) fetches them.
struct CachedGap {
    std::string after_id;
    int cursor_kind = 0;
    std::string cursor_value;
};

struct CachedTimeline {
    std::vector<TimelineItem> items;
    bool truncated = false;
    int cursor_kind = 0;
    std::string cursor_value;
    std::vector<CachedGap> gaps;
};

std::string encode_cache(const std::vector<TimelineItem>& items, bool truncated, int cursor_kind,
                         const std::string& cursor_value, const std::vector<CachedGap>& gaps);
CachedTimeline decode_cache(std::string_view data);

// Back-compat helpers (rows only) used by roundtrip tests.
std::string encode_items(const std::vector<TimelineItem>& items);
std::vector<TimelineItem> decode_items(std::string_view data);

} // namespace fastsm::store
