#include "server_filters_dialog.hpp"

#include <commctrl.h>

#include <array>
#include <string>
#include <vector>

#include "../resources/resource.h"
#include "utf.hpp"

using nlohmann::json;

namespace fastsmui {
namespace {

struct ExpiryOption {
    const wchar_t* label;
    int seconds;
};
constexpr std::array<ExpiryOption, 7> kExpiryOptions = {{{L"Never", 0},
                                                         {L"30 minutes", 1800},
                                                         {L"1 hour", 3600},
                                                         {L"6 hours", 21600},
                                                         {L"12 hours", 43200},
                                                         {L"1 day", 86400},
                                                         {L"1 week", 604800}}};

// Context checkbox <-> Mastodon context token.
struct CtxBox {
    int id;
    const char* token;
};
constexpr std::array<CtxBox, 5> kContexts = {{{IDC_EF_CTX_HOME, "home"},
                                              {IDC_EF_CTX_NOTIF, "notifications"},
                                              {IDC_EF_CTX_PUBLIC, "public"},
                                              {IDC_EF_CTX_THREAD, "thread"},
                                              {IDC_EF_CTX_ACCOUNT, "account"}}};

std::wstring field_text(HWND dlg, int id) {
    HWND ctrl = GetDlgItem(dlg, id);
    const int len = GetWindowTextLengthW(ctrl);
    std::wstring buf(static_cast<size_t>(len) + 1, L'\0');
    const int got = GetWindowTextW(ctrl, buf.data(), len + 1);
    buf.resize(static_cast<size_t>(got));
    return buf;
}

std::wstring trim(const std::wstring& s) {
    size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos)
        return {};
    size_t b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}

// --- Add/Edit sub-dialog ---

INT_PTR CALLBACK EditFilterProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        SetWindowLongPtrW(dlg, DWLP_USER, static_cast<LONG_PTR>(lp));
        auto* f = reinterpret_cast<json*>(lp);

