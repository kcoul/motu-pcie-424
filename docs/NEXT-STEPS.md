# Next steps

State after the 2026-09-12 PCI Audio Setup session.

## Settled

- **PCI Audio Setup is functionally complete** and survived a stress test.
  Every write follows the call sequence recovered by disassembling MOTU's own
  console (symbols intact): `docs/CHANNEL-STATE.md`.
  - Channel checkboxes, Bank personalities, Enable Routing (and its "Disable"
    bank item), Enable Volume Controls (`'Mvol'`), Default In/Out.
  - All three Options panes, with MOTU's value lists.
  - Save/Load Configuration (`.mcfg`), Refresh (re-probe), live refresh.
- **Corrections to earlier sessions:**
  - `GetInputState`'s bytes are `exists, enabled` (not the other way round).
  - Output enable is `source == -1`.
  - The HD192 Clip and Peak/Hold keys are crossed.
  - Interface options live in the per-OS prefs.
- **Hazards found:**
  - A commit can make the driver rebuild its Interface objects, so never cache
    one.
  - The CueMix balance/width/mapping getters do no bounds checking and
    segfault past the end.
- **CueMix buses are output pairs** (48, numbered 0, 2, … 94).
- **MotuSpy** is on the Mojave Desktop for differential reverse-engineering.
- **CueMix FX console** (`src/cuemix-fx/`) follows the card live and is read-only.
  - Classic skin: pixel-matched to MOTU's, from MOTU's own sprites in
    `assets/classic/`. Modern skin: same layout. Switch with View.
  - Real data: strips = active inputs; MIX = output-pair buses; master fader and
    mute; fader budget.
  - Placeholders: MONO/STEREO, talkback section, Scope popups, meters, and the
    trim/pan scales.

## Next

1. **CueMix stage 1 on Mojave with MotuSpy** (`docs/CUEMIX-PLAN.md`). Pass
   signal through channels so meters and clip LEDs show up too; that needs
   `ReadLevelMeters` decoded, which snapshots can't do alone.
2. **Done 2026-09-12 (standalone items):**
   - **Edit Channel Names** in PCI Audio Setup. `SetCustomChannelNameCFString`
     takes effect immediately (CoreAudio's category name changes), and an empty
     name restores the hardware name. *Import Names…* copies another volume's
     names by channel id; the Mojave file decodes to 60 named inputs.
   - **CueMix menu bar** identical to MOTU's. Unbuilt items say which stage
     delivers them in the LCD; the skin choice sits in the application menu.
   - **Talkback read-only** in both skins: sources, TALK/LINK/LISTEN, dim knobs.
     The dim range is assumed 0-255.
3. **Verify the channel-name import end to end** by importing the Mojave names,
   then check they persist in `~/Library/Preferences/com.motu.PCIAudio/`.
4. **Put PCI Audio Setup into daily use**, then install it into `/Applications`
   once trusted.
5. Modern skin polish once features are done (Classic is the reference).
6. Tidy `ORIGINAL-UI.md` against the corrections above.

## Still to capture from Mojave

- CueMix FX behaviour, via MotuSpy (stage 1).

## Build

```sh
git submodule update --init --recursive
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
  cmake -B build/cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release
DEVELOPER_DIR=/Library/Developer/CommandLineTools cmake --build build/cmake -j8
open "build/cmake/src/pci-audio-setup/PCIAudioSetup_artefacts/Release/MOTU PCI Audio Setup.app"
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
