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
    /// What the table currently shows, for skipping no-op reloads (which would
    /// make VoiceOver re-announce the focused row).
    private var lastRenderedRows: [Row] = []
    private var lastRenderedKey = ""

    private var rows: [Row] { state.currentRows }
    private var currentKey: String {
        guard state.timelines.indices.contains(state.currentIndex) else { return "" }
        let timeline = state.timelines[state.currentIndex]
        // Scope the remembered reading position by account, so the same kind
        // (e.g. "home") in two accounts doesn't share a position — switching
        // accounts was landing on the other account's post when its id existed
        // in both (same-instance accounts share post ids). Title keeps two
        // spawned timelines of the same kind distinct too.
        return "\(state.selectedAccountKey)/\(timeline.kind)/\(timeline.title)"
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
        // Option+arrows drive movement units (Up/Down jump by the unit, Left/Right
        // pick the unit). Handled on the table (not menu key-equivalents) so
        // VoiceOver speaks the core's announcement, not a menu title. (Reordering
        // timelines with Shift+arrows lives on the timelines sidebar.)
        tableView.onOptionUp = { [weak self] in self?.state.moveByUnit(dir: "prev") }
        tableView.onOptionDown = { [weak self] in self?.state.moveByUnit(dir: "next") }
        tableView.onOptionLeft = { [weak self] in self?.state.cycleMovement(dir: "prev") }
        tableView.onOptionRight = { [weak self] in self?.state.cycleMovement(dir: "next") }
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
            case "p": self.pinPostSelection(nil)
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
        // Auto-refresh and streaming re-emit timeline_updated even when nothing
        // visible changed, and reloadData tears down the focused cell — which
        // makes VoiceOver re-announce the row the user is sitting on, over and
        // over. If the rows are identical and the selection is already where
        // we'd restore it, there is nothing to do.
        if currentKey == lastRenderedKey, rows == lastRenderedRows,
           tableView.selectedRow >= 0 {
            return
        }
        // Same timeline with a selection to protect: update incrementally so the
        // focused cell view is never torn down. reloadData rebuilds every cell,
        // and each rebuild makes VoiceOver re-read the row the user is sitting
        // on — which happened constantly (every relative-timestamp re-render,
        // every post streaming in). Insert/remove keeps surviving cells (and
        // the selection, which AppKit shifts along) untouched, and content-only
        // changes are written straight into the existing cell views.
        if currentKey == lastRenderedKey, tableView.selectedRow >= 0,
           tableView.numberOfRows == lastRenderedRows.count, !lastRenderedRows.isEmpty,
           selectionSurvives() {
            applyIncrementalUpdate()
            return
        }
        lastRenderedKey = currentKey
        lastRenderedRows = rows
        // Hold the programmatic-selection guard across this whole reload AND the next
        // runloop turn. reloadData + selectRow trigger selection-change notifications
        // that AppKit frequently delivers asynchronously — after this method returns
        // and after a synchronous guard has already cleared. Without spanning the turn,
        // that late notification reports whatever row got auto-selected (often row 0)
        // and the core persists the top as the reading position.
        isUpdatingSelectionProgrammatically = true
        tableView.reloadData()
        updateStatusLabel()
        // Prefer our own remembered row for this timeline (updated on every move, so it
        // survives leaving/returning and streaming updates). If that row is gone, adopt
        // the core's re-anchored position — it re-emits selected_id pointing at whatever
        // now occupies the vanished row's old slot, never the top. Only fall to the edge
        // when neither resolves (e.g. the remembered row hasn't loaded yet).
        let tracked = selectionByKey[currentKey]
        if let tracked, let index = rows.firstIndex(where: { $0.id == tracked }) {
            selectRow(index)
        } else if !state.currentSelectedId.isEmpty,
                  let index = rows.firstIndex(where: { $0.id == state.currentSelectedId }) {
            selectionByKey[currentKey] = state.currentSelectedId // follow the core re-anchor
            selectRow(index)
        } else if !rows.isEmpty {
            selectRow(state.currentReversed ? rows.count - 1 : 0)
        }
        DispatchQueue.main.async { [weak self] in
            self?.isUpdatingSelectionProgrammatically = false
        }
    }

    /// The row the user is reading still exists in the new row set (so an
    /// incremental update can preserve it).
    private func selectionSurvives() -> Bool {
        let selected = tableView.selectedRow
        guard lastRenderedRows.indices.contains(selected) else { return false }
        let id = lastRenderedRows[selected].id
        return rows.contains { $0.id == id }
    }

    /// Apply the new row set as insert/remove/edit-in-place against what the
    /// table currently shows, without reloadData.
    private func applyIncrementalUpdate() {
        let old = lastRenderedRows
        let new = rows
        lastRenderedRows = new

        // Structural changes (posts streamed in, gaps filled, deletions).
        let diff = new.map(\.id).difference(from: old.map(\.id))
        if !diff.isEmpty {
            isUpdatingSelectionProgrammatically = true
            tableView.beginUpdates()
            for change in diff {
                switch change {
                case let .remove(offset, _, _):
                    tableView.removeRows(at: IndexSet(integer: offset), withAnimation: [])
                case let .insert(offset, _, _):
                    tableView.insertRows(at: IndexSet(integer: offset), withAnimation: [])
                }
            }
            tableView.endUpdates()
            DispatchQueue.main.async { [weak self] in
                self?.isUpdatingSelectionProgrammatically = false
            }
        }

        // Content-only changes (relative timestamps, favorite/boost counts):
        // write into the existing cell views; no cell replacement, no announce.
        var oldById: [String: Row] = [:]
        for row in old { oldById[row.id] = row }
        for (index, row) in new.enumerated() where oldById[row.id] != row {
            guard oldById[row.id] != nil else { continue } // freshly inserted; cell is current
            if let cell = tableView.view(atColumn: 0, row: index, makeIfNecessary: false)
                as? NSTableCellView, cell.textField?.stringValue != row.text {
                cell.textField?.stringValue = row.text
                cell.textField?.setAccessibilityLabel(row.text)
                tableView.noteHeightOfRows(withIndexesChanged: IndexSet(integer: index))
            }
        }
        updateStatusLabel()
        // Keep the reading row on screen (it may have shifted down as posts
        // streamed in above), and keep our per-timeline anchor accurate.
        if tableView.selectedRow >= 0 {
            tableView.scrollRowToVisible(tableView.selectedRow)
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
        // Save/restore rather than force false: when called from reload() the guard is
        // already held for the runloop turn, and clearing it here would expose the
        // coalesced post-reload selection notification.
        let prev = isUpdatingSelectionProgrammatically
        isUpdatingSelectionProgrammatically = true
        tableView.selectRowIndexes(IndexSet(integer: index), byExtendingSelection: false)
        tableView.scrollRowToVisible(index)
        isUpdatingSelectionProgrammatically = prev
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
    @objc func bookmarkSelection(_ sender: Any?) {
        guard let id = selectedRowId else { return }
        state.toggleBookmark(id: id)
    }
    // The standard Copy action (⌘C), reachable via the Edit menu when the posts
    // list is first responder; the core composes the copy string.
    @objc func copySelection(_ sender: Any?) {
        guard let id = selectedRowId else { return }
        state.copy(id: id)
    }
    @objc func copy(_ sender: Any?) { copySelection(sender) }
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
