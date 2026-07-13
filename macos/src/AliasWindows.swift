//
//  AliasWindows.swift
//
//  User aliases (global, cross-account custom display names): a prompt to
//  add/edit/clear the alias for one user, and a manager listing every alias.
//  Aliases live in the core; these only send commands and render aliases_list.
//

import AppKit

/// Prompt for a user's alias. An empty value clears the alias (matches FastSM).
@MainActor
enum AliasPromptSheet {
    static func run(handle: String, current: String, in window: NSWindow?,
                    _ done: @escaping (String) -> Void) {
        let alert = NSAlert()
        alert.messageText = "Alias for @\(handle)"
        alert.informativeText = "Enter a custom display name, or leave blank to remove."
        alert.addButton(withTitle: "OK")
        alert.addButton(withTitle: "Cancel")
        let field = NSTextField(frame: NSRect(x: 0, y: 0, width: 260, height: 24))
        field.stringValue = current
        field.setAccessibilityLabel("Alias for @\(handle)")
        alert.accessoryView = field
        let handler: (NSApplication.ModalResponse) -> Void = { resp in
            if resp == .alertFirstButtonReturn {
                done(field.stringValue.trimmingCharacters(in: .whitespaces))
            }
        }
        if let window {
            alert.beginSheetModal(for: window) { r in handler(r) }
        } else {
            handler(alert.runModal())
        }
        field.window?.makeFirstResponder(field)
    }
}

/// User aliases manager: edit or remove each saved alias.
@MainActor
final class AliasesWindowController: NSWindowController, NSTableViewDataSource, NSTableViewDelegate {
    private let state: AppState
    private var aliases: [AliasItem]
    private let tableView = NSTableView()
    private let cellIdentifier = NSUserInterfaceItemIdentifier("AliasCell")

    init(state: AppState, list: AliasesList) {
        self.state = state
        self.aliases = list.aliases
        let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 420, height: 340),
                              styleMask: [.titled], backing: .buffered, defer: false)
        super.init(window: window)
        window.title = "User Aliases"
        buildUI()
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    func beginSheet(for parent: NSWindow, completion: @escaping () -> Void) {
        parent.beginSheet(window!) { _ in completion() }
        window?.makeFirstResponder(tableView)
    }

    func update(_ list: AliasesList) {
        aliases = list.aliases
        tableView.reloadData()
    }

    private func dismiss() {
        guard let window, let parent = window.sheetParent else { return }
        parent.endSheet(window)
    }

    private func buildUI() {
        guard let content = window?.contentView else { return }
        let aliasCol = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("alias"))
        aliasCol.title = "Alias"
        aliasCol.width = 180
        tableView.addTableColumn(aliasCol)
        let handleCol = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("handle"))
        handleCol.title = "User"
        handleCol.width = 200
        tableView.addTableColumn(handleCol)
        tableView.dataSource = self
        tableView.delegate = self
        tableView.doubleAction = #selector(editSelected)
        tableView.target = self
        tableView.setAccessibilityLabel("User aliases")

        let scroll = NSScrollView()
        scroll.documentView = tableView
        scroll.hasVerticalScroller = true
        scroll.borderType = .bezelBorder
        scroll.translatesAutoresizingMaskIntoConstraints = false
        scroll.heightAnchor.constraint(greaterThanOrEqualToConstant: 200).isActive = true

        let edit = NSButton(title: "Edit", target: self, action: #selector(editSelected))
        edit.bezelStyle = .rounded
        let remove = NSButton(title: "Remove", target: self, action: #selector(removeSelected))
        remove.bezelStyle = .rounded
        let rowButtons = NSStackView(views: [edit, remove])
        rowButtons.spacing = 8

        let close = NSButton(title: "Close", target: self, action: #selector(closeSheet))
        close.bezelStyle = .rounded
        close.keyEquivalent = "\u{1b}"
        let bottom = NSStackView(views: [NSView(), close])

        let stack = NSStackView(views: [scroll, rowButtons, bottom])
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

    private var selected: AliasItem? {
        aliases.indices.contains(tableView.selectedRow) ? aliases[tableView.selectedRow] : nil
    }

    @objc private func editSelected() {
        guard let item = selected else { return }
        AliasPromptSheet.run(handle: item.handle, current: item.alias, in: window) { [weak self] value in
            guard let self else { return }
            if value.isEmpty {
                self.state.clearAlias(key: item.key, handle: item.handle)
            } else {
                self.state.setAlias(key: item.key, handle: item.handle, alias: value)
            }
            self.state.listAliases() // refresh this manager (runs after the change)
        }
    }
    @objc private func removeSelected() {
        guard let item = selected else { return }
        state.clearAlias(key: item.key, handle: item.handle)
        state.listAliases() // refresh the list after the removal
    }
    @objc private func closeSheet() { dismiss() }

    func numberOfRows(in tableView: NSTableView) -> Int { aliases.count }
    func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?,
                   row: Int) -> NSView? {
        let cell = reuseCell(tableView, cellIdentifier)
        if aliases.indices.contains(row) {
            let item = aliases[row]
            let isHandle = tableColumn?.identifier.rawValue == "handle"
            let text = isHandle ? "@\(item.handle)" : item.alias
            cell.textField?.stringValue = text
            cell.setAccessibilityLabel(isHandle ? "User @\(item.handle)" : "Alias \(item.alias)")
        }
        return cell
    }
}
