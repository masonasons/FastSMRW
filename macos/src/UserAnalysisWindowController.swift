//
//  UserAnalysisWindowController.swift
//
//  "User Analysis…" sheet (Account menu): pick an analysis of your follow
//  relationships. Running one sends analyze_users to the core, which fetches your
//  complete followers/following lists and spawns a user timeline of the result
//  (or announces an error if the lists can't be fully loaded).
//

import AppKit

@MainActor
final class UserAnalysisWindowController: NSWindowController, NSTableViewDataSource,
    NSTableViewDelegate {
    // The analyses offered, in list order. Keep in sync with the Windows/Android
    // pickers and the categories handled in CoreSession::cmd_analyze_users.
    private struct Analysis {
        let title: String
        let category: String
    }
    private let analyses: [Analysis] = [
        Analysis(title: "People who follow you that you don't follow back",
                 category: "not_following_back"),
        Analysis(title: "People you follow who don't follow you back", category: "no_followback"),
        Analysis(title: "Mutual follows (you both follow each other)", category: "mutuals"),
    ]

    private let state: AppState
    private let tableView = NSTableView()
    private let runButton = NSButton()

    init(state: AppState) {
        self.state = state
        let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 420, height: 260),
                              styleMask: [.titled], backing: .buffered, defer: false)
        super.init(window: window)
        window.title = "User Analysis"
        buildUI()
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    func beginSheet(for parent: NSWindow, completion: @escaping () -> Void) {
        parent.beginSheet(window!) { _ in completion() }
        tableView.selectRowIndexes(IndexSet(integer: 0), byExtendingSelection: false)
        window?.makeFirstResponder(tableView)
    }

    private func dismiss() {
        guard let window, let parent = window.sheetParent else { return }
        parent.endSheet(window)
    }

    private var selected: Analysis? {
        analyses.indices.contains(tableView.selectedRow) ? analyses[tableView.selectedRow] : nil
    }

    private func buildUI() {
        guard let content = window?.contentView else { return }
        let column = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("analysis"))
        column.resizingMask = .autoresizingMask
        tableView.addTableColumn(column)
        tableView.headerView = nil
        tableView.dataSource = self
        tableView.delegate = self
        tableView.allowsEmptySelection = false
        tableView.doubleAction = #selector(run)
        tableView.target = self
        tableView.setAccessibilityLabel("Analyses to run")

        let scroll = NSScrollView()
        scroll.documentView = tableView
        scroll.hasVerticalScroller = true
        scroll.borderType = .bezelBorder
        scroll.translatesAutoresizingMaskIntoConstraints = false
        scroll.heightAnchor.constraint(greaterThanOrEqualToConstant: 160).isActive = true

        runButton.title = "Run"
        runButton.bezelStyle = .rounded
        runButton.keyEquivalent = "\r"
        runButton.target = self
        runButton.action = #selector(run)
        let cancel = NSButton(title: "Cancel", target: self, action: #selector(cancel))
        cancel.bezelStyle = .rounded
        cancel.keyEquivalent = "\u{1b}"

        let buttons = NSStackView(views: [NSView(), cancel, runButton])
        buttons.orientation = .horizontal
        buttons.spacing = 8

        let stack = NSStackView(views: [scroll, buttons])
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

    // MARK: Actions

    @objc private func run() {
        guard let entry = selected else { return }
        state.analyzeUsers(category: entry.category)
        dismiss()
    }

    @objc private func cancel() { dismiss() }

    // MARK: Table

    private let cellIdentifier = NSUserInterfaceItemIdentifier("AnalysisCell")

    func numberOfRows(in tableView: NSTableView) -> Int { analyses.count }

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
        guard analyses.indices.contains(row) else { return cell }
        let title = analyses[row].title
        cell.textField?.stringValue = title
        cell.setAccessibilityLabel(title)
        return cell
    }
}
