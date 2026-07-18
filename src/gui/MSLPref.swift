//
// Copyright (c) 2026 Sunneva N. Mariu
//
// MSLPref.swift
//
// The System Settings preference pane. Mirrors the menu-bar app's controls, in
// the layout of Apple's stock panes.
//
// The pane is where batching earns its place. Flipping three switches with a
// prompt-per-action would mean three password prompts; here the switches only
// record intent, and Apply sends the whole set through one authorization. Until
// Apply is pressed nothing has happened, so Revert is always available and a
// half-finished change is never left on the system.
//
import AppKit
import PreferencePanes
import ServiceManagement

private let kDescription = """
A filesystem-layout compatibility layer that presents macOS through a \
Linux-shaped namespace. Home directories appear under /home, removable volumes \
under /media, and /mnt exists for manual mounts - without a container, a virtual \
machine, or a kernel extension. The dynamic parts of a Linux tree, /proc and \
/sys, are supplied by separate projects; this pane reports their state but does \
not manage them.
"""

// The Info.plist names this class in NSPrincipalClass, so it needs a stable
// Objective-C name rather than Swift's mangled one.
@objc(MSLPref)
final class MSLPref: NSPreferencePane {
    private var state = MSLState()
    private var pending: [Component: Bool] = [:]

    private var switches: [Component: NSSwitch] = [:]
    private var details: [Component: NSTextField] = [:]
    private var applyButton: NSButton!
    private var revertButton: NSButton!
    private var daemonLabel: NSTextField!
    private var procLabel: NSTextField!
    private var sysLabel: NSTextField!

    /// One content width for the whole pane. Every card and row is constrained
    /// to it, so nothing depends on a subview happening to have an intrinsic
    /// size - a card whose contents carry no width constraint collapses, and
    /// its contents then draw over whatever is above and below it.
    private let contentWidth: CGFloat = 560

    override func loadMainView() -> NSView {
        let view = NSView(frame: NSRect(x: 0, y: 0, width: 600, height: 560))

        let stack = NSStackView()
        stack.orientation = .vertical
        stack.alignment = .leading
        stack.spacing = 14
        stack.translatesAutoresizingMaskIntoConstraints = false

        let sections: [NSView] = [
            makeHeader(),
            sectionLabel("Components"),
            card(makeComponents()),
            sectionLabel("Pseudo-filesystems"),
            card(makePseudoFS()),
            makeNote("Managed by their own installers. mSL/XNU reports their "
                   + "state and never mounts or unmounts them."),
            makeFooter(),
        ]

        for section in sections {
            stack.addArrangedSubview(section)
            section.widthAnchor.constraint(equalToConstant: contentWidth).isActive = true
        }

        view.addSubview(stack)
        NSLayoutConstraint.activate([
            stack.leadingAnchor.constraint(equalTo: view.leadingAnchor, constant: 20),
            stack.topAnchor.constraint(equalTo: view.topAnchor, constant: 20),
            view.trailingAnchor.constraint(greaterThanOrEqualTo: stack.trailingAnchor,
                                           constant: 20),
            view.bottomAnchor.constraint(greaterThanOrEqualTo: stack.bottomAnchor,
                                         constant: 20),
        ])

        mainView = view
        refresh()
        return view
    }

    override func didSelect() { refresh() }

    // MARK: - Construction

    private func makeHeader() -> NSView {
        let title = NSTextField(labelWithString: "mSL/XNU")
        title.font = .systemFont(ofSize: 22, weight: .semibold)

        let blurb = NSTextField(wrappingLabelWithString: kDescription)
        blurb.font = .systemFont(ofSize: 11)
        blurb.textColor = .secondaryLabelColor
        blurb.preferredMaxLayoutWidth = 560

        let stack = NSStackView(views: [title, blurb])
        stack.orientation = .vertical
        stack.alignment = .leading
        stack.spacing = 6
        return stack
    }

    /// Width available inside a card, once its padding is taken off.
    private var cardWidth: CGFloat { contentWidth - 32 }

    /// A row of "name / detail" text with an optional control on the right.
    /// Used for both components and pseudo-filesystems so the two sections
    /// line up rather than each inventing its own layout.
    private func row(name: NSTextField, detail: NSTextField,
                     accessory: NSView?) -> NSStackView {
        name.font = .systemFont(ofSize: 13, weight: .medium)
        detail.font = .systemFont(ofSize: 11)
        detail.textColor = .secondaryLabelColor
        detail.lineBreakMode = .byTruncatingTail

        let text = NSStackView(views: [name, detail])
        text.orientation = .vertical
        text.alignment = .leading
        text.spacing = 1

        let spacer = NSView()
        let row = NSStackView(views: accessory == nil
            ? [text, spacer] : [text, spacer, accessory!])
        row.orientation = .horizontal
        row.spacing = 12
        row.widthAnchor.constraint(equalToConstant: cardWidth).isActive = true

        // The spacer absorbs slack so the accessory stays pinned right and the
        // text truncates rather than pushing the switch off the card.
        spacer.setContentHuggingPriority(.defaultLow, for: .horizontal)
        text.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
        return row
    }

    private func makeComponents() -> NSView {
        let stack = NSStackView()
        stack.orientation = .vertical
        stack.alignment = .leading
        stack.spacing = 12

        for component in Component.allCases {
            let sw = NSSwitch()
            sw.target = self
            sw.action = #selector(switched(_:))
            sw.tag = Component.allCases.firstIndex(of: component)!
            switches[component] = sw

            let detail = NSTextField(labelWithString: component.summary)
            details[component] = detail

            stack.addArrangedSubview(row(
                name: NSTextField(labelWithString: component.path),
                detail: detail,
                accessory: sw))
        }

        return stack
    }

