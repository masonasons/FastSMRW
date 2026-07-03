#pragma once

#include <windows.h>
#include <commctrl.h>

// Standard Win32 EDIT controls don't implement Ctrl+A (select all) -- only Rich
// Edit does. Read-only edits (profile text, Post Info) still want it so a screen
// reader user can select-all and copy. Subclass the control to add it.

namespace fastsmui {

inline LRESULT CALLBACK SelectAllEditProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR,
                                          DWORD_PTR) {
    // Ctrl+A arrives as WM_CHAR with code 0x01; swallow it so no control
    // character is inserted (harmless on a read-only edit, but still wrong).
    if (msg == WM_CHAR && wp == 1) {
        SendMessageW(h, EM_SETSEL, 0, -1);
        return 0;
    }
    return DefSubclassProc(h, msg, wp, lp);
}

// Give a read-only (or any) EDIT control Ctrl+A select-all support.
inline void enable_edit_select_all(HWND edit) {
    if (edit)
        SetWindowSubclass(edit, SelectAllEditProc, 1, 0);
}

} // namespace fastsmui
