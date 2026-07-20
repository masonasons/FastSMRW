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

// WM_APP_TRAYICON is the notification-area (system tray) icon's callback message;
// the mouse event is in the low word of lParam.
constexpr UINT WM_APP_TRAYICON = WM_APP + 3;

// Single-instance activation. A second launch posts this message to the already-
// running instance's window so it surfaces itself. RegisterWindowMessageW returns
// the same system-wide-unique value in every process for the same string, so the
// two processes agree on it without sharing a constant (it can't be a compile-time
// case label, so the WndProc matches it with an if before its switch).
inline UINT wm_show_instance() {
    static const UINT msg = RegisterWindowMessageW(L"FastSMRW_ShowInstance");
    return msg;
}
