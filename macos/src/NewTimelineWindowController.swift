//
//  NewTimelineWindowController.swift
//
//  ⌘T sheet: pick a timeline to open (Home, Local, Federated, Bookmarks, a
//  Mastodon list, …) or a parameterized one (hashtag, search, remote instance)
//  that prompts for a value. Sends spawn_timeline to the core.
//

import AppKit

@MainActor
final class NewTimelineWindowController: NSWindowController, NSTableViewDataSource,
    NSTableViewDelegate {
    private let state: AppState
    private let spawnables: [Spawnable]
    private let tableView = NSTableView()
    private let valueField = NSTextField()
    private let valueLabel = NSTextField(labelWithString: "")
    private let openButton = NSButton()

    init(state: AppState, spawnables: [Spawnable]) {
        self.state = state
        self.spawnables = spawnables
        let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 420, height: 380),
                              styleMask: [.titled], backing: .buffered, defer: false)
        super.init(window: window)
        window.title = "New Timeline"
        buildUI()
        updateValueRow()
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    func beginSheet(for parent: NSWindow, completion: @escaping () -> Void) {
        parent.beginSheet(window!) { _ in completion() }
        if !spawnables.isEmpty {
            tableView.selectRowIndexes(IndexSet(integer: 0), byExtendingSelection: false)
        }
        window?.makeFirstResponder(tableView)
    }

    private func dismiss() {
        guard let window, let parent = window.sheetParent else { return }
        parent.endSheet(window)
    }

    private var selected: Spawnable? {
        spawnables.indices.contains(tableView.selectedRow) ? spawnables[tableView.selectedRow] : nil
    }

    private func buildUI() {
        guard let content = window?.contentView else { return }
        let column = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("timeline"))
        column.resizingMask = .autoresizingMask
        tableView.addTableColumn(column)
        tableView.headerView = nil
        tableView.dataSource = self
        tableView.delegate = self
        tableView.allowsEmptySelection = false
        tableView.doubleAction = #selector(open)
        tableView.target = self
        tableView.setAccessibilityLabel("Timelines to open")

        let scroll = NSScrollView()
        scroll.documentView = tableView
        scroll.hasVerticalScroller = true
        scroll.borderType = .bezelBorder
        scroll.translatesAutoresizingMaskIntoConstraints = false
        scroll.heightAnchor.constraint(greaterThanOrEqualToConstant: 220).isActive = true

        valueField.setAccessibilityLabel("Value")
        valueField.target = self
        valueField.action = #selector(open)

        openButton.title = "Open"
        openButton.bezelStyle = .rounded
        openButton.keyEquivalent = "\r"
        openButton.target = self
        openButton.action = #selector(open)
        let cancel = NSButton(title: "Cancel", target: self, action: #selector(cancel))
        cancel.bezelStyle = .rounded
        cancel.keyEquivalent = "\u{1b}"

        let valueRow = NSStackView(views: [valueLabel, valueField])
        valueRow.orientation = .horizontal
        valueRow.spacing = 8
        valueField.widthAnchor.constraint(greaterThanOrEqualToConstant: 240).isActive = true

        let buttons = NSStackView(views: [NSView(), cancel, openButton])
        buttons.orientation = .horizontal
        buttons.spacing = 8

        let stack = NSStackView(views: [scroll, valueRow, buttons])
        stack.orientation = .vertical
        stack.alignment = .leading
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

    private func updateValueRow() {
        let input = selected?.input
        valueLabel.stringValue = (input ?? "") + ":"
        valueField.placeholderString = input
        let needsValue = input != nil
        valueLabel.isHidden = !needsValue
        valueField.isHidden = !needsValue
        if needsValue { valueField.stringValue = "" }
    }

    // MARK: Actions

    @objc private func open() {
        guard let entry = selected else { return }
        if let _ = entry.input {
            let value = valueField.stringValue.trimmingCharacters(in: .whitespaces)
            guard !value.isEmpty else { window?.makeFirstResponder(valueField); NSSound.beep(); return }
            state.spawnTimeline(kind: entry.kind, value: value)
        } else if let param = entry.param {
            state.spawnTimeline(kind: entry.kind, param: param)
        } else {
            state.spawnTimeline(kind: entry.kind)
        }
        dismiss()
    }

    @objc private func cancel() { dismiss() }

    // MARK: Table

    private let cellIdentifier = NSUserInterfaceItemIdentifier("SpawnCell")

    func numberOfRows(in tableView: NSTableView) -> Int { spawnables.count }

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
        guard spawnables.indices.contains(row) else { return cell }
        let title = spawnables[row].title
        cell.textField?.stringValue = title
        cell.setAccessibilityLabel(title)
        return cell
    }

    func tableViewSelectionDidChange(_ notification: Notification) { updateValueRow() }
}
