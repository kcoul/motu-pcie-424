# Next steps

State at the end of the 2026-09-12 session.

## Where things stand

**MOTU PCI Audio Setup: feature-complete, not yet in daily use.**
- Every control is wired to the call sequence recovered by disassembling MOTU's
  own console (`docs/CHANNEL-STATE.md`):
  - channel checkboxes, bank personalities, Enable Routing (with its "Disable"
    bank item), Enable Volume Controls, Default In/Out;
  - all three Options panes;
  - Save/Load Configuration, Refresh, live refresh from CoreAudio.
- **Edit Channel Names** replaces MOTU's i386 helper. *Import Names…* brought
  all 60 Mojave names across, and the driver saved them to this OS's prefs.
- Stress-tested with no crashes after the stale-Interface fix.
- MOTU's original icon.

**CueMix FX: console roughed in, live, read-only.**
- **Skins:** Classic is pixel-matched to MOTU's, using MOTU's own sprites
  (`assets/classic/`). Modern has the same layout. Switch in the application menu.
- **Live from the card:** strips = active inputs (12 with a lone HD192, up to
  96), MIX = output-pair buses, master fader and mute, fader budget, and
  talkback state.
- **Menu bar identical to MOTU's.** Unbuilt items say in the LCD which stage
  delivers them.
- **Placeholders:** MONO/STEREO, Scope sources, meters, the trim/pan/dB scales
  and the dim-knob range.
- MOTU's original icon.

**Tools**
- **MotuSpy** is on the Mojave Desktop: snapshot every readable card value,
  change one thing in MOTU's app, snapshot again, and it diffs the two.
- `src/motu-spy/bounds.mm` found the CueMix index ranges, and that the
  balance/width/mapping getters segfault past the end.

## Next session: pick one

1. **Stage 0 on the M4 + UltraLite mk3 Hybrid** (`docs/CUEMIX-PLAN.md`).
   Live, with Claude Code alongside CueMix FX:
   - check the binary: symbols, and whether the PCI backend (`CoreDeviceAW`)
     is still in it;
   - write an OSC logger for CueMix's parameter tree, including `/meters`
     and the `…/str` display strings;
   - write the results up as `docs/CUEMIX-MODEL.md`.
2. **Stage 1 on Mojave with MotuSpy.** Work through the controls in the plan's
   capture order: fader dB steps, pan, trim, solo/mute, MIX and OUTPUT,
   talkback. Plan a signal-through session for meters and clip LEDs:
   `ReadLevelMeters` needs decoding and snapshots alone won't do it.

Either one unlocks the same thing: **making CueMix's controls write**, starting
with faders, pan, mute and solo, once the encodings are known.

## Small known issues

- CueMix only re-reads channel names when the set of active inputs changes, so
  renames made while it is open show after a relaunch.
- The Modern skin can open scrolled a few strips in.
- The dim-knob range (0–255) and the "Sequencer using N faders" figure are
  guesses.
- `ORIGINAL-UI.md` predates the corrections in `CHANNEL-STATE.md`.

## Later

- Put PCI Audio Setup into daily use, including at the HD192-only studio, then
  install both apps to `/Applications` once trusted.
- CueMix stages 3–8: meters, talkback writes, configurations, analysis windows,
  control surfaces, and Modern skin polish.

## Still to capture from Mojave

- CueMix FX behaviour and meters, via MotuSpy (see above).

## Build

```sh
git submodule update --init --recursive
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
  cmake -B build/cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release
DEVELOPER_DIR=/Library/Developer/CommandLineTools cmake --build build/cmake -j8
open "build/cmake/src/pci-audio-setup/PCIAudioSetup_artefacts/Release/MOTU PCI Audio Setup.app"
open "build/cmake/src/cuemix-fx/CueMixFX_artefacts/Release/CueMix FX.app"
```

`DEVELOPER_DIR` is required: Xcode's licence is unaccepted, so CMake must be
pointed at the Command Line Tools toolchain.

Back-end regression check, any time the card seems wrong:

```sh
tools/build.sh MotuDump  src/common/motu_card.mm src/common/motu_dump.mm  --run
tools/build.sh MotuProbe src/common/motu_card.mm src/common/motu_probe.mm && \
  open build/MotuProbe.app && cat /tmp/motu-probe.txt
```

### Code signing

Ad-hoc signing gives every build a new cdhash, so TCC treats each rebuild as a
new app and re-prompts for the microphone. `tools/build.sh` and `motu_sign()`
now use an `Apple Development` identity if the keychain has one.

A grant made by an *earlier* build is stored with that build's requirement,
so a correctly signed build still gets *"Failed to match existing code
requirement"* in the `com.apple.TCC` log and prompts once more. Allowing it then
rewrites the grant against the certificate. MotuProbe was re-granted on
2026-09-12, and a byte-identical relaunch then went through without a prompt.
MotuDump and the PCI Audio Setup app may each prompt one more time. If one keeps
prompting after that, check the log:

```sh
log show --last 10m --style compact \
  --predicate 'subsystem == "com.apple.TCC" AND eventMessage CONTAINS "zenbox"'
```

If `security find-identity -v -p codesigning` reports **0 valid identities**
while an `Apple Development` cert is present, the WWDR intermediate has expired
(the 2013 one died 2023-02-07). Install the current G3:

```sh
curl -O https://www.apple.com/certificateauthority/AppleWWDRCAG3.cer
security import AppleWWDRCAG3.cer -k ~/Library/Keychains/login.keychain-db
```

## Open questions

- The `+ 2` in the console's MB/sec formula (confirmed in MOTU's code, meaning unknown).
- `GetCueMixResourceUsage`'s middle int (22 here).
- `GetPCIUsage` returns −1/−1 on this card; may be FireWire-only.
- `GetCueMixResourceUsage`'s middle int, and whether bus 0's `usage=22` is the same count.
