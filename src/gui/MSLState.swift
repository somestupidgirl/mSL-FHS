//
// Copyright (c) 2026 Sunneva N. Mariu
//
// MSLState.swift
//
// Shared between the menu-bar app and the preference pane: reading the layer's
// state, and applying changes to it.
//
// State comes from `mslctl porcelain`, which prints one key=value per line.
// Parsing the human-readable output instead would couple the GUI to wording
// that exists to be read by people and will be reworded; the porcelain keys are
// an interface. Both surfaces going through the same tool also means the GUI
// and the CLI can never report different things.
//
// Mutations run through the standard administrator-authorization prompt. The
// daemon is already root and could apply them without one, but that would mean
// a root process taking reconfiguration commands over a socket, and any peer
// policy permissive enough to be convenient would let anything running as the
// user silently remask /etc/auto_master. The prompt is not only an access
// check - it is what makes the change visible and attributable.
//
import Foundation

let kMslctl = "/usr/local/sbin/mslctl"

/// The three components, in the order they are presented.
enum Component: String, CaseIterable {
    case home, mnt, media

    var path: String { "/" + rawValue }

    var summary: String {
        switch self {
        case .home:  return "Linux-style /home/<user> paths"
        case .mnt:   return "/mnt, an empty directory"
        case .media: return "Removable volumes under /media/<user>"
        }
    }

    /// Whether turning this on or off only takes effect after a restart.
    var needsReboot: Bool {
        // /home repurposes a directory macOS already provides, so it applies
        // immediately. The other two need a root-level entry, which only
        // /etc/synthetic.conf can create and only at boot.
        self != .home
    }
}

/// Why a node's Finder visibility cannot be changed - mirrors the C enum's
/// porcelain words. Only `.changeable` permits a Show/Hide toggle.
enum VisLock: String {
    case changeable, absent, sip, readonly, unsupported, protected, unknown

    var canChange: Bool { self == .changeable }

    /// Reason to show when a toggle is offered but disabled.
    var reason: String? {
        switch self {
        case .changeable:  return nil
        case .absent:      return "Does not exist"
        case .sip:         return "Protected by System Integrity Protection"
        case .readonly:    return "On the sealed system volume"
        case .unsupported: return "This filesystem ignores visibility flags"
        case .protected:   return "The system refuses to change this entry"
        case .unknown:     return "Cannot be changed"
        }
    }
}

/// One root-level directory node, as the menu presents it. Covers both the
/// mSL-managed components (which also carry an on/off state) and the native
/// macOS directories (which only have visibility).
struct NodeInfo {
    let name: String       // "opt"
    let exists: Bool
    let hidden: Bool
    let lock: VisLock
    let linuxOnly: Bool
    let isMount: Bool
    let browsable: Bool
    let fstype: String

    var path: String { "/" + name }

    /// The managed component this node corresponds to, if any.
    var component: Component? { Component(rawValue: name) }
}

/// A snapshot of the whole layer.
struct MSLState {
    private var values: [String: String] = [:]

    var available: Bool { !values.isEmpty }

    init() {
        for line in MSLState.run(kMslctl, ["porcelain"]).split(separator: "\n") {
            let parts = line.split(separator: "=", maxSplits: 1,
                                   omittingEmptySubsequences: false)
            if parts.count == 2 {
                values[String(parts[0])] = String(parts[1])
            }
        }
    }

    func string(_ key: String) -> String { values[key] ?? "" }
    func int(_ key: String) -> Int { Int(values[key] ?? "") ?? 0 }
    func flag(_ key: String) -> Bool { int(key) != 0 }

    func enabled(_ c: Component) -> Bool { flag("\(c.rawValue).enabled") }

    /// True when a component is on but not yet visible, pending a restart.
    func rebootPending(_ c: Component) -> Bool {
        c == .home ? false : flag("\(c.rawValue).reboot_pending")
    }

    /// True when synthetic.conf declares the name but not as ours - we will
    /// not touch it, and the user needs to know why nothing is happening.
    func conflicting(_ c: Component) -> Bool {
        c == .home ? false : flag("\(c.rawValue).conflicting")
    }

    var version: String { string("version") }
    var daemonRunning: Bool { flag("daemon.running") }

    /// A short status line for a component, suitable for a disabled menu item.
    func detail(_ c: Component) -> String {
        if conflicting(c) {
            return "conflicts with an existing entry"
        }
        if !enabled(c) {
            return "off"
        }
        if rebootPending(c) {
            return "on — restart to appear"
        }

        switch c {
        case .home:
            let links = int("home.links"), users = int("home.users")
            if flag("home.automounter") { return "on — automounter still owns /home" }
            return links == users ? "\(links) of \(users) accounts"
                                  : "\(links) of \(users) accounts — needs sync"
        case .mnt:
            return "No mount points detected"
        case .media:
            let stale = int("media.stale")
            if stale > 0 { return "\(int("media.links")) links, \(stale) stale" }
            let vols = int("media.volumes")
            return vols == 0 ? "No removable volumes" : "\(vols) volume(s)"
        }
    }