        HWND action = GetDlgItem(dlg, IDC_EF_ACTION);
        SendMessageW(action, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Hide completely"));
        SendMessageW(action, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Show with warning"));
        const std::string act = f->value("action", std::string("hide"));
        SendMessageW(action, CB_SETCURSEL, act == "warn" ? 1 : 0, 0);

        HWND exp = GetDlgItem(dlg, IDC_EF_EXPIRES);
        for (const auto& o : kExpiryOptions)
            SendMessageW(exp, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(o.label));
        SendMessageW(exp, CB_SETCURSEL, 0, 0); // Never (edit does not restore remaining time)

        SetDlgItemTextW(dlg, IDC_EF_TITLE, to_wide(f->value("title", std::string{})).c_str());

        // Keywords: one per line; a single whole-word toggle (FastSM behavior).
        bool whole_word = true;
        std::wstring keywords;
        const bool is_new = !f->contains("keywords");
        if (auto it = f->find("keywords"); it != f->end() && it->is_array()) {
            bool first = true;
            for (const auto& k : *it) {
                if (!first)
                    keywords += L"\r\n";
                first = false;
                keywords += to_wide(k.value("keyword", std::string{}));
                whole_word = k.value("whole_word", true);
            }
        }
        SetDlgItemTextW(dlg, IDC_EF_KEYWORDS, keywords.c_str());
        CheckDlgButton(dlg, IDC_EF_WHOLE_WORD, whole_word ? BST_CHECKED : BST_UNCHECKED);

        // Contexts: default all on for a new filter, else reflect the stored set.
        std::vector<std::string> ctx;
        if (auto it = f->find("context"); it != f->end() && it->is_array())
            for (const auto& c : *it)
                if (c.is_string())
                    ctx.push_back(c.get<std::string>());
        for (const auto& box : kContexts) {
            bool on = is_new;
            for (const auto& c : ctx)
                if (c == box.token)
                    on = true;
            CheckDlgButton(dlg, box.id, on ? BST_CHECKED : BST_UNCHECKED);
        }
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK) {
            auto* f = reinterpret_cast<json*>(GetWindowLongPtrW(dlg, DWLP_USER));
            const std::wstring title = trim(field_text(dlg, IDC_EF_TITLE));
            if (title.empty()) {
                MessageBoxW(dlg, L"Please enter a title.", L"Filter", MB_OK | MB_ICONWARNING);
                return TRUE;
            }
            // Split keywords by line, dropping blanks.
            std::vector<std::wstring> lines;
            {
                std::wstring raw = field_text(dlg, IDC_EF_KEYWORDS);
                size_t start = 0;
                while (start <= raw.size()) {
                    size_t nl = raw.find(L'\n', start);
                    std::wstring line =
                        trim(raw.substr(start, nl == std::wstring::npos ? std::wstring::npos : nl - start));
                    if (!line.empty())
                        lines.push_back(line);
                    if (nl == std::wstring::npos)
                        break;
                    start = nl + 1;
                }
            }
            if (lines.empty()) {
                MessageBoxW(dlg, L"Please enter at least one keyword.", L"Filter",
                            MB_OK | MB_ICONWARNING);
                return TRUE;
            }
            const bool whole_word = IsDlgButtonChecked(dlg, IDC_EF_WHOLE_WORD) == BST_CHECKED;
            json context = json::array();
            for (const auto& box : kContexts)
                if (IsDlgButtonChecked(dlg, box.id) == BST_CHECKED)
                    context.push_back(box.token);
            if (context.empty()) {
                MessageBoxW(dlg, L"Please choose at least one place to apply the filter.", L"Filter",
                            MB_OK | MB_ICONWARNING);
                return TRUE;
            }
            const int act = static_cast<int>(SendMessageW(GetDlgItem(dlg, IDC_EF_ACTION),
                                                          CB_GETCURSEL, 0, 0));
            int expi = static_cast<int>(SendMessageW(GetDlgItem(dlg, IDC_EF_EXPIRES),
                                                     CB_GETCURSEL, 0, 0));
            if (expi < 0 || expi >= static_cast<int>(kExpiryOptions.size()))
                expi = 0;

            json keywords = json::array();
            for (const auto& l : lines)
                keywords.push_back({{"keyword", to_utf8(l)}, {"whole_word", whole_word}});

            json out;
            if (const auto id = f->find("id"); id != f->end() && id->is_string())
                out["id"] = *id; // editing keeps the id
            out["title"] = to_utf8(title);
            out["action"] = act == 1 ? "warn" : "hide";
            out["context"] = context;
            out["expires_in"] = kExpiryOptions[static_cast<size_t>(expi)].seconds;
            out["keywords"] = keywords;
            *f = std::move(out);
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

std::wstring filter_row_label(const json& f) {
    std::wstring label = to_wide(f.value("title", std::string{}));
    std::wstring kws;
    if (auto it = f.find("keywords"); it != f.end() && it->is_array()) {
        bool first = true;
        for (const auto& k : *it) {
            if (!first)
                kws += L", ";
            first = false;
            kws += to_wide(k.value("keyword", std::string{}));
        }
    }
    if (!kws.empty())
        label += L" \x2014 " + kws; // em dash
    label += f.value("action", std::string("warn")) == "hide" ? L" (Hide)" : L" (Warn)";
    return label;
}

} // namespace

bool show_edit_filter_dialog(HWND parent, HINSTANCE inst, json& filter) {
    return DialogBoxParamW(inst, MAKEINTRESOURCEW(IDD_EDIT_FILTER), parent, EditFilterProc,
                           reinterpret_cast<LPARAM>(&filter)) == IDOK;
}

// --- Manager dialog ---

ServerFiltersDialog::ServerFiltersDialog(HINSTANCE inst,
                                         std::function<void(const json&)> dispatch)
    : inst_(inst), dispatch_(std::move(dispatch)) {}

void ServerFiltersDialog::run(HWND parent, const json& initial) {
    if (initial.contains("filters") && initial["filters"].is_array())
        filters_ = initial["filters"];
    DialogBoxParamW(inst_, MAKEINTRESOURCEW(IDD_SERVER_FILTERS), parent, &ServerFiltersDialog::proc,
                    reinterpret_cast<LPARAM>(this));
}

void ServerFiltersDialog::on_server_filters(const json& e) {
    if (e.contains("filters") && e["filters"].is_array())
        filters_ = e["filters"];
    if (dlg_) {
        refresh_list();
        update_enabled();
    }
}

INT_PTR CALLBACK ServerFiltersDialog::proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_INITDIALOG)
        SetWindowLongPtrW(dlg, DWLP_USER, lp);
    auto* self = reinterpret_cast<ServerFiltersDialog*>(GetWindowLongPtrW(dlg, DWLP_USER));
    return self ? self->handle(dlg, msg, wp, lp) : FALSE;
}

