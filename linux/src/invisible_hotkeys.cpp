#include "invisible_hotkeys.hpp"

#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <unistd.h>

#include <cstring>
#include <fstream>
#include <sstream>

#include <glib.h>
#include <gdk/gdkx.h>
#include <X11/XKBlib.h>

namespace fastsmgtk {

namespace {

// Linux keycode -> the core's canonical key name (input/keymap.cpp). Physical
// QWERTY positions, like the original FastSM's table.
const char* key_name(unsigned code) {
    switch (code) {
    // clang-format off
    case KEY_A: return "a"; case KEY_B: return "b"; case KEY_C: return "c";
    case KEY_D: return "d"; case KEY_E: return "e"; case KEY_F: return "f";
    case KEY_G: return "g"; case KEY_H: return "h"; case KEY_I: return "i";
    case KEY_J: return "j"; case KEY_K: return "k"; case KEY_L: return "l";
    case KEY_M: return "m"; case KEY_N: return "n"; case KEY_O: return "o";
    case KEY_P: return "p"; case KEY_Q: return "q"; case KEY_R: return "r";
    case KEY_S: return "s"; case KEY_T: return "t"; case KEY_U: return "u";
    case KEY_V: return "v"; case KEY_W: return "w"; case KEY_X: return "x";
    case KEY_Y: return "y"; case KEY_Z: return "z";
    case KEY_1: return "1"; case KEY_2: return "2"; case KEY_3: return "3";
    case KEY_4: return "4"; case KEY_5: return "5"; case KEY_6: return "6";
    case KEY_7: return "7"; case KEY_8: return "8"; case KEY_9: return "9";
    case KEY_0: return "0";
    case KEY_UP: return "up"; case KEY_DOWN: return "down";
    case KEY_LEFT: return "left"; case KEY_RIGHT: return "right";
    case KEY_HOME: return "home"; case KEY_END: return "end";
    case KEY_PAGEUP: return "pageup"; case KEY_PAGEDOWN: return "pagedown";
    case KEY_INSERT: return "insert"; case KEY_DELETE: return "delete";
    case KEY_ENTER: return "return"; case KEY_ESC: return "escape";
    case KEY_SPACE: return "space"; case KEY_TAB: return "tab";
    case KEY_BACKSPACE: return "back"; case KEY_COMPOSE: return "apps";
    case KEY_PAUSE: return "pause"; case KEY_SYSRQ: return "printscreen";
    case KEY_SLASH: return "/"; case KEY_SEMICOLON: return ";";
    case KEY_LEFTBRACE: return "["; case KEY_RIGHTBRACE: return "]";
    case KEY_BACKSLASH: return "\\"; case KEY_APOSTROPHE: return "'";
    case KEY_EQUAL: return "="; case KEY_COMMA: return ",";
    case KEY_MINUS: return "-"; case KEY_DOT: return "."; case KEY_GRAVE: return "`";
        // clang-format on
    default:
        if (code >= KEY_F1 && code <= KEY_F10) {
            static const char* f[] = {"f1", "f2", "f3", "f4", "f5",
                                      "f6", "f7", "f8", "f9", "f10"};
            return f[code - KEY_F1];
        }
        if (code == KEY_F11)
            return "f11";
        if (code == KEY_F12)
            return "f12";
        return nullptr;
    }
}

// Keyboards per the original: /proc/bus/input/devices blocks whose EV bitmask
// has the EV_REP bit, taking the block's eventN handler.
std::vector<std::string> keyboard_devices() {
    std::vector<std::string> out;
    std::ifstream in("/proc/bus/input/devices");
    std::string line, event;
    bool has_rep = false;
    auto flush = [&] {
        if (has_rep && !event.empty())
            out.push_back("/dev/input/" + event);
        event.clear();
        has_rep = false;
    };
    while (std::getline(in, line)) {
        if (line.empty()) {
            flush();
        } else if (line.rfind("H: Handlers=", 0) == 0) {
            std::stringstream ss(line.substr(12));
            std::string tok;
            while (ss >> tok)
                if (tok.rfind("event", 0) == 0)
                    event = tok;
        } else if (line.rfind("B: EV=", 0) == 0) {
            const unsigned long mask = std::stoul(line.substr(6), nullptr, 16);
            has_rep = (mask >> EV_REP) & 1;
        }
    }
    flush();
    return out;
}

} // namespace

bool InvisibleHotkeys::start(std::unordered_map<std::string, std::string> bindings,
                             ActionFn on_action, std::string& error) {
    stop();
    bool denied = false;
    for (const auto& path : keyboard_devices()) {
        const int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK);
        if (fd >= 0)
            fds_.push_back(fd);
        else if (errno == EACCES)
            denied = true;
    }
    if (fds_.empty()) {
        if (start_x11(bindings)) {
            on_action_ = std::move(on_action);
            running_.store(true);
            return true;
        }
        error = denied ? "Invisible interface: no permission to read the keyboard. Add your "
                         "user to the input group and log in again."
                       : "Invisible interface: no keyboard devices found.";
        return false;
    }
    if (pipe(wake_pipe_) != 0) {
        stop();
        error = "Invisible interface: could not start.";
        return false;
    }
    bindings_ = std::move(bindings);
    on_action_ = std::move(on_action);
    running_.store(true);
    thread_ = std::thread([this] { run(); });
    return true;
}

