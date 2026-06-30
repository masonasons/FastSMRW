#pragma once

#include <string>

#include <windows.h>

#include "fastsm/speech/speaker.hpp"

#include "app_controller.hpp"
#include "compose_dialog.hpp"
#include "win_executor.hpp"

namespace fastsmui {

// The main application window: a left "Timelines" list and a right virtual
// "Timeline" posts list. Implements AppView so the controller can push updates.
class MainWindow : public AppView {
public:
    MainWindow(HINSTANCE inst, WinExecutor* exec);

    bool create();
    HWND hwnd() const { return hwnd_; }
    HACCEL accel() const { return accel_; }
    void set_app(AppController* app) { app_ = app; }
    void set_speaker(fastsm::speech::Speaker* speaker) { speaker_ = speaker; }
    void set_enter_to_send(bool v) { enter_to_send_ = v; }
    void cycle_focus(); // Tab between the two panes

    // AppView
    void timelines_rebuilt() override;
    void current_timeline_changed() override;
    void timeline_updated(fastsm::TimelineController* tc) override;
    void announce(const std::string& message) override;

private:
    static LRESULT CALLBACK WndProcStatic(HWND, UINT, WPARAM, LPARAM);
    LRESULT WndProc(UINT, WPARAM, LPARAM);

    void create_children();
    void layout();
    void populate_timelines_list();
    void bind_current_to_view();
    int selected_row() const;
    const fastsm::Status* selected_status() const; // actionable status of selected row
    void on_view_keydown(int vk);
    void handle_command(int id);
    void do_boost();
    void do_favorite();
    void do_reply();
    void do_quote();
    void do_edit();
    void do_new_post();
    void do_post_info();
    void do_new_timeline();
    void do_add_account();
    void about();
    // Builds the compose request for a mode and posts/edits the result.
    void present_compose(ComposeMode mode, const fastsm::Status* target);

    HINSTANCE inst_;
    WinExecutor* exec_;
    AppController* app_ = nullptr;
    fastsm::speech::Speaker* speaker_ = nullptr;
    bool enter_to_send_ = false;
    HWND hwnd_ = nullptr;
    HWND timelines_list_ = nullptr;
    HWND timeline_view_ = nullptr;
    HACCEL accel_ = nullptr;
    std::wstring scratch_;             // backing store for virtual-list item text
    std::vector<std::string> rendered_ids_; // last rendered row ids (reload guard)
    bool updating_selection_ = false;
};

} // namespace fastsmui
