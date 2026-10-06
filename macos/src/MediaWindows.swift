//
//  MediaWindows.swift
//
//  Viewing media and opening links from a post. The core resolves a row to a
//  concrete attachment (media_open) or asks the UI to pick one (media_picker);
//  likewise for links (open_url / url_picker). Images open in a viewer, audio/
//  video in an accessible player.
//

import AppKit
import AVKit

/// Routes a media_open event to the right window (kept alive by the caller).
@MainActor
enum MediaPresenter {
    static func open(_ media: MediaOpen, from parent: NSWindow?,
                     state: AppState? = nil) -> NSWindowController? {
        guard let url = URL(string: media.url) else { return nil }
        let controller: NSWindowController = media.kind == "image"
            ? ImageViewerWindowController(url: url, title: media.title)
            : MediaPlayerWindowController(url: url, title: media.title, state: state)
        controller.showWindow(nil)
        controller.window?.makeKeyAndOrderFront(nil)
        return controller
    }
}

/// Shows a single image, downloaded off the main thread.
@MainActor
final class ImageViewerWindowController: NSWindowController {
    private let imageView = NSImageView()

    init(url: URL, title: String) {
        let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 640, height: 520),
                              styleMask: [.titled, .closable, .resizable, .miniaturizable],
                              backing: .buffered, defer: false)
        super.init(window: window)
        window.title = title.isEmpty ? "Image" : title
        window.center()
        imageView.imageScaling = .scaleProportionallyUpOrDown
        imageView.setAccessibilityLabel(title.isEmpty ? "Image" : title)
        window.contentView = imageView
        load(url)
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    private func load(_ url: URL) {
        URLSession.shared.dataTask(with: url) { [weak self] data, _, _ in
            guard let data, let image = NSImage(data: data) else { return }
            DispatchQueue.main.async { self?.imageView.image = image }
        }.resume()
    }
}

/// AVPlayerView with Up/Down bound to the volume, matching the Windows and Linux
/// players (Left/Right stay AVPlayerView's own seek).
@MainActor
final class VolumeKeyPlayerView: AVPlayerView {
    /// Called with the new level, 0-100, so it can be spoken and remembered.
    var onVolume: ((Int) -> Void)?

    override func keyDown(with event: NSEvent) {
        let up = UInt16(126), down = UInt16(125)
        guard event.keyCode == up || event.keyCode == down, let player else {
            super.keyDown(with: event)
            return
        }
        let level = Int((player.volume * 100).rounded()) + (event.keyCode == up ? 10 : -10)
        let clamped = max(0, min(100, level))
        player.volume = Float(clamped) / 100
        onVolume?(clamped)
    }
}

/// An audio/video player. AVPlayerView exposes play/pause/scrubbing to VoiceOver;
/// Space also toggles playback, and Up/Down change the volume.
@MainActor
final class MediaPlayerWindowController: NSWindowController, NSWindowDelegate {
    private let player: AVPlayer

    init(url: URL, title: String, state: AppState? = nil) {
        player = AVPlayer(url: url)
        let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 640, height: 420),
                              styleMask: [.titled, .closable, .resizable, .miniaturizable],
                              backing: .buffered, defer: false)
        super.init(window: window)
        window.title = title.isEmpty ? "Media" : title
        window.delegate = self // so windowWillClose fires on ⌘W / the close button

        // Start at the level and on the device the settings remember. An unknown
        // device name (unplugged since) leaves playback on the system's output.
        let volume = state?.settingsRaw["media_volume"] as? Int ?? 100
        player.volume = Float(max(0, min(100, volume))) / 100
        if let name = state?.settingsRaw["media_device"] as? String,
           let uid = AudioDevices.uid(forName: name) {
            player.audioOutputDeviceUniqueID = uid
        }

        let playerView = VolumeKeyPlayerView()
        playerView.player = player
        playerView.controlsStyle = .floating
        playerView.setAccessibilityLabel(title.isEmpty ? "Media player" : title)
        playerView.onVolume = { [weak state] level in
            state?.onAnnounce?("Volume \(level) percent")
            state?.setMediaVolume(level) // the next thing you play starts here
        }
        window.contentView = playerView
        window.center()
        player.play()
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    // Stop playback however the window is closed (⌘W, red button, or close()).
    func windowWillClose(_ notification: Notification) { player.pause() }
}

