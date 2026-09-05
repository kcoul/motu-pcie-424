#!/bin/zsh
# Post-reboot check: did the PCI-424 allocate a CueMix fader bank?
# Run this after booting with ALL interfaces powered on.

print "== boot time =="
sysctl -n kern.boottime
print "\n== interfaces on the AudioWire =="
ioreg -p IOService -w0 -c com_motu_driver_PCIAudio_Engine -r -d 1 2>/dev/null \
  | tr ',' '\n' | grep -E '"UniqueName"=' | sed 's/^ */  /'

print "\n== CueMix fader allocation =="
cue=$(ioreg -p IOService -w0 -c com_motu_driver_PCIAudio_Engine -r -d 1 2>/dev/null \
  | grep -oE '"CueMix" = \{[^}]*\}')
print "  $cue"
faders=$(print -r -- "$cue" | grep -oE '"CueMixFaders"=[0-9]+' | cut -d= -f2)

print "\n== CoreAudio =="
system_profiler SPAudioDataType 2>/dev/null | grep -A6 'PCI-424' | grep -E 'Channels|SampleRate' | sed 's/^ */  /'

print ""
if [[ -n "$faders" && "$faders" != "0" ]]; then
  print "CueMixFaders = $faders  -> non-zero. Launching CueMix FX..."
  open -a "/Applications/CueMix FX.app"
  sleep 6
  if pgrep -f "CueMix FX" >/dev/null; then
    print "RESULT: CueMix FX is RUNNING."
  else
    print "RESULT: CueMix FX exited anyway. Next: grant it Accessibility"
    print "        (Privacy & Security), then csr-active-config -> 02000000."
  fi
else
  print "CueMixFaders = 0  -> the driver still allocated no CueMix mixer."
  print "CueMix FX will exit; that is expected, not a new fault."
  print "Confirm every interface was powered BEFORE boot, then fall back to"
  print "Accessibility for CueMix FX, then the SIP change."
fi
