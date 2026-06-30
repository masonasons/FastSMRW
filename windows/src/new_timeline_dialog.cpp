#include "new_timeline_dialog.hpp"

#include "../resources/resource.h"

namespace fastsmui {
namespace {

struct Ctx {
    const std::vector<std::wstring>* titles;
    int choice = -1;
};

INT_PTR CALLBACK Proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        SetWindowLongPtrW(dlg, DWLP_USER, static_cast<LONG_PTR>(lp));
        auto* ctx = reinterpret_cast<Ctx*>(lp);
        HWND combo = GetDlgItem(dlg, IDC_TIMELINE_TYPE);
        for (const auto& t : *ctx->titles)
            SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(t.c_str()));
        SendMessageW(combo, CB_SETCURSEL, 0, 0);
        SetFocus(combo);
        return FALSE;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK) {
            auto* ctx = reinterpret_cast<Ctx*>(GetWindowLongPtrW(dlg, DWLP_USER));
            ctx->choice = static_cast<int>(
                SendMessageW(GetDlgItem(dlg, IDC_TIMELINE_TYPE), CB_GETCURSEL, 0, 0));
            EndDialog(dlg, IDOK);
            return TRUE;
        }
        if (LOWORD(wp) == IDCANCEL) {
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

} // namespace

std::optional<int> show_new_timeline_dialog(HWND parent, HINSTANCE inst,
                                            const std::vector<std::wstring>& titles) {
    Ctx ctx;
    ctx.titles = &titles;
    const INT_PTR r = DialogBoxParamW(inst, MAKEINTRESOURCEW(IDD_NEW_TIMELINE), parent, Proc,
                                      reinterpret_cast<LPARAM>(&ctx));
    if (r == IDOK && ctx.choice >= 0)
        return ctx.choice;
    return std::nullopt;
}

} // namespace fastsmui
