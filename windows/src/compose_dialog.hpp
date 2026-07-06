#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <windows.h>

#include "fastsm/platform/social_account.hpp" // PostDraft, PlatformFeatures, Visibility

namespace fastsmui {

enum class ComposeMode { New, Reply, Quote, Edit };

// A reply recipient shown in the compose dialog's toggleable checklist.
struct ComposeRecipient {
    std::string acct;     // handle without '@' (mentioned only if left checked)
    std::wstring display; // checklist label
    bool checked = true;  // pre-checked state (the booster is offered unchecked)
};

// A media file staged for upload: bytes are already base64-encoded (the UI read
// the file) so it drops straight into the post command for the core to decode.
struct ComposeAttachment {
    std::wstring filename;   // display name
    std::string mime;        // guessed from the extension
    std::string data_base64; // file bytes, base64
    std::wstring alt;        // alt-text description
};

struct ComposeRequest {
    ComposeMode mode = ComposeMode::New;
    fastsm::PlatformFeatures features;
    int max_chars = 500;
    bool enter_to_send = false;
    std::wstring title;
    std::string context_label;                     // "Replying to X: ..." (reply/quote)
    std::string prefill_text;                       // source / quote url
    std::string prefill_cw;                         // edit content warning
    std::optional<fastsm::Visibility> default_visibility;
    std::vector<ComposeRecipient> recipients;       // reply targets (checklist); empty otherwise
};

struct ComposeResult {
    fastsm::PostDraft draft;          // text/CW/visibility/language/poll/scheduled_at
    ComposeMode mode = ComposeMode::New;
    std::vector<std::string> mentions;         // handles the user left checked (prepended by the core)
    std::vector<ComposeAttachment> attachments; // media to upload with the post
};

// Alt+A @-mention autocomplete hook: given the owner window and the partial
// handle under the caret, returns the chosen handle (without '@') to insert, or
// nullopt if the user cancelled. Supplied by the host so the picker can reach the
// core; when unset, Alt+A does nothing.
using MentionPicker = std::function<std::optional<std::string>(HWND owner, const std::string& partial)>;

// Modal compose dialog (new/reply/quote/edit). Returns the editable fields if
// the user posts; the caller fills in reply_to_id/quoted_status_id/edit_id.
std::optional<ComposeResult> show_compose_dialog(HWND parent, HINSTANCE inst,
                                                 const ComposeRequest& req,
                                                 MentionPicker pick_mention = {});

} // namespace fastsmui
