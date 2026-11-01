#!/bin/zsh
# Build Release and install into /Applications.
#
#   tools/install.sh                 # PCI Audio Setup
#   tools/install.sh cuemix          # CueMix FX
#   tools/install.sh all             # both
#   tools/install.sh setup --no-launch --dest ~/Applications
#
# This exists because the alternative -- running whatever .app happens to be
# lying around in build/ -- wasted an afternoon on 2026-10-31. Two bugs were
# reported against a binary that predated their fix by six weeks, and the
# repository had no one-step way to say "put the current code on this machine".
#
# Notes on the three things that are easy to get wrong here:
#
#   ditto, not cp -R. The bundle is code-signed, and cp -R does not reliably
#   carry extended attributes; the copy then fails Gatekeeper and the HAL hands
#   back a NULL card pointer, which looks like a driver fault rather than a
#   broken copy.
#
#   Quit first. Replacing the bundle under a running process leaves it with a
#   deleted executable, and the next CommitChanges can hit a prefs file the old
#   instance still has open.
#
#   Check what is already there. /Applications/MOTU PCI Audio Setup.app and
#   /Applications/CueMix FX.app are names MOTU and a third-party rebuild both
#   use, and the third-party one even reuses MOTU's bundle id and version
#   string. We refuse to overwrite a bundle that is not ours unless --force,
#   so nobody clobbers reference prior art by accident.
set -e

ROOT="${0:A:h:h}"
cd "$ROOT"

BUILD="build/release"
DEST="/Applications"
LAUNCH=1
FORCE=0
RELEASE_SIGN=0
TARGETS=()

while (( $# )); do
  case "$1" in
    setup)      TARGETS+=(setup) ;;
    cuemix)     TARGETS+=(cuemix) ;;
    all)        TARGETS+=(setup cuemix) ;;
    --dest)     shift; DEST="${~1}" ;;
    --no-launch) LAUNCH=0 ;;
    --force)    FORCE=1 ;;
    # Hardened runtime + secure timestamp, for a build you intend to notarize.
    # Needs a Developer ID identity; see CMakeLists.txt and tools/notarize.sh.
    --release-sign) RELEASE_SIGN=1 ;;
    -h|--help)
      sed -n '2,26p' "$0" | sed 's/^# \{0,1\}//'
      exit 0 ;;
    *) print -u2 "unknown argument: $1"; exit 2 ;;
  esac
  shift
done

(( ${#TARGETS} )) || TARGETS=(setup)

# target -> cmake target, source subdirectory, PRODUCT_NAME, installed bundle id.
# PRODUCT_NAME and BUNDLE_ID mirror src/*/CMakeLists.txt. They are duplicated
# here on purpose: the destination has to be known *before* the build, so that
# a refusal costs a second rather than a full compile. The glob after the build
# cross-checks them, so drift is caught rather than silently installed.
typeset -A CMAKE_TARGET SRC_DIR PRODUCT BUNDLE_ID
CMAKE_TARGET=(setup PCIAudioSetup      cuemix CueMixFX)
SRC_DIR=(     setup pci-audio-setup    cuemix cuemix-fx)
PRODUCT=(     setup "MOTU PCI Audio Setup" \
              cuemix "CueMix FX")
BUNDLE_ID=(   setup space.zenbox.motu.pci-audio-setup \
              cuemix space.zenbox.motu.cuemix-fx)

# Refuse to stomp on something that is not a previous install of ours, before
# spending any time building.
for t in $TARGETS; do
  dst="$DEST/${PRODUCT[$t]}.app"
  [[ -d "$dst" ]] || continue
  existing=$(/usr/libexec/PlistBuddy -c "Print CFBundleIdentifier" \
             "$dst/Contents/Info.plist" 2>/dev/null || true)
  if [[ "$existing" != "${BUNDLE_ID[$t]}" ]] && (( ! FORCE )); then
    print -u2 "refusing to replace $dst"
    print -u2 "  it is \"${existing:-unknown}\", not \"${BUNDLE_ID[$t]}\""
    print -u2 "  pass --force if you really mean to replace it"
    exit 1
  fi
done

CMAKE_ARGS=(-B "$BUILD" -DCMAKE_BUILD_TYPE=Release)
if (( RELEASE_SIGN )); then
  CMAKE_ARGS+=(-DMOTU_RELEASE_SIGN=ON)
fi

print "configuring $BUILD"
cmake $CMAKE_ARGS >/dev/null

for t in $TARGETS; do
  tgt="${CMAKE_TARGET[$t]}"
  print "\n=== $tgt ==="
  cmake --build "$BUILD" --target "$tgt" -j$(sysctl -n hw.ncpu)

  # JUCE drops the bundle in <target>_artefacts/<CONFIG>/. Glob rather than
  # spell the product name, so renaming PRODUCT_NAME does not break this.
  app=("$BUILD/src/${SRC_DIR[$t]}/${tgt}_artefacts/Release/"*.app(N))
  (( ${#app} == 1 )) || { print -u2 "expected one .app, found ${#app}"; exit 1 }
  src="${app[1]}"
  name="${src:t}"
  dst="$DEST/$name"

  # The pre-flight guard above checked PRODUCT[$t]; if the build produced some
  # other name, that check covered the wrong path.
  if [[ "$name" != "${PRODUCT[$t]}.app" ]]; then
    print -u2 "PRODUCT_NAME drift: built \"$name\", tools/install.sh expects \"${PRODUCT[$t]}.app\""
    exit 1
  fi

  # A running instance holds the card and its prefs file; let it exit first.
  if pgrep -f "$dst/Contents/MacOS/" >/dev/null 2>&1; then
    print "quitting the running $name"
    osascript -e "quit app id \"${BUNDLE_ID[$t]}\"" >/dev/null 2>&1 || true
    for i in {1..20}; do
      pgrep -f "$dst/Contents/MacOS/" >/dev/null 2>&1 || break
      sleep 0.25
    done
  fi

  rm -rf "$dst"
  ditto "$src" "$dst"
  codesign --verify --strict "$dst"
  print "installed $dst"

  if (( LAUNCH )); then
    open -a "$dst"
  fi
done
