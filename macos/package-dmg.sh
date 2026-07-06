#!/usr/bin/env bash
# Builds FastSMRW.app (Release) and packages it into FastSMRW.dmg — the asset the
# in-app updater looks for (update_checker matches any .dmg release asset).
#
# Ad-hoc by default (runs locally; Gatekeeper warns on other Macs). For a
# distributable, notarizable build, set:
#   MACOS_SIGN_IDENTITY   e.g. "Developer ID Application: Your Name (TEAMID)"
#   MACOS_NOTARY_PROFILE  a `xcrun notarytool store-credentials` profile name
# When both are set the app is hardened-runtime signed and the DMG is notarized
# and stapled.
set -euo pipefail
cd "$(dirname "$0")/.."   # repo root

OUT=dist
DD=build/macos-dd
mkdir -p "$OUT"

./macos/fetch-deps.sh
command -v xcodegen >/dev/null 2>&1 || { echo "Install XcodeGen: brew install xcodegen" >&2; exit 1; }
( cd macos && xcodegen generate )

echo "=== Building Release ==="
xcodebuild -project macos/FastSMRW.xcodeproj -scheme FastSMRW -configuration Release \
    -derivedDataPath "$DD" -destination 'platform=macOS' build
APP="$DD/Build/Products/Release/FastSMRW.app"
[ -d "$APP" ] || { echo "app not found at $APP" >&2; exit 1; }

if [ -n "${MACOS_SIGN_IDENTITY:-}" ]; then
    echo "=== Signing with Developer ID (hardened runtime) ==="
    codesign --force --deep --options runtime --timestamp \
        --entitlements macos/FastSMRW.entitlements \
        --sign "$MACOS_SIGN_IDENTITY" "$APP"
    codesign --verify --deep --strict "$APP"
else
    echo "=== No MACOS_SIGN_IDENTITY set — ad-hoc build (Gatekeeper will warn) ==="
fi

echo "=== Building DMG ==="
STAGING=$(mktemp -d)
cp -R "$APP" "$STAGING/"
ln -s /Applications "$STAGING/Applications"   # drag-to-install layout
DMG="$OUT/FastSMRW.dmg"
rm -f "$DMG"
hdiutil create -volname "FastSMRW" -srcfolder "$STAGING" -ov -format UDZO "$DMG"
rm -rf "$STAGING"

if [ -n "${MACOS_SIGN_IDENTITY:-}" ]; then
    codesign --force --sign "$MACOS_SIGN_IDENTITY" "$DMG"
fi

if [ -n "${MACOS_NOTARY_PROFILE:-}" ]; then
    echo "=== Notarizing ==="
    xcrun notarytool submit "$DMG" --keychain-profile "$MACOS_NOTARY_PROFILE" --wait
    xcrun stapler staple "$DMG"
fi

echo
echo "Built $DMG"
