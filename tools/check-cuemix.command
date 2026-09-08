#!/bin/zsh
# Driver-state check for the PCI-424: what the kext thinks is attached, and how
# much of the CueMix mixer is allocated.
#
# NOTE: "CueMixFaders = 0" is the NORMAL idle state, not a fault. The card
# allocates faders as mixes are configured (0 used of 180 max here). An earlier
# theory blamed CueMix FX's immediate exit on this; that was wrong. The real
# blocker is the CoreAudio run-loop registration — see README.md and
# docs/HALPLUGIN-API.md. Do NOT disable SIP; it is not required.
#
# For the full picture use the real tool instead:
#   tools/build.sh MotuDump src/common/motu_card.mm src/common/motu_dump.mm --run

print "== boot time =="
sysctl -n kern.boottime

print "\n== interfaces on the AudioWire =="
ioreg -p IOService -w0 -c com_motu_driver_PCIAudio_Engine -r -d 1 2>/dev/null \
  | tr ',' '\n' | grep -E '"UniqueName"=' | sed 's/^ */  /'

print "\n== CueMix fader allocation =="
ioreg -p IOService -w0 -c com_motu_driver_PCIAudio_Engine -r -d 1 2>/dev/null \
  | grep -oE '"CueMix" = \{[^}]*\}' | sed 's/^/  /'

print "\n== CoreAudio =="
system_profiler SPAudioDataType 2>/dev/null | grep -A6 'PCI-424' \
  | grep -E 'Channels|SampleRate' | sed 's/^ */  /'
