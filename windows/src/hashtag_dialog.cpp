#include "hashtag_dialog.hpp"

#include <commctrl.h>

#include <string>

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

// --- Follow-a-hashtag prompt (editable combo) ---

struct PromptCtx {
    const std::vector<std::wstring>* prefill;
    std::optional<std::string> result;
};

INT_PTR CALLBACK PromptProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        SetWindowLongPtrW(dlg, DWLP_USER, lp);
        auto* c = reinterpret_cast<PromptCtx*>(lp);
        HWND combo = GetDlgItem(dlg, IDC_FH_COMBO);
        for (const std::wstring& tag : *c->prefill)
            SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(tag.c_str()));
        // Pre-select the first hashtag so a single Enter follows it; blank otherwise.
        if (!c->prefill->empty())
            SetWindowTextW(combo, c->prefill->front().c_str());
        SetFocus(combo);
        return FALSE; // focus set explicitly
    }
    case WM_COMMAND: {
        auto* c = reinterpret_cast<PromptCtx*>(GetWindowLongPtrW(dlg, DWLP_USER));
        if (LOWORD(wp) == IDOK) {
            wchar_t buf[256] = {0};
            GetDlgItemTextW(dlg, IDC_FH_COMBO, buf, 256);
            std::wstring name = trim(buf);
            if (!name.empty() && name.front() == L'#')
                name.erase(name.begin());
            name = trim(std::move(name));
            if (name.empty()) { // nothing to follow — just close
                EndDialog(dlg, IDCANCEL);
                return TRUE;
            }
            if (c)
                c->result = to_utf8(name);
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

std::optional<std::string> show_follow_hashtag_dialog(HWND parent, HINSTANCE inst,
                                                      const std::vector<std::wstring>& prefill) {
    PromptCtx ctx{&prefill, std::nullopt};
    DialogBoxParamW(inst, MAKEINTRESOURCEW(IDD_FOLLOW_HASHTAG), parent, PromptProc,
                    reinterpret_cast<LPARAM>(&ctx));
    return ctx.result;
}

// --- Followed Hashtags manager ---

FollowedHashtagsDialog::FollowedHashtagsDialog(HINSTANCE inst,
                                               std::function<void(const json&)> dispatch)
    : inst_(inst), dispatch_(std::move(dispatch)) {}

void FollowedHashtagsDialog::run(HWND parent, const json& initial) {
    if (initial.contains("tags") && initial["tags"].is_array())
        tags_ = initial["tags"];
    DialogBoxParamW(inst_, MAKEINTRESOURCEW(IDD_FOLLOWED_HASHTAGS), parent,
                    &FollowedHashtagsDialog::proc, reinterpret_cast<LPARAM>(this));
}

void FollowedHashtagsDialog::on_followed(const json& e) {
    if (e.contains("tags") && e["tags"].is_array())
        tags_ = e["tags"];
    if (dlg_) {
        refresh_list();
        update_enabled();
    }
}

INT_PTR CALLBACK FollowedHashtagsDialog::proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_INITDIALOG)
        SetWindowLongPtrW(dlg, DWLP_USER, lp);
    auto* self = reinterpret_cast<FollowedHashtagsDialog*>(GetWindowLongPtrW(dlg, DWLP_USER));
    return self ? self->handle(dlg, msg, wp, lp) : FALSE;
}

INT_PTR FollowedHashtagsDialog::handle(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG:
        dlg_ = dlg;
        refresh_list();
        update_enabled();
        SetFocus(GetDlgItem(dlg, IDC_FHM_LIST));
        return FALSE;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_FHM_OPEN:
            do_open();
            return TRUE;
        case IDC_FHM_UNFOLLOW:
            do_unfollow();
            return TRUE;
        case IDCANCEL:
            dlg_ = nullptr;
            EndDialog(dlg, 0);
            return TRUE;
        }
        break;
    case WM_NOTIFY:
        if (reinterpret_cast<LPNMHDR>(lp)->idFrom == IDC_FHM_LIST) {
            auto* nm = reinterpret_cast<LPNMLISTVIEW>(lp);
            if (nm->hdr.code == LVN_ITEMCHANGED)
                update_enabled();
            // Double-click opens the tag's timeline.
            else if (nm->hdr.code == NM_DBLCLK)
                do_open();
        }
        break;
    }
    return FALSE;
}

