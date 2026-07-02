#include "keymap_manager_dialog.hpp"

#include <algorithm>
#include <cwctype>

#include <commctrl.h>

#include "../resources/resource.h"
#include "utf.hpp"

using nlohmann::json;

namespace fastsmui {
namespace {

// --- key-string helpers (mirror the core's normalize_key canonical form) ---

const wchar_t* const kNamedKeys[] = {
    L"up",     L"down",   L"left",       L"right",  L"home",  L"end",   L"pageup",
    L"pagedown", L"insert", L"delete",   L"return", L"enter", L"escape", L"space",
    L"tab",    L"back",   L"backspace",  L"apps",   L"pause", L"printscreen",
};

bool is_valid_base(const std::string& low) {
    if (low.size() == 1) {
        const char c = low[0];
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
            return true;
        return std::string("/;[]\\'=,-.`").find(c) != std::string::npos;
    }
    for (const wchar_t* n : kNamedKeys)
        if (to_utf8(n) == low)
            return true;
    if (low.size() >= 2 && low[0] == 'f') { // f1..f24
        int v = 0;
        for (size_t i = 1; i < low.size(); ++i) {
            if (low[i] < '0' || low[i] > '9')
                return false;
            v = v * 10 + (low[i] - '0');
        }
        return v >= 1 && v <= 24;
    }
    return false;
}

std::string canonical_key(bool ctrl, bool alt, bool shift, bool win, std::string base) {
    std::transform(base.begin(), base.end(), base.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (base == "enter")
        base = "return";
    if (base == "backspace")
        base = "back";
    std::string out;
    if (ctrl)
        out += "control+";
    if (alt)
        out += "alt+";
    if (shift)
        out += "shift+";
    if (win)
        out += "win+";
    return out + base;
}

void split_key(const std::string& key, bool& ctrl, bool& alt, bool& shift, bool& win,
               std::string& base) {
    ctrl = alt = shift = win = false;
    base.clear();
    size_t start = 0;
    while (start <= key.size()) {
        size_t plus = key.find('+', start);
        std::string tok = key.substr(start, plus == std::string::npos ? std::string::npos
                                                                       : plus - start);
        if (tok == "control")
            ctrl = true;
        else if (tok == "alt")
            alt = true;
        else if (tok == "shift")
            shift = true;
        else if (tok == "win")
            win = true;
        else if (!tok.empty())
            base = tok;
        if (plus == std::string::npos)
            break;
        start = plus + 1;
    }
}

// --- New-keymap name prompt ---

struct NameCtx {
    std::wstring name;
    bool ok = false;
};

INT_PTR CALLBACK name_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_INITDIALOG) {
        SetWindowLongPtrW(dlg, DWLP_USER, static_cast<LONG_PTR>(lp));
        return TRUE;
    }
    if (msg == WM_COMMAND) {
        if (LOWORD(wp) == IDOK) {
            auto* ctx = reinterpret_cast<NameCtx*>(GetWindowLongPtrW(dlg, DWLP_USER));
            wchar_t buf[64];
            GetDlgItemTextW(dlg, IDC_KMN_NAME, buf, 64);
            ctx->name = buf;
            ctx->ok = true;
            EndDialog(dlg, IDOK);
            return TRUE;
        }
        if (LOWORD(wp) == IDCANCEL) {
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        }
    }
    return FALSE;
}

bool valid_keymap_name(const std::wstring& n) {
    if (n.empty())
        return false;
    for (wchar_t c : n)
        if (!std::iswalnum(c) && c != L'-' && c != L'_')
            return false;
    return true;
}

// --- Binding-capture sub-dialog ---

struct BindingCtx {
    std::string current_key; // to prefill
    std::string result;      // canonical key on OK
    bool ok = false;
};

INT_PTR CALLBACK binding_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_INITDIALOG) {
        SetWindowLongPtrW(dlg, DWLP_USER, static_cast<LONG_PTR>(lp));
        auto* ctx = reinterpret_cast<BindingCtx*>(lp);
        bool ctrl, alt, shift, win;
        std::string base;
        split_key(ctx->current_key, ctrl, alt, shift, win, base);
        CheckDlgButton(dlg, IDC_KMB_CTRL, ctrl ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dlg, IDC_KMB_ALT, alt ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dlg, IDC_KMB_SHIFT, shift ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dlg, IDC_KMB_WIN, win ? BST_CHECKED : BST_UNCHECKED);
        SetDlgItemTextW(dlg, IDC_KMB_KEY, to_wide(base).c_str());
        return TRUE;
    }
    if (msg == WM_COMMAND) {
        if (LOWORD(wp) == IDOK) {
            auto* ctx = reinterpret_cast<BindingCtx*>(GetWindowLongPtrW(dlg, DWLP_USER));
            wchar_t buf[32];
            GetDlgItemTextW(dlg, IDC_KMB_KEY, buf, 32);
            std::string base = to_utf8(buf);
            // trim + lowercase for validation
            std::string low = base;
            std::transform(low.begin(), low.end(), low.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            while (!low.empty() && (low.front() == ' '))
                low.erase(low.begin());
            while (!low.empty() && (low.back() == ' '))
                low.pop_back();
            if (low.empty() || !is_valid_base(low)) {
                MessageBoxW(dlg,
                            L"Enter a single key like t, /, up, return, delete, or f5 "
                            L"(use the checkboxes for modifiers).",
                            L"Invalid key", MB_OK | MB_ICONERROR);
                SetFocus(GetDlgItem(dlg, IDC_KMB_KEY));
                return TRUE;
            }
            ctx->result = canonical_key(IsDlgButtonChecked(dlg, IDC_KMB_CTRL) == BST_CHECKED,
                                        IsDlgButtonChecked(dlg, IDC_KMB_ALT) == BST_CHECKED,
                                        IsDlgButtonChecked(dlg, IDC_KMB_SHIFT) == BST_CHECKED,
                                        IsDlgButtonChecked(dlg, IDC_KMB_WIN) == BST_CHECKED, low);
            ctx->ok = true;
            EndDialog(dlg, IDOK);
            return TRUE;
        }
        if (LOWORD(wp) == IDCANCEL) {
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        }
    }
    return FALSE;
}

} // namespace