INT_PTR ServerFiltersDialog::handle(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        dlg_ = dlg;
        HWND list = GetDlgItem(dlg, IDC_SF_LIST);
        ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_LABELTIP);
        LVCOLUMNW col{};
        col.mask = LVCF_TEXT | LVCF_WIDTH;
        col.pszText = const_cast<wchar_t*>(L"Filter");
        col.cx = 296;
        ListView_InsertColumn(list, 0, &col);
        refresh_list();
        update_enabled();
        return TRUE;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_SF_ADD:
            do_add();
            return TRUE;
        case IDC_SF_EDIT:
            do_edit();
            return TRUE;
        case IDC_SF_DELETE:
            do_delete();
            return TRUE;
        case IDCANCEL:
            dlg_ = nullptr;
            EndDialog(dlg, 0);
            return TRUE;
        }
        break;
    case WM_NOTIFY:
        if (reinterpret_cast<LPNMHDR>(lp)->idFrom == IDC_SF_LIST) {
            auto* nm = reinterpret_cast<LPNMLISTVIEW>(lp);
            if (nm->hdr.code == LVN_ITEMCHANGED)
                update_enabled();
        }
        break;
    }
    return FALSE;
}

void ServerFiltersDialog::refresh_list() {
    HWND list = GetDlgItem(dlg_, IDC_SF_LIST);
    ListView_DeleteAllItems(list);
    int i = 0;
    for (const auto& f : filters_) {
        std::wstring label = filter_row_label(f);
        LVITEMW it{};
        it.mask = LVIF_TEXT;
        it.iItem = i;
        it.pszText = label.data();
        ListView_InsertItem(list, &it);
        ++i;
    }
    SetDlgItemTextW(dlg_, IDC_SF_STATUS,
                    filters_.empty() ? L"No server filters." : L"");
}

void ServerFiltersDialog::update_enabled() {
    const bool has_sel = selected_index() >= 0;
    EnableWindow(GetDlgItem(dlg_, IDC_SF_EDIT), has_sel);
    EnableWindow(GetDlgItem(dlg_, IDC_SF_DELETE), has_sel);
}

int ServerFiltersDialog::selected_index() const {
    return ListView_GetNextItem(GetDlgItem(dlg_, IDC_SF_LIST), -1, LVNI_SELECTED);
}

void ServerFiltersDialog::do_add() {
    json f = json::object();
    if (show_edit_filter_dialog(dlg_, inst_, f)) {
        dispatch_({{"cmd", "save_server_filter"}, {"filter", f}});
        SetDlgItemTextW(dlg_, IDC_SF_STATUS, L"Saving…");
    }
}

void ServerFiltersDialog::do_edit() {
    const int idx = selected_index();
    if (idx < 0 || idx >= static_cast<int>(filters_.size()))
        return;
    json f = filters_[static_cast<size_t>(idx)];
    if (show_edit_filter_dialog(dlg_, inst_, f)) {
        dispatch_({{"cmd", "save_server_filter"}, {"filter", f}});
        SetDlgItemTextW(dlg_, IDC_SF_STATUS, L"Saving…");
    }
}

void ServerFiltersDialog::do_delete() {
    const int idx = selected_index();
    if (idx < 0 || idx >= static_cast<int>(filters_.size()))
        return;
    const json& f = filters_[static_cast<size_t>(idx)];
    std::wstring msg = L"Delete the filter \"" + to_wide(f.value("title", std::string{})) + L"\"?";
    if (MessageBoxW(dlg_, msg.c_str(), L"Server Filters", MB_YESNO | MB_ICONQUESTION) != IDYES)
        return;
    dispatch_({{"cmd", "delete_server_filter"}, {"id", f.value("id", std::string{})}});
    SetDlgItemTextW(dlg_, IDC_SF_STATUS, L"Deleting…");
}

} // namespace fastsmui
