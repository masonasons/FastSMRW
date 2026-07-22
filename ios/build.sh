#!/usr/bin/env bash
# Builds the iOS app: fetch deps, (re)generate the Xcode project from
# project.yml, then xcodebuild. The generated FastSMRW.xcodeproj is gitignored —
# project.yml is the source of truth. Requires XcodeGen (brew install xcodegen).
#
# Usage:  ios/build.sh [release] [device]
#   default: Debug build for the iOS Simulator (no signing needed)
#   device:  build for a real iPhone (automatic signing via the dev team)
set -euo pipefail
cd "$(dirname "$0")"

../macos/fetch-deps.sh

if ! command -v xcodegen >/dev/null 2>&1; then
    echo "XcodeGen not found. Install with: brew install xcodegen" >&2
    exit 1
fi
xcodegen generate

CONFIG=Debug
DEST='generic/platform=iOS Simulator'
EXTRA=()
for arg in "$@"; do
    case "$arg" in
        release) CONFIG=Release ;;
        device)
            DEST='generic/platform=iOS'
            EXTRA+=(-allowProvisioningUpdates)
            ;;
    esac
done

xcodebuild -project FastSMRW.xcodeproj -scheme FastSMRW \
    -configuration "$CONFIG" -destination "$DEST" \
    ${EXTRA[@]+"${EXTRA[@]}"} build

echo
echo "Built FastSMRW ($CONFIG, $DEST)."
echo "To run on your iPhone, open ios/FastSMRW.xcodeproj in Xcode and hit Run."
