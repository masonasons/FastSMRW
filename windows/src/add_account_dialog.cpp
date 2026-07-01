#include "add_account_dialog.hpp"

#include "../resources/resource.h"
#include "utf.hpp"

namespace fastsmui {
namespace {

std::string field_utf8(HWND dlg, int id) {
    HWND ctrl = GetDlgItem(dlg, id);
    const int len = GetWindowTextLengthW(ctrl);
    std::wstring buf(static_cast<size_t>(len) + 1, L'\0');
    const int got = GetWindowTextW(ctrl, buf.data(), len + 1);
    buf.resize(static_cast<size_t>(got));
    return to_utf8(buf);
}

void update_fields(HWND dlg, int platform) {
    const bool bluesky = platform == 1;
    SetDlgItemTextW(dlg, IDC_SERVICE_LABEL, bluesky ? L"&Service:" : L"&Instance:");
    const int show = bluesky ? SW_SHOW : SW_HIDE;
    ShowWindow(GetDlgItem(dlg, IDC_HANDLE_LABEL), show);
    ShowWindow(GetDlgItem(dlg, IDC_HANDLE_EDIT), show);
    ShowWindow(GetDlgItem(dlg, IDC_APPPASS_LABEL), show);
    ShowWindow(GetDlgItem(dlg, IDC_APPPASS_EDIT), show);
    // Auto-fill Bluesky's default service, but don't let it linger as a bogus
    // Mastodon instance when you toggle back (the field is shared).
    if (bluesky) {
        if (GetWindowTextLengthW(GetDlgItem(dlg, IDC_SERVICE_EDIT)) == 0)
            SetDlgItemTextW(dlg, IDC_SERVICE_EDIT, L"bsky.social");
    } else {
        wchar_t buf[32] = {0};
        GetDlgItemTextW(dlg, IDC_SERVICE_EDIT, buf, 32);
        if (wcscmp(buf, L"bsky.social") == 0)
            SetDlgItemTextW(dlg, IDC_SERVICE_EDIT, L"");
    }
}

INT_PTR CALLBACK AddProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        SetWindowLongPtrW(dlg, DWLP_USER, static_cast<LONG_PTR>(lp));
        HWND combo = GetDlgItem(dlg, IDC_PLATFORM_COMBO);
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Mastodon"));
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Bluesky"));
        SendMessageW(combo, CB_SETCURSEL, 0, 0);
        update_fields(dlg, 0);
        SetFocus(combo);
        return FALSE;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_PLATFORM_COMBO && HIWORD(wp) == CBN_SELCHANGE) {
            const int sel = static_cast<int>(
                SendMessageW(GetDlgItem(dlg, IDC_PLATFORM_COMBO), CB_GETCURSEL, 0, 0));
            update_fields(dlg, sel);
            return TRUE;
        }
        if (LOWORD(wp) == IDOK) {
            auto* data = reinterpret_cast<AddAccountData*>(GetWindowLongPtrW(dlg, DWLP_USER));
            data->platform = static_cast<int>(
                SendMessageW(GetDlgItem(dlg, IDC_PLATFORM_COMBO), CB_GETCURSEL, 0, 0));
            data->service = field_utf8(dlg, IDC_SERVICE_EDIT);
            data->handle = field_utf8(dlg, IDC_HANDLE_EDIT);
            data->app_password = field_utf8(dlg, IDC_APPPASS_EDIT);
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

std::optional<AddAccountData> show_add_account_dialog(HWND parent, HINSTANCE inst) {
    AddAccountData data;
    const INT_PTR r = DialogBoxParamW(inst, MAKEINTRESOURCEW(IDD_ADD_ACCOUNT), parent, AddProc,
                                      reinterpret_cast<LPARAM>(&data));
    if (r == IDOK)
        return data;
    return std::nullopt;
}

} // namespace fastsmui
