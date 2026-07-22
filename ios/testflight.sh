#!/usr/bin/env bash
# Archive, export, and upload FastSMRW-iOS to TestFlight via the App Store
# Connect API key (same key/flow as FastSMApple's Scripts/testflight.sh).
# Requires an App Store Connect app record for me.masonasons.FastSMRW.
#
# The marketing version comes from core/src/version.cpp; the build number is
# the git commit count, so every upload is automatically higher than the last.
set -euo pipefail
cd "$(dirname "$0")"

TEAM="9QBYDAX396"
KEY_ID="FB9N292RPN"
ISSUER="02117eeb-7d87-4d4d-bf11-850f80204c4c"
KEY_PATH="$HOME/.appstoreconnect/private_keys/AuthKey_${KEY_ID}.p8"

VERSION=$(sed -n 's/.*return "\([0-9.]*\)";/\1/p' ../core/src/version.cpp | head -1)
BUILD=$(git rev-list --count HEAD)
echo "==> FastSMRW $VERSION ($BUILD)"

../macos/fetch-deps.sh
xcodegen generate

ARCHIVE="build/FastSMRW.xcarchive"
EXPORT_DIR="build/export"

echo "==> Archiving"
xcodebuild archive -project FastSMRW.xcodeproj -scheme FastSMRW \
    -configuration Release -destination 'generic/platform=iOS' \
    -archivePath "$ARCHIVE" -derivedDataPath build/dd-archive \
    -allowProvisioningUpdates \
    -authenticationKeyPath "$KEY_PATH" -authenticationKeyID "$KEY_ID" \
    -authenticationKeyIssuerID "$ISSUER" \
    DEVELOPMENT_TEAM="$TEAM" CODE_SIGN_STYLE=Automatic \
    MARKETING_VERSION="$VERSION" CURRENT_PROJECT_VERSION="$BUILD"

echo "==> Exporting (App Store)"
rm -rf "$EXPORT_DIR"
xcodebuild -exportArchive -archivePath "$ARCHIVE" -exportPath "$EXPORT_DIR" \
    -exportOptionsPlist exportOptions.plist -allowProvisioningUpdates \
    -authenticationKeyPath "$KEY_PATH" -authenticationKeyID "$KEY_ID" \
    -authenticationKeyIssuerID "$ISSUER"

echo "==> Uploading to TestFlight"
xcrun altool --upload-app -f "$EXPORT_DIR"/*.ipa -t ios \
    --apiKey "$KEY_ID" --apiIssuer "$ISSUER"

echo "Uploaded. Processing in App Store Connect -> TestFlight may take a few minutes."
