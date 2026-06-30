#pragma once

#include <string>
#include <vector>

#include <windows.h>

#include <nlohmann/json.hpp>

#include "fastsm/capi/fastsm_core.h"
#include "fastsm/speech/speaker.hpp"

#include "compose_dialog.hpp"

namespace fastsmui {

// The main application window: a left "Timelines" list and a right virtual
// "Timeline" posts list. It is a pure client of the core's C ABI: it dispatches
// JSON commands and renders JSON events; it holds no engine logic, only a cache
// of what to draw (so a Swift/Kotlin UI would mirror this exactly).
class MainWindow {
public:
    explicit MainWindow(HINSTANCE inst);

    bool create();
    HWND hwnd() const { return hwnd_; }
    HACCEL accel() const { return accel_; }
    void set_core(fastsm_core* core) { core_ = core; }
    void set_speaker(fastsm::speech::Speaker* speaker) { speaker_ = speaker; }
    void cycle_focus(); // Tab between the two panes

    // Event sink registered with the core (called on the core thread); marshals
    // the JSON onto the UI thread via WM_APP_EVENT.
    static void event_sink(void* user, const char* event_json, size_t len);

private:
    // A cached, render-ready row (the text is the core-composed speech label).
    struct Row {
        std::string id;
        std::wstring text;
        bool favorited = false;
        bool boosted = false;
    };
    struct Timeline {
        std::wstring title;
        std::string kind;
        bool dismissable = false;
        std::vector<Row> rows;
        std::string selected_id; // UI-authoritative remembered position
    };

    static LRESULT CALLBACK WndProcStatic(HWND, UINT, WPARAM, LPARAM);
    LRESULT WndProc(UINT, WPARAM, LPARAM);

    void create_children();
    void layout();
    void populate_timelines_list();
    void bind_current_to_view(bool force = false);
    Timeline* current();
    int index_of_id(const Timeline& tl, const std::string& id) const;
    int selected_row() const;
    std::string selected_id();
    void restore_selection(const std::string& id);
    void on_view_keydown(int vk);
    void handle_command(int id);
    void announce(const std::string& message);

    // Commands.
    void dispatch_cmd(const nlohmann::json& cmd);
    void do_boost();
    void do_favorite();
    void compose(const char* mode); // dispatch compose_context for the selection
    void do_post_info();
    void do_new_timeline();
    void do_add_account();
    void do_settings();
    void about();

    // Events.
    void on_event(const std::string& json);
    void ev_timelines_changed(const nlohmann::json& e);
    void ev_timeline_updated(const nlohmann::json& e);
    void ev_settings(const nlohmann::json& e);
    void ev_compose_context(const nlohmann::json& e);
    void ev_spawnable(const nlohmann::json& e);
    void ev_post_info(const nlohmann::json& e);
    void ev_user_profile(const nlohmann::json& e);
    void ev_user_picker(const nlohmann::json& e);

    HINSTANCE inst_;
    fastsm_core* core_ = nullptr;
    fastsm::speech::Speaker* speaker_ = nullptr;
    HWND hwnd_ = nullptr;
    HWND timelines_list_ = nullptr;
    HWND timeline_view_ = nullptr;
    HACCEL accel_ = nullptr;
    std::wstring scratch_;                   // backing store for virtual-list item text
    std::vector<std::string> rendered_ids_;  // last rendered row ids (reload guard)
    bool updating_selection_ = false;

    std::vector<Timeline> timelines_;
    int current_ = 0;
    nlohmann::json settings_ = nlohmann::json::object(); // cached settings object
    std::vector<std::string> soundpacks_;
    std::vector<std::string> spawnable_kinds_; // parallels the New Timeline dialog
};

} // namespace fastsmui
