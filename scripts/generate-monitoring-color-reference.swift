import AppKit

// Calibrated Cocoa colors used by the literal monitoring canvas ports.
let colors: [UInt32] = [0xff2a1d, 0xff7f18, 0xe8d14a, 0x65d778, 0x2d7f4a, 0x111313, 0x171919, 0x171a1a, 0x394141, 0x3d4444, 0x3f4747, 0x54716e, 0x667070, 0x687070, 0x6b8f8a, 0x6eaaa2, 0x6fa49d, 0x75aaa3, 0x777f7f, 0x79a7a1, 0x9a7584, 0xa17386, 0xaeb7b7, 0xb8b8b8, 0xc49255, 0xd8d8d8, 0xe0e0e0]
for rgb in colors {
    let calibrated = NSColor(calibratedRed: CGFloat((rgb >> 16) & 255) / 255,
                             green: CGFloat((rgb >> 8) & 255) / 255,
                             blue: CGFloat(rgb & 255) / 255, alpha: 1)
    let converted = calibrated.usingColorSpace(.sRGB)!
    let result = (UInt32((converted.redComponent * 255).rounded()) << 16)
               | (UInt32((converted.greenComponent * 255).rounded()) << 8)
               | UInt32((converted.blueComponent * 255).rounded())
    print(String(format: "0x%06x -> 0x%06x", rgb, result))
}
