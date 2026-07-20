//
//  SettingsWindowController.swift
//
//  The preferences window: a toolbar-style tab view mirroring the Windows
//  settings pages (General, Timelines, Audio, Speech, Behavior, Advanced,
//  Confirmation). Each control reads from the core's settings and writes back
//  through update_settings, which the core applies live.
//
//  Deferred: the per-category reorderable "Speech Details" field lists — those
//  need human-readable field names that live in the core but aren't exposed over
//  the event bus yet (a small speech-catalog command would add them).
//

import AppKit

@MainActor
final class SettingsWindowController: NSWindowController {
    init(state: AppState) {
        let emojiOptions = [("Off", "none"), ("Unicode emoji", "unicode"),
                            ("Custom shortcodes", "mastodon"), ("Both", "both")]
        let tabs = NSTabViewController()
        tabs.tabStyle = .toolbar

        func tab(_ title: String, _ symbol: String, _ build: (SettingsPane) -> Void) {
            let pane = SettingsPane(state: state)
            build(pane)
            let item = NSTabViewItem(viewController: pane)
            item.label = title
            item.image = NSImage(systemSymbolName: symbol, accessibilityDescription: title)
            tabs.addTabViewItem(item)
        }

        tab("General", "gearshape") { p in
            p.checkbox("Return key sends the post (off: ⌘Return sends)",
                       key: "enter_to_send", default: false)
        }

        tab("Timelines", "list.bullet") { p in
            p.intRow("Cache limit (posts):", key: "cache_limit", default: 200, min: 0, max: 20000)
            p.popup("Auto-refresh:", options: [("Off", 0), ("Every 30 seconds", 30),
                                               ("Every minute", 60), ("Every 2 minutes", 120),
                                               ("Every 5 minutes", 300)],
                    key: "auto_refresh_seconds", default: 60)
            p.checkbox("Live streaming", key: "streaming_enabled", default: true)
            p.checkbox("Show mentions in Notifications",
                       key: "show_mentions_in_notifications", default: true)
            p.checkbox("Reverse order (newest posts at the bottom)",
                       key: "reverse_timelines", default: false)
            p.checkbox("Automatically load older posts", key: "auto_load_older", default: true)
        }

        tab("Audio", "speaker.wave.2") { p in
            p.checkbox("Play sounds", key: "sounds_enabled", default: true)
            p.checkbox("Play a sound at the top/bottom of a list",
                       key: "boundary_sound", default: true)
            p.popup("Soundpack:", options: state.soundpacks.map { ($0, $0) },
                    key: "soundpack", default: "Default")
            p.slider("Volume:", key: "sound_volume", default: 100, min: 0, max: 100)
            p.button("Open Soundpacks Folder") {
                let dir = state.configDir.appendingPathComponent("soundpacks", isDirectory: true)
                try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
                NSWorkspace.shared.open(dir)
            }
        }

        tab("Earcons", "waveform") { p in
            p.checkbox("Image (post has an image)", key: "earcon_image", default: true)
            p.checkbox("Media (post has video, audio, or a GIF)", key: "earcon_media", default: true)
            p.checkbox("Mention (post mentions you)", key: "earcon_mention", default: true)
            p.checkbox("Pinned (post is pinned to a profile)", key: "earcon_pinned", default: true)
            p.checkbox("Poll (post has a poll)", key: "earcon_poll", default: true)
        }

        tab("Speech", "text.bubble") { p in
            p.popup("Content warnings:", options: [("Hide post text", "hide"),
                                                   ("Show warning, then text", "show"),
                                                   ("Ignore warning", "ignore")],
                    key: "cw_mode", default: "hide")
            p.popup("Emoji in posts:", options: emojiOptions,
                    key: "post_emoji_removal", default: "none")
            p.popup("Emoji in names:", options: emojiOptions,
                    key: "name_emoji_removal", default: "none")
            p.intRow("Max usernames read per post (0 = all):",
                     key: "max_usernames_in_post", default: 0, min: 0, max: 20)
            p.checkbox("Speak absolute times", key: "absolute_time", default: false)
            p.button("Post Fields…") { [weak p] in
                p?.openSpeechDetail("Speech Details — Posts", category: "status")
            }
            p.button("User Fields…") { [weak p] in
                p?.openSpeechDetail("Speech Details — Users", category: "user")
            }
            p.button("Notification Fields…") { [weak p] in
                p?.openSpeechDetail("Speech Details — Notifications", category: "notification")
            }
        }

        tab("Behavior", "hand.tap") { p in
            p.popup("Enter on a post:", options: [("View post info", "post_info"),
                                                  ("View thread", "thread"), ("Reply", "reply"),
                                                  ("Open links", "links")],
                    key: "enter_post_action", default: "post_info")
            p.popup("Enter on a user:", options: [("User actions", "actions"),
                                                  ("View profile", "profile"),
                                                  ("View their timeline", "timeline")],
                    key: "enter_user_action", default: "actions")
            p.popup("Secondary action:", options: [("Play media", "play_media"),
                                                   ("View post info", "post_info"),
                                                   ("View thread", "thread"), ("Reply", "reply"),
                                                   ("Open links", "links")],
                    key: "secondary_post_action", default: "play_media")
            p.checkbox("Keep the media player in the background",
                       key: "media_background", default: false)
            p.checkbox("Move extra mentions to the end of replies",
                       key: "reply_mentions_at_end", default: false)
        }

        tab("Advanced", "wrench.and.screwdriver") { p in
            p.intRow("API pages per fetch:", key: "fetch_pages", default: 3, min: 1, max: 10)
        }

        tab("Confirmation", "checkmark.shield") { p in
            p.checkbox("Confirm boost", key: "confirm_boost", default: false)
            p.checkbox("Confirm un-boost", key: "confirm_unboost", default: false)
            p.checkbox("Confirm favorite", key: "confirm_favorite", default: false)
            p.checkbox("Confirm un-favorite", key: "confirm_unfavorite", default: false)
            p.checkbox("Confirm clearing a timeline", key: "confirm_clear_timeline", default: true)
            p.checkbox("Confirm block", key: "confirm_block", default: true)
            p.checkbox("Confirm un-block", key: "confirm_unblock", default: false)
            p.checkbox("Confirm deleting a post", key: "confirm_delete_post", default: true)
        }

        let window = NSWindow(contentViewController: tabs)
        window.title = "Settings"
        window.styleMask = [.titled, .closable]
        super.init(window: window)
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }
}