std::optional<std::string> capture_key_binding(HWND parent, HINSTANCE inst,
                                               const std::string& current_key) {
    BindingCtx ctx;
    ctx.current_key = current_key;
    if (DialogBoxParamW(inst, MAKEINTRESOURCEW(IDD_KM_BINDING), parent, &binding_proc,
                        reinterpret_cast<LPARAM>(&ctx)) == IDOK &&
        ctx.ok && !ctx.result.empty())
        return ctx.result;
    return std::nullopt;
}

std::wstring format_key_display(const std::string& key) {
    if (key.empty())
        return L"(unbound)";
    std::wstring out;
    size_t start = 0;
    while (start <= key.size()) {
        size_t plus = key.find('+', start);
        std::string tok =
            key.substr(start, plus == std::string::npos ? std::string::npos : plus - start);
        std::wstring w;
        if (tok == "control")
            w = L"Ctrl";
        else if (tok == "alt")
            w = L"Alt";
        else if (tok == "shift")
            w = L"Shift";
        else if (tok == "win")
            w = L"Win";
        else if (tok.size() == 1)
            w = std::wstring(1, static_cast<wchar_t>(std::toupper(tok[0])));
        else if (!tok.empty()) {
            w = to_wide(tok);
            if (!w.empty())
                w[0] = static_cast<wchar_t>(std::towupper(w[0]));
        }
        if (!w.empty()) {
            if (!out.empty())
                out += L"+";
            out += w;
        }
        if (plus == std::string::npos)
            break;
        start = plus + 1;
    }
    return out;
}

KeymapManagerDialog::KeymapManagerDialog(HINSTANCE inst, std::vector<KmAction> catalog,
                                         std::string active_keymap,
                                         std::function<void(const json&)> dispatch)
    : inst_(inst), catalog_(std::move(catalog)), dispatch_(std::move(dispatch)),
      current_name_(std::move(active_keymap)) {
    for (const auto& a : catalog_)
        if (!a.default_key.empty())
            default_key_[a.id] = a.default_key;
}

void KeymapManagerDialog::run(HWND parent) {
    DialogBoxParamW(inst_, MAKEINTRESOURCEW(IDD_KEYMAP_MANAGER), parent, &KeymapManagerDialog::proc,
                    reinterpret_cast<LPARAM>(this));
}

