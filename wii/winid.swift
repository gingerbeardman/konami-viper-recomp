import CoreGraphics
import Foundation
// usage: winid PID  -> prints id of the largest on-screen window owned by PID
// Build: swiftc -O wii/winid.swift -o build/winid
let pid = Int32(CommandLine.arguments[1])!
let list = CGWindowListCopyWindowInfo([.optionOnScreenOnly], kCGNullWindowID) as! [[String: Any]]
var best: (Int, Double) = (0, 0)
for w in list where (w[kCGWindowOwnerPID as String] as? Int32) == pid {
    let b = w[kCGWindowBounds as String] as! [String: Double]
    let area = b["Width"]! * b["Height"]!
    if area > best.1 { best = (w[kCGWindowNumber as String] as! Int, area) }
}
if best.0 == 0 { exit(1) }
print(best.0)
