#pragma once

#include <optional>
#include <string>
#include <vector>

#include <windows.h>

namespace fastsmui {

// One openable timeline type. `input_label` empty means no value is needed (a
// standing feed); non-empty means the dialog prompts for that value (e.g. a
// hashtag or search query).
struct NewTimelineEntry {
    std::wstring title;
    std::wstring input_label;
};

struct NewTimelineChoice {
    int index = -1;
    std::wstring value; // the entered value (empty for input-less types)
};

// Shows the New Timeline dialog. Returns the chosen entry index + any entered
// value, or nullopt if cancelled.
std::optional<NewTimelineChoice> show_new_timeline_dialog(HWND parent, HINSTANCE inst,
                                                          const std::vector<NewTimelineEntry>& entries);

} // namespace fastsmui
