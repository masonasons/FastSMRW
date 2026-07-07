//
//  MainMenu.swift
//
//  Builds the application main menu in code. Status actions target the first
//  responder (the TimelineViewController / MainWindowController) so their key
//  equivalents work whenever the timeline has focus. Trimmed to the M1 feature
//  set; more menus land with settings, lists, media, etc.
//

import AppKit

enum MainMenu {
    static func build() -> NSMenu {
        let mainMenu = NSMenu()

        // App menu
        let appItem = NSMenuItem()
        mainMenu.addItem(appItem)
        let appMenu = NSMenu()
        appItem.submenu = appMenu
        appMenu.addItem(withTitle: "About FastSMRW",
                        action: #selector(NSApplication.orderFrontStandardAboutPanel(_:)),
                        keyEquivalent: "")
        appMenu.addItem(withTitle: "Check for Updates…",
                        action: #selector(AppDelegate.checkForUpdates(_:)), keyEquivalent: "")
        appMenu.addItem(.separator())
        appMenu.addItem(withTitle: "Settings…", action: #selector(AppDelegate.showSettings(_:)),
                        keyEquivalent: ",")
        appMenu.addItem(.separator())
        appMenu.addItem(withTitle: "Hide FastSMRW", action: #selector(NSApplication.hide(_:)),
                        keyEquivalent: "h")
        appMenu.addItem(.separator())
        appMenu.addItem(withTitle: "Quit FastSMRW", action: #selector(NSApplication.terminate(_:)),
                        keyEquivalent: "q")

        // File menu
        let fileItem = NSMenuItem()
        mainMenu.addItem(fileItem)
        let fileMenu = NSMenu(title: "File")
        fileItem.submenu = fileMenu
        fileMenu.addItem(withTitle: "New Post",
                         action: #selector(MainWindowController.composePost(_:)), keyEquivalent: "n")
        let refresh = fileMenu.addItem(withTitle: "Refresh Timeline",
                                       action: #selector(MainWindowController.refreshTimeline(_:)),
                                       keyEquivalent: "r")
        refresh.keyEquivalentModifierMask = [.command]
        fileMenu.addItem(.separator())
        fileMenu.addItem(withTitle: "Close", action: #selector(NSWindow.performClose(_:)),
                         keyEquivalent: "w")

        // Edit menu (standard editing for text fields / compose)
        let editItem = NSMenuItem()
        mainMenu.addItem(editItem)
        let editMenu = NSMenu(title: "Edit")
        editItem.submenu = editMenu
        editMenu.addItem(withTitle: "Cut", action: #selector(NSText.cut(_:)), keyEquivalent: "x")
        editMenu.addItem(withTitle: "Copy", action: #selector(NSText.copy(_:)), keyEquivalent: "c")
        editMenu.addItem(withTitle: "Paste", action: #selector(NSText.paste(_:)), keyEquivalent: "v")
        editMenu.addItem(withTitle: "Select All", action: #selector(NSText.selectAll(_:)),
                         keyEquivalent: "a")

        // Status menu — the timeline's single-key shortcuts, discoverable + spoken.
        let statusItem = NSMenuItem()
        mainMenu.addItem(statusItem)
        let statusMenu = NSMenu(title: "Status")
        statusItem.submenu = statusMenu
        func add(_ title: String, _ action: Selector, _ key: String,
                 _ mask: NSEvent.ModifierFlags = []) {
            let mi = statusMenu.addItem(withTitle: title, action: action, keyEquivalent: key)
            mi.keyEquivalentModifierMask = mask
        }
        add("Reply", #selector(MainWindowController.replyToSelection(_:)), "r")
        add("Boost", #selector(MainWindowController.boostSelection(_:)), "b")
        add("Favorite", #selector(MainWindowController.favoriteSelection(_:)), "f")
        add("Quote", #selector(MainWindowController.quoteSelection(_:)), "q")
        add("Edit", #selector(MainWindowController.editSelection(_:)), "e")
        let delete = statusMenu.addItem(withTitle: "Delete Post",
                                        action: #selector(MainWindowController.deleteSelection(_:)),
                                        keyEquivalent: "\u{8}")
        delete.keyEquivalentModifierMask = [.command]
        add("Pin to Profile", #selector(MainWindowController.pinPostSelection(_:)), "")
        statusMenu.addItem(.separator())
        add("Speak User", #selector(MainWindowController.speakUserForSelection(_:)), ";")
        add("Speak Referenced Reply", #selector(MainWindowController.speakReplyForSelection(_:)), ";", [.command])
        statusMenu.addItem(.separator())
        add("Post Info…", #selector(MainWindowController.showPostInfo(_:)), "i", [.command])
        add("View Media…", #selector(MainWindowController.playMediaForSelection(_:)), "\r", [.shift])
        add("Open Link…", #selector(MainWindowController.openLinksForSelection(_:)), "\r", [.command])
        add("View Thread", #selector(MainWindowController.viewThread(_:)), " ")
        add("Open User Timeline", #selector(MainWindowController.openUserTimeline(_:)), "u")
        add("Open User Profile", #selector(MainWindowController.openUserProfile(_:)), "u", [.command])
        add("Followers", #selector(MainWindowController.openFollowers(_:)), "")
        add("Following", #selector(MainWindowController.openFollowing(_:)), "")
        add("Follow Hashtag…", #selector(MainWindowController.followHashtag(_:)), "")
        statusMenu.addItem(.separator())
        add("Open in Browser", #selector(MainWindowController.openSelectionInBrowser(_:)), "")

        // Timeline menu — ⌘1…⌘9 jump to a timeline.
        let timelineItem = NSMenuItem()
        mainMenu.addItem(timelineItem)
        let timelineMenu = NSMenu(title: "Timeline")
        timelineItem.submenu = timelineMenu
        let newTimeline = timelineMenu.addItem(withTitle: "New Timeline…",
                                               action: #selector(MainWindowController.newTimeline(_:)),
                                               keyEquivalent: "t")
        newTimeline.keyEquivalentModifierMask = [.command]
        timelineMenu.addItem(withTitle: "Close Timeline",
                             action: #selector(MainWindowController.closeTimeline(_:)),
                             keyEquivalent: "")
        let refreshAll = timelineMenu.addItem(withTitle: "Refresh All Timelines",
                                              action: #selector(MainWindowController.refreshAll(_:)),
                                              keyEquivalent: "r")
        refreshAll.keyEquivalentModifierMask = [.command, .shift]
        let goBack = timelineMenu.addItem(withTitle: "Go Back",
                                          action: #selector(MainWindowController.goBack(_:)),
                                          keyEquivalent: "z")
        goBack.keyEquivalentModifierMask = [.command]
        let clearTimeline = timelineMenu.addItem(withTitle: "Clear Timeline",
                             action: #selector(MainWindowController.clearTimeline(_:)),
                             keyEquivalent: "\u{8}")
        clearTimeline.keyEquivalentModifierMask = [.command, .shift]
        let clearAll = timelineMenu.addItem(withTitle: "Clear All Timelines",
                             action: #selector(MainWindowController.clearAllTimelines(_:)),
                             keyEquivalent: "\u{8}")
        clearAll.keyEquivalentModifierMask = [.command, .shift, .option]
        let filter = timelineMenu.addItem(withTitle: "Filter Timeline…",
                                          action: #selector(MainWindowController.filterTimeline(_:)),
                                          keyEquivalent: "l")
        filter.keyEquivalentModifierMask = [.command]
        timelineMenu.addItem(.separator())
        timelineMenu.addItem(withTitle: "Pin / Unpin Timeline",
                             action: #selector(MainWindowController.togglePin(_:)),
                             keyEquivalent: "")
        let moveUp = timelineMenu.addItem(withTitle: "Move Timeline Up",
                                          action: #selector(MainWindowController.moveTimelineUp(_:)),
                                          keyEquivalent: "")
        moveUp.keyEquivalentModifierMask = [.command, .option]
        moveUp.keyEquivalent = String(UnicodeScalar(NSUpArrowFunctionKey)!)
        let moveDown = timelineMenu.addItem(withTitle: "Move Timeline Down",
                                            action: #selector(MainWindowController.moveTimelineDown(_:)),
                                            keyEquivalent: "")
        moveDown.keyEquivalentModifierMask = [.command, .option]
        moveDown.keyEquivalent = String(UnicodeScalar(NSDownArrowFunctionKey)!)
        timelineMenu.addItem(.separator())
        for number in 1...9 {
            let item = timelineMenu.addItem(withTitle: "Go to Timeline \(number)",
                                            action: #selector(MainWindowController.selectTimelineNumber(_:)),
                                            keyEquivalent: "\(number)")
            item.keyEquivalentModifierMask = [.command]
            item.tag = number
        }

        // Account menu
        let accountItem = NSMenuItem()
        mainMenu.addItem(accountItem)
        let accountMenu = NSMenu(title: "Account")
        accountItem.submenu = accountMenu
        let addAccount = accountMenu.addItem(withTitle: "Add Account…",
                                             action: #selector(AppDelegate.showAddAccount(_:)),
                                             keyEquivalent: "a")
        addAccount.keyEquivalentModifierMask = [.command, .shift]
        accountMenu.addItem(.separator())
        accountMenu.addItem(withTitle: "Followed Hashtags…",
                            action: #selector(MainWindowController.manageHashtags(_:)),
                            keyEquivalent: "")
        accountMenu.addItem(withTitle: "Manage Lists…",
                            action: #selector(MainWindowController.manageLists(_:)),
                            keyEquivalent: "")
        accountMenu.addItem(withTitle: "Server Filters…",
                            action: #selector(MainWindowController.manageServerFilters(_:)),
                            keyEquivalent: "")
        accountMenu.addItem(.separator())
        accountMenu.addItem(withTitle: "Remove Current Account…",
                            action: #selector(MainWindowController.removeCurrentAccount(_:)),
                            keyEquivalent: "")
        accountMenu.addItem(.separator())
        let prev = accountMenu.addItem(withTitle: "Previous Account",
                                       action: #selector(MainWindowController.previousAccount(_:)),
                                       keyEquivalent: "[")
        prev.keyEquivalentModifierMask = [.command]
        let next = accountMenu.addItem(withTitle: "Next Account",
                                       action: #selector(MainWindowController.nextAccount(_:)),
                                       keyEquivalent: "]")
        next.keyEquivalentModifierMask = [.command]

        // Window menu
        let windowItem = NSMenuItem()
        mainMenu.addItem(windowItem)
        let windowMenu = NSMenu(title: "Window")
        windowItem.submenu = windowMenu
        windowMenu.addItem(withTitle: "Minimize", action: #selector(NSWindow.performMiniaturize(_:)),
                           keyEquivalent: "m")
        NSApp.windowsMenu = windowMenu

        return mainMenu
    }
}
