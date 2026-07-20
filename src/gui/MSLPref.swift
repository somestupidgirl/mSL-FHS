//
// Copyright (c) 2026 Sunneva N. Mariu
//
// MSLPref.swift
//
// The System Settings preference pane, laid out after Apple's stock panes: a
// description card at the top, grouped setting cards below, and a version and
// copyright line at the foot.
//
// The pane is where batching earns its place. It collects two kinds of change -
// component on/off and per-directory Finder visibility - and applies them all
// through one authorization when Apply is pressed. Flipping several switches
// otherwise means one password prompt per switch. Until Apply nothing has
// happened, so Revert is always available and a half-finished change is never
// left on the system.
//
import AppKit
import PreferencePanes
import ServiceManagement

private let kRepoURL = "https://github.com/somestupidgirl/mSL-XNU"

private let kDescription = """
A filesystem-layout compatibility layer that presents macOS through a \
Linux-shaped namespace. Home directories appear under /home, removable volumes \
under /media, and /mnt exists for manual mounts - without a container, a virtual \
machine, or a kernel extension. It can also show or hide the root-level \
directories in the Finder. The dynamic parts of a Linux tree, /proc and /sys, \
are supplied by separate projects; this pane reports their state but does not \
manage them.
"""

// The Info.plist names this class in NSPrincipalClass, so it needs a stable
// Objective-C name rather than Swift's mangled one.
@objc(MSLPref)
final class MSLPref: NSPreferencePane {
    private var state = MSLState()

    // Pending, unapplied intent. Both are empty between Apply presses.
    private var pendingComponents: [Component: Bool] = [:]
    private var pendingVisibility: [String: Bool] = [:]

    private var componentSwitches: [Component: NSSwitch] = [:]
    private var componentDetails: [Component: NSTextField] = [:]
    private var visSwitches: [String: NSSwitch] = [:]
    private var visRows: [String: NSTextField] = [:]

    private var applyButton: NSButton!
    private var revertButton: NSButton!
    private var daemonLabel: NSTextField!
    private var procLabel: NSTextField!
    private var sysLabel: NSTextField!

    /// One content width for the whole pane. Every card and row is constrained
    /// to it, so nothing depends on a subview's intrinsic size - a card whose
    /// contents carry no width constraint collapses and draws over its
    /// neighbours.
    private let contentWidth: CGFloat = 580

