//
//  TimelineViewController.swift
//
//  The right pane: the posts list. Each row is one post/notification, its
//  VoiceOver label composed by the core (StatusPresenter). Up/down navigate
//  rows; left/right switch timelines; Tab moves to the sidebar; single-key
//  r/b/f/q act on the selected post (via the Status menu). Adapted from
//  FastSMApple to the command/event core. M1 subset — more actions land later.
//

import AppKit

@MainActor
final class TimelineViewController: NSViewController, NSTableViewDataSource, NSTableViewDelegate {
    private let state: AppState
    private let tableView = NavigableTableView()
    private let cellIdentifier = NSUserInterfaceItemIdentifier("PostCell")
    private let statusLabel = NSTextField(labelWithString: "")

    /// Called when the user presses Tab to move to the timelines pane.
    var onMoveToTimelines: (() -> Void)?

    private var isUpdatingSelectionProgrammatically = false
    /// Prevents stacking load requests; reset when a fresh update arrives.
    private var loadPending = false
    /// The post the user is on in each timeline, tracked by id keyed by the
    /// timeline's stable kind. Updated on every user move (the core saves the
    /// position but doesn't re-emit it), so leaving and returning to a timeline
    /// restores where you actually left off — not the core's last-emitted row.
    private var selectionByKey: [String: String] = [:]

    private var rows: [Row] { state.currentRows }
    private var currentKey: String {
        state.timelines.indices.contains(state.currentIndex)
            ? state.timelines[state.currentIndex].kind : ""
    }
    private var selectedRowId: String? {
        let row = tableView.selectedRow
        return rows.indices.contains(row) ? rows[row].id : nil
    }

    init(state: AppState) {
        self.state = state
        super.init(nibName: nil, bundle: nil)
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    override func loadView() {
        let column = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("post"))
        column.title = "Posts"
        column.resizingMask = .autoresizingMask
        tableView.addTableColumn(column)
        tableView.headerView = nil
        tableView.usesAutomaticRowHeights = true
        tableView.dataSource = self
        tableView.delegate = self
        tableView.allowsEmptySelection = true
        tableView.allowsMultipleSelection = false
        tableView.style = .inset
        tableView.setAccessibilityLabel("Posts")

        tableView.onTab = { [weak self] in self?.onMoveToTimelines?() }
        tableView.onBacktab = { [weak self] in self?.onMoveToTimelines?() }
        tableView.onLeftArrow = { [weak self] in self?.state.selectTimeline(dir: "prev") }
        tableView.onRightArrow = { [weak self] in self?.state.selectTimeline(dir: "next") }
        tableView.onBoundary = { [weak self] in self?.state.playEarcon("boundary") }
        // Return / Shift+Return are the configurable interact / secondary-interact
        // actions; the core resolves them from Behavior settings for the current row.
        tableView.onReturn = { [weak self] in self?.state.performAction("Enter") }
        tableView.onShiftReturn = { [weak self] in self?.state.performAction("SecondaryAction") }
        // Enter on a user row with enter_user_action = "actions" (the default):
        // the core asks for the platform "user actions" affordance — the profile.
        state.onUserActionsMenu = { [weak self] in self?.openUserProfile(nil) }
        tableView.onCommandReturn = { [weak self] in self?.openLinksForSelection(nil) }
        tableView.onSpace = { [weak self] in self?.viewThread(nil) }
        // Backspace closes the current timeline; ⌘Backspace deletes the post.
        tableView.onDelete = { [weak self] in self?.closeCurrentTimeline(nil) }
        tableView.onCommandDelete = { [weak self] in self?.deleteSelection(nil) }
        // Single-key post actions (the table would otherwise type-select on them).
        // These mirror the Status menu's letter shortcuts; consuming the key here
        // is safe because menu key-equivalents run before keyDown, never both.
        tableView.onCharacter = { [weak self] ch in
            guard let self else { return false }
            switch ch {
            case "r": self.replyToSelection(nil)
            case "b": self.boostSelection(nil)
            case "f": self.favoriteSelection(nil)
            case "q": self.quoteSelection(nil)
            case "u": self.openUserTimeline(nil)
            case "e": self.editSelection(nil)
            default: return false
            }
            return true
        }

        let scrollView = NSScrollView()
        scrollView.documentView = tableView
        scrollView.hasVerticalScroller = true
        scrollView.translatesAutoresizingMaskIntoConstraints = false

        statusLabel.translatesAutoresizingMaskIntoConstraints = false
        statusLabel.textColor = .secondaryLabelColor
        statusLabel.font = .systemFont(ofSize: 11)
        statusLabel.setAccessibilityLabel("Timeline status")

        let container = NSView()
        container.addSubview(scrollView)
        container.addSubview(statusLabel)
        NSLayoutConstraint.activate([
            scrollView.topAnchor.constraint(equalTo: container.topAnchor),
            scrollView.leadingAnchor.constraint(equalTo: container.leadingAnchor),
            scrollView.trailingAnchor.constraint(equalTo: container.trailingAnchor),
            scrollView.bottomAnchor.constraint(equalTo: statusLabel.topAnchor, constant: -2),
            statusLabel.leadingAnchor.constraint(equalTo: container.leadingAnchor, constant: 8),
            statusLabel.trailingAnchor.constraint(equalTo: container.trailingAnchor, constant: -8),
            statusLabel.bottomAnchor.constraint(equalTo: container.bottomAnchor, constant: -4),
        ])
        view = container
    }

