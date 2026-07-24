//
//  SettingsViewController.swift
//
//  The Settings screens, two-level like the Android app: a root list of
//  categories mirroring the desktop settings pages (General, Timelines, Audio,
//  Earcons, Speech, Behavior, Advanced, Confirmation), each pushing a panel of
//  its controls. Each control reads the core's settings and writes back
//  through update_settings, which the core applies live.
//

import UIKit

/// The actions offered for the interact / secondary-interact pickers (a curated
/// subset of the post-action catalog — the ones that make sense as a single
/// tap). The full catalog is available in the reorderable Post Actions list.
let interactActionOptions: [(String, Any)] = [
    ("View post info", "post_info"), ("View thread", "thread"), ("Reply", "reply"),
    ("Quote", "quote"), ("Favorite", "favorite"), ("Boost", "boost"),
    ("Bookmark", "bookmark"), ("View media", "play_media"), ("Open links", "links"),
    ("Open in browser", "browser"), ("Copy", "copy"),
]

enum SettingRow {
    case movement(String)
    case postActions(String)
    case toggle(String, key: String, def: Bool)
    case picker(String, key: String, options: [(String, Any)], def: Any)
    case stepper(String, key: String, def: Int, min: Int, max: Int, step: Int)
    case slider(String, key: String, def: Int, min: Int, max: Int)
    case speech(String, category: String)
}

struct SettingPanel {
    let title: String
    var footer: String? = nil
    let rows: [SettingRow]
}

/// The root list of setting categories.
@MainActor
final class SettingsViewController: UITableViewController {
    private let state: AppState
    private var panels: [SettingPanel] = []

