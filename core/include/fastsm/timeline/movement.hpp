#pragma once

#include <string>
#include <vector>

#include "fastsm/models/timeline_item.hpp"

// Movement units for fast keyboard navigation (Mac parity): jump by same user,
// by conversation thread, or across a time gap. Ctrl+Left/Right pick the unit;
// Ctrl+Up/Down jump by it.

namespace fastsm {

struct MovementUnit {
    enum class Kind { SameUser, Thread, Time };
    Kind kind = Kind::SameUser;
    int seconds = 0; // gap for Kind::Time

    std::string title() const;

    // Default set the user cycles through (config/reorder lands later).
    static std::vector<MovementUnit> catalog();
};

namespace movement {

// The visible index to jump to from `index` by one `unit` step (down = toward
// older/higher indices), or -1 if there's nowhere to go.
int destination(const std::vector<TimelineItem>& items, int index, const MovementUnit& unit,
                bool down);

} // namespace movement
} // namespace fastsm
