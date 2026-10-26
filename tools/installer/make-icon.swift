// Draw the RS Re-ARMed Installer icon (an orange guitar pick with a download arrow) as a 1024x1024 PNG.
//   swift tools/installer/make-icon.swift <out.png>
// build.sh turns it into AppIcon.icns. Our own artwork: no game assets are used.
import CoreGraphics
import Foundation
import ImageIO
import UniformTypeIdentifiers

let size = 1024
let rgb = CGColorSpace(name: CGColorSpace.sRGB)!
let ctx = CGContext(data: nil, width: size, height: size, bitsPerComponent: 8, bytesPerRow: 0, space: rgb,
                    bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!

func color(_ r: CGFloat, _ g: CGFloat, _ b: CGFloat, _ a: CGFloat = 1) -> CGColor {
    CGColor(colorSpace: rgb, components: [r / 255, g / 255, b / 255, a])!
}

func fillGradient(_ path: CGPath, _ top: CGColor, _ bottom: CGColor, from y0: CGFloat, to y1: CGFloat) {
    ctx.saveGState()
    ctx.addPath(path)
    ctx.clip()
    let g = CGGradient(colorsSpace: rgb, colors: [top, bottom] as CFArray, locations: [0, 1])!
    ctx.drawLinearGradient(g, start: CGPoint(x: 0, y: y1), end: CGPoint(x: 0, y: y0), options: [])
    ctx.restoreGState()
}

// Background: the macOS icon grid's rounded square (824 pt inside 1024), dark slate. CG's y axis points up.
let tile = CGRect(x: 100, y: 100, width: 824, height: 824)
let tilePath = CGPath(roundedRect: tile, cornerWidth: 185, cornerHeight: 185, transform: nil)
ctx.saveGState()
ctx.setShadow(offset: CGSize(width: 0, height: -10), blur: 28, color: color(0, 0, 0, 0.35))
ctx.addPath(tilePath)
ctx.setFillColor(color(30, 34, 44))
ctx.fillPath()
ctx.restoreGState()
fillGradient(tilePath, color(52, 58, 74), color(18, 20, 28), from: tile.minY, to: tile.maxY)

// Guitar pick: wide rounded top, point at the bottom.
let pick = CGMutablePath()
pick.move(to: CGPoint(x: 512, y: 215))
pick.addCurve(to: CGPoint(x: 262, y: 650), control1: CGPoint(x: 370, y: 300), control2: CGPoint(x: 240, y: 500))
pick.addCurve(to: CGPoint(x: 762, y: 650), control1: CGPoint(x: 290, y: 870), control2: CGPoint(x: 734, y: 870))
pick.addCurve(to: CGPoint(x: 512, y: 215), control1: CGPoint(x: 784, y: 500), control2: CGPoint(x: 654, y: 300))
pick.closeSubpath()
ctx.saveGState()
ctx.setShadow(offset: CGSize(width: 0, height: -14), blur: 30, color: color(0, 0, 0, 0.5))
ctx.addPath(pick)
ctx.setFillColor(color(240, 120, 30))
ctx.fillPath()
ctx.restoreGState()
fillGradient(pick, color(255, 178, 64), color(214, 72, 22), from: 215, to: 815)

// Download arrow, white: one outline (shaft with rounded top, then the head).
let arrow = CGMutablePath()
arrow.move(to: CGPoint(x: 476, y: 520))
arrow.addArc(tangent1End: CGPoint(x: 476, y: 700), tangent2End: CGPoint(x: 548, y: 700), radius: 20)
arrow.addArc(tangent1End: CGPoint(x: 548, y: 700), tangent2End: CGPoint(x: 548, y: 520), radius: 20)
arrow.addLine(to: CGPoint(x: 548, y: 520))
arrow.addLine(to: CGPoint(x: 632, y: 520))
arrow.addLine(to: CGPoint(x: 512, y: 380))
arrow.addLine(to: CGPoint(x: 392, y: 520))
arrow.closeSubpath()
ctx.saveGState()
ctx.setShadow(offset: CGSize(width: 0, height: -6), blur: 14, color: color(90, 30, 0, 0.45))
ctx.addPath(arrow)
ctx.setFillColor(color(255, 255, 255))
ctx.fillPath()
ctx.restoreGState()

let out = URL(fileURLWithPath: CommandLine.arguments[1])
let dest = CGImageDestinationCreateWithURL(out as CFURL, UTType.png.identifier as CFString, 1, nil)!
CGImageDestinationAddImage(dest, ctx.makeImage()!, nil)
guard CGImageDestinationFinalize(dest) else { fatalError("could not write \(out.path)") }
