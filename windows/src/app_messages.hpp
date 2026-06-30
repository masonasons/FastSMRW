#pragma once

#include <windows.h>

// Custom window messages.
// WM_APP_EVENT carries a heap-allocated std::string* (a JSON event from the core,
// posted from the core thread); the window takes ownership and deletes it.
constexpr UINT WM_APP_EVENT = WM_APP + 1;
