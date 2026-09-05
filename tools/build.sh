#!/bin/zsh
# Build, bundle, ad-hoc sign and optionally run a MOTU card tool.
#   tools/build.sh src/common/motu-card-access.mm MotuCardAccess --run
set -e
SRC="${1:-src/common/motu-card-access.mm}"
NAME="${2:-MotuCardAccess}"
ROOT="${0:A:h:h}"
cd "$ROOT"

# Xcode.app's licence is unaccepted, which blocks clang/swiftc when
# xcode-select points at it. Use the Command Line Tools toolchain instead.
export DEVELOPER_DIR=/Library/Developer/CommandLineTools
CXX=/Library/Developer/CommandLineTools/usr/bin/clang++
SDK=$(ls -d /Library/Developer/CommandLineTools/SDKs/MacOSX*.sdk | tail -1)

APP="build/$NAME.app"
mkdir -p "$APP/Contents/MacOS"
$CXX -isysroot "$SDK" -std=c++17 -ObjC++ -O2 \
     -o "$APP/Contents/MacOS/$NAME" "$SRC" \
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

codesign -f -s - --entitlements tools/entitlements.plist "$APP"
echo "built $APP"
[[ "$3" == "--run" ]] && { rm -f /tmp/motu-spike.txt; open -a "$ROOT/$APP"; sleep 8; cat /tmp/motu-spike.txt; }
exit 0