/// A single settings tab: a vertical stack of controls that read the core's
/// settings and write changes back through `AppState.updateSettings`.
@MainActor
final class SettingsPane: NSViewController {
    private let state: AppState
    private let stack = NSStackView()
    private var handlers: [ObjectIdentifier: (NSControl) -> Void] = [:]
    private var speechDetailController: SpeechDetailsWindowController?

    init(state: AppState) {
        self.state = state
        super.init(nibName: nil, bundle: nil)
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    override func loadView() {
        stack.orientation = .vertical
        stack.alignment = .leading
        stack.spacing = 12
        stack.edgeInsets = NSEdgeInsets(top: 20, left: 20, bottom: 20, right: 20)
        stack.translatesAutoresizingMaskIntoConstraints = false

        let container = NSView()
        container.addSubview(stack)
        NSLayoutConstraint.activate([
            stack.topAnchor.constraint(equalTo: container.topAnchor),
            stack.leadingAnchor.constraint(equalTo: container.leadingAnchor),
            stack.trailingAnchor.constraint(lessThanOrEqualTo: container.trailingAnchor),
            stack.bottomAnchor.constraint(lessThanOrEqualTo: container.bottomAnchor),
            container.widthAnchor.constraint(greaterThanOrEqualToConstant: 460),
        ])
        view = container
    }

    // MARK: Value access

    private func boolVal(_ key: String, _ def: Bool) -> Bool { state.settingsRaw[key] as? Bool ?? def }
    private func intVal(_ key: String, _ def: Int) -> Int {
        (state.settingsRaw[key] as? NSNumber)?.intValue ?? def
    }
    private func strVal(_ key: String, _ def: String) -> String {
        state.settingsRaw[key] as? String ?? def
    }

    @objc private func controlChanged(_ sender: NSControl) {
        handlers[ObjectIdentifier(sender)]?(sender)
    }

    // MARK: Control builders

    func checkbox(_ title: String, key: String, default def: Bool) {
        let button = NSButton(checkboxWithTitle: title, target: self,
                              action: #selector(controlChanged(_:)))
        button.state = boolVal(key, def) ? .on : .off
        handlers[ObjectIdentifier(button)] = { [weak self] s in
            let on = (s as? NSButton)?.state == .on
            self?.state.updateSettings { $0[key] = on }
        }
        stack.addArrangedSubview(button)
    }

    func popup<Value: Equatable>(_ title: String, options: [(String, Value)], key: String,
                                 default def: Value) {
        let popup = NSPopUpButton()
        popup.addItems(withTitles: options.map { $0.0 })
        popup.target = self
        popup.action = #selector(controlChanged(_:))
        popup.setAccessibilityLabel(title)
        let current = currentValue(key: key, default: def)
        if let index = options.firstIndex(where: { $0.1 == current }) {
            popup.selectItem(at: index)
        }
        handlers[ObjectIdentifier(popup)] = { [weak self] s in
            guard let p = s as? NSPopUpButton, options.indices.contains(p.indexOfSelectedItem)
            else { return }
            let value = options[p.indexOfSelectedItem].1
            self?.state.updateSettings { $0[key] = value }
        }
        stack.addArrangedSubview(labeledRow(title, popup))
    }

    func intRow(_ title: String, key: String, default def: Int, min: Int, max: Int) {
        let value = intVal(key, def)
        let field = NSTextField(string: String(value))
        field.alignment = .right
        field.setAccessibilityLabel(title)
        field.widthAnchor.constraint(equalToConstant: 80).isActive = true
        let stepper = NSStepper()
        stepper.minValue = Double(min)
        stepper.maxValue = Double(max)
        stepper.increment = 1
        stepper.integerValue = value
        stepper.valueWraps = false
        stepper.target = self
        stepper.action = #selector(controlChanged(_:))
        field.target = self
        field.action = #selector(controlChanged(_:))

        let apply: (Int) -> Void = { [weak self] v in
            let clamped = Swift.max(min, Swift.min(max, v))
            field.integerValue = clamped
            stepper.integerValue = clamped
            self?.state.updateSettings { $0[key] = clamped }
        }
        handlers[ObjectIdentifier(stepper)] = { s in apply((s as? NSStepper)?.integerValue ?? def) }
        handlers[ObjectIdentifier(field)] = { s in apply((s as? NSTextField)?.integerValue ?? def) }

        let row = NSStackView(views: [field, stepper])
        row.spacing = 4
        stack.addArrangedSubview(labeledRow(title, row))
    }

    func slider(_ title: String, key: String, default def: Int, min: Int, max: Int) {
        let slider = NSSlider(value: Double(intVal(key, def)), minValue: Double(min),
                              maxValue: Double(max), target: self,
                              action: #selector(controlChanged(_:)))
        slider.setAccessibilityLabel(title)
        slider.widthAnchor.constraint(equalToConstant: 200).isActive = true
        handlers[ObjectIdentifier(slider)] = { [weak self] s in
            let v = Int((s as? NSSlider)?.doubleValue ?? Double(def))
            self?.state.updateSettings { $0[key] = v }
        }
        stack.addArrangedSubview(labeledRow(title, slider))
    }

    /// Opens the reorderable "Speech Details" sheet for a category on the
    /// settings window. category ∈ status / user / notification.
    func openSpeechDetail(_ title: String, category: String) {
        guard state.speechCatalog != nil, let window = view.window else { NSSound.beep(); return }
        let controller = SpeechDetailsWindowController(state: state, category: category, title: title)
        speechDetailController = controller
        controller.beginSheet(for: window) { [weak self] in self?.speechDetailController = nil }
    }

    func button(_ title: String, action: @escaping () -> Void) {
        let button = NSButton(title: title, target: self, action: #selector(controlChanged(_:)))
        button.bezelStyle = .rounded
        handlers[ObjectIdentifier(button)] = { _ in action() }
        stack.addArrangedSubview(button)
    }

    // MARK: Helpers

    private func currentValue<Value>(key: String, default def: Value) -> Value {
        if Value.self == String.self { return strVal(key, def as! String) as! Value }
        if Value.self == Int.self { return intVal(key, def as! Int) as! Value }
        return def
    }

    private func labeledRow(_ title: String, _ control: NSView) -> NSView {
        let label = NSTextField(labelWithString: title)
        label.setContentHuggingPriority(.defaultLow, for: .horizontal)
        let row = NSStackView(views: [label, control])
        row.orientation = .horizontal
        row.spacing = 10
        row.alignment = .firstBaseline
        return row
    }
}