    override func loadMainView() -> NSView {
        let view = NSView(frame: NSRect(x: 0, y: 0, width: 620, height: 720))

        let stack = NSStackView()
        stack.orientation = .vertical
        stack.alignment = .leading
        stack.spacing = 14
        stack.translatesAutoresizingMaskIntoConstraints = false

        let sections: [NSView] = [
            card(makeHeader()),
            sectionLabel("Components"),
            card(makeComponents()),
            sectionLabel("Directory Visibility"),
            card(makeVisibilityList()),
            makeNote("The Finder hides most root-level directories. Some can be "
                   + "shown; others are held hidden by macOS and are listed with "
                   + "the reason."),
            sectionLabel("Pseudo-filesystems"),
            card(makePseudoFS()),
            makeNote("Managed by their own installers. mSL/XNU reports their "
                   + "state and never mounts or unmounts them."),
            makeButtons(),
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

    // MARK: - Header

    private func makeHeader() -> NSView {
        // The app icon itself, as the stock panes show, rather than a symbol.
        let icon = NSImageView()
        icon.imageScaling = .scaleProportionallyUpOrDown
        if let url = Bundle(for: type(of: self)).url(forResource: "appicon",
                                                     withExtension: "png") {
            icon.image = NSImage(contentsOf: url)
        } else {
            icon.image = NSImage(systemSymbolName: "folder.badge.gearshape",
                                 accessibilityDescription: "mSL/XNU")
        }
        icon.wantsLayer = true
        icon.layer?.cornerRadius = 10
        icon.layer?.masksToBounds = true
        icon.setContentHuggingPriority(.required, for: .horizontal)
        NSLayoutConstraint.activate([
            icon.widthAnchor.constraint(equalToConstant: 56),
            icon.heightAnchor.constraint(equalToConstant: 56),
        ])

        let title = NSTextField(labelWithString: "mSL/XNU")
        title.font = .systemFont(ofSize: 22, weight: .semibold)

        let blurb = NSTextField(wrappingLabelWithString: kDescription)
        blurb.font = .systemFont(ofSize: 11)
        blurb.textColor = .secondaryLabelColor
        blurb.preferredMaxLayoutWidth = contentWidth - 32 - 60

        let text = NSStackView(views: [title, blurb])
        text.orientation = .vertical
        text.alignment = .leading
        text.spacing = 6

        let row = NSStackView(views: [icon, text])
        row.orientation = .horizontal
        row.alignment = .top
        row.spacing = 16
        return row
    }

    // MARK: - Rows

    /// Width available inside a card, once its padding is taken off.
    private var cardWidth: CGFloat { contentWidth - 32 }

    /// A row of "name / detail" text with an optional control on the right.
    private func row(name: NSTextField, detail: NSTextField,
                     accessory: NSView?, width: CGFloat) -> NSStackView {
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
        row.widthAnchor.constraint(equalToConstant: width).isActive = true

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
            sw.action = #selector(componentSwitched(_:))
            sw.tag = Component.allCases.firstIndex(of: component)!
            componentSwitches[component] = sw

            let detail = NSTextField(labelWithString: component.summary)
            componentDetails[component] = detail

            stack.addArrangedSubview(row(
                name: NSTextField(labelWithString: component.path),
                detail: detail, accessory: sw, width: cardWidth))
        }

        return stack
    }

    /// The visibility list can be tall (one row per existing root directory),
    /// so it lives in a fixed-height scroll view rather than pushing the rest
    /// of the pane off the bottom.
    private func makeVisibilityList() -> NSView {
        let stack = NSStackView()
        stack.orientation = .vertical
        stack.alignment = .leading
        stack.spacing = 10
        stack.translatesAutoresizingMaskIntoConstraints = false
        stack.edgeInsets = NSEdgeInsets(top: 2, left: 0, bottom: 2, right: 0)

        // Only directories that actually exist can be shown or hidden; the
        // not-yet-created Linux nodes have nothing to reveal.
        let innerWidth = cardWidth - 20   // leave room for the scroller
        for node in state.nodes where node.exists {
            let sw = NSSwitch()
            sw.target = self
            sw.action = #selector(visibilitySwitched(_:))
            visSwitches[node.name] = sw

            let detail = NSTextField(labelWithString: "")
            visRows[node.name] = detail

            stack.addArrangedSubview(row(
                name: NSTextField(labelWithString: node.path),
                detail: detail, accessory: sw, width: innerWidth))
        }

        let scroll = NSScrollView()
        scroll.hasVerticalScroller = true
        scroll.drawsBackground = false
        scroll.documentView = stack
        scroll.translatesAutoresizingMaskIntoConstraints = false
        scroll.heightAnchor.constraint(equalToConstant: 200).isActive = true

        NSLayoutConstraint.activate([
            stack.topAnchor.constraint(equalTo: scroll.contentView.topAnchor),
            stack.leadingAnchor.constraint(equalTo: scroll.contentView.leadingAnchor),
            stack.widthAnchor.constraint(equalToConstant: innerWidth),
        ])

        return scroll
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
            detail: procLabel, accessory: nil, width: cardWidth))
        stack.addArrangedSubview(row(
            name: NSTextField(labelWithString: "/sys"),
            detail: sysLabel, accessory: nil, width: cardWidth))