    func focusTable() { view.window?.makeFirstResponder(tableView) }

    func reload() {
        loadPending = false // a fresh list arrived; allow loading again
        // Prefer our own remembered position for this timeline (updated on every
        // move, so it survives leaving/returning and streaming updates); fall back
        // to the core's restored position the first time we show it.
        let target = selectionByKey[currentKey]
            ?? (state.currentSelectedId.isEmpty ? nil : state.currentSelectedId)
        // Guard reloadData too: it can fire a selection change that would post a
        // spurious note_selection for a stale row.
        isUpdatingSelectionProgrammatically = true
        tableView.reloadData()
        isUpdatingSelectionProgrammatically = false
        updateStatusLabel()
        if let target, let index = rows.firstIndex(where: { $0.id == target }) {
            selectRow(index)
        } else if !rows.isEmpty {
            selectRow(state.currentReversed ? rows.count - 1 : 0)
        }
    }

    /// Move selection to a specific post because the core asked us to (e.g. after
    /// Go Back or a jump): remember it as this timeline's position.
    func select(id: String) {
        guard let index = rows.firstIndex(where: { $0.id == id }) else { return }
        selectionByKey[currentKey] = id
        selectRow(index)
    }

    /// Purely visual selection; does not change the anchor.
    private func selectRow(_ index: Int) {
        guard rows.indices.contains(index) else { return }
        isUpdatingSelectionProgrammatically = true
        tableView.selectRowIndexes(IndexSet(integer: index), byExtendingSelection: false)
        tableView.scrollRowToVisible(index)
        isUpdatingSelectionProgrammatically = false
    }

    private func updateStatusLabel() {
        statusLabel.stringValue = rows.isEmpty ? "No posts"
            : (rows.count == 1 ? "1 item" : "\(rows.count) items")
    }

    // MARK: Actions (invoked via MainWindowController forwarders / Status menu)

