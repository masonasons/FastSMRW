//
//  DetailScreens.swift
//
//  The navigation-layer detail screens, mirroring the Mac's detail dialogs:
//  Post Info (with poll voting), User Profile (relationship actions), Report,
//  New Timeline, and Add to List. All are pushed table screens — the body text
//  is a plain row VoiceOver reads naturally, followed by action rows.
//

import UIKit

// MARK: - Shared body + actions table

@MainActor
class ActionListViewController: UITableViewController {
    struct Item {
        let title: String
        /// Leave this screen before running (for actions that navigate).
        var pops = false
        var destructive = false
        let run: () -> Void
    }

    private let body: String
    private var items: [Item]

    init(title: String, body: String, items: [Item]) {
        self.body = body
        self.items = items
        super.init(style: .insetGrouped)
        self.title = title
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    override func numberOfSections(in tableView: UITableView) -> Int { 2 }

    override func tableView(_ tableView: UITableView,
                            numberOfRowsInSection section: Int) -> Int {
        section == 0 ? 1 : items.count
    }

    override func tableView(_ tableView: UITableView,
                            cellForRowAt indexPath: IndexPath) -> UITableViewCell {
        let cell = UITableViewCell(style: .default, reuseIdentifier: nil)
        var content = cell.defaultContentConfiguration()
        if indexPath.section == 0 {
            content.text = body
            content.textProperties.numberOfLines = 0
            cell.selectionStyle = .none
        } else {
            let item = items[indexPath.row]
            content.text = item.title
            content.textProperties.color = item.destructive ? .systemRed : .tintColor
            cell.accessibilityTraits = .button
        }
        cell.contentConfiguration = content
        return cell
    }

    override func tableView(_ tableView: UITableView, didSelectRowAt indexPath: IndexPath) {
        tableView.deselectRow(at: indexPath, animated: true)
        guard indexPath.section == 1, items.indices.contains(indexPath.row) else { return }
        let item = items[indexPath.row]
        if item.pops {
            navigationController?.popToRootViewController(animated: true)
        }
        item.run()
    }
}

// MARK: - Post Info

@MainActor
final class PostInfoViewController: ActionListViewController {
    init(state: AppState, info: PostInfo) {
        let id = info.id
        var items: [Item] = [
            Item(title: "Reply", pops: true) { state.requestCompose(mode: "reply", id: id) },
            Item(title: "Boost") { state.toggleBoost(id: id) },
            Item(title: "Favorite") { state.toggleFavorite(id: id) },
        ]
        if info.features?["quote"] != false {
            items.append(Item(title: "Quote", pops: true) {
                state.requestCompose(mode: "quote", id: id)
            })
        }
        items.append(Item(title: "View Thread", pops: true) { state.openThread(id: id) })
        if info.favoritesCount > 0 {
            items.append(Item(title: "View Favorited (\(info.favoritesCount))", pops: true) {
                state.openFavoritedBy(id: id)
            })
        }
        if info.boostsCount > 0 {
            items.append(Item(title: "View Reposters (\(info.boostsCount))", pops: true) {
                state.openRebloggedBy(id: id)
            })
        }
        if info.features?["mute_conversations"] == true {
            items.append(Item(title: info.muted ? "Unmute Conversation" : "Mute Conversation") {
                state.toggleMuteConversation(id: id)
            })
        }
        if info.hasUrl {
            items.append(Item(title: "Open in Browser", pops: true) { state.openStatus(id: id) })
        }
        super.init(title: "Post Info", body: info.text, items: items)

        if let poll = info.poll {
            insertVoteItem(state: state, statusId: id, poll: poll)
        }
        addReportItem(state: state, id: id, accountId: nil, acct: "", remote: false)
    }

