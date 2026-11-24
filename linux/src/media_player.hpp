#pragma once

#include <functional>
#include <string>

#include <gtk/gtk.h>
#include <nlohmann/json.hpp>

namespace fastsmgtk {

// The keys-only media player — the GTK twin of windows/src/media_player_window.
// The core plays the media (FastPlay's engine) and says what happens; this
// window is only somewhere for the keys to go: Space play/pause, Left/Right
// seek, Up/Down volume (Page Up/Down by more, Home/End full or silent), P where
// it is, Escape stop. Each is sent to the core as a command. The core's
// media_player events open, retitle and close it (MainWindow::ev_media_player).
//
// Shows the window (or, if one is open, retitles and raises it). `dispatch`
// sends a command.
void show_media_player(GtkWindow* parent, const std::string& title,
                       std::function<void(const nlohmann::json&)> dispatch);
// Retitles an open player ("Playing: <title>"); nothing if none is open.
void retitle_media_player(const std::string& title);
// Closes it without telling the core (the core said it stopped).
void close_media_player();

} // namespace fastsmgtk
