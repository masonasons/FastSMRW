#include "settings_dialog.hpp"

#include <algorithm>
#include <functional>
#include <iterator>
#include <map>
#include <utility>

#include <commctrl.h>
#include <prsht.h>
#include <shellapi.h>

#include "../resources/resource.h"
#include "keymap_manager_dialog.hpp"
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
    std::function<void(HWND)> open_manager;
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

// Read an integer from an edit control, tolerating any digit-group separators the
// up-down control inserts (UDS_SETBUDDYINT adds thousands commas, e.g. "20,000",
// which GetDlgItemInt can't parse - that silently reset the setting). Returns
// `fallback` if the field has no digits.
int read_int_field(HWND dlg, int edit_id, int fallback) {
    wchar_t buf[32] = {};
    GetDlgItemTextW(dlg, edit_id, buf, static_cast<int>(std::size(buf)));
    std::wstring digits;
    for (wchar_t c : buf)
        if (c >= L'0' && c <= L'9')
            digits += c;
    if (digits.empty())
        return fallback;
    try {
        return std::stoi(digits);
    } catch (...) {
        return fallback; // overflow / out of range -> keep the existing value
    }
}

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
        checked(dlg, IDC_SET_SHOW_MENTIONS, ctx->settings.show_mentions_in_notifications);
        checked(dlg, IDC_SET_REVERSE, ctx->settings.reverse_timelines);
        checked(dlg, IDC_SET_AUTOLOAD, ctx->settings.auto_load_older);
        return TRUE;
    }
    case WM_NOTIFY:
        if (is_apply(lp)) {
            Ctx* ctx = ctx_of(dlg);
            int v = read_int_field(dlg, IDC_SET_CACHELIMIT, ctx->settings.cache_limit);
            v = std::clamp(v, AppSettings::kCacheLimitMin, AppSettings::kCacheLimitMax);
            ctx->settings.cache_limit = v;
            const int sel =
                static_cast<int>(SendDlgItemMessageW(dlg, IDC_SET_AUTOREFRESH, CB_GETCURSEL, 0, 0));
            const int n = static_cast<int>(std::size(AppSettings::kAutoRefreshOptions));
            if (sel >= 0 && sel < n)
                ctx->settings.auto_refresh_seconds = AppSettings::kAutoRefreshOptions[sel];
            ctx->settings.streaming_enabled = is_checked(dlg, IDC_SET_STREAMING);
            ctx->settings.show_mentions_in_notifications = is_checked(dlg, IDC_SET_SHOW_MENTIONS);
            ctx->settings.reverse_timelines = is_checked(dlg, IDC_SET_REVERSE);
            ctx->settings.auto_load_older = is_checked(dlg, IDC_SET_AUTOLOAD);
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
        checked(dlg, IDC_SET_BOUNDARY, ctx->settings.boundary_sound);
        HWND combo = GetDlgItem(dlg, IDC_SET_SOUNDPACK);
        int sel = 0;
        for (size_t i = 0; i < ctx->soundpacks.size(); ++i) {
            SendMessageW(combo, CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(to_wide(ctx->soundpacks[i]).c_str()));
            if (ctx->soundpacks[i] == ctx->settings.soundpack)
                sel = static_cast<int>(i);
        }
        SendMessageW(combo, CB_SETCURSEL, sel, 0);
        HWND vol = GetDlgItem(dlg, IDC_SET_VOLUME);
        SendMessageW(vol, TBM_SETRANGE, TRUE, MAKELONG(0, 100));
        SendMessageW(vol, TBM_SETPOS, TRUE, std::clamp(ctx->settings.sound_volume, 0, 100));
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
            ctx->settings.boundary_sound = is_checked(dlg, IDC_SET_BOUNDARY);
            ctx->settings.sound_volume =
                static_cast<int>(SendDlgItemMessageW(dlg, IDC_SET_VOLUME, TBM_GETPOS, 0, 0));
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

INT_PTR CALLBACK EarconsProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        Ctx* ctx = on_init(dlg, lp);
        checked(dlg, IDC_SET_EARCON_IMAGE, ctx->settings.earcon_image);
        checked(dlg, IDC_SET_EARCON_MEDIA, ctx->settings.earcon_media);
        checked(dlg, IDC_SET_EARCON_MENTION, ctx->settings.earcon_mention);
        checked(dlg, IDC_SET_EARCON_PINNED, ctx->settings.earcon_pinned);
        checked(dlg, IDC_SET_EARCON_POLL, ctx->settings.earcon_poll);
        return TRUE;
    }
    case WM_NOTIFY:
        if (is_apply(lp)) {
            Ctx* ctx = ctx_of(dlg);
            ctx->settings.earcon_image = is_checked(dlg, IDC_SET_EARCON_IMAGE);
            ctx->settings.earcon_media = is_checked(dlg, IDC_SET_EARCON_MEDIA);
            ctx->settings.earcon_mention = is_checked(dlg, IDC_SET_EARCON_MENTION);
            ctx->settings.earcon_pinned = is_checked(dlg, IDC_SET_EARCON_PINNED);
            ctx->settings.earcon_poll = is_checked(dlg, IDC_SET_EARCON_POLL);
            ctx->applied = true;
            SetWindowLongPtrW(dlg, DWLP_MSGRESULT, PSNRET_NOERROR);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

// --- Reusable "Speech Details" modal: a checked, reorderable field list ---

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

void move_speech_in_list(HWND list, int delta) {
    const int sel = ListView_GetNextItem(list, -1, LVNI_SELECTED);
    const int n = ListView_GetItemCount(list);
    const int target = sel + delta;
    if (sel < 0 || target < 0 || target >= n)
        return;
    swap_speech_items(list, sel, target);
    ListView_SetItemState(list, target, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_EnsureVisible(list, target, FALSE);
}

// Ctrl+Up / Ctrl+Down reorder the focused field.
LRESULT CALLBACK SpeechListProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    if (msg == WM_KEYDOWN && (wp == VK_UP || wp == VK_DOWN) &&
        (GetKeyState(VK_CONTROL) & 0x8000)) {
        move_speech_in_list(h, wp == VK_UP ? -1 : +1);
        return 0;
    }
    return DefSubclassProc(h, msg, wp, lp);
}

// One generic, orderable, toggleable row (id carries the field enum value), with
// optional text wrapped around the field's value when spoken.
struct SpeechRow {
    int id;
    std::wstring label;
    bool enabled;
    std::wstring before;
    std::wstring after;
    bool no_sep_after = false;
};

// Per-item wrap state kept by field id, so it survives list reordering.
struct ItemWrap {
    std::wstring before;
    std::wstring after;
    bool no_sep = false;
};

struct DetailCtx {
    const std::wstring* title;
    std::vector<SpeechRow>* rows;
    std::map<int, ItemWrap> wrap; // field id -> its before/after/no-separator
    int loaded = -1;              // field id currently shown in the edits/checkbox
};

std::wstring dlg_text(HWND dlg, int id) {
    HWND c = GetDlgItem(dlg, id);
    const int len = GetWindowTextLengthW(c);
    std::wstring buf(static_cast<size_t>(len) + 1, L'\0');
    const int got = GetWindowTextW(c, buf.data(), len + 1);
    buf.resize(static_cast<size_t>(got));
    return buf;
}

// Save the before/after edits + no-separator checkbox for the loaded field.
void save_loaded_wrap(HWND dlg, DetailCtx* c) {
    if (c->loaded >= 0)
        c->wrap[c->loaded] = {dlg_text(dlg, IDC_SPEECH_DETAIL_BEFORE),
                              dlg_text(dlg, IDC_SPEECH_DETAIL_AFTER),
                              IsDlgButtonChecked(dlg, IDC_SPEECH_DETAIL_NOSEP) == BST_CHECKED};
}

// Load the selected row's before/after + no-separator into the controls.
void load_sel_wrap(HWND dlg, DetailCtx* c) {
    HWND list = GetDlgItem(dlg, IDC_SPEECH_DETAIL_LIST);
    const int sel = ListView_GetNextItem(list, -1, LVNI_SELECTED);
    if (sel < 0) {
        c->loaded = -1;
        return;
    }
    LVITEMW lv{};
    lv.mask = LVIF_PARAM;
    lv.iItem = sel;
    ListView_GetItem(list, &lv);
    const int id = static_cast<int>(lv.lParam);
    const auto& w = c->wrap[id];
    SetDlgItemTextW(dlg, IDC_SPEECH_DETAIL_BEFORE, w.before.c_str());
    SetDlgItemTextW(dlg, IDC_SPEECH_DETAIL_AFTER, w.after.c_str());
    CheckDlgButton(dlg, IDC_SPEECH_DETAIL_NOSEP, w.no_sep ? BST_CHECKED : BST_UNCHECKED);
    c->loaded = id;
}

INT_PTR CALLBACK SpeechDetailProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        auto* c = reinterpret_cast<DetailCtx*>(lp);
        SetWindowLongPtrW(dlg, DWLP_USER, reinterpret_cast<LONG_PTR>(c));
        SetWindowTextW(dlg, c->title->c_str());
        HWND list = GetDlgItem(dlg, IDC_SPEECH_DETAIL_LIST);
        ListView_SetExtendedListViewStyle(list, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT);
        LVCOLUMNW col{};
        col.mask = LVCF_WIDTH;
        col.cx = 168;
        ListView_InsertColumn(list, 0, &col);
        int row = 0;
        for (const auto& r : *c->rows) {
            LVITEMW lv{};
            lv.mask = LVIF_TEXT | LVIF_PARAM;
            lv.iItem = row;
            lv.pszText = const_cast<wchar_t*>(r.label.c_str());
            lv.lParam = r.id;
            ListView_InsertItem(list, &lv);
            ListView_SetCheckState(list, row, r.enabled);
            c->wrap[r.id] = {r.before, r.after, r.no_sep_after};
            ++row;
        }
        if (row > 0)
            ListView_SetItemState(list, 0, LVIS_SELECTED | LVIS_FOCUSED,
                                  LVIS_SELECTED | LVIS_FOCUSED);
        load_sel_wrap(dlg, c); // show row 0's wrap text
        SetWindowSubclass(list, SpeechListProc, 1, 0);
        SetFocus(list);
        return FALSE;
    }
    case WM_NOTIFY: {
        auto* nm = reinterpret_cast<LPNMHDR>(lp);
        if (nm->idFrom == IDC_SPEECH_DETAIL_LIST && nm->code == LVN_ITEMCHANGED) {
            auto* nv = reinterpret_cast<LPNMLISTVIEW>(lp);
            // Only when a row gains selection: stash the old wrap edits, load new.
            if ((nv->uNewState & LVIS_SELECTED) && !(nv->uOldState & LVIS_SELECTED)) {
                auto* c = reinterpret_cast<DetailCtx*>(GetWindowLongPtrW(dlg, DWLP_USER));
                save_loaded_wrap(dlg, c);
                load_sel_wrap(dlg, c);
            }
        }
        break;
    }
    case WM_DESTROY:
        RemoveWindowSubclass(GetDlgItem(dlg, IDC_SPEECH_DETAIL_LIST), SpeechListProc, 1);
        break;
    case WM_COMMAND: {
        const int id = LOWORD(wp);
        if (id == IDC_SPEECH_DETAIL_UP) {
            move_speech_in_list(GetDlgItem(dlg, IDC_SPEECH_DETAIL_LIST), -1);
            return TRUE;
        }
        if (id == IDC_SPEECH_DETAIL_DOWN) {
            move_speech_in_list(GetDlgItem(dlg, IDC_SPEECH_DETAIL_LIST), +1);
            return TRUE;
        }
        if (id == IDOK) {
            auto* c = reinterpret_cast<DetailCtx*>(GetWindowLongPtrW(dlg, DWLP_USER));
            save_loaded_wrap(dlg, c); // capture the currently-shown edits
            HWND list = GetDlgItem(dlg, IDC_SPEECH_DETAIL_LIST);
            const int n = ListView_GetItemCount(list);
            std::vector<SpeechRow> out;
            for (int i = 0; i < n; ++i) {
                LVITEMW lv{};
                lv.mask = LVIF_PARAM;
                lv.iItem = i;
                ListView_GetItem(list, &lv);
                wchar_t label[256] = {};
                ListView_GetItemText(list, i, 0, label, 256);
                const int fid = static_cast<int>(lv.lParam);
                const auto& w = c->wrap[fid];
                out.push_back(
                    {fid, label, ListView_GetCheckState(list, i) != 0, w.before, w.after, w.no_sep});
            }
            *c->rows = std::move(out);
            EndDialog(dlg, IDOK);
            return TRUE;
        }
        if (id == IDCANCEL) {
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    }
    return FALSE;
}

