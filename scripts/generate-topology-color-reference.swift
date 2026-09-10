import AppKit
func rgb(_ r: Double, _ g: Double, _ b: Double) -> UInt32 {
  let c = NSColor(calibratedRed: r, green: g, blue: b, alpha: 1).usingColorSpace(.sRGB)!
  return UInt32((c.redComponent * 255).rounded()) << 16 | UInt32((c.greenComponent * 255).rounded()) << 8 | UInt32((c.blueComponent * 255).rounded())
}
print("// Generated from Cocoa's calibrated colors converted to sRGB.\n// Retains the original heatColor stops and interpolation, on both platforms.\n#pragma once\n#include <array>\n#include <cstdint>\nnamespace s3g::portable_gui::topology_colors {")
print("inline constexpr std::array<uint32_t, 256> gray {{")
for i in 0..<256 {
  let v = Double(i) / 255
  print(String(format: "0x%06x,", rgb(v,v,v)), terminator: i % 8 == 7 ? "\n" : " ")
}
print("}};\ninline constexpr std::array<uint32_t, 1025> heat {{")
let stops: [(Double,Double,Double,Double)] = [(0,10,24,94),(0.22,0,146,232),(0.48,255,232,42),(0.72,255,84,12),(1,238,0,0)]
for i in 0...1024 {
  let v = Double(i) / 1024
  let index = (1..<stops.count).first(where: { v <= stops[$0].0 })!
  let a = stops[index-1], b = stops[index]
  let m = (v-a.0)/(b.0-a.0)
  print(String(format: "0x%06x,", rgb((a.1+(b.1-a.1)*m)/255,(a.2+(b.2-a.2)*m)/255,(a.3+(b.3-a.3)*m)/255)), terminator: i % 8 == 7 ? "\n" : " ")
}
print("\n}};\n} // namespace s3g::portable_gui::topology_colors")
