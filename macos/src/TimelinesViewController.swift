//
//  TimelinesViewController.swift
//
//  The left pane: a table of the current account's timelines (Home, Local,
//  Mentions, …). Up/down selects a timeline and loads its posts; Tab moves
//  focus to the posts pane. Adapted from FastSMApple to the command/event core.
//

import AppKit

@MainActor
final class TimelinesViewController: NSViewController, NSTableViewDataSource, NSTableViewDelegate {
    private let state: AppState
    private let tableView = NavigableTableView()
    private let cellIdentifier = NSUserInterfaceItemIdentifier("TimelineCell")

    /// Called when the user presses Tab to move to the posts pane.
    var onMoveToPosts: (() -> Void)?

    /// Suppresses the selection-change handler while we set selection in code.
    private var isUpdatingSelectionProgrammatically = false

    private var timelines: [Timeline] { state.timelines }

    init(state: AppState) {
        self.state = state
        super.init(nibName: nil, bundle: nil)
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    override func loadView() {
        let column = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("timeline"))
        column.title = "Timelines"
        column.resizingMask = .autoresizingMask
        tableView.addTableColumn(column)
        tableView.headerView = nil
        tableView.dataSource = self
        tableView.delegate = self
        tableView.allowsEmptySelection = false
        tableView.allowsMultipleSelection = false
        tableView.style = .sourceList
        tableView.setAccessibilityLabel("Timelines")
        tableView.onTab = { [weak self] in self?.onMoveToPosts?() }
        tableView.onBoundary = { [weak self] in self?.state.playEarcon("boundary") }
        tableView.onDelete = { [weak self] in self?.closeSelectedTimeline() }
        // Shift+Up/Down reorder the timelines from the sidebar too (matches the
        // posts pane and Windows). The core announces the new position.
        tableView.onShiftUp = { [weak self] in self?.reorderSelected(dir: "up") }
        tableView.onShiftDown = { [weak self] in self?.reorderSelected(dir: "down") }
        tableView.menuProvider = { [weak self] row in self?.contextMenu(row: row) }

        let scrollView = NSScrollView()
        scrollView.documentView = tableView
        scrollView.hasVerticalScroller = true
        scrollView.translatesAutoresizingMaskIntoConstraints = false

        let container = NSView()
        container.addSubview(scrollView)
        NSLayoutConstraint.activate([
            scrollView.topAnchor.constraint(equalTo: container.topAnchor),
            scrollView.leadingAnchor.constraint(equalTo: container.leadingAnchor),
            scrollView.trailingAnchor.constraint(equalTo: container.trailingAnchor),
            scrollView.bottomAnchor.constraint(equalTo: container.bottomAnchor),
        ])
        view = container
    }

    func reload() {
        // Guard the WHOLE refresh: reloadData() on a non-empty-selection table
        // fires a selection change with the old row while the core has already
        // switched, which would send select_timeline{index:old} and snap the
        // core back (left/right appeared to stay stuck on Home). Only genuine
        // user selection in the sidebar should switch timelines.
        isUpdatingSelectionProgrammatically = true
        tableView.reloadData()
        let row = state.currentIndex
        if timelines.indices.contains(row) {
            tableView.selectRowIndexes(IndexSet(integer: row), byExtendingSelection: false)
            tableView.scrollRowToVisible(row)
        }
        isUpdatingSelectionProgrammatically = false
    }

    func focusTable() { view.window?.makeFirstResponder(tableView) }

    // MARK: Right-click menu

    private func contextMenu(row: Int) -> NSMenu? {
        guard timelines.indices.contains(row) else { return nil }
        let menu = NSMenu()
        func item(_ title: String, _ action: Selector) {
            let mi = menu.addItem(withTitle: title, action: action, keyEquivalent: "")
            mi.target = self
            mi.tag = row
        }
        item(timelines[row].pinned ? "Unpin" : "Pin", #selector(pinRow(_:)))
        item(timelines[row].muted ? "Unmute Sounds" : "Mute Sounds", #selector(muteRow(_:)))
        item("Move Up", #selector(moveUpRow(_:)))
        item("Move Down", #selector(moveDownRow(_:)))
        menu.addItem(.separator())
        item("Clear Items", #selector(clearRow(_:)))
        if timelines[row].dismissable {
            item("Close", #selector(closeRow(_:)))
        }
        return menu
    }

    @objc private func pinRow(_ sender: NSMenuItem) {
        state.selectTimeline(index: sender.tag)
        state.togglePin()
    }
    @objc private func muteRow(_ sender: NSMenuItem) {
        state.selectTimeline(index: sender.tag)
        state.toggleMute()
    }
    /// Shift+Up/Down from the sidebar: reorder the selected timeline. Select the
    /// row first so the core's "current" timeline is the one we're moving.
    private func reorderSelected(dir: String) {
        let row = tableView.selectedRow
        guard timelines.indices.contains(row) else { return }
        state.selectTimeline(index: row)
        state.reorderTimeline(dir: dir)
    }

    @objc private func moveUpRow(_ sender: NSMenuItem) {
        state.selectTimeline(index: sender.tag)
        state.reorderTimeline(dir: "up")
    }
    @objc private func moveDownRow(_ sender: NSMenuItem) {
        state.selectTimeline(index: sender.tag)
        state.reorderTimeline(dir: "down")
    }

    // The core's clear/close act on the current timeline, so select the target
    // row first; both commands run in order on the core loop thread.
    @objc private func clearRow(_ sender: NSMenuItem) {
        state.selectTimeline(index: sender.tag)
        state.clearTimeline()
    }
    @objc private func closeRow(_ sender: NSMenuItem) {
        state.selectTimeline(index: sender.tag)
        state.closeTimeline()
        focusTable()
    }

    /// Backspace on the sidebar closes the selected timeline (if it can be
    /// closed); otherwise an earcon signals the boundary.
    private func closeSelectedTimeline() {
        let row = tableView.selectedRow
        guard timelines.indices.contains(row), timelines[row].dismissable else {
            state.playEarcon("boundary")
            return
        }
        state.selectTimeline(index: row)
        state.closeTimeline()
        focusTable()
    }

    // MARK: Table

    func numberOfRows(in tableView: NSTableView) -> Int { timelines.count }

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
        guard timelines.indices.contains(row) else { return cell }
        let title = timelines[row].title
        cell.textField?.stringValue = title
        cell.setAccessibilityLabel(title)
        return cell
    }

    func tableViewSelectionDidChange(_ notification: Notification) {
        guard !isUpdatingSelectionProgrammatically else { return }
        let row = tableView.selectedRow
        guard row >= 0, row != state.currentIndex else { return }
        state.selectTimeline(index: row)
    }
}
