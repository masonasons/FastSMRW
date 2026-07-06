//
//  MentionPickerWindowController.swift
//
//  @-mention autocomplete while composing (the Windows Alt+A feature). Type to
//  search; the core returns matching users via autocomplete_users → the
//  user_suggestions event; choosing one inserts their handle.
//

import AppKit

@MainActor
final class MentionPickerWindowController: NSWindowController, NSTableViewDataSource,
    NSTableViewDelegate {
    private let state: AppState
    private let onChoose: (String) -> Void
    private var suggestions: [UserSuggestion] = []
    private let searchField = NSTextField()
    private let tableView = NSTableView()
    private let cellIdentifier = NSUserInterfaceItemIdentifier("MentionCell")

    init(state: AppState, initialQuery: String, onChoose: @escaping (String) -> Void) {
        self.state = state
        self.onChoose = onChoose
        let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 360, height: 320),
                              styleMask: [.titled], backing: .buffered, defer: false)
        super.init(window: window)
        window.title = "Mention Someone"
        buildUI()
        searchField.stringValue = initialQuery
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    func beginSheet(for parent: NSWindow, completion: @escaping () -> Void) {
        state.onUserSuggestions = { [weak self] s in
            self?.suggestions = s.users
            self?.tableView.reloadData()
            if !s.users.isEmpty {
                self?.tableView.selectRowIndexes(IndexSet(integer: 0), byExtendingSelection: false)
            }
        }
        parent.beginSheet(window!) { _ in
            self.state.onUserSuggestions = nil
            completion()
        }
        window?.makeFirstResponder(searchField)
        query() // seed with the initial word
    }

    private func dismiss() {
        guard let window, let parent = window.sheetParent else { return }
        parent.endSheet(window)
    }

    private func buildUI() {
        guard let content = window?.contentView else { return }
        searchField.placeholderString = "Search users…"
        searchField.setAccessibilityLabel("Search users")
        searchField.target = self
        searchField.action = #selector(chooseSelected) // Return chooses the selected row
        // Live-query as the user types.
        NotificationCenter.default.addObserver(self, selector: #selector(queryChanged),
                                               name: NSControl.textDidChangeNotification,
                                               object: searchField)

        let column = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("user"))
        column.resizingMask = .autoresizingMask
        tableView.addTableColumn(column)
        tableView.headerView = nil
        tableView.dataSource = self
        tableView.delegate = self
        tableView.doubleAction = #selector(chooseSelected)
        tableView.target = self
        tableView.setAccessibilityLabel("Matching users")

        let scroll = NSScrollView()
        scroll.documentView = tableView
        scroll.hasVerticalScroller = true
        scroll.borderType = .bezelBorder
        scroll.translatesAutoresizingMaskIntoConstraints = false

        let insert = NSButton(title: "Insert", target: self, action: #selector(chooseSelected))
        insert.bezelStyle = .rounded
        insert.keyEquivalent = "\r"
        let cancel = NSButton(title: "Cancel", target: self, action: #selector(cancel))
        cancel.bezelStyle = .rounded
        cancel.keyEquivalent = "\u{1b}"
        let buttons = NSStackView(views: [NSView(), cancel, insert])
        buttons.spacing = 8

        let stack = NSStackView(views: [searchField, scroll, buttons])
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
            searchField.leadingAnchor.constraint(equalTo: stack.leadingAnchor, constant: 14),
            searchField.trailingAnchor.constraint(equalTo: stack.trailingAnchor, constant: -14),
            scroll.leadingAnchor.constraint(equalTo: stack.leadingAnchor, constant: 14),
            scroll.trailingAnchor.constraint(equalTo: stack.trailingAnchor, constant: -14),
        ])
    }

    @objc private func queryChanged() { query() }
    private func query() {
        let q = searchField.stringValue.trimmingCharacters(in: .whitespaces)
        guard !q.isEmpty else { suggestions = []; tableView.reloadData(); return }
        state.autocompleteUsers(query: q)
    }

    @objc private func chooseSelected() {
        guard suggestions.indices.contains(tableView.selectedRow) else { return }
        onChoose(suggestions[tableView.selectedRow].acct)
        dismiss()
    }
    @objc private func cancel() { dismiss() }

    func numberOfRows(in tableView: NSTableView) -> Int { suggestions.count }
    func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?,
                   row: Int) -> NSView? {
        let cell = reuseCell(tableView, cellIdentifier)
        if suggestions.indices.contains(row) {
            let s = suggestions[row]
            let label = s.label.isEmpty ? "@\(s.acct)" : s.label
            cell.textField?.stringValue = label
            cell.setAccessibilityLabel(label)
        }
        return cell
    }
}
