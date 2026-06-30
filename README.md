# FastSMRW

A ground-up rewrite of [FastSM](https://github.com/masonasons/FastSM) — a
fast, accessible Mastodon/Bluesky client built with blind users in mind.

Redesigned to have a single c++ multiplatform core with platform specific UIs.

## Dependencies

Native + vendored only — **no package manager**:

- **WinHTTP** — networking/TLS (built in)
- **DPAPI** (`crypt32`) — encrypt stored credentials at rest (built in)
- **nlohmann/json** — JSON (vendored single header)
- **miniaudio** — soundpack/earcon playback (vendored single header)

## Building

Requires the **MSVC "Desktop development with C++"** toolset (Visual Studio 2022
or Build Tools). No CMake — the build is a direct `cl`/`lib` batch script in the
same style as FastPlay; it locates Visual Studio via `vswhere` and sets up the
x64 environment with `vcvars64.bat` itself, so you can run it from any prompt.

Dependencies are **not committed** — fetch them first (the build also auto-runs
this if the headers are missing):

```bat
download-deps.bat    :: fetch vendored single-headers + clone UniversalSpeech
```

Then build:

```bat
build.bat            :: Release build  -> build\release\  (+ dist\ run folder)
build.bat debug      :: Debug build (/MTd, symbols)
build.bat test       :: Build + run the unit tests
build.bat clean      :: Remove the build\ tree
```

Outputs: `build\<config>\FastSMRW.exe`, `fastsm_core.lib`, `fastsm_tests.exe`.
The CRT is linked statically (`/MT`) so the app ships without a VC redist.