bool show_speech_detail(HWND parent, const std::wstring& title, std::vector<SpeechRow>& rows) {
    DetailCtx c{&title, &rows};
    return DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_SPEECH_DETAIL), parent,
                           SpeechDetailProc, reinterpret_cast<LPARAM>(&c)) == IDOK;
}

// Edit one category's typed field list via the generic modal.
template <class Field>
void edit_speech(HWND parent, const std::wstring& title,
                 std::vector<present::SpeechItem<Field>>& items) {
    std::vector<SpeechRow> rows;
    for (const auto& it : items)
        rows.push_back({static_cast<int>(it.field),
                        to_wide(present::field_display_name(it.field)), it.enabled,
                        to_wide(it.before), to_wide(it.after), it.no_separator_after});
    if (!show_speech_detail(parent, title, rows))
        return;
    std::vector<present::SpeechItem<Field>> out;
    for (const auto& r : rows) {
        present::SpeechItem<Field> item(static_cast<Field>(r.id), r.enabled);
        item.before = to_utf8(r.before);
        item.after = to_utf8(r.after);
        item.no_separator_after = r.no_sep_after;
        out.push_back(std::move(item));
    }
    items = std::move(out);
}

// Content-warning modes and emoji-removal modes, index order parallel to these.
const present::CwMode kCwModes[] = {present::CwMode::Hide, present::CwMode::Show,
                                    present::CwMode::Ignore};
