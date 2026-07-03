#pragma once

#include <optional>
#include <string>
#include <vector>

#include <windows.h>

namespace fastsmui {

enum class PostInfoAction {
    Reply,
    Boost,
    Favorite,
    Quote,
    OpenBrowser,
    OpenLinks,
    ViewThread,
    ViewAuthor,
    Vote,
};

// A votable poll to surface in the dialog (only sent while the viewer can still
// vote). Results for already-voted/closed polls are baked into the text instead.
struct PollInfo {
    bool present = false;
    bool multiple = false; // multi-choice -> a checklist; single -> a list box
    std::vector<std::wstring> options;
};

// What the Post Info dialog returned: an action, plus the chosen option indexes
// when the action is Vote.
struct PostInfoResult {
    std::optional<PostInfoAction> action;
    std::vector<int> choices; // set only for Vote
};

// Modal Post Info dialog (Mac parity): a read-only review of the post plus action
// buttons, and (when the poll is votable) a voting list + Vote button. The caller
// performs the action (dispatches the matching command).
PostInfoResult show_post_info_dialog(HWND parent, HINSTANCE inst, const std::wstring& text,
                                     bool quote_ok, bool browser_ok, const PollInfo& poll);

} // namespace fastsmui
