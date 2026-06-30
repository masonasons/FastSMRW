#include "new_timeline_dialog.hpp"

#include "../resources/resource.h"

namespace fastsmui {
namespace {

struct Ctx {
    const std::vector<NewTimelineEntry>* entries;
    NewTimelineChoice choice;
};

// Show/hide the value field + label for the selected entry.
void update_value_field(HWND dlg, const Ctx* ctx, int sel) {
    const bool needs_input =
        sel >= 0 && sel < static_cast<int>(ctx->entries->size()) &&
        !(*ctx->entries)[static_cast<size_t>(sel)].input_label.empty();
    HWND value = GetDlgItem(dlg, IDC_TIMELINE_VALUE);
    HWND label = GetDlgItem(dlg, IDC_TIMELINE_VALUE_LABEL);
    if (needs_input) {
        std::wstring l = (*ctx->entries)[static_cast<size_t>(sel)].input_label + L":";
        SetWindowTextW(label, l.c_str());
    } else {
        SetWindowTextW(label, L"");
        SetWindowTextW(value, L"");
    }
    EnableWindow(value, needs_input);
}

INT_PTR CALLBACK Proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        SetWindowLongPtrW(dlg, DWLP_USER, static_cast<LONG_PTR>(lp));
        auto* ctx = reinterpret_cast<Ctx*>(lp);
        HWND combo = GetDlgItem(dlg, IDC_TIMELINE_TYPE);
        for (const auto& e : *ctx->entries)
            SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(e.title.c_str()));
        SendMessageW(combo, CB_SETCURSEL, 0, 0);
        update_value_field(dlg, ctx, 0);
        SetFocus(combo);
        return FALSE;
    }
    case WM_COMMAND: {
        auto* ctx = reinterpret_cast<Ctx*>(GetWindowLongPtrW(dlg, DWLP_USER));
        if (LOWORD(wp) == IDC_TIMELINE_TYPE && HIWORD(wp) == CBN_SELCHANGE) {
            const int sel = static_cast<int>(
                SendMessageW(GetDlgItem(dlg, IDC_TIMELINE_TYPE), CB_GETCURSEL, 0, 0));
            update_value_field(dlg, ctx, sel);
            return TRUE;
        }
        if (LOWORD(wp) == IDOK) {
            const int sel = static_cast<int>(
                SendMessageW(GetDlgItem(dlg, IDC_TIMELINE_TYPE), CB_GETCURSEL, 0, 0));
            ctx->choice.index = sel;
            wchar_t buf[512] = {0};
            GetDlgItemTextW(dlg, IDC_TIMELINE_VALUE, buf, 512);
            ctx->choice.value = buf;
            // A type that needs input but got none stays open.
            if (sel >= 0 && sel < static_cast<int>(ctx->entries->size()) &&
                !(*ctx->entries)[static_cast<size_t>(sel)].input_label.empty() &&
                ctx->choice.value.find_first_not_of(L" \t") == std::wstring::npos) {
                SetFocus(GetDlgItem(dlg, IDC_TIMELINE_VALUE));
                MessageBeep(MB_ICONWARNING);
                return TRUE;
            }
            EndDialog(dlg, IDOK);
            return TRUE;
        }
        if (LOWORD(wp) == IDCANCEL) {
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    }
    return FALSE;
}

} // namespace

std::optional<NewTimelineChoice>
show_new_timeline_dialog(HWND parent, HINSTANCE inst,
                         const std::vector<NewTimelineEntry>& entries) {
    Ctx ctx;
    ctx.entries = &entries;
    const INT_PTR r = DialogBoxParamW(inst, MAKEINTRESOURCEW(IDD_NEW_TIMELINE), parent, Proc,
                                      reinterpret_cast<LPARAM>(&ctx));
    if (r == IDOK && ctx.choice.index >= 0)
        return ctx.choice;
    return std::nullopt;
}

} // namespace fastsmui
