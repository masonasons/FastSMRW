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
    void save(const std::string& key, const std::vector<TimelineItem>& items,
              const std::optional<PageCursor>& scrollback,
              const std::vector<CacheGap>& gaps) const;
    void remove(const std::string& key) const;

    void set_max_entries(int n) { max_entries_ = n; }

private:
    std::filesystem::path file_for(const std::string& key) const;

    std::filesystem::path dir_;
    int max_entries_;
};

} // namespace fastsm::store