void InvisibleHotkeys::stop() {
    running_.store(false);
    stop_x11();
    if (wake_pipe_[1] >= 0) {
        const char x = 'x';
        [[maybe_unused]] ssize_t n = ::write(wake_pipe_[1], &x, 1);
    }
    if (thread_.joinable())
        thread_.join();
    for (int fd : fds_)
        ::close(fd);
    fds_.clear();
    for (int& fd : wake_pipe_) {
        if (fd >= 0)
            ::close(fd);
        fd = -1;
    }
}

bool InvisibleHotkeys::start_x11(
    const std::unordered_map<std::string, std::string>& bindings) {
    auto* display = gdk_display_get_default();
    if (!GDK_IS_X11_DISPLAY(display))
        return false;

    auto* xdisplay = gdk_x11_display_get_xdisplay(display);
    auto* root = gdk_get_default_root_window();
    const auto xid = gdk_x11_window_get_xid(root);
    const unsigned alt = XkbKeysymToModifiers(xdisplay, XK_Alt_L) |
                         XkbKeysymToModifiers(xdisplay, XK_Alt_R);
    const unsigned super = XkbKeysymToModifiers(xdisplay, XK_Super_L) |
                           XkbKeysymToModifiers(xdisplay, XK_Super_R);
    x11_locks_ = LockMask | XkbKeysymToModifiers(xdisplay, XK_Num_Lock) |
                 XkbKeysymToModifiers(xdisplay, XK_Scroll_Lock);

    for (const auto& [combo, action] : bindings) {
        const auto split = combo.rfind('+');
        if (split == std::string::npos)
            continue;
        unsigned modifiers = 0;
        std::istringstream tokens(combo.substr(0, split));
        for (std::string token; std::getline(tokens, token, '+');) {
            if (token == "control") modifiers |= ControlMask;
            else if (token == "shift") modifiers |= ShiftMask;
            else if (token == "alt" && alt) modifiers |= alt;
            else if (token == "win" && super) modifiers |= super;
            else { modifiers = 0; break; }
        }
        // Never take over ordinary typing in another window.
        if (!(modifiers & (ControlMask | alt | super)))
            continue;

        const auto name = combo.substr(split + 1);
        for (unsigned code = 1; code + 8 <= 255; ++code) {
            const char* known = key_name(code);
            if (!known || name != known)
                continue;
            // Xorg evdev and Xvfb use the Linux keycode plus eight.
            const unsigned key = code + 8;
            const unsigned id = (modifiers << 8) | key;
            if (x11_actions_.contains(id))
                break;
            gdk_x11_display_error_trap_push(display);
            for (unsigned locks = x11_locks_;; locks = (locks - 1) & x11_locks_) {
                XGrabKey(xdisplay, key, modifiers | locks, xid, False,
                         GrabModeAsync, GrabModeAsync);
                if (!locks) break;
            }
            if (gdk_x11_display_error_trap_pop(display) == 0) {
                x11_actions_.emplace(id, action);
            } else {
                for (unsigned locks = x11_locks_;; locks = (locks - 1) & x11_locks_) {
                    XUngrabKey(xdisplay, key, modifiers | locks, xid);
                    if (!locks) break;
                }
                g_warning("FastSMRW could not register X11 shortcut %s", combo.c_str());
            }
            break;
        }
    }
    if (x11_actions_.empty())
        return false;
    x11_root_ = root;
    gdk_window_add_filter(root, filter_x11, this);
    return true;
}

