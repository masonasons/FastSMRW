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

## Status — M1 (vertical slice)

Working end-to-end (both platforms behind one `SocialAccount`):

- **Add accounts** — Mastodon (browser OAuth via a 127.0.0.1 loopback redirect,
  source `FastSMRW`) and Bluesky (handle + app password).
- **Cache-first home timeline** — cached rows paint instantly, then refresh.
- **Compose / reply**, **boost / favorite** (optimistic, with revert on failure).
- **Keyboard UX** — ←/→ switch timelines, ↑/↓ navigate rows, R/B/F actions, Tab
  between panes; earcons via miniaudio.
- **Persistence** — accounts/tokens in DPAPI-encrypted `config.json`; per-timeline
  on-disk cache.
- **Client-side filtering** is built into the `TimelineController` chokepoint
  (raw rows vs. the filtered view) so filters are trivial to add.
- 134 core unit checks (models, mapping, util, store, presentation).

Deferred to later milestones: full timeline set + movement units + streaming
(M2), and the configurable "invisible interface" speech via UIA (M3).

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
