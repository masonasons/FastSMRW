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

std::string encode_items(const std::vector<TimelineItem>& items);
std::vector<TimelineItem> decode_items(std::string_view data);

} // namespace fastsm::store
