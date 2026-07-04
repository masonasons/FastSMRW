#include "account_settings_dialog.hpp"

#include <shellapi.h>

#include "../resources/resource.h"
#include "fastsm/store/paths.hpp"
#include "utf.hpp"

namespace fastsmui {
namespace {

struct AccountSettingsCtx {
    std::wstring title;
    const std::vector<std::string>* packs;
    std::string current;         // effective pack, to pre-select
    std::optional<std::string> result;
};

INT_PTR CALLBACK proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        SetWindowLongPtrW(dlg, DWLP_USER, lp);
        auto* c = reinterpret_cast<AccountSettingsCtx*>(lp);
        SetWindowTextW(dlg, c->title.c_str());
        HWND list = GetDlgItem(dlg, IDC_AS_SOUNDPACK);
        int sel = 0;
        for (size_t i = 0; i < c->packs->size(); ++i) {
            SendMessageW(list, LB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(to_wide((*c->packs)[i]).c_str()));
            if ((*c->packs)[i] == c->current)
                sel = static_cast<int>(i);
        }
        SendMessageW(list, LB_SETCURSEL, sel, 0);
        SetFocus(list);
        return FALSE; // focus set explicitly
    }
    case WM_COMMAND: {
        auto* c = reinterpret_cast<AccountSettingsCtx*>(GetWindowLongPtrW(dlg, DWLP_USER));
        switch (LOWORD(wp)) {
        case IDC_AS_OPENPACKS: {
            const std::wstring dir = (fastsm::store::config_dir() / L"soundpacks").wstring();
            ShellExecuteW(dlg, L"explore", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return TRUE;
        }
        case IDOK: {
            const int sel = static_cast<int>(
                SendDlgItemMessageW(dlg, IDC_AS_SOUNDPACK, LB_GETCURSEL, 0, 0));
            if (c && sel >= 0 && sel < static_cast<int>(c->packs->size()))
                c->result = (*c->packs)[static_cast<size_t>(sel)];
            EndDialog(dlg, IDOK);
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    }
    return FALSE;
}

} // namespace

std::optional<std::string> show_account_settings_dialog(HWND parent, HINSTANCE inst,
                                                        const std::wstring& title,
                                                        const std::vector<std::string>& packs,
                                                        const std::string& current) {
    AccountSettingsCtx ctx{title, &packs, current, std::nullopt};
    DialogBoxParamW(inst, MAKEINTRESOURCEW(IDD_ACCOUNT_SETTINGS), parent, &proc,
                    reinterpret_cast<LPARAM>(&ctx));
    return ctx.result;
}

} // namespace fastsmui
