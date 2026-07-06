//
//  HashtagsAndListsWindows.swift
//
//  Mastodon followed-hashtags and lists management. Both managers refresh in
//  place when the core re-emits their event after a mutation (unfollow, create,
//  delete, …); MainWindowController routes those events to the open sheet.
//

import AppKit

/// Simple text prompt (NSAlert + a text field) for "new list title" etc.
@MainActor
enum TextPrompt {
    static func run(_ message: String, defaultValue: String = "", in window: NSWindow?,
                    _ done: @escaping (String) -> Void) {
        let alert = NSAlert()
        alert.messageText = message
        alert.addButton(withTitle: "OK")
        alert.addButton(withTitle: "Cancel")
        let field = NSTextField(frame: NSRect(x: 0, y: 0, width: 260, height: 24))
        field.stringValue = defaultValue
        field.setAccessibilityLabel(message)
        alert.accessoryView = field
        let handle: (NSApplication.ModalResponse) -> Void = { resp in
            let text = field.stringValue.trimmingCharacters(in: .whitespaces)
            if resp == .alertFirstButtonReturn, !text.isEmpty { done(text) }
        }
        if let window {
            alert.beginSheetModal(for: window) { r in handle(r) }
        } else {
            handle(alert.runModal())
        }
        field.window?.makeFirstResponder(field)
    }
}

