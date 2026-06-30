#include "post_info_dialog.hpp"

#include "../resources/resource.h"

namespace fastsmui {
namespace {

struct Ctx {
    const std::wstring* text;
    bool quote_ok;
    bool browser_ok;
    std::optional<PostInfoAction> result;
};

INT_PTR CALLBACK Proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        auto* c = reinterpret_cast<Ctx*>(lp);
        SetWindowLongPtrW(dlg, DWLP_USER, reinterpret_cast<LONG_PTR>(c));
        SetDlgItemTextW(dlg, IDC_POSTINFO_TEXT, c->text->c_str());
        EnableWindow(GetDlgItem(dlg, IDC_POSTINFO_QUOTE), c->quote_ok);
        EnableWindow(GetDlgItem(dlg, IDC_POSTINFO_BROWSER), c->browser_ok);
        SetFocus(GetDlgItem(dlg, IDC_POSTINFO_REPLY));
        return FALSE; // focus set explicitly
    }
    case WM_COMMAND: {
        auto* c = reinterpret_cast<Ctx*>(GetWindowLongPtrW(dlg, DWLP_USER));
        auto finish = [&](PostInfoAction a) {
            if (c)
                c->result = a;
            EndDialog(dlg, 1);
        };
        switch (LOWORD(wp)) {
        case IDC_POSTINFO_REPLY:
            finish(PostInfoAction::Reply);
            return TRUE;
        case IDC_POSTINFO_BOOST:
            finish(PostInfoAction::Boost);
            return TRUE;
        case IDC_POSTINFO_FAVORITE:
            finish(PostInfoAction::Favorite);
            return TRUE;
        case IDC_POSTINFO_QUOTE:
            finish(PostInfoAction::Quote);
            return TRUE;
        case IDC_POSTINFO_BROWSER:
            finish(PostInfoAction::OpenBrowser);
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

std::optional<PostInfoAction> show_post_info_dialog(HWND parent, HINSTANCE inst,
                                                    const std::wstring& text, bool quote_ok,
                                                    bool browser_ok) {
    Ctx ctx{&text, quote_ok, browser_ok, std::nullopt};
    DialogBoxParamW(inst, MAKEINTRESOURCEW(IDD_POST_INFO), parent, Proc,
                    reinterpret_cast<LPARAM>(&ctx));
    return ctx.result;
}

} // namespace fastsmui