INT_PTR CALLBACK KeymapManagerDialog::proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    KeymapManagerDialog* self = nullptr;
    if (msg == WM_INITDIALOG) {
        self = reinterpret_cast<KeymapManagerDialog*>(lp);
        SetWindowLongPtrW(dlg, DWLP_USER, reinterpret_cast<LONG_PTR>(self));
        self->dlg_ = dlg;
    } else {
        self = reinterpret_cast<KeymapManagerDialog*>(GetWindowLongPtrW(dlg, DWLP_USER));
    }
    return self ? self->handle(dlg, msg, wp, lp) : FALSE;
}

INT_PTR KeymapManagerDialog::handle(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        HWND list = GetDlgItem(dlg, IDC_KM_LIST);
        ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT);
        LVCOLUMNW col{};
        col.mask = LVCF_TEXT | LVCF_WIDTH;
        struct {
            const wchar_t* t;
            int w;
        } cols[] = {{L"Action", 170}, {L"Key", 110}, {L"Source", 60}};
        for (int i = 0; i < 3; ++i) {
            col.pszText = const_cast<wchar_t*>(cols[i].t);
            col.cx = cols[i].w;
            ListView_InsertColumn(list, i, &col);
        }
        // Ask the core for the active keymap's raw overrides/unbinds.
        requested_name_ = current_name_;
        dispatch_({{"cmd", "get_keymap"}, {"name", current_name_}});
        populate_keymap_combo();
        refresh_list();
        update_enabled();
        set_status();
        return TRUE;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_KM_KEYMAP:
            if (HIWORD(wp) == CBN_SELCHANGE && !suppress_combo_) {
                const int sel =
                    static_cast<int>(SendMessageW(GetDlgItem(dlg, IDC_KM_KEYMAP), CB_GETCURSEL, 0, 0));
                if (sel >= 0 && sel < static_cast<int>(keymaps_.size())) {
                    if (dirty_ && !confirm_discard()) {
                        populate_keymap_combo(); // revert selection
                        return TRUE;
                    }
                    switch_keymap(keymaps_[static_cast<size_t>(sel)]);
                }
            }
            return TRUE;
        case IDC_KM_NEW:
            do_new();
            return TRUE;
        case IDC_KM_DELETE:
            do_delete();
            return TRUE;
        case IDC_KM_SET:
            do_set_binding();
            return TRUE;
        case IDC_KM_UNBIND:
            do_unbind();
            return TRUE;
        case IDC_KM_RESET:
            do_reset();
            return TRUE;
        case IDC_KM_SAVE:
            do_save();
            return TRUE;
        case IDCANCEL:
            if (dirty_ && !confirm_discard())
                return TRUE;
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        }
        break;
    case WM_NOTIFY: {
        auto* hdr = reinterpret_cast<NMHDR*>(lp);
        if (hdr->idFrom == IDC_KM_LIST && hdr->code == LVN_ITEMACTIVATE)
            do_set_binding();
        break;
    }
    }
    return FALSE;
}

void KeymapManagerDialog::on_keymap(const json& e) {
    const std::string name = e.value("name", std::string{});
    if (e.contains("builtins")) {
        builtins_.clear();
        for (const auto& n : e["builtins"])
            builtins_.insert(n.get<std::string>());
    }
    if (e.contains("keymaps")) {
        keymaps_.clear();
        for (const auto& n : e["keymaps"])
            keymaps_.push_back(n.get<std::string>());
        populate_keymap_combo();
    }
    if (name != requested_name_)
        return; // an unrelated keymap event (e.g. driver reload) — ignore
    current_name_ = name;
    overrides_.clear();
    unbinds_.clear();
    const json ov = e.value("overrides", json::object());
    for (const auto& [action, key] : ov.items())
        overrides_[action] = key.get<std::string>();
    for (const auto& a : e.value("unbinds", json::array()))
        unbinds_.insert(a.get<std::string>());
    dirty_ = false;
    populate_keymap_combo();
    refresh_list();
    update_enabled();
    set_status();
}

