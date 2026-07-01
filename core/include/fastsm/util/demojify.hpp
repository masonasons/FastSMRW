#pragma once

#include <string>

// Text clean-up transforms for how posts and names are displayed/spoken. These
// mirror the desktop FastSM ("demojify") and the Mac port's Demojify.swift: strip
// emoji so screen readers don't announce a wall of pictographs, and collapse a
// long leading @mention run on reply chains.
namespace fastsm::util {

// Remove Mastodon-style custom-emoji shortcodes (":like_this:") and collapse any
// double spaces they leave behind.
std::string strip_custom_emoji(const std::string& utf8);

// Remove Unicode emoji code points (the ranges the Python/Mac clients strip),
// including the zero-width joiner and variation selectors that compose them, and
// collapse any double spaces left behind.
std::string strip_unicode_emoji(const std::string& utf8);

// Collapse runs of whitespace to a single space and trim the ends.
std::string collapse_spaces(const std::string& s);

// Collapse a long leading @mention run (e.g. big reply chains): keep the first
// `max` handles, then "and N other(s)", followed by the rest of the text. `max`
// of 0 (or fewer handles than `max`) leaves the text unchanged. Handles match
// "@user" or "@user@domain".
std::string truncate_leading_mentions(const std::string& utf8, int max);

} // namespace fastsm::util
