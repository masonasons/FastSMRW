#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "fastsm/models/timeline_item.hpp"
#include "fastsm/platform/social_account.hpp" // PageCursor

namespace fastsm::store {

// A gap: after the row with after_id there are unloaded posts; cursor fetches them.
struct CacheGap {
    std::string after_id;
    PageCursor cursor;
};

// What a cache load returns: the rows plus the scrollback cursor, whether the
// rows were truncated to the cap (so the cursor may point below un-cached posts),
// and any middle gaps.
struct LoadedTimeline {
    std::vector<TimelineItem> items;
    std::optional<PageCursor> scrollback;
    bool truncated = false;
    std::vector<CacheGap> gaps;
    std::vector<CacheGap> marks; // page-boundary cursors (id -> cursor below it)
};

// On-disk cache of timeline rows, one compact BINARY file (.fsc) per
// (account:source) key — see timeline_codec. Capped to maxEntries. Reads/writes
// are synchronous and intended to run on the worker thread (the controller never
// touches it from the UI thread). (Debounced/coalesced writes are a later
// refinement.)
class TimelineCache {
public:
    explicit TimelineCache(std::filesystem::path dir, int max_entries = 200);

    LoadedTimeline load(const std::string& key) const;
    // `truncated` = the caller already capped to a page boundary (so `scrollback`
    // is that boundary's cursor). The cache still caps to max_entries as a
    // backstop and ORs that into the stored flag.
    void save(const std::string& key, const std::vector<TimelineItem>& items,
              const std::optional<PageCursor>& scrollback, bool truncated,
              const std::vector<CacheGap>& gaps, const std::vector<CacheGap>& marks) const;
    void remove(const std::string& key) const;
    // Delete every cached timeline file (used when caching is turned off).
    void clear_all() const;

    void set_max_entries(int n) { max_entries_ = n; }
    int max_entries() const { return max_entries_; }

private:
    std::filesystem::path file_for(const std::string& key) const;

    std::filesystem::path dir_;
    int max_entries_;
};

} // namespace fastsm::store
