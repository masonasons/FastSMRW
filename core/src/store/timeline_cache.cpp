#include "fastsm/store/timeline_cache.hpp"

#include <fstream>
#include <iterator>
#include <system_error>
#include <unordered_set>

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
    auto to_cursor = [](int k, const std::string& v) -> std::optional<PageCursor> {
        if (k == 1)
            return PageCursor::max_id(v);
        if (k == 2)
            return PageCursor::token(v);
        return std::nullopt;
    };
    out.scrollback = to_cursor(c.cursor_kind, c.cursor_value);
    for (const auto& g : c.gaps)
        if (auto pc = to_cursor(g.cursor_kind, g.cursor_value))
            out.gaps.push_back({g.after_id, *pc});
    for (const auto& g : c.marks)
        if (auto pc = to_cursor(g.cursor_kind, g.cursor_value))
            out.marks.push_back({g.after_id, *pc});
    return out;
}

void TimelineCache::save(const std::string& key, const std::vector<TimelineItem>& items,
                         const std::optional<PageCursor>& scrollback, bool truncated_in,
                         const std::vector<CacheGap>& gaps, const std::vector<CacheGap>& marks) const {
    if (max_entries_ <= 0) { // caching disabled: never leave a file behind
        remove(key);
        return;
    }
    // Ensure the cache directory exists. Front ends that build the core through
    // the C ABI (macOS, Android) pass config_dir/cache directly and never create
    // it — without this the ofstream below silently fails and nothing is cached.
    std::error_code mkec;
    std::filesystem::create_directories(dir_, mkec);
    const size_t cap = static_cast<size_t>(max_entries_);
    const bool over_cap = items.size() > cap; // backstop cap (caller usually pre-caps)
    const bool truncated = truncated_in || over_cap;
    std::vector<TimelineItem> capped;
    if (over_cap)
        capped.assign(items.begin(), items.begin() + cap);
    const std::vector<TimelineItem>& stored = over_cap ? capped : items;
    auto kind_of = [](const PageCursor& c) {
        return c.kind == CursorKind::MaxID ? 1 : c.kind == CursorKind::Token ? 2 : 0;
    };
    int kind = 0;
    std::string value;
    if (scrollback) {
        kind = kind_of(*scrollback);
        value = scrollback->value;
    }
    // Keep only gaps/marks that land within the rows we actually store.
    std::unordered_set<std::string> stored_ids;
    for (const auto& it : stored)
        stored_ids.insert(it.id());
    auto to_cached = [&](const std::vector<CacheGap>& in) {
        std::vector<CachedGap> out;
        for (const auto& g : in)
            if (stored_ids.count(g.after_id))
                out.push_back({g.after_id, kind_of(g.cursor), g.cursor.value});
        return out;
    };
    const std::string blob =
        encode_cache(stored, truncated, kind, value, to_cached(gaps), to_cached(marks));

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

void TimelineCache::clear_all() const {
    std::error_code ec;
    if (!std::filesystem::is_directory(dir_, ec))
        return;
    for (const auto& entry : std::filesystem::directory_iterator(dir_, ec)) {
        if (entry.is_regular_file(ec) && entry.path().extension() == ".fsc")
            std::filesystem::remove(entry.path(), ec);
    }
}

} // namespace fastsm::store