    /// "Vote…" needs self to push, so it's added after super.init.
    private func insertVoteItem(state: AppState, statusId: String, poll: PostInfoPoll) {
        add(Item(title: "Vote…") { [weak self] in
            let vote = PollVoteViewController(state: state, statusId: statusId, poll: poll)
            self?.navigationController?.pushViewController(vote, animated: true)
        })
    }
}

// MARK: - User Profile

@MainActor
final class UserProfileViewController: ActionListViewController {
    init(state: AppState, profile: UserProfile) {
        let acct = profile.acct
        let accountId = profile.accountId
        var items: [Item] = []
        if profile.hasRelationship {
            let following = profile.following ?? false
            items.append(Item(title: following ? "Unfollow" : "Follow", pops: true) {
                state.setRelationship(accountId: accountId,
                                      action: following ? "unfollow" : "follow", acct: acct)
            })
            let muting = profile.muting ?? false
            items.append(Item(title: muting ? "Unmute" : "Mute", pops: true) {
                state.setRelationship(accountId: accountId,
                                      action: muting ? "unmute" : "mute", acct: acct)
            })
            let blocking = profile.blocking ?? false
            items.append(Item(title: blocking ? "Unblock" : "Block", pops: true,
                              destructive: !blocking) {
                state.setRelationship(accountId: accountId,
                                      action: blocking ? "unblock" : "block", acct: acct)
            })
        }
        items.append(Item(title: "Open Timeline", pops: true) {
            state.openUserTimeline(accountId: accountId, acct: acct)
        })
        if profile.canUseLists {
            items.append(Item(title: "Add to List…") {
                state.getUserLists(accountId: accountId, acct: acct)
            })
        }
        if let url = profile.url, let link = URL(string: url) {
            items.append(Item(title: "Open in Browser") { UIApplication.shared.open(link) })
        }
        super.init(title: "User: @\(acct)", body: profile.text, items: items)
        addReportItem(state: state, id: nil, accountId: accountId, acct: acct,
                      remote: acct.contains("@"))
    }
}

extension ActionListViewController {
    func add(_ item: Item) {
        items.append(item)
        // During init the view isn't loaded yet; the first layout shows
        // everything. Only live additions need a refresh.
        if viewIfLoaded != nil { tableView.reloadData() }
    }

    func addReportItem(state: AppState, id: String?, accountId: String?, acct: String,
                       remote: Bool) {
        add(Item(title: "Report…", destructive: true) { [weak self] in
            let report = ReportViewController(state: state, id: id, accountId: accountId,
                                              acct: acct, remote: remote)
            self?.navigationController?.pushViewController(report, animated: true)
        })
    }
}

// MARK: - Poll voting

@MainActor
final class PollVoteViewController: UITableViewController {
    private let state: AppState
    private let statusId: String
    private let poll: PostInfoPoll
    private var selected = Set<Int>()

