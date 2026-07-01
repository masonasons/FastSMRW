#pragma once

#include <string>

#include <windows.h>

namespace fastsmui {

// The per-timeline client-side display filter (post-type toggles + a text
// substring), ported from FastSM for Windows. Mirrors the core's ClientFilter.
struct ClientFilterValues {
    bool original = true;
    bool replies = true;
    bool replies_to_me = true;
    bool threads = true;
    bool boosts = true;
    bool quotes = true;
    bool media = true;
    bool no_media = true;
    bool my_posts = true;
    bool my_replies = true;
    std::wstring text;
};

// What the user chose in the Client Filters dialog.
enum class ClientFilterAction { Cancel, Apply, Clear };

// Show the dialog prefilled from `values`; on Apply, `values` is updated in place.
ClientFilterAction show_client_filter_dialog(HWND parent, HINSTANCE inst,
                                             ClientFilterValues& values);

} // namespace fastsmui