    private func makePseudoFS() -> NSView {
        procLabel = NSTextField(labelWithString: "")
        sysLabel = NSTextField(labelWithString: "")

        let stack = NSStackView()
        stack.orientation = .vertical
        stack.alignment = .leading
        stack.spacing = 12

        stack.addArrangedSubview(row(
            name: NSTextField(labelWithString: "/proc"),
            detail: procLabel, accessory: nil))
        stack.addArrangedSubview(row(
            name: NSTextField(labelWithString: "/sys"),
            detail: sysLabel, accessory: nil))

        return stack
    }

    private func makeNote(_ s: String) -> NSView {
        let note = NSTextField(wrappingLabelWithString: s)
        note.font = .systemFont(ofSize: 11)
        note.textColor = .secondaryLabelColor
        note.preferredMaxLayoutWidth = contentWidth
        return note
    }

    private func makeFooter() -> NSView {
        daemonLabel = NSTextField(labelWithString: "")
        daemonLabel.font = .systemFont(ofSize: 11)
        daemonLabel.textColor = .secondaryLabelColor

        revertButton = NSButton(title: "Revert", target: self, action: #selector(revert))
        revertButton.bezelStyle = .rounded

        applyButton = NSButton(title: "Apply", target: self, action: #selector(apply))
        applyButton.bezelStyle = .rounded
        applyButton.keyEquivalent = "\r"

        let spacer = NSView()
        let row = NSStackView(views: [daemonLabel, spacer, revertButton, applyButton])
        row.orientation = .horizontal
        row.spacing = 10
        spacer.setContentHuggingPriority(.defaultLow, for: .horizontal)
        return row
    }

    /// A rounded card. The content is pinned to the box's edges rather than
    /// relying on `contentViewMargins`, so the box derives its height from the
    /// content instead of collapsing and letting the content draw over its
    /// neighbours.
    private func card(_ content: NSView, padding: CGFloat = 16) -> NSView {
        let box = NSBox()
        box.boxType = .custom
        box.fillColor = .controlBackgroundColor
        box.borderColor = .separatorColor
        box.cornerRadius = 8
        box.contentViewMargins = .zero
        box.contentView = content

        content.translatesAutoresizingMaskIntoConstraints = false
        NSLayoutConstraint.activate([
            content.leadingAnchor.constraint(equalTo: box.leadingAnchor,
                                             constant: padding),
            content.trailingAnchor.constraint(equalTo: box.trailingAnchor,
                                              constant: -padding),
            content.topAnchor.constraint(equalTo: box.topAnchor, constant: padding),
            content.bottomAnchor.constraint(equalTo: box.bottomAnchor,
                                            constant: -padding),
        ])

        return box
    }

    private func sectionLabel(_ s: String) -> NSView {
        let label = NSTextField(labelWithString: s)
        label.font = .systemFont(ofSize: 12, weight: .semibold)
        label.textColor = .secondaryLabelColor
        return label
    }

    // MARK: - State

    private func refresh() {
        state = MSLState()

        for component in Component.allCases {
            let want = pending[component] ?? state.enabled(component)
            switches[component]?.state = want ? .on : .off

            // A conflicting synthetic.conf entry is not ours to change, so the
            // switch is disabled and says why rather than failing on Apply.
            switches[component]?.isEnabled = !state.conflicting(component)
            details[component]?.stringValue =
                component.summary + " — " + state.detail(component)
        }

        // The path is now the row's own label, so these carry only the state.
        procLabel.stringValue = state.pseudofs("proc")
        sysLabel.stringValue = state.pseudofs("sys")
        daemonLabel.stringValue = "mslxd — "
            + (state.daemonRunning ? "running" : "not running")

        applyButton.isEnabled = !pending.isEmpty
        revertButton.isEnabled = !pending.isEmpty
    }

    @objc private func switched(_ sender: NSSwitch) {
        let component = Component.allCases[sender.tag]
        let want = sender.state == .on

        // Recording intent, not acting on it. Returning a switch to its
        // original position clears the pending change rather than queuing a
        // second one that undoes the first.
        if want == state.enabled(component) {
            pending.removeValue(forKey: component)
        } else {
            pending[component] = want
        }

        applyButton.isEnabled = !pending.isEmpty
        revertButton.isEnabled = !pending.isEmpty
    }

    @objc private func revert() {
        pending.removeAll()
        refresh()
    }

    @objc private func apply() {
        let changes = pending
        guard !changes.isEmpty else { return }

        guard MSLState.apply(changes) else {
            // Cancelling the authorization prompt lands here too, which is a
            // perfectly ordinary thing to do - so the pending changes are kept
            // rather than discarded, and the user can simply press Apply again.
            let alert = NSAlert()
            alert.messageText = "Changes were not applied."
            alert.informativeText =
                "The authorization was cancelled or the command failed. "
                + "Your selections have been kept."
            alert.runModal()
            refresh()
            return
        }

        pending.removeAll()
        refresh()

        let restarters = changes.keys.filter { $0.needsReboot }
        if !restarters.isEmpty {
            let names = restarters.map(\.path).sorted().joined(separator: " and ")
            let alert = NSAlert()
            alert.messageText = "Restart required"
            alert.informativeText =
                "macOS creates root-level directories only at startup, so "
                + "\(names) will appear or disappear after you restart."
            alert.runModal()
        }
    }
}
