//
// Copyright (c) 2026 Sunneva N. Mariu
//
// MSLMenu.swift
//
// The menu-bar app. Its menu shows live state for each component of the layout
// layer and for the pseudo-filesystems, and offers one-click toggles.
//
// The menu is rebuilt every time it opens rather than cached, so it can never
// show a state the system has since left - the layer is changed by mslctl and
// by mslxd as volumes come and go, so a cached view would go stale without any
// notification to invalidate it.
//
import Cocoa
import ServiceManagement

final class AppDelegate: NSObject, NSApplicationDelegate, NSMenuDelegate {
    private var statusItem: NSStatusItem!
    private var pending: [Component: Bool] = [:]

    func applicationDidFinishLaunching(_ note: Notification) {
        statusItem = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
        setIcon()

        let menu = NSMenu()
        menu.delegate = self
        statusItem.menu = menu

        registerLoginItemOnFirstLaunch()
    }

    /// A template SF Symbol, so macOS tints it correctly for a light or dark
    /// menu bar without shipping two versions of the art.
    private func setIcon() {
        guard let button = statusItem?.button else { return }

        if let image = NSImage(systemSymbolName: "folder.badge.gearshape",
                               accessibilityDescription: "mSL/XNU") {
            image.isTemplate = true
            button.image = image
            button.title = ""
        } else {
            button.image = nil
            button.title = "mSL"
        }
    }

    // MARK: - Menu

    func menuNeedsUpdate(_ menu: NSMenu) {
        menu.removeAllItems()

        let state = MSLState()

        addAction(menu, "Preferences…", #selector(openPreferences))
        menu.addItem(.separator())

        guard state.available else {
            // mslctl missing or unrunnable: say so plainly rather than
            // presenting an empty menu that looks like "nothing is enabled".
            addDisabled(menu, "mslctl not found")
            addDisabled(menu, "Install with: sudo make install")
            menu.addItem(.separator())
            addAction(menu, "Quit", #selector(NSApplication.terminate(_:)), target: NSApp)
            return
        }

        addDisabled(menu, "mSL/XNU \(state.version)")
        menu.addItem(.separator())

        for component in Component.allCases {
            let item = addAction(menu, component.path, #selector(toggle(_:)))
            item.state = state.enabled(component) ? .on : .off
            item.representedObject = component.rawValue
            item.isEnabled = !state.conflicting(component)

            addDisabled(menu, "    " + state.detail(component))
        }

        // Offer the fix for the one inconsistency the user can act on.
        if state.int("media.stale") > 0 {
            menu.addItem(.separator())
            addAction(menu, "Clear \(state.int("media.stale")) stale link(s)",
                      #selector(clearStale))
        }

        menu.addItem(.separator())
        addDisabled(menu, "Pseudo-filesystems")
        addDisabled(menu, "    /proc — \(state.pseudofs("proc"))")
        addDisabled(menu, "    /sys — \(state.pseudofs("sys"))")

        menu.addItem(.separator())
        addDisabled(menu, "mslxd — " + (state.daemonRunning ? "running" : "not running"))

        menu.addItem(.separator())
        if #available(macOS 13.0, *) {
            let loginItem = addAction(menu, "Open at Login", #selector(toggleLoginItem))
            loginItem.state = loginEnabled ? .on : .off
        }
        addAction(menu, "Quit", #selector(NSApplication.terminate(_:)), target: NSApp)
    }

    private func addDisabled(_ menu: NSMenu, _ title: String) {
        let item = NSMenuItem(title: title, action: nil, keyEquivalent: "")
        item.isEnabled = false
        menu.addItem(item)
    }

    @discardableResult
    private func addAction(_ menu: NSMenu, _ title: String, _ sel: Selector,
                           target: AnyObject? = nil) -> NSMenuItem {
        let item = NSMenuItem(title: title, action: sel, keyEquivalent: "")
        item.target = target ?? self
        menu.addItem(item)
        return item
    }

    // MARK: - Actions

    @objc private func openPreferences() {
        NSWorkspace.shared.open(URL(fileURLWithPath: "/Library/PreferencePanes/mSL.prefPane"))
    }

    @objc private func toggle(_ sender: NSMenuItem) {
        guard let raw = sender.representedObject as? String,
              let component = Component(rawValue: raw) else { return }

        let state = MSLState()
        let want = !state.enabled(component)

        guard MSLState.apply([component: want]) else { return }

        // Changing /mnt or /media edits synthetic.conf, which is only read at
        // boot. Saying so at the moment of the change is far more useful than
        // leaving the user to wonder why /media did not appear.
        if component.needsReboot {
            let fresh = MSLState()
            if fresh.rebootPending(component) || (!want && fresh.enabled(component) == false) {
                notifyRestartNeeded(component, enabling: want)
            }
        }
    }

    @objc private func clearStale() {
        MSLState.syncMedia()
    }

    private func notifyRestartNeeded(_ component: Component, enabling: Bool) {
        let alert = NSAlert()
        alert.messageText = "Restart required"
        alert.informativeText = enabling
            ? "\(component.path) has been enabled, but macOS only creates "
            + "root-level directories at startup. It will appear after you restart."
            : "\(component.path) has been disabled. It will disappear after you restart."
        alert.addButton(withTitle: "OK")
        alert.runModal()
    }

    // MARK: - Login item

    private var loginEnabled: Bool {
        if #available(macOS 13.0, *) {
            return SMAppService.mainApp.status == .enabled
        }
        return false
    }

    /// Register to open at login exactly once, on first launch, so a fresh
    /// install starts on later reboots. Tracked in UserDefaults so a user who
    /// turns it off is never silently re-enrolled.
    private func registerLoginItemOnFirstLaunch() {
        guard #available(macOS 13.0, *) else { return }
        let key = "MSLDidAutoRegisterLoginItem"
        let defaults = UserDefaults.standard
        guard !defaults.bool(forKey: key) else { return }
        defaults.set(true, forKey: key)
        if SMAppService.mainApp.status != .enabled {
            try? SMAppService.mainApp.register()
        }
    }

    @objc private func toggleLoginItem() {
        guard #available(macOS 13.0, *) else { return }
        do {
            if SMAppService.mainApp.status == .enabled {
                try SMAppService.mainApp.unregister()
            } else {
                try SMAppService.mainApp.register()
            }
        } catch {
            let alert = NSAlert()
            alert.messageText = "Could not change the Open at Login setting."
            alert.informativeText = error.localizedDescription
            alert.runModal()
        }
    }
}