void InvisibleHotkeys::stop_x11() {
    if (!x11_root_)
        return;
    gdk_window_remove_filter(x11_root_, filter_x11, this);
    auto* display = gdk_x11_display_get_xdisplay(gdk_window_get_display(x11_root_));
    const auto root = gdk_x11_window_get_xid(x11_root_);
    for (const auto& [id, action] : x11_actions_) {
        for (unsigned locks = x11_locks_;; locks = (locks - 1) & x11_locks_) {
            XUngrabKey(display, id & 255, (id >> 8) | locks, root);
            if (!locks) break;
        }
    }
    XFlush(display);
    x11_actions_.clear();
    x11_root_ = nullptr;
}

GdkFilterReturn InvisibleHotkeys::filter_x11(GdkXEvent* event, GdkEvent*, gpointer data) {
    auto* self = static_cast<InvisibleHotkeys*>(data);
    const auto* xevent = static_cast<const XEvent*>(event);
    if (xevent->type != KeyPress)
        return GDK_FILTER_CONTINUE;
    const auto& key = xevent->xkey;
    const unsigned mods = (key.state & 255) & ~self->x11_locks_;
    const auto match = self->x11_actions_.find((mods << 8) | key.keycode);
    if (match == self->x11_actions_.end())
        return GDK_FILTER_CONTINUE;
    const auto action = match->second;
    self->on_action_(action, key.time);
    return GDK_FILTER_REMOVE;
}

void InvisibleHotkeys::run() {
    bool ctrl = false, alt = false, shift = false, win = false;
    std::vector<pollfd> pfds;
    for (int fd : fds_)
        pfds.push_back({fd, POLLIN, 0});
    pfds.push_back({wake_pipe_[0], POLLIN, 0});

    while (running_.load()) {
        if (::poll(pfds.data(), pfds.size(), -1) <= 0)
            continue;
        for (size_t i = 0; i + 1 < pfds.size(); ++i) {
            if (!(pfds[i].revents & POLLIN))
                continue;
            input_event ev{};
            while (::read(pfds[i].fd, &ev, sizeof(ev)) == sizeof(ev)) {
                if (ev.type != EV_KEY)
                    continue;
                const bool down = ev.value != 0; // 1 press, 2 repeat
                switch (ev.code) {
                case KEY_LEFTCTRL: case KEY_RIGHTCTRL: ctrl = down; continue;
                case KEY_LEFTALT: case KEY_RIGHTALT: alt = down; continue;
                case KEY_LEFTSHIFT: case KEY_RIGHTSHIFT: shift = down; continue;
                case KEY_LEFTMETA: case KEY_RIGHTMETA: win = down; continue;
                default:
                    break;
                }
                if (ev.value != 1)
                    continue; // fire on the initial press only
                const char* name = key_name(ev.code);
                if (!name)
                    continue;
                std::string combo;
                if (ctrl)
                    combo += "control+";
                if (alt)
                    combo += "alt+";
                if (shift)
                    combo += "shift+";
                if (win)
                    combo += "win+";
                combo += name;
                const auto it = bindings_.find(combo);
                if (it == bindings_.end())
                    continue;
                struct Fire {
                    InvisibleHotkeys* self;
                    std::string action;
                };
                g_idle_add(
                    +[](gpointer data) -> gboolean {
                        auto* f = static_cast<Fire*>(data);
                        if (f->self->running_.load() && f->self->on_action_)
                            f->self->on_action_(f->action, GDK_CURRENT_TIME);
                        delete f;
                        return G_SOURCE_REMOVE;
                    },
                    new Fire{this, it->second});
            }
        }
    }
}

} // namespace fastsmgtk
