//
//  MainWindowController.swift
//
//  Owns the main window: a split view with the timelines pane (left) and the
//  posts pane (right). Tab moves focus between the two; left/right in the posts
//  pane switch timelines. Toolbar: New Post / Refresh / Add Account.
//
//  It is also the hub for UI-structure core events and the forwarder for post
//  actions, so menu commands work no matter which pane has focus (the window
//  controller is always in the responder chain; the posts view controller is
//  not when the timelines pane is focused).
//

import AppKit

@MainActor
final class MainWindowController: NSWindowController, NSToolbarDelegate {
    private let state: AppState
    private let timelinesViewController: TimelinesViewController
    private let postsViewController: TimelineViewController
    private var hasFocusedInitially = false
    private var promptedForAccount = false

    private var addAccountController: AddAccountWindowController?
    private var composeController: ComposeWindowController?
    private var newTimelineController: NewTimelineWindowController?
    private var userAnalysisController: UserAnalysisWindowController?
    private var detailController: NSWindowController?
    private var mediaControllers: [NSWindowController] = []
    private var hashtagsController: HashtagsWindowController?
    private var trendingHashtagsController: TrendingHashtagsWindowController?
    private var listsController: ListsWindowController?
    private var aliasesController: AliasesWindowController?
    private var serverFiltersController: ServerFiltersWindowController?

