# FastSMRW — working instructions

FastSMRW is a C++ rewrite of FastSM: a fast, accessible Mastodon/Bluesky client
for blind users. A portable core (`fastsm_core`) backs native front ends; the
first is a **pure Win32** Windows app.

## Changelog — REQUIRED for every user-noticeable change

After a change that a user would notice, add a short, **human-readable,
non-technical** entry to `docs/changelog.txt` describing what changed from a
user's point of view (what they can now do or what got fixed) — not how it was
implemented. Write for an end user, not a developer.

**Keep each entry to ONE line.** One sentence per change — no multi-line
paragraphs. If it needs more than a line, it's too detailed for a changelog.

Only log changes a user would notice. Purely internal work (refactors, build/CI
tweaks, test-only changes) does **not** get a changelog entry.

- Put entries under the **current version heading** at the top of the file.
- **Do NOT bump the version number** unless I explicitly tell you to. Keep adding
  to the current (unreleased) version until then.
- The version number's source of truth is `core/src/version.cpp`
  (`fastsm::version()`). When I ask for a bump, update it there and start a new
  heading in the changelog.
- Group related entries; avoid jargon (no "WinHTTP", "DTO", "DLGPROC", etc.).

## Design goal: match the Mac app

Behavior and UX should mirror the Apple port at `C:\git\FastSMApple` (Swift
`FastSMCore` + AppKit). **Read the relevant Mac source before implementing UX or
behavior** rather than guessing — keyboard model, presenter strings, position
handling, compose, sounds, etc. The original Python app is at `C:\stuff\FastSM`.

Priorities: accessibility first (screen readers, keyboard-only, earcons),
minimal CPU/RAM, fast cache-first startup.

## Feature parity: every feature ships in every front end — REQUIRED

Anything implemented for FastSM must work in **all** front ends — Windows,
Android, and any future app (iOS, etc.). A feature is not "done" until it's
usable in every existing front end, not just the one you started in. Because the
logic lives in the shared core, adding a feature is: core command/event(s) once,
then wire the UI in **each** app. When you add a feature to one app, add it to
the others in the same change (or say clearly what's still missing and why).

The apps should also **match each other** in behavior and UX as closely as each
platform sensibly allows — same settings, same options, same organization (e.g.
Android settings panels mirror the Windows settings pages). Don't reinvent a
feature's shape per platform.

If full parity isn't a good idea for a specific feature — it's genuinely
platform-specific, technically impossible on a platform, or would be a worse
experience there — **stop and talk to me about it** rather than silently
shipping it in one app only. (Example: the update flow — the *check* is shared,
but the desktop downloads an installer while Android opens an APK; the "latest"
commit-based channel can't work on Android, so it's omitted there. Those are
deliberate, discussed exceptions, not drift.)

## Architecture rules

- **The core is driven through a C ABI command/event boundary, not C++ classes.**
  `core/include/fastsm/capi/fastsm_core.h` is the whole public surface
  (`create`/`set_event_sink`/`dispatch`/`destroy`); front ends submit **commands**
  as JSON and render **events** as JSON. `CoreSession`
  (`core/src/session/core_session.cpp`) is the orchestration that handles every
  command and emits every event, running engine state on its own core-loop thread.
  The Win32 app is a **pure client** of this boundary (it holds no engine logic),
  which is exactly how a future Swift/Kotlin UI binds the same core. Adding a
  feature = a new command + event(s) in `CoreSession`, plus UI to send/render
  them — never engine logic in `windows/`.
- **Features live in the core; only UI lives in the apps.** Anything that isn't
  inherently UI — models, networking, account/timeline logic, settings, string
  and speech composition, sound, filtering, parsing — goes in `fastsm_core` so
  every front end shares it. The Win32 app (and future front ends) only do
  platform UI: windows, dialogs, controls, menus, and rendering what the core
  produces. If you're tempted to put logic in `windows/`, put it in `core/`
  instead and call it from the UI.
- **All string and speech composition lives in the core.** Don't assemble post
  text, labels, or spoken strings in the UI layer — the UI renders what the core
  returns and only provides settings to configure it.
- The UI is **pure Win32** (no frameworks). Keep it light.
- Keep client-side filtering trivial: the `TimelineController` exposes a filter
  chokepoint (raw rows vs. the visible list).

## Build & test

No CMake. FastPlay-style batch build that self-locates MSVC via vcvars.

- `download-deps.bat` — fetches dependencies into the gitignored `deps/`
  (single-header libs + builds UniversalSpeech). `build.bat` auto-runs it if the
  headers are missing. Dependencies are **not committed**.
- `build.bat [debug] [test] [clean]` — builds `fastsm_core.lib`, `FastSMRW.exe`,
  and assembles the `dist/` run folder (exe + bundled default soundpack + speech
  DLLs). `test` also runs the unit tests; keep them green.
- Run the app from `dist\FastSMRW.exe`.

When adding source files: add core `.cpp` to `CORE_SRC` and app `.cpp` to
`APP_SRC` in `build.bat`; add test files to the test compile line and
declare/call them in `tests/main.cpp`. Keep source basenames unique within a
target (objects are flattened per target).

## Compatibility

- Target **Windows 7+**. Don't statically import APIs newer than Win7
  (e.g. `GetDpiForWindow`); load them dynamically with a fallback. TLS 1.2/1.3 is
  enabled explicitly for Win7.

## Notes

- I'll verify network/UI behavior live (real accounts, screen reader); you can't
  from here. Say plainly what's verified vs. needs a live run.
- Commit only when I ask. End commit messages with the Co-Authored-By line.
