#pragma once

#include <string>
#include <vector>

#include "fastsm/models/timeline_item.hpp"

// Movement units for fast keyboard navigation (Mac parity): jump by same user,
// by conversation thread, across a time gap, or a fixed number of items.
// Ctrl+Left/Right pick the unit; Ctrl+Up/Down jump by it.

namespace fastsm {

struct MovementUnit {
    enum class Kind { SameUser, Thread, Time, Count };
    Kind kind = Kind::SameUser;
    int seconds = 0; // gap for Kind::Time
    int count = 0;   // number of items for Kind::Count

    std::string title() const;
    // Stable settings key: "same_user" / "thread" / "time:<seconds>" / "count:<n>".
    std::string key() const;
    static bool from_key(const std::string& key, MovementUnit& out);

    // Every available unit; which are active (and their order) comes from the
    // movement_units setting.
    static std::vector<MovementUnit> catalog();
};

namespace movement {

// The visible index to jump to from `index` by one `unit` step (down = toward
// older/higher indices), or -1 if there's nowhere to go.
int destination(const std::vector<TimelineItem>& items, int index, const MovementUnit& unit,
                bool down);

// Root-ancestor id per index (empty for non-status rows), so posts of one
// conversation share a key. Also emitted per row so a synchronous navigator
// (the iOS VoiceOver rotor) can compute thread jumps without a round-trip.
std::vector<std::string> thread_keys(const std::vector<TimelineItem>& items);

} // namespace movement
} // namespace fastsm