void FollowedHashtagsDialog::refresh_list() {
    HWND list = GetDlgItem(dlg_, IDC_FHM_LIST);
    const int keep = selected_index();
    ListView_DeleteAllItems(list);
    int i = 0;
    for (const auto& t : tags_) {
        std::wstring text = L"#" + to_wide(t.value("name", std::string{}));
        LVITEMW it{};
        it.mask = LVIF_TEXT;
        it.iItem = i;
        it.pszText = text.data();
        ListView_InsertItem(list, &it);
        ++i;
    }
    const int count = static_cast<int>(tags_.size());
    if (count > 0) {
        int sel = keep < 0 ? 0 : (keep >= count ? count - 1 : keep);
        ListView_SetItemState(list, sel, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    }
}

void FollowedHashtagsDialog::update_enabled() {
    const bool has_sel = selected_index() >= 0;
    EnableWindow(GetDlgItem(dlg_, IDC_FHM_OPEN), has_sel);
    EnableWindow(GetDlgItem(dlg_, IDC_FHM_UNFOLLOW), has_sel);
}

int FollowedHashtagsDialog::selected_index() const {
    return ListView_GetNextItem(GetDlgItem(dlg_, IDC_FHM_LIST), -1, LVNI_SELECTED);
}

void FollowedHashtagsDialog::do_open() {
    const int idx = selected_index();
    if (idx < 0 || idx >= static_cast<int>(tags_.size()))
        return;
    const std::string name = tags_[static_cast<size_t>(idx)].value("name", std::string{});
    if (name.empty())
        return;
    dispatch_({{"cmd", "spawn_timeline"}, {"kind", "hashtag"}, {"value", name}});
    HWND dlg = dlg_;
    dlg_ = nullptr;
    EndDialog(dlg, 0); // close the manager; the timeline opens behind it
}

void FollowedHashtagsDialog::do_unfollow() {
    const int idx = selected_index();
    if (idx < 0 || idx >= static_cast<int>(tags_.size()))
        return;
    const std::string name = tags_[static_cast<size_t>(idx)].value("name", std::string{});
    if (name.empty())
        return;
    // The core unfollows, then re-emits `followed_hashtags`, which refreshes the
    // list via on_followed() while this modal loop keeps pumping.
    dispatch_({{"cmd", "unfollow_hashtag"}, {"name", name}});
}

// --- Trending Hashtags manager ---

TrendingHashtagsDialog::TrendingHashtagsDialog(HINSTANCE inst,
                                               std::function<void(const json&)> dispatch)
    : inst_(inst), dispatch_(std::move(dispatch)) {}

void TrendingHashtagsDialog::run(HWND parent, const json& initial) {
    if (initial.contains("tags") && initial["tags"].is_array())
        tags_ = initial["tags"];
    DialogBoxParamW(inst_, MAKEINTRESOURCEW(IDD_TRENDING_HASHTAGS), parent,
                    &TrendingHashtagsDialog::proc, reinterpret_cast<LPARAM>(this));
}

INT_PTR CALLBACK TrendingHashtagsDialog::proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_INITDIALOG)
        SetWindowLongPtrW(dlg, DWLP_USER, lp);
    auto* self = reinterpret_cast<TrendingHashtagsDialog*>(GetWindowLongPtrW(dlg, DWLP_USER));
    return self ? self->handle(dlg, msg, wp, lp) : FALSE;
}

INT_PTR TrendingHashtagsDialog::handle(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG:
        dlg_ = dlg;
        refresh_list();
        update_enabled();
        SetFocus(GetDlgItem(dlg, IDC_THM_LIST));
        return FALSE;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_THM_OPEN:
            do_open();
            return TRUE;
        case IDC_THM_FOLLOW:
            do_follow();
            return TRUE;
        case IDCANCEL:
            dlg_ = nullptr;
            EndDialog(dlg, 0);
            return TRUE;
        }
        break;
    case WM_NOTIFY:
        if (reinterpret_cast<LPNMHDR>(lp)->idFrom == IDC_THM_LIST) {
            auto* nm = reinterpret_cast<LPNMLISTVIEW>(lp);
            if (nm->hdr.code == LVN_ITEMCHANGED)
                update_enabled();
            // Double-click opens the tag's timeline.
            else if (nm->hdr.code == NM_DBLCLK)
                do_open();
        }
        break;
    }
    return FALSE;
}

void TrendingHashtagsDialog::refresh_list() {
    HWND list = GetDlgItem(dlg_, IDC_THM_LIST);
    const int keep = selected_index();
    ListView_DeleteAllItems(list);
    int i = 0;
    for (const auto& t : tags_) {
        std::wstring text = L"#" + to_wide(t.value("name", std::string{}));
        LVITEMW it{};
        it.mask = LVIF_TEXT;
        it.iItem = i;
        it.pszText = text.data();
        ListView_InsertItem(list, &it);
        ++i;
    }
    const int count = static_cast<int>(tags_.size());
    if (count > 0) {
        int sel = keep < 0 ? 0 : (keep >= count ? count - 1 : keep);
        ListView_SetItemState(list, sel, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    }
}

void TrendingHashtagsDialog::update_enabled() {
    const int idx = selected_index();
    const bool has_sel = idx >= 0 && idx < static_cast<int>(tags_.size());
    EnableWindow(GetDlgItem(dlg_, IDC_THM_OPEN), has_sel);
    // Already-followed tags can't be followed again; a disabled Follow button is
    // the spoken cue that you already follow this one.
    const bool followable =
        has_sel && !tags_[static_cast<size_t>(idx)].value("following", false);
    EnableWindow(GetDlgItem(dlg_, IDC_THM_FOLLOW), followable);
}

int TrendingHashtagsDialog::selected_index() const {
    return ListView_GetNextItem(GetDlgItem(dlg_, IDC_THM_LIST), -1, LVNI_SELECTED);
}

void TrendingHashtagsDialog::do_open() {
    const int idx = selected_index();
    if (idx < 0 || idx >= static_cast<int>(tags_.size()))
        return;
    const std::string name = tags_[static_cast<size_t>(idx)].value("name", std::string{});
    if (name.empty())
        return;
    dispatch_({{"cmd", "spawn_timeline"}, {"kind", "hashtag"}, {"value", name}});
    HWND dlg = dlg_;
    dlg_ = nullptr;
    EndDialog(dlg, 0); // close the manager; the timeline opens behind it
}

void TrendingHashtagsDialog::do_follow() {
    const int idx = selected_index();
    if (idx < 0 || idx >= static_cast<int>(tags_.size()))
        return;
    const std::string name = tags_[static_cast<size_t>(idx)].value("name", std::string{});
    if (name.empty())
        return;
    // The core follows and announces the result. Reflect it optimistically so the
    // Follow button greys out for this tag (and stays greyed if reopened).
    dispatch_({{"cmd", "follow_hashtag"}, {"name", name}});
    tags_[static_cast<size_t>(idx)]["following"] = true;
    update_enabled();
}

} // namespace fastsmui
