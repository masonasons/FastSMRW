#pragma once

#include <string>

namespace fastsm::util {

// When a post carries a quoted post, its body text often also contains the
// quoted post's URL (a "RE:/QT: <url>" prefix, or the bare status URL appended).
// Since the quote is presented separately, that URL is noise — strip it from the
// display text. Mirrors the Mac app's QuoteText and FastSM for Windows.
// `quoted_url` is the quoted post's own URL, when known (may be empty).
std::string strip_quote_url(const std::string& text, const std::string& quoted_url);

} // namespace fastsm::util
