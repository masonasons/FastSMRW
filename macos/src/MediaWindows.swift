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
    static func open(_ media: MediaOpen, from parent: NSWindow?) -> NSWindowController? {
        guard let url = URL(string: media.url) else { return nil }
        let controller: NSWindowController = media.kind == "image"
            ? ImageViewerWindowController(url: url, title: media.title)
            : MediaPlayerWindowController(url: url, title: media.title)
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

/// An audio/video player. AVPlayerView exposes play/pause/scrubbing to VoiceOver;
/// Space also toggles playback.
@MainActor
final class MediaPlayerWindowController: NSWindowController, NSWindowDelegate {
    private let player: AVPlayer

    init(url: URL, title: String) {
        player = AVPlayer(url: url)
        let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 640, height: 420),
                              styleMask: [.titled, .closable, .resizable, .miniaturizable],
                              backing: .buffered, defer: false)
        super.init(window: window)
        window.title = title.isEmpty ? "Media" : title
        window.delegate = self // so windowWillClose fires on ⌘W / the close button

        let playerView = AVPlayerView()
        playerView.player = player
        playerView.controlsStyle = .floating
        playerView.setAccessibilityLabel(title.isEmpty ? "Media player" : title)
        window.contentView = playerView
        window.center()
        player.play()
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    // Stop playback however the window is closed (⌘W, red button, or close()).
    func windowWillClose(_ notification: Notification) { player.pause() }
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
