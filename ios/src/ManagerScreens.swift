//
//  ManagerScreens.swift
//
//  The management screens, mirroring the Mac's manager sheets: followed and
//  trending hashtags, Mastodon lists, user aliases, the per-timeline client
//  filter, server-side filters, the profile editor, and user analysis.
//

import UIKit

// MARK: - Hashtags (followed + trending)

@MainActor
final class HashtagsViewController: UITableViewController {
    enum Mode { case followed, trending }
    private let state: AppState
    private let mode: Mode
    private var tags: [FollowedTag]

    init(state: AppState, mode: Mode, hashtags: FollowedHashtags) {
        self.state = state
        self.mode = mode
        self.tags = hashtags.tags
        super.init(style: .insetGrouped)
        title = mode == .followed ? "Followed Hashtags" : "Trending Hashtags"
        if mode == .followed {
            navigationItem.rightBarButtonItem = UIBarButtonItem(
                barButtonSystemItem: .add, target: self, action: #selector(followNew))
            navigationItem.rightBarButtonItem?.accessibilityLabel = "Follow a hashtag"
        }
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    func update(_ hashtags: FollowedHashtags) {
        tags = hashtags.tags
        tableView.reloadData()
    }

    override func tableView(_ tableView: UITableView,
                            numberOfRowsInSection section: Int) -> Int { tags.count }

    override func tableView(_ tableView: UITableView,
                            titleForFooterInSection section: Int) -> String? {
        if tags.isEmpty {
            return mode == .followed ? "You don't follow any hashtags yet." : "Nothing trending."
        }
        return mode == .followed
            ? "Tap a hashtag to open its timeline; use its actions to unfollow."
            : "Tap a hashtag to open its timeline; use its actions to follow."
    }

    override func tableView(_ tableView: UITableView,
                            cellForRowAt indexPath: IndexPath) -> UITableViewCell {
        let cell = UITableViewCell(style: .default, reuseIdentifier: nil)
        guard tags.indices.contains(indexPath.row) else { return cell }
        let tag = tags[indexPath.row]
        var content = cell.defaultContentConfiguration()
        content.text = "#\(tag.name)"
        cell.contentConfiguration = content
        cell.accessibilityCustomActions = [UIAccessibilityCustomAction(
            name: mode == .followed ? "Unfollow" : "Follow") { [weak self] _ in
            self?.toggleFollow(row: indexPath.row)
            return true
        }]
        return cell
    }

    override func tableView(_ tableView: UITableView, didSelectRowAt indexPath: IndexPath) {
        tableView.deselectRow(at: indexPath, animated: true)
        guard tags.indices.contains(indexPath.row) else { return }
        state.spawnTimeline(kind: "hashtag", value: tags[indexPath.row].name)
        navigationController?.popToRootViewController(animated: true)
    }

    override func tableView(_ tableView: UITableView,
                            trailingSwipeActionsConfigurationForRowAt indexPath: IndexPath)
        -> UISwipeActionsConfiguration? {
        let title = mode == .followed ? "Unfollow" : "Follow"
        let action = UIContextualAction(style: mode == .followed ? .destructive : .normal,
                                        title: title) { [weak self] _, _, done in
            self?.toggleFollow(row: indexPath.row)
            done(true)
        }
        return UISwipeActionsConfiguration(actions: [action])
    }

    private func toggleFollow(row: Int) {
        guard tags.indices.contains(row) else { return }
        let name = tags[row].name
        if mode == .followed {
            state.unfollowHashtag(name: name) // core re-emits followed_hashtags → update()
        } else {
            state.followHashtag(name: name) // core follows + announces the result
        }
    }

    @objc private func followNew() {
        let alert = UIAlertController(title: "Follow Hashtag",
                                      message: "Enter a hashtag (without the #).",
                                      preferredStyle: .alert)
        alert.addTextField { $0.autocapitalizationType = .none }
        alert.addAction(UIAlertAction(title: "Cancel", style: .cancel))
        alert.addAction(UIAlertAction(title: "Follow", style: .default) {
            [weak self, weak alert] _ in
            let name = (alert?.textFields?.first?.text ?? "")
                .trimmingCharacters(in: CharacterSet(charactersIn: " #"))
            guard !name.isEmpty else { return }
            self?.state.followHashtag(name: name)
            self?.state.listFollowedHashtags() // follow doesn't auto-emit; refresh
        })
        present(alert, animated: true)
    }
}

// MARK: - Mastodon lists

@MainActor
final class ListsViewController: UITableViewController {
    private let state: AppState
    private var lists: [ListInfo]

    init(state: AppState, lists: Lists) {
        self.state = state
        self.lists = lists.lists
        super.init(style: .insetGrouped)
        title = "Manage Lists"
        navigationItem.rightBarButtonItem = UIBarButtonItem(
            barButtonSystemItem: .add, target: self, action: #selector(createNew))
        navigationItem.rightBarButtonItem?.accessibilityLabel = "New list"
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    func update(_ lists: Lists) {
        self.lists = lists.lists
        tableView.reloadData()
    }

    override func tableView(_ tableView: UITableView,
                            numberOfRowsInSection section: Int) -> Int { lists.count }

    override func tableView(_ tableView: UITableView,
                            titleForFooterInSection section: Int) -> String? {
        lists.isEmpty ? "You have no lists yet."
            : "Tap a list to open its timeline; use its actions to rename or delete."
    }

    override func tableView(_ tableView: UITableView,
                            cellForRowAt indexPath: IndexPath) -> UITableViewCell {
        let cell = UITableViewCell(style: .default, reuseIdentifier: nil)
        guard lists.indices.contains(indexPath.row) else { return cell }
        var content = cell.defaultContentConfiguration()
        content.text = lists[indexPath.row].title
        cell.contentConfiguration = content
        cell.accessibilityCustomActions = [
            UIAccessibilityCustomAction(name: "Rename") { [weak self] _ in
                self?.rename(row: indexPath.row)
                return true
            },
            UIAccessibilityCustomAction(name: "Delete") { [weak self] _ in
                self?.delete(row: indexPath.row)
                return true
            },
        ]
        return cell
    }

    override func tableView(_ tableView: UITableView, didSelectRowAt indexPath: IndexPath) {
        tableView.deselectRow(at: indexPath, animated: true)
        guard lists.indices.contains(indexPath.row) else { return }
        state.spawnTimeline(kind: "list", param: lists[indexPath.row].id)
        navigationController?.popToRootViewController(animated: true)
    }

    override func tableView(_ tableView: UITableView,
                            trailingSwipeActionsConfigurationForRowAt indexPath: IndexPath)
        -> UISwipeActionsConfiguration? {
        let rename = UIContextualAction(style: .normal, title: "Rename") {
            [weak self] _, _, done in
            self?.rename(row: indexPath.row)
            done(true)
        }
        let delete = UIContextualAction(style: .destructive, title: "Delete") {
            [weak self] _, _, done in
            self?.delete(row: indexPath.row)
            done(true)
        }
        return UISwipeActionsConfiguration(actions: [delete, rename])
    }

    @objc private func createNew() {
        prompt(title: "New List", message: "Name for the new list:", value: "") {
            [weak self] title in
            self?.state.createList(title: title) // core re-emits lists → update()
        }
    }

    private func rename(row: Int) {
        guard lists.indices.contains(row) else { return }
        let list = lists[row]
        prompt(title: "Rename List", message: nil, value: list.title) { [weak self] title in
            self?.state.renameList(id: list.id, title: title)
        }
    }

    private func delete(row: Int) {
        guard lists.indices.contains(row) else { return }
        let list = lists[row]
        confirm("Delete List", message: "Delete \"\(list.title)\"?", actionTitle: "Delete",
                on: self) { [weak self] in
            self?.state.deleteList(id: list.id)
        }
    }

    private func prompt(title: String, message: String?, value: String,
                        then run: @escaping (String) -> Void) {
        let alert = UIAlertController(title: title, message: message, preferredStyle: .alert)
        alert.addTextField { $0.text = value }
        alert.addAction(UIAlertAction(title: "Cancel", style: .cancel))
        alert.addAction(UIAlertAction(title: "OK", style: .default) { [weak alert] _ in
            let text = (alert?.textFields?.first?.text ?? "")
                .trimmingCharacters(in: .whitespaces)
            guard !text.isEmpty else { return }
            run(text)
        })
        present(alert, animated: true)
    }
}

// MARK: - User aliases

@MainActor
final class AliasesViewController: UITableViewController {
    private let state: AppState
    private var aliases: [AliasItem]

    init(state: AppState, list: AliasesList) {
        self.state = state
        self.aliases = list.aliases
        super.init(style: .insetGrouped)
        title = "User Aliases"
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    func update(_ list: AliasesList) {
        aliases = list.aliases
        tableView.reloadData()
    }

    override func tableView(_ tableView: UITableView,
                            numberOfRowsInSection section: Int) -> Int { aliases.count }

    override func tableView(_ tableView: UITableView,
                            titleForFooterInSection section: Int) -> String? {
        aliases.isEmpty ? "No aliases yet. Add one from a post's actions."
            : "Tap an alias to edit it; swipe (or use its actions) to remove."
    }

    override func tableView(_ tableView: UITableView,
                            cellForRowAt indexPath: IndexPath) -> UITableViewCell {
        let cell = UITableViewCell(style: .subtitle, reuseIdentifier: nil)
        guard aliases.indices.contains(indexPath.row) else { return cell }
        let item = aliases[indexPath.row]
        var content = cell.defaultContentConfiguration()
        content.text = item.alias
        content.secondaryText = "@\(item.handle)"
        cell.contentConfiguration = content
        cell.accessibilityLabel = "\(item.alias), @\(item.handle)"
        cell.accessibilityCustomActions = [UIAccessibilityCustomAction(name: "Remove") {
            [weak self] _ in
            self?.remove(row: indexPath.row)
            return true
        }]
        return cell
    }

    override func tableView(_ tableView: UITableView, didSelectRowAt indexPath: IndexPath) {
        tableView.deselectRow(at: indexPath, animated: true)
        guard aliases.indices.contains(indexPath.row) else { return }
        let item = aliases[indexPath.row]
        let alert = UIAlertController(title: "Alias for @\(item.handle)",
                                      message: "Leave empty to remove the alias.",
                                      preferredStyle: .alert)
        alert.addTextField { $0.text = item.alias }
        alert.addAction(UIAlertAction(title: "Cancel", style: .cancel))
        alert.addAction(UIAlertAction(title: "Save", style: .default) {
            [weak self, weak alert] _ in
            guard let self else { return }
            let value = (alert?.textFields?.first?.text ?? "")
                .trimmingCharacters(in: .whitespaces)
            if value.isEmpty {
                self.state.clearAlias(key: item.key, handle: item.handle)
            } else {
                self.state.setAlias(key: item.key, handle: item.handle, alias: value)
            }
            self.state.listAliases() // refresh this manager
        })
        present(alert, animated: true)
    }

    override func tableView(_ tableView: UITableView,
                            trailingSwipeActionsConfigurationForRowAt indexPath: IndexPath)
        -> UISwipeActionsConfiguration? {
        let remove = UIContextualAction(style: .destructive, title: "Remove") {
            [weak self] _, _, done in
            self?.remove(row: indexPath.row)
            done(true)
        }
        return UISwipeActionsConfiguration(actions: [remove])
    }

    private func remove(row: Int) {
        guard aliases.indices.contains(row) else { return }
        let item = aliases[row]
        state.clearAlias(key: item.key, handle: item.handle)
        state.listAliases()
    }
}

// MARK: - Static form base (eagerly-built cells)

@MainActor
class StaticFormViewController: UITableViewController {
    struct FormSection {
        var title: String?
        var footer: String?
        var cells: [UITableViewCell]
    }
    var formSections: [FormSection] = []

    init() { super.init(style: .insetGrouped) }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    override func numberOfSections(in tableView: UITableView) -> Int { formSections.count }
    override func tableView(_ tableView: UITableView,
                            numberOfRowsInSection section: Int) -> Int {
        formSections[section].cells.count
    }
    override func tableView(_ tableView: UITableView,
                            titleForHeaderInSection section: Int) -> String? {
        formSections[section].title
    }
    override func tableView(_ tableView: UITableView,
                            titleForFooterInSection section: Int) -> String? {
        formSections[section].footer
    }
    override func tableView(_ tableView: UITableView,
                            cellForRowAt indexPath: IndexPath) -> UITableViewCell {
        formSections[indexPath.section].cells[indexPath.row]
    }

    // MARK: Cell builders

    func fieldCell(_ label: String, text: String, placeholder: String? = nil) -> (UITableViewCell, UITextField) {
        let cell = UITableViewCell(style: .default, reuseIdentifier: nil)
        let field = UITextField()
        field.text = text
        field.placeholder = placeholder ?? label
        field.accessibilityLabel = label
        field.autocapitalizationType = .none
        field.translatesAutoresizingMaskIntoConstraints = false
        cell.contentView.addSubview(field)
        NSLayoutConstraint.activate([
            field.leadingAnchor.constraint(equalTo: cell.contentView.layoutMarginsGuide.leadingAnchor),
            field.trailingAnchor.constraint(equalTo: cell.contentView.layoutMarginsGuide.trailingAnchor),
            field.topAnchor.constraint(equalTo: cell.contentView.topAnchor, constant: 12),
            field.bottomAnchor.constraint(equalTo: cell.contentView.bottomAnchor, constant: -12),
        ])
        cell.selectionStyle = .none
        return (cell, field)
    }

    func pickerCell(_ label: String, options: [String], selected: Int,
                    onPick: @escaping (Int) -> Void) -> UITableViewCell {
        let cell = UITableViewCell(style: .value1, reuseIdentifier: nil)
        var content = cell.defaultContentConfiguration()
        content.text = label
        content.secondaryText = options.indices.contains(selected) ? options[selected] : ""
        cell.contentConfiguration = content
        cell.selectionStyle = .none
        let button = UIButton(type: .system)
        var index = selected
        func rebuild() {
            button.menu = UIMenu(children: options.enumerated().map { i, name in
                UIAction(title: name, state: i == index ? .on : .off) { _ in
                    index = i
                    var c = cell.defaultContentConfiguration()
                    c.text = label
                    c.secondaryText = name
                    cell.contentConfiguration = c
                    cell.accessibilityValue = name
                    rebuild()
                    onPick(i)
                }
            })
        }
        rebuild()
        button.showsMenuAsPrimaryAction = true
        button.frame = cell.contentView.bounds
        button.autoresizingMask = [.flexibleWidth, .flexibleHeight]
        cell.contentView.addSubview(button)
        cell.isAccessibilityElement = true
        cell.accessibilityLabel = label
        cell.accessibilityValue = options.indices.contains(selected) ? options[selected] : ""
        cell.accessibilityTraits = .button
        return cell
    }
}

// MARK: - Client filter (per-timeline)

@MainActor
final class ClientFilterViewController: StaticFormViewController {
    private let state: AppState
    private var toggles: [(key: String, control: UISwitch)] = []
    private var textField: UITextField!

    init(state: AppState, filter: ClientFilter) {
        self.state = state
        super.init()
        title = "Filter Timeline"
        navigationItem.rightBarButtonItem = UIBarButtonItem(
            title: "Apply", style: .done, target: self, action: #selector(apply))

        let options: [(String, String, Bool)] = [
            ("Original posts", "original", filter.original),
            ("Replies", "replies", filter.replies),
            ("Replies to me", "replies_to_me", filter.repliesToMe),
            ("Threads (self-replies)", "threads", filter.threads),
            ("Boosts", "boosts", filter.boosts),
            ("Quotes", "quotes", filter.quotes),
            ("Posts with media", "media", filter.media),
            ("Posts without media", "no_media", filter.noMedia),
            ("My posts", "my_posts", filter.myPosts),
            ("My replies", "my_replies", filter.myReplies),
        ]
        var showCells: [UITableViewCell] = []
        for (label, key, on) in options {
            let toggle = UISwitch()
            toggle.isOn = on
            let cell = ToggleHostCell(title: label, toggle: toggle)
            toggles.append((key, toggle))
            showCells.append(cell)
        }
        let (textCell, field) = fieldCell("Only posts containing", text: filter.text,
                                          placeholder: "Text to require (optional)")
        textField = field

        let clearCell = UITableViewCell(style: .default, reuseIdentifier: nil)
        var clearContent = clearCell.defaultContentConfiguration()
        clearContent.text = "Clear Filter"
        clearContent.textProperties.color = .systemRed
        clearCell.contentConfiguration = clearContent
        clearCell.accessibilityTraits = .button

        formSections = [
            FormSection(title: "Show", footer: nil, cells: showCells),
            FormSection(title: "Text", footer: "Keep only posts containing this text.",
                        cells: [textCell]),
            FormSection(title: nil, footer: nil, cells: [clearCell]),
        ]
    }

    override func tableView(_ tableView: UITableView, didSelectRowAt indexPath: IndexPath) {
        tableView.deselectRow(at: indexPath, animated: true)
        guard indexPath.section == 2 else { return }
        state.clearClientFilter()
        navigationController?.popViewController(animated: true)
    }

    @objc private func apply() {
        var filter: [String: Any] = [:]
        for (key, toggle) in toggles { filter[key] = toggle.isOn }
        filter["text"] = textField.text ?? ""
        state.setClientFilter(filter)
        navigationController?.popViewController(animated: true)
    }
}

/// A plain switch row hosting an externally-owned UISwitch (so the form can
/// read it back on submit). One VoiceOver element; double-tap toggles.
final class ToggleHostCell: UITableViewCell {
    private let toggle: UISwitch

    init(title: String, toggle: UISwitch) {
        self.toggle = toggle
        super.init(style: .default, reuseIdentifier: nil)
        var content = defaultContentConfiguration()
        content.text = title
        contentConfiguration = content
        accessoryView = toggle
        selectionStyle = .none
        isAccessibilityElement = true
        accessibilityLabel = title
        accessibilityValue = toggle.isOn ? "On" : "Off"
        accessibilityTraits = .button
        toggle.addTarget(self, action: #selector(changed), for: .valueChanged)
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    @objc private func changed() { accessibilityValue = toggle.isOn ? "On" : "Off" }

    override func accessibilityActivate() -> Bool {
        toggle.setOn(!toggle.isOn, animated: true)
        changed()
        return true
    }
}

// MARK: - Profile editor

@MainActor
final class ProfileEditorViewController: StaticFormViewController {
    private let state: AppState
    private let editor: ProfileEditor
    private var nameField: UITextField!
    private let bioView = UITextView()
    private var privacyIndex = 0
    private let privacyTokens = ["public", "unlisted", "private", "direct"]
    private var checks: [(key: String, control: UISwitch)] = []
    private var fieldNames: [UITextField] = []
    private var fieldValues: [UITextField] = []

    init(state: AppState, editor: ProfileEditor) {
        self.state = state
        self.editor = editor
        super.init()
        title = "Edit Profile"
        navigationItem.rightBarButtonItem = UIBarButtonItem(
            barButtonSystemItem: .save, target: self, action: #selector(save))

        let (nameCell, name) = fieldCell("Display name", text: editor.displayName)
        name.autocapitalizationType = .words
        nameField = name

        let bioCell = UITableViewCell(style: .default, reuseIdentifier: nil)
        bioView.font = .preferredFont(forTextStyle: .body)
        bioView.accessibilityLabel = "Bio"
        bioView.translatesAutoresizingMaskIntoConstraints = false
        bioView.text = editor.note
        bioView.isScrollEnabled = false
        bioCell.contentView.addSubview(bioView)
        NSLayoutConstraint.activate([
            bioView.leadingAnchor.constraint(
                equalTo: bioCell.contentView.layoutMarginsGuide.leadingAnchor),
            bioView.trailingAnchor.constraint(
                equalTo: bioCell.contentView.layoutMarginsGuide.trailingAnchor),
            bioView.topAnchor.constraint(equalTo: bioCell.contentView.topAnchor, constant: 8),
            bioView.bottomAnchor.constraint(equalTo: bioCell.contentView.bottomAnchor,
                                            constant: -8),
            bioView.heightAnchor.constraint(greaterThanOrEqualToConstant: 100),
        ])
        bioCell.selectionStyle = .none

        formSections = [FormSection(title: nil, footer: nil, cells: [nameCell, bioCell])]

        if !editor.simple {
            privacyIndex = privacyTokens.firstIndex(of: editor.privacy) ?? 0
            let privacy = pickerCell("Default post privacy",
                                     options: ["Public", "Unlisted", "Followers only", "Direct"],
                                     selected: privacyIndex) { [weak self] index in
                self?.privacyIndex = index
            }
            var optionCells: [UITableViewCell] = [privacy]
            let checkDefs: [(String, String, Bool)] = [
                ("Require follow requests", "locked", editor.locked),
                ("This is a bot account", "bot", editor.bot),
                ("List me in the profile directory", "discoverable", editor.discoverable),
                ("Mark my media sensitive by default", "sensitive", editor.sensitive),
            ]
            for (label, key, on) in checkDefs {
                let toggle = UISwitch()
                toggle.isOn = on
                checks.append((key, toggle))
                optionCells.append(ToggleHostCell(title: label, toggle: toggle))
            }
            formSections.append(FormSection(title: "Options", footer: nil, cells: optionCells))

            var fieldCells: [UITableViewCell] = []
            for i in 0..<max(1, editor.maxFields) {
                let (nCell, n) = fieldCell("Field \(i + 1) label",
                                           text: i < editor.fields.count
                                               ? editor.fields[i].name : "",
                                           placeholder: "Label")
                let (vCell, v) = fieldCell("Field \(i + 1) content",
                                           text: i < editor.fields.count
                                               ? editor.fields[i].value : "",
                                           placeholder: "Content")
                fieldNames.append(n)
                fieldValues.append(v)
                fieldCells.append(nCell)
                fieldCells.append(vCell)
            }
            formSections.append(FormSection(title: "Profile fields",
                                            footer: "Label and content pairs shown on your profile.",
                                            cells: fieldCells))
        }
    }

    @objc private func save() {
        var fields: [[String: String]] = []
        for i in 0..<fieldNames.count {
            fields.append(["name": fieldNames[i].text ?? "",
                           "value": fieldValues[i].text ?? ""])
        }
        func check(_ key: String) -> Bool {
            checks.first { $0.key == key }?.control.isOn ?? false
        }
        state.updateProfile(displayName: nameField.text ?? "", note: bioView.text ?? "",
                            locked: check("locked"), bot: check("bot"),
                            discoverable: check("discoverable"), sensitive: check("sensitive"),
                            privacy: privacyTokens[privacyIndex], fields: fields)
        navigationController?.popViewController(animated: true)
    }
}

// MARK: - User analysis

@MainActor
final class UserAnalysisViewController: UITableViewController {
    private let state: AppState
    // Keep in sync with the Windows/Mac/Android pickers and the categories
    // handled in CoreSession::cmd_analyze_users.
    private let analyses: [(title: String, category: String)] = [
        ("People who follow you that you don't follow back", "not_following_back"),
        ("People you follow who don't follow you back", "no_followback"),
        ("Mutual follows (you both follow each other)", "mutuals"),
    ]

    init(state: AppState) {
        self.state = state
        super.init(style: .insetGrouped)
        title = "User Analysis"
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    override func tableView(_ tableView: UITableView,
                            numberOfRowsInSection section: Int) -> Int { analyses.count }

    override func tableView(_ tableView: UITableView,
                            titleForFooterInSection section: Int) -> String? {
        "Loads your full follow lists, then opens the chosen group as a user timeline."
    }

    override func tableView(_ tableView: UITableView,
                            cellForRowAt indexPath: IndexPath) -> UITableViewCell {
        let cell = UITableViewCell(style: .default, reuseIdentifier: nil)
        var content = cell.defaultContentConfiguration()
        content.text = analyses[indexPath.row].title
        content.textProperties.numberOfLines = 0
        cell.contentConfiguration = content
        cell.accessibilityTraits = .button
        return cell
    }

    override func tableView(_ tableView: UITableView, didSelectRowAt indexPath: IndexPath) {
        tableView.deselectRow(at: indexPath, animated: true)
        state.analyzeUsers(category: analyses[indexPath.row].category)
        navigationController?.popToRootViewController(animated: true)
    }
}

// MARK: - Server filters

@MainActor
final class ServerFiltersViewController: UITableViewController {
    private let state: AppState
    private var filters: [ServerFilter]

    init(state: AppState, filters: ServerFilters) {
        self.state = state
        self.filters = filters.filters
        super.init(style: .insetGrouped)
        title = "Server Filters"
        navigationItem.rightBarButtonItem = UIBarButtonItem(
            barButtonSystemItem: .add, target: self, action: #selector(createNew))
        navigationItem.rightBarButtonItem?.accessibilityLabel = "New filter"
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    func update(_ filters: ServerFilters) {
        self.filters = filters.filters
        tableView.reloadData()
    }

    override func tableView(_ tableView: UITableView,
                            numberOfRowsInSection section: Int) -> Int { filters.count }

    override func tableView(_ tableView: UITableView,
                            titleForFooterInSection section: Int) -> String? {
        filters.isEmpty ? "No server-side filters yet."
            : "Tap a filter to edit it; swipe (or use its actions) to delete."
    }

    override func tableView(_ tableView: UITableView,
                            cellForRowAt indexPath: IndexPath) -> UITableViewCell {
        let cell = UITableViewCell(style: .subtitle, reuseIdentifier: nil)
        guard filters.indices.contains(indexPath.row) else { return cell }
        let filter = filters[indexPath.row]
        var content = cell.defaultContentConfiguration()
        content.text = filter.title
        content.secondaryText = filter.action == "hide" ? "Hides posts" : "Warns"
        cell.contentConfiguration = content
        cell.accessibilityCustomActions = [UIAccessibilityCustomAction(name: "Delete") {
            [weak self] _ in
            self?.delete(row: indexPath.row)
            return true
        }]
        return cell
    }

    override func tableView(_ tableView: UITableView, didSelectRowAt indexPath: IndexPath) {
        tableView.deselectRow(at: indexPath, animated: true)
        guard filters.indices.contains(indexPath.row) else { return }
        navigationController?.pushViewController(
            ServerFilterEditorViewController(state: state, filter: filters[indexPath.row]),
            animated: true)
    }

    override func tableView(_ tableView: UITableView,
                            trailingSwipeActionsConfigurationForRowAt indexPath: IndexPath)
        -> UISwipeActionsConfiguration? {
        let delete = UIContextualAction(style: .destructive, title: "Delete") {
            [weak self] _, _, done in
            self?.delete(row: indexPath.row)
            done(true)
        }
        return UISwipeActionsConfiguration(actions: [delete])
    }

    @objc private func createNew() {
        navigationController?.pushViewController(
            ServerFilterEditorViewController(state: state, filter: ServerFilter()),
            animated: true)
    }

    private func delete(row: Int) {
        guard filters.indices.contains(row) else { return }
        let filter = filters[row]
        confirm("Delete Filter", message: "Delete \"\(filter.title)\"?", actionTitle: "Delete",
                on: self) { [weak self] in
            self?.state.deleteServerFilter(id: filter.id) // re-emits server_filters
        }
    }
}

@MainActor
final class ServerFilterEditorViewController: StaticFormViewController {
    private let state: AppState
    private let filter: ServerFilter

    private var titleField: UITextField!
    private var actionIndex = 0
    private var expiryIndex = 0
    private var contextToggles: [(key: String, control: UISwitch)] = []
    private var keywordFields: [(id: String, field: UITextField, wholeWord: UISwitch)] = []

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
        super.init()
        title = filter.id.isEmpty ? "New Filter" : "Edit Filter"
        navigationItem.rightBarButtonItem = UIBarButtonItem(
            barButtonSystemItem: .save, target: self, action: #selector(save))

        let (titleCell, field) = fieldCell("Filter name", text: filter.title)
        titleField = field
        actionIndex = filter.action == "hide" ? 1 : 0
        let actionCell = pickerCell("Action", options: ["Warn", "Hide completely"],
                                    selected: actionIndex) { [weak self] i in
            self?.actionIndex = i
        }
        let expiryCell = pickerCell("Expires", options: expiryOptions.map { $0.0 },
                                    selected: 0) { [weak self] i in
            self?.expiryIndex = i
        }

        var contextCells: [UITableViewCell] = []
        for (label, key) in contextOptions {
            let toggle = UISwitch()
            toggle.isOn = filter.context.contains(key)
            contextToggles.append((key, toggle))
            contextCells.append(ToggleHostCell(title: label, toggle: toggle))
        }

        formSections = [
            FormSection(title: nil, footer: nil, cells: [titleCell, actionCell, expiryCell]),
            FormSection(title: "Apply in", footer: nil, cells: contextCells),
            FormSection(title: "Keywords",
                        footer: "Each keyword row has a whole-word switch. "
                              + "Swipe a keyword to remove it.",
                        cells: []),
        ]
        for keyword in filter.keywords {
            appendKeywordCell(keyword: keyword)
        }
        appendAddKeywordCell()
    }

    private func appendKeywordCell(keyword: ServerFilterKeyword) {
        let cell = UITableViewCell(style: .default, reuseIdentifier: nil)
        let field = UITextField()
        field.text = keyword.keyword
        field.placeholder = "Keyword"
        field.accessibilityLabel = "Keyword"
        field.autocapitalizationType = .none
        let whole = UISwitch()
        whole.isOn = keyword.wholeWord
        whole.accessibilityLabel = "Whole word only"
        let stack = UIStackView(arrangedSubviews: [field, whole])
        stack.spacing = 8
        stack.translatesAutoresizingMaskIntoConstraints = false
        cell.contentView.addSubview(stack)
        NSLayoutConstraint.activate([
            stack.leadingAnchor.constraint(
                equalTo: cell.contentView.layoutMarginsGuide.leadingAnchor),
            stack.trailingAnchor.constraint(
                equalTo: cell.contentView.layoutMarginsGuide.trailingAnchor),
            stack.topAnchor.constraint(equalTo: cell.contentView.topAnchor, constant: 8),
            stack.bottomAnchor.constraint(equalTo: cell.contentView.bottomAnchor, constant: -8),
        ])
        cell.selectionStyle = .none
        keywordFields.append((keyword.id, field, whole))
        formSections[2].cells.insert(cell, at: formSections[2].cells.count - max(0, addRowCount))
        if viewIfLoaded != nil { tableView.reloadData() }
    }

    /// 1 once the trailing "Add Keyword" row exists.
    private var addRowCount = 0

    private func appendAddKeywordCell() {
        let cell = UITableViewCell(style: .default, reuseIdentifier: nil)
        var content = cell.defaultContentConfiguration()
        content.text = "Add Keyword"
        content.textProperties.color = .tintColor
        cell.contentConfiguration = content
        cell.accessibilityTraits = .button
        formSections[2].cells.append(cell)
        addRowCount = 1
    }

    override func tableView(_ tableView: UITableView, didSelectRowAt indexPath: IndexPath) {
        tableView.deselectRow(at: indexPath, animated: true)
        guard indexPath.section == 2,
              indexPath.row == formSections[2].cells.count - 1 else { return }
        appendKeywordCell(keyword: ServerFilterKeyword(keyword: "", wholeWord: false))
    }

    override func tableView(_ tableView: UITableView,
                            trailingSwipeActionsConfigurationForRowAt indexPath: IndexPath)
        -> UISwipeActionsConfiguration? {
        guard indexPath.section == 2,
              indexPath.row < formSections[2].cells.count - addRowCount else { return nil }
        let remove = UIContextualAction(style: .destructive, title: "Remove") {
            [weak self] _, _, done in
            guard let self, self.keywordFields.indices.contains(indexPath.row) else {
                done(false)
                return
            }
            self.keywordFields.remove(at: indexPath.row)
            self.formSections[2].cells.remove(at: indexPath.row)
            self.tableView.reloadData()
            done(true)
        }
        return UISwipeActionsConfiguration(actions: [remove])
    }

    @objc private func save() {
        let title = (titleField.text ?? "").trimmingCharacters(in: .whitespaces)
        let keywords: [[String: Any]] = keywordFields.compactMap { entry in
            let text = (entry.field.text ?? "").trimmingCharacters(in: .whitespaces)
            guard !text.isEmpty else { return nil }
            var d: [String: Any] = ["keyword": text, "whole_word": entry.wholeWord.isOn]
            if !entry.id.isEmpty { d["id"] = entry.id }
            return d
        }
        let contexts = contextToggles.filter { $0.control.isOn }.map { $0.key }
        guard !title.isEmpty, !keywords.isEmpty, !contexts.isEmpty else {
            showError("A filter needs a name, at least one keyword, and at least one context.",
                      on: self)
            return
        }
        state.saveServerFilter([
            "id": filter.id,
            "title": title,
            "action": actionIndex == 1 ? "hide" : "warn",
            "context": contexts,
            "expires_in": expiryOptions[expiryIndex].1,
            "keywords": keywords,
        ])
        navigationController?.popViewController(animated: true)
    }
}
