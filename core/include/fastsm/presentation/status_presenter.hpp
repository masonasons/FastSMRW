#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "fastsm/models/status.hpp"
#include "fastsm/models/timeline_item.hpp"
#include "fastsm/presentation/speech_settings.hpp"

// Builds display strings and screen-reader labels from models. All composition
// lives here in the core; the front end only configures field order/visibility
// (SpeechConfig) and renders the result. All strings are UTF-8.
namespace fastsm::present {

// Single dense line for the visual list row (fixed Mac compactLine format).
std::string compact_line(const Status& s, std::int64_t now);
std::string compact_line(const TimelineItem& item, std::int64_t now);

// The string for one speech field, or nullopt if it shouldn't be spoken.
std::optional<std::string> status_field_string(StatusSpeechField field, const Status& s,
                                               std::int64_t now);

// Rich comma-separated label spoken by the screen reader, built from the given
// ordered/toggled fields (defaults to the current SpeechConfig).
std::string accessibility_label(const Status& s, std::int64_t now,
                                const std::vector<SpeechItem<StatusSpeechField>>& fields);
std::string accessibility_label(const Status& s, std::int64_t now);
std::string accessibility_label(const TimelineItem& item, std::int64_t now);

} // namespace fastsm::present