/// Followed hashtags: open one as a timeline, unfollow, or follow a new one.
@MainActor
final class HashtagsWindowController: NSWindowController, NSTableViewDataSource,
    NSTableViewDelegate {
    private let state: AppState
    private var tags: [FollowedTag]
    private let tableView = NSTableView()
    private let addField = NSTextField()
    private let cellIdentifier = NSUserInterfaceItemIdentifier("TagCell")

    init(state: AppState, hashtags: FollowedHashtags) {
        self.state = state
        self.tags = hashtags.tags
        let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 380, height: 360),
                              styleMask: [.titled], backing: .buffered, defer: false)
        super.init(window: window)
        window.title = "Followed Hashtags"
        buildUI()
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    func beginSheet(for parent: NSWindow, completion: @escaping () -> Void) {
        parent.beginSheet(window!) { _ in completion() }
        window?.makeFirstResponder(tableView)
    }

    func update(_ hashtags: FollowedHashtags) {
        tags = hashtags.tags
        tableView.reloadData()
    }

    private func dismiss() {
        guard let window, let parent = window.sheetParent else { return }
        parent.endSheet(window)
    }

    private func buildUI() {
        guard let content = window?.contentView else { return }
        let column = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("tag"))
        column.resizingMask = .autoresizingMask
        tableView.addTableColumn(column)
        tableView.headerView = nil
        tableView.dataSource = self
        tableView.delegate = self
        tableView.doubleAction = #selector(openSelected)
        tableView.target = self
        tableView.setAccessibilityLabel("Followed hashtags")

        let scroll = NSScrollView()
        scroll.documentView = tableView
        scroll.hasVerticalScroller = true
        scroll.borderType = .bezelBorder
        scroll.translatesAutoresizingMaskIntoConstraints = false
        scroll.heightAnchor.constraint(greaterThanOrEqualToConstant: 180).isActive = true

        let open = NSButton(title: "Open", target: self, action: #selector(openSelected))
        open.bezelStyle = .rounded
        let unfollow = NSButton(title: "Unfollow", target: self, action: #selector(unfollowSelected))
        unfollow.bezelStyle = .rounded
        let rowButtons = NSStackView(views: [open, unfollow])
        rowButtons.spacing = 8

        addField.placeholderString = "Follow a hashtag…"
        addField.setAccessibilityLabel("Follow a hashtag")
        addField.target = self
        addField.action = #selector(followNew)
        let follow = NSButton(title: "Follow", target: self, action: #selector(followNew))
        follow.bezelStyle = .rounded
        let addRow = NSStackView(views: [addField, follow])
        addRow.spacing = 8
        addField.widthAnchor.constraint(greaterThanOrEqualToConstant: 220).isActive = true

        let close = NSButton(title: "Close", target: self, action: #selector(closeSheet))
        close.bezelStyle = .rounded
        close.keyEquivalent = "\u{1b}"
        let bottom = NSStackView(views: [NSView(), close])

        let stack = NSStackView(views: [scroll, rowButtons, addRow, bottom])
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

    private var selected: FollowedTag? {
        tags.indices.contains(tableView.selectedRow) ? tags[tableView.selectedRow] : nil
    }

    @objc private func openSelected() {
        guard let tag = selected else { return }
        state.spawnTimeline(kind: "hashtag", value: tag.name)
        dismiss()
    }
    @objc private func unfollowSelected() {
        guard let tag = selected else { return }
        state.unfollowHashtag(name: tag.name) // core re-emits followed_hashtags -> update()
    }
    @objc private func followNew() {
        let name = addField.stringValue.trimmingCharacters(in: .whitespaces)
        guard !name.isEmpty else { return }
        state.followHashtag(name: name)
        addField.stringValue = ""
        state.listFollowedHashtags() // refresh (follow doesn't auto-emit)
    }
    @objc private func closeSheet() { dismiss() }

    func numberOfRows(in tableView: NSTableView) -> Int { tags.count }
    func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?,
                   row: Int) -> NSView? {
        let cell = reuseCell(tableView, cellIdentifier)
        if tags.indices.contains(row) {
            let text = "#\(tags[row].name)"
            cell.textField?.stringValue = text
            cell.setAccessibilityLabel(text)
        }
        return cell
    }
}

/// Follow a hashtag mentioned in a post (or type one).
@MainActor
final class FollowHashtagPromptWindowController: NSWindowController {
    private let state: AppState
    private let suggestions: [String]
    private let field = NSTextField()
    private let popup = NSPopUpButton()

    init(state: AppState, tags: [String]) {
        self.state = state
        self.suggestions = tags
        let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 360, height: 150),
                              styleMask: [.titled], backing: .buffered, defer: false)
        super.init(window: window)
        window.title = "Follow Hashtag"
        buildUI()
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    func beginSheet(for parent: NSWindow, completion: @escaping () -> Void) {
        parent.beginSheet(window!) { _ in completion() }
        window?.makeFirstResponder(suggestions.isEmpty ? field : popup)
    }

    private func dismiss() {
        guard let window, let parent = window.sheetParent else { return }
        parent.endSheet(window)
    }

    private func buildUI() {
        guard let content = window?.contentView else { return }
        var rows: [NSView] = []
        if !suggestions.isEmpty {
            popup.addItems(withTitles: suggestions.map { "#\($0)" })
            popup.setAccessibilityLabel("Hashtags in this post")
            popup.target = self
            popup.action = #selector(pickSuggestion)
            rows.append(labeled("In this post:", popup))
            field.stringValue = suggestions[0]
        }
        field.placeholderString = "hashtag"
        field.setAccessibilityLabel("Hashtag")
        field.target = self
        field.action = #selector(follow)
        rows.append(labeled("Hashtag:", field))
        field.widthAnchor.constraint(greaterThanOrEqualToConstant: 200).isActive = true

        let followBtn = NSButton(title: "Follow", target: self, action: #selector(follow))
        followBtn.bezelStyle = .rounded
        followBtn.keyEquivalent = "\r"
        let cancel = NSButton(title: "Cancel", target: self, action: #selector(cancel))
        cancel.bezelStyle = .rounded
        cancel.keyEquivalent = "\u{1b}"
        rows.append(NSStackView(views: [NSView(), cancel, followBtn]))

        let stack = NSStackView(views: rows)
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
        ])
    }

    private func labeled(_ title: String, _ control: NSView) -> NSView {
        let row = NSStackView(views: [NSTextField(labelWithString: title), control])
        row.spacing = 8
        return row
    }

    @objc private func pickSuggestion() {
        let i = popup.indexOfSelectedItem
        if suggestions.indices.contains(i) { field.stringValue = suggestions[i] }
    }
    @objc private func follow() {
        let name = field.stringValue.trimmingCharacters(in: .whitespaces)
        guard !name.isEmpty else { NSSound.beep(); return }
        state.followHashtag(name: name)
        dismiss()
    }
    @objc private func cancel() { dismiss() }
}

/// Mastodon lists: open, create, rename, delete.
@MainActor
final class ListsWindowController: NSWindowController, NSTableViewDataSource, NSTableViewDelegate {
    private let state: AppState
    private var lists: [ListInfo]
    private let tableView = NSTableView()
    private let cellIdentifier = NSUserInterfaceItemIdentifier("ListCell")

    init(state: AppState, lists: Lists) {
        self.state = state
        self.lists = lists.lists
        let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 380, height: 340),
                              styleMask: [.titled], backing: .buffered, defer: false)
        super.init(window: window)
        window.title = "Manage Lists"
        buildUI()
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    func beginSheet(for parent: NSWindow, completion: @escaping () -> Void) {
        parent.beginSheet(window!) { _ in completion() }
        window?.makeFirstResponder(tableView)
    }

    func update(_ l: Lists) {
        lists = l.lists
        tableView.reloadData()
    }

    private func dismiss() {
        guard let window, let parent = window.sheetParent else { return }
        parent.endSheet(window)
    }

    private func buildUI() {
        guard let content = window?.contentView else { return }
        let column = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("list"))
        column.resizingMask = .autoresizingMask
        tableView.addTableColumn(column)
        tableView.headerView = nil
        tableView.dataSource = self
        tableView.delegate = self
        tableView.doubleAction = #selector(openSelected)
        tableView.target = self
        tableView.setAccessibilityLabel("Lists")

        let scroll = NSScrollView()
        scroll.documentView = tableView
        scroll.hasVerticalScroller = true
        scroll.borderType = .bezelBorder
        scroll.translatesAutoresizingMaskIntoConstraints = false
        scroll.heightAnchor.constraint(greaterThanOrEqualToConstant: 180).isActive = true

        func button(_ t: String, _ a: Selector) -> NSButton {
            let b = NSButton(title: t, target: self, action: a); b.bezelStyle = .rounded; return b
        }
        let actions = NSStackView(views: [
            button("Open", #selector(openSelected)),
            button("New…", #selector(createNew)),
            button("Rename…", #selector(renameSelected)),
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

    private var selected: ListInfo? {
        lists.indices.contains(tableView.selectedRow) ? lists[tableView.selectedRow] : nil
    }

    @objc private func openSelected() {
        guard let list = selected else { return }
        state.spawnTimeline(kind: "list", param: list.id)
        dismiss()
    }
    @objc private func createNew() {
        TextPrompt.run("New list name:", in: window) { [weak self] title in
            self?.state.createList(title: title) // core re-emits lists -> update()
        }
    }
    @objc private func renameSelected() {
        guard let list = selected else { return }
        TextPrompt.run("Rename list:", defaultValue: list.title, in: window) { [weak self] title in
            self?.state.renameList(id: list.id, title: title)
        }
    }
    @objc private func deleteSelected() {
        guard let list = selected else { return }
        let alert = NSAlert()
        alert.messageText = "Delete the list “\(list.title)”?"
        alert.addButton(withTitle: "Delete")
        alert.addButton(withTitle: "Cancel")
        alert.beginSheetModal(for: window!) { [weak self] r in
            if r == .alertFirstButtonReturn { self?.state.deleteList(id: list.id) }
        }
    }
    @objc private func closeSheet() { dismiss() }

    func numberOfRows(in tableView: NSTableView) -> Int { lists.count }
    func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?,
                   row: Int) -> NSView? {
        let cell = reuseCell(tableView, cellIdentifier)
        if lists.indices.contains(row) {
            cell.textField?.stringValue = lists[row].title
            cell.setAccessibilityLabel(lists[row].title)
        }
        return cell
    }
}

/// Toggle a user's membership in each Mastodon list (checkbox per list).
@MainActor
final class AddToListWindowController: NSWindowController {
    private let state: AppState
    private let userLists: UserLists
    private var checks: [(String, NSButton)] = [] // list id, checkbox

    init(state: AppState, userLists: UserLists) {
        self.state = state
        self.userLists = userLists
        let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 340, height: 280),
                              styleMask: [.titled], backing: .buffered, defer: false)
        super.init(window: window)
        window.title = "Add @\(userLists.acct) to Lists"
        buildUI()
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

    private func buildUI() {
        guard let content = window?.contentView else { return }
        var views: [NSView] = []
        if userLists.lists.isEmpty {
            views.append(NSTextField(labelWithString: "You have no lists yet."))
        }
        for list in userLists.lists {
            let box = NSButton(checkboxWithTitle: list.title, target: self, action: #selector(toggle(_:)))
            box.state = list.member ? .on : .off
            box.tag = checks.count
            checks.append((list.id, box))
            views.append(box)
        }
        let close = NSButton(title: "Done", target: self, action: #selector(closeSheet))
        close.bezelStyle = .rounded
        close.keyEquivalent = "\r"
        views.append(NSStackView(views: [NSView(), close]))

        let scrollStack = NSStackView(views: views)
        scrollStack.orientation = .vertical
        scrollStack.alignment = .leading
        scrollStack.spacing = 6
        scrollStack.edgeInsets = NSEdgeInsets(top: 16, left: 16, bottom: 16, right: 16)
        scrollStack.translatesAutoresizingMaskIntoConstraints = false
        content.addSubview(scrollStack)
        NSLayoutConstraint.activate([
            scrollStack.topAnchor.constraint(equalTo: content.topAnchor),
            scrollStack.leadingAnchor.constraint(equalTo: content.leadingAnchor),
            scrollStack.trailingAnchor.constraint(equalTo: content.trailingAnchor),
            scrollStack.bottomAnchor.constraint(lessThanOrEqualTo: content.bottomAnchor),
        ])
    }

    @objc private func toggle(_ sender: NSButton) {
        guard checks.indices.contains(sender.tag) else { return }
        state.setUserList(listId: checks[sender.tag].0, accountId: userLists.accountId,
                          add: sender.state == .on)
    }
    @objc private func closeSheet() { dismiss() }
}

/// Shared single-label table cell factory.
@MainActor
func reuseCell(_ tableView: NSTableView, _ id: NSUserInterfaceItemIdentifier) -> NSTableCellView {
    if let reused = tableView.makeView(withIdentifier: id, owner: nil) as? NSTableCellView {
        return reused
    }
    let cell = NSTableCellView()
    cell.identifier = id
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
    return cell
}
