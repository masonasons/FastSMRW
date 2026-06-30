#pragma once

#include <windows.h>

// Custom window messages.
constexpr UINT WM_APP_DISPATCH = WM_APP + 1; // drain the main-thread executor queue
