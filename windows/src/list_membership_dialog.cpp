#include "list_membership_dialog.hpp"

#include <commctrl.h>

#include "../resources/resource.h"

namespace fastsmui {
namespace {

struct Ctx {
    const std::wstring* heading;
    std::vector<ListMembershipItem>* items;
    bool ok = false;
};

INT_PTR CALLBACK Proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        auto* c = reinterpret_cast<Ctx*>(lp);
        SetWindowLongPtrW(dlg, DWLP_USER, reinterpret_cast<LONG_PTR>(c));
        SetDlgItemTextW(dlg, IDC_LM_HEADING, c->heading->c_str());
        HWND lv = GetDlgItem(dlg, IDC_LM_LIST);
        ListView_SetExtendedListViewStyle(lv, LVS_EX_CHECKBOXES);
        for (size_t i = 0; i < c->items->size(); ++i) {
            LVITEMW it{};
            it.mask = LVIF_TEXT;
            it.iItem = static_cast<int>(i);
            it.pszText = const_cast<wchar_t*>((*c->items)[i].title.c_str());
            ListView_InsertItem(lv, &it);
            ListView_SetCheckState(lv, static_cast<int>(i), (*c->items)[i].member);
        }
        SetFocus(lv);
        return FALSE; // focus set explicitly
    }
    case WM_COMMAND: {
        auto* c = reinterpret_cast<Ctx*>(GetWindowLongPtrW(dlg, DWLP_USER));
        switch (LOWORD(wp)) {
        case IDOK: {
            if (c) {
                HWND lv = GetDlgItem(dlg, IDC_LM_LIST);
                for (size_t i = 0; i < c->items->size(); ++i)
                    (*c->items)[i].member = ListView_GetCheckState(lv, static_cast<int>(i)) != 0;
                c->ok = true;
            }
            EndDialog(dlg, 1);
            return TRUE;
        }
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

std::optional<std::vector<ListMembershipItem>>
show_list_membership_dialog(HWND parent, HINSTANCE inst, const std::wstring& heading,
                            std::vector<ListMembershipItem> items) {
    Ctx ctx{&heading, &items, false};
    DialogBoxParamW(inst, MAKEINTRESOURCEW(IDD_LIST_MEMBERSHIP), parent, Proc,
                    reinterpret_cast<LPARAM>(&ctx));
    if (!ctx.ok)
        return std::nullopt;
    return items;
}

} // namespace fastsmui
