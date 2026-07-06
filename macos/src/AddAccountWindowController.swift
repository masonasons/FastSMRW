//
//  AddAccountWindowController.swift
//
//  Sheet for adding an account. Mastodon: enter an instance, then authorize in
//  the browser (the core emits the authorize URL; the fastsm:// redirect comes
//  back through AppDelegate). Bluesky: enter a handle and an app password.
//  The sheet is dismissed by MainWindowController when auth_result reports OK.
//

import AppKit

@MainActor
final class AddAccountWindowController: NSWindowController {
    private let state: AppState

    private let platformPopup = NSPopUpButton()
    private let serverLabel = NSTextField(labelWithString: "Instance:")
    private let serverField = NSTextField()
    private let handleLabel = NSTextField(labelWithString: "Handle:")
    private let handleField = NSTextField()
    private let passwordLabel = NSTextField(labelWithString: "App password:")
    private let passwordField = NSSecureTextField()
    private let progress = NSProgressIndicator()
    private let addButton = NSButton()

    init(state: AppState) {
        self.state = state
        let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 440, height: 260),
                              styleMask: [.titled], backing: .buffered, defer: false)
        super.init(window: window)
        window.title = "Add Account"
        buildUI()
        updateForPlatform()
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    func beginSheet(for parent: NSWindow, completion: @escaping () -> Void) {
        parent.beginSheet(window!) { _ in completion() }
        window?.makeFirstResponder(serverField)
    }

    func dismiss() {
        guard let window, let parent = window.sheetParent else { return }
        parent.endSheet(window)
    }

    private enum Platform: Int { case mastodon = 0, bluesky = 1 }
    private var selectedPlatform: Platform {
        Platform(rawValue: platformPopup.indexOfSelectedItem) ?? .mastodon
    }

    private func buildUI() {
        guard let content = window?.contentView else { return }
        platformPopup.addItems(withTitles: ["Mastodon", "Bluesky"])
        platformPopup.target = self
        platformPopup.action = #selector(platformChanged)
        platformPopup.setAccessibilityLabel("Platform")

        serverField.setAccessibilityLabel("Server")
        handleField.setAccessibilityLabel("Handle")
        passwordField.setAccessibilityLabel("App password")
        serverField.placeholderString = "mastodon.social"
        handleField.placeholderString = "you.bsky.social"

        addButton.title = "Add"
        addButton.bezelStyle = .rounded
        addButton.keyEquivalent = "\r"
        addButton.target = self
        addButton.action = #selector(add)

        let cancelButton = NSButton(title: "Cancel", target: self, action: #selector(cancel))
        cancelButton.bezelStyle = .rounded
        cancelButton.keyEquivalent = "\u{1b}"

        progress.style = .spinning
        progress.controlSize = .small
        progress.isDisplayedWhenStopped = false

        func row(_ label: NSTextField, _ field: NSView) -> NSStackView {
            label.setContentHuggingPriority(.defaultHigh, for: .horizontal)
            let r = NSStackView(views: [label, field])
            r.orientation = .horizontal
            r.spacing = 8
            label.widthAnchor.constraint(equalToConstant: 110).isActive = true
            field.translatesAutoresizingMaskIntoConstraints = false
            (field as? NSTextField)?.widthAnchor.constraint(greaterThanOrEqualToConstant: 240).isActive = true
            return r
        }

        let buttons = NSStackView(views: [progress, cancelButton, addButton])
        buttons.orientation = .horizontal
        buttons.spacing = 8

        let stack = NSStackView(views: [
            row(NSTextField(labelWithString: "Platform:"), platformPopup),
            row(serverLabel, serverField),
            row(handleLabel, handleField),
            row(passwordLabel, passwordField),
            buttons,
        ])
        stack.orientation = .vertical
        stack.alignment = .leading
        stack.spacing = 12
        stack.edgeInsets = NSEdgeInsets(top: 18, left: 18, bottom: 18, right: 18)
        stack.translatesAutoresizingMaskIntoConstraints = false
        content.addSubview(stack)
        NSLayoutConstraint.activate([
            stack.topAnchor.constraint(equalTo: content.topAnchor),
            stack.leadingAnchor.constraint(equalTo: content.leadingAnchor),
            stack.trailingAnchor.constraint(equalTo: content.trailingAnchor),
            stack.bottomAnchor.constraint(equalTo: content.bottomAnchor),
        ])
    }

    @objc private func platformChanged() { updateForPlatform() }

    private func updateForPlatform() {
        let bluesky = selectedPlatform == .bluesky
        handleLabel.superview?.isHidden = !bluesky
        passwordLabel.superview?.isHidden = !bluesky
        serverLabel.stringValue = bluesky ? "Service:" : "Instance:"
        serverField.placeholderString = bluesky ? "https://bsky.social" : "mastodon.social"
    }

    @objc private func add() {
        let server = serverField.stringValue.trimmingCharacters(in: .whitespaces)
        progress.startAnimation(nil)
        addButton.isEnabled = false
        switch selectedPlatform {
        case .mastodon:
            guard !server.isEmpty else { reset(); return }
            state.beginMastodonLogin(instance: server)
        case .bluesky:
            let service = server.isEmpty ? "https://bsky.social" : server
            let handle = handleField.stringValue.trimmingCharacters(in: .whitespaces)
            let password = passwordField.stringValue
            guard !handle.isEmpty, !password.isEmpty else { reset(); return }
            state.addBluesky(service: service, handle: handle, appPassword: password)
        }
    }

    /// Re-enable the form after a failed attempt so the user can retry.
    func reset() {
        progress.stopAnimation(nil)
        addButton.isEnabled = true
    }

    @objc private func cancel() { dismiss() }
}
