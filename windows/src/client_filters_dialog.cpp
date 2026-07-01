#include "client_filters_dialog.hpp"

#include "../resources/resource.h"
#include "utf.hpp"

namespace fastsmui {
namespace {

void set_check(HWND dlg, int id, bool on) {
    CheckDlgButton(dlg, id, on ? BST_CHECKED : BST_UNCHECKED);
}
bool get_check(HWND dlg, int id) { return IsDlgButtonChecked(dlg, id) == BST_CHECKED; }

std::wstring field_text(HWND dlg, int id) {
    HWND ctrl = GetDlgItem(dlg, id);
    const int len = GetWindowTextLengthW(ctrl);
    std::wstring buf(static_cast<size_t>(len) + 1, L'\0');
    const int got = GetWindowTextW(ctrl, buf.data(), len + 1);
    buf.resize(static_cast<size_t>(got));
    return buf;
}

INT_PTR CALLBACK ClientFilterProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        SetWindowLongPtrW(dlg, DWLP_USER, static_cast<LONG_PTR>(lp));
        auto* v = reinterpret_cast<ClientFilterValues*>(lp);
        set_check(dlg, IDC_CF_ORIGINAL, v->original);
        set_check(dlg, IDC_CF_REPLIES, v->replies);
        set_check(dlg, IDC_CF_REPLIES_ME, v->replies_to_me);
        set_check(dlg, IDC_CF_THREADS, v->threads);
        set_check(dlg, IDC_CF_BOOSTS, v->boosts);
        set_check(dlg, IDC_CF_QUOTES, v->quotes);
        set_check(dlg, IDC_CF_MEDIA, v->media);
        set_check(dlg, IDC_CF_NO_MEDIA, v->no_media);
        set_check(dlg, IDC_CF_MY_POSTS, v->my_posts);
        set_check(dlg, IDC_CF_MY_REPLIES, v->my_replies);
        SetDlgItemTextW(dlg, IDC_CF_TEXT, v->text.c_str());
        return TRUE;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDOK: {
            auto* v = reinterpret_cast<ClientFilterValues*>(GetWindowLongPtrW(dlg, DWLP_USER));
            v->original = get_check(dlg, IDC_CF_ORIGINAL);
            v->replies = get_check(dlg, IDC_CF_REPLIES);
            v->replies_to_me = get_check(dlg, IDC_CF_REPLIES_ME);
            v->threads = get_check(dlg, IDC_CF_THREADS);
            v->boosts = get_check(dlg, IDC_CF_BOOSTS);
            v->quotes = get_check(dlg, IDC_CF_QUOTES);
            v->media = get_check(dlg, IDC_CF_MEDIA);
            v->no_media = get_check(dlg, IDC_CF_NO_MEDIA);
            v->my_posts = get_check(dlg, IDC_CF_MY_POSTS);
            v->my_replies = get_check(dlg, IDC_CF_MY_REPLIES);
            v->text = field_text(dlg, IDC_CF_TEXT);
            EndDialog(dlg, static_cast<INT_PTR>(ClientFilterAction::Apply));
            return TRUE;
        }
        case IDC_CF_CLEAR:
            EndDialog(dlg, static_cast<INT_PTR>(ClientFilterAction::Clear));
            return TRUE;
        case IDCANCEL:
            EndDialog(dlg, static_cast<INT_PTR>(ClientFilterAction::Cancel));
            return TRUE;
        }
        break;
    }
    return FALSE;
}

} // namespace

ClientFilterAction show_client_filter_dialog(HWND parent, HINSTANCE inst,
                                             ClientFilterValues& values) {
    const INT_PTR r = DialogBoxParamW(inst, MAKEINTRESOURCEW(IDD_CLIENT_FILTER), parent,
                                      ClientFilterProc, reinterpret_cast<LPARAM>(&values));
    if (r == static_cast<INT_PTR>(ClientFilterAction::Apply))
        return ClientFilterAction::Apply;
    if (r == static_cast<INT_PTR>(ClientFilterAction::Clear))
        return ClientFilterAction::Clear;
    return ClientFilterAction::Cancel;
}

} // namespace fastsmui