void KeymapManagerDialog::populate_keymap_combo() {
    HWND combo = GetDlgItem(dlg_, IDC_KM_KEYMAP);
    suppress_combo_ = true;
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    int sel = 0;
    for (size_t i = 0; i < keymaps_.size(); ++i) {
        std::wstring label = to_wide(keymaps_[i]);
        if (is_builtin(keymaps_[i]))
            label += L" (built-in)";
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        if (keymaps_[i] == current_name_)
            sel = static_cast<int>(i);
    }
    SendMessageW(combo, CB_SETCURSEL, sel, 0);
    suppress_combo_ = false;
}

std::pair<std::string, std::string> KeymapManagerDialog::effective(const std::string& action) const {
    if (auto it = overrides_.find(action); it != overrides_.end())
        return {it->second, "custom"};
    if (unbinds_.count(action))
        return {"", "unbound"};
    if (auto it = default_key_.find(action); it != default_key_.end())
        return {it->second, "default"};
    return {"", "unbound"};
}

void KeymapManagerDialog::refresh_list() {
    HWND list = GetDlgItem(dlg_, IDC_KM_LIST);
    const int prev = ListView_GetNextItem(list, -1, LVNI_SELECTED);
    ListView_DeleteAllItems(list);
    for (size_t i = 0; i < catalog_.size(); ++i) {
        const auto& a = catalog_[i];
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = static_cast<int>(i);
        std::wstring label = to_wide(a.label);
        item.pszText = const_cast<wchar_t*>(label.c_str());
        ListView_InsertItem(list, &item);
        auto [key, source] = effective(a.id);
        const std::wstring keytext = format_key_display(key);
        ListView_SetItemText(list, static_cast<int>(i), 1, const_cast<wchar_t*>(keytext.c_str()));
        std::wstring src = source == "custom"    ? L"custom"
                           : source == "unbound" ? L"unbound"
                                                 : L"default";
        ListView_SetItemText(list, static_cast<int>(i), 2, const_cast<wchar_t*>(src.c_str()));
    }
    const int want = prev >= 0 && prev < static_cast<int>(catalog_.size()) ? prev : 0;
    if (!catalog_.empty()) {
        ListView_SetItemState(list, want, LVIS_SELECTED | LVIS_FOCUSED,
                              LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(list, want, FALSE);
    }
}

void KeymapManagerDialog::update_enabled() {
    const BOOL on = editable() ? TRUE : FALSE;
    EnableWindow(GetDlgItem(dlg_, IDC_KM_SET), on);
    EnableWindow(GetDlgItem(dlg_, IDC_KM_UNBIND), on);
    EnableWindow(GetDlgItem(dlg_, IDC_KM_RESET), on);
    EnableWindow(GetDlgItem(dlg_, IDC_KM_SAVE), on);
    EnableWindow(GetDlgItem(dlg_, IDC_KM_DELETE), on);
}

void KeymapManagerDialog::set_status() {
    std::wstring msg;
    if (!editable())
        msg = L"This keymap is built-in and read-only. Create a new keymap to make changes.";
    else
        msg = L"Editing '" + to_wide(current_name_) + L"'" + (dirty_ ? L" (unsaved)." : L".") +
              L" Save also makes it the active keymap.";
    SetDlgItemTextW(dlg_, IDC_KM_STATUS, msg.c_str());
}

std::string KeymapManagerDialog::selected_action() const {
    const int idx = ListView_GetNextItem(GetDlgItem(dlg_, IDC_KM_LIST), -1, LVNI_SELECTED);
    if (idx < 0 || idx >= static_cast<int>(catalog_.size()))
        return {};
    return catalog_[static_cast<size_t>(idx)].id;
}

void KeymapManagerDialog::switch_keymap(const std::string& name) {
    requested_name_ = name;
    current_name_ = name;
    overrides_.clear();
    unbinds_.clear();
    dirty_ = false;
    dispatch_({{"cmd", "get_keymap"}, {"name", name}}); // on_keymap will fill in
    dispatch_({{"cmd", "set_active_keymap"}, {"name", name}}); // selecting also activates it
    refresh_list();
    update_enabled();
    set_status();
}

void KeymapManagerDialog::do_set_binding() {
    if (!editable())
        return;
    const std::string action = selected_action();
    if (action.empty())
        return;
    BindingCtx ctx;
    ctx.current_key = effective(action).first;
    if (DialogBoxParamW(inst_, MAKEINTRESOURCEW(IDD_KM_BINDING), dlg_, &binding_proc,
                        reinterpret_cast<LPARAM>(&ctx)) != IDOK ||
        !ctx.ok || ctx.result.empty())
        return;
    // Collision: if another action already uses this key, offer to reassign.
    for (const auto& a : catalog_) {
        if (a.id == action)
            continue;
        if (effective(a.id).first == ctx.result) {
            const std::wstring m = L"'" + format_key_display(ctx.result) + L"' is already bound to '" +
                                   to_wide(a.label) + L"'. Reassign it to this action?";
            if (MessageBoxW(dlg_, m.c_str(), L"Key in use", MB_YESNO | MB_ICONQUESTION) != IDYES)
                return;
            if (overrides_.count(a.id))
                overrides_.erase(a.id); // was custom -> just drop it
            else
                unbinds_.insert(a.id); // shadowed a default -> unbind so it doesn't leak back
            break;
        }
    }
    overrides_[action] = ctx.result;
    unbinds_.erase(action);
    dirty_ = true;
    refresh_list();
    set_status();
}

void KeymapManagerDialog::do_unbind() {
    if (!editable())
        return;
    const std::string action = selected_action();
    if (action.empty())
        return;
    overrides_.erase(action);
    if (default_key_.count(action)) // shadow the inherited default
        unbinds_.insert(action);
    dirty_ = true;
    refresh_list();
    set_status();
}

void KeymapManagerDialog::do_reset() {
    if (!editable())
        return;
    const std::string action = selected_action();
    if (action.empty())
        return;
    bool changed = overrides_.erase(action) > 0;
    changed = unbinds_.erase(action) > 0 || changed;
    if (changed) {
        dirty_ = true;
        refresh_list();
        set_status();
    }
}

void KeymapManagerDialog::do_new() {
    NameCtx ctx;
    if (DialogBoxParamW(inst_, MAKEINTRESOURCEW(IDD_KM_NEWNAME), dlg_, &name_proc,
                        reinterpret_cast<LPARAM>(&ctx)) != IDOK ||
        !ctx.ok)
        return;
    if (!valid_keymap_name(ctx.name)) {
        MessageBoxW(dlg_, L"Use letters, digits, dashes, and underscores only.", L"Invalid name",
                    MB_OK | MB_ICONERROR);
        return;
    }
    const std::string name = to_utf8(ctx.name);
    if (name == "default" ||
        std::find(keymaps_.begin(), keymaps_.end(), name) != keymaps_.end()) {
        MessageBoxW(dlg_, L"A keymap with that name already exists.", L"Name in use",
                    MB_OK | MB_ICONERROR);
        return;
    }
    // Create an empty keymap file (inherits everything from default) and edit it.
    dispatch_({{"cmd", "save_keymap"}, {"name", name}, {"overrides", json::object()},
               {"unbinds", json::array()}});
    switch_keymap(name);
}

void KeymapManagerDialog::do_delete() {
    if (!editable())
        return;
    const std::wstring m = L"Delete keymap '" + to_wide(current_name_) + L"'? This cannot be undone.";
    if (MessageBoxW(dlg_, m.c_str(), L"Delete keymap", MB_YESNO | MB_ICONWARNING) != IDYES)
        return;
    dispatch_({{"cmd", "delete_keymap"}, {"name", current_name_}});
    dirty_ = false;
    switch_keymap("default");
}

void KeymapManagerDialog::do_save() {
    if (!editable())
        return;
    json ov = json::object();
    for (const auto& [action, key] : overrides_)
        ov[action] = key;
    json ub = json::array();
    for (const auto& a : unbinds_)
        ub.push_back(a);
    dispatch_({{"cmd", "save_keymap"}, {"name", current_name_}, {"overrides", ov}, {"unbinds", ub}});
    dispatch_({{"cmd", "set_active_keymap"}, {"name", current_name_}}); // save activates it
    dirty_ = false;
    set_status();
    MessageBoxW(dlg_, L"Saved and made active.", L"Keyboard Manager", MB_OK | MB_ICONINFORMATION);
}

bool KeymapManagerDialog::confirm_discard() {
    return MessageBoxW(dlg_, L"Discard unsaved changes?", L"Unsaved changes",
                       MB_YESNO | MB_ICONWARNING) == IDYES;
}

} // namespace fastsmui
