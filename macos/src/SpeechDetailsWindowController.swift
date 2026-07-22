//
//  SpeechDetailsWindowController.swift
//
//  The reorderable, checkable list that chooses exactly which fields VoiceOver
//  reads for a post / user / notification, and in what order. Order + enabled
//  come from settings.speech.<category>; the labels come from the core's
//  speech_catalog. Up/Down navigate, ⌘Up/⌘Down reorder, Space toggles. Saving
//  writes the reordered list back via update_settings.
//
//  Deliberately avoids the word "checkbox": each row reads "<field>, checked/
//  unchecked", matching the Windows and FastSMApple speech-detail dialogs.
//

import AppKit

@MainActor
final class SpeechDetailsWindowController: NSWindowController, NSTableViewDataSource,
    NSTableViewDelegate {
    private let state: AppState
    private let category: String
    private let tableView = NavigableTableView()
    private let cellIdentifier = NSUserInterfaceItemIdentifier("SpeechFieldCell")
    // Wrap-text editors for the selected field.
    private let beforeField = NSTextField()
    private let afterField = NSTextField()
    private let noSepCheck = NSButton(checkboxWithTitle: "No separator after this field",
                                      target: nil, action: nil)

    // Working copy: (key, label, enabled) plus preserved wrap text.
    private struct Field {
        let key: String
        let label: String
        var enabled: Bool
        var before: String
        var after: String
        var noSeparatorAfter: Bool
    }
    private var fields: [Field]
    /// The row the wrap editors are currently bound to, so a field's action
    /// commits to the right field even if selection has already moved.
    private var wrapBoundRow = -1

    init(state: AppState, category: String, title: String) {
        self.state = state
        self.category = category

        // Merge saved order/enabled/wrap with the catalog's labels; append any
        // catalog field the saved list is missing (enabled) so nothing is lost.
        let catalog = state.speechCatalog?.fields(for: category) ?? []
        let labels = Dictionary(catalog.map { ($0.key, $0.label) }, uniquingKeysWith: { a, _ in a })
        var built: [Field] = []
        var seen = Set<String>()
        for item in state.speechItems(for: category) {
            guard let key = item["field"] as? String, let label = labels[key] else { continue }
            seen.insert(key)
            built.append(Field(key: key, label: label,
                               enabled: item["enabled"] as? Bool ?? true,
                               before: item["before"] as? String ?? "",
                               after: item["after"] as? String ?? "",
                               noSeparatorAfter: item["no_separator_after"] as? Bool ?? false))
        }
        for field in catalog where !seen.contains(field.key) {
            built.append(Field(key: field.key, label: field.label, enabled: true,
                               before: "", after: "", noSeparatorAfter: false))
        }
        fields = built

        let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 380, height: 420),
                              styleMask: [.titled], backing: .buffered, defer: false)
        super.init(window: window)
        window.title = title
        buildUI()
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    func beginSheet(for parent: NSWindow, completion: @escaping () -> Void) {
        parent.beginSheet(window!) { _ in completion() }
        if !fields.isEmpty { select(0) }
        window?.makeFirstResponder(tableView)
    }

    private func dismiss() {
        guard let window, let parent = window.sheetParent else { return }
        parent.endSheet(window)
    }

    private func buildUI() {
        guard let content = window?.contentView else { return }
        let hint = NSTextField(labelWithString: "Space toggles · ⌘↑/⌘↓ reorder")
        hint.textColor = .secondaryLabelColor
        hint.font = .systemFont(ofSize: 11)

        let column = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("field"))
        column.resizingMask = .autoresizingMask
        tableView.addTableColumn(column)
        tableView.headerView = nil
        tableView.dataSource = self
        tableView.delegate = self
        tableView.allowsEmptySelection = false
        tableView.setAccessibilityLabel("Spoken fields")
        tableView.onSpace = { [weak self] in self?.toggleSelected() }
        tableView.onCommandUp = { [weak self] in self?.move(-1) }
        tableView.onCommandDown = { [weak self] in self?.move(1) }

        let scroll = NSScrollView()
        scroll.documentView = tableView
        scroll.hasVerticalScroller = true
        scroll.borderType = .bezelBorder
        scroll.translatesAutoresizingMaskIntoConstraints = false

        // Every change saves immediately (like the other apps), so closing the
        // sheet any way — Close, Return, or Escape — keeps the edits. The old
        // save-on-OK design silently discarded everything when the sheet was
        // closed with Escape, which reads as "my changes did nothing".
        let close = NSButton(title: "Close", target: self, action: #selector(save))
        close.bezelStyle = .rounded
        close.keyEquivalent = "\r"
        let up = NSButton(title: "Move Up", target: self, action: #selector(moveFieldUp))
        up.bezelStyle = .rounded
        let down = NSButton(title: "Move Down", target: self, action: #selector(moveFieldDown))
        down.bezelStyle = .rounded
        let buttons = NSStackView(views: [up, down, NSView(), close])
        buttons.orientation = .horizontal
        buttons.spacing = 8

        // Wrap-text editor for the selected field (e.g. "(" before + ")" after).
        beforeField.placeholderString = "Spoken before"
        afterField.placeholderString = "Spoken after"
        beforeField.setAccessibilityLabel("Spoken before this field")
        afterField.setAccessibilityLabel("Spoken after this field")
        for c in [beforeField, afterField] { c.target = self; c.action = #selector(commitWrap) }
        noSepCheck.target = self
        noSepCheck.action = #selector(commitWrap)
        let wrapRow = NSStackView(views: [
            NSTextField(labelWithString: "Before:"), beforeField,
            NSTextField(labelWithString: "After:"), afterField,
        ])
        wrapRow.spacing = 6
        beforeField.widthAnchor.constraint(greaterThanOrEqualToConstant: 90).isActive = true
        afterField.widthAnchor.constraint(greaterThanOrEqualToConstant: 90).isActive = true

        let stack = NSStackView(views: [hint, scroll, wrapRow, noSepCheck, buttons])
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

    private func select(_ index: Int) {
        guard fields.indices.contains(index) else { return }
        tableView.selectRowIndexes(IndexSet(integer: index), byExtendingSelection: false)
        tableView.scrollRowToVisible(index)
    }

    // When the selected field changes, save the wrap edits to the row they were
    // bound to, then load the newly-selected field's wrap into the editors.
    func tableViewSelectionDidChange(_ notification: Notification) {
        commitWrap()
        let row = tableView.selectedRow
        guard fields.indices.contains(row) else { wrapBoundRow = -1; return }
        wrapBoundRow = row
        beforeField.stringValue = fields[row].before
        afterField.stringValue = fields[row].after
        noSepCheck.state = fields[row].noSeparatorAfter ? .on : .off
    }

    @objc private func commitWrap() {
        guard fields.indices.contains(wrapBoundRow) else { return }
        let before = beforeField.stringValue
        let after = afterField.stringValue
        let noSep = noSepCheck.state == .on
        guard before != fields[wrapBoundRow].before || after != fields[wrapBoundRow].after
            || noSep != fields[wrapBoundRow].noSeparatorAfter else { return }
        fields[wrapBoundRow].before = before
        fields[wrapBoundRow].after = after
        fields[wrapBoundRow].noSeparatorAfter = noSep
        writeThrough()
    }

    private func toggleSelected() {
        let row = tableView.selectedRow
        guard fields.indices.contains(row) else { return }
        fields[row].enabled.toggle()
        writeThrough()
        tableView.reloadData(forRowIndexes: IndexSet(integer: row),
                             columnIndexes: IndexSet(integer: 0))
        select(row)
        Speech.announce("\(fields[row].label), \(fields[row].enabled ? "checked" : "unchecked")")
    }

    private func move(_ delta: Int) {
        let row = tableView.selectedRow
        let target = row + delta
        guard fields.indices.contains(row), fields.indices.contains(target) else { return }
        fields.swapAt(row, target)
        writeThrough()
        tableView.reloadData()
        select(target)
    }

    @objc private func moveFieldUp() { move(-1) }
    @objc private func moveFieldDown() { move(1) }

    /// Escape closes the sheet; edits are already saved.
    override func cancelOperation(_ sender: Any?) {
        commitWrap()
        dismiss()
    }

    /// Push the current field list into settings.speech.<category> — called on
    /// every mutation, so the sheet can be closed any way without losing edits.
    private func writeThrough() {
        let items: [[String: Any]] = fields.map { f in
            var item: [String: Any] = ["field": f.key, "enabled": f.enabled]
            if !f.before.isEmpty { item["before"] = f.before }
            if !f.after.isEmpty { item["after"] = f.after }
            if f.noSeparatorAfter { item["no_separator_after"] = true }
            return item
        }
        state.setSpeechItems(items, for: category)
    }

    @objc private func save() {
        commitWrap() // capture edits to the currently-shown field
        dismiss()
    }

    // MARK: Table

    func numberOfRows(in tableView: NSTableView) -> Int { fields.count }

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
        guard fields.indices.contains(row) else { return cell }
        let field = fields[row]
        cell.textField?.stringValue = (field.enabled ? "✓ " : "   ") + field.label
        cell.setAccessibilityLabel("\(field.label), \(field.enabled ? "checked" : "unchecked")")
        return cell
    }
}
