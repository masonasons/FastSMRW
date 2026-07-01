#pragma once

#include <windows.h>

// Custom window messages.
// WM_APP_EVENT carries a heap-allocated std::string* (a JSON event from the core,
// posted from the core thread); the window takes ownership and deletes it.
constexpr UINT WM_APP_EVENT = WM_APP + 1;

// WM_APP_INV_ACTION carries a heap-allocated std::string* (an invisible-interface
// action id) posted from the low-level keyboard hook; the window takes ownership
// and deletes it after dispatching perform_action.
constexpr UINT WM_APP_INV_ACTION = WM_APP + 2;