    init(state: AppState) {
        self.state = state
        self.timelinesViewController = TimelinesViewController(state: state)
        self.postsViewController = TimelineViewController(state: state)

        let splitViewController = NSSplitViewController()
        let timelinesItem = NSSplitViewItem(sidebarWithViewController: timelinesViewController)
        timelinesItem.minimumThickness = 180
        timelinesItem.maximumThickness = 340
        timelinesItem.canCollapse = false
        let postsItem = NSSplitViewItem(viewController: postsViewController)
        splitViewController.addSplitViewItem(timelinesItem)
        splitViewController.addSplitViewItem(postsItem)

        let window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 920, height: 720),
            styleMask: [.titled, .closable, .miniaturizable, .resizable, .fullSizeContentView],
            backing: .buffered,
            defer: false
        )
        window.title = "FastSMRW"
        window.contentViewController = splitViewController
        // Never let a pane collapse to near-zero — a zero-height posts pane is
        // invisible to VoiceOver's VO-key navigation.
        window.contentMinSize = NSSize(width: 680, height: 440)
        window.setFrameAutosaveName("FastSMRWMainWindow")
        if window.frame.height < 440 || window.frame.width < 680 {
            window.setContentSize(NSSize(width: 920, height: 720))
        }
        super.init(window: window)

        // Tab cycles focus between the two panes.
        timelinesViewController.onMoveToPosts = { [weak self] in self?.postsViewController.focusTable() }
        postsViewController.onMoveToTimelines = { [weak self] in self?.timelinesViewController.focusTable() }

        wireEvents()

        let toolbar = NSToolbar(identifier: "MainToolbar")
        toolbar.delegate = self
        toolbar.displayMode = .iconAndLabel
        window.toolbar = toolbar
        window.center()
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    private func wireEvents() {
        let state = self.state // captured by the event closures below
        state.onAccountsChanged = { [weak self] in
            guard let self else { return }
            self.updateSubtitle()
            if self.state.accounts.isEmpty, !self.promptedForAccount {
                self.promptedForAccount = true
                self.presentAddAccount()
            }
        }
        state.onTimelinesChanged = { [weak self] in
            guard let self else { return }
            self.timelinesViewController.reload()
            self.postsViewController.reload()
            self.updateSubtitle()
            if !self.hasFocusedInitially, !self.state.timelines.isEmpty {
                self.hasFocusedInitially = true
                self.postsViewController.focusTable()
            }
        }
        state.onTimelineRows = { [weak self] index in
            guard let self, index == self.state.currentIndex else { return }
            self.postsViewController.reload()
        }
        state.onSelectRow = { [weak self] id in
            self?.postsViewController.select(id: id)
        }
        state.onComposeContext = { [weak self] context in
            self?.presentCompose(context)
        }
        state.onSpawnable = { [weak self] spawnables in
            self?.presentNewTimeline(spawnables)
        }
        state.onPostInfo = { [weak self] info in
            self?.presentDetail(PostInfoWindowController(state: state, info: info))
        }
        state.onUserProfile = { [weak self] profile in
            self?.presentDetail(UserProfileWindowController(state: state, profile: profile))
        }
        state.onUserPicker = { [weak self] picker in
            guard let self, let window = self.window, self.detailController == nil else { return }
            let controller = UserPickerWindowController(state: state, picker: picker)
            self.detailController = controller
            controller.beginSheet(for: window) { [weak self] in self?.detailController = nil }
        }
        state.onClientFilter = { [weak self] filter in
            guard let self, let window = self.window, self.detailController == nil else { return }
            let controller = ClientFilterWindowController(state: state, filter: filter)
            self.detailController = controller
            controller.beginSheet(for: window) { [weak self] in
                self?.detailController = nil
                self?.postsViewController.focusTable()
            }
        }
        state.onMediaOpen = { [weak self] media in
            guard let self, let controller = MediaPresenter.open(media, from: self.window) else { return }
            self.mediaControllers.removeAll { $0.window?.isVisible != true }
            self.mediaControllers.append(controller)
        }
        state.onMediaPicker = { [weak self] picker in
            guard let self, let window = self.window, self.detailController == nil else { return }
            let controller = MediaPickerWindowController(state: state, picker: picker)
            self.detailController = controller
            controller.beginSheet(for: window) { [weak self] in self?.detailController = nil }
        }
        state.onURLPicker = { [weak self] picker in
            guard let self, let window = self.window, self.detailController == nil else { return }
            let controller = URLPickerWindowController(picker: picker)
            self.detailController = controller
            controller.beginSheet(for: window) { [weak self] in self?.detailController = nil }
        }
        state.onHashtagPrompt = { [weak self] tags in
            guard let self, let window = self.window, window.attachedSheet == nil else { return }
            let controller = FollowHashtagPromptWindowController(state: state, tags: tags)
            self.detailController = controller
            controller.beginSheet(for: window) { [weak self] in self?.detailController = nil }
        }
        state.onFollowedHashtags = { [weak self] hashtags in
            guard let self, let window = self.window else { return }
            if let open = self.hashtagsController {
                open.update(hashtags)
            } else if hashtags.supported, window.attachedSheet == nil {
                let controller = HashtagsWindowController(state: state, hashtags: hashtags)
                self.hashtagsController = controller
                controller.beginSheet(for: window) { [weak self] in self?.hashtagsController = nil }
            } else if !hashtags.supported {
                ErrorAlert.present("Followed hashtags are only available for Mastodon accounts.",
                                   in: window)
            }
        }
        state.onTrendingHashtags = { [weak self] hashtags in
            guard let self, let window = self.window else { return }
            if let open = self.trendingHashtagsController {
                open.update(hashtags)
            } else if hashtags.supported, window.attachedSheet == nil {
                let controller = TrendingHashtagsWindowController(state: state, hashtags: hashtags)
                self.trendingHashtagsController = controller
                controller.beginSheet(for: window) { [weak self] in
                    self?.trendingHashtagsController = nil
                }
            } else if !hashtags.supported {
                ErrorAlert.present("Trending hashtags are only available for Mastodon accounts.",
                                   in: window)
            }
        }
        state.onAliasPrompt = { [weak self] prompt in
            guard let self, let window = self.window else { return }
            AliasPromptSheet.run(handle: prompt.handle, current: prompt.current, in: window) { value in
                if value.isEmpty {
                    state.clearAlias(key: prompt.key, handle: prompt.handle)
                } else {
                    state.setAlias(key: prompt.key, handle: prompt.handle, alias: value)
                }
            }
        }
        state.onAliasesList = { [weak self] list in
            guard let self, let window = self.window else { return }
            if let open = self.aliasesController {
                open.update(list)
            } else if window.attachedSheet == nil {
                let controller = AliasesWindowController(state: state, list: list)
                self.aliasesController = controller
                controller.beginSheet(for: window) { [weak self] in self?.aliasesController = nil }
            }
        }
        state.onLists = { [weak self] lists in
            guard let self, let window = self.window else { return }
            if let open = self.listsController {
                open.update(lists)
            } else if lists.supported, window.attachedSheet == nil {
                let controller = ListsWindowController(state: state, lists: lists)
                self.listsController = controller
                controller.beginSheet(for: window) { [weak self] in self?.listsController = nil }
            } else if !lists.supported {
                ErrorAlert.present("Lists are only available for Mastodon accounts.", in: window)
            }
        }
        state.onUserLists = { [weak self] userLists in
            guard let self, let window = self.window, self.detailController == nil else { return }
            if !userLists.supported {
                ErrorAlert.present("Lists are only available for Mastodon accounts.", in: window)
                return
            }
            let controller = AddToListWindowController(state: state, userLists: userLists)
            self.detailController = controller
            controller.beginSheet(for: window) { [weak self] in self?.detailController = nil }
        }
        state.onServerFilters = { [weak self] filters in
            guard let self, let window = self.window else { return }
            if let open = self.serverFiltersController {
                open.update(filters)
            } else if filters.supported, window.attachedSheet == nil {
                let controller = ServerFiltersWindowController(state: state, filters: filters)
                self.serverFiltersController = controller
                controller.beginSheet(for: window) { [weak self] in self?.serverFiltersController = nil }
            } else if !filters.supported {
                ErrorAlert.present("Server-side filters are only available for Mastodon accounts.",
                                   in: window)
            }
        }
        state.onAuthResult = { [weak self] result in
            guard let self else { return }
            if result.ok {
                self.addAccountController?.dismiss()
                self.addAccountController = nil
            } else {
                self.addAccountController?.reset() // re-enable the form for a retry
                ErrorAlert.present("Could not add the account.", detail: result.error,
                                   in: self.addAccountController?.window ?? self.window)
            }
        }
    }

    private func updateSubtitle() {
        let parts = [state.currentAccountHandle, state.currentTimelineTitle]
        window?.subtitle = parts.compactMap { $0 }.joined(separator: " — ")
    }

    // MARK: Action forwarders (work regardless of focused pane)

    @objc func composePost(_ sender: Any?) { postsViewController.composePost(sender) }
    @objc func refreshTimeline(_ sender: Any?) { postsViewController.refreshTimeline(sender) }
    @objc func replyToSelection(_ sender: Any?) { postsViewController.replyToSelection(sender) }
    @objc func boostSelection(_ sender: Any?) { postsViewController.boostSelection(sender) }
    @objc func favoriteSelection(_ sender: Any?) { postsViewController.favoriteSelection(sender) }
    @objc func quoteSelection(_ sender: Any?) { postsViewController.quoteSelection(sender) }
    @objc func openSelectionInBrowser(_ sender: Any?) {
        postsViewController.openSelectionInBrowser(sender)
    }
    @objc func playMediaForSelection(_ sender: Any?) {
        postsViewController.playMediaForSelection(sender)
    }
    @objc func openLinksForSelection(_ sender: Any?) {
        postsViewController.openLinksForSelection(sender)
    }

    @objc func togglePin(_ sender: Any?) { state.togglePin() }
    @objc func toggleMute(_ sender: Any?) { state.toggleMute() }
    @objc func moveTimelineUp(_ sender: Any?) { state.reorderTimeline(dir: "up") }
    @objc func moveTimelineDown(_ sender: Any?) { state.reorderTimeline(dir: "down") }
    @objc func newTimeline(_ sender: Any?) { state.getSpawnable() }
    @objc func closeTimeline(_ sender: Any?) {
        state.closeTimeline()
        postsViewController.focusTable()
    }
    @objc func clearTimeline(_ sender: Any?) {
        confirmClear("Clear this timeline?",
                     detail: "This removes the loaded posts and its cache.") {
            [weak self] in self?.state.clearTimeline()
        }
    }
    @objc func clearAllTimelines(_ sender: Any?) {
        confirmClear("Clear all timelines?",
                     detail: "This removes the loaded posts and caches for every open timeline.") {
            [weak self] in self?.state.clearAllTimelines()
        }
    }

    /// Run `action`, first asking to confirm unless the user disabled the prompt.
    private func confirmClear(_ message: String, detail: String, _ action: @escaping () -> Void) {
        guard state.confirmClearTimeline, let window else { action(); return }
        let alert = NSAlert()
        alert.messageText = message
        alert.informativeText = detail
        alert.addButton(withTitle: "Clear")
        alert.addButton(withTitle: "Cancel")
        alert.beginSheetModal(for: window) { r in
            if r == .alertFirstButtonReturn { action() }
        }
    }
    @objc func refreshAll(_ sender: Any?) { state.refreshAll() }
    @objc func goBack(_ sender: Any?) { state.goBack() }
    @objc func filterTimeline(_ sender: Any?) { state.getClientFilter() }
    @objc func removeCurrentAccount(_ sender: Any?) {
        guard let window, !state.selectedAccountKey.isEmpty else { return }
        let handle = state.currentAccountHandle ?? "this account"
        let alert = NSAlert()
        alert.messageText = "Remove \(handle)?"
        alert.informativeText = "Its timelines and cached posts will be removed from this app."
        alert.addButton(withTitle: "Remove")
        alert.addButton(withTitle: "Cancel")
        let key = state.selectedAccountKey
        alert.beginSheetModal(for: window) { [weak self] r in
            if r == .alertFirstButtonReturn { self?.state.removeAccount(key: key) }
        }
    }
    @objc func showPostInfo(_ sender: Any?) { postsViewController.showPostInfo(sender) }
    @objc func editSelection(_ sender: Any?) { postsViewController.editSelection(sender) }
    @objc func deleteSelection(_ sender: Any?) { postsViewController.deleteSelection(sender) }
    @objc func pinPostSelection(_ sender: Any?) { postsViewController.pinPostSelection(sender) }
    @objc func muteConversationSelection(_ sender: Any?) { postsViewController.muteConversationSelection(sender) }
    @objc func speakUserForSelection(_ sender: Any?) { postsViewController.speakUserForSelection(sender) }
    @objc func speakReplyForSelection(_ sender: Any?) { postsViewController.speakReplyForSelection(sender) }
    @objc func viewThread(_ sender: Any?) { postsViewController.viewThread(sender) }
    @objc func openUserTimeline(_ sender: Any?) { postsViewController.openUserTimeline(sender) }
    @objc func openUserProfile(_ sender: Any?) { postsViewController.openUserProfile(sender) }
    @objc func addAlias(_ sender: Any?) { postsViewController.addAlias(sender) }
    @objc func manageAliases(_ sender: Any?) { state.listAliases() }
    @objc func userAnalysis(_ sender: Any?) { presentUserAnalysis() }
    @objc func openFollowers(_ sender: Any?) { postsViewController.openFollowers(sender) }
    @objc func openFollowing(_ sender: Any?) { postsViewController.openFollowing(sender) }
    @objc func followHashtag(_ sender: Any?) { postsViewController.followHashtagForSelection(sender) }
    @objc func manageHashtags(_ sender: Any?) { state.listFollowedHashtags() }
    @objc func trendingHashtags(_ sender: Any?) { state.listTrendingHashtags() }
    @objc func manageLists(_ sender: Any?) { state.listLists() }
    @objc func manageServerFilters(_ sender: Any?) { state.listServerFilters() }

    @objc func selectTimelineNumber(_ sender: NSMenuItem) {
        state.selectTimeline(number: sender.tag)
        postsViewController.focusTable()
    }
    @objc func previousAccount(_ sender: Any?) {
        state.selectAccount(dir: "prev")
        postsViewController.focusTable()
    }
    @objc func nextAccount(_ sender: Any?) {
        state.selectAccount(dir: "next")
        postsViewController.focusTable()
    }

    // MARK: Sheets

    func presentAddAccount() {
        guard let window else { return }
        let controller = AddAccountWindowController(state: state)
        addAccountController = controller
        controller.beginSheet(for: window) { [weak self] in
            self?.addAccountController = nil
        }
    }

    private func presentDetail(_ controller: DetailSheetController) {
        guard let window, detailController == nil else { return }
        detailController = controller
        controller.beginSheet(for: window) { [weak self] in
            self?.detailController = nil
            self?.postsViewController.focusTable()
        }
    }

    private func presentNewTimeline(_ spawnables: [Spawnable]) {
        guard let window, newTimelineController == nil else { return }
        let controller = NewTimelineWindowController(state: state, spawnables: spawnables)
        newTimelineController = controller
        controller.beginSheet(for: window) { [weak self] in
            self?.newTimelineController = nil
            self?.postsViewController.focusTable()
        }
    }

    private func presentUserAnalysis() {
        guard let window, userAnalysisController == nil else { return }
        let controller = UserAnalysisWindowController(state: state)
        userAnalysisController = controller
        controller.beginSheet(for: window) { [weak self] in
            self?.userAnalysisController = nil
            self?.postsViewController.focusTable()
        }
    }

    private func presentCompose(_ context: ComposeContext) {
        guard let window else { return }
        let controller = ComposeWindowController(state: state, context: context)
        composeController = controller
        controller.beginSheet(for: window) { [weak self] in
            self?.composeController = nil
            self?.postsViewController.focusTable()
        }
    }

    // MARK: Toolbar

    private enum ItemID {
        static let refresh = NSToolbarItem.Identifier("refresh")
        static let compose = NSToolbarItem.Identifier("compose")
        static let account = NSToolbarItem.Identifier("account")
    }

    func toolbarDefaultItemIdentifiers(_ toolbar: NSToolbar) -> [NSToolbarItem.Identifier] {
        [ItemID.compose, ItemID.refresh, .flexibleSpace, ItemID.account]
    }

    func toolbarAllowedItemIdentifiers(_ toolbar: NSToolbar) -> [NSToolbarItem.Identifier] {
        [ItemID.compose, ItemID.refresh, ItemID.account, .flexibleSpace, .space]
    }

    func toolbar(_ toolbar: NSToolbar, itemForItemIdentifier identifier: NSToolbarItem.Identifier,
                 willBeInsertedIntoToolbar flag: Bool) -> NSToolbarItem? {
        let item = NSToolbarItem(itemIdentifier: identifier)
        switch identifier {
        case ItemID.refresh:
            item.label = "Refresh"
            item.image = NSImage(systemSymbolName: "arrow.clockwise", accessibilityDescription: "Refresh")
            item.target = self
            item.action = #selector(refreshTimeline(_:))
        case ItemID.compose:
            item.label = "New Post"
            item.image = NSImage(systemSymbolName: "square.and.pencil", accessibilityDescription: "New Post")
            item.target = self
            item.action = #selector(composePost(_:))
        case ItemID.account:
            item.label = "Add Account"
            item.image = NSImage(systemSymbolName: "person.crop.circle.badge.plus",
                                 accessibilityDescription: "Add Account")
            item.target = self
            item.action = #selector(presentAddAccountAction(_:))
        default:
            return nil
        }
        item.isBordered = true
        return item
    }

    @objc private func presentAddAccountAction(_ sender: Any?) { presentAddAccount() }
}
