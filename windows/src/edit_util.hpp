#pragma once

#include <windows.h>
#include <commctrl.h>

#include <cwctype>
#include <string>

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

// Delete the word before (forward=false) or after (forward=true) the caret in a
// plain EDIT control -- standard EDIT controls don't do Ctrl+Backspace/Delete.
inline void edit_delete_word(HWND edit, bool forward) {
    DWORD start = 0, end = 0;
    SendMessageW(edit, EM_GETSEL, reinterpret_cast<WPARAM>(&start),
                 reinterpret_cast<LPARAM>(&end));
    if (start != end) { // a selection: just delete it
        SendMessageW(edit, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L""));
        return;
    }
    const int len = GetWindowTextLengthW(edit);
    std::wstring text(static_cast<size_t>(len) + 1, L'\0');
    if (len)
        GetWindowTextW(edit, text.data(), len + 1);
    const int caret = static_cast<int>(start);
    if (!forward) {
        int t = caret;
        while (t > 0 && iswspace(text[static_cast<size_t>(t) - 1]))
            --t; // skip spaces before the caret
        while (t > 0 && !iswspace(text[static_cast<size_t>(t) - 1]))
            --t; // then the word
        SendMessageW(edit, EM_SETSEL, t, caret);
    } else {
        int t = caret;
        while (t < len && iswspace(text[static_cast<size_t>(t)]))
            ++t;
        while (t < len && !iswspace(text[static_cast<size_t>(t)]))
            ++t;
        SendMessageW(edit, EM_SETSEL, caret, t);
    }
    SendMessageW(edit, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L""));
}

inline LRESULT CALLBACK WordDeleteEditProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR,
                                           DWORD_PTR) {
    // Ctrl+Backspace arrives as WM_CHAR 0x7F (which would insert a box); swallow
    // it. The actual delete happens on the WM_KEYDOWN below.
    if (msg == WM_CHAR && wp == 0x7F)
        return 0;
    if (msg == WM_KEYDOWN && (GetKeyState(VK_CONTROL) & 0x8000)) {
        if (wp == VK_BACK) {
            edit_delete_word(h, false);
            return 0;
        }
        if (wp == VK_DELETE) {
            edit_delete_word(h, true);
            return 0;
        }
    }
    return DefSubclassProc(h, msg, wp, lp);
}

// Give an editable EDIT control Ctrl+Backspace / Ctrl+Delete word deletion.
inline void enable_edit_word_delete(HWND edit) {
    if (edit)
        SetWindowSubclass(edit, WordDeleteEditProc, 2, 0);
}

} // namespace fastsmui
