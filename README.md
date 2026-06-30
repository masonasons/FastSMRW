# FastSMRW

A ground-up **C++ rewrite** of [FastSM](https://github.com/masonasons/FastSM) — a
fast, accessible Mastodon/Bluesky client built with blind users in mind.

The project is structured like the Apple port: one **portable C++ shared core**
(`fastsm_core`) consumed by native front ends. The first front end is a **pure
Win32** Windows app. The design goal is minimal CPU/RAM with fast, cache-first
startup, and — over the milestones — the macOS-style UX (←/→ to move between
timelines, single-key post actions) and the configurable "invisible interface"
for screen-reader users.

The OAuth client/source registered with servers is **`FastSMRW`** (it appears as
"via FastSMRW" on posted Mastodon statuses).

## Status — M0 (scaffolding)

Builds end-to-end into:

- `fastsm_core.lib` — the shared core (interfaces stubbed; models/networking/
  timeline land in M1).
- `FastSMRW.exe` — a Win32 shell: themed, DPI-aware main window split into a
  left **Timelines** list and a right **Timeline** posts list (both report-mode
  ListViews, natively accessible to screen readers).
- `fastsm_tests.exe` — a tiny dependency-free unit-test runner.

See `docs`/the plan for the M1+ feature list (accounts, cache-first home
timeline, compose/boost/favorite, then UX parity and the invisible interface).

## Layout

```
core/         fastsm_core static library (portable C++20 + Windows impls)
  include/fastsm/   public API headers the front ends link against
  src/              implementation (models, net, platform, timeline, ...)
windows/      pure Win32 front end (links fastsm_core)
tests/        core unit tests
third_party/  vendored single-header libs (nlohmann/json, miniaudio)
```

## Dependencies

Native + vendored only — **no package manager**:

- **WinHTTP** — networking/TLS (built in)
- **DPAPI** (`crypt32`) — encrypt stored credentials at rest (built in)
- **Windows Compression API** — timeline cache compression (built in; used later)
- **nlohmann/json** — JSON (vendored single header)
- **miniaudio** — soundpack/earcon playback (vendored single header)

## Building

Requires the **MSVC "Desktop development with C++"** toolset (Visual Studio 2022
or Build Tools). No CMake — the build is a direct `cl`/`lib` batch script in the
same style as FastPlay; it locates Visual Studio via `vswhere` and sets up the
x64 environment with `vcvars64.bat` itself, so you can run it from any prompt:

```bat
build.bat            :: Release build  -> build\release\
build.bat debug      :: Debug build (/MTd, symbols)
build.bat test       :: Build + run the unit tests
build.bat clean      :: Remove the build\ tree
```

Outputs: `build\<config>\FastSMRW.exe`, `fastsm_core.lib`, `fastsm_tests.exe`.
The CRT is linked statically (`/MT`) so the app ships without a VC redist.
