#pragma once

#include <optional>
#include <string>
#include <vector>

#include <windows.h>

namespace fastsmui {

// Shows the New Timeline dialog with a combo of timeline titles. Returns the
// chosen index (into `titles`) if opened, else nullopt.
std::optional<int> show_new_timeline_dialog(HWND parent, HINSTANCE inst,
                                            const std::vector<std::wstring>& titles);

} // namespace fastsmui
