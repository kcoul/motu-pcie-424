#!/bin/zsh
# Disassemble MOTU's own CueMix FX — the 2025 universal build is not stripped and
# contains the PCI/AudioWire back end, so CoreDeviceAW can be read directly.
# Findings from it live in docs/CUEMIX-API.md.
#
#   tools/dis-cuemix.sh syms  [pattern]        list matching symbols, demangled
#   tools/dis-cuemix.sh fn    <pattern>        disassemble every matching function
#   tools/dis-cuemix.sh range <start> <stop>   disassemble an address range
#   tools/dis-cuemix.sh calls <symbol-or-addr> which functions reference it
#
# --disassemble-symbols does NOT work on these: the CoreDeviceAW methods are
# local ('t') symbols. Hence the address-range approach.

set -e
APP="${CUEMIX_APP:-/Applications/CueMix FX.app}"
BIN="$APP/Contents/MacOS/CueMix FX"
ARCH="${CUEMIX_ARCH:-arm64}"
OD=/Library/Developer/CommandLineTools/usr/bin/llvm-objdump
WORK="${TMPDIR:-/tmp}/dis-cuemix-$ARCH"

[[ -f "$BIN" ]] || { print -u2 "not found: $BIN  (set CUEMIX_APP)"; exit 1 }

if [[ ! -f "$WORK.bin" || "$BIN" -nt "$WORK.bin" ]]; then
  lipo -thin "$ARCH" "$BIN" -output "$WORK.bin"
  nm -n "$WORK.bin" | awk 'NF==3 && $1 ~ /^[0-9a-f]+$/ {print $1, $3}' > "$WORK.sym"
fi

# NOTE: compare as strings. Addresses are zero-padded fixed-width hex, but awk
# reads one containing 'e' (e.g. 100001e94) as scientific notation, so a bare
# `$1 > s` silently picks the wrong symbol. Prefixing forces a string compare.
next_addr() { awk -v s="$1" '("x" $1) > ("x" s) { print $1; exit }' "$WORK.sym" }

case "$1" in
  syms)
    if [[ -n "$2" ]]; then grep -- "$2" "$WORK.sym" | c++filt; else c++filt < "$WORK.sym"; fi ;;

  fn)
    [[ -n "$2" ]] || { print -u2 "usage: $0 fn <pattern>"; exit 1 }
    grep -n -- "$2" "$WORK.sym" | cut -d: -f1 | while read ln; do
      line=$(sed -n "${ln}p" "$WORK.sym"); start=${line%% *}; name=${line##* }
      stop=$(next_addr "$start")
      print "\n######## $(print $name | c++filt)   [0x$start .. 0x$stop]"
      $OD -d --no-show-raw-insn --symbolize-operands \
          --start-address="0x$start" --stop-address="0x$stop" "$WORK.bin" | tail -n +5
    done ;;

  range)
    [[ -n "$3" ]] || { print -u2 "usage: $0 range <start-hex> <stop-hex>"; exit 1 }
    $OD -d --no-show-raw-insn --symbolize-operands \
        --start-address="0x${2#0x}" --stop-address="0x${3#0x}" "$WORK.bin" | tail -n +5 ;;

  calls)
    [[ -n "$2" ]] || { print -u2 "usage: $0 calls <symbol-or-addr>"; exit 1 }
    [[ -f "$WORK.full" && "$WORK.full" -nt "$WORK.bin" ]] || \
      $OD -d --no-show-raw-insn --symbolize-operands "$WORK.bin" > "$WORK.full" 2>/dev/null
    awk -v t="$2" '
      /^[0-9a-f]{16} </ { cur = $0; sub(/^[0-9a-f]+ </, "", cur); sub(/>:$/, "", cur); next }
      index($0, t) && /	(bl|b)	/ { if (!(cur in seen)) { seen[cur]; print cur "\n      " $0 } }
    ' "$WORK.full" | c++filt ;;

  *) sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//'; exit 1 ;;
esac
