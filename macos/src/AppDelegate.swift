//
//  AppDelegate.swift
//
//  Boots the core (via AppState), builds the main menu, shows the main window,
//  wires app-level event callbacks (speech, opening URLs), and handles the
//  fastsm:// OAuth redirect. UI-structure event wiring lives in
//  MainWindowController, which owns the window and its sheets.
//

import AppKit

@MainActor
final class AppDelegate: NSObject, NSApplicationDelegate {
    private let state = AppState()
    private var mainWindowController: MainWindowController?
    private var settingsWindowController: SettingsWindowController?
    private var hasBeenActive = false

    func applicationDidFinishLaunching(_ notification: Notification) {
        NSApp.mainMenu = MainMenu.build()

        guard let state else {
            ErrorAlert.present("FastSMRW could not start.",
                               detail: "The core failed to initialize.", in: nil)
            return
        }

        // App-level callbacks; MainWindowController wires the rest.
        state.onAnnounce = { Speech.announce($0) }
        state.onOpenURL = { NSWorkspace.shared.open($0) }
        state.onUpdateStatus = { [weak self] in self?.handleUpdate($0) }

        let controller = MainWindowController(state: state)
        controller.showWindow(nil)
        controller.window?.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
        mainWindowController = controller

        state.start()

		let workspaceNotifications = NSWorkspace.shared.notificationCenter
		workspaceNotifications.addObserver(self, selector: #selector(systemWillSleep(_:)),
			name: NSWorkspace.willSleepNotification, object: nil)
		workspaceNotifications.addObserver(self, selector: #selector(systemDidWake(_:)),
			name: NSWorkspace.didWakeNotification, object: nil)
    }

	@objc private func systemWillSleep(_ notification: Notification) {
		state?.suspendAudio()
	}

	@objc private func systemDidWake(_ notification: Notification) {
		state?.resetAudio()
	}

	func applicationWillTerminate(_ notification: Notification) {
		NSWorkspace.shared.notificationCenter.removeObserver(self)
	}

    func applicationDidBecomeActive(_ notification: Notification) {
        // The initial activation is already covered by start(). On subsequent
        // activations (for example Command-Tabbing back), refresh and begin a
        // fresh server-marker pull so this client follows the active device.
        guard hasBeenActive else {
            hasBeenActive = true
            return
        }
        state?.resume()
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }

    // Custom URL scheme: fastsm://oauth?code=… completes a Mastodon login.
    func application(_ application: NSApplication, open urls: [URL]) {
        for url in urls where url.scheme == "fastsm" {
            let items = URLComponents(url: url, resolvingAgainstBaseURL: false)?.queryItems
            if let code = items?.first(where: { $0.name == "code" })?.value {
                state?.finishMastodonLogin(code: code)
            }
        }
    }

    // MARK: Menu actions

    @objc func showAddAccount(_ sender: Any?) { mainWindowController?.presentAddAccount() }

    @objc func checkForUpdates(_ sender: Any?) { state?.checkForUpdate(silent: false) }

    // Open the Mac user guide on GitHub in the default browser.
    @objc func showHelp(_ sender: Any?) {
        if let url = URL(string:
            "https://github.com/masonasons/FastSMRW/blob/main/README-macOS.md") {
            NSWorkspace.shared.open(url)
        }
    }

    // MARK: Updates

    private func handleUpdate(_ status: UpdateStatus) {
        let window = mainWindowController?.window
        if !status.error.isEmpty {
            if !status.silent {
                ErrorAlert.present("Couldn't check for updates.", detail: status.error, in: window)
            }
            return
        }
        guard status.available else {
            if !status.silent {
                let alert = NSAlert()
                alert.messageText = "You’re up to date."
				alert.informativeText = status.branch == "latest"
					? "You’re running the latest build of FastSMRW."
					: "FastSMRW \(CoreClient.version) is the latest version."
                alert.addButton(withTitle: "OK")
                present(alert, on: window) { _ in }
            }
            return
        }
		guard !status.dmgUrl.isEmpty else {
			if !status.silent {
				ErrorAlert.present("Mac update isn't available yet.",
					detail: "The selected release has no Mac download yet. Check again later.",
					in: window)
			}
			return
		}
        let alert = NSAlert()
		alert.messageText = status.branch == "latest"
			? "New build available: \(status.version)"
			: "Update available: \(status.version)"
		alert.informativeText = status.notes.isEmpty
			? (status.branch == "latest" ? "A new build of FastSMRW is available."
				: "A new version of FastSMRW is available.")
            : status.notes
        alert.addButton(withTitle: "Download")
        alert.addButton(withTitle: "Later")
        present(alert, on: window) { [weak self] response in
            guard response == .alertFirstButtonReturn else { return }
			self?.downloadAndOpenDMG(status.dmgUrl)
        }
    }

    private func present(_ alert: NSAlert, on window: NSWindow?,
                         _ done: @escaping (NSApplication.ModalResponse) -> Void) {
        if let window {
            alert.beginSheetModal(for: window, completionHandler: done)
        } else {
            done(alert.runModal())
        }
    }

    /// Download the .dmg to a temp file and open it (mounts the disk image so the
    /// user can drag the app to Applications).
    private func downloadAndOpenDMG(_ urlString: String) {
        guard let url = URL(string: urlString) else { return }
        Speech.announce("Downloading update…")
        let fail = { DispatchQueue.main.async { Speech.announce("Update download failed.") } }
        URLSession.shared.downloadTask(with: url) { tmp, response, error in
            let status = (response as? HTTPURLResponse)?.statusCode ?? 0
            guard error == nil, let tmp, (200..<300).contains(status) else { fail(); return }
            let dest = FileManager.default.temporaryDirectory
                .appendingPathComponent("FastSMRW-update.dmg")
            try? FileManager.default.removeItem(at: dest)
            guard (try? FileManager.default.moveItem(at: tmp, to: dest)) != nil else { fail(); return }
            DispatchQueue.main.async { NSWorkspace.shared.open(dest) }
        }.resume()
    }

    @objc func showSettings(_ sender: Any?) {
        guard let state else { return }
        if settingsWindowController == nil {
            settingsWindowController = SettingsWindowController(state: state)
        }
        settingsWindowController?.window?.center()
        settingsWindowController?.showWindow(nil)
        settingsWindowController?.window?.makeKeyAndOrderFront(nil)
    }
}
