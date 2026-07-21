# FastSMRW

A ground-up rewrite of [FastSM](https://github.com/masonasons/FastSM) — a fast,
accessible Mastodon/Bluesky client built with blind users in mind.

FastSMRW is a single portable **C++ core** (`fastsm_core`) shared by several
native front ends. The core owns everything that isn't inherently UI —
networking, accounts, timelines, parsing, filtering, settings, sound, and all
string/speech composition. Each app is a thin, platform-native client that only
draws windows and forwards keystrokes.

Front ends today:

- **Windows** — pure Win32 (no frameworks), the reference front end.
- **Android** — Kotlin + Jetpack Compose.
- **macOS** — Swift + AppKit.

## Design goals

- **Accessibility first** — screen-reader output, full keyboard control, and
  earcons are core features, not afterthoughts.
- **Low footprint** — minimal CPU/RAM and fast, cache-first startup.
- **Parity everywhere** — every feature ships in every front end, with the apps
  matching each other in behavior and layout as closely as each platform allows.

A few things worth calling out:

- **Invisible interface** (Windows) — global hotkeys, a low-level key hook, or a
  "layer" mode let you drive FastSMRW without the window ever having focus.
- **Soundpacks / earcons** — navigating a post plays semantic earcons (image,
  media, mention, pinned, poll); packs are folders of `.ogg` (also `.wav`/`.mp3`)
  files. A default pack ships with the app; drop your own into the user
  soundpacks folder and pick it in settings.
- **Auto-read** and **copy** — optionally read new posts aloud as they arrive,
  and copy the focused post/user/notification with configurable templates.

## Architecture

The core is driven through a **C ABI command/event boundary**, not C++ classes.
The entire public surface is
[`core/include/fastsm/capi/fastsm_core.h`](core/include/fastsm/capi/fastsm_core.h)
(`create` / `set_event_sink` / `dispatch` / `destroy`): a front end submits
**commands** as JSON and renders **events** as JSON. `CoreSession`
(`core/src/session/core_session.cpp`) is the orchestration that handles every
command and emits every event on its own core-loop thread.

Adding a feature is therefore: a new command + event(s) in the core once, then
wiring the UI to send/render them in **each** app — never engine logic in a front
end. A future iOS/Linux/other UI binds the same core the same way.

See [`CLAUDE.md`](CLAUDE.md) for the full architecture and contribution rules.

## Dependencies

Native + vendored only — **no package manager**:

- **WinHTTP** — networking/TLS on Windows (built in; TLS 1.2/1.3 opted in for
  Windows 7). The other front ends use their platform HTTP stacks.
- **DPAPI** (`crypt32`) — encrypt stored credentials at rest on Windows (built in).
- **nlohmann/json** — JSON (vendored single header).
- **miniaudio** — audio playback/mixing (vendored single header).
- **stb_vorbis** — OGG Vorbis decoding for soundpacks, since miniaudio has no
  Vorbis decoder (vendored single file).
- **UniversalSpeech** — screen-reader/speech output on Windows, built from source
  (samtupy/UniversalSpeechMSVCStatic). If it can't be built, speech is disabled
  and the rest of the app still works.

The vendored single-headers live in the gitignored `deps/` folder and are **not
committed**. `download-deps.bat` fetches them and builds UniversalSpeech;
`build.bat` auto-runs it when the headers are missing.

## Building — Windows

Requires the **MSVC "Desktop development with C++"** toolset (Visual Studio 2022
or Build Tools). No CMake — the build is a direct `cl`/`lib` batch script in the
style of FastPlay; it locates Visual Studio via `vswhere` and sets up the x64
environment with `vcvars64.bat` itself, so you can run it from any prompt.

```bat
download-deps.bat    :: fetch vendored headers + build UniversalSpeech (auto-run by build.bat)

build.bat            :: Release build -> build\release\ + the dist\ run folder
build.bat debug      :: Debug build (/MTd, symbols)
build.bat test       :: Build + run the unit tests
build.bat clean      :: Remove the build\ tree
```

`build.bat` produces `fastsm_core.lib`, `FastSMRW.exe`, and assembles the
`dist\` run folder (exe + bundled default soundpack + speech DLLs) — run the app
from `dist\FastSMRW.exe`. When Inno Setup is installed it also builds
`FastSMRWInstaller.exe`. The CRT is linked statically (`/MT`) so the app ships
without a VC redist. Target is **Windows 7 and up**.

## Building — Android

Standard Gradle project under `android/` (Kotlin + Compose; the shared core is
built for `arm64-v8a` and `x86_64` via the NDK).

```bat
cd android
gradlew.bat assembleRelease    :: -> android\app\build\...\FastSMRW.apk
```

## Building — macOS

Swift + AppKit, driven by `macos/build.sh` (needs Xcode and
`brew install xcodegen`; the `.xcodeproj` is generated from `project.yml`).

```sh
macos/build.sh            # Debug build
macos/build.sh release    # Release build
macos/package-dmg.sh      # Package a .dmg
```
