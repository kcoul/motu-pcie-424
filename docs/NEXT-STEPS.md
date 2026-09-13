# Next steps

State after the screenshot / channel-state session.

## Settled this session

- **Card access from inside JUCE works.** `Card::installRunLoop()` from
  `JUCEApplication::initialise` does win the run-loop registration; the app
  reports *Connected to PCI-424* and renders all four interfaces, their banks
  and personalities, and the CueMix fader budget. This was last session's one
  open question.
- **The screenshots are MOTU's originals**, not a reimplementation: the Mojave
  copy is i386 / 10.6 SDK / `LSRequiresCarbon`. They are in `docs/reference/`.
- **The window is titled "PCI-424"**, after the card — not "MOTU PCI Audio
  Console", which is the menu-bar application name. Fixed in `Main.cpp`.
- **602 × 334 confirmed** by measuring the screenshot (602 px frame, 356 px less
  Mojave's 22 px title bar).
- **Layout is no longer a blocker** for PCI Audio Setup. See `ORIGINAL-UI.md`.
- `GetInputState`, `GetOutputState` and `OtherInterfaceOp` are wrapped and
  decoded against MOTU's own console — see `docs/CHANNEL-STATE.md`.
- Builds now sign with a real certificate when one is in the keychain, so the
  microphone grant survives rebuilds instead of re-prompting every launch.

## Next

1. **Lay out the main window for real**, from `docs/reference/setup-main-*.png`.
   Everything it needs is now wrapped. The grid reflows per interface: 6 pairs
   in 2 columns for the HD192, 12 in 3 for a 24I/O, 4 under each of the
   2408mk3's three bank popups.
2. **Interface Options panes.** `getOption`/`setOption` already say which
   controls each interface should show. The 2408mk3 pane is captured; the HD192
   and 24I/O panes still need a Mojave screenshot.
3. **Edit Channel Names.** Custom names live in the per-OS prefs plist, not on
   the card (`docs/CHANNEL-STATE.md`), so this window owns that storage. An
   importer for the Mojave names would be worth having.
4. **Wire up `CommitChanges` / `FlushPrefs`** — nothing persists yet. Resolve
   the enabled-vs-active question in `CHANNEL-STATE.md` first.
5. **Start `src/cuemix-fx/`.** `cuemix-console-full.png` gives the strip order,
   the LCD, the right-hand PCI panel, the mix selector and the Talkback/Listen
   cluster. Still needed: its menus and the Talkback panel.
6. Level meters (`ReadLevelMeters`, CueMix slot 21) are still unwrapped; the
   `AudioWireLevelMeterRequest/Results` layout is unknown.

## Still to capture from Mojave

- HD192 Options pane, 24I/O Options pane.
- CueMix FX menus and the Talkback/Listenback panel.

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

If `security find-identity -v -p codesigning` reports **0 valid identities**
while an `Apple Development` cert is present, the WWDR intermediate has expired
(the 2013 one died 2023-02-07). Install the current G3:

```sh
curl -O https://www.apple.com/certificateauthority/AppleWWDRCAG3.cer
security import AppleWWDRCAG3.cer -k ~/Library/Keychains/login.keychain-db
```

## Open questions

- `enabled` 84 vs `active` 36 — see `docs/CHANNEL-STATE.md`.
- The `+ 2` in the console's MB/sec formula.
- `GetCueMixResourceUsage`'s middle int (22 here).
- `GetPCIUsage` returns −1/−1 on this card; may be FireWire-only.
- `InputLevels` bit order (only the all-zero case has been seen).
