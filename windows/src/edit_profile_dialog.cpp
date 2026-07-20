#include "edit_profile_dialog.hpp"

#include <array>
#include <string>
#include <utility>

#include "../resources/resource.h"
#include "utf.hpp"

namespace fastsmui {
namespace {

// The static field-row slots in the dialog resource (name id, value id). The
// dialog shows the first `max_fields` of these and hides the rest.
constexpr int kMaxRows = 8;
const std::array<std::pair<int, int>, kMaxRows> kFieldIds = {{
    {IDC_EP_F1_NAME, IDC_EP_F1_VALUE}, {IDC_EP_F2_NAME, IDC_EP_F2_VALUE},
    {IDC_EP_F3_NAME, IDC_EP_F3_VALUE}, {IDC_EP_F4_NAME, IDC_EP_F4_VALUE},
    {IDC_EP_F5_NAME, IDC_EP_F5_VALUE}, {IDC_EP_F6_NAME, IDC_EP_F6_VALUE},
    {IDC_EP_F7_NAME, IDC_EP_F7_VALUE}, {IDC_EP_F8_NAME, IDC_EP_F8_VALUE},
}};

// Default post visibility: human label + Mastodon token.
const std::array<std::pair<const wchar_t*, const char*>, 4> kPrivacy = {{
    {L"Public", "public"}, {L"Unlisted", "unlisted"},
    {L"Followers only", "private"}, {L"Direct", "direct"},
}};

struct Ctx {
    ProfileEdit current;
    ProfileEdit result;
    int rows = 0; // visible field rows = min(max_fields, kMaxRows)
    bool ok = false;
};

// bare LF -> CRLF for a multiline edit control.
std::wstring to_crlf(const std::string& utf8) {
    std::wstring out;
    for (wchar_t c : to_wide(utf8)) {
        if (c == L'\r')
            continue;
        out += (c == L'\n') ? L"\r\n" : std::wstring(1, c);
    }
    return out;
}

// Read an edit control as UTF-8, normalizing CRLF back to LF.
std::string read_text(HWND dlg, int id) {
    HWND edit = GetDlgItem(dlg, id);
    const int len = GetWindowTextLengthW(edit);
    if (len <= 0)
        return {};
    std::wstring w(static_cast<size_t>(len), L'\0');
    GetWindowTextW(edit, w.data(), len + 1);
    std::string s = to_utf8(w);
    std::string out;
    out.reserve(s.size());
    for (char c : s)
        if (c != '\r')
            out += c;
    return out;
}

INT_PTR CALLBACK Proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        SetWindowLongPtrW(dlg, GWLP_USERDATA, static_cast<LONG_PTR>(lp));
        auto* ctx = reinterpret_cast<Ctx*>(lp);
        const ProfileEdit& c = ctx->current;
        SetDlgItemTextW(dlg, IDC_EP_DISPLAYNAME, to_wide(c.display_name).c_str());
        SetDlgItemTextW(dlg, IDC_EP_NOTE, to_crlf(c.note).c_str());
        CheckDlgButton(dlg, IDC_EP_LOCKED, c.locked ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dlg, IDC_EP_BOT, c.bot ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dlg, IDC_EP_DISCOVERABLE, c.discoverable ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dlg, IDC_EP_SENSITIVE, c.sensitive ? BST_CHECKED : BST_UNCHECKED);
        HWND combo = GetDlgItem(dlg, IDC_EP_PRIVACY);
        int sel = 0;
        for (size_t i = 0; i < kPrivacy.size(); ++i) {
            SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(kPrivacy[i].first));
            if (c.privacy == kPrivacy[i].second)
                sel = static_cast<int>(i);
        }
        SendMessageW(combo, CB_SETCURSEL, sel, 0);
        // Show only as many field rows as the server allows; prefill existing ones.
        ctx->rows = c.max_fields < 1 ? 1 : (c.max_fields > kMaxRows ? kMaxRows : c.max_fields);
        for (int i = 0; i < kMaxRows; ++i) {
            const bool show = i < ctx->rows;
            ShowWindow(GetDlgItem(dlg, kFieldIds[i].first), show ? SW_SHOW : SW_HIDE);
            ShowWindow(GetDlgItem(dlg, kFieldIds[i].second), show ? SW_SHOW : SW_HIDE);
            if (show && i < static_cast<int>(c.fields.size())) {
                SetDlgItemTextW(dlg, kFieldIds[i].first, to_wide(c.fields[i].name).c_str());
                SetDlgItemTextW(dlg, kFieldIds[i].second, to_wide(c.fields[i].value).c_str());
            }
        }
        SetFocus(GetDlgItem(dlg, IDC_EP_DISPLAYNAME));
        return FALSE; // we set focus ourselves
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK) {
            auto* ctx = reinterpret_cast<Ctx*>(GetWindowLongPtrW(dlg, GWLP_USERDATA));
            ProfileEdit& r = ctx->result;
            r.display_name = read_text(dlg, IDC_EP_DISPLAYNAME);
            r.note = read_text(dlg, IDC_EP_NOTE);
            r.locked = IsDlgButtonChecked(dlg, IDC_EP_LOCKED) == BST_CHECKED;
            r.bot = IsDlgButtonChecked(dlg, IDC_EP_BOT) == BST_CHECKED;
            r.discoverable = IsDlgButtonChecked(dlg, IDC_EP_DISCOVERABLE) == BST_CHECKED;
            r.sensitive = IsDlgButtonChecked(dlg, IDC_EP_SENSITIVE) == BST_CHECKED;
            int sel = static_cast<int>(SendDlgItemMessageW(dlg, IDC_EP_PRIVACY, CB_GETCURSEL, 0, 0));
            if (sel < 0 || sel >= static_cast<int>(kPrivacy.size()))
                sel = 0;
            r.privacy = kPrivacy[static_cast<size_t>(sel)].second;
            // Every visible row is sent (blank rows clear a removed field).
            for (int i = 0; i < ctx->rows; ++i)
                r.fields.push_back({read_text(dlg, kFieldIds[i].first),
                                    read_text(dlg, kFieldIds[i].second)});
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

std::optional<ProfileEdit> show_edit_profile_dialog(HWND parent, HINSTANCE inst,
                                                    const ProfileEdit& current) {
    Ctx ctx;
    ctx.current = current;
    DialogBoxParamW(inst, MAKEINTRESOURCEW(IDD_EDIT_PROFILE), parent, Proc,
                    reinterpret_cast<LPARAM>(&ctx));
    if (!ctx.ok)
        return std::nullopt;
    return ctx.result;
}

} // namespace fastsmui