const wchar_t* const kCwModeLabels[] = {L"Hide post text (show warning only)",
                                        L"Show warning, then post text",
                                        L"Ignore warning (show post text)"};
const present::EmojiRemoval kEmojiModes[] = {present::EmojiRemoval::None,
                                             present::EmojiRemoval::Unicode,
                                             present::EmojiRemoval::Mastodon,
                                             present::EmojiRemoval::Both};
const wchar_t* const kEmojiModeLabels[] = {L"Off", L"Unicode emoji",
                                           L"Custom (:shortcode:)", L"Both"};

template <class T, size_t N>
void fill_combo(HWND dlg, int id, const wchar_t* const (&labels)[N], const T (&values)[N],
                T current) {
    HWND combo = GetDlgItem(dlg, id);
    int sel = 0;
    for (size_t i = 0; i < N; ++i) {
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(labels[i]));
        if (values[i] == current)
            sel = static_cast<int>(i);
    }
    SendMessageW(combo, CB_SETCURSEL, sel, 0);
}

template <class T, size_t N>
T read_combo(HWND dlg, int id, const T (&values)[N], T fallback) {
    const int sel = static_cast<int>(SendDlgItemMessageW(dlg, id, CB_GETCURSEL, 0, 0));
    return (sel >= 0 && sel < static_cast<int>(N)) ? values[sel] : fallback;
}

