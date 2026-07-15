#include "user_analysis_dialog.hpp"

#include <array>

#include "../resources/resource.h"

namespace fastsmui {
namespace {

// The analyses offered, in list order. `label` is what the user sees; `category`
// is the id sent to the core. Keep in sync with the Mac/Android pickers and the
// categories handled in CoreSession::cmd_analyze_users.
struct Analysis {
    const wchar_t* label;
    const char* category;
};
constexpr std::array<Analysis, 3> kAnalyses = {{
    {L"People who follow you that you don't follow back", "not_following_back"},
    {L"People you follow who don't follow you back", "no_followback"},
    {L"Mutual follows (you both follow each other)", "mutuals"},
}};

INT_PTR CALLBACK proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        SetWindowLongPtrW(dlg, DWLP_USER, static_cast<LONG_PTR>(lp));
        HWND list = GetDlgItem(dlg, IDC_UA_LIST);
        for (const auto& a : kAnalyses)
            SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(a.label));
        SendMessageW(list, LB_SETCURSEL, 0, 0);
        SetFocus(list);
        return FALSE;
    }
    case WM_COMMAND: {
        // Double-clicking an item runs it.
        if (LOWORD(wp) == IDC_UA_LIST && HIWORD(wp) == LBN_DBLCLK) {
            SendMessageW(dlg, WM_COMMAND, IDOK, 0);
            return TRUE;
        }
        if (LOWORD(wp) == IDOK) {
            const int sel =
                static_cast<int>(SendMessageW(GetDlgItem(dlg, IDC_UA_LIST), LB_GETCURSEL, 0, 0));
            if (sel < 0 || sel >= static_cast<int>(kAnalyses.size())) {
                MessageBeep(MB_ICONWARNING);
                return TRUE;
            }
            *reinterpret_cast<int*>(GetWindowLongPtrW(dlg, DWLP_USER)) = sel;
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

std::optional<std::string> show_user_analysis_dialog(HWND parent, HINSTANCE inst) {
    int chosen = -1;
    const INT_PTR r = DialogBoxParamW(inst, MAKEINTRESOURCEW(IDD_USER_ANALYSIS), parent, proc,
                                      reinterpret_cast<LPARAM>(&chosen));
    if (r == IDOK && chosen >= 0 && chosen < static_cast<int>(kAnalyses.size()))
        return std::string(kAnalyses[static_cast<size_t>(chosen)].category);
    return std::nullopt;
}

} // namespace fastsmui
