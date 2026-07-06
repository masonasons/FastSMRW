#!/usr/bin/env bash
# Builds the macOS app: fetch deps, (re)generate the Xcode project from
# project.yml, then xcodebuild. The generated FastSMRW.xcodeproj is gitignored —
# project.yml is the source of truth. Requires XcodeGen (brew install xcodegen).
#
# Usage:  macos/build.sh [release]
set -euo pipefail
cd "$(dirname "$0")"

../macos/fetch-deps.sh

if ! command -v xcodegen >/dev/null 2>&1; then
    echo "XcodeGen not found. Install with: brew install xcodegen" >&2
    exit 1
fi
xcodegen generate

CONFIG=Debug
[ "${1:-}" = "release" ] && CONFIG=Release

xcodebuild -project FastSMRW.xcodeproj -scheme FastSMRW \
    -configuration "$CONFIG" -destination 'platform=macOS' build

echo
echo "Built FastSMRW.app ($CONFIG). Launch it from Xcode, or:"
echo "  open ~/Library/Developer/Xcode/DerivedData/FastSMRW-*/Build/Products/$CONFIG/FastSMRW.app"