// Speech page: per-category speech-detail buttons plus content-warning, emoji
// removal, and leading-mention collapse — how post text and names read.
INT_PTR CALLBACK SpeechProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        Ctx* ctx = on_init(dlg, lp);
        fill_combo(dlg, IDC_SET_CW_MODE, kCwModeLabels, kCwModes, ctx->settings.text.cw);
        fill_combo(dlg, IDC_SET_POST_EMOJI, kEmojiModeLabels, kEmojiModes,
                   ctx->settings.text.post_emoji);
        fill_combo(dlg, IDC_SET_NAME_EMOJI, kEmojiModeLabels, kEmojiModes,
                   ctx->settings.text.name_emoji);
        HWND spin = GetDlgItem(dlg, IDC_SET_MAX_MENTIONS_SPIN);
        SendMessageW(spin, UDM_SETRANGE32, AppSettings::kMaxMentionsMin,
                     AppSettings::kMaxMentionsMax);
        SendMessageW(spin, UDM_SETPOS32, 0, ctx->settings.text.max_mentions);
        checked(dlg, IDC_SET_REPEAT_EDGE, ctx->settings.invisible_repeat_at_edge);
        checked(dlg, IDC_SET_ABSOLUTE_TIME, ctx->settings.text.absolute_time);
        SetDlgItemTextW(dlg, IDC_SET_SEPARATOR, to_wide(ctx->settings.speech.separator).c_str());
        return TRUE;
    }
    case WM_COMMAND: {
        Ctx* ctx = ctx_of(dlg);
        if (!ctx)
            break;
        switch (LOWORD(wp)) {
        case IDC_SET_SPEECH_POSTS:
            edit_speech(dlg, L"Speech Details — Posts", ctx->settings.speech.status);
            return TRUE;
        case IDC_SET_SPEECH_USERS:
            edit_speech(dlg, L"Speech Details — Users", ctx->settings.speech.user);
            return TRUE;
        case IDC_SET_SPEECH_NOTIFS:
            edit_speech(dlg, L"Speech Details — Notifications",
                        ctx->settings.speech.notification);
            return TRUE;
        }
        break;
    }
    case WM_NOTIFY:
        if (is_apply(lp)) {
            Ctx* ctx = ctx_of(dlg);
            ctx->settings.text.cw = read_combo(dlg, IDC_SET_CW_MODE, kCwModes, present::CwMode::Hide);
            ctx->settings.text.post_emoji =
                read_combo(dlg, IDC_SET_POST_EMOJI, kEmojiModes, present::EmojiRemoval::None);
            ctx->settings.text.name_emoji =
                read_combo(dlg, IDC_SET_NAME_EMOJI, kEmojiModes, present::EmojiRemoval::None);
            int v = read_int_field(dlg, IDC_SET_MAX_MENTIONS, ctx->settings.text.max_mentions);
            ctx->settings.text.max_mentions =
                std::clamp(v, AppSettings::kMaxMentionsMin, AppSettings::kMaxMentionsMax);
            ctx->settings.invisible_repeat_at_edge = is_checked(dlg, IDC_SET_REPEAT_EDGE);
            ctx->settings.text.absolute_time = is_checked(dlg, IDC_SET_ABSOLUTE_TIME);
            ctx->settings.speech.separator = to_utf8(dlg_text(dlg, IDC_SET_SEPARATOR));
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
            int v = read_int_field(dlg, IDC_SET_FETCHPAGES, ctx_of(dlg)->settings.fetch_pages);
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
        checked(dlg, IDC_SET_CONFIRM_UNBOOST, ctx->settings.confirm_unboost);
        checked(dlg, IDC_SET_CONFIRM_FAV, ctx->settings.confirm_favorite);
        checked(dlg, IDC_SET_CONFIRM_UNFAV, ctx->settings.confirm_unfavorite);
        checked(dlg, IDC_SET_CONFIRM_CLEAR, ctx->settings.confirm_clear_timeline);
        checked(dlg, IDC_SET_CONFIRM_BLOCK, ctx->settings.confirm_block);
        checked(dlg, IDC_SET_CONFIRM_UNBLOCK, ctx->settings.confirm_unblock);
        checked(dlg, IDC_SET_CONFIRM_DELETE, ctx->settings.confirm_delete_post);
        return TRUE;
    }
    case WM_NOTIFY:
        if (is_apply(lp)) {
            Ctx* ctx = ctx_of(dlg);
            ctx->settings.confirm_boost = is_checked(dlg, IDC_SET_CONFIRM_BOOST);
            ctx->settings.confirm_unboost = is_checked(dlg, IDC_SET_CONFIRM_UNBOOST);
            ctx->settings.confirm_favorite = is_checked(dlg, IDC_SET_CONFIRM_FAV);
            ctx->settings.confirm_unfavorite = is_checked(dlg, IDC_SET_CONFIRM_UNFAV);
            ctx->settings.confirm_clear_timeline = is_checked(dlg, IDC_SET_CONFIRM_CLEAR);
            ctx->settings.confirm_block = is_checked(dlg, IDC_SET_CONFIRM_BLOCK);
            ctx->settings.confirm_unblock = is_checked(dlg, IDC_SET_CONFIRM_UNBLOCK);
            ctx->settings.confirm_delete_post = is_checked(dlg, IDC_SET_CONFIRM_DELETE);
            ctx->applied = true;
            SetWindowLongPtrW(dlg, DWLP_MSGRESULT, PSNRET_NOERROR);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

// The invisible-interface mode combo. Index order parallels these ids.
const char* const kInvisibleModes[] = {"off", "hotkey", "keyhook", "layer"};
const wchar_t* const kInvisibleModeLabels[] = {
    L"Off", L"Global hotkeys", L"Low-level keyhook (captures reserved keys)",
    L"Layer (a key opens a FastSM layer)"};

// Layer mode shows the "change activation key" button; the other modes show the
// Keyboard Manager button (they use editable keymaps, Layer doesn't).
void update_invisible_buttons(HWND dlg) {
    const int sel =
        static_cast<int>(SendDlgItemMessageW(dlg, IDC_SET_INVIS_MODE, CB_GETCURSEL, 0, 0));
    const bool layer = sel >= 0 && sel < static_cast<int>(std::size(kInvisibleModes)) &&
                       std::string("layer") == kInvisibleModes[sel];
    ShowWindow(GetDlgItem(dlg, IDC_SET_INVIS_LAYERKEY), layer ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(dlg, IDC_SET_INVIS_MANAGER), layer ? SW_HIDE : SW_SHOW);
}

INT_PTR CALLBACK InvisibleProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        Ctx* ctx = on_init(dlg, lp);
        HWND combo = GetDlgItem(dlg, IDC_SET_INVIS_MODE);
        int sel = 0;
        for (int i = 0; i < static_cast<int>(std::size(kInvisibleModes)); ++i) {
            SendMessageW(combo, CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(kInvisibleModeLabels[i]));
            if (ctx->settings.invisible_mode == kInvisibleModes[i])
                sel = i;
        }
        SendMessageW(combo, CB_SETCURSEL, sel, 0);
        update_invisible_buttons(dlg);
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_SET_INVIS_MODE && HIWORD(wp) == CBN_SELCHANGE) {
            update_invisible_buttons(dlg);
            return TRUE;
        }
        if (LOWORD(wp) == IDC_SET_INVIS_MANAGER) {
            Ctx* ctx = ctx_of(dlg);
            if (ctx && ctx->open_manager)
                ctx->open_manager(dlg);
            return TRUE;
        }
        if (LOWORD(wp) == IDC_SET_INVIS_LAYERKEY) {
            Ctx* ctx = ctx_of(dlg);
            if (ctx) {
                auto key = capture_key_binding(dlg, GetModuleHandleW(nullptr),
                                               ctx->settings.invisible_layer_key);
                if (key)
                    ctx->settings.invisible_layer_key = *key;
            }
            return TRUE;
        }
        break;
    case WM_NOTIFY:
        if (is_apply(lp)) {
            const int sel = static_cast<int>(
                SendDlgItemMessageW(dlg, IDC_SET_INVIS_MODE, CB_GETCURSEL, 0, 0));
            if (sel >= 0 && sel < static_cast<int>(std::size(kInvisibleModes)))
                ctx_of(dlg)->settings.invisible_mode = kInvisibleModes[sel];
            ctx_of(dlg)->applied = true;
            SetWindowLongPtrW(dlg, DWLP_MSGRESULT, PSNRET_NOERROR);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

// Updates page: which release branch to follow + whether to check on startup.
const char* const kUpdateBranches[] = {"stable", "latest"};
const wchar_t* const kUpdateBranchLabels[] = {L"Stable (version updates)",
                                              L"Latest (every new build)"};

INT_PTR CALLBACK UpdatesProc(HWND dlg, UINT msg, WPARAM, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        Ctx* ctx = on_init(dlg, lp);
        HWND combo = GetDlgItem(dlg, IDC_SET_UPDATE_BRANCH);
        int sel = 0;
        for (int i = 0; i < static_cast<int>(std::size(kUpdateBranches)); ++i) {
            SendMessageW(combo, CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(kUpdateBranchLabels[i]));
            if (ctx->settings.update_branch == kUpdateBranches[i])
                sel = i;
        }
        SendMessageW(combo, CB_SETCURSEL, sel, 0);
        checked(dlg, IDC_SET_UPDATE_STARTUP, ctx->settings.check_updates_on_startup);
        return TRUE;
    }
    case WM_NOTIFY:
        if (is_apply(lp)) {
            Ctx* ctx = ctx_of(dlg);
            const int sel = static_cast<int>(
                SendDlgItemMessageW(dlg, IDC_SET_UPDATE_BRANCH, CB_GETCURSEL, 0, 0));
            if (sel >= 0 && sel < static_cast<int>(std::size(kUpdateBranches)))
                ctx->settings.update_branch = kUpdateBranches[sel];
            ctx->settings.check_updates_on_startup = is_checked(dlg, IDC_SET_UPDATE_STARTUP);
            ctx->applied = true;
            SetWindowLongPtrW(dlg, DWLP_MSGRESULT, PSNRET_NOERROR);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

// Behavior page: what pressing Enter on a post / a user does by default.
const char* const kEnterPostActions[] = {"post_info", "thread", "reply", "links"};
const wchar_t* const kEnterPostLabels[] = {L"View post (info)", L"View thread", L"Reply",
                                           L"Open links"};
const char* const kEnterUserActions[] = {"actions", "profile", "timeline"};
const wchar_t* const kEnterUserLabels[] = {L"User actions menu", L"View profile",
                                           L"View their timeline"};
const char* const kSecondPostActions[] = {"play_media", "post_info", "thread", "reply", "links"};
const wchar_t* const kSecondPostLabels[] = {L"Play media", L"View post (info)", L"View thread",
                                            L"Reply", L"Open links"};

INT_PTR CALLBACK BehaviorProc(HWND dlg, UINT msg, WPARAM, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        Ctx* ctx = on_init(dlg, lp);
        HWND post = GetDlgItem(dlg, IDC_SET_ENTER_POST);
        int psel = 0;
        for (int i = 0; i < static_cast<int>(std::size(kEnterPostActions)); ++i) {
            SendMessageW(post, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(kEnterPostLabels[i]));
            if (ctx->settings.enter_post_action == kEnterPostActions[i])
                psel = i;
        }
        SendMessageW(post, CB_SETCURSEL, psel, 0);
        HWND usr = GetDlgItem(dlg, IDC_SET_ENTER_USER);
        int usel = 0;
        for (int i = 0; i < static_cast<int>(std::size(kEnterUserActions)); ++i) {
            SendMessageW(usr, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(kEnterUserLabels[i]));
            if (ctx->settings.enter_user_action == kEnterUserActions[i])
                usel = i;
        }
        SendMessageW(usr, CB_SETCURSEL, usel, 0);
        HWND sec = GetDlgItem(dlg, IDC_SET_SECOND_POST);
        int ssel = 0;
        for (int i = 0; i < static_cast<int>(std::size(kSecondPostActions)); ++i) {
            SendMessageW(sec, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(kSecondPostLabels[i]));
            if (ctx->settings.secondary_post_action == kSecondPostActions[i])
                ssel = i;
        }
        SendMessageW(sec, CB_SETCURSEL, ssel, 0);
        checked(dlg, IDC_SET_MEDIA_BG, ctx->settings.media_background);
        checked(dlg, IDC_SET_REPLY_MENTIONS_END, ctx->settings.reply_mentions_at_end);
        return TRUE;
    }
    case WM_NOTIFY:
        if (is_apply(lp)) {
            Ctx* ctx = ctx_of(dlg);
            const int p =
                static_cast<int>(SendDlgItemMessageW(dlg, IDC_SET_ENTER_POST, CB_GETCURSEL, 0, 0));
            if (p >= 0 && p < static_cast<int>(std::size(kEnterPostActions)))
                ctx->settings.enter_post_action = kEnterPostActions[p];
            const int u =
                static_cast<int>(SendDlgItemMessageW(dlg, IDC_SET_ENTER_USER, CB_GETCURSEL, 0, 0));
            if (u >= 0 && u < static_cast<int>(std::size(kEnterUserActions)))
                ctx->settings.enter_user_action = kEnterUserActions[u];
            const int se =
                static_cast<int>(SendDlgItemMessageW(dlg, IDC_SET_SECOND_POST, CB_GETCURSEL, 0, 0));
            if (se >= 0 && se < static_cast<int>(std::size(kSecondPostActions)))
                ctx->settings.secondary_post_action = kSecondPostActions[se];
            ctx->settings.media_background = is_checked(dlg, IDC_SET_MEDIA_BG);
            ctx->settings.reply_mentions_at_end = is_checked(dlg, IDC_SET_REPLY_MENTIONS_END);
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
                                                const std::vector<std::string>& soundpacks,
                                                std::function<void(HWND)> open_manager) {
    Ctx ctx;
    ctx.settings = current;
    ctx.soundpacks = soundpacks;
    ctx.open_manager = std::move(open_manager);

    PROPSHEETPAGEW pages[] = {
        make_page(inst, IDD_SET_GENERAL, GeneralProc, &ctx),
        make_page(inst, IDD_SET_TIMELINES, TimelinesProc, &ctx),
        make_page(inst, IDD_SET_AUDIO, AudioProc, &ctx),
        make_page(inst, IDD_SET_EARCONS, EarconsProc, &ctx),
        make_page(inst, IDD_SET_SPEECH, SpeechProc, &ctx),
        make_page(inst, IDD_SET_ADVANCED, AdvancedProc, &ctx),
        make_page(inst, IDD_SET_CONFIRM, ConfirmProc, &ctx),
        make_page(inst, IDD_SET_BEHAVIOR, BehaviorProc, &ctx),
        make_page(inst, IDD_SET_INVISIBLE, InvisibleProc, &ctx),
        make_page(inst, IDD_SET_UPDATES, UpdatesProc, &ctx),
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
