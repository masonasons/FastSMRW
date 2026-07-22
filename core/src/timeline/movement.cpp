#include "fastsm/timeline/movement.hpp"

#include <optional>
#include <unordered_map>

namespace fastsm {

std::string MovementUnit::title() const {
    switch (kind) {
    case Kind::SameUser:
        return "Same User";
    case Kind::Thread:
        return "Thread";
    case Kind::Time: {
        if (seconds % 86400 == 0) {
            const int d = seconds / 86400;
            return std::to_string(d) + (d == 1 ? " day" : " days");
        }
        const int h = seconds / 3600;
        return std::to_string(h) + (h == 1 ? " hour" : " hours");
    }
    case Kind::Count:
        return std::to_string(count) + (count == 1 ? " item" : " items");
    }
    return {};
}

std::string MovementUnit::key() const {
    switch (kind) {
    case Kind::SameUser:
        return "same_user";
    case Kind::Thread:
        return "thread";
    case Kind::Time:
        return "time:" + std::to_string(seconds);
    case Kind::Count:
        return "count:" + std::to_string(count);
    }
    return {};
}

bool MovementUnit::from_key(const std::string& key, MovementUnit& out) {
    auto number_after = [&](const char* prefix) -> int {
        const size_t len = std::char_traits<char>::length(prefix);
        if (key.rfind(prefix, 0) != 0)
            return -1;
        try {
            return std::stoi(key.substr(len));
        } catch (...) {
            return -1;
        }
    };
    if (key == "same_user") {
        out = {Kind::SameUser, 0};
        return true;
    }
    if (key == "thread") {
        out = {Kind::Thread, 0};
        return true;
    }
    if (int seconds = number_after("time:"); seconds > 0) {
        out = {Kind::Time, seconds};
        return true;
    }
    if (int count = number_after("count:"); count > 0) {
        out = {Kind::Count, 0, count};
        return true;
    }
    return false;
}

std::vector<MovementUnit> MovementUnit::catalog() {
    return {
        {MovementUnit::Kind::SameUser, 0}, {MovementUnit::Kind::Thread, 0},
        {MovementUnit::Kind::Time, 3600},  {MovementUnit::Kind::Time, 2 * 3600},
        {MovementUnit::Kind::Time, 6 * 3600}, {MovementUnit::Kind::Time, 86400},
        {MovementUnit::Kind::Count, 0, 20}, {MovementUnit::Kind::Count, 0, 50},
        {MovementUnit::Kind::Count, 0, 100},
    };
}

namespace movement {

// Root-ancestor id per index (empty for non-status rows). Follows in_reply_to_id
// among the loaded items so posts of one conversation share a key.
std::vector<std::string> thread_keys(const std::vector<TimelineItem>& items) {
    std::unordered_map<std::string, std::optional<std::string>> parent;
    for (const auto& it : items)
        if (const Status* s = it.actionable_status())
            parent[s->id] = s->in_reply_to_id;

    auto root = [&](std::string current) {
        for (int hops = 0; hops < 1000; ++hops) {
            auto it = parent.find(current);
            if (it == parent.end() || !it->second)
                break; // no parent / not a reply
            const std::string& up = *it->second;
            if (parent.find(up) == parent.end())
                break; // parent not loaded
            current = up;
        }
        return current;
    };

    std::vector<std::string> keys;
    keys.reserve(items.size());
    for (const auto& it : items) {
        if (const Status* s = it.actionable_status())
            keys.push_back(root(s->id));
        else
            keys.push_back({});
    }
    return keys;
}

int destination(const std::vector<TimelineItem>& items, int index, const MovementUnit& unit,
                bool down) {
    if (index < 0 || index >= static_cast<int>(items.size()))
        return -1;
    const int step = down ? 1 : -1;
    const int n = static_cast<int>(items.size());

    switch (unit.kind) {
    case MovementUnit::Kind::Time: {
        const Status* base = items[static_cast<size_t>(index)].actionable_status();
        if (!base)
            return -1;
        for (int i = index + step; i >= 0 && i < n; i += step) {
            if (const Status* s = items[static_cast<size_t>(i)].actionable_status()) {
                const std::int64_t diff =
                    down ? base->created_at - s->created_at : s->created_at - base->created_at;
                if (diff >= unit.seconds)
                    return i;
            }
        }
        return -1;
    }
    case MovementUnit::Kind::SameUser: {
        const Status* base = items[static_cast<size_t>(index)].actionable_status();
        if (!base)
            return -1;
        for (int i = index + step; i >= 0 && i < n; i += step)
            if (const Status* s = items[static_cast<size_t>(i)].actionable_status();
                s && s->account.id == base->account.id)
                return i;
        return -1;
    }
    case MovementUnit::Kind::Thread: {
        const std::vector<std::string> keys = thread_keys(items);
        const std::string& key = keys[static_cast<size_t>(index)];
        if (key.empty())
            return -1;
        for (int i = index + step; i >= 0 && i < n; i += step)
            if (keys[static_cast<size_t>(i)] == key)
                return i;
        return -1;
    }
    case MovementUnit::Kind::Count: {
        int dest = index + step * unit.count;
        if (dest < 0)
            dest = 0;
        if (dest > n - 1)
            dest = n - 1;
        return dest == index ? -1 : dest; // -1 = already at that edge
    }
    }
    return -1;
}

} // namespace movement
} // namespace fastsm
