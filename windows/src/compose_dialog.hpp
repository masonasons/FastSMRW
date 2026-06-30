#pragma once

#include <optional>
#include <string>

#include <windows.h>

#include "fastsm/platform/social_account.hpp" // PostDraft, PlatformFeatures, Visibility

namespace fastsmui {

enum class ComposeMode { New, Reply, Quote, Edit };

struct ComposeRequest {
    ComposeMode mode = ComposeMode::New;
    fastsm::PlatformFeatures features;
    int max_chars = 500;
    bool enter_to_send = false;
    std::wstring title;
    std::string context_label;                     // "Replying to X: ..." (reply/quote)
    std::string prefill_text;                       // mentions / source / quote url
    std::string prefill_cw;                         // edit content warning
    std::optional<fastsm::Visibility> default_visibility;
};

struct ComposeResult {
    fastsm::PostDraft draft; // text/CW/visibility/language/poll/scheduled_at
    ComposeMode mode = ComposeMode::New;
};

// Modal compose dialog (new/reply/quote/edit). Returns the editable fields if
// the user posts; the caller fills in reply_to_id/quoted_status_id/edit_id.
std::optional<ComposeResult> show_compose_dialog(HWND parent, HINSTANCE inst,
                                                 const ComposeRequest& req);

} // namespace fastsmui
