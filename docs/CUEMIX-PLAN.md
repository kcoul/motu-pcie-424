# CueMix FX — outline

CueMix FX is a much bigger app than PCI Audio Setup, so it is built in stages.
Each stage ends with something that runs against the card and is checked
against MOTU's own CueMix FX 1.6, which still runs on the Mojave volume.

The rule from PCI Audio Setup still applies: a feature is only "done" once it
changes the card the way MOTU's app does. Match the layout, and add a classic
(pixel-matched) skin next to a modern one once the features are done.

## What we are replacing

From `docs/reference/cuemix-*.png`, `LocalizableStrings.xml` and the binary
(i386, stripped; AwesomeLib drawing through Carbon, backend
`Source/Device/CoreDeviceAW.cpp` for PCI cards):

**Console window**, titled after the card:

- **Input strips**, one per active input, scrolling horizontally. Top to bottom:
  - input section: MUTE, TRIM knob with dB readout, MONO/STEREO, channel name well;
  - mix section: PAN knob with readout and BAL/WIDTH, fader with dB readout,
    meter, SOLO, MUTE.
- **Right panel (PCI variant):**
  - LCD: hovered control's name and description, plus the fader budget
    ("6 out of 180 faders in use / Sequencer using 0 faders / CueMix using 6 faders");
  - OUTPUT popup for the selected mix;
  - DSP meter and global SOLO;
  - Talkback / Listenback: two input popups, TALK / LINK / LISTEN, MONITOR DIM knobs;
  - MIX popup, which chooses which bus the strips show;
  - Scope Channel Selection (Left / Right);
  - the mix master fader with MUTE.

**Menus.** All captured; see `ORIGINAL-UI.md`.

- **File:** Peak Hold Time, Hardware Follows Console Stereo Settings, Close.
  The hardware-preset items are greyed out on PCI.
- **Edit:** Copy, Paste (a mix bus), Clear Peaks.
- **Devices:** the card, plus FFT Analysis, Oscilloscope, X-Y Plot, Phase Analysis, Tuner.
- **Configurations:** Create New, Save, Save To, Delete, Import, Export.
- **Talkback:** Configure Talkback/Listenback…, Toggle Talkback, Toggle Listenback.
- **Phones:** empty on PCI.
- **Control Surfaces:** Application Follows, Share Surfaces, CueMix Control
  Surfaces ▸ (Enabled, Configure…), Configure OSC Devices…

**Dialogs and windows:** the Talkback/Listenback sheet, Create/Save/Delete/Load
configuration, Mackie control-surface settings, OSC configuration, and the five
analysis windows.

**Dead weight for a PCI-424:** EQ, dynamics, reverb, phantom power, pad,
hardware presets, MicroBook registration. Those strings and assets belong to
the FireWire/USB FX boxes.

## What we have, and what we don't

| Area | State |
|---|---|
| `CueMix` API (32 slots): input mute/trim, per-bus solo/mute/volume/pan, bus mute/volume/solo, balance/width, resources | wrapped, **no semantics verified** |
| `Talkback` API (20 slots) | wrapped, unverified |
| `ReadLevelMeters` (slot 21) | **not wrapped**; request/result structs unknown |
| Bus numbering | **found (read-only bounds probe, Sequoia):** a bus is an *output pair*, numbered by its left output id: 48 buses, 0, 2, … 94. Odd numbers and ≥ 96 raise `CueMixAPIImpl.cpp:49`. Strip channels are card-wide input ids 0–95 (past that raises `:42`) |
| How a mix is assigned to an OUTPUT, and what "Mix N" in the MIX popup maps to | unknown; the bus *is* an output pair, so the popups may just pick which bus to view |
| Value encodings | observed defaults, dB law unknown: volume 32768 (≈ 0 dB; 24320 and 32256 also seen), pan 0–128 centre 64, trim 64, balance/width 64, talkback input 4095 = Disabled, talkback/listenback outputs 0–47 (one per bus) |
| **Unchecked getters** | `GetCueMixInputBalance` / `Width` / `BalanceWidthPref` / `InputChannelMapping` do no bounds check and **segfault** on a bad bus (seen at bus 150). Only call them with indexes a checked getter accepted |
| CueMix state persistence | the driver's `PCI-424.bus<N>.slot0.CueMix.plist` (`CueMixSettings`, 18816-byte blob) and `CueMixStereo.plist` (14208 bytes) |
| App preferences | `com.motu.CueMixFX.plist` (`PeakHoldTime`, `HWFollowsConsole`, scope and OSC state, control surfaces) |
| Configurations format | XML (AwesomeLib `XMLSerializable`); location and schema unknown |

## Method: diff MOTU's app against the card

