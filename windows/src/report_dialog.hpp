#pragma once

#include <optional>
#include <string>

#include <windows.h>

namespace fastsmui {

// What the report dialog collected. category is the Mastodon API token
// ("spam" | "violation" | "legal" | "other").
struct ReportInput {
    std::string category;
    std::string comment;
    bool forward = false;
};

// Modal "Report" dialog. `remote` pre-checks the "forward to their server" option
// (only meaningful when the user is on another instance). Returns the collected
// fields, or nullopt if the dialog was cancelled.
std::optional<ReportInput> show_report_dialog(HWND parent, HINSTANCE inst, bool remote);

} // namespace fastsmui
