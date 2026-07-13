#include "aliases_dialog.hpp"

#include <commctrl.h>

#include <string>
#include <vector>

#include "../resources/resource.h"
#include "utf.hpp"

using nlohmann::json;

namespace fastsmui {
namespace {

std::wstring trim(std::wstring s) {
    const size_t b = s.find_first_not_of(L" \t");
    const size_t e = s.find_last_not_of(L" \t");
    return (b == std::wstring::npos) ? std::wstring{} : s.substr(b, e - b + 1);
}

// --- Add/edit alias prompt ---

struct PromptCtx {
    std::wstring handle;
    std::wstring current;
    std::optional<std::string> result; // set on OK (may be empty = clear)
};

INT_PTR CALLBACK PromptProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        SetWindowLongPtrW(dlg, DWLP_USER, lp);
        auto* c = reinterpret_cast<PromptCtx*>(lp);
        std::wstring label = L"&Alias for @" + c->handle + L" (leave blank to remove):";
        SetDlgItemTextW(dlg, IDC_ALIAS_LABEL, label.c_str());
        HWND edit = GetDlgItem(dlg, IDC_ALIAS_EDIT);
        SetWindowTextW(edit, c->current.c_str());
        SendMessageW(edit, EM_SETSEL, 0, -1);
        SetFocus(edit);
        return FALSE; // focus set explicitly
    }
    case WM_COMMAND: {
        auto* c = reinterpret_cast<PromptCtx*>(GetWindowLongPtrW(dlg, DWLP_USER));
        if (LOWORD(wp) == IDOK) {
            wchar_t buf[256] = {0};
            GetDlgItemTextW(dlg, IDC_ALIAS_EDIT, buf, 256);
            if (c)
                c->result = to_utf8(trim(buf)); // empty string = clear the alias
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

std::optional<std::string> show_alias_dialog(HWND parent, HINSTANCE inst, const std::string& handle,
                                             const std::string& current) {
    PromptCtx ctx{to_wide(handle), to_wide(current), std::nullopt};
    DialogBoxParamW(inst, MAKEINTRESOURCEW(IDD_ALIAS_PROMPT), parent, PromptProc,
                    reinterpret_cast<LPARAM>(&ctx));
    return ctx.result;
}

// --- Aliases manager ---

AliasesManagerDialog::AliasesManagerDialog(HINSTANCE inst, std::function<void(const json&)> dispatch)
    : inst_(inst), dispatch_(std::move(dispatch)) {}

void AliasesManagerDialog::run(HWND parent, const json& initial) {
    if (initial.contains("aliases") && initial["aliases"].is_array())
        aliases_ = initial["aliases"];
    DialogBoxParamW(inst_, MAKEINTRESOURCEW(IDD_ALIASES_MANAGER), parent,
                    &AliasesManagerDialog::proc, reinterpret_cast<LPARAM>(this));
}

void AliasesManagerDialog::on_aliases(const json& e) {
    if (e.contains("aliases") && e["aliases"].is_array())
        aliases_ = e["aliases"];
    if (dlg_) {
        refresh_list();
        update_enabled();
    }
}

INT_PTR CALLBACK AliasesManagerDialog::proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_INITDIALOG)
        SetWindowLongPtrW(dlg, DWLP_USER, lp);
    auto* self = reinterpret_cast<AliasesManagerDialog*>(GetWindowLongPtrW(dlg, DWLP_USER));
    return self ? self->handle(dlg, msg, wp, lp) : FALSE;
}

INT_PTR AliasesManagerDialog::handle(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        dlg_ = dlg;
        HWND list = GetDlgItem(dlg, IDC_ALM_LIST);
        ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT);
        LVCOLUMNW col{};
        col.mask = LVCF_TEXT | LVCF_WIDTH;
        std::wstring c0 = L"Alias";
        col.pszText = c0.data();
        col.cx = 120;
        ListView_InsertColumn(list, 0, &col);
        std::wstring c1 = L"User";
        col.pszText = c1.data();
        col.cx = 116;
        ListView_InsertColumn(list, 1, &col);
        refresh_list();
        update_enabled();
        SetFocus(list);
        return FALSE;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_ALM_EDIT:
            do_edit();
            return TRUE;
        case IDC_ALM_REMOVE:
            do_remove();
            return TRUE;
        case IDCANCEL:
            dlg_ = nullptr;
            EndDialog(dlg, 0);
            return TRUE;
        }
        break;
    case WM_NOTIFY:
        if (reinterpret_cast<LPNMHDR>(lp)->idFrom == IDC_ALM_LIST) {
            auto* nm = reinterpret_cast<LPNMLISTVIEW>(lp);
            if (nm->hdr.code == LVN_ITEMCHANGED)
                update_enabled();
            else if (nm->hdr.code == NM_DBLCLK)
                do_edit();
        }
        break;
    }
    return FALSE;
}

void AliasesManagerDialog::refresh_list() {
    HWND list = GetDlgItem(dlg_, IDC_ALM_LIST);
    const int keep = selected_index();
    ListView_DeleteAllItems(list);
    int i = 0;
    for (const auto& a : aliases_) {
        std::wstring alias = to_wide(a.value("alias", std::string{}));
        LVITEMW it{};
        it.mask = LVIF_TEXT;
        it.iItem = i;
        it.pszText = alias.data();
        ListView_InsertItem(list, &it);
        std::wstring handle = L"@" + to_wide(a.value("handle", std::string{}));
        ListView_SetItemText(list, i, 1, handle.data());
        ++i;
    }
    const int count = static_cast<int>(aliases_.size());
    if (count > 0) {
        int sel = keep < 0 ? 0 : (keep >= count ? count - 1 : keep);
        ListView_SetItemState(list, sel, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    }
}

void AliasesManagerDialog::update_enabled() {
    const bool has_sel = selected_index() >= 0;
    EnableWindow(GetDlgItem(dlg_, IDC_ALM_EDIT), has_sel);
    EnableWindow(GetDlgItem(dlg_, IDC_ALM_REMOVE), has_sel);
}

int AliasesManagerDialog::selected_index() const {
    return ListView_GetNextItem(GetDlgItem(dlg_, IDC_ALM_LIST), -1, LVNI_SELECTED);
}

void AliasesManagerDialog::do_edit() {
    const int idx = selected_index();
    if (idx < 0 || idx >= static_cast<int>(aliases_.size()))
        return;
    const auto& a = aliases_[static_cast<size_t>(idx)];
    const std::string key = a.value("key", std::string{});
    const std::string handle = a.value("handle", std::string{});
    if (key.empty())
        return;
    auto result = show_alias_dialog(dlg_, inst_, handle, a.value("alias", std::string{}));
    if (!result)
        return; // cancelled
    if (result->empty())
        dispatch_({{"cmd", "clear_alias"}, {"key", key}, {"handle", handle}});
    else
        dispatch_({{"cmd", "set_alias"}, {"key", key}, {"handle", handle}, {"alias", *result}});
    dispatch_({{"cmd", "list_aliases"}}); // refresh this manager (runs after the change)
}

void AliasesManagerDialog::do_remove() {
    const int idx = selected_index();
    if (idx < 0 || idx >= static_cast<int>(aliases_.size()))
        return;
    const auto& a = aliases_[static_cast<size_t>(idx)];
    const std::string key = a.value("key", std::string{});
    if (key.empty())
        return;
    dispatch_({{"cmd", "clear_alias"}, {"key", key}, {"handle", a.value("handle", std::string{})}});
    dispatch_({{"cmd", "list_aliases"}}); // refresh the list after the removal
}

} // namespace fastsmui
