//
//  main.swift
//
//  Programmatic entry point — no storyboard. Keeps the app fully code-driven,
//  which is easier to reason about for an accessibility-first AppKit app.
//

import AppKit

// Top-level code runs on the main thread at process start, so it is safe to
// assume main-actor isolation to construct the (main-actor) delegate. The
// delegate is held by this global because NSApplication.delegate is weak.
let delegate = MainActor.assumeIsolated { AppDelegate() }

MainActor.assumeIsolated {
    let application = NSApplication.shared
    application.delegate = delegate
    application.setActivationPolicy(.regular)
    application.run()
}
