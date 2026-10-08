#!/bin/sh
# Build "ShadowCast Viewer.app" (wii/shadowcast_view.swift): a low-latency
# viewer for the Wii through a USB capture device. Output in build/.
# --game-mode declares the app a game, so macOS turns Game Mode on while it is
# full screen (CPU/GPU priority, lower Bluetooth latency); macOS offers to turn
# it off from the menu bar. There is no API to toggle it at run time.
set -eu
GAME_MODE=0
[ "${1:-}" = "--game-mode" ] && GAME_MODE=1
ROOT=$(cd "$(dirname "$0")/.." && pwd)
APP="$ROOT/build/ShadowCast Viewer.app"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
swiftc -O "$ROOT/wii/shadowcast_view.swift" -o "$APP/Contents/MacOS/ShadowCast Viewer"
cat > "$APP/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key><string>ShadowCast Viewer</string>
  <key>CFBundleDisplayName</key><string>ShadowCast Viewer</string>
  <key>CFBundleIdentifier</key><string>com.gingerbeardman.shadowcast-viewer</string>
  <key>CFBundleExecutable</key><string>ShadowCast Viewer</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>1.0</string>
  <key>CFBundleVersion</key><string>1</string>
  <key>LSMinimumSystemVersion</key><string>13.0</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>NSCameraUsageDescription</key><string>Shows the capture device's video.</string>
  <key>NSMicrophoneUsageDescription</key><string>Plays the capture device's audio.</string>
  <key>NSCameraUseContinuityCameraDeviceType</key><true/>
</dict>
</plist>
PLIST
if [ $GAME_MODE = 1 ]; then
  plutil -insert LSApplicationCategoryType -string public.app-category.games "$APP/Contents/Info.plist"
  plutil -insert LSSupportsGameMode -bool YES "$APP/Contents/Info.plist"
fi
# Sign with a real identity (override with SIGN_ID) so camera and microphone
# permission survives rebuilds; an ad-hoc signature changes every build, so
# macOS asks again. Ad hoc only when no Developer ID is in the keychain.
SIGN_ID=${SIGN_ID:-$(security find-identity -v -p codesigning | sed -n 's/.*"\(Developer ID Application: [^"]*\)".*/\1/p' | head -1)}
codesign --force --sign "${SIGN_ID:--}" "$APP"
echo "$APP"
