#!/bin/zsh
# Build, sign, notarize and staple a release.
#
#   tools/notarize.sh [target ...]        default: PCIAudioSetup
#
# Requires, once:
#
#   1. A **Developer ID Application** certificate. An Apple Development cert is
#      not enough -- apps signed with one will not launch on other people's
#      Macs. Only a team's Account Holder can create one:
#        developer.apple.com/account/resources/certificates  ->  +
#        -> Developer ID Application  -> upload a CSR from Keychain Access
#           (Certificate Assistant > Request a Certificate From a Certificate
#            Authority > Saved to disk), then double-click the download.
#
#   2. Notarization credentials in the keychain:
#        xcrun notarytool store-credentials "$MOTU_NOTARY_PROFILE" \
#          --apple-id you@example.com --team-id <TEAMID> \
#          --password <app-specific-password>
#      The app-specific password comes from appleid.apple.com, not your Apple
#      ID password.
#
# Why hardened runtime is safe here, when it is what broke MOTU's own app:
# notarization requires it, hardening turns on library validation, and library
# validation is exactly what stops MOTU's 2017 SHA-1-signed PCI HALPlugin from
# loading (docs/ORIGINAL-UI.md). tools/entitlements.plist carries
# com.apple.security.cs.disable-library-validation to compensate. Verified
# against the card on 2026-09-19: hardened plus the entitlement reaches the
# card, hardened without it does not.
set -e

ROOT="${0:A:h:h}"
cd "$ROOT"

PROFILE="${MOTU_NOTARY_PROFILE:-motu-notary}"
BUILD="${MOTU_RELEASE_BUILD:-build/release}"
TARGETS=("${@:-PCIAudioSetup}")

ID="${MOTU_SIGN_IDENTITY:-$(security find-identity -v -p codesigning \
      | grep "Developer ID Application" | head -1 \
      | sed -E 's/^.*"(.*)"$/\1/')}"

if [[ -z "$ID" ]]; then
  print -u2 "No 'Developer ID Application' certificate found."
  print -u2 "Apps signed with an Apple Development cert will not launch on"
  print -u2 "other machines. See the header of this script for how to make one."
  exit 1
fi
print "signing identity: $ID"

export DEVELOPER_DIR=/Library/Developer/CommandLineTools
cmake -B "$BUILD" -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
      -DMOTU_RELEASE_SIGN=ON -DMOTU_SIGN_IDENTITY="$ID" >/dev/null
cmake --build "$BUILD" -j8 --target "${TARGETS[@]}"

for t in "${TARGETS[@]}"; do
  APP=$(find "$BUILD" -maxdepth 5 -name "*.app" -path "*${t}_artefacts*" | head -1)
  [[ -d "$APP" ]] || { print -u2 "no .app built for $t"; exit 1 }
  print "\n=== $APP ==="

  # Fail loudly rather than notarizing something that cannot work.
  codesign -v --strict "$APP"
  flags=$(codesign -d --verbose=2 "$APP" 2>&1 | grep -oE 'flags=0x[0-9a-f]+')
  [[ "$flags" == *10000* ]] || { print -u2 "not hardened ($flags); notarization will reject it"; exit 1 }
  codesign -d --entitlements - "$APP" 2>/dev/null \
    | grep -q disable-library-validation \
    || { print -u2 "missing disable-library-validation: this build cannot reach the card"; exit 1 }

  ZIP="${APP:r}.zip"
  ditto -c -k --keepParent "$APP" "$ZIP"
  print "submitting to Apple (this usually takes a few minutes)..."
  xcrun notarytool submit "$ZIP" --keychain-profile "$PROFILE" --wait
  xcrun stapler staple "$APP"
  rm -f "$ZIP"

  # What a downloader's Mac will actually decide.
  spctl -a -vvv -t exec "$APP"
  ditto -c -k --keepParent "$APP" "${APP:r}.zip"
  print "ready to upload: ${APP:r}.zip"
done
