// The iOS app's icon: the game's own icon (the pick from Rocksmith2014.exe, as the Mac app's RS2014.icns is) on an
// opaque 1024x1024 square, as iOS wants it (it shows transparency as black). The installer's dark slate behind it.
//   swift ios/make_icon.swift <game icon.png> <out.png>
// ios/build_lp.sh runs it with the icon read from the user's game image: no game asset is in the repository.
import CoreGraphics
import Foundation
import ImageIO
import UniformTypeIdentifiers

let args = CommandLine.arguments
guard args.count == 3,
      let src = CGImageSourceCreateWithURL(URL(fileURLWithPath: args[1]) as CFURL, nil),
      let pick = CGImageSourceCreateImageAtIndex(src, 0, nil) else {
    FileHandle.standardError.write("usage: make_icon.swift <icon.png> <out.png>\n".data(using: .utf8)!)
    exit(2)
}
let size = 1024
let rgb = CGColorSpace(name: CGColorSpace.sRGB)!
let ctx = CGContext(data: nil, width: size, height: size, bitsPerComponent: 8, bytesPerRow: 0, space: rgb,
                    bitmapInfo: CGImageAlphaInfo.noneSkipLast.rawValue)!
func color(_ r: CGFloat, _ g: CGFloat, _ b: CGFloat) -> CGColor {
    CGColor(colorSpace: rgb, components: [r / 255, g / 255, b / 255, 1])!
}
// the installer icon's slate (tools/installer/make-icon.swift), edge to edge: iOS rounds the corners itself
let g = CGGradient(colorsSpace: rgb, colors: [color(52, 58, 74), color(18, 20, 28)] as CFArray, locations: [0, 1])!
ctx.drawLinearGradient(g, start: CGPoint(x: 0, y: size), end: CGPoint(x: 0, y: 0), options: [])
ctx.interpolationQuality = .high
let inset = 150.0
ctx.draw(pick, in: CGRect(x: inset, y: inset, width: Double(size) - 2 * inset, height: Double(size) - 2 * inset))
let out = CGImageDestinationCreateWithURL(URL(fileURLWithPath: args[2]) as CFURL, UTType.png.identifier as CFString, 1, nil)!
CGImageDestinationAddImage(out, ctx.makeImage()!, nil)
exit(CGImageDestinationFinalize(out) ? 0 : 1)