    init(state: AppState) {
        self.state = state
        super.init(style: .insetGrouped)
        title = "Settings"
        panels = Self.buildPanels(state: state)
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    override func tableView(_ tableView: UITableView,
                            numberOfRowsInSection section: Int) -> Int { panels.count }

    override func tableView(_ tableView: UITableView,
                            cellForRowAt indexPath: IndexPath) -> UITableViewCell {
        let cell = UITableViewCell(style: .default, reuseIdentifier: nil)
        var content = cell.defaultContentConfiguration()
        content.text = panels[indexPath.row].title
        cell.contentConfiguration = content
        cell.accessoryType = .disclosureIndicator
        return cell
    }

    override func tableView(_ tableView: UITableView, didSelectRowAt indexPath: IndexPath) {
        tableView.deselectRow(at: indexPath, animated: true)
        let panel = SettingsPanelViewController(state: state, panel: panels[indexPath.row])
        navigationController?.pushViewController(panel, animated: true)
    }

    private static func buildPanels(state: AppState) -> [SettingPanel] {
        let emojiOptions: [(String, Any)] = [("Off", "none"), ("Unicode emoji", "unicode"),
                                             ("Custom shortcodes", "mastodon"), ("Both", "both")]
        return [
            SettingPanel(title: "General", rows: [
                .toggle("Return key sends the post", key: "enter_to_send", def: false),
            ]),
            SettingPanel(title: "Timelines",
                    footer: "Check timelines for new posts on this interval; new posts play "
                          + "that timeline's sound.",
                    rows: [
                .stepper("Cache limit (posts)", key: "cache_limit", def: 200,
                         min: 0, max: 20000, step: 100),
                .picker("Auto-refresh", key: "auto_refresh_seconds",
                        options: [("Off", 0), ("Every 30 seconds", 30), ("Every minute", 60),
                                  ("Every 2 minutes", 120), ("Every 5 minutes", 300)],
                        def: 60),
                .toggle("Live streaming", key: "streaming_enabled", def: true),
                .toggle("Show mentions in Notifications",
                        key: "show_mentions_in_notifications", def: true),
                .toggle("Reverse order (newest posts at the bottom)",
                        key: "reverse_timelines", def: false),
                .toggle("Automatically load older posts", key: "auto_load_older", def: true),
                .toggle("Sync home position with the server (Mastodon)",
                        key: "sync_home_position", def: false),
                .picker("Tab bar position", key: "tab_bar_position",
                        options: [("Top of the screen", "top"),
                                  ("Bottom of the screen", "bottom")],
                        def: "bottom"),
                .movement("Movement Rotors"),
            ]),
            SettingPanel(title: "Audio", rows: [
                .toggle("Play sounds", key: "sounds_enabled", def: true),
                .toggle("Play a sound at the top/bottom of a list",
                        key: "boundary_sound", def: true),
                .picker("Soundpack", key: "soundpack",
                        options: state.soundpacks.map { ($0, $0) }, def: "Default"),
                .slider("Volume", key: "sound_volume", def: 100, min: 0, max: 100),
            ]),
            SettingPanel(title: "Earcons",
                    footer: "A short sound plays as you move onto a post that has each of "
                          + "these. Turn off any you don't want.",
                    rows: [
                .toggle("Image (post has an image)", key: "earcon_image", def: true),
                .toggle("Media (post has video, audio, or a GIF)", key: "earcon_media", def: true),
                .toggle("Mention (post mentions you)", key: "earcon_mention", def: true),
                .toggle("Pinned (post is pinned to a profile)", key: "earcon_pinned", def: true),
                .toggle("Poll (post has a poll)", key: "earcon_poll", def: true),
            ]),
            SettingPanel(title: "Speech",
                    footer: "Choose which details VoiceOver speaks, and their order, for "
                          + "each kind of row.",
                    rows: [
                .picker("Content warnings", key: "cw_mode",
                        options: [("Hide post text", "hide"), ("Show warning, then text", "show"),
                                  ("Ignore warning", "ignore")],
                        def: "hide"),
                .picker("Emoji in posts", key: "post_emoji_removal",
                        options: emojiOptions, def: "none"),
                .picker("Emoji in names", key: "name_emoji_removal",
                        options: emojiOptions, def: "none"),
                .stepper("Max usernames read per post (0 = all)",
                         key: "max_usernames_in_post", def: 0, min: 0, max: 20, step: 1),
                .toggle("Speak absolute times", key: "absolute_time", def: false),
                .speech("Post Fields", category: "status"),
                .speech("User Fields", category: "user"),
                .speech("Notification Fields", category: "notification"),
                .speech("Auto-read Fields", category: "autoread"),
                .speech("Copy Fields — Posts", category: "copy_status"),
                .speech("Copy Fields — Users", category: "copy_user"),
                .speech("Copy Fields — Notifications", category: "copy_notification"),
            ]),
            SettingPanel(title: "Behavior", rows: [
                .picker("Tap on a post", key: "enter_post_action",
                        options: interactActionOptions, def: "post_info"),
                .picker("Tap on a user", key: "enter_user_action",
                        options: [("User actions", "actions"), ("View profile", "profile"),
                                  ("View their timeline", "timeline")],
                        def: "actions"),
                .picker("Secondary action", key: "secondary_post_action",
                        options: interactActionOptions, def: "play_media"),
                .postActions("Post Actions"),
                .toggle("Keep the media player in the background",
                        key: "media_background", def: false),
                .toggle("Move extra mentions to the end of replies",
                        key: "reply_mentions_at_end", def: false),
            ]),
            SettingPanel(title: "Advanced", rows: [
                .stepper("API pages per fetch", key: "fetch_pages", def: 3,
                         min: 1, max: 10, step: 1),
            ]),
            SettingPanel(title: "Confirmation", footer: "Show a confirmation before each of these.",
                    rows: [
                .toggle("Boost", key: "confirm_boost", def: false),
                .toggle("Un-boost", key: "confirm_unboost", def: false),
                .toggle("Favorite", key: "confirm_favorite", def: false),
                .toggle("Un-favorite", key: "confirm_unfavorite", def: false),
                .toggle("Clearing a timeline", key: "confirm_clear_timeline", def: true),
                .toggle("Block", key: "confirm_block", def: true),
                .toggle("Un-block", key: "confirm_unblock", def: false),
                .toggle("Deleting a post", key: "confirm_delete_post", def: true),
            ]),
        ]
    }
}

// MARK: - One category's panel

@MainActor
final class SettingsPanelViewController: UITableViewController {
    private let state: AppState
    private let panel: SettingPanel

