#pragma once

#include <string>
#include <string_view>

namespace fastsm::util {

// Strip HTML tags (Mastodon content is HTML), inserting spaces at block
// boundaries, decoding entities, and collapsing/trimming whitespace. Returns
// display-ready plain text.
std::string strip_html(std::string_view html);

// Decode HTML entities (named + numeric) into UTF-8. Unknown entities pass
// through unchanged. Exposed for reuse and testing.
std::string decode_entities(std::string_view text);

} // namespace fastsm::util
