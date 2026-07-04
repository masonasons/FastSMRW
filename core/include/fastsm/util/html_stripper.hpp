#pragma once

#include <string>
#include <string_view>

namespace fastsm::util {

// Strip HTML tags (Mastodon content is HTML), inserting spaces at block
// boundaries, decoding entities, and collapsing/trimming whitespace. Returns
// display-ready plain text.
//
// When keep_breaks is true, block boundaries and <br> become real newlines
// (paragraphs separated by a blank line) instead of spaces — for the full post
// view, where the original line structure should be preserved. Runs of spaces
// are still collapsed. The default (false) collapses everything to single
// spaces, which is what timeline rows and spoken strings want.
std::string strip_html(std::string_view html, bool keep_breaks = false);

// Decode HTML entities (named + numeric) into UTF-8. Unknown entities pass
// through unchanged. Exposed for reuse and testing.
std::string decode_entities(std::string_view text);

} // namespace fastsm::util
