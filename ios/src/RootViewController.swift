//
//  RootViewController.swift
//
//  Container that shows the add-account screen until an account exists, then
//  the main timeline screen. Also owns the app-level event routes that outlive
//  any one screen: auth results and open_url (which is either the OAuth
//  authorize page — routed to the add-account screen's in-app browser — or a
//  post link for Safari).
//

import UIKit

@MainActor
final class RootViewController: UIViewController {
    private let state: AppState
    private var mainNav: UINavigationController?
    private weak var mainVC: MainViewController?
    private var addAccountNav: UINavigationController?
    /// Whichever add-account screen is frontmost (root child or modal sheet).
    private weak var activeAddAccount: AddAccountViewController?

    init(state: AppState) {
        self.state = state
        super.init(nibName: nil, bundle: nil)
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    override func viewDidLoad() {
        super.viewDidLoad()
        view.backgroundColor = .systemBackground

        state.onAccountsChanged = { [weak self] in self?.refreshChildren() }
        state.onAuthResult = { [weak self] result in self?.handleAuthResult(result) }
        state.onOpenURL = { [weak self] url in
            if let add = self?.activeAddAccount, add.handleOpenURL(url) { return }
            UIApplication.shared.open(url)
        }
        refreshChildren()
    }

    private func refreshChildren() {
        let hasAccounts = !state.accounts.isEmpty
        if hasAccounts {
            // A login just succeeded from the modal sheet? Dismiss it.
            if presentedViewController is UINavigationController,
               activeAddAccount != nil, activeAddAccount?.isModal == true {
                dismiss(animated: true)
            }
            removeChildNav(&addAccountNav)
            if mainNav == nil {
                let main = MainViewController(state: state)
                main.onAddAccount = { [weak self] in self?.presentAddAccount() }
                mainVC = main
                mainNav = embed(main)
            }
        } else {
            removeChildNav(&mainNav)
            if addAccountNav == nil {
                let add = AddAccountViewController(state: state)
                activeAddAccount = add
                addAccountNav = embed(add)
            }
        }
    }

    /// Add Account chosen from the main screen: present as a sheet.
    private func presentAddAccount() {
        let add = AddAccountViewController(state: state)
        add.isModal = true
        activeAddAccount = add
        let nav = UINavigationController(rootViewController: add)
        present(nav, animated: true)
    }

    private func handleAuthResult(_ result: AuthResult) {
        activeAddAccount?.loginFinished()
        if !result.ok {
            let message = result.error ?? "Login failed."
            showError(message, on: presentedViewController ?? self)
        }
        // Success needs no action here: accounts_changed swaps the UI.
    }

    // MARK: Child management

    private func embed(_ root: UIViewController) -> UINavigationController {
        let nav = UINavigationController(rootViewController: root)
        addChild(nav)
        nav.view.frame = view.bounds
        nav.view.autoresizingMask = [.flexibleWidth, .flexibleHeight]
        view.addSubview(nav.view)
        nav.didMove(toParent: self)
        return nav
    }

    private func removeChildNav(_ nav: inout UINavigationController?) {
        guard let child = nav else { return }
        child.willMove(toParent: nil)
        child.view.removeFromSuperview()
        child.removeFromParent()
        nav = nil
    }
}

extension RootViewController: MagicTapResponder {
    /// On a post → its configurable secondary action; anywhere else → compose.
    func performMagicTap() {
        if mainVC?.isPostFocused == true {
            state.performAction("SecondaryAction")
        } else {
            state.requestCompose(mode: "new")
        }
    }
}
