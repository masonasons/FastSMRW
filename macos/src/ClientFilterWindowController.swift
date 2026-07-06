//
//  ClientFilterWindowController.swift
//
//  Per-timeline client-side filter: check which kinds of posts to show and,
//  optionally, keep only posts containing some text. Maps to set_client_filter /
//  clear_client_filter. Trivial by design — the heavy filtering lives in the
//  core's TimelineController filter chokepoint.
//

import AppKit

@MainActor
final class ClientFilterWindowController: NSWindowController {
    private let state: AppState
    private let textField = NSTextField()
    // (label, JSON key, checkbox) in display order.
    private var toggles: [(String, NSButton)] = []

    // The filter categories, in order: label, JSON key, current value.
    private let categories: [(String, String, Bool)]

    init(state: AppState, filter: ClientFilter) {
        self.state = state
        categories = [
            ("Original posts", "original", filter.original),
            ("Replies", "replies", filter.replies),
            ("Replies to me", "replies_to_me", filter.repliesToMe),
            ("Threads (self-replies)", "threads", filter.threads),
            ("Boosts", "boosts", filter.boosts),
            ("Quotes", "quotes", filter.quotes),
            ("Posts with media", "media", filter.media),
            ("Posts without media", "no_media", filter.noMedia),
            ("My posts", "my_posts", filter.myPosts),
            ("My replies", "my_replies", filter.myReplies),
        ]
        let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 380, height: 460),
                              styleMask: [.titled], backing: .buffered, defer: false)
        super.init(window: window)
        window.title = "Filter Timeline"
        buildUI(text: filter.text)
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    func beginSheet(for parent: NSWindow, completion: @escaping () -> Void) {
        parent.beginSheet(window!) { _ in completion() }
    }

    private func dismiss() {
        guard let window, let parent = window.sheetParent else { return }
        parent.endSheet(window)
    }

    private func buildUI(text: String) {
        guard let content = window?.contentView else { return }
        let heading = NSTextField(labelWithString: "Show:")
        var views: [NSView] = [heading]
        for (label, key, on) in categories {
            let box = NSButton(checkboxWithTitle: label, target: nil, action: nil)
            box.state = on ? .on : .off
            toggles.append((key, box))
            views.append(box)
        }

        textField.stringValue = text
        textField.placeholderString = "Only show posts containing…"
        textField.setAccessibilityLabel("Only show posts containing")
        textField.widthAnchor.constraint(greaterThanOrEqualToConstant: 300).isActive = true
        views.append(textField)

        let apply = NSButton(title: "Apply", target: self, action: #selector(applyFilter))
        apply.bezelStyle = .rounded
        apply.keyEquivalent = "\r"
        let clear = NSButton(title: "Clear", target: self, action: #selector(clearFilter))
        clear.bezelStyle = .rounded
        let cancel = NSButton(title: "Cancel", target: self, action: #selector(cancel))
        cancel.bezelStyle = .rounded
        cancel.keyEquivalent = "\u{1b}"
        let buttons = NSStackView(views: [clear, NSView(), cancel, apply])
        buttons.orientation = .horizontal
        buttons.spacing = 8
        views.append(buttons)

        let stack = NSStackView(views: views)
        stack.orientation = .vertical
        stack.alignment = .leading
        stack.spacing = 8
        stack.edgeInsets = NSEdgeInsets(top: 16, left: 16, bottom: 16, right: 16)
        stack.translatesAutoresizingMaskIntoConstraints = false
        content.addSubview(stack)
        NSLayoutConstraint.activate([
            stack.topAnchor.constraint(equalTo: content.topAnchor),
            stack.leadingAnchor.constraint(equalTo: content.leadingAnchor),
            stack.trailingAnchor.constraint(equalTo: content.trailingAnchor),
            stack.bottomAnchor.constraint(equalTo: content.bottomAnchor),
            textField.leadingAnchor.constraint(equalTo: stack.leadingAnchor, constant: 16),
            textField.trailingAnchor.constraint(equalTo: stack.trailingAnchor, constant: -16),
            buttons.leadingAnchor.constraint(equalTo: stack.leadingAnchor, constant: 16),
            buttons.trailingAnchor.constraint(equalTo: stack.trailingAnchor, constant: -16),
        ])
    }

    @objc private func applyFilter() {
        var filter: [String: Any] = [:]
        for (key, box) in toggles { filter[key] = (box.state == .on) }
        filter["text"] = textField.stringValue
        state.setClientFilter(filter)
        dismiss()
    }

    @objc private func clearFilter() {
        state.clearClientFilter()
        dismiss()
    }

    @objc private func cancel() { dismiss() }
}