    init(state: AppState, panel: SettingPanel) {
        self.state = state
        self.panel = panel
        super.init(style: .insetGrouped)
        title = panel.title
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    // MARK: Value access

    private func boolVal(_ key: String, _ def: Bool) -> Bool {
        state.settingsRaw[key] as? Bool ?? def
    }
    private func intVal(_ key: String, _ def: Int) -> Int {
        (state.settingsRaw[key] as? NSNumber)?.intValue ?? def
    }
    private func anyVal(_ key: String, _ def: Any) -> Any { state.settingsRaw[key] ?? def }
    private func matches(_ a: Any, _ b: Any) -> Bool {
        (a as AnyObject).isEqual(b as AnyObject)
    }

    // MARK: Table

    override func tableView(_ tableView: UITableView,
                            numberOfRowsInSection section: Int) -> Int {
        panel.rows.count
    }
    override func tableView(_ tableView: UITableView,
                            titleForFooterInSection section: Int) -> String? {
        panel.footer
    }

    override func tableView(_ tableView: UITableView,
                            cellForRowAt indexPath: IndexPath) -> UITableViewCell {
        // The table is small and static; fresh cells keep the control wiring
        // trivial (no reuse bookkeeping).
        switch panel.rows[indexPath.row] {
        case let .toggle(title, key, def):
            return ToggleCell(title: title, on: boolVal(key, def)) { [weak self] on in
                self?.state.updateSettings { $0[key] = on }
            }
        case let .picker(title, key, options, def):
            let cell = UITableViewCell(style: .value1, reuseIdentifier: nil)
            var content = cell.defaultContentConfiguration()
            content.text = title
            let current = anyVal(key, def)
            let index = options.firstIndex { matches($0.1, current) } ?? 0
            content.secondaryText = options.indices.contains(index) ? options[index].0 : ""
            cell.contentConfiguration = content
            cell.accessoryType = .disclosureIndicator
            return cell
        case let .stepper(title, key, def, min, max, step):
            return StepperCell(title: title, value: intVal(key, def),
                               min: min, max: max, step: step) { [weak self] value in
                self?.state.updateSettings { $0[key] = value }
            }
        case let .slider(title, key, def, min, max):
            return SliderCell(title: title, value: intVal(key, def),
                              min: min, max: max) { [weak self] value in
                self?.state.updateSettings { $0[key] = value }
            }
        case let .speech(title, _), let .movement(title), let .postActions(title):
            let cell = UITableViewCell(style: .default, reuseIdentifier: nil)
            var content = cell.defaultContentConfiguration()
            content.text = title
            cell.contentConfiguration = content
            cell.accessoryType = .disclosureIndicator
            return cell
        }
    }

    override func tableView(_ tableView: UITableView, didSelectRowAt indexPath: IndexPath) {
        tableView.deselectRow(at: indexPath, animated: true)
        switch panel.rows[indexPath.row] {
        case let .toggle(_, key, def):
            let on = !boolVal(key, def)
            state.updateSettings { $0[key] = on }
            (tableView.cellForRow(at: indexPath) as? ToggleCell)?.set(on: on)
        case let .picker(title, key, options, def):
            let current = anyVal(key, def)
            let index = options.firstIndex { matches($0.1, current) } ?? 0
            let picker = OptionPickerViewController(title: title,
                                                    options: options.map { $0.0 },
                                                    selected: index) { [weak self] picked in
                self?.state.updateSettings { $0[key] = options[picked].1 }
                self?.tableView.reloadRows(at: [indexPath], with: .none)
            }
            navigationController?.pushViewController(picker, animated: true)
        case let .speech(title, category):
            guard state.speechCatalog != nil else { return }
            let fields = SpeechFieldsViewController(state: state, category: category,
                                                    title: title)
            navigationController?.pushViewController(fields, animated: true)
        case .movement:
            guard !state.movementCatalog.isEmpty else { return }
            navigationController?.pushViewController(
                MovementUnitsViewController(state: state), animated: true)
        case .postActions:
            guard !state.postActionCatalog.isEmpty else { return }
            navigationController?.pushViewController(
                PostActionsViewController(state: state), animated: true)
        default:
            break
        }
    }
}

// MARK: - Option picker (checkmark list)

@MainActor
final class OptionPickerViewController: UITableViewController {
    private let options: [String]
    private var selected: Int
    private let onPick: (Int) -> Void

