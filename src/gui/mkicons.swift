//
// Copyright (c) 2026 Sunneva N. Mariu
//
// mkicons.swift
//
// Draws the mSL/FHS icons, so they have a source rather than being opaque
// binaries in the tree: the shape, the gradient and the geometry are all
// readable and adjustable here, and `make icons` regenerates the PNGs.
//
// The design is deliberately a sibling of procfs's, not a copy. Both are a
// macOS squircle with a vertical gradient and a white glyph of nodes joined by
// lines. procfs draws a process tree - a root fanning out downwards. mSL/FHS
// draws a directory tree, spine on the left with branches to the right, which
// is how a filesystem hierarchy is conventionally drawn and what this project
// is about.
//
// The gradients share their lower colour and differ above: procfs runs
// purple to blue, mSL/FHS green to blue. Side by side in System Settings they
// read as related without being mistaken for each other.
//
import AppKit

// MARK: - Geometry, in a 1024-point canvas

let canvas: CGFloat = 1024

/// Apple insets icon art from the canvas; the shadow occupies the margin.
let inset: CGFloat = 100
let plateRect = NSRect(x: inset, y: inset + 12,
                       width: canvas - inset * 2, height: canvas - inset * 2)

/// Apple's corner-radius ratio for the rounded-rectangle app icon.
let cornerRadius = plateRect.width * 0.2237

let gradientTop = NSColor(srgbRed: 0.30, green: 0.79, blue: 0.49, alpha: 1)  // green
let gradientBottom = NSColor(srgbRed: 0.07, green: 0.63, blue: 0.78, alpha: 1) // blue

// MARK: - The glyph

/// A directory tree: a root node, a spine descending from it, and three
/// branches to child nodes. Drawn into `rect`, in whatever colour is set.
///
/// Proportions are expressed as fractions of `rect` so the same routine draws
/// the 1024-point app icon and the small menu-bar template.
func drawTree(in rect: NSRect, color: NSColor) {
    let w = rect.width
    /*
     * Four rows stack vertically here - the root and three children - where
     * procfs's process tree has only two. The nodes are correspondingly
     * smaller: sized for two rows they very nearly touch, which reads as a
     * blob rather than a tree. The stroke keeps roughly procfs's ratio of line
     * weight to node size.
     */
    let stroke = w * 0.075
    let node = w * 0.095          // radius of a node circle

    let spineX = rect.minX + w * 0.20
    let childX = rect.maxX - w * 0.20

    /*
     * Positions are node *centres*, inset by the node radius so the circles
     * touch the edges of `rect` rather than overflowing it. Getting this wrong
     * is not subtle: the glyph then sits low in the plate and reads as
     * bottom-heavy even though its box is centred.
     */
    let rootY = rect.maxY - node
    let lastY = rect.minY + node
    let span = rootY - lastY

    // Three branches, evenly spaced below the root.
    let branchYs = (1...3).map { rootY - span * CGFloat($0) / 3.0 }

    color.set()

    // The spine, from the root node down to the last branch.
    let spine = NSBezierPath()
    spine.lineWidth = stroke
    spine.lineCapStyle = .round
    spine.move(to: NSPoint(x: spineX, y: rootY))
    spine.line(to: NSPoint(x: spineX, y: branchYs.last!))
    spine.stroke()

    // Each branch, running right from the spine to its child node.
    for y in branchYs {
        let branch = NSBezierPath()
        branch.lineWidth = stroke
        branch.lineCapStyle = .round
        branch.move(to: NSPoint(x: spineX, y: y))
        branch.line(to: NSPoint(x: childX, y: y))
        branch.stroke()
    }

    // The nodes: the root, and one at the end of each branch.
    func dot(_ x: CGFloat, _ y: CGFloat) {
        NSBezierPath(ovalIn: NSRect(x: x - node, y: y - node,
                                    width: node * 2, height: node * 2)).fill()
    }
    dot(spineX, rootY)
    for y in branchYs { dot(childX, y) }
}

// MARK: - Drawing

/// Draw into a bitmap of exactly `size` pixels.
///
/// Explicitly, rather than through NSImage.lockFocus: that captures at the
/// display's backing scale, so on a Retina machine a 1024-point icon comes out
/// 2048 pixels. The icon's pixel size must not depend on which machine built
/// it.
func makeImage(_ size: CGFloat, _ draw: (NSRect) -> Void) -> NSBitmapImageRep {
    let px = Int(size)
    guard let rep = NSBitmapImageRep(
            bitmapDataPlanes: nil, pixelsWide: px, pixelsHigh: px,
            bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true,
            isPlanar: false, colorSpaceName: .deviceRGB,
            bytesPerRow: 0, bitsPerPixel: 0) else {
        FileHandle.standardError.write("cannot create bitmap\n".data(using: .utf8)!)
        exit(1)
    }
    rep.size = NSSize(width: size, height: size)   // one point per pixel

    NSGraphicsContext.saveGraphicsState()
    NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: rep)
    NSGraphicsContext.current?.imageInterpolation = .high
    draw(NSRect(x: 0, y: 0, width: size, height: size))
    NSGraphicsContext.restoreGraphicsState()

    return rep
}

/// The full-colour app icon: gradient squircle, soft shadow, white glyph.
func appIcon() -> NSBitmapImageRep {
    makeImage(canvas) { _ in
        let plate = NSBezierPath(roundedRect: plateRect,
                                 xRadius: cornerRadius, yRadius: cornerRadius)

        // Shadow beneath the plate, as the stock icons have.
        NSGraphicsContext.saveGraphicsState()
        let shadow = NSShadow()
        shadow.shadowColor = NSColor.black.withAlphaComponent(0.28)
        shadow.shadowOffset = NSSize(width: 0, height: -14)
        shadow.shadowBlurRadius = 34
        shadow.set()
        NSColor.black.set()
        plate.fill()
        NSGraphicsContext.restoreGraphicsState()

        // The gradient, clipped to the plate.
        NSGraphicsContext.saveGraphicsState()
        plate.addClip()
        NSGradient(starting: gradientTop, ending: gradientBottom)?
            .draw(in: plateRect, angle: -90)
        NSGraphicsContext.restoreGraphicsState()

        // The glyph, centred in the plate.
        let side = plateRect.width * 0.56
        let glyph = NSRect(x: plateRect.midX - side / 2,
                           y: plateRect.midY - side / 2,
                           width: side, height: side)
        drawTree(in: glyph, color: .white)
    }
}

/// The menu-bar icon: the glyph alone, black on transparent.
///
/// A single template image rather than the separate light and dark files
/// procfs ships: macOS tints a template with the menu bar's own colour, so one
/// file is correct in both appearances and while highlighted.
func menuIcon(_ size: CGFloat) -> NSBitmapImageRep {
    makeImage(size) { rect in
        drawTree(in: rect.insetBy(dx: size * 0.10, dy: size * 0.10), color: .black)
    }
}

// MARK: - Writing

func write(_ rep: NSBitmapImageRep, to path: String) {
    guard let png = rep.representation(using: .png, properties: [:]) else {
        FileHandle.standardError.write("cannot encode \(path)\n".data(using: .utf8)!)
        exit(1)
    }
    do {
        try png.write(to: URL(fileURLWithPath: path))
        print("  wrote \(path)")
    } catch {
        FileHandle.standardError.write("cannot write \(path): \(error)\n".data(using: .utf8)!)
        exit(1)
    }
}

let dir = CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "."
write(appIcon(), to: "\(dir)/appicon.png")
write(menuIcon(88), to: "\(dir)/icon_menu.png")
