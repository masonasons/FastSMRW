//
//  MainViewController.swift
//
//  The main screen: a horizontally scrolling strip of timeline tabs above the
//  posts table — the phone-shaped equivalent of the desktop sidebar + posts
//  split. Every label and spoken string comes composed from the core; rows get
//  VoiceOver custom actions for the post verbs, magic tap composes, and the
//  escape gesture goes back.
//

import UIKit

@MainActor
final class MainViewController: UIViewController {
    private let state: AppState

    /// Root asked to present the add-account sheet.
    var onAddAccount: (() -> Void)?

    private let tabScroll = UIScrollView()
    private let tabStack = UIStackView()
    private let tableView = UITableView(frame: .zero, style: .plain)
    private let refreshControl = UIRefreshControl()
    private var tabTopConstraints: [NSLayoutConstraint] = []
    private var tabBottomConstraints: [NSLayoutConstraint] = []
    private var tabsAtBottom: Bool?

    private func applyTabBarPosition() {
        let bottom = (state.settingsRaw["tab_bar_position"] as? String ?? "bottom") == "bottom"
        guard bottom != tabsAtBottom else { return }
        tabsAtBottom = bottom
        NSLayoutConstraint.deactivate(bottom ? tabTopConstraints : tabBottomConstraints)
        NSLayoutConstraint.activate(bottom ? tabBottomConstraints : tabTopConstraints)
    }

    private var rows: [Row] { state.currentRows }
    private var loadPending = false
    /// What the table currently shows, for skipping no-op reloads (which would
    /// disturb VoiceOver's reading position).
    private var lastRenderedRows: [Row] = []
    private var lastRenderedKey = ""
    /// The post row VoiceOver is currently on (nil when focus is elsewhere —
    /// a bar button, the tab strip, …). Drives the magic-tap behavior.
    private weak var focusedPostCell: PostCell?
    var isPostFocused: Bool { focusedPostCell != nil }
    /// Reading position per timeline, tracked by post id so it survives leaving
    /// / returning and posts streaming in above — same pattern as Mac/Windows.
    private var selectionByKey: [String: String] = [:]
    private weak var composeVC: ComposeViewController?
    // Open managers, so refresh events update them in place instead of
    // pushing duplicates.
    private weak var followedHashtagsVC: HashtagsViewController?
    private weak var trendingHashtagsVC: HashtagsViewController?
    private weak var listsVC: ListsViewController?
    private weak var serverFiltersVC: ServerFiltersViewController?
    private weak var aliasesVC: AliasesViewController?

    private var currentKey: String {
        guard state.timelines.indices.contains(state.currentIndex) else { return "" }
        let timeline = state.timelines[state.currentIndex]
        // Scope the remembered reading position by account, so the same kind
        // (e.g. "home") in two accounts doesn't share a position — switching
        // accounts otherwise lands on the other account's post when its id
        // exists in both (same-instance accounts share post ids).
        return "\(state.selectedAccountKey)/\(timeline.kind)/\(timeline.title)"
    }