    init(title: String, options: [String], selected: Int, onPick: @escaping (Int) -> Void) {
        self.options = options
        self.selected = selected
        self.onPick = onPick
        super.init(style: .insetGrouped)
        self.title = title
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    override func tableView(_ tableView: UITableView,
                            numberOfRowsInSection section: Int) -> Int { options.count }

    override func tableView(_ tableView: UITableView,
                            cellForRowAt indexPath: IndexPath) -> UITableViewCell {
        let cell = UITableViewCell(style: .default, reuseIdentifier: nil)
        var content = cell.defaultContentConfiguration()
        content.text = options[indexPath.row]
        cell.contentConfiguration = content
        cell.accessoryType = indexPath.row == selected ? .checkmark : .none
        if indexPath.row == selected {
            cell.accessibilityTraits.insert(.selected)
        }
        return cell
    }

    override func tableView(_ tableView: UITableView, didSelectRowAt indexPath: IndexPath) {
        selected = indexPath.row
        onPick(indexPath.row)
        navigationController?.popViewController(animated: true)
    }
}

// MARK: - Control cells

/// A switch row VoiceOver reads as one element ("<title>, on/off"); tapping the
/// row (or double-tapping with VoiceOver) toggles it.
final class ToggleCell: UITableViewCell {
    private let toggle = UISwitch()
    private let onChange: (Bool) -> Void

    init(title: String, on: Bool, onChange: @escaping (Bool) -> Void) {
        self.onChange = onChange
        super.init(style: .default, reuseIdentifier: nil)
        var content = defaultContentConfiguration()
        content.text = title
        contentConfiguration = content
        toggle.isOn = on
        toggle.addTarget(self, action: #selector(switched), for: .valueChanged)
        accessoryView = toggle
        selectionStyle = .none
        isAccessibilityElement = true
        accessibilityLabel = title
        accessibilityValue = on ? "On" : "Off"
        accessibilityTraits = .button
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    func set(on: Bool) {
        toggle.setOn(on, animated: true)
        accessibilityValue = on ? "On" : "Off"
    }

    @objc private func switched() {
        accessibilityValue = toggle.isOn ? "On" : "Off"
        onChange(toggle.isOn)
    }

    override func accessibilityActivate() -> Bool {
        toggle.setOn(!toggle.isOn, animated: true)
        switched()
        return true
    }
}

/// A numeric row exposed to VoiceOver as adjustable (swipe up/down to change).
final class StepperCell: UITableViewCell {
    private let stepper = UIStepper()
    private let valueLabel = UILabel()
    private let onChange: (Int) -> Void

    init(title: String, value: Int, min: Int, max: Int, step: Int,
         onChange: @escaping (Int) -> Void) {
        self.onChange = onChange
        super.init(style: .default, reuseIdentifier: nil)
        var content = defaultContentConfiguration()
        content.text = title
        contentConfiguration = content
        stepper.minimumValue = Double(min)
        stepper.maximumValue = Double(max)
        stepper.stepValue = Double(step)
        stepper.value = Double(value)
        stepper.addTarget(self, action: #selector(stepped), for: .valueChanged)
        valueLabel.text = String(value)
        valueLabel.font = .preferredFont(forTextStyle: .body)
        valueLabel.textColor = .secondaryLabel
        let row = UIStackView(arrangedSubviews: [valueLabel, stepper])
        row.spacing = 8
        row.alignment = .center
        row.frame = CGRect(x: 0, y: 0, width: 150, height: 32)
        accessoryView = row
        selectionStyle = .none
        isAccessibilityElement = true
        accessibilityLabel = title
        accessibilityValue = String(value)
        accessibilityTraits = .adjustable
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    @objc private func stepped() {
        let value = Int(stepper.value)
        valueLabel.text = String(value)
        accessibilityValue = String(value)
        onChange(value)
    }

    override func accessibilityIncrement() {
        stepper.value += stepper.stepValue
        stepped()
    }

    override func accessibilityDecrement() {
        stepper.value -= stepper.stepValue
        stepped()
    }
}

/// A slider row (volume); the slider itself is VoiceOver-adjustable.
final class SliderCell: UITableViewCell {
    private let slider = UISlider()
    private let onChange: (Int) -> Void

    init(title: String, value: Int, min: Int, max: Int, onChange: @escaping (Int) -> Void) {
        self.onChange = onChange
        super.init(style: .default, reuseIdentifier: nil)
        var content = defaultContentConfiguration()
        content.text = title
        contentConfiguration = content
        slider.minimumValue = Float(min)
        slider.maximumValue = Float(max)
        slider.value = Float(value)
        slider.accessibilityLabel = title
        slider.addTarget(self, action: #selector(slid), for: .valueChanged)
        slider.translatesAutoresizingMaskIntoConstraints = false
        slider.widthAnchor.constraint(equalToConstant: 160).isActive = true
        slider.frame = CGRect(x: 0, y: 0, width: 160, height: 32)
        accessoryView = slider
        selectionStyle = .none
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    @objc private func slid() { onChange(Int(slider.value)) }
}
