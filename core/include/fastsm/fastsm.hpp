#pragma once

// FastSMRW shared core — umbrella header.
//
// The core is a portable C++20 static library that backs native front ends
// (Win32 first). It mirrors the layering of the Apple FastSMCore framework:
// Models / Platform / Auth / Networking / Store / Timeline / Presentation / Util.

namespace fastsm {

// Semantic version string of the core library, e.g. "0.0.1".
const char* version();

} // namespace fastsm