PCI Audio Setup was cracked by disassembly because its symbols were intact.
CueMix FX is stripped, so the main tool is **differential observation**: do one
thing in MOTU's CueMix FX on Mojave and see what changed on the card.

1. **`MotuSpy` for Mojave** (built: `src/motu-spy/spy.mm`). This is `MotuDump` rebuilt with a 10.14
   deployment target (the HAL plugin there is the same 1.6 73220, and ships
   x86_64). It snapshots every getter (device, channels, options, all 48
   buses × every strip, talkback, SMPTE) plus the driver's prefs blobs as hex,
   about 6,100 `key = value` lines, into `~/Desktop/MotuSpy/NNN-label.txt` with
   a `.diff` against the previous snapshot. Two back-to-back snapshots diff
   empty, so a diff is only what you changed.
2. **Snapshot, change one control in MOTU's CueMix FX, snapshot again, diff.**
   That turns each UI control into "slot N, bus B, channel C, value V", and a
   sweep of a fader or knob gives the value law.
3. **Disassemble only where diffs can't answer**: `ReadLevelMeters` struct
   layout, the Scope audio path, and how configurations are written. The
   `CoreDeviceAW.cpp` assert strings anchor the PCI backend in the stripped
   binary.
4. **Decode the `CueMixSettings` blob** from the same diffs, since it is the
   saved form of that state.

Everything decoded goes into `docs/CUEMIX-API.md`, the way `CHANNEL-STATE.md`
did for Setup.

## Stages

Each stage lists its decode work first, then what gets built, then how it is
checked.

### 1. Map the mixer model (no UI)
- `MotuSpy` on Mojave; diff every control on one strip and on the right panel.
- Answer: bus numbering vs "Mix N"; what OUTPUT sets; volume/trim/pan ranges and
  dB law; what solo does across buses; balance/width and the stereo preference.
- **Check:** a written table in which every console control has a slot,
  arguments and an encoding.

### 2. A plain working mixer
- A JUCE window with one strip per active input, bound to the card: trim,
  input mute, pan, fader, solo and mute for the selected mix; MIX and OUTPUT
  popups; master fader and mute; the fader-budget readout.
- The HAL plugin keeps the card pointer run-loop-bound, so this is a separate
  app sharing `motu_card`. No `AudioDeviceManager`.
- **Check:** move a control in ours, confirm it with `MotuSpy`, then confirm by
  ear on a real mix.

### 3. Meters
- Decode and wrap `ReadLevelMeters`, then add strip meters, the master meter,
  Clear Peaks and Peak Hold Time.
- **Check:** feed a known level and compare against MOTU's meters on Mojave.

### 4. Talkback / Listenback
- Input popups, TALK / LINK / LISTEN, dim knobs, the Configure sheet (per
  output: which mix bus, Talk, Listen), and Toggle ⌘T / ⌘L.
- **Check:** diff each control on Mojave, then test with a mic.

### 5. Configurations and preferences
- Create / Save / Save To / Delete / Import / Export, in MOTU's XML format so
  files move both ways; Copy/Paste mix bus; Hardware Follows Console Stereo
  Settings; Mix 1 Return Includes Computer Output (greyed on PCI; confirm).
- **Check:** export from MOTU's app on Mojave, import into ours, and compare
  card state.

### 6. Analysis windows
- Scope Channel Selection, then FFT, Oscilloscope, X-Y, Phase, Tuner.
- These need audio. Keeping them out of the control app avoids the run-loop
  trap: either a helper process that opens the device, or capture started only
  after `installRunLoop` has won.
- **Check:** a test tone through each window against MOTU's.

### 7. Control surfaces
- OSC first (it has a documented protocol and a TouchOSC layout ships in
  `Resources/`), then Mackie Control; "Application Follows" / "Share".
- Lowest priority: it is only worth doing if actually used.

### 8. Skins
- Classic: MOTU's 145 PNGs are in the bundle, and the PCI/Legacy subset is
  listed in `ORIGINAL-UI.md`. Modern: our own.
- The same layout model underneath, so switching skins is just a look-and-feel swap.

## Open questions to carry into stage 1

- Is CueMix state shared between the two apps? For example, does Setup's
  channel enable change which strips CueMix shows (it should: strips = active
  inputs)?
- On PCI, are the mixes the 12 HD192/24I/O output pairs? The Talkback sheet
  lists `HD192:Analog/AES 1-2 → Mix 1`, `Analog-A 3-4 → Mix 2`, … one mix per
  output pair.
- Does the 180-fader budget make some faders refuse
  (`DoesCueMixFaderHaveResources`)?
- Who writes the CueMix prefs blob, and when: the driver on commit, or the app?
