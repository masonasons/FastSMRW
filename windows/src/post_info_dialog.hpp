#pragma once

#include <optional>
#include <string>

#include <windows.h>

namespace fastsmui {

enum class PostInfoAction { Reply, Boost, Favorite, Quote, OpenBrowser, ViewThread, ViewAuthor };

// Modal Post Info dialog (Mac parity): a read-only review of the post plus action
// buttons. Returns the chosen action, or nullopt if dismissed. The caller
// performs the action (dispatches the matching command).
std::optional<PostInfoAction> show_post_info_dialog(HWND parent, HINSTANCE inst,
                                                    const std::wstring& text, bool quote_ok,
                                                    bool browser_ok);

} // namespace fastsmui
