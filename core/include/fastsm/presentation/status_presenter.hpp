#pragma once

#include <cstdint>
#include <string>

#include "fastsm/models/status.hpp"
#include "fastsm/models/timeline_item.hpp"

// Builds display strings and screen-reader labels from models. M1 uses a fixed
// field order; configurable "Speech" ordering arrives in M3. All strings are
// UTF-8.
namespace fastsm::present {

// Single dense line for the visual list row.
std::string compact_line(const Status& s, std::int64_t now);
std::string compact_line(const TimelineItem& item, std::int64_t now);

// Rich comma-separated label spoken by the screen reader.
std::string accessibility_label(const Status& s, std::int64_t now);
std::string accessibility_label(const TimelineItem& item, std::int64_t now);

} // namespace fastsm::present
