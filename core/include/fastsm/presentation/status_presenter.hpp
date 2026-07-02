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
// Configurable spoken labels for a user row / a notification (SpeechConfig).
std::string accessibility_label(const User& u);
std::string accessibility_label(const Notification& n, std::int64_t now);

// A single openable link offered by the "open link in post" action, with a
// human-readable title for the chooser.
struct PostLink {
    std::string title;
    std::string url;
};

// Every openable link in a post (mirrors the Mac app's PostLinks.links): links
// embedded in the text (skipping @mention / #hashtag anchors), the link-preview
// card (titled), media attachments (labeled by description + type), and finally
// the post's own URL ("Open this post in browser"). Boosts are unwrapped;
// deduplicated by URL, preserving order.
std::vector<PostLink> post_links(const Status& s);

// A readable, multi-line rendering of a post for the Post Info dialog (Mac
// parity): author/@handle, spoken time, content warning, text, media, stats.
std::string post_info(const Status& s, std::int64_t now);

// Multi-line profile for the Open User Profile dialog: name, @handle, flags
// (bot / locked), bio, then follower/following/post counts. Mirrors the Mac.
std::string user_profile(const User& u);

} // namespace fastsm::present