        return stack
    }

    private func makeNote(_ s: String) -> NSView {
        let note = NSTextField(wrappingLabelWithString: s)
        note.font = .systemFont(ofSize: 11)
        note.textColor = .secondaryLabelColor
        note.preferredMaxLayoutWidth = contentWidth
        return note
    }

    private func makeButtons() -> NSView {
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

    /// The version-and-repository line and copyright, as on the procfs pane.
    private func makeFooter() -> NSView {
        let version = NSTextField(labelWithString: "mSL/XNU v\(state.version): ")
        version.font = .systemFont(ofSize: 11)
        version.textColor = .secondaryLabelColor

        let link = NSButton(title: kRepoURL, target: self, action: #selector(openRepo))
        link.isBordered = false
        link.contentTintColor = .linkColor
        link.attributedTitle = NSAttributedString(
            string: kRepoURL,
            attributes: [.foregroundColor: NSColor.linkColor,
                         .font: NSFont.systemFont(ofSize: 11)])

        let line = NSStackView(views: [version, link])
        line.orientation = .horizontal
        line.spacing = 0

        let copyright = NSTextField(labelWithString: "Copyright © 2026 Sunneva N. Mariu")
        copyright.font = .systemFont(ofSize: 11)
        copyright.textColor = .secondaryLabelColor

        let stack = NSStackView(views: [line, copyright])
        stack.orientation = .vertical
        stack.alignment = .leading
        stack.spacing = 3
        return stack
    }

    // MARK: - Card / labels

    /// Wrap `content` in a rounded System-Settings-style card, matching the
    /// procfs pane: the background is drawn by the layer-backed CardBox (white
    /// in Aqua, a subtle raised overlay in Dark Aqua, no border), and the card's
    /// size is driven by the content pinned to its edges so it never collapses.
    private func card(_ content: NSView, padding: CGFloat = 16) -> NSView {
        let box = CardBox()
        content.translatesAutoresizingMaskIntoConstraints = false
        box.addSubview(content)
        NSLayoutConstraint.activate([
            content.leadingAnchor.constraint(equalTo: box.leadingAnchor, constant: padding),
            content.trailingAnchor.constraint(equalTo: box.trailingAnchor, constant: -padding),
            content.topAnchor.constraint(equalTo: box.topAnchor, constant: padding),
            content.bottomAnchor.constraint(equalTo: box.bottomAnchor, constant: -padding),
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
            let want = pendingComponents[component] ?? state.enabled(component)
            componentSwitches[component]?.state = want ? .on : .off
            componentSwitches[component]?.isEnabled = !state.conflicting(component)
            componentDetails[component]?.stringValue =
                component.summary + " — " + state.detail(component)
        }

        for node in state.nodes where node.exists {
            guard let sw = visSwitches[node.name] else { continue }
            let want = pendingVisibility[node.name] ?? !node.hidden   // on == visible
            sw.state = want ? .on : .off
            sw.isEnabled = node.lock.canChange
            visRows[node.name]?.stringValue = node.lock.reason ?? (node.hidden
                ? "Hidden in the Finder" : "Visible in the Finder")
        }

        procLabel.stringValue = state.pseudofs("proc")
        sysLabel.stringValue = state.pseudofs("sys")
        daemonLabel.stringValue = "mslxd — "
            + (state.daemonRunning ? "running" : "not running")

        updateButtons()
    }

    private var hasPending: Bool {
        !pendingComponents.isEmpty || !pendingVisibility.isEmpty
    }

    private func updateButtons() {
        applyButton.isEnabled = hasPending
        revertButton.isEnabled = hasPending
    }

    @objc private func componentSwitched(_ sender: NSSwitch) {
        let component = Component.allCases[sender.tag]
        let want = sender.state == .on

        // Returning a switch to its original position clears the pending change
        // rather than queuing a second one that undoes the first.
        if want == state.enabled(component) {
            pendingComponents.removeValue(forKey: component)
        } else {
            pendingComponents[component] = want
        }
        updateButtons()
    }

    @objc private func visibilitySwitched(_ sender: NSSwitch) {
        guard let name = visSwitches.first(where: { $0.value === sender })?.key,
              let node = state.nodes.first(where: { $0.name == name }) else { return }

        let wantVisible = sender.state == .on
        if wantVisible == !node.hidden {
            pendingVisibility.removeValue(forKey: name)
        } else {
            pendingVisibility[name] = wantVisible
        }
        updateButtons()
    }

    @objc private func revert() {
        pendingComponents.removeAll()
        pendingVisibility.removeAll()
        refresh()
    }

    @objc private func apply() {
        let components = pendingComponents
        let visibility = pendingVisibility
        guard !components.isEmpty || !visibility.isEmpty else { return }

        guard MSLState.applyAll(components: components, visibility: visibility) else {
            // Cancelling the authorization prompt lands here too - an ordinary
            // thing to do - so the pending changes are kept and the user can
            // simply press Apply again.
            let alert = NSAlert()
            alert.messageText = "Changes were not applied."
            alert.informativeText =
                "The authorization was cancelled or a command failed. "
                + "Your selections have been kept."
            alert.runModal()
            refresh()
            return
        }

        pendingComponents.removeAll()
        pendingVisibility.removeAll()
        refresh()

        let restarters = components.keys.filter { $0.needsReboot && components[$0] != nil }
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

    @objc private func openRepo() {
        if let url = URL(string: kRepoURL) {
            NSWorkspace.shared.open(url)
        }
    }
}

// MARK: - Rounded card background

/// A layer-backed rounded rectangle used as the background of a settings group,
/// matching the procfs pane: a white card in Aqua, a subtle raised overlay in
/// Dark Aqua, no border. Redraws on an appearance change.
private final class CardBox: NSView {
    override var wantsUpdateLayer: Bool { true }

    override func updateLayer() {
        wantsLayer = true
        layer?.cornerRadius = 10
        let dark = effectiveAppearance.bestMatch(from: [.aqua, .darkAqua]) == .darkAqua
        layer?.backgroundColor = (dark ? NSColor(white: 1.0, alpha: 0.09)
                                        : NSColor.white).cgColor
    }

    override func viewDidChangeEffectiveAppearance() {
        super.viewDidChangeEffectiveAppearance()
        needsDisplay = true
    }
}