    init(state: AppState) {
        self.state = state
        super.init(nibName: nil, bundle: nil)
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    // MARK: Layout

    override func viewDidLoad() {
        super.viewDidLoad()
        view.backgroundColor = .systemBackground

        navigationItem.rightBarButtonItems = [
            UIBarButtonItem(barButtonSystemItem: .compose, target: self,
                            action: #selector(composeTapped)),
            UIBarButtonItem(barButtonSystemItem: .refresh, target: self,
                            action: #selector(refreshTapped)),
        ]
        navigationItem.leftBarButtonItem = UIBarButtonItem(
            image: UIImage(systemName: "ellipsis.circle"), menu: moreMenu())
        navigationItem.leftBarButtonItem?.accessibilityLabel = "More"

        tabScroll.showsHorizontalScrollIndicator = false
        tabScroll.translatesAutoresizingMaskIntoConstraints = false
        tabStack.axis = .horizontal
        tabStack.spacing = 4
        tabStack.translatesAutoresizingMaskIntoConstraints = false
        tabScroll.addSubview(tabStack)

        tableView.dataSource = self
        tableView.delegate = self
        tableView.register(PostCell.self, forCellReuseIdentifier: PostCell.reuseIdentifier)
        tableView.translatesAutoresizingMaskIntoConstraints = false
        refreshControl.addTarget(self, action: #selector(pullToRefresh), for: .valueChanged)
        tableView.refreshControl = refreshControl

        // Horizontal swipes on the posts area switch timelines (matching the
        // left/right arrows on desktop). VoiceOver users use the tab strip.
        let swipeLeft = UISwipeGestureRecognizer(target: self, action: #selector(swipedLeft))
        swipeLeft.direction = .left
        let swipeRight = UISwipeGestureRecognizer(target: self, action: #selector(swipedRight))
        swipeRight.direction = .right
        tableView.addGestureRecognizer(swipeLeft)
        tableView.addGestureRecognizer(swipeRight)

        view.addSubview(tabScroll)
        view.addSubview(tableView)
        NSLayoutConstraint.activate([
            tabScroll.leadingAnchor.constraint(equalTo: view.leadingAnchor),
            tabScroll.trailingAnchor.constraint(equalTo: view.trailingAnchor),
            tabScroll.heightAnchor.constraint(equalToConstant: 44),
            tabStack.topAnchor.constraint(equalTo: tabScroll.contentLayoutGuide.topAnchor),
            tabStack.bottomAnchor.constraint(equalTo: tabScroll.contentLayoutGuide.bottomAnchor),
            tabStack.leadingAnchor.constraint(equalTo: tabScroll.contentLayoutGuide.leadingAnchor,
                                              constant: 8),
            tabStack.trailingAnchor.constraint(equalTo: tabScroll.contentLayoutGuide.trailingAnchor,
                                               constant: -8),
            tabStack.heightAnchor.constraint(equalTo: tabScroll.frameLayoutGuide.heightAnchor),
            tableView.leadingAnchor.constraint(equalTo: view.leadingAnchor),
            tableView.trailingAnchor.constraint(equalTo: view.trailingAnchor),
        ])
        // The tab strip can sit above or below the posts (the tab_bar_position
        // setting, like Android); only the positional constraints swap.
        tabTopConstraints = [
            tabScroll.topAnchor.constraint(equalTo: view.safeAreaLayoutGuide.topAnchor),
            tableView.topAnchor.constraint(equalTo: tabScroll.bottomAnchor),
            tableView.bottomAnchor.constraint(equalTo: view.bottomAnchor),
        ]
        tabBottomConstraints = [
            tableView.topAnchor.constraint(equalTo: view.safeAreaLayoutGuide.topAnchor),
            tabScroll.topAnchor.constraint(equalTo: tableView.bottomAnchor),
            tabScroll.bottomAnchor.constraint(equalTo: view.safeAreaLayoutGuide.bottomAnchor),
        ]
        applyTabBarPosition()
        state.onSettings = { [weak self] in self?.settingsChanged() }

        wireCallbacks()
        buildHardwareKeys()
        reloadTimelines()
        reloadRows()
    }

    override func viewDidAppear(_ animated: Bool) {
        super.viewDidAppear(animated)
        becomeFirstResponder() // hardware key commands
    }

    private func wireCallbacks() {
        state.onTimelinesChanged = { [weak self] in self?.reloadTimelines() }
        state.onTimelineRows = { [weak self] index in
            guard let self, index == self.state.currentIndex else { return }
            self.reloadRows()
        }
        state.onSelectRow = { [weak self] id in self?.select(id: id) }
        state.onComposeContext = { [weak self] context in self?.presentCompose(context) }
        state.onPostResult = { [weak self] ok in self?.composeVC?.postFinished(ok: ok) }
        state.onPostInfo = { [weak self] info in
            guard let self else { return }
            self.push(PostInfoViewController(state: self.state, info: info))
        }
        state.onURLPicker = { [weak self] picker in self?.showURLPicker(picker) }
        state.onAccountSettings = { [weak self] settings in
            self?.showAccountSettings(settings)
        }
        state.onSpawnable = { [weak self] spawnables in
            guard let self else { return }
            self.push(NewTimelineViewController(state: self.state, spawnables: spawnables))
        }
        state.onUserProfile = { [weak self] profile in
            guard let self else { return }
            self.push(UserProfileViewController(state: self.state, profile: profile))
        }
        state.onUserLists = { [weak self] userLists in
            guard let self else { return }
            guard userLists.supported else {
                showError("Lists are only available for Mastodon accounts.",
                          on: self.topPresenter)
                return
            }
            self.push(UserListsViewController(state: self.state, userLists: userLists))
        }
        state.onUserPicker = { [weak self] picker in self?.showUserPicker(picker) }
        state.onAliasPrompt = { [weak self] prompt in self?.showAliasPrompt(prompt) }
        state.onFollowedHashtags = { [weak self] hashtags in
            guard let self else { return }
            if let open = self.followedHashtagsVC {
                open.update(hashtags)
            } else if hashtags.supported {
                let vc = HashtagsViewController(state: self.state, mode: .followed,
                                                hashtags: hashtags)
                self.followedHashtagsVC = vc
                self.push(vc)
            } else {
                showError("Followed hashtags are only available for Mastodon accounts.",
                          on: self.topPresenter)
            }
        }
        state.onTrendingHashtags = { [weak self] hashtags in
            guard let self else { return }
            if let open = self.trendingHashtagsVC {
                open.update(hashtags)
            } else if hashtags.supported {
                let vc = HashtagsViewController(state: self.state, mode: .trending,
                                                hashtags: hashtags)
                self.trendingHashtagsVC = vc
                self.push(vc)
            } else {
                showError("Trending hashtags are only available for Mastodon accounts.",
                          on: self.topPresenter)
            }
        }
        state.onLists = { [weak self] lists in
            guard let self else { return }
            if let open = self.listsVC {
                open.update(lists)
            } else if lists.supported {
                let vc = ListsViewController(state: self.state, lists: lists)
                self.listsVC = vc
                self.push(vc)
            } else {
                showError("Lists are only available for Mastodon accounts.",
                          on: self.topPresenter)
            }
        }
        state.onServerFilters = { [weak self] filters in
            guard let self else { return }
            if let open = self.serverFiltersVC {
                open.update(filters)
            } else if filters.supported {
                let vc = ServerFiltersViewController(state: self.state, filters: filters)
                self.serverFiltersVC = vc
                self.push(vc)
            } else {
                showError("Server filters are only available for Mastodon accounts.",
                          on: self.topPresenter)
            }
        }
        state.onAliasesList = { [weak self] list in
            guard let self else { return }
            if let open = self.aliasesVC {
                open.update(list)
            } else {
                let vc = AliasesViewController(state: self.state, list: list)
                self.aliasesVC = vc
                self.push(vc)
            }
        }
        state.onClientFilter = { [weak self] filter in
            guard let self else { return }
            self.push(ClientFilterViewController(state: self.state, filter: filter))
        }
        state.onProfileEditor = { [weak self] editor in
            guard let self else { return }
            self.push(ProfileEditorViewController(state: self.state, editor: editor))
        }
        state.onHashtagPrompt = { [weak self] tags in self?.showHashtagPrompt(tags) }
        state.onMediaOpen = { [weak self] media in
            guard let self else { return }
            Media.present(media, from: self.topPresenter)
        }
        state.onMediaPicker = { [weak self] picker in self?.showMediaPicker(picker) }
    }

    // MARK: Timeline tabs

    private func reloadTimelines() {
        navigationItem.title = state.currentTimelineTitle ?? "FastSM"
        for view in tabStack.arrangedSubviews { view.removeFromSuperview() }
        let count = state.timelines.count
        for (index, timeline) in state.timelines.enumerated() {
            let selected = index == state.currentIndex
            var config: UIButton.Configuration = selected ? .filled() : .plain()
            config.title = timeline.title
            config.contentInsets = NSDirectionalEdgeInsets(top: 4, leading: 10,
                                                           bottom: 4, trailing: 10)
            let button = UIButton(configuration: config)
            // Spoken form matches the Android tabs: "Home tab, pinned, muted".
            var label = "\(timeline.title) tab"
            if timeline.pinned { label += ", pinned" }
            if timeline.muted { label += ", muted" }
            button.accessibilityLabel = label
            button.accessibilityTraits = selected ? [.button, .selected] : .button
            button.tag = index
            button.addTarget(self, action: #selector(tabTapped(_:)), for: .touchUpInside)

            // One list drives both the VoiceOver custom actions and the
            // long-press menu (like Android), so the two paths can't drift.
            // Pin/mute/move/close act on the core's current timeline, so
            // select this tab first; the commands queue in order.
            var tabActions: [(String, () -> Void)] = [
                (timeline.pinned ? "Unpin Tab" : "Pin Tab", { [weak self] in
                    self?.state.selectTimeline(index: index)
                    self?.state.togglePin()
                }),
                (timeline.muted ? "Unmute Sounds" : "Mute Sounds", { [weak self] in
                    self?.state.selectTimeline(index: index)
                    self?.state.toggleMute()
                }),
            ]
            if index > 0 {
                tabActions.append(("Move Left", { [weak self] in
                    self?.state.selectTimeline(index: index)
                    self?.state.reorderTimeline(dir: "up")
                }))
            }
            if index < count - 1 {
                tabActions.append(("Move Right", { [weak self] in
                    self?.state.selectTimeline(index: index)
                    self?.state.reorderTimeline(dir: "down")
                }))
            }
            if timeline.dismissable {
                tabActions.append(("Close Tab", { [weak self] in
                    self?.state.selectTimeline(index: index)
                    self?.state.closeTimeline()
                }))
            }
            button.accessibilityCustomActions = tabActions.map { action in
                UIAccessibilityCustomAction(name: action.0) { _ in
                    action.1()
                    return true
                }
            }
            button.menu = UIMenu(children: tabActions.map { action in
                UIAction(title: action.0) { _ in action.1() }
            })
            tabStack.addArrangedSubview(button)
        }
        reloadRows()
        // Keep the selected tab visible.
        DispatchQueue.main.async { [weak self] in
            guard let self, self.tabStack.arrangedSubviews.indices.contains(self.state.currentIndex)
            else { return }
            let tab = self.tabStack.arrangedSubviews[self.state.currentIndex]
            self.tabScroll.scrollRectToVisible(tab.frame.insetBy(dx: -12, dy: 0), animated: false)
        }
        navigationItem.leftBarButtonItem?.menu = moreMenu()
    }

    @objc private func tabTapped(_ sender: UIButton) {
        state.selectTimeline(index: sender.tag)
    }

    @objc private func swipedLeft() { state.selectTimeline(dir: "next") }
    @objc private func swipedRight() { state.selectTimeline(dir: "prev") }

    // MARK: Posts table

    private func reloadRows() {
        loadPending = false // a fresh list arrived; allow loading again
        refreshControl.endRefreshing()
        // One VoiceOver rotor per movement unit (stops computed by the core).
        tableView.accessibilityCustomRotors = makeMovementRotors()
        // Never reloadData over a live timeline: rebuilding every cell yanks
        // VoiceOver's reading position around on each refresh / streamed-in
        // post / timestamp re-render. Apply changes incrementally instead; a
        // full reload only happens on timeline switches and first fill.
        if currentKey == lastRenderedKey, rows == lastRenderedRows { return }
        if currentKey == lastRenderedKey, !lastRenderedRows.isEmpty,
           tableView.numberOfRows(inSection: 0) == lastRenderedRows.count {
            applyIncrementalUpdate()
            return
        }
        lastRenderedKey = currentKey
        lastRenderedRows = rows
        tableView.reloadData()
        // Restore this timeline's remembered reading position (or adopt the
        // core's re-anchor), mirroring the Mac posts pane — but only scroll;
        // VoiceOver focus is moved solely when the core asks via select_row.
        let tracked = selectionByKey[currentKey]
        if let tracked, let index = rows.firstIndex(where: { $0.id == tracked }) {
            scrollTo(index)
        } else if !state.currentSelectedId.isEmpty,
                  let index = rows.firstIndex(where: { $0.id == state.currentSelectedId }) {
            selectionByKey[currentKey] = state.currentSelectedId
            scrollTo(index)
        } else if !rows.isEmpty, state.currentReversed {
            scrollTo(rows.count - 1)
        }
    }

    /// Apply the new row set as insert/remove/edit-in-place against what the
    /// table currently shows, without reloadData (which disturbs VoiceOver).
    private func applyIncrementalUpdate() {
        let old = lastRenderedRows
        let new = rows
        lastRenderedRows = new

        // Structural changes (posts streamed in, gaps filled, deletions).
        let diff = new.map(\.id).difference(from: old.map(\.id))
        if !diff.isEmpty {
            tableView.performBatchUpdates {
                for change in diff {
                    switch change {
                    case let .remove(offset, _, _):
                        tableView.deleteRows(at: [IndexPath(row: offset, section: 0)],
                                             with: .none)
                    case let .insert(offset, _, _):
                        tableView.insertRows(at: [IndexPath(row: offset, section: 0)],
                                             with: .none)
                    }
                }
            }
        }

        // Content-only changes (timestamps, counts, boost/favorite state):
        // rewrite the existing cells; no cell replacement, no focus disturbance.
        var oldById: [String: Row] = [:]
        for row in old { oldById[row.id] = row }
        var heightsChanged = false
        for (index, row) in new.enumerated() where oldById[row.id] != nil
            && oldById[row.id] != row {
            if let cell = tableView.cellForRow(at: IndexPath(row: index, section: 0))
                as? PostCell {
                cell.configure(text: row.text)
                cell.accessibilityCustomActions = accessibilityActions(for: row)
                heightsChanged = true
            }
        }
        if heightsChanged {
            tableView.performBatchUpdates {} // re-measure heights without reloading
        }
    }

    private func scrollTo(_ index: Int) {
        guard rows.indices.contains(index) else { return }
        tableView.scrollToRow(at: IndexPath(row: index, section: 0), at: .middle,
                              animated: false)
    }

    // MARK: Hardware keyboard (iPad / Bluetooth keyboards)

    override var canBecomeFirstResponder: Bool { true }
    override var keyCommands: [UIKeyCommand]? { hardwareKeyCommands }

    private var hardwareKeyCommands: [UIKeyCommand] = []
    private var keyHandlers: [String: () -> Void] = [:]

    private func keyId(_ input: String, _ flags: UIKeyModifierFlags) -> String {
        "\(input)#\(flags.rawValue)"
    }

    private func addKey(_ input: String, _ flags: UIKeyModifierFlags = [], _ title: String,
                        priority: Bool = false, _ run: @escaping () -> Void) {
        let command = UIKeyCommand(title: title, action: #selector(hardwareKey(_:)),
                                   input: input, modifierFlags: flags)
        if priority { command.wantsPriorityOverSystemBehavior = true }
        hardwareKeyCommands.append(command)
        keyHandlers[keyId(input, flags)] = run
    }

    @objc private func hardwareKey(_ sender: UIKeyCommand) {
        guard let input = sender.input else { return }
        keyHandlers[keyId(input, sender.modifierFlags)]?()
    }

    /// The keyboard cursor: the tracked reading position (shared with VoiceOver
    /// focus and taps).
    private var keyboardRow: Int {
        guard let id = selectionByKey[currentKey],
              let index = rows.firstIndex(where: { $0.id == id }) else { return -1 }
        return index
    }

    private func withKeyboardRow(_ run: (Row) -> Void) {
        let index = keyboardRow
        guard rows.indices.contains(index) else { return }
        run(rows[index])
    }

    private func focusRow(_ index: Int) {
        guard rows.indices.contains(index) else { return }
        let indexPath = IndexPath(row: index, section: 0)
        tableView.selectRow(at: indexPath, animated: false, scrollPosition: .middle)
        cursorMoved(to: index)
        // Keep VoiceOver in step when it's running alongside the keyboard.
        UIAccessibility.post(notification: .layoutChanged,
                             argument: tableView.cellForRow(at: indexPath))
    }

    private func moveKeyboardCursor(_ delta: Int) {
        guard !rows.isEmpty else { return }
        let current = keyboardRow
        let target = current < 0 ? (delta > 0 ? 0 : rows.count - 1) : current + delta
        guard rows.indices.contains(target) else {
            state.playEarcon("boundary")
            return
        }
        focusRow(target)
    }

    /// Same bindings as the desktop apps, adapted to iPad conventions.
    private func buildHardwareKeys() {
        addKey(UIKeyCommand.inputUpArrow, [], "Previous Post", priority: true) { [weak self] in
            self?.moveKeyboardCursor(-1)
        }
        addKey(UIKeyCommand.inputDownArrow, [], "Next Post", priority: true) { [weak self] in
            self?.moveKeyboardCursor(1)
        }
        addKey(UIKeyCommand.inputLeftArrow, [], "Previous Timeline", priority: true) {
            [weak self] in
            self?.state.selectTimeline(dir: "prev")
        }
        addKey(UIKeyCommand.inputRightArrow, [], "Next Timeline", priority: true) {
            [weak self] in
            self?.state.selectTimeline(dir: "next")
        }
        addKey(UIKeyCommand.inputUpArrow, .alternate, "Jump Back by Movement Unit") {
            [weak self] in
            self?.state.moveByUnit(dir: "prev")
        }
        addKey(UIKeyCommand.inputDownArrow, .alternate, "Jump Forward by Movement Unit") {
            [weak self] in
            self?.state.moveByUnit(dir: "next")
        }
        addKey(UIKeyCommand.inputLeftArrow, .alternate, "Previous Movement Unit") {
            [weak self] in
            self?.state.cycleMovement(dir: "prev")
        }
        addKey(UIKeyCommand.inputRightArrow, .alternate, "Next Movement Unit") { [weak self] in
            self?.state.cycleMovement(dir: "next")
        }
        addKey(UIKeyCommand.inputUpArrow, .shift, "Move Timeline Up") { [weak self] in
            self?.state.reorderTimeline(dir: "up")
        }
        addKey(UIKeyCommand.inputDownArrow, .shift, "Move Timeline Down") { [weak self] in
            self?.state.reorderTimeline(dir: "down")
        }

        addKey("\r", [], "Interact", priority: true) { [weak self] in
            guard let self, self.keyboardRow >= 0 else { return }
            self.state.performAction("Enter")
        }
        addKey("\r", .shift, "Secondary Action") { [weak self] in
            guard let self, self.keyboardRow >= 0 else { return }
            self.state.performAction("SecondaryAction")
        }
        addKey(" ", [], "View Thread", priority: true) { [weak self] in
            self?.withKeyboardRow { row in self?.state.openThread(id: row.id) }
        }

        addKey("r", [], "Reply") { [weak self] in
            self?.withKeyboardRow { row in self?.state.requestCompose(mode: "reply", id: row.id) }
        }
        addKey("b", [], "Boost") { [weak self] in
            self?.withKeyboardRow { row in self?.state.toggleBoost(id: row.id) }
        }
        addKey("f", [], "Favorite") { [weak self] in
            self?.withKeyboardRow { row in self?.state.toggleFavorite(id: row.id) }
        }
        addKey("m", [], "Bookmark") { [weak self] in
            self?.withKeyboardRow { row in self?.state.toggleBookmark(id: row.id) }
        }
        addKey("q", [], "Quote") { [weak self] in
            self?.withKeyboardRow { row in self?.state.requestCompose(mode: "quote", id: row.id) }
        }
        addKey("u", [], "User Timeline") { [weak self] in
            self?.withKeyboardRow { row in self?.state.openUserTimeline(id: row.id) }
        }
        addKey("e", [], "Edit Post") { [weak self] in
            self?.withKeyboardRow { row in self?.state.requestCompose(mode: "edit", id: row.id) }
        }
        addKey("p", [], "Pin to Profile") { [weak self] in
            self?.withKeyboardRow { row in self?.state.togglePinPost(id: row.id) }
        }
        addKey("u", .command, "User Profile") { [weak self] in
            self?.withKeyboardRow { row in self?.state.openUserProfile(id: row.id) }
        }
        addKey("i", .command, "Post Info") { [weak self] in
            self?.withKeyboardRow { row in self?.state.postInfo(id: row.id) }
        }
        addKey("o", .command, "Open Link") { [weak self] in
            self?.withKeyboardRow { row in self?.state.openPostLinks(id: row.id) }
        }
        addKey("c", [.command, .shift], "Copy Post") { [weak self] in
            self?.withKeyboardRow { row in self?.state.copy(id: row.id) }
        }
        addKey("\u{8}", .command, "Delete Post") { [weak self] in
            self?.withKeyboardRow { row in
                guard let self, row.isMine else { return }
                if self.state.confirmDeletePost {
                    confirm("Delete Post", message: "Delete this post?", actionTitle: "Delete",
                            on: self) { [weak self] in self?.state.deletePost(id: row.id) }
                } else {
                    self.state.deletePost(id: row.id)
                }
            }
        }

        addKey("n", .command, "New Post") { [weak self] in
            self?.state.requestCompose(mode: "new")
        }
        addKey("r", .command, "Refresh") { [weak self] in self?.state.refresh() }
        addKey("r", [.command, .shift], "Refresh All Timelines") { [weak self] in
            self?.state.refreshAll()
        }
        addKey("z", .command, "Undo Navigation") { [weak self] in self?.state.goBack() }
        addKey("t", .command, "New Timeline") { [weak self] in self?.state.getSpawnable() }
        addKey("w", .command, "Close Timeline") { [weak self] in
            guard let self else { return }
            let index = self.state.currentIndex
            guard self.state.timelines.indices.contains(index),
                  self.state.timelines[index].dismissable else {
                self.state.playEarcon("boundary")
                return
            }
            self.state.closeTimeline()
        }
        addKey("l", .command, "Filter Timeline") { [weak self] in self?.state.getClientFilter() }
        addKey("[", .command, "Previous Account") { [weak self] in
            self?.state.selectAccount(dir: "prev")
        }
        addKey("]", .command, "Next Account") { [weak self] in
            self?.state.selectAccount(dir: "next")
        }
        for number in 1...9 {
            addKey("\(number)", .command, "Go to Timeline \(number)") { [weak self] in
                self?.state.selectTimeline(number: number)
            }
        }
    }

    /// Movement units as VoiceOver rotors (like the original FastSM iOS app):
    /// twist to a unit — Same User, Thread, a time gap, or an item count — and
    /// flick up/down to jump by it. Which units appear (and their order) comes
    /// from the movement-units setting; jumps are computed here with the same
    /// algorithm as the desktop `move` command, from per-row data the core
    /// ships with each timeline update (a rotor must answer synchronously,
    /// which rules out the async command round-trip).
    private func makeMovementRotors() -> [UIAccessibilityCustomRotor] {
        state.activeMovementUnits.compactMap { entry in
            guard let unit = MovementUnit.parse(entry.key) else { return nil }
            return UIAccessibilityCustomRotor(name: entry.label) { [weak self] predicate in
                guard let self else { return nil }
                var current = -1
                if let cell = predicate.currentItem.targetElement as? PostCell,
                   let indexPath = self.tableView.indexPath(for: cell) {
                    current = indexPath.row
                } else if let tracked = self.selectionByKey[self.currentKey],
                          let index = self.rows.firstIndex(where: { $0.id == tracked }) {
                    current = index
                }
                guard self.rows.indices.contains(current),
                      let target = unit.destination(in: self.rows, from: current,
                                                    down: predicate.searchDirection == .next)
                else { return nil } // nowhere to jump: VoiceOver plays the boundary sound
                let indexPath = IndexPath(row: target, section: 0)
                // Materialize the destination cell so we can hand it to VoiceOver.
                self.tableView.scrollToRow(at: indexPath, at: .middle, animated: false)
                self.tableView.layoutIfNeeded()
                guard let cell = self.tableView.cellForRow(at: indexPath) else { return nil }
                self.cursorMoved(to: target) // record the jump (position + Undo Navigation)
                return UIAccessibilityCustomRotorItemResult(targetElement: cell,
                                                            targetRange: nil)
            }
        }
    }

    /// The core moved the cursor (Go Back, a movement jump): scroll there and
    /// move VoiceOver focus to the row.
    private func select(id: String) {
        guard let index = rows.firstIndex(where: { $0.id == id }) else { return }
        selectionByKey[currentKey] = id
        // Visible highlight for hardware-keyboard users (movement jumps, Undo
        // Navigation land here).
        tableView.selectRow(at: IndexPath(row: index, section: 0), animated: false,
                            scrollPosition: .none)
        scrollTo(index)
        DispatchQueue.main.async { [weak self] in
            guard let self else { return }
            let cell = self.tableView.cellForRow(at: IndexPath(row: index, section: 0))
            UIAccessibility.post(notification: .layoutChanged, argument: cell)
        }
    }

    /// The reading cursor landed on a row (tap or VoiceOver focus): remember
    /// it, persist it in the core, and page in older posts near the edge.
    private func cursorMoved(to row: Int) {
        guard rows.indices.contains(row) else { return }
        let id = rows[row].id
        selectionByKey[currentKey] = id
        state.noteSelection(id: id)
        maybeLoadOlder(row: row)
    }

    /// Load older posts as the cursor nears the bottom (or fill a tracked gap
    /// near it). Front-end-driven, same logic as the Mac and Windows apps.
    private func maybeLoadOlder(row: Int) {
        guard !loadPending else { return }
        let count = rows.count
        for d in 0...5 {
            for g in [row + d, row - d] where rows.indices.contains(g) {
                if rows[g].gapAfter {
                    loadPending = true
                    state.loadGap(id: rows[g].id)
                    return
                }
            }
        }
        let nearEdge = state.currentReversed ? (row <= 9) : (row >= count - 10)
        if count > 0, nearEdge, state.autoLoadOlder {
            loadPending = true
            state.loadOlder(automatic: true)
        }
    }

    // MARK: Bar actions

    @objc private func composeTapped() { state.requestCompose(mode: "new") }
    @objc private func refreshTapped() { state.refresh() }
    @objc private func pullToRefresh() { state.refresh() }

    private func moreMenu() -> UIMenu {
        UIMenu(children: [UIDeferredMenuElement.uncached { [weak self] completion in
            guard let self else { completion([]); return }
            var items: [UIMenuElement] = [
                UIAction(title: "Settings…",
                         image: UIImage(systemName: "gearshape")) { [weak self] _ in
                    guard let self else { return }
                    self.navigationController?.pushViewController(
                        SettingsViewController(state: self.state), animated: true)
                },
                UIAction(title: "New Timeline…",
                         image: UIImage(systemName: "plus.rectangle.on.rectangle")) {
                    [weak self] _ in
                    self?.state.getSpawnable()
                },
                UIAction(title: "Refresh All Timelines",
                         image: UIImage(systemName: "arrow.clockwise")) { [weak self] _ in
                    self?.state.refreshAll()
                },
                UIAction(title: "Undo Navigation",
                         image: UIImage(systemName: "arrow.uturn.backward")) { [weak self] _ in
                    self?.state.goBack()
                },
            ]
            var timelineItems: [UIMenuElement] = [
                UIAction(title: "Filter Timeline…",
                         image: UIImage(systemName: "line.3.horizontal.decrease.circle")) {
                    [weak self] _ in
                    self?.state.getClientFilter()
                },
                UIAction(title: "Clear Timeline",
                         image: UIImage(systemName: "trash")) { [weak self] _ in
                    guard let self else { return }
                    if self.state.confirmClearTimeline {
                        confirm("Clear Timeline",
                                message: "Remove all loaded posts from this timeline?",
                                actionTitle: "Clear", on: self) { [weak self] in
                            self?.state.clearTimeline()
                        }
                    } else {
                        self.state.clearTimeline()
                    }
                },
            ]
            let index = self.state.currentIndex
            if self.state.timelines.indices.contains(index),
               self.state.timelines[index].dismissable {
                timelineItems.append(UIAction(title: "Close Timeline",
                                              image: UIImage(systemName: "xmark.rectangle"),
                                              attributes: .destructive) { [weak self] _ in
                    self?.state.closeTimeline()
                })
            }
            items.append(UIMenu(options: .displayInline, children: timelineItems))
            var accountItems: [UIMenuElement] = [
                UIAction(title: "Account Settings…",
                         image: UIImage(systemName: "person.crop.circle.badge.checkmark")) {
                    [weak self] _ in
                    self?.state.getAccountSettings()
                },
                UIAction(title: "Add Account…",
                         image: UIImage(systemName: "person.badge.plus")) { [weak self] _ in
                    self?.onAddAccount?()
                },
            ]
            if self.state.accounts.count > 1 {
                accountItems.append(UIAction(title: "Next Account") { [weak self] _ in
                    self?.state.selectAccount(dir: "next")
                })
                accountItems.append(UIAction(title: "Previous Account") { [weak self] _ in
                    self?.state.selectAccount(dir: "prev")
                })
            }
            if let handle = self.state.currentAccountHandle,
               let key = self.state.accounts.first(where: {
                   $0.key == self.state.selectedAccountKey })?.key {
                accountItems.append(UIAction(title: "Remove \(handle)…",
                                             attributes: .destructive) { [weak self] _ in
                    guard let self else { return }
                    confirm("Remove Account",
                            message: "Remove \(handle) from FastSM?",
                            actionTitle: "Remove", on: self) { [weak self] in
                        self?.state.removeAccount(key: key)
                    }
                })
            }
            let accountTitle = self.state.currentAccountHandle ?? "Accounts"
            items.append(UIMenu(title: accountTitle, options: .displayInline,
                                children: accountItems))
            items.append(UIMenu(title: "Manage", image: UIImage(systemName: "folder"),
                                children: [
                UIAction(title: "Edit Profile…") { [weak self] _ in
                    self?.state.openProfileEditor()
                },
                UIAction(title: "View My Followers") { [weak self] _ in
                    self?.state.spawnTimeline(kind: "my_followers")
                },
                UIAction(title: "View My Following") { [weak self] _ in
                    self?.state.spawnTimeline(kind: "my_following")
                },
                UIAction(title: "Manage Lists…") { [weak self] _ in
                    self?.state.listLists()
                },
                UIAction(title: "Followed Hashtags…") { [weak self] _ in
                    self?.state.listFollowedHashtags()
                },
                UIAction(title: "Trending Hashtags…") { [weak self] _ in
                    self?.state.listTrendingHashtags()
                },
                UIAction(title: "Server Filters…") { [weak self] _ in
                    self?.state.listServerFilters()
                },
                UIAction(title: "User Aliases…") { [weak self] _ in
                    self?.state.listAliases()
                },
                UIAction(title: "User Analysis…") { [weak self] _ in
                    guard let self else { return }
                    self.push(UserAnalysisViewController(state: self.state))
                },
            ]))
            completion(items)
        }])
    }

    // MARK: Compose

    private func presentCompose(_ context: ComposeContext) {
        let compose = ComposeViewController(state: state, context: context)
        composeVC = compose
        let nav = UINavigationController(rootViewController: compose)
        nav.modalPresentationStyle = .formSheet
        present(nav, animated: true)
    }

    // MARK: Post actions

    /// A settings change arrived (echoed on update_settings): re-apply the tab
    /// bar position, and refresh the visible cells' VoiceOver actions in place
    /// (the post-action list may have been reordered/toggled) without a reload
    /// that would disturb the reading position.
    private func settingsChanged() {
        applyTabBarPosition()
        for cell in tableView.visibleCells {
            guard let postCell = cell as? PostCell,
                  let indexPath = tableView.indexPath(for: cell),
                  rows.indices.contains(indexPath.row) else { continue }
            postCell.accessibilityCustomActions = accessibilityActions(for: rows[indexPath.row])
        }
    }

    private func accessibilityActions(for row: Row) -> [UIAccessibilityCustomAction] {
        actions(for: row).map { action in
            UIAccessibilityCustomAction(name: action.title) { _ in action.run(); return true }
        }
    }

    /// The VoiceOver actions (and long-press menu) for a post: the user's
    /// configured post-action list, in order, keeping only the ones that apply
    /// to this particular post. "expand_links" turns into one action per link.
    private func actions(for row: Row) -> [(title: String, run: () -> Void)] {
        state.enabledPostActions.flatMap { key -> [(title: String, run: () -> Void)] in
            if key == "expand_links" {
                return row.links.map { link in
                    (link.title, { if let url = URL(string: link.url) {
                        UIApplication.shared.open(url)
                    } })
                }
            }
            return postAction(key, row).map { [$0] } ?? []
        }
    }

    /// One catalog key mapped to its live action on `row` (dynamic title +
    /// effect), or nil when it doesn't apply (media on a text post, delete on
    /// someone else's post). Keys match post_action_catalog() in the core.
    private func postAction(_ key: String, _ row: Row) -> (title: String, run: () -> Void)? {
        let id = row.id
        switch key {
        case "reply":
            return ("Reply", { [weak self] in self?.state.requestCompose(mode: "reply", id: id) })
        case "quote":
            return ("Quote", { [weak self] in self?.state.requestCompose(mode: "quote", id: id) })
        case "boost":
            return (row.boosted ? "Remove Boost" : "Boost",
                    { [weak self] in self?.state.toggleBoost(id: id) })
        case "favorite":
            return (row.favorited ? "Remove Favorite" : "Favorite",
                    { [weak self] in self?.state.toggleFavorite(id: id) })
        case "bookmark":
            return ("Bookmark", { [weak self] in self?.state.toggleBookmark(id: id) })
        case "thread":
            return ("View Thread", { [weak self] in self?.state.openThread(id: id) })
        case "post_info":
            return ("Post Info", { [weak self] in self?.state.postInfo(id: id) })
        case "play_media":
            return row.hasMedia
                ? ("View Media", { [weak self] in self?.state.playMedia(id: id) }) : nil
        case "links":
            return ("Open Links", { [weak self] in self?.state.openPostLinks(id: id) })
        case "browser":
            return ("Open in Browser", { [weak self] in self?.state.openStatus(id: id) })
        case "copy":
            return ("Copy", { [weak self] in self?.state.copy(id: id) })
        case "user_profile":
            return ("User Profile", { [weak self] in self?.state.openUserProfile(id: id) })
        case "user_timeline":
            return ("User Timeline", { [weak self] in self?.state.openUserTimeline(id: id) })
        case "followers":
            return ("Followers", { [weak self] in self?.state.openFollowers(id: id) })
        case "following":
            return ("Following", { [weak self] in self?.state.openFollowing(id: id) })
        case "mute_conversation":
            return ("Mute Conversation",
                    { [weak self] in self?.state.toggleMuteConversation(id: id) })
        case "favorited_by":
            return row.favoritesCount > 0
                ? ("See Who Favorited", { [weak self] in self?.state.openFavoritedBy(id: id) })
                : nil
        case "reblogged_by":
            return row.boostsCount > 0
                ? ("See Who Boosted", { [weak self] in self?.state.openRebloggedBy(id: id) })
                : nil
        case "alias":
            return ("Add or Edit Alias", { [weak self] in self?.state.beginAlias(id: id) })
        case "follow_hashtag":
            return ("Follow Hashtag", { [weak self] in self?.state.followHashtagPrompt(id: id) })
        case "speak_user":
            return ("Speak User", { [weak self] in self?.state.speakUser(id: id) })
        case "speak_reply":
            return row.isReply
                ? ("Speak Referenced Reply", { [weak self] in self?.state.speakReply(id: id) })
                : nil
        case "jump_reply":
            return row.isReply
                ? ("Jump to Referenced Reply", { [weak self] in self?.state.jumpToReply(id: id) })
                : nil
        case "edit":
            return row.isMine
                ? ("Edit Post", { [weak self] in self?.state.requestCompose(mode: "edit", id: id) })
                : nil
        case "pin_post":
            return row.isMine
                ? ("Pin to Profile", { [weak self] in self?.state.togglePinPost(id: id) })
                : nil
        case "report":
            return ("Report", { [weak self] in
                guard let self else { return }
                let report = ReportViewController(state: self.state, id: id, accountId: nil,
                                                  acct: "", remote: false)
                self.push(report)
            })
        case "delete":
            guard row.isMine else { return nil }
            return ("Delete Post", { [weak self] in
                guard let self else { return }
                if self.state.confirmDeletePost {
                    confirm("Delete Post", message: "Delete this post?",
                            actionTitle: "Delete", on: self) { [weak self] in
                        self?.state.deletePost(id: id)
                    }
                } else {
                    self.state.deletePost(id: id)
                }
            })
        default:
            return nil
        }
    }

    /// The controller alerts/sheets should be presented on (a pushed detail
    /// screen may be visible instead of this one).
    private var topPresenter: UIViewController {
        navigationController?.topViewController ?? self
    }

    private func push(_ viewController: UIViewController) {
        navigationController?.pushViewController(viewController, animated: true)
    }

    /// Disambiguation: pick which referenced user the action applies to.
    private func showUserPicker(_ picker: UserPicker) {
        let sheet = UIAlertController(title: "Choose a User", message: nil,
                                      preferredStyle: .actionSheet)
        for user in picker.users {
            sheet.addAction(UIAlertAction(title: "@\(user.acct)", style: .default) {
                [weak self] _ in
                guard let self else { return }
                switch picker.purpose {
                case "timeline": self.state.openUserTimeline(accountId: user.id, acct: user.acct)
                case "profile": self.state.openUserProfile(accountId: user.id, acct: user.acct)
                case "follow_toggle": self.state.followToggle(accountId: user.id, acct: user.acct)
                case "alias": self.state.beginAliasAccount(id: picker.id, accountId: user.id)
                default: break
                }
            })
        }
        sheet.addAction(UIAlertAction(title: "Cancel", style: .cancel))
        sheet.popoverPresentationController?.sourceView = view
        sheet.popoverPresentationController?.sourceRect = view.bounds
        topPresenter.present(sheet, animated: true)
    }

    /// A post with several attachments: choose which one to view.
    private func showMediaPicker(_ picker: MediaPicker) {
        guard !picker.items.isEmpty else { return }
        let sheet = UIAlertController(title: "View Media", message: nil,
                                      preferredStyle: .actionSheet)
        for item in picker.items {
            sheet.addAction(UIAlertAction(title: item.title, style: .default) { [weak self] _ in
                self?.state.playMedia(url: item.url, kind: item.kind, title: item.title)
            })
        }
        sheet.addAction(UIAlertAction(title: "Cancel", style: .cancel))
        sheet.popoverPresentationController?.sourceView = view
        sheet.popoverPresentationController?.sourceRect = view.bounds
        topPresenter.present(sheet, animated: true)
    }

    /// Follow a hashtag from a post: offer the post's hashtags, or type one.
    private func showHashtagPrompt(_ tags: [String]) {
        let sheet = UIAlertController(title: "Follow Hashtag", message: nil,
                                      preferredStyle: .actionSheet)
        for tag in tags {
            sheet.addAction(UIAlertAction(title: "#\(tag)", style: .default) { [weak self] _ in
                self?.state.followHashtag(name: tag)
            })
        }
        sheet.addAction(UIAlertAction(title: "Other…", style: .default) { [weak self] _ in
            guard let self else { return }
            let alert = UIAlertController(title: "Follow Hashtag",
                                          message: "Enter a hashtag (without the #).",
                                          preferredStyle: .alert)
            alert.addTextField { $0.autocapitalizationType = .none }
            alert.addAction(UIAlertAction(title: "Cancel", style: .cancel))
            alert.addAction(UIAlertAction(title: "Follow", style: .default) { [weak alert] _ in
                let name = (alert?.textFields?.first?.text ?? "")
                    .trimmingCharacters(in: CharacterSet(charactersIn: " #"))
                guard !name.isEmpty else { return }
                self.state.followHashtag(name: name)
            })
            self.topPresenter.present(alert, animated: true)
        })
        sheet.addAction(UIAlertAction(title: "Cancel", style: .cancel))
        sheet.popoverPresentationController?.sourceView = view
        sheet.popoverPresentationController?.sourceRect = view.bounds
        topPresenter.present(sheet, animated: true)
    }

    /// Add or edit a user's alias (a custom name spoken instead of their own).
    private func showAliasPrompt(_ prompt: AliasPrompt) {
        let alert = UIAlertController(title: "Alias for @\(prompt.handle)",
                                      message: "Leave empty to remove the alias.",
                                      preferredStyle: .alert)
        alert.addTextField { field in
            field.text = prompt.current
            field.placeholder = "Alias"
        }
        alert.addAction(UIAlertAction(title: "Cancel", style: .cancel))
        alert.addAction(UIAlertAction(title: "Save", style: .default) { [weak self, weak alert] _ in
            let value = (alert?.textFields?.first?.text ?? "")
                .trimmingCharacters(in: .whitespaces)
            if value.isEmpty {
                self?.state.clearAlias(key: prompt.key, handle: prompt.handle)
            } else {
                self?.state.setAlias(key: prompt.key, handle: prompt.handle, alias: value)
            }
        })
        topPresenter.present(alert, animated: true)
    }

    /// Per-account settings (the soundpack this account's timelines play):
    /// reuse the settings option-picker pushed onto the nav stack.
    private func showAccountSettings(_ settings: AccountSettings) {
        let packs = settings.soundpacks.isEmpty ? ["Default"] : settings.soundpacks
        let title = settings.acct.isEmpty ? "Account Soundpack" : "Soundpack for @\(settings.acct)"
        let picker = OptionPickerViewController(
            title: title, options: packs,
            selected: packs.firstIndex(of: settings.soundpack) ?? 0) { [weak self] picked in
            self?.state.setAccountSettings(soundpack: packs[picked])
        }
        navigationController?.pushViewController(picker, animated: true)
    }

    private func showURLPicker(_ picker: URLPicker) {
        guard !picker.links.isEmpty else { return }
        let sheet = UIAlertController(title: "Open Link", message: nil,
                                      preferredStyle: .actionSheet)
        for link in picker.links {
            sheet.addAction(UIAlertAction(title: link.title, style: .default) { _ in
                if let url = URL(string: link.url) { UIApplication.shared.open(url) }
            })
        }
        sheet.addAction(UIAlertAction(title: "Cancel", style: .cancel))
        sheet.popoverPresentationController?.sourceView = view
        sheet.popoverPresentationController?.sourceRect = view.bounds
        present(sheet, animated: true)
    }

    // MARK: VoiceOver app-level gestures

    // Magic tap (two-finger double-tap) performs the post's secondary action;
    // it's handled at the window level (MagicTapWindow) so it fires from
    // anywhere on screen, not just when a post row has focus.

    /// Escape (two-finger Z): close the focused timeline, when it's one that
    /// can be closed (a thread, a spawned user/hashtag timeline, …); a
    /// boundary earcon signals a permanent timeline. Undo Navigation (in the
    /// More menu) separately re-opens where you navigated from.
    override func accessibilityPerformEscape() -> Bool {
        let index = state.currentIndex
        if state.timelines.indices.contains(index), state.timelines[index].dismissable {
            state.closeTimeline()
        } else {
            state.playEarcon("boundary")
        }
        return true
    }
}

// MARK: - Table data source / delegate

extension MainViewController: UITableViewDataSource, UITableViewDelegate {
    func tableView(_ tableView: UITableView, numberOfRowsInSection section: Int) -> Int {
        rows.count
    }

    func tableView(_ tableView: UITableView,
                   cellForRowAt indexPath: IndexPath) -> UITableViewCell {
        let cell = tableView.dequeueReusableCell(withIdentifier: PostCell.reuseIdentifier,
                                                 for: indexPath)
        guard let postCell = cell as? PostCell, rows.indices.contains(indexPath.row) else {
            return cell
        }
        let row = rows[indexPath.row]
        postCell.configure(text: row.text)
        // Resolve the cell's row at focus time — incremental inserts/removes
        // shift indexes under existing cells, so a captured index goes stale.
        postCell.onFocused = { [weak self, weak postCell] in
            guard let self, let cell = postCell,
                  let indexPath = self.tableView.indexPath(for: cell) else { return }
            self.focusedPostCell = cell
            self.cursorMoved(to: indexPath.row)
        }
        // Clear only if this exact cell is still the tracked one — focus often
        // moves to the next cell (its become-focus fires first) before this
        // one's lose-focus, and we must not stomp the newer focus.
        postCell.onUnfocused = { [weak self, weak postCell] in
            if self?.focusedPostCell === postCell { self?.focusedPostCell = nil }
        }
        postCell.accessibilityCustomActions = accessibilityActions(for: row)
        return postCell
    }

    /// Tap / VoiceOver double-tap: the configurable "interact" action, resolved
    /// by the core from the Behavior settings against the selected row.
    func tableView(_ tableView: UITableView, didSelectRowAt indexPath: IndexPath) {
        tableView.deselectRow(at: indexPath, animated: false)
        guard rows.indices.contains(indexPath.row) else { return }
        cursorMoved(to: indexPath.row)
        state.performAction("Enter")
    }

    /// Long-press context menu for sighted users — same verbs as the VoiceOver
    /// custom actions.
    func tableView(_ tableView: UITableView,
                   contextMenuConfigurationForRowAt indexPath: IndexPath,
                   point: CGPoint) -> UIContextMenuConfiguration? {
        guard rows.indices.contains(indexPath.row) else { return nil }
        let row = rows[indexPath.row]
        return UIContextMenuConfiguration(identifier: nil, previewProvider: nil) {
            [weak self] _ in
            guard let self else { return nil }
            let children = self.actions(for: row).map { action in
                UIAction(title: action.title) { _ in action.run() }
            }
            return UIMenu(children: children)
        }
    }

    /// Sighted scrolling reaches the bottom: page in older posts too.
    func tableView(_ tableView: UITableView, willDisplay cell: UITableViewCell,
                   forRowAt indexPath: IndexPath) {
        maybeLoadOlder(row: indexPath.row)
    }
}

// MARK: - Cell

/// One post row. The whole cell is a single VoiceOver element whose label is
/// the core-composed row text; focusing it moves the reading cursor.
final class PostCell: UITableViewCell {
    static let reuseIdentifier = "PostCell"
    var onFocused: (() -> Void)?
    var onUnfocused: (() -> Void)?

    func configure(text: String) {
        var content = defaultContentConfiguration()
        content.text = text
        content.textProperties.numberOfLines = 6
        content.textProperties.font = .preferredFont(forTextStyle: .body)
        contentConfiguration = content
        isAccessibilityElement = true
        accessibilityLabel = text
    }

    override func accessibilityElementDidBecomeFocused() {
        super.accessibilityElementDidBecomeFocused()
        onFocused?()
    }

    override func accessibilityElementDidLoseFocus() {
        super.accessibilityElementDidLoseFocus()
        onUnfocused?()
    }
}