    @objc func composePost(_ sender: Any?) { state.requestCompose(mode: "new") }
    @objc func refreshTimeline(_ sender: Any?) { state.refresh() }
    @objc func replyToSelection(_ sender: Any?) {
        guard let id = selectedRowId else { return }
        state.requestCompose(mode: "reply", id: id)
    }
    @objc func quoteSelection(_ sender: Any?) {
        guard let id = selectedRowId else { return }
        state.requestCompose(mode: "quote", id: id)
    }
    @objc func boostSelection(_ sender: Any?) {
        guard let id = selectedRowId else { return }
        state.toggleBoost(id: id)
    }
    @objc func favoriteSelection(_ sender: Any?) {
        guard let id = selectedRowId else { return }
        state.toggleFavorite(id: id)
    }
    @objc func openSelectionInBrowser(_ sender: Any?) {
        guard let id = selectedRowId else { return }
        state.openStatus(id: id)
    }
    @objc func viewThread(_ sender: Any?) {
        guard let id = selectedRowId else { return }
        state.openThread(id: id)
    }
    @objc func openUserTimeline(_ sender: Any?) {
        guard let id = selectedRowId else { return }
        state.openUserTimeline(id: id)
    }
    @objc func openUserProfile(_ sender: Any?) {
        guard let id = selectedRowId else { return }
        state.openUserProfile(id: id)
    }
    @objc func addAlias(_ sender: Any?) {
        guard let id = selectedRowId else { return }
        state.beginAlias(id: id)
    }
    @objc func openFollowers(_ sender: Any?) {
        guard let id = selectedRowId else { return }
        state.openFollowers(id: id)
    }
    @objc func openFollowing(_ sender: Any?) {
        guard let id = selectedRowId else { return }
        state.openFollowing(id: id)
    }
    @objc func followHashtagForSelection(_ sender: Any?) {
        guard let id = selectedRowId else { return }
        state.followHashtagPrompt(id: id)
    }
    @objc func showPostInfo(_ sender: Any?) {
        guard let id = selectedRowId else { return }
        state.postInfo(id: id)
    }
    @objc func editSelection(_ sender: Any?) {
        guard let id = selectedRowId else { return }
        state.requestCompose(mode: "edit", id: id)
    }
    @objc func pinPostSelection(_ sender: Any?) {
        guard let id = selectedRowId else { return }
        state.togglePinPost(id: id)
    }
    @objc func muteConversationSelection(_ sender: Any?) {
        guard let id = selectedRowId else { return }
        state.toggleMuteConversation(id: id)
    }
    @objc func speakUserForSelection(_ sender: Any?) {
        guard let id = selectedRowId else { return }
        state.speakUser(id: id)
    }
    @objc func speakReplyForSelection(_ sender: Any?) {
        guard let id = selectedRowId else { return }
        state.speakReply(id: id)
    }
    @objc func closeCurrentTimeline(_ sender: Any?) {
        guard state.timelines.indices.contains(state.currentIndex),
              state.timelines[state.currentIndex].dismissable else {
            state.playEarcon("boundary")
            return
        }
        state.closeTimeline()
    }
    @objc func deleteSelection(_ sender: Any?) {
        guard let id = selectedRowId else { return }
        guard state.confirmDeletePost, let window = view.window else {
            state.deletePost(id: id); return
        }
        let alert = NSAlert()
        alert.messageText = "Delete this post?"
        alert.addButton(withTitle: "Delete")
        alert.addButton(withTitle: "Cancel")
        alert.beginSheetModal(for: window) { [weak self] r in
            if r == .alertFirstButtonReturn { self?.state.deletePost(id: id) }
        }
    }
    @objc func playMediaForSelection(_ sender: Any?) {
        guard let id = selectedRowId else { return }
        state.playMedia(id: id)
    }
    @objc func openLinksForSelection(_ sender: Any?) {
        guard let id = selectedRowId else { return }
        state.openPostLinks(id: id)
    }

    // MARK: Table

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
            let textField = NSTextField(wrappingLabelWithString: "")
            textField.translatesAutoresizingMaskIntoConstraints = false
            textField.isSelectable = false
            cell.addSubview(textField)
            cell.textField = textField
            NSLayoutConstraint.activate([
                textField.leadingAnchor.constraint(equalTo: cell.leadingAnchor, constant: 8),
                textField.trailingAnchor.constraint(equalTo: cell.trailingAnchor, constant: -8),
                textField.topAnchor.constraint(equalTo: cell.topAnchor, constant: 4),
                textField.bottomAnchor.constraint(equalTo: cell.bottomAnchor, constant: -4),
            ])
        }
        guard rows.indices.contains(row) else { return cell }
        let text = rows[row].text
        cell.textField?.stringValue = text
        // The core composes the spoken label; the visible text is the same string.
        cell.textField?.setAccessibilityLabel(text)
        cell.setAccessibilityLabel(text)
        return cell
    }

    func tableViewSelectionDidChange(_ notification: Notification) {
        guard !isUpdatingSelectionProgrammatically else { return }
        let row = tableView.selectedRow
        guard rows.indices.contains(row) else { return }
        // The user moved: remember this post for this timeline and persist it.
        let id = rows[row].id
        selectionByKey[currentKey] = id
        state.noteSelection(id: id)
        maybeLoadOlder(row: row)
    }

    /// Load older posts as the cursor nears the bottom (or fill a tracked gap
    /// near it). Front-end-driven, mirroring the Windows app.
    private func maybeLoadOlder(row: Int) {
        guard !loadPending else { return }
        let count = rows.count
        // A tracked gap within a few rows of the cursor: fill it transparently.
        for d in 0...5 {
            for g in [row + d, row - d] where rows.indices.contains(g) {
                if rows[g].gapAfter {
                    loadPending = true
                    state.loadGap(id: rows[g].id)
                    return
                }
            }
        }
        // Older posts are at the bottom normally, at the top when reversed.
        let nearEdge = state.currentReversed ? (row <= 9) : (row >= count - 10)
        if count > 0, nearEdge, state.autoLoadOlder {
            loadPending = true
            state.loadOlder(automatic: true)
        }
    }
}
