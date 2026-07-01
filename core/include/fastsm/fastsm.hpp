#pragma once

// FastSMRW shared core — umbrella header.
//
// The core is a portable C++20 static library that backs native front ends
// (Win32 first). It mirrors the layering of the Apple FastSMCore framework:
// Models / Platform / Auth / Networking / Store / Timeline / Presentation / Util.

namespace fastsm {

// Semantic version string of the core library, e.g. "0.0.1".
const char* version();

// Short git commit the binary was built from (e.g. "a1b2c3d"), or "" for a local
// build without the commit embedded. Used by the "latest" update branch to tell
// one rolling build from another.
const char* build_commit();

} // namespace fastsm