/// The core's own player (audio attachments and YouTube, through FastPlay's
/// engine), as on Windows: a window that is somewhere for the keys to go. Space
/// plays or pauses, Left/Right seek, Up/Down change the volume, P says where it
/// is, Escape (or closing the window) stops. The core speaks what happens.
@MainActor
final class CorePlayerWindowController: NSWindowController, NSWindowDelegate {
    private let state: AppState
    private var closingFromCore = false

    init(state: AppState, title: String) {
        self.state = state
        let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 360, height: 120),
                              styleMask: [.titled, .closable], backing: .buffered, defer: false)
        super.init(window: window)
        window.delegate = self
        let keys = PlayerKeyView(state: state)
        keys.setAccessibilityElement(true)
        keys.setAccessibilityRole(.group)
        keys.setAccessibilityLabel("Media player. Space plays or pauses, the arrows seek and change "
            + "the volume, P says where it is, Escape stops.")
        window.contentView = keys
        retitle(title)
        window.center()
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    func retitle(_ title: String) { window?.title = "Playing: " + (title.isEmpty ? "Media" : title) }

    func show() {
        showWindow(nil)
        window?.makeKeyAndOrderFront(nil)
        window?.makeFirstResponder(window?.contentView)
    }

    /// The core stopped it (ended, failed, stopped elsewhere): close without
    /// telling it to stop again.
    func closeFromCore() {
        closingFromCore = true
        close()
    }

    func windowWillClose(_ notification: Notification) {
        if !closingFromCore { state.mediaStop() }
    }
}

/// The player window's keys, each a command to the core.
@MainActor
final class PlayerKeyView: NSView {
    private let state: AppState

    init(state: AppState) {
        self.state = state
        super.init(frame: .zero)
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    override var acceptsFirstResponder: Bool { true }

    override func keyDown(with event: NSEvent) {
        switch event.keyCode {
        case 49: state.mediaToggle()          // Space
        case 123: state.mediaSeek(by: -5)     // Left
        case 124: state.mediaSeek(by: 5)      // Right
        case 126: state.mediaVolume(by: 10)   // Up
        case 125: state.mediaVolume(by: -10)  // Down
        case 35: state.mediaPosition()        // P
        case 53: window?.close()              // Escape
        default: super.keyDown(with: event)
        }
    }
}

/// Pick which attachment to view when a post has several.
@MainActor
final class MediaPickerWindowController: ListPickerWindowController {
    init(state: AppState, picker: MediaPicker) {
        super.init(title: "Choose Media", rows: picker.items.map(\.title)) { index in
            let item = picker.items[index]
            state.playMedia(url: item.url, kind: item.kind, title: item.title)
        }
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }
}

/// Pick which link to open when a post has several.
@MainActor
final class URLPickerWindowController: ListPickerWindowController {
    init(picker: URLPicker) {
        super.init(title: "Open Link", rows: picker.links.map { $0.title.isEmpty ? $0.url : $0.title }) { index in
            if let url = URL(string: picker.links[index].url) { NSWorkspace.shared.open(url) }
        }
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }
}

/// Choose which of a post's hashtags to open a timeline for. The core only
/// sends the picker for posts with several tags; a single tag opens directly.
final class HashtagTimelinePickerWindowController: ListPickerWindowController {
    init(state: AppState, tags: [String]) {
        super.init(title: "Open Hashtag Timeline", rows: tags.map { "#" + $0 }) { index in
            state.spawnTimeline(kind: "hashtag", value: tags[index])
        }
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }
}

/// A reusable single-column list sheet: choose one row, invoke a callback.
@MainActor
class ListPickerWindowController: NSWindowController, NSTableViewDataSource, NSTableViewDelegate {
    private let rows: [String]
    private let onChoose: (Int) -> Void
    private let tableView = NSTableView()
    private let cellIdentifier = NSUserInterfaceItemIdentifier("ListCell")

