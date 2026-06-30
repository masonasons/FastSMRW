#include "fastsm/store/timeline_cache.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <system_error>

#include "fastsm/models/serialization.hpp"

using nlohmann::json;

namespace fastsm::store {
namespace {

// Make a key filesystem-safe (keys look like "mastodon:123:home").
std::string sanitize(const std::string& key) {
    std::string out;
    out.reserve(key.size());
    for (char c : key) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.')
            out.push_back(c);
        else
            out.push_back('_');
    }
    return out;
}

} // namespace

TimelineCache::TimelineCache(std::filesystem::path dir, int max_entries)
    : dir_(std::move(dir)), max_entries_(max_entries) {}

std::filesystem::path TimelineCache::file_for(const std::string& key) const {
    return dir_ / (sanitize(key) + ".json");
}

std::vector<TimelineItem> TimelineCache::load(const std::string& key) const {
    std::ifstream in(file_for(key), std::ios::binary);
    if (!in)
        return {};
    try {
        json j;
        in >> j;
        if (!j.is_array())
            return {};
        return j.get<std::vector<TimelineItem>>();
    } catch (...) {
        return {};
    }
}

void TimelineCache::save(const std::string& key, const std::vector<TimelineItem>& items) const {
    const size_t cap = static_cast<size_t>(max_entries_ < 0 ? 0 : max_entries_);
    std::vector<TimelineItem> capped;
    if (items.size() > cap)
        capped.assign(items.begin(), items.begin() + cap);
    const std::vector<TimelineItem>& to_write = items.size() > cap ? capped : items;

    try {
        const json j = to_write;
        const std::filesystem::path path = file_for(key);
        const std::filesystem::path tmp = path.string() + ".tmp";
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            if (!out)
                return;
            out << j.dump(); // compact
        }
        std::error_code ec;
        std::filesystem::rename(tmp, path, ec); // atomic-ish replace
        if (ec)
            std::filesystem::remove(tmp, ec);
    } catch (...) {
        // Best-effort cache; ignore failures.
    }
}

void TimelineCache::remove(const std::string& key) const {
    std::error_code ec;
    std::filesystem::remove(file_for(key), ec);
}

} // namespace fastsm::store
