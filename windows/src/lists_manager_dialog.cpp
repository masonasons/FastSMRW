#include "lists_manager_dialog.hpp"

#include <commctrl.h>

#include <array>
#include <string>

#include "../resources/resource.h"
#include "utf.hpp"

using nlohmann::json;

namespace fastsmui {
namespace {

// Replies-policy combo <-> Mastodon token, in display order.
constexpr std::array<const char*, 3> kRepliesTokens = {"none", "list", "followed"};
constexpr std::array<const wchar_t*, 3> kRepliesLabels = {
    L"No one", L"List members only", L"People you follow"};

int replies_index(const std::string& token) {
    for (int i = 0; i < static_cast<int>(kRepliesTokens.size()); ++i)
        if (token == kRepliesTokens[static_cast<size_t>(i)])
            return i;
    return 1; // default "list"
}

// The create/edit sub-dialog. lp = json* prefilled (empty for New).
INT_PTR CALLBACK EditProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        SetWindowLongPtrW(dlg, DWLP_USER, lp);
        auto* j = reinterpret_cast<json*>(lp);
        const bool editing = j->contains("id");
        SetWindowTextW(dlg, editing ? L"Edit List" : L"New List");
        HWND combo = GetDlgItem(dlg, IDC_LSE_REPLIES);
        for (const wchar_t* label : kRepliesLabels)
            SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
        SendMessageW(combo, CB_SETCURSEL,
                     static_cast<WPARAM>(replies_index(j->value("replies_policy", std::string("list")))),
                     0);
        SetDlgItemTextW(dlg, IDC_LSE_NAME, to_wide(j->value("title", std::string{})).c_str());
        CheckDlgButton(dlg, IDC_LSE_EXCLUSIVE, j->value("exclusive", false) ? BST_CHECKED : BST_UNCHECKED);
        SetFocus(GetDlgItem(dlg, IDC_LSE_NAME));
        return FALSE;
    }
    case WM_COMMAND: {
        auto* j = reinterpret_cast<json*>(GetWindowLongPtrW(dlg, DWLP_USER));
        if (LOWORD(wp) == IDOK) {
            wchar_t buf[256] = {0};
            GetDlgItemTextW(dlg, IDC_LSE_NAME, buf, 256);
            std::wstring name = buf;
            // Trim whitespace; require a non-empty name.
            const size_t b = name.find_first_not_of(L" \t");
            const size_t e = name.find_last_not_of(L" \t");
            name = (b == std::wstring::npos) ? std::wstring{} : name.substr(b, e - b + 1);
            if (name.empty()) {
                MessageBoxW(dlg, L"Please enter a name for the list.", L"List", MB_OK | MB_ICONINFORMATION);
                return TRUE;
            }
            int ri = static_cast<int>(SendMessageW(GetDlgItem(dlg, IDC_LSE_REPLIES), CB_GETCURSEL, 0, 0));
            if (ri < 0 || ri >= static_cast<int>(kRepliesTokens.size()))
                ri = 1;
            json out;
            if (const auto id = j->find("id"); id != j->end() && id->is_string())
                out["id"] = *id; // editing keeps the id
            out["title"] = to_utf8(name);
            out["replies_policy"] = kRepliesTokens[static_cast<size_t>(ri)];
            out["exclusive"] = IsDlgButtonChecked(dlg, IDC_LSE_EXCLUSIVE) == BST_CHECKED;
            *j = std::move(out);
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

bool show_list_edit_dialog(HWND parent, HINSTANCE inst, json& list) {
    return DialogBoxParamW(inst, MAKEINTRESOURCEW(IDD_LIST_EDIT), parent, EditProc,
                           reinterpret_cast<LPARAM>(&list)) == IDOK;
}

// --- Manager dialog ---

ListsManagerDialog::ListsManagerDialog(HINSTANCE inst, std::function<void(const json&)> dispatch)
    : inst_(inst), dispatch_(std::move(dispatch)) {}

void ListsManagerDialog::run(HWND parent, const json& initial) {
    if (initial.contains("lists") && initial["lists"].is_array())
        lists_ = initial["lists"];
    DialogBoxParamW(inst_, MAKEINTRESOURCEW(IDD_LISTS_MANAGER), parent, &ListsManagerDialog::proc,
                    reinterpret_cast<LPARAM>(this));
}

void ListsManagerDialog::on_lists(const json& e) {
    if (e.contains("lists") && e["lists"].is_array())
        lists_ = e["lists"];
    if (dlg_) {
        refresh_list();
        update_enabled();
    }
}

INT_PTR CALLBACK ListsManagerDialog::proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_INITDIALOG)
        SetWindowLongPtrW(dlg, DWLP_USER, lp);
    auto* self = reinterpret_cast<ListsManagerDialog*>(GetWindowLongPtrW(dlg, DWLP_USER));
    return self ? self->handle(dlg, msg, wp, lp) : FALSE;
}

INT_PTR ListsManagerDialog::handle(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        dlg_ = dlg;
        refresh_list();
        update_enabled();
        SetFocus(GetDlgItem(dlg, IDC_LSM_LIST));
        return FALSE;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_LSM_OPEN:
            do_open();
            return TRUE;
        case IDC_LSM_NEW:
            do_new();
            return TRUE;
        case IDC_LSM_EDIT:
            do_edit();
            return TRUE;
        case IDC_LSM_DELETE:
            do_delete();
            return TRUE;
        case IDCANCEL:
            dlg_ = nullptr;
            EndDialog(dlg, 0);
            return TRUE;
        }
        break;
    case WM_NOTIFY:
        if (reinterpret_cast<LPNMHDR>(lp)->idFrom == IDC_LSM_LIST) {
            auto* nm = reinterpret_cast<LPNMLISTVIEW>(lp);
            if (nm->hdr.code == LVN_ITEMCHANGED)
                update_enabled();
        }
        break;
    }
    return FALSE;
}

