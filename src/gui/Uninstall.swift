//
// Copyright (c) 2026 Sunneva N. Mariu
//
// Uninstall.swift
//
// The uninstaller app. It confirms, then runs `fhsctl uninstall` behind one
// administrator authorization, and reports what happened.
//
// Deliberately thin. The teardown itself lives in fhsctl, which is the only
// place that knows the full component list - earlier copies of the sequence in
// a shell script and in the Makefile had each drifted, disabling only three of
// the seven components and leaving the rest with live /etc/synthetic.conf
// entries after an "uninstall". This app adds a way to ask for that work, and
// nothing else.
//
import AppKit

let kFhsctl = "/usr/local/sbin/fhsctl"

private func alert(_ title: String, _ body: String,
                   style: NSAlert.Style = .informational,
                   buttons: [String] = ["OK"]) -> NSApplication.ModalResponse {
    let a = NSAlert()
    a.messageText = title
    a.informativeText = body
    a.alertStyle = style
    for b in buttons { a.addButton(withTitle: b) }
    return a.runModal()
}

/// Run a command as root behind the standard authorization prompt.
private func runPrivileged(_ command: String) -> (ok: Bool, output: String) {
    let escaped = command.replacingOccurrences(of: "\\", with: "\\\\")
                         .replacingOccurrences(of: "\"", with: "\\\"")
    let source = "do shell script \"\(escaped) 2>&1\" with administrator privileges"
    guard let script = NSAppleScript(source: source) else {
        return (false, "could not build the authorization request")
    }
    var err: NSDictionary?
    let result = script.executeAndReturnError(&err)
    return (err == nil, result.stringValue ?? "")
}

NSApplication.shared.setActivationPolicy(.regular)
NSApplication.shared.activate(ignoringOtherApps: true)

// The tool is what does the work, so its absence is the one thing worth
// checking before asking for a password.
guard FileManager.default.isExecutableFile(atPath: kFhsctl) else {
    _ = alert("mSL/FHS does not appear to be installed.",
              "\(kFhsctl) is missing, so there is nothing to remove.\n\n"
              + "If files were left behind by a partial install, your original "
              + "/etc/auto_master is kept at /var/db/fhs.auto_master.orig.",
              style: .warning)
    exit(0)
}

let go = alert(
    "Remove mSL/FHS?",
    "Every component will be switched off and all installed files removed.\n\n"
    + "Switching the components off is what restores /etc/auto_master and "
    + "withdraws the /etc/synthetic.conf entries, so the system is left as it "
    + "was found. The root-level directories stay until you restart.\n\n"
    + "A copy of your original /etc/auto_master is kept at "
    + "/var/db/fhs.auto_master.orig either way.",
    style: .warning,
    buttons: ["Remove", "Cancel"])

guard go == .alertFirstButtonReturn else { exit(0) }

let (ok, output) = runPrivileged("\(kFhsctl) uninstall")

if ok {
    _ = alert("mSL/FHS has been removed.",
              "The root-level directories remain until you restart.\n\n"
              + (output.isEmpty ? "" : output))
} else {
    // Cancelling the prompt lands here too, which is an ordinary thing to do.
    _ = alert("mSL/FHS was not removed.",
              "The authorization was cancelled, or the command failed. Nothing "
              + "has been changed.\n\n"
              + (output.isEmpty ? "" : output),
              style: .warning)
}

exit(ok ? 0 : 1)
