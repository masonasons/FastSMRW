#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "fastsm/models/timeline_item.hpp"

namespace fastsm::store {

// On-disk cache of timeline rows, one compact BINARY file (.fsc) per
// (account:source) key — see timeline_codec. Capped to maxEntries. Reads/writes
// are synchronous and intended to run on the worker thread (the controller never
// touches it from the UI thread). (Debounced/coalesced writes are a later
// refinement.)
class TimelineCache {
public:
    explicit TimelineCache(std::filesystem::path dir, int max_entries = 200);

    std::vector<TimelineItem> load(const std::string& key) const;
    void save(const std::string& key, const std::vector<TimelineItem>& items) const;
    void remove(const std::string& key) const;

    void set_max_entries(int n) { max_entries_ = n; }

private:
    std::filesystem::path file_for(const std::string& key) const;

    std::filesystem::path dir_;
    int max_entries_;
};

} // namespace fastsm::store
