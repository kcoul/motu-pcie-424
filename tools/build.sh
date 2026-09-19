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

# MACOSX_DEPLOYMENT_TARGET=10.14 builds something the Mojave volume can run
# (MotuSpy). The default is this machine's own version.
MINVER=${MACOSX_DEPLOYMENT_TARGET:-$(sw_vers -productVersion | cut -d. -f1-2)}

$CXX -isysroot "$SDK" -mmacosx-version-min=$MINVER -arch x86_64 -std=c++17 -ObjC++ -O2 -Wall \
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
<key>LSMinimumSystemVersion</key><string>$MINVER</string>
</dict></plist>
PLIST

# Signing identity comes from tools/sign-identity.sh -- shared with
# CMakeLists.txt so the two cannot drift. It explains why "the first Apple
# Development cert" is the wrong answer.
IDENTITY=$(tools/sign-identity.sh)
if [[ "$IDENTITY" == "-" ]]; then
  print "signing ad-hoc (expect a microphone prompt on each rebuild)"
else
  print "signing as: $IDENTITY"
fi

# NOT hardened (--options runtime) on purpose. Hardened runtime turns on library
# validation, and the card only works because MOTU's HALPlugin -- signed by MOTU,
# a different team -- gets loaded into this process. Hardening this app without
# also adding com.apple.security.cs.disable-library-validation would block that
# load, and the failure would look like "the card pointer is NULL again".
codesign -f -s "$IDENTITY" --entitlements tools/entitlements.plist "$APP"
print "built $APP"

if (( RUN )); then
  rm -f /tmp/motu-dump.txt
  open -W -a "$ROOT/$APP" 2>/dev/null || open -a "$ROOT/$APP"
  for i in {1..40}; do [[ -s /tmp/motu-dump.txt ]] && break; sleep 0.5; done
  sleep 1
  cat /tmp/motu-dump.txt
fi
