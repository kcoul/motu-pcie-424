#!/bin/zsh
# Build, bundle, ad-hoc sign and optionally run a MOTU card tool.
#
#   tools/build.sh NAME src.mm [more.mm ...] [--run]
#
# A bundled .app is not optional: the HAL plugin hands out the card pointer only
# to a real NSApplication, and only when the process is signed with the
# audio-input entitlement. A bare CLI always gets NULL.
set -e

NAME="$1"; shift || { print -u2 "usage: build.sh NAME src... [--run]"; exit 2 }
RUN=0
SRCS=()
for a in "$@"; do
  [[ "$a" == "--run" ]] && { RUN=1; continue }
  SRCS+=("$a")
done
(( ${#SRCS} )) || { print -u2 "no sources given"; exit 2 }

ROOT="${0:A:h:h}"
cd "$ROOT"

# Xcode.app's licence is unaccepted, which blocks clang when xcode-select points
# at it. Use the Command Line Tools toolchain instead.
export DEVELOPER_DIR=/Library/Developer/CommandLineTools
CXX=/Library/Developer/CommandLineTools/usr/bin/clang++
SDK=$(ls -d /Library/Developer/CommandLineTools/SDKs/MacOSX*.sdk | tail -1)

APP="build/$NAME.app"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS"

$CXX -isysroot "$SDK" -std=c++17 -ObjC++ -O2 -Wall \
     -Isrc/common \
     -o "$APP/Contents/MacOS/$NAME" "${SRCS[@]}" \
     -framework Cocoa -framework CoreAudio -framework CoreFoundation

cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleExecutable</key><string>$NAME</string>
<key>CFBundleIdentifier</key><string>space.zenbox.$NAME</string>
<key>CFBundleName</key><string>$NAME</string>
<key>CFBundlePackageType</key><string>APPL</string>
<key>CFBundleShortVersionString</key><string>1.0</string>
<key>CFBundleVersion</key><string>1.0</string>
<key>NSMicrophoneUsageDescription</key><string>Access to the MOTU PCIe-424 audio inputs</string>
<key>NSPrincipalClass</key><string>NSApplication</string>
</dict></plist>
PLIST

# Signing identity: ad-hoc gives every build a fresh cdhash, so TCC sees each
# rebuild as a new app and re-prompts for the microphone. A real certificate
# keeps the designated requirement stable and the grant sticks. Use one if the
# keychain has it; fall back to ad-hoc so the build never depends on it.
IDENTITY=$(security find-identity -v -p codesigning 2>/dev/null \
           | sed -n 's/.*"\(Apple Development[^"]*\)".*/\1/p' | head -1)
if [[ -n "$IDENTITY" ]]; then
  print "signing as: $IDENTITY"
else
  IDENTITY="-"
fi
codesign -f -s "$IDENTITY" --entitlements tools/entitlements.plist "$APP"
print "built $APP"

if (( RUN )); then
  rm -f /tmp/motu-dump.txt
  open -W -a "$ROOT/$APP" 2>/dev/null || open -a "$ROOT/$APP"
  for i in {1..40}; do [[ -s /tmp/motu-dump.txt ]] && break; sleep 0.5; done
  sleep 1
  cat /tmp/motu-dump.txt
fi
