#include "fastsm/store/timeline_cache.hpp"

#include <fstream>
#include <iterator>
#include <system_error>

#include "fastsm/store/timeline_codec.hpp"

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
    return dir_ / (sanitize(key) + ".fsc"); // FastSM cache, binary
}

LoadedTimeline TimelineCache::load(const std::string& key) const {
    std::ifstream in(file_for(key), std::ios::binary);
    if (!in)
        return {};
    std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CachedTimeline c = decode_cache(data);
    LoadedTimeline out;
    out.items = std::move(c.items);
    out.truncated = c.truncated;
    if (c.cursor_kind == 1)
        out.scrollback = PageCursor::max_id(c.cursor_value);
    else if (c.cursor_kind == 2)
        out.scrollback = PageCursor::token(c.cursor_value);
    return out;
}

void TimelineCache::save(const std::string& key, const std::vector<TimelineItem>& items,
                         const std::optional<PageCursor>& scrollback) const {
    const size_t cap = static_cast<size_t>(max_entries_ < 0 ? 0 : max_entries_);
    const bool truncated = items.size() > cap;
    std::vector<TimelineItem> capped;
    if (truncated)
        capped.assign(items.begin(), items.begin() + cap);
    int kind = 0;
    std::string value;
    if (scrollback) {
        kind = scrollback->kind == CursorKind::MaxID ? 1 : scrollback->kind == CursorKind::Token ? 2
                                                                                                  : 0;
        value = scrollback->value;
    }
    const std::string blob = encode_cache(truncated ? capped : items, truncated, kind, value);

    const std::filesystem::path path = file_for(key);
    const std::filesystem::path tmp = path.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out)
            return;
        out.write(blob.data(), static_cast<std::streamsize>(blob.size()));
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec); // atomic-ish replace
    if (ec)
        std::filesystem::remove(tmp, ec);
}

void TimelineCache::remove(const std::string& key) const {
    std::error_code ec;
    std::filesystem::remove(file_for(key), ec);
}

} // namespace fastsm::store
