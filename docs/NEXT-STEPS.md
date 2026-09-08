# Next steps

State as of the CMake/JUCE commit.

## Where we got to

- `src/common/motu_card.{h,mm}` wraps the whole HAL-plugin API and is verified
  working on Sequoia 15.7.4 with SIP enabled (`motu-dump`).
- `MOTU PCI Audio Setup` builds under CMake + JUCE 9, ad-hoc signed with the
  audio-input entitlement, launches, and opens a **602 × 362** window titled
  *MOTU PCI Audio Console* — the original's exact size.

## Verify first, next session

**Card access from inside JUCE is not yet visually confirmed.** The app builds
and runs, but the window was behind the terminal on display 2 and raising it
needs Accessibility permission, which needs a terminal restart. So the one thing
still to check is whether `MainComponent` shows *"Connected to PCI-424."* or an
error — i.e. whether `Card::installRunLoop()` from `JUCEApplication::initialise`
really does win the run-loop registration.

If it shows an error, the cause is almost certainly that something in JUCE's
startup touched CoreAudio first. `motu-dump` is the known-good control:

```sh
tools/build.sh MotuDump src/common/motu_card.mm src/common/motu_dump.mm --run
```

## Build

```sh
git submodule update --init --recursive
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
  cmake -B build/cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release
DEVELOPER_DIR=/Library/Developer/CommandLineTools cmake --build build/cmake -j8
open "build/cmake/src/pci-audio-setup/PCIAudioSetup_artefacts/Release/MOTU PCI Audio Setup.app"
```

`DEVELOPER_DIR` is required: Xcode 26.3 is installed but its licence is
unaccepted, so CMake must be pointed at the Command Line Tools toolchain.
Ninja is not installed; the Makefiles generator works.

## The Mojave capture trip

Static extraction is exhausted. We have every string, every skin asset and its
size, the menu structure and the main window size — but **not layout**, because
both originals position every control in code. `docs/ORIGINAL-UI.md` ends with
the shot list. Boot `/Volumes/Sierra` (mislabelled; it is Mojave 10.14.6) and
capture into `docs/reference/`:

1. `MOTU PCI Audio Console` main window, full size.
2. Each **Interface Options** pane — we have three interface types
   (HD192, 24I/O, 2408mk3).
3. `MOTU Channel Names` window.
4. CueMix FX console with all four interfaces attached: strip order, how 96
   inputs are paged, the bus selector, the PCI-variant right-hand panel.
5. CueMix FX menus and the Talkback/Listenback panel.

## Then

- Fill in PCI Audio Setup's real layout and the remaining controls (Default
  Input/Output, Enable Routing, Enable Volume Controls, PCI Use / AudioWire
  meters, per-interface options, Edit Channel Names).
- Start `src/cuemix-fx/` once the console layout is captured. It needs the
  Legacy/PCI skin assets listed in `docs/ORIGINAL-UI.md`; check the licence
  position before copying MOTU's PNGs into the repo, or redraw them.
- Wire up `CommitChanges` / `FlushPrefs` — nothing currently persists changes.
- Level meters (`CueMixAPIImpl::ReadLevelMeters`, slot 21) are mapped but not
  yet wrapped; the struct layout of `AudioWireLevelMeterRequest/Results` is
  still unknown.

## Open questions

- `GetCueMixResourceUsage` returns three ints; the middle one (22 here) is
  unidentified. `used` and `max` match ioreg's `CueMixFaders` / `MaxFaders`.
- `GetPCIUsage` returns -1/-1 on this card — may be FireWire-only.
- The exception `kind` field (0–5) maps onto MOTU's "HAL / kernel / MOTU / Unix
  / OS / unknown error" strings, but the exact ordering is unconfirmed.
