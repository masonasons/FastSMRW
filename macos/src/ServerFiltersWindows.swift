//
//  ServerFiltersWindows.swift
//
//  Mastodon server-side keyword filters: a manager (list / new / edit / delete)
//  and an editor (title, action, contexts, keywords, expiry). Saving sends
//  save_server_filter; the core re-emits server_filters so the manager refreshes.
//

import AppKit

@MainActor
final class ServerFiltersWindowController: NSWindowController, NSTableViewDataSource,
    NSTableViewDelegate {
    private let state: AppState
    private var filters: [ServerFilter]
    private let tableView = NSTableView()
    private let cellIdentifier = NSUserInterfaceItemIdentifier("FilterCell")
    private var editor: ServerFilterEditorWindowController?

    init(state: AppState, filters: ServerFilters) {
        self.state = state
        self.filters = filters.filters
        let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 400, height: 340),
                              styleMask: [.titled], backing: .buffered, defer: false)
        super.init(window: window)
        window.title = "Server Filters"
        buildUI()
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    func beginSheet(for parent: NSWindow, completion: @escaping () -> Void) {
        parent.beginSheet(window!) { _ in completion() }
        window?.makeFirstResponder(tableView)
    }

    func update(_ f: ServerFilters) {
        filters = f.filters
        tableView.reloadData()
    }

    private func dismiss() {
        guard let window, let parent = window.sheetParent else { return }
        parent.endSheet(window)
    }

    private func buildUI() {
        guard let content = window?.contentView else { return }
        let column = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("filter"))
        column.resizingMask = .autoresizingMask
        tableView.addTableColumn(column)
        tableView.headerView = nil
        tableView.dataSource = self
        tableView.delegate = self
        tableView.doubleAction = #selector(editSelected)
        tableView.target = self
        tableView.setAccessibilityLabel("Server filters")

        let scroll = NSScrollView()
        scroll.documentView = tableView
        scroll.hasVerticalScroller = true
        scroll.borderType = .bezelBorder
        scroll.translatesAutoresizingMaskIntoConstraints = false
        scroll.heightAnchor.constraint(greaterThanOrEqualToConstant: 190).isActive = true

        func button(_ t: String, _ a: Selector) -> NSButton {
            let b = NSButton(title: t, target: self, action: a); b.bezelStyle = .rounded; return b
        }
        let actions = NSStackView(views: [
            button("New…", #selector(newFilter)),
            button("Edit…", #selector(editSelected)),
            button("Delete", #selector(deleteSelected)),
        ])
        actions.spacing = 8
        let close = NSButton(title: "Close", target: self, action: #selector(closeSheet))
        close.bezelStyle = .rounded
        close.keyEquivalent = "\u{1b}"
        let bottom = NSStackView(views: [NSView(), close])

        let stack = NSStackView(views: [scroll, actions, bottom])
        stack.orientation = .vertical
        stack.alignment = .leading
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

    private var selected: ServerFilter? {
        filters.indices.contains(tableView.selectedRow) ? filters[tableView.selectedRow] : nil
    }

    private func presentEditor(_ filter: ServerFilter) {
        guard let window else { return }
        let controller = ServerFilterEditorWindowController(state: state, filter: filter)
        editor = controller
        controller.beginSheet(for: window) { [weak self] in self?.editor = nil }
    }

    @objc private func newFilter() { presentEditor(ServerFilter()) }
    @objc private func editSelected() { if let f = selected { presentEditor(f) } }
    @objc private func deleteSelected() {
        guard let filter = selected else { return }
        let alert = NSAlert()
        alert.messageText = "Delete the filter “\(filter.title)”?"
        alert.addButton(withTitle: "Delete")
        alert.addButton(withTitle: "Cancel")
        alert.beginSheetModal(for: window!) { [weak self] r in
            if r == .alertFirstButtonReturn { self?.state.deleteServerFilter(id: filter.id) }
        }
    }
    @objc private func closeSheet() { dismiss() }

    func numberOfRows(in tableView: NSTableView) -> Int { filters.count }
    func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?,
                   row: Int) -> NSView? {
        let cell = reuseCell(tableView, cellIdentifier)
        if filters.indices.contains(row) {
            let f = filters[row]
            let label = "\(f.title) — \(f.action == "hide" ? "hide" : "warn")"
            cell.textField?.stringValue = label
            cell.setAccessibilityLabel(label)
        }
        return cell
    }
}

/// Edit one server filter: title, action, contexts, keywords, expiry.
@MainActor
final class ServerFilterEditorWindowController: NSWindowController, NSTableViewDataSource,
    NSTableViewDelegate {
    private let state: AppState
    private var filter: ServerFilter
    private let titleField = NSTextField()
    private let actionPopup = NSPopUpButton()
    private let expiryPopup = NSPopUpButton()
    private var contextChecks: [(String, NSButton)] = []
    private var keywords: [ServerFilterKeyword]
    private let keywordTable = NSTableView()
    private let keywordField = NSTextField()
    private let wholeWordCheck = NSButton(checkboxWithTitle: "Whole word only", target: nil, action: nil)
    private let cellIdentifier = NSUserInterfaceItemIdentifier("KeywordCell")

    private let contextOptions: [(String, String)] = [
        ("Home", "home"), ("Notifications", "notifications"), ("Public timelines", "public"),
        ("Threads", "thread"), ("Profiles", "account"),
    ]
    private let expiryOptions: [(String, Int)] = [
        ("Never", 0), ("30 minutes", 1800), ("1 hour", 3600), ("6 hours", 21600),
        ("12 hours", 43200), ("1 day", 86400), ("1 week", 604800),
    ]

    init(state: AppState, filter: ServerFilter) {
        self.state = state
        self.filter = filter
        self.keywords = filter.keywords
        let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 420, height: 520),
                              styleMask: [.titled], backing: .buffered, defer: false)
        super.init(window: window)
        window.title = filter.id.isEmpty ? "New Filter" : "Edit Filter"
        buildUI()
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    func beginSheet(for parent: NSWindow, completion: @escaping () -> Void) {
        parent.beginSheet(window!) { _ in completion() }
        window?.makeFirstResponder(titleField)
    }

    private func dismiss() {
        guard let window, let parent = window.sheetParent else { return }
        parent.endSheet(window)
    }

    private func buildUI() {
        guard let content = window?.contentView else { return }
        titleField.stringValue = filter.title
        titleField.placeholderString = "Filter name"
        titleField.setAccessibilityLabel("Filter name")

        actionPopup.addItems(withTitles: ["Warn (show behind a warning)", "Hide completely"])
        actionPopup.selectItem(at: filter.action == "hide" ? 1 : 0)
        actionPopup.setAccessibilityLabel("Action")

        expiryPopup.addItems(withTitles: expiryOptions.map { $0.0 })
        expiryPopup.setAccessibilityLabel("Expires")

        var contextViews: [NSView] = [NSTextField(labelWithString: "Apply in:")]
        for (label, key) in contextOptions {
            let box = NSButton(checkboxWithTitle: label, target: nil, action: nil)
            box.state = filter.context.contains(key) ? .on : .off
            contextChecks.append((key, box))
            contextViews.append(box)
        }
        let contextStack = NSStackView(views: contextViews)
        contextStack.orientation = .vertical
        contextStack.alignment = .leading
        contextStack.spacing = 4

        keywordTable.addTableColumn(NSTableColumn(identifier: NSUserInterfaceItemIdentifier("kw")))
        keywordTable.headerView = nil
        keywordTable.dataSource = self
        keywordTable.delegate = self
        keywordTable.setAccessibilityLabel("Keywords")
        let kwScroll = NSScrollView()
        kwScroll.documentView = keywordTable
        kwScroll.hasVerticalScroller = true
        kwScroll.borderType = .bezelBorder
        kwScroll.translatesAutoresizingMaskIntoConstraints = false
        kwScroll.heightAnchor.constraint(greaterThanOrEqualToConstant: 100).isActive = true

        keywordField.placeholderString = "Add a keyword or phrase…"
        keywordField.setAccessibilityLabel("Keyword")
        keywordField.target = self
        keywordField.action = #selector(addKeyword)
        let addBtn = NSButton(title: "Add", target: self, action: #selector(addKeyword))
        addBtn.bezelStyle = .rounded
        let removeBtn = NSButton(title: "Remove", target: self, action: #selector(removeKeyword))
        removeBtn.bezelStyle = .rounded
        let kwControls = NSStackView(views: [keywordField, wholeWordCheck, addBtn, removeBtn])
        kwControls.spacing = 8
        keywordField.widthAnchor.constraint(greaterThanOrEqualToConstant: 160).isActive = true

        let save = NSButton(title: "Save", target: self, action: #selector(saveFilter))
        save.bezelStyle = .rounded
        save.keyEquivalent = "\r"
        let cancel = NSButton(title: "Cancel", target: self, action: #selector(cancel))
        cancel.bezelStyle = .rounded
        cancel.keyEquivalent = "\u{1b}"
        let buttons = NSStackView(views: [NSView(), cancel, save])

        func row(_ label: String, _ control: NSView) -> NSView {
            let r = NSStackView(views: [NSTextField(labelWithString: label), control])
            r.spacing = 8
            return r
        }

        let stack = NSStackView(views: [
            row("Name:", titleField),
            row("Action:", actionPopup),
            contextStack,
            NSTextField(labelWithString: "Keywords:"),
            kwScroll, kwControls,
            row("Expires:", expiryPopup),
            buttons,
        ])
        stack.orientation = .vertical
        stack.alignment = .leading
        stack.spacing = 10
        stack.edgeInsets = NSEdgeInsets(top: 16, left: 16, bottom: 16, right: 16)
        stack.translatesAutoresizingMaskIntoConstraints = false
        content.addSubview(stack)
        NSLayoutConstraint.activate([
            stack.topAnchor.constraint(equalTo: content.topAnchor),
            stack.leadingAnchor.constraint(equalTo: content.leadingAnchor),
            stack.trailingAnchor.constraint(equalTo: content.trailingAnchor),
            stack.bottomAnchor.constraint(equalTo: content.bottomAnchor),
            titleField.widthAnchor.constraint(greaterThanOrEqualToConstant: 240),
            kwScroll.leadingAnchor.constraint(equalTo: stack.leadingAnchor, constant: 16),
            kwScroll.trailingAnchor.constraint(equalTo: stack.trailingAnchor, constant: -16),
            buttons.leadingAnchor.constraint(equalTo: stack.leadingAnchor, constant: 16),
            buttons.trailingAnchor.constraint(equalTo: stack.trailingAnchor, constant: -16),
        ])
    }

    @objc private func addKeyword() {
        let text = keywordField.stringValue.trimmingCharacters(in: .whitespaces)
        guard !text.isEmpty else { return }
        keywords.append(ServerFilterKeyword(keyword: text, wholeWord: wholeWordCheck.state == .on))
        keywordField.stringValue = ""
        wholeWordCheck.state = .off
        keywordTable.reloadData()
    }
    @objc private func removeKeyword() {
        let row = keywordTable.selectedRow
        guard keywords.indices.contains(row) else { return }
        keywords.remove(at: row)
        keywordTable.reloadData()
    }
    @objc private func cancel() { dismiss() }

    @objc private func saveFilter() {
        let title = titleField.stringValue.trimmingCharacters(in: .whitespaces)
        guard !title.isEmpty, !keywords.isEmpty else { NSSound.beep(); return }
        let contexts = contextChecks.filter { $0.1.state == .on }.map { $0.0 }
        guard !contexts.isEmpty else { NSSound.beep(); return }
        let expiresIn = expiryOptions[expiryPopup.indexOfSelectedItem].1
        var dict: [String: Any] = [
            "id": filter.id,
            "title": title,
            "action": actionPopup.indexOfSelectedItem == 1 ? "hide" : "warn",
            "context": contexts,
            "expires_in": expiresIn,
            "keywords": keywords.map { kw -> [String: Any] in
                var d: [String: Any] = ["keyword": kw.keyword, "whole_word": kw.wholeWord]
                if !kw.id.isEmpty { d["id"] = kw.id }
                return d
            },
        ]
        if filter.id.isEmpty { dict["id"] = "" }
        state.saveServerFilter(dict)
        dismiss()
    }

    func numberOfRows(in tableView: NSTableView) -> Int { keywords.count }
    func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?,
                   row: Int) -> NSView? {
        let cell = reuseCell(tableView, cellIdentifier)
        if keywords.indices.contains(row) {
            let kw = keywords[row]
            let label = kw.wholeWord ? "\(kw.keyword) (whole word)" : kw.keyword
            cell.textField?.stringValue = label
            cell.setAccessibilityLabel(label)
        }
        return cell
    }
}
