//
//  MovementUnitsWindowController.swift
//
//  Choose which movement units are active and their order — the set cycled
//  with Option+Left/Right here (and the VoiceOver rotor ring on iPhone).
//  Space toggles a unit, ⌘↑/⌘↓ reorder; every change saves immediately.
//

import AppKit

@MainActor
final class MovementUnitsWindowController: NSWindowController, NSTableViewDataSource,
    NSTableViewDelegate {
    private let state: AppState
    private let tableView = NavigableTableView()
    private let cellIdentifier = NSUserInterfaceItemIdentifier("MovementCell")

    private struct Unit {
        let key: String
        let label: String
        var enabled: Bool
    }
    private var units: [Unit]

    init(state: AppState) {
        self.state = state
        // Merge saved order/enabled with the catalog's labels; append any
        // catalog unit the saved list is missing (enabled) — the same shape
        // the core's normalization produces.
        let labels = Dictionary(state.movementCatalog.map { ($0.key, $0.label) },
                                uniquingKeysWith: { a, _ in a })
        var built: [Unit] = []
        var seen = Set<String>()
        for item in state.movementItems() {
            guard let key = item["unit"] as? String, let label = labels[key],
                  seen.insert(key).inserted else { continue }
            built.append(Unit(key: key, label: label,
                              enabled: item["enabled"] as? Bool ?? true))
        }
        for field in state.movementCatalog where seen.insert(field.key).inserted {
            built.append(Unit(key: field.key, label: field.label, enabled: true))
        }
        units = built

        let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 340, height: 380),
                              styleMask: [.titled], backing: .buffered, defer: false)
        super.init(window: window)
        window.title = "Movement Units"
        buildUI()
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    func beginSheet(for parent: NSWindow, completion: @escaping () -> Void) {
        parent.beginSheet(window!) { _ in completion() }
        if !units.isEmpty {
            tableView.selectRowIndexes(IndexSet(integer: 0), byExtendingSelection: false)
        }
        window?.makeFirstResponder(tableView)
    }

    private func dismiss() {
        guard let window, let parent = window.sheetParent else { return }
        parent.endSheet(window)
    }

    /// Escape closes the sheet; edits are already saved.
    override func cancelOperation(_ sender: Any?) { dismiss() }

    private func buildUI() {
        guard let content = window?.contentView else { return }
        let hint = NSTextField(labelWithString: "Space toggles · ⌘↑/⌘↓ reorder")
        hint.textColor = .secondaryLabelColor
        hint.font = .systemFont(ofSize: 11)

        let column = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("unit"))
        column.resizingMask = .autoresizingMask
        tableView.addTableColumn(column)
        tableView.headerView = nil
        tableView.dataSource = self
        tableView.delegate = self
        tableView.allowsEmptySelection = false
        tableView.setAccessibilityLabel("Movement units")
        tableView.onSpace = { [weak self] in self?.toggleSelected() }
        tableView.onCommandUp = { [weak self] in self?.move(-1) }
        tableView.onCommandDown = { [weak self] in self?.move(1) }

        let scroll = NSScrollView()
        scroll.documentView = tableView
        scroll.hasVerticalScroller = true
        scroll.borderType = .bezelBorder
        scroll.translatesAutoresizingMaskIntoConstraints = false

        let close = NSButton(title: "Close", target: self, action: #selector(closeSheet))
        close.bezelStyle = .rounded
        close.keyEquivalent = "\r"
        let up = NSButton(title: "Move Up", target: self, action: #selector(moveUnitUp))
        up.bezelStyle = .rounded
        let down = NSButton(title: "Move Down", target: self, action: #selector(moveUnitDown))
        down.bezelStyle = .rounded
        let buttons = NSStackView(views: [up, down, NSView(), close])
        buttons.orientation = .horizontal
        buttons.spacing = 8

        let stack = NSStackView(views: [hint, scroll, buttons])
        stack.orientation = .vertical
        stack.spacing = 10
        stack.edgeInsets = NSEdgeInsets(top: 14, left: 14, bottom: 14, right: 14)
        stack.translatesAutoresizingMaskIntoConstraints = false
        content.addSubview(stack)
        NSLayoutConstraint.activate([
            stack.topAnchor.constraint(equalTo: content.topAnchor),
            stack.leadingAnchor.constraint(equalTo: content.leadingAnchor),
            stack.trailingAnchor.constraint(equalTo: content.trailingAnchor),
            stack.bottomAnchor.constraint(equalTo: content.bottomAnchor),
            scroll.leadingAnchor.constraint(equalTo: stack.leadingAnchor, constant: 14),
            scroll.trailingAnchor.constraint(equalTo: stack.trailingAnchor, constant: -14),
        ])
    }

    private func save() {
        state.setMovementItems(units.map { ["unit": $0.key, "enabled": $0.enabled] })
    }

    private func select(_ index: Int) {
        guard units.indices.contains(index) else { return }
        tableView.selectRowIndexes(IndexSet(integer: index), byExtendingSelection: false)
        tableView.scrollRowToVisible(index)
    }

    private func toggleSelected() {
        let row = tableView.selectedRow
        guard units.indices.contains(row) else { return }
        units[row].enabled.toggle()
        save()
        tableView.reloadData(forRowIndexes: IndexSet(integer: row),
                             columnIndexes: IndexSet(integer: 0))
        select(row)
        Speech.announce("\(units[row].label), \(units[row].enabled ? "checked" : "unchecked")")
    }

    private func move(_ delta: Int) {
        let row = tableView.selectedRow
        let target = row + delta
        guard units.indices.contains(row), units.indices.contains(target) else { return }
        units.swapAt(row, target)
        save()
        tableView.reloadData()
        select(target)
        Speech.announce("\(units[target].label), position \(target + 1) of \(units.count)")
    }

    @objc private func moveUnitUp() { move(-1) }
    @objc private func moveUnitDown() { move(1) }
    @objc private func closeSheet() { dismiss() }

    // MARK: Table

    func numberOfRows(in tableView: NSTableView) -> Int { units.count }

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
            cell.addSubview(textField)
            cell.textField = textField
            NSLayoutConstraint.activate([
                textField.leadingAnchor.constraint(equalTo: cell.leadingAnchor, constant: 6),
                textField.trailingAnchor.constraint(equalTo: cell.trailingAnchor, constant: -6),
                textField.centerYAnchor.constraint(equalTo: cell.centerYAnchor),
            ])
        }
        guard units.indices.contains(row) else { return cell }
        let unit = units[row]
        cell.textField?.stringValue = (unit.enabled ? "✓ " : "   ") + unit.label
        cell.setAccessibilityLabel("\(unit.label), \(unit.enabled ? "checked" : "unchecked")")
        return cell
    }
}