void ListsManagerDialog::refresh_list() {
    HWND list = GetDlgItem(dlg_, IDC_LSM_LIST);
    ListView_DeleteAllItems(list);
    int i = 0;
    for (const auto& l : lists_) {
        std::wstring title = to_wide(l.value("title", std::string{}));
        LVITEMW it{};
        it.mask = LVIF_TEXT;
        it.iItem = i;
        it.pszText = title.data();
        ListView_InsertItem(list, &it);
        ++i;
    }
    if (!lists_.empty())
        ListView_SetItemState(list, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
}

void ListsManagerDialog::update_enabled() {
    const bool has_sel = selected_index() >= 0;
    EnableWindow(GetDlgItem(dlg_, IDC_LSM_OPEN), has_sel);
    EnableWindow(GetDlgItem(dlg_, IDC_LSM_EDIT), has_sel);
    EnableWindow(GetDlgItem(dlg_, IDC_LSM_DELETE), has_sel);
}

int ListsManagerDialog::selected_index() const {
    return ListView_GetNextItem(GetDlgItem(dlg_, IDC_LSM_LIST), -1, LVNI_SELECTED);
}

void ListsManagerDialog::do_open() {
    const int idx = selected_index();
    if (idx < 0 || idx >= static_cast<int>(lists_.size()))
        return;
    const json& l = lists_[static_cast<size_t>(idx)];
    dispatch_({{"cmd", "spawn_timeline"}, {"kind", "list"}, {"param", l.value("id", std::string{})}});
    HWND dlg = dlg_;
    dlg_ = nullptr;
    EndDialog(dlg, 0); // close the manager; the timeline opens behind it
}

void ListsManagerDialog::do_new() {
    json l = json::object();
    if (show_list_edit_dialog(dlg_, inst_, l))
        dispatch_({{"cmd", "create_list"},
                   {"title", l.value("title", std::string{})},
                   {"replies_policy", l.value("replies_policy", std::string("list"))},
                   {"exclusive", l.value("exclusive", false)}});
}

void ListsManagerDialog::do_edit() {
    const int idx = selected_index();
    if (idx < 0 || idx >= static_cast<int>(lists_.size()))
        return;
    json l = lists_[static_cast<size_t>(idx)];
    if (show_list_edit_dialog(dlg_, inst_, l))
        dispatch_({{"cmd", "rename_list"},
                   {"id", l.value("id", std::string{})},
                   {"title", l.value("title", std::string{})},
                   {"replies_policy", l.value("replies_policy", std::string("list"))},
                   {"exclusive", l.value("exclusive", false)}});
}

void ListsManagerDialog::do_delete() {
    const int idx = selected_index();
    if (idx < 0 || idx >= static_cast<int>(lists_.size()))
        return;
    const json& l = lists_[static_cast<size_t>(idx)];
    std::wstring msg = L"Delete the list \"" + to_wide(l.value("title", std::string{})) + L"\"?";
    if (MessageBoxW(dlg_, msg.c_str(), L"Lists", MB_YESNO | MB_ICONQUESTION) != IDYES)
        return;
    dispatch_({{"cmd", "delete_list"}, {"id", l.value("id", std::string{})}});
}

} // namespace fastsmui
