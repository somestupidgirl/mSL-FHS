//
// Copyright (c) 2026 Sunneva N. Mariu
//
// MSLMenu.swift
//
// The menu-bar app. It lists every root-level directory with a status dot, and
// hovering one opens a dropdown for that node: an On/Off toggle for the mSL
// components, a Show/Hide toggle for its Finder visibility, a status line, and
// "Open in Finder". Below the directories it reports the pseudo-filesystems,
// which it only observes.
//
// The whole menu is rebuilt every time it opens rather than cached: the layer
// is changed by mslctl, by mslxd as volumes come and go, and by the Finder
// itself, so a cached view would drift with no notification to invalidate it.
//
import Cocoa
import ServiceManagement

final class AppDelegate: NSObject, NSApplicationDelegate, NSMenuDelegate {
    private var statusItem: NSStatusItem!

    func applicationDidFinishLaunching(_ note: Notification) {
        statusItem = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
        setIcon()

        let menu = NSMenu()
        menu.delegate = self
        statusItem.menu = menu

        registerLoginItemOnFirstLaunch()
    }

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

    // MARK: - Building the menu

    func menuNeedsUpdate(_ menu: NSMenu) {
        menu.removeAllItems()

        let state = MSLState()

        guard state.available else {
            addDisabled(menu, "mslctl not found")
            addDisabled(menu, "Install with: sudo make install")
            menu.addItem(.separator())
            addAction(menu, "Quit", #selector(NSApplication.terminate(_:)), target: NSApp)
            return
        }

        addDisabled(menu, "mSL/XNU \(state.version)")
        menu.addItem(.separator())

        // A quick health line, matching the mockup's System / Daemon rows.
        let missing = state.nodes.contains { $0.component != nil && !$0.exists && state.enabled($0.component!) }
        addDisabled(menu, "System: " + (missing ? "Incomplete" : "Installed"))
        addDisabled(menu, "Daemon: " + (state.daemonRunning ? "Running" : "Not Running"))
        menu.addItem(.separator())

        addAction(menu, "Preferences…", #selector(openPreferences))
        menu.addItem(.separator())

        addDisabled(menu, "Directories:")
        for node in state.nodes {
            addNode(menu, node, state)
        }
        menu.addItem(.separator())

        addDisabled(menu, "Pseudo-Filesystems:")
        addPseudoFS(menu, "devfs", "/dev", state)
        addPseudoFS(menu, "procfs", "/proc", state)
        addPseudoFS(menu, "sysfs", "/sys", state)
        menu.addItem(.separator())

        if #available(macOS 13.0, *) {
            let loginItem = addAction(menu, "Open at Login", #selector(toggleLoginItem))
            loginItem.state = loginEnabled ? .on : .off
        }
        menu.addItem(.separator())
        addAction(menu, "About mSL/XNU", #selector(showAbout))
        addAction(menu, "Quit", #selector(NSApplication.terminate(_:)),
                  target: NSApp, key: "q")
    }

    /// One directory row: a coloured dot, the path, and a dropdown.
    private func addNode(_ menu: NSMenu, _ node: NodeInfo, _ state: MSLState) {
        let item = NSMenuItem(title: "\(dot(state.nodeOn(node)))  \(node.path)",
                              action: nil, keyEquivalent: "")
        item.submenu = nodeSubmenu(node, state)
        menu.addItem(item)
    }

    private func nodeSubmenu(_ node: NodeInfo, _ state: MSLState) -> NSMenu {
        let sub = NSMenu()

        // On/Off, only for the mSL-managed components.
        if let component = node.component {
            let onOff = NSMenuItem(title: "mSL: " + (state.enabled(component) ? "On" : "Off"),
                                   action: #selector(toggleComponent(_:)), keyEquivalent: "")
            onOff.target = self
            onOff.representedObject = node.name
            onOff.state = state.enabled(component) ? .on : .off
            onOff.isEnabled = !state.conflicting(component)
            sub.addItem(onOff)
        }

        // Show/Hide, for any node that exists. Disabled - with the reason shown
        // beneath - when the platform will not permit the change, so the user
        // is never offered a toggle that silently does nothing.
        if node.exists {
            let canChange = node.lock.canChange
            let show = NSMenuItem(
                title: "Finder: " + (node.hidden ? "Hidden" : "Visible"),
                action: canChange ? #selector(toggleVisibility(_:)) : nil,
                keyEquivalent: "")
            show.target = self
            show.representedObject = node.name
            show.state = node.hidden ? .off : .on
            show.isEnabled = canChange
            sub.addItem(show)

            if !canChange, let reason = node.lock.reason {
                addDisabled(sub, "    \(reason)")
            }
        }

        sub.addItem(.separator())
        for line in state.nodeDetailLines(node) {
            addDisabled(sub, line)
        }

        // Open in Finder, for anything that actually exists.
        if node.exists {
            sub.addItem(.separator())
            let open = NSMenuItem(title: "Open in Finder",
                                  action: #selector(openInFinder(_:)), keyEquivalent: "")
            open.target = self
            open.representedObject = node.path
            sub.addItem(open)
        }

        return sub
    }

    /// A pseudo-filesystem row. The key derived from the path ("/proc" -> "proc")
    /// is what the porcelain uses, so the dot and the text come from the same
    /// state rather than one being inferred from the other's wording.
    private func addPseudoFS(_ menu: NSMenu, _ name: String, _ path: String,
                             _ state: MSLState) {
        let key = String(path.dropFirst())
        let mounted = state.pseudofsMounted(key)
        let item = NSMenuItem(
            title: "\(dot(mounted))  \(name): "
                 + (mounted ? "Mounted at \(path)" : state.pseudofs(key)),
            action: nil, keyEquivalent: "")
        item.isEnabled = false
        menu.addItem(item)
    }

    // MARK: - Small builders

    /// A status dot as text, green (on) or red (off). Kept in the title rather
    /// than set as item.image on purpose: an item image adds a leading gutter
    /// that pushes the imageless section headers out of alignment with the rest
    /// of the menu. Emoji in the title has no such gutter, and matches the
    /// intended design.
    private func dot(_ on: Bool) -> String {
        on ? "🟢" : "🔴"
    }

    private func addDisabled(_ menu: NSMenu, _ title: String) {
        let item = NSMenuItem(title: title, action: nil, keyEquivalent: "")
        item.isEnabled = false
        menu.addItem(item)
    }

    @discardableResult
    private func addAction(_ menu: NSMenu, _ title: String, _ sel: Selector,
                           target: AnyObject? = nil, key: String = "") -> NSMenuItem {
        let item = NSMenuItem(title: title, action: sel, keyEquivalent: key)
        item.target = target ?? self
        menu.addItem(item)
        return item
    }

    // MARK: - Actions

    @objc private func openPreferences() {
        NSWorkspace.shared.open(URL(fileURLWithPath: "/Library/PreferencePanes/mSL.prefPane"))
    }

    @objc private func toggleComponent(_ sender: NSMenuItem) {
        guard let name = sender.representedObject as? String,
              let component = Component(rawValue: name) else { return }

        let want = !MSLState().enabled(component)
        guard MSLState.apply([component: want]) else { return }

        if component.needsReboot, MSLState().rebootPending(component) {
            notifyRestart(component, enabling: want)
        }
    }

    @objc private func toggleVisibility(_ sender: NSMenuItem) {
        guard let name = sender.representedObject as? String else { return }

        // The porcelain flag is the source of truth for the current state.
        let node = MSLState().nodes.first { $0.name == name }
        let makeVisible = node?.hidden ?? true
        MSLState.setVisible(name, makeVisible)
    }

    @objc private func openInFinder(_ sender: NSMenuItem) {
        guard let path = sender.representedObject as? String else { return }
        NSWorkspace.shared.selectFile(nil, inFileViewerRootedAtPath: path)
    }

    private func notifyRestart(_ component: Component, enabling: Bool) {
        let alert = NSAlert()
        alert.messageText = "Restart required"
        alert.informativeText = enabling
            ? "\(component.path) has been enabled, but macOS only creates "
            + "root-level directories at startup. It will appear after you restart."
            : "\(component.path) has been disabled. It will disappear after you restart."
        alert.runModal()
    }

    @objc private func showAbout() {
        let alert = NSAlert()
        alert.messageText = "mSL/XNU \(MSLState().version)"
        alert.informativeText =
            "macOS Subsystem for Linux / X is Now UNIX\n\n"
            + "A filesystem-layout compatibility layer that presents macOS "
            + "through a Linux-shaped namespace.\n\n"
            + "https://github.com/somestupidgirl/mSL-XNU\n"
            + "Copyright © 2026 Sunneva N. Mariu"
        alert.runModal()
    }

    // MARK: - Login item

    private var loginEnabled: Bool {
        if #available(macOS 13.0, *) {
            return SMAppService.mainApp.status == .enabled
        }
        return false
    }

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
