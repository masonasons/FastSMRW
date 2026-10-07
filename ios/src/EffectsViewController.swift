import UIKit

/// The player's audio effects: which are on, which of their settings you are adjusting,
/// and its value.
///
/// Laid out the way the FastPlay iOS app does it, because it is the shape that works
/// without sight: the effects are plain switches, and the two things you change while
/// listening — which setting, and its value — are sliders you can reach without hunting
/// through a list. Every string shown here (the effect names, the value as text) is
/// composed by the core, so this reads the same as the desktop apps.
final class EffectsViewController: UIViewController {
    private let state: AppState
    private let table = UITableView(frame: .zero, style: .insetGrouped)
    /// The parameters that currently do anything, in catalog order. Rebuilt whenever
    /// the catalog arrives, since an effect being switched off removes its settings.
    private var shown: [MediaParam] = []
    /// Which of `shown` the value slider is adjusting.
    private var selected = 0

    init(state: AppState) {
        self.state = state
        super.init(nibName: nil, bundle: nil)
        title = "Effects"
    }

    required init?(coder: NSCoder) { fatalError("not used") }

    override func viewDidLoad() {
        super.viewDidLoad()
        view.backgroundColor = .systemGroupedBackground
        table.dataSource = self
        table.delegate = self
        table.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(table)
        NSLayoutConstraint.activate([
            table.topAnchor.constraint(equalTo: view.safeAreaLayoutGuide.topAnchor),
            table.leadingAnchor.constraint(equalTo: view.leadingAnchor),
            table.trailingAnchor.constraint(equalTo: view.trailingAnchor),
            table.bottomAnchor.constraint(equalTo: view.bottomAnchor),
        ])

        state.onMediaEffects = { [weak self] in self?.reload() }
        state.getMediaEffects()
        reload()
    }

    deinit { state.onMediaEffects = nil }

    private func reload() {
        let was = shown.indices.contains(selected) ? shown[selected].key : nil
        shown = state.mediaEffects.params.filter { $0.applies(given: state.mediaEffects.effects) }
        // Keep adjusting the same setting across a reload where it still applies, so
        // switching an unrelated effect doesn't move you somewhere else.
        selected = was.flatMap { key in shown.firstIndex { $0.key == key } } ?? 0
        table.reloadData()
    }

    private var current: MediaParam? { shown.indices.contains(selected) ? shown[selected] : nil }
}

extension EffectsViewController: UITableViewDataSource, UITableViewDelegate {
    func numberOfSections(in tableView: UITableView) -> Int {
        state.mediaEffects.available ? 2 : 1
    }

    func tableView(_ tableView: UITableView, titleForHeaderInSection section: Int) -> String? {
        guard state.mediaEffects.available else { return nil }
        return section == 0 ? "Effects" : "Adjust"
    }

    func tableView(_ tableView: UITableView, titleForFooterInSection section: Int) -> String? {
        guard state.mediaEffects.available else { return nil }
        if section == 0 {
            return "An effect's settings can only be adjusted while it is on."
        }
        return shown.isEmpty
            ? "Turn an effect on above to adjust its settings."
            : "Setting chooses what to change; Value changes it. Both take effect at once."
    }

    func tableView(_ tableView: UITableView, numberOfRowsInSection section: Int) -> Int {
        guard state.mediaEffects.available else { return 1 }
        if section == 0 { return state.mediaEffects.effects.count }
        return shown.isEmpty ? 0 : 2 // the setting chooser, then its value
    }

    func tableView(_ tableView: UITableView, cellForRowAt indexPath: IndexPath) -> UITableViewCell {
        if !state.mediaEffects.available {
            let cell = UITableViewCell(style: .default, reuseIdentifier: nil)
            var content = cell.defaultContentConfiguration()
            content.text = "This build of FastSMRW has no FastPlay player."
            cell.contentConfiguration = content
            cell.selectionStyle = .none
            return cell
        }

        if indexPath.section == 0 {
            let effect = state.mediaEffects.effects[indexPath.row]
            let toggle = UISwitch()
            toggle.isOn = effect.enabled
            toggle.accessibilityLabel = effect.name
            toggle.addAction(UIAction { [weak self] _ in
                // The core answers with a fresh catalog, which reload() picks up.
                self?.state.setMediaEffect(effect.key, on: toggle.isOn)
            }, for: .valueChanged)
            return ToggleHostCell(title: effect.name, toggle: toggle)
        }

        // Row 0 picks the setting, row 1 changes it. Sliders rather than a list: these
        // are the two controls you reach for while something is playing.
        if indexPath.row == 0 {
            let slider = UISlider()
            slider.minimumValue = 0
            slider.maximumValue = Float(max(shown.count - 1, 1))
            slider.value = Float(selected)
            slider.isContinuous = false
            // VoiceOver reads the name, not the index, because "3 of 11" says nothing
            // about what you are about to change.
            slider.accessibilityLabel = "Setting"
            slider.accessibilityValue = current?.name ?? ""
            slider.addAction(UIAction { [weak self] _ in
                guard let self else { return }
                let index = Int(slider.value.rounded())
                guard self.shown.indices.contains(index) else { return }
                self.selected = index
                slider.accessibilityValue = self.shown[index].name
                // Only the value row changes; rebuilding the whole table would take
                // focus off the slider mid-adjustment.
                self.table.reloadRows(at: [IndexPath(row: 1, section: 1)], with: .none)
                UIAccessibility.post(notification: .announcement,
                                     argument: self.shown[index].name)
            }, for: .valueChanged)
            return SliderHostCell(title: "Setting", slider: slider)
        }

        let param = current
        let slider = UISlider()
        slider.minimumValue = param?.min ?? 0
        slider.maximumValue = param?.max ?? 1
        slider.value = param?.value ?? 0
        slider.isContinuous = false
        slider.accessibilityLabel = param.map { "Value, \($0.name)" } ?? "Value"
        slider.accessibilityValue = param?.display ?? ""
        slider.addAction(UIAction { [weak self] _ in
            guard let self, let param = self.current else { return }
            // Snap to the parameter's step: a choice parameter only has whole values,
            // and a free slider would land between two of them.
            let step = param.step > 0 ? param.step : 1
            let snapped = (((slider.value - param.min) / step).rounded() * step) + param.min
            slider.value = snapped
            self.state.setMediaParam(param.key, value: snapped)
            // The core speaks where it landed (it clamps), so nothing is announced here.
        }, for: .valueChanged)
        return SliderHostCell(title: param.map { "Value (\($0.name))" } ?? "Value", slider: slider)
    }
}