    init(state: AppState, statusId: String, poll: PostInfoPoll) {
        self.state = state
        self.statusId = statusId
        self.poll = poll
        super.init(style: .insetGrouped)
        title = "Vote"
        navigationItem.rightBarButtonItem = UIBarButtonItem(
            title: "Vote", style: .done, target: self, action: #selector(submit))
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    override func tableView(_ tableView: UITableView,
                            numberOfRowsInSection section: Int) -> Int { poll.options.count }

    override func tableView(_ tableView: UITableView,
                            titleForFooterInSection section: Int) -> String? {
        poll.multiple ? "Choose one or more options." : "Choose one option."
    }

    override func tableView(_ tableView: UITableView,
                            cellForRowAt indexPath: IndexPath) -> UITableViewCell {
        let cell = UITableViewCell(style: .default, reuseIdentifier: nil)
        var content = cell.defaultContentConfiguration()
        content.text = poll.options[indexPath.row]
        cell.contentConfiguration = content
        let isOn = selected.contains(indexPath.row)
        cell.accessoryType = isOn ? .checkmark : .none
        cell.accessibilityValue = isOn ? "Selected" : "Not selected"
        return cell
    }

    override func tableView(_ tableView: UITableView, didSelectRowAt indexPath: IndexPath) {
        tableView.deselectRow(at: indexPath, animated: false)
        if poll.multiple {
            if selected.contains(indexPath.row) {
                selected.remove(indexPath.row)
            } else {
                selected.insert(indexPath.row)
            }
        } else {
            selected = [indexPath.row]
        }
        tableView.reloadData()
    }

    @objc private func submit() {
        guard !selected.isEmpty else { return }
        state.votePoll(id: statusId, choices: selected.sorted())
        navigationController?.popViewController(animated: true)
    }
}

// MARK: - Report

@MainActor
final class ReportViewController: UIViewController {
    private let state: AppState
    private let id: String?
    private let accountId: String?
    private let acct: String

    private let categoryTokens = ["spam", "violation", "legal", "other"]
    private let categoryTitles = ["It's spam", "It breaks a server rule",
                                  "It's illegal content", "Something else"]
    private var categoryIndex = 0
    private let categoryButton = UIButton(type: .system)
    private let commentField = UITextField()
    private let forwardToggle = UISwitch()

    init(state: AppState, id: String?, accountId: String?, acct: String, remote: Bool) {
        self.state = state
        self.id = id
        self.accountId = accountId
        self.acct = acct
        super.init(nibName: nil, bundle: nil)
        title = "Report"
        forwardToggle.isOn = remote
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    override func viewDidLoad() {
        super.viewDidLoad()
        view.backgroundColor = .systemBackground
        navigationItem.rightBarButtonItem = UIBarButtonItem(
            title: "Submit", style: .done, target: self, action: #selector(submit))

        let info = UILabel()
        info.text = "This report is sent to your server's moderators."
        info.font = .preferredFont(forTextStyle: .footnote)
        info.textColor = .secondaryLabel
        info.numberOfLines = 0

        categoryButton.menu = UIMenu(children: categoryTitles.enumerated().map { index, name in
            UIAction(title: name, state: index == categoryIndex ? .on : .off) { [weak self] _ in
                self?.categoryIndex = index
                self?.updateCategoryButton()
            }
        })
        categoryButton.showsMenuAsPrimaryAction = true
        categoryButton.changesSelectionAsPrimaryAction = true
        categoryButton.contentHorizontalAlignment = .leading
        updateCategoryButton()

        commentField.placeholder = "Additional details (optional)"
        commentField.accessibilityLabel = "Additional details"
        commentField.borderStyle = .roundedRect

        let forwardLabel = UILabel()
        forwardLabel.text = "Forward to the user's home server"
        forwardLabel.font = .preferredFont(forTextStyle: .body)
        forwardLabel.numberOfLines = 0
        forwardToggle.accessibilityLabel = "Forward to the user's home server"
        let forwardRow = UIStackView(arrangedSubviews: [forwardLabel, forwardToggle])
        forwardRow.spacing = 8

        let stack = UIStackView(arrangedSubviews: [info, categoryButton, commentField,
                                                   forwardRow])
        stack.axis = .vertical
        stack.spacing = 16
        stack.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(stack)
        NSLayoutConstraint.activate([
            stack.topAnchor.constraint(equalTo: view.safeAreaLayoutGuide.topAnchor,
                                       constant: 20),
            stack.leadingAnchor.constraint(equalTo: view.layoutMarginsGuide.leadingAnchor),
            stack.trailingAnchor.constraint(equalTo: view.layoutMarginsGuide.trailingAnchor),
        ])
    }

    private func updateCategoryButton() {
        categoryButton.menu = UIMenu(children: categoryTitles.enumerated().map { index, name in
            UIAction(title: name, state: index == categoryIndex ? .on : .off) { [weak self] _ in
                self?.categoryIndex = index
                self?.updateCategoryButton()
            }
        })
        categoryButton.setTitle("Reason: \(categoryTitles[categoryIndex])", for: .normal)
        categoryButton.accessibilityLabel = "Reason, \(categoryTitles[categoryIndex])"
    }

    @objc private func submit() {
        state.report(id: id, accountId: accountId, acct: acct,
                     category: categoryTokens[categoryIndex],
                     comment: commentField.text ?? "", forward: forwardToggle.isOn)
        navigationController?.popToRootViewController(animated: true)
    }
}

// MARK: - New Timeline

@MainActor
final class NewTimelineViewController: UITableViewController {
    private let state: AppState
    private let spawnables: [Spawnable]

    init(state: AppState, spawnables: [Spawnable]) {
        self.state = state
        self.spawnables = spawnables
        super.init(style: .insetGrouped)
        title = "New Timeline"
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    override func tableView(_ tableView: UITableView,
                            numberOfRowsInSection section: Int) -> Int { spawnables.count }

    override func tableView(_ tableView: UITableView,
                            cellForRowAt indexPath: IndexPath) -> UITableViewCell {
        let cell = UITableViewCell(style: .default, reuseIdentifier: nil)
        var content = cell.defaultContentConfiguration()
        content.text = spawnables[indexPath.row].title
        cell.contentConfiguration = content
        cell.accessibilityTraits = .button
        return cell
    }

    override func tableView(_ tableView: UITableView, didSelectRowAt indexPath: IndexPath) {
        tableView.deselectRow(at: indexPath, animated: true)
        guard spawnables.indices.contains(indexPath.row) else { return }
        let entry = spawnables[indexPath.row]
        if let input = entry.input {
            // Parameterized (hashtag, search, remote instance): ask for the value.
            let alert = UIAlertController(title: entry.title, message: nil,
                                          preferredStyle: .alert)
            alert.addTextField { field in
                field.placeholder = input
                field.autocapitalizationType = .none
            }
            alert.addAction(UIAlertAction(title: "Cancel", style: .cancel))
            alert.addAction(UIAlertAction(title: "Open", style: .default) {
                [weak self, weak alert] _ in
                let value = (alert?.textFields?.first?.text ?? "")
                    .trimmingCharacters(in: .whitespaces)
                guard !value.isEmpty else { return }
                self?.state.spawnTimeline(kind: entry.kind, value: value)
                self?.navigationController?.popToRootViewController(animated: true)
            })
            present(alert, animated: true)
        } else {
            if let param = entry.param {
                state.spawnTimeline(kind: entry.kind, param: param)
            } else {
                state.spawnTimeline(kind: entry.kind)
            }
            navigationController?.popToRootViewController(animated: true)
        }
    }
}

// MARK: - Add to List

@MainActor
final class UserListsViewController: UITableViewController {
    private let state: AppState
    private var lists: [UserListEntry]
    private let accountId: String

    init(state: AppState, userLists: UserLists) {
        self.state = state
        self.lists = userLists.lists
        self.accountId = userLists.accountId
        super.init(style: .insetGrouped)
        title = userLists.acct.isEmpty ? "Lists" : "Lists for @\(userLists.acct)"
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    override func tableView(_ tableView: UITableView,
                            numberOfRowsInSection section: Int) -> Int { lists.count }

    override func tableView(_ tableView: UITableView,
                            titleForFooterInSection section: Int) -> String? {
        lists.isEmpty ? "You have no lists yet." : "Tap a list to add or remove this user."
    }

    override func tableView(_ tableView: UITableView,
                            cellForRowAt indexPath: IndexPath) -> UITableViewCell {
        let cell = UITableViewCell(style: .default, reuseIdentifier: nil)
        guard lists.indices.contains(indexPath.row) else { return cell }
        let list = lists[indexPath.row]
        var content = cell.defaultContentConfiguration()
        content.text = list.title
        cell.contentConfiguration = content
        cell.accessoryType = list.member ? .checkmark : .none
        cell.accessibilityValue = list.member ? "Member" : "Not a member"
        return cell
    }

    override func tableView(_ tableView: UITableView, didSelectRowAt indexPath: IndexPath) {
        tableView.deselectRow(at: indexPath, animated: false)
        guard lists.indices.contains(indexPath.row) else { return }
        lists[indexPath.row].member.toggle()
        let list = lists[indexPath.row]
        state.setUserList(listId: list.id, accountId: accountId, add: list.member)
        tableView.reloadRows(at: [indexPath], with: .none)
    }
}