    init(title: String, rows: [String], onChoose: @escaping (Int) -> Void) {
        self.rows = rows
        self.onChoose = onChoose
        let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 380, height: 320),
                              styleMask: [.titled], backing: .buffered, defer: false)
        super.init(window: window)
        window.title = title
        buildUI()
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    func beginSheet(for parent: NSWindow, completion: @escaping () -> Void) {
        parent.beginSheet(window!) { _ in completion() }
        if !rows.isEmpty { tableView.selectRowIndexes(IndexSet(integer: 0), byExtendingSelection: false) }
        window?.makeFirstResponder(tableView)
    }

    private func dismiss() {
        guard let window, let parent = window.sheetParent else { return }
        parent.endSheet(window)
    }

    private func buildUI() {
        guard let content = window?.contentView else { return }
        let column = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("row"))
        column.resizingMask = .autoresizingMask
        tableView.addTableColumn(column)
        tableView.headerView = nil
        tableView.dataSource = self
        tableView.delegate = self
        tableView.doubleAction = #selector(choose)
        tableView.target = self
        tableView.setAccessibilityLabel("Choices")

        let scroll = NSScrollView()
        scroll.documentView = tableView
        scroll.hasVerticalScroller = true
        scroll.borderType = .bezelBorder
        scroll.translatesAutoresizingMaskIntoConstraints = false

        let ok = NSButton(title: "Open", target: self, action: #selector(choose))
        ok.bezelStyle = .rounded
        ok.keyEquivalent = "\r"
        let cancel = NSButton(title: "Cancel", target: self, action: #selector(self.cancel))
        cancel.bezelStyle = .rounded
        cancel.keyEquivalent = "\u{1b}"
        let buttons = NSStackView(views: [NSView(), cancel, ok])
        buttons.orientation = .horizontal
        buttons.spacing = 8

        let stack = NSStackView(views: [scroll, buttons])
        stack.orientation = .vertical
        stack.spacing = 12
        stack.edgeInsets = NSEdgeInsets(top: 16, left: 16, bottom: 16, right: 16)
        stack.translatesAutoresizingMaskIntoConstraints = false
        content.addSubview(stack)
        NSLayoutConstraint.activate([
            stack.topAnchor.constraint(equalTo: content.topAnchor),
            stack.leadingAnchor.constraint(equalTo: content.leadingAnchor),
            stack.trailingAnchor.constraint(equalTo: content.trailingAnchor),
            stack.bottomAnchor.constraint(equalTo: content.bottomAnchor),
            scroll.leadingAnchor.constraint(equalTo: stack.leadingAnchor, constant: 16),
            scroll.trailingAnchor.constraint(equalTo: stack.trailingAnchor, constant: -16),
        ])
    }

    @objc private func choose() {
        guard rows.indices.contains(tableView.selectedRow) else { return }
        onChoose(tableView.selectedRow)
        dismiss()
    }

    @objc private func cancel() { dismiss() }

    func numberOfRows(in tableView: NSTableView) -> Int { rows.count }

    func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?,
                   row: Int) -> NSView? {
        let cell: NSTableCellView
        if let reused = tableView.makeView(withIdentifier: cellIdentifier, owner: self)
            as? NSTableCellView {
            cell = reused
        } else {
            cell = NSTableCellView()
            cell.identifier = cellIdentifier
            let textField = NSTextField(labelWithString: "")
            textField.translatesAutoresizingMaskIntoConstraints = false
            textField.lineBreakMode = .byTruncatingTail
            cell.addSubview(textField)
            cell.textField = textField
            NSLayoutConstraint.activate([
                textField.leadingAnchor.constraint(equalTo: cell.leadingAnchor, constant: 6),
                textField.trailingAnchor.constraint(equalTo: cell.trailingAnchor, constant: -6),
                textField.centerYAnchor.constraint(equalTo: cell.centerYAnchor),
            ])
        }
        guard rows.indices.contains(row) else { return cell }
        cell.textField?.stringValue = rows[row]
        cell.setAccessibilityLabel(rows[row])
        return cell
    }
}
