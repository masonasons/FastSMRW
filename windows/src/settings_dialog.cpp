#include "settings_dialog.hpp"

#include <algorithm>
#include <iterator>

#include <commctrl.h>
#include <prsht.h>
#include <shellapi.h>

#include "../resources/resource.h"
#include "utf.hpp"

#include "fastsm/presentation/speech_settings.hpp"
#include "fastsm/store/paths.hpp"

using namespace fastsm;
using fastsm::store::AppSettings;

namespace fastsmui {
namespace {

struct Ctx {
    AppSettings settings;
    std::vector<std::string> soundpacks;
    bool applied = false;
};

Ctx* ctx_of(HWND dlg) { return reinterpret_cast<Ctx*>(GetWindowLongPtrW(dlg, DWLP_USER)); }

Ctx* on_init(HWND dlg, LPARAM lp) {
    auto* psp = reinterpret_cast<PROPSHEETPAGEW*>(lp);
    Ctx* ctx = reinterpret_cast<Ctx*>(psp->lParam);
    SetWindowLongPtrW(dlg, DWLP_USER, reinterpret_cast<LONG_PTR>(ctx));
    return ctx;
}

bool is_apply(LPARAM lp) {
    return reinterpret_cast<NMHDR*>(lp)->code == PSN_APPLY;
}

void checked(HWND dlg, int id, bool on) {
    CheckDlgButton(dlg, id, on ? BST_CHECKED : BST_UNCHECKED);
}
bool is_checked(HWND dlg, int id) { return IsDlgButtonChecked(dlg, id) == BST_CHECKED; }

INT_PTR CALLBACK GeneralProc(HWND dlg, UINT msg, WPARAM, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG:
        // Checked = use Ctrl+Return to send (enter_to_send == false).
        checked(dlg, IDC_SET_CMDRETURN, !on_init(dlg, lp)->settings.enter_to_send);
        return TRUE;
    case WM_NOTIFY:
        if (is_apply(lp)) {
            ctx_of(dlg)->settings.enter_to_send = !is_checked(dlg, IDC_SET_CMDRETURN);
            ctx_of(dlg)->applied = true;
            SetWindowLongPtrW(dlg, DWLP_MSGRESULT, PSNRET_NOERROR);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

std::wstring auto_refresh_label(int secs) {
    switch (secs) {
    case 0:
        return L"Off";
    case 30:
        return L"Every 30 seconds";
    case 60:
        return L"Every minute";
    case 120:
        return L"Every 2 minutes";
    case 300:
        return L"Every 5 minutes";
    default:
        return L"Every " + std::to_wstring(secs) + L" seconds";
    }
}

INT_PTR CALLBACK TimelinesProc(HWND dlg, UINT msg, WPARAM, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        Ctx* ctx = on_init(dlg, lp);
        HWND spin = GetDlgItem(dlg, IDC_SET_CACHELIMIT_SPIN);
        SendMessageW(spin, UDM_SETRANGE32, AppSettings::kCacheLimitMin, AppSettings::kCacheLimitMax);
        SendMessageW(spin, UDM_SETPOS32, 0, ctx->settings.cache_limit);

        HWND combo = GetDlgItem(dlg, IDC_SET_AUTOREFRESH);
        int sel = 0, i = 0;
        for (int secs : AppSettings::kAutoRefreshOptions) {
            SendMessageW(combo, CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(auto_refresh_label(secs).c_str()));
            if (secs == ctx->settings.auto_refresh_seconds)
                sel = i;
            ++i;
        }
        SendMessageW(combo, CB_SETCURSEL, sel, 0);
        checked(dlg, IDC_SET_STREAMING, ctx->settings.streaming_enabled);
        return TRUE;
    }
    case WM_NOTIFY:
        if (is_apply(lp)) {
            Ctx* ctx = ctx_of(dlg);
            int v = static_cast<int>(GetDlgItemInt(dlg, IDC_SET_CACHELIMIT, nullptr, FALSE));
            v = std::clamp(v, AppSettings::kCacheLimitMin, AppSettings::kCacheLimitMax);
            ctx->settings.cache_limit = v;
            const int sel =
                static_cast<int>(SendDlgItemMessageW(dlg, IDC_SET_AUTOREFRESH, CB_GETCURSEL, 0, 0));
            const int n = static_cast<int>(std::size(AppSettings::kAutoRefreshOptions));
            if (sel >= 0 && sel < n)
                ctx->settings.auto_refresh_seconds = AppSettings::kAutoRefreshOptions[sel];
            ctx->settings.streaming_enabled = is_checked(dlg, IDC_SET_STREAMING);
            ctx->applied = true;
            SetWindowLongPtrW(dlg, DWLP_MSGRESULT, PSNRET_NOERROR);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

INT_PTR CALLBACK AudioProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        Ctx* ctx = on_init(dlg, lp);
        checked(dlg, IDC_SET_SOUNDS, ctx->settings.sounds_enabled);
        HWND combo = GetDlgItem(dlg, IDC_SET_SOUNDPACK);
        int sel = 0;
        for (size_t i = 0; i < ctx->soundpacks.size(); ++i) {
            SendMessageW(combo, CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(to_wide(ctx->soundpacks[i]).c_str()));
            if (ctx->soundpacks[i] == ctx->settings.soundpack)
                sel = static_cast<int>(i);
        }
        SendMessageW(combo, CB_SETCURSEL, sel, 0);
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_SET_OPENPACKS) {
            const std::wstring dir = (store::config_dir() / L"soundpacks").wstring();
            ShellExecuteW(dlg, L"explore", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return TRUE;
        }
        break;
    case WM_NOTIFY:
        if (is_apply(lp)) {
            Ctx* ctx = ctx_of(dlg);
            ctx->settings.sounds_enabled = is_checked(dlg, IDC_SET_SOUNDS);
            const int sel = static_cast<int>(
                SendDlgItemMessageW(dlg, IDC_SET_SOUNDPACK, CB_GETCURSEL, 0, 0));
            if (sel >= 0 && sel < static_cast<int>(ctx->soundpacks.size()))
                ctx->settings.soundpack = ctx->soundpacks[static_cast<size_t>(sel)];
            ctx->applied = true;
            SetWindowLongPtrW(dlg, DWLP_MSGRESULT, PSNRET_NOERROR);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

// --- Speech page: a checked, reorderable list of post fields ---

void swap_speech_items(HWND list, int a, int b) {
    wchar_t ta[128] = {}, tb[128] = {};
    ListView_GetItemText(list, a, 0, ta, 128);
    ListView_GetItemText(list, b, 0, tb, 128);
    LVITEMW ia{}, ib{};
    ia.mask = ib.mask = LVIF_PARAM;
    ia.iItem = a;
    ib.iItem = b;
    ListView_GetItem(list, &ia);
    ListView_GetItem(list, &ib);
    const bool ca = ListView_GetCheckState(list, a) != 0;
    const bool cb = ListView_GetCheckState(list, b) != 0;

    ListView_SetItemText(list, a, 0, tb);
    ListView_SetItemText(list, b, 0, ta);
    LVITEMW sa{};
    sa.mask = LVIF_PARAM;
    sa.iItem = a;
    sa.lParam = ib.lParam;
    LVITEMW sb{};
    sb.mask = LVIF_PARAM;
    sb.iItem = b;
    sb.lParam = ia.lParam;
    ListView_SetItem(list, &sa);
    ListView_SetItem(list, &sb);
    ListView_SetCheckState(list, a, cb);
    ListView_SetCheckState(list, b, ca);
}

void move_speech(HWND dlg, int delta) {
    HWND list = GetDlgItem(dlg, IDC_SET_SPEECH_LIST);
    const int sel = ListView_GetNextItem(list, -1, LVNI_SELECTED);
    const int n = ListView_GetItemCount(list);
    const int target = sel + delta;
    if (sel < 0 || target < 0 || target >= n)
        return;
    swap_speech_items(list, sel, target);
    ListView_SetItemState(list, target, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_EnsureVisible(list, target, FALSE);
}

// Ctrl+Up / Ctrl+Down reorder the focused field (and suppress the default
// focus-rectangle move).
LRESULT CALLBACK SpeechListProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    if (msg == WM_KEYDOWN && (wp == VK_UP || wp == VK_DOWN) &&
        (GetKeyState(VK_CONTROL) & 0x8000)) {
        move_speech(GetParent(h), wp == VK_UP ? -1 : +1);
        return 0;
    }
    return DefSubclassProc(h, msg, wp, lp);
}

INT_PTR CALLBACK SpeechProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        Ctx* ctx = on_init(dlg, lp);
        HWND list = GetDlgItem(dlg, IDC_SET_SPEECH_LIST);
        ListView_SetExtendedListViewStyle(list, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT);
        LVCOLUMNW col{};
        col.mask = LVCF_WIDTH;
        col.cx = 150;
        ListView_InsertColumn(list, 0, &col);
        int row = 0;
        for (const auto& item : ctx->settings.speech.status) {
            LVITEMW lv{};
            lv.mask = LVIF_TEXT | LVIF_PARAM;
            lv.iItem = row;
            const std::wstring name = to_wide(present::field_display_name(item.field));
            lv.pszText = const_cast<wchar_t*>(name.c_str());
            lv.lParam = static_cast<LPARAM>(item.field);
            ListView_InsertItem(list, &lv);
            ListView_SetCheckState(list, row, item.enabled);
            ++row;
        }
        if (row > 0)
            ListView_SetItemState(list, 0, LVIS_SELECTED | LVIS_FOCUSED,
                                  LVIS_SELECTED | LVIS_FOCUSED);
        SetWindowSubclass(list, SpeechListProc, 1, 0);
        return TRUE;
    }
    case WM_DESTROY:
        RemoveWindowSubclass(GetDlgItem(dlg, IDC_SET_SPEECH_LIST), SpeechListProc, 1);
        break;
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_SET_SPEECH_UP) {
            move_speech(dlg, -1);
            return TRUE;
        }
        if (LOWORD(wp) == IDC_SET_SPEECH_DOWN) {
            move_speech(dlg, +1);
            return TRUE;
        }
        break;
    case WM_NOTIFY:
        if (is_apply(lp)) {
            Ctx* ctx = ctx_of(dlg);
            HWND list = GetDlgItem(dlg, IDC_SET_SPEECH_LIST);
            const int n = ListView_GetItemCount(list);
            std::vector<present::SpeechItem<present::StatusSpeechField>> items;
            for (int i = 0; i < n; ++i) {
                LVITEMW lv{};
                lv.mask = LVIF_PARAM;
                lv.iItem = i;
                ListView_GetItem(list, &lv);
                items.push_back({static_cast<present::StatusSpeechField>(lv.lParam),
                                 ListView_GetCheckState(list, i) != 0});
            }
            ctx->settings.speech.status = std::move(items);
            ctx->applied = true;
            SetWindowLongPtrW(dlg, DWLP_MSGRESULT, PSNRET_NOERROR);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

INT_PTR CALLBACK AdvancedProc(HWND dlg, UINT msg, WPARAM, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        Ctx* ctx = on_init(dlg, lp);
        HWND spin = GetDlgItem(dlg, IDC_SET_FETCHPAGES_SPIN);
        SendMessageW(spin, UDM_SETRANGE32, AppSettings::kFetchPagesMin, AppSettings::kFetchPagesMax);
        SendMessageW(spin, UDM_SETPOS32, 0, ctx->settings.fetch_pages);
        return TRUE;
    }
    case WM_NOTIFY:
        if (is_apply(lp)) {
            int v = static_cast<int>(GetDlgItemInt(dlg, IDC_SET_FETCHPAGES, nullptr, FALSE));
            v = std::clamp(v, AppSettings::kFetchPagesMin, AppSettings::kFetchPagesMax);
            ctx_of(dlg)->settings.fetch_pages = v;
            ctx_of(dlg)->applied = true;
            SetWindowLongPtrW(dlg, DWLP_MSGRESULT, PSNRET_NOERROR);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

INT_PTR CALLBACK ConfirmProc(HWND dlg, UINT msg, WPARAM, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        Ctx* ctx = on_init(dlg, lp);
        checked(dlg, IDC_SET_CONFIRM_BOOST, ctx->settings.confirm_boost);
        checked(dlg, IDC_SET_CONFIRM_FAV, ctx->settings.confirm_favorite);
        checked(dlg, IDC_SET_CONFIRM_CLEAR, ctx->settings.confirm_clear_timeline);
        checked(dlg, IDC_SET_CONFIRM_BLOCK, ctx->settings.confirm_block);
        return TRUE;
    }
    case WM_NOTIFY:
        if (is_apply(lp)) {
            Ctx* ctx = ctx_of(dlg);
            ctx->settings.confirm_boost = is_checked(dlg, IDC_SET_CONFIRM_BOOST);
            ctx->settings.confirm_favorite = is_checked(dlg, IDC_SET_CONFIRM_FAV);
            ctx->settings.confirm_clear_timeline = is_checked(dlg, IDC_SET_CONFIRM_CLEAR);
            ctx->settings.confirm_block = is_checked(dlg, IDC_SET_CONFIRM_BLOCK);
            ctx->applied = true;
            SetWindowLongPtrW(dlg, DWLP_MSGRESULT, PSNRET_NOERROR);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

PROPSHEETPAGEW make_page(HINSTANCE inst, int dlg, DLGPROC proc, Ctx* ctx) {
    PROPSHEETPAGEW page{};
    page.dwSize = sizeof(page);
    page.hInstance = inst;
    page.pszTemplate = MAKEINTRESOURCEW(dlg);
    page.pfnDlgProc = proc;
    page.lParam = reinterpret_cast<LPARAM>(ctx);
    return page;
}

} // namespace

std::optional<AppSettings> show_settings_dialog(HWND parent, HINSTANCE inst,
                                                const AppSettings& current,
                                                const std::vector<std::string>& soundpacks) {
    Ctx ctx;
    ctx.settings = current;
    ctx.soundpacks = soundpacks;

    PROPSHEETPAGEW pages[] = {
        make_page(inst, IDD_SET_GENERAL, GeneralProc, &ctx),
        make_page(inst, IDD_SET_TIMELINES, TimelinesProc, &ctx),
        make_page(inst, IDD_SET_AUDIO, AudioProc, &ctx),
        make_page(inst, IDD_SET_SPEECH, SpeechProc, &ctx),
        make_page(inst, IDD_SET_ADVANCED, AdvancedProc, &ctx),
        make_page(inst, IDD_SET_CONFIRM, ConfirmProc, &ctx),
    };

    PROPSHEETHEADERW hdr{};
    hdr.dwSize = sizeof(hdr);
    hdr.dwFlags = PSH_PROPSHEETPAGE | PSH_NOAPPLYNOW;
    hdr.hwndParent = parent;
    hdr.hInstance = inst;
    hdr.pszCaption = L"Settings";
    hdr.nPages = static_cast<UINT>(std::size(pages));
    hdr.ppsp = pages;

    PropertySheetW(&hdr);
    if (ctx.applied)
        return ctx.settings;
    return std::nullopt;
}

} // namespace fastsmui