    /// Every root-level node, in the order the CLI lists them.
    var nodes: [NodeInfo] {
        string("nodes").split(separator: ",").map { raw in
            let name = String(raw)
            return NodeInfo(
                name: name,
                exists: flag("vis.\(name).exists"),
                hidden: flag("vis.\(name).hidden"),
                lock: VisLock(rawValue: string("vis.\(name).lock")) ?? .unknown,
                linuxOnly: flag("vis.\(name).linux"),
                isMount: flag("vis.\(name).mount"),
                browsable: flag("vis.\(name).browsable"),
                fstype: string("vis.\(name).fstype"))
        }
    }

    /// The status dot: a node is "on" when its managed component is enabled,
    /// or - for a native directory with no component - when it exists.
    func nodeOn(_ n: NodeInfo) -> Bool {
        if let c = n.component { return enabled(c) }
        return n.exists
    }

    /// The line shown inside a node's dropdown.
    func nodeDetail(_ n: NodeInfo) -> String {
        if let c = n.component {
            return detail(c)
        }
        if !n.exists {
            return "Does not exist on this system"
        }
        if let reason = n.lock.reason {
            return "Visibility locked: \(reason)"
        }
        return n.hidden ? "Hidden in the Finder" : "Visible in the Finder"
    }

    /// Status of a pseudo-filesystem we only observe, never manage.
    func pseudofs(_ name: String) -> String {
        if flag("\(name).mounted") { return "Mounted" }
        if flag("\(name).installed") { return "Installed, not mounted" }
        return "Not installed"
    }

    // MARK: - Running things

    /// Run a tool unprivileged and capture stdout. Empty string on failure.
    static func run(_ path: String, _ args: [String]) -> String {
        let task = Process()
        task.executableURL = URL(fileURLWithPath: path)
        task.arguments = args
        let out = Pipe()
        task.standardOutput = out
        task.standardError = Pipe()
        do { try task.run() } catch { return "" }
        let data = out.fileHandleForReading.readDataToEndOfFile()
        task.waitUntilExit()
        return String(data: data, encoding: .utf8) ?? ""
    }

    /// Run a shell command as root behind one authorization prompt.
    @discardableResult
    static func runPrivileged(_ command: String) -> Bool {
        // Escape for AppleScript's string literal, then for the shell it runs.
        let escaped = command.replacingOccurrences(of: "\\", with: "\\\\")
                             .replacingOccurrences(of: "\"", with: "\\\"")
        let source = "do shell script \"\(escaped)\" with administrator privileges"
        guard let script = NSAppleScript(source: source) else { return false }
        var err: NSDictionary?
        script.executeAndReturnError(&err)
        return err == nil
    }

    /// Apply a set of component changes in a single authorized step.
    ///
    /// Batching is the whole point: a per-component matrix behind a
    /// prompt-per-action would mean a password prompt per checkbox. The
    /// commands are chained with `&&` so a failure stops the sequence rather
    /// than leaving the layer half-applied with no indication which part failed.
    @discardableResult
    static func apply(_ changes: [Component: Bool]) -> Bool {
        guard !changes.isEmpty else { return true }

        let commands = changes
            .sorted { $0.key.rawValue < $1.key.rawValue }
            .map { "\(kMslctl) \($0.key.rawValue) \($0.value ? "enable" : "disable")" }

        return runPrivileged(commands.joined(separator: " && "))
    }

    /// Clear links left behind by volumes that were ejected.
    @discardableResult
    static func syncMedia() -> Bool {
        runPrivileged("\(kMslctl) media sync")
    }

    /// Show or hide a node in the Finder. One menu click is one action, so this
    /// takes its own authorization - the batching that the preference pane does
    /// is for its multi-switch Apply, not for a single toggle.
    @discardableResult
    static func setVisible(_ name: String, _ visible: Bool) -> Bool {
        runPrivileged("\(kMslctl) vis \(visible ? "show" : "hide") \(name)")
    }

    /// Apply component and visibility changes together, in one authorized step.
    ///
    /// The preference pane collects both kinds of change and applies them at
    /// once, so a session of flipping switches costs a single prompt rather
    /// than one per switch. Component commands run before visibility ones, and
    /// the whole sequence is chained with `&&` so a failure stops it rather than
    /// leaving the layer half-applied.
    @discardableResult
    static func applyAll(components: [Component: Bool],
                         visibility: [String: Bool]) -> Bool {
        var commands: [String] = []

        for (c, on) in components.sorted(by: { $0.key.rawValue < $1.key.rawValue }) {
            commands.append("\(kMslctl) \(c.rawValue) \(on ? "enable" : "disable")")
        }
        for (name, visible) in visibility.sorted(by: { $0.key < $1.key }) {
            commands.append("\(kMslctl) vis \(visible ? "show" : "hide") \(name)")
        }

        guard !commands.isEmpty else { return true }
        return runPrivileged(commands.joined(separator: " && "))
    }
}
