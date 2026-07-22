//
//  AddAccountViewController.swift
//
//  Add a Mastodon account (OAuth in an in-app browser session; the core emits
//  the authorize URL and the fastsm:// redirect completes it) or a Bluesky
//  account (handle + app password). Shown as the root screen when no accounts
//  exist, or as a sheet from the main screen's menu.
//

import AuthenticationServices
import UIKit

@MainActor
final class AddAccountViewController: UIViewController {
    private let state: AppState
    /// True when presented as a sheet over the main screen (dismissed by Root
    /// on success) rather than as the app's root child.
    var isModal = false

    private let platformControl = UISegmentedControl(items: ["Mastodon", "Bluesky"])
    private let instanceField = UITextField()
    private let serviceField = UITextField()
    private let handleField = UITextField()
    private let passwordField = UITextField()
    private let submitButton = UIButton(type: .system)
    private let spinner = UIActivityIndicatorView(style: .medium)
    private let mastodonStack = UIStackView()
    private let blueskyStack = UIStackView()

    /// Set while a login is in flight; open_url events are routed here.
    private var awaitingLogin = false
    private var authSession: ASWebAuthenticationSession?

    init(state: AppState) {
        self.state = state
        super.init(nibName: nil, bundle: nil)
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    override func viewDidLoad() {
        super.viewDidLoad()
        title = "Add Account"
        view.backgroundColor = .systemBackground
        if isModal {
            navigationItem.leftBarButtonItem = UIBarButtonItem(
                barButtonSystemItem: .cancel, target: self, action: #selector(cancelTapped))
        }

        platformControl.selectedSegmentIndex = 0
        platformControl.addTarget(self, action: #selector(platformChanged), for: .valueChanged)

        configure(field: instanceField, placeholder: "Instance, e.g. mastodon.social",
                  label: "Instance")
        instanceField.keyboardType = .URL

        configure(field: serviceField, placeholder: "https://bsky.social", label: "Service")
        serviceField.text = "https://bsky.social"
        serviceField.keyboardType = .URL
        configure(field: handleField, placeholder: "you.bsky.social", label: "Handle")
        configure(field: passwordField, placeholder: "App password", label: "App password")
        passwordField.isSecureTextEntry = true

        submitButton.setTitle("Log In", for: .normal)
        submitButton.addTarget(self, action: #selector(submit), for: .touchUpInside)
        spinner.hidesWhenStopped = true

        mastodonStack.axis = .vertical
        mastodonStack.spacing = 12
        mastodonStack.addArrangedSubview(instanceField)

        blueskyStack.axis = .vertical
        blueskyStack.spacing = 12
        blueskyStack.isHidden = true
        for field in [serviceField, handleField, passwordField] {
            blueskyStack.addArrangedSubview(field)
        }

        // No scroll view around the form (VoiceOver navigates plain stacks much
        // more reliably) — it's short enough to fit any iPhone.
        let stack = UIStackView(arrangedSubviews: [platformControl, mastodonStack,
                                                   blueskyStack, submitButton, spinner])
        stack.axis = .vertical
        stack.spacing = 16
        stack.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(stack)
        NSLayoutConstraint.activate([
            stack.topAnchor.constraint(equalTo: view.safeAreaLayoutGuide.topAnchor, constant: 20),
            stack.leadingAnchor.constraint(equalTo: view.layoutMarginsGuide.leadingAnchor),
            stack.trailingAnchor.constraint(equalTo: view.layoutMarginsGuide.trailingAnchor),
        ])
    }

    private func configure(field: UITextField, placeholder: String, label: String) {
        field.placeholder = placeholder
        field.borderStyle = .roundedRect
        field.autocapitalizationType = .none
        field.autocorrectionType = .no
        field.accessibilityLabel = label
    }

    @objc private func platformChanged() {
        let mastodon = platformControl.selectedSegmentIndex == 0
        mastodonStack.isHidden = !mastodon
        blueskyStack.isHidden = mastodon
        submitButton.setTitle(mastodon ? "Log In" : "Add Account", for: .normal)
    }

    @objc private func cancelTapped() { dismiss(animated: true) }

    @objc private func submit() {
        view.endEditing(true)
        if platformControl.selectedSegmentIndex == 0 {
            let instance = (instanceField.text ?? "").trimmingCharacters(in: .whitespaces)
            guard !instance.isEmpty else { return }
            setBusy(true)
            state.beginMastodonLogin(instance: instance)
        } else {
            let service = (serviceField.text ?? "").trimmingCharacters(in: .whitespaces)
            let handle = (handleField.text ?? "").trimmingCharacters(in: .whitespaces)
            let password = passwordField.text ?? ""
            guard !handle.isEmpty, !password.isEmpty else { return }
            setBusy(true)
            state.addBluesky(service: service.isEmpty ? "https://bsky.social" : service,
                             handle: handle, appPassword: password)
        }
    }

    private func setBusy(_ busy: Bool) {
        awaitingLogin = busy
        submitButton.isEnabled = !busy
        if busy { spinner.startAnimating() } else { spinner.stopAnimating() }
    }

    /// Root routes auth_result here: clear the busy state (on success the
    /// accounts_changed event replaces this screen; on failure Root shows the
    /// error and the user can retry).
    func loginFinished() { setBusy(false) }

    /// Root routes open_url events here while a login is in flight: show the
    /// authorize page in an in-app browser session whose fastsm:// callback
    /// completes the login. Returns false when no login is pending (the URL is
    /// a post link — the caller opens it in Safari).
    func handleOpenURL(_ url: URL) -> Bool {
        guard awaitingLogin, platformControl.selectedSegmentIndex == 0 else { return false }
        let session = ASWebAuthenticationSession(url: url, callbackURLScheme: "fastsm") {
            [weak self] callbackURL, error in
            Task { @MainActor in
                guard let self else { return }
                guard let callbackURL,
                      let code = URLComponents(url: callbackURL, resolvingAgainstBaseURL: false)?
                          .queryItems?.first(where: { $0.name == "code" })?.value else {
                    // Cancelled or failed before the redirect: just re-enable.
                    self.setBusy(false)
                    _ = error
                    return
                }
                self.state.finishMastodonLogin(code: code)
            }
        }
        session.presentationContextProvider = self
        self.authSession = session
        if !session.start() { setBusy(false); return false }
        return true
    }
}

extension AddAccountViewController: ASWebAuthenticationPresentationContextProviding {
    nonisolated func presentationAnchor(for session: ASWebAuthenticationSession)
        -> ASPresentationAnchor {
        MainActor.assumeIsolated { self.view.window ?? ASPresentationAnchor() }
    }
}
