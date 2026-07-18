//
// Copyright (c) 2026 Sunneva N. Mariu
//
// main.swift
//
// Entry point for the menu-bar app. LSUIElement in Info.plist keeps it out of
// the Dock and the app switcher: it is a status item, not a window.
//
import Cocoa

let app = NSApplication.shared
let delegate = AppDelegate()
app.delegate = delegate
app.setActivationPolicy(.accessory)
app.run()
