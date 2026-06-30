#include "compose_dialog.hpp"

#include "../resources/resource.h"
#include "utf.hpp"

namespace fastsmui {
namespace {

struct ComposeCtx {
    std::wstring title;
    std::wstring context;
    std::string text;
};

INT_PTR CALLBACK ComposeProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        SetWindowLongPtrW(dlg, DWLP_USER, static_cast<LONG_PTR>(lp));
        auto* ctx = reinterpret_cast<ComposeCtx*>(lp);
        if (!ctx->title.empty())
            SetWindowTextW(dlg, ctx->title.c_str());
        if (!ctx->context.empty())
            SetDlgItemTextW(dlg, IDC_COMPOSE_CONTEXT, ctx->context.c_str());
        SetFocus(GetDlgItem(dlg, IDC_COMPOSE_EDIT));
        return FALSE; // focus set explicitly
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK) {
            auto* ctx = reinterpret_cast<ComposeCtx*>(GetWindowLongPtrW(dlg, DWLP_USER));
            HWND edit = GetDlgItem(dlg, IDC_COMPOSE_EDIT);
            const int len = GetWindowTextLengthW(edit);
            std::wstring buf(static_cast<size_t>(len) + 1, L'\0');
            const int got = GetWindowTextW(edit, buf.data(), len + 1);
            buf.resize(static_cast<size_t>(got));
            ctx->text = to_utf8(buf);
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

std::optional<std::string> show_compose_dialog(HWND parent, HINSTANCE inst,
                                               const std::wstring& title,
                                               const std::wstring& context) {
    ComposeCtx ctx;
    ctx.title = title;
    ctx.context = context;
    const INT_PTR r = DialogBoxParamW(inst, MAKEINTRESOURCEW(IDD_COMPOSE), parent, ComposeProc,
                                      reinterpret_cast<LPARAM>(&ctx));
    if (r == IDOK && !ctx.text.empty())
        return ctx.text;
    return std::nullopt;
}

} // namespace fastsmui
