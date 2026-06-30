#include "user_profile_dialog.hpp"

#include "../resources/resource.h"

namespace fastsmui {
namespace {

struct Ctx {
    const std::wstring* text;
    std::optional<UserProfileAction> result;
};

INT_PTR CALLBACK Proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        auto* c = reinterpret_cast<Ctx*>(lp);
        SetWindowLongPtrW(dlg, DWLP_USER, reinterpret_cast<LONG_PTR>(c));
        // Win32 multiline EDIT needs CRLF line breaks; the core composes with LF.
        std::wstring crlf;
        crlf.reserve(c->text->size() + 16);
        for (wchar_t ch : *c->text) {
            if (ch == L'\n')
                crlf += L"\r\n";
            else
                crlf += ch;
        }
        SetDlgItemTextW(dlg, IDC_PROFILE_TEXT, crlf.c_str());
        SetFocus(GetDlgItem(dlg, IDC_PROFILE_TEXT)); // read the profile immediately
        return FALSE;                                // focus set explicitly
    }
    case WM_COMMAND: {
        auto* c = reinterpret_cast<Ctx*>(GetWindowLongPtrW(dlg, DWLP_USER));
        auto finish = [&](UserProfileAction a) {
            if (c)
                c->result = a;
            EndDialog(dlg, 1);
        };
        switch (LOWORD(wp)) {
        case IDC_PROFILE_POSTS:
            finish(UserProfileAction::ViewPosts);
            return TRUE;
        case IDC_PROFILE_BROWSER:
            finish(UserProfileAction::OpenBrowser);
            return TRUE;
        case IDCANCEL:
            EndDialog(dlg, 0);
            return TRUE;
        }
        break;
    }
    }
    return FALSE;
}

} // namespace

std::optional<UserProfileAction> show_user_profile_dialog(HWND parent, HINSTANCE inst,
                                                          const std::wstring& text) {
    Ctx ctx{&text, std::nullopt};
    DialogBoxParamW(inst, MAKEINTRESOURCEW(IDD_USER_PROFILE), parent, Proc,
                    reinterpret_cast<LPARAM>(&ctx));
    return ctx.result;
}

} // namespace fastsmui
