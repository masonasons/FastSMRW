#include "report_dialog.hpp"

#include <utility>
#include <vector>

#include "../resources/resource.h"
#include "utf.hpp"

namespace fastsmui {
namespace {

struct Ctx {
    bool remote = false;
    ReportInput result;
    bool ok = false;
};

// Report categories: a human label paired with the Mastodon API token.
const std::vector<std::pair<const wchar_t*, const char*>>& categories() {
    static const std::vector<std::pair<const wchar_t*, const char*>> c = {
        {L"It's spam", "spam"},
        {L"It breaks a server rule", "violation"},
        {L"It's illegal content", "legal"},
        {L"Something else", "other"},
    };
    return c;
}

INT_PTR CALLBACK Proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        SetWindowLongPtrW(dlg, GWLP_USERDATA, static_cast<LONG_PTR>(lp));
        auto* ctx = reinterpret_cast<Ctx*>(lp);
        HWND combo = GetDlgItem(dlg, IDC_REPORT_CATEGORY);
        for (const auto& [label, token] : categories())
            SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
        SendMessageW(combo, CB_SETCURSEL, 0, 0);
        if (ctx->remote)
            CheckDlgButton(dlg, IDC_REPORT_FORWARD, BST_CHECKED);
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK) {
            auto* ctx = reinterpret_cast<Ctx*>(GetWindowLongPtrW(dlg, GWLP_USERDATA));
            int sel = static_cast<int>(
                SendDlgItemMessageW(dlg, IDC_REPORT_CATEGORY, CB_GETCURSEL, 0, 0));
            if (sel < 0 || sel >= static_cast<int>(categories().size()))
                sel = static_cast<int>(categories().size()) - 1; // "other"
            ctx->result.category = categories()[static_cast<size_t>(sel)].second;
            HWND edit = GetDlgItem(dlg, IDC_REPORT_COMMENT);
            const int len = GetWindowTextLengthW(edit);
            if (len > 0) {
                std::wstring w(static_cast<size_t>(len), L'\0');
                GetWindowTextW(edit, w.data(), len + 1);
                ctx->result.comment = to_utf8(w);
            }
            ctx->result.forward = IsDlgButtonChecked(dlg, IDC_REPORT_FORWARD) == BST_CHECKED;
            ctx->ok = true;
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

std::optional<ReportInput> show_report_dialog(HWND parent, HINSTANCE inst, bool remote) {
    Ctx ctx;
    ctx.remote = remote;
    DialogBoxParamW(inst, MAKEINTRESOURCEW(IDD_REPORT), parent, Proc,
                    reinterpret_cast<LPARAM>(&ctx));
    if (!ctx.ok)
        return std::nullopt;
    return ctx.result;
}

} // namespace fastsmui
