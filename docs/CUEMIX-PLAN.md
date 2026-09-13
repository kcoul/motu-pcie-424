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

## Method: learn the app on the M4, then map it to the PCI card

There are two separate things to learn, and they are best learned in different
places.

**1. What CueMix FX *is*: its parameter model and behaviour.** Study this at
home, on the M4 Mac mini with the UltraLite mk3 Hybrid. CueMix FX runs natively
there, and Claude Code can run alongside it: live, with signal, with a debugger
if needed.

The reason this generalises: CueMix FX keeps a hardware-independent parameter
tree and publishes it over OSC. The TouchOSC layout it ships (`Resources/
TouchOSC-iPad.layout_description`, also in the Mojave copy) addresses it as:

```
/bin/<mix>/<input>/{fader,pan,mute,solo,...}   per-mix strip controls
/in/<input>/in/{trm,st,namS,...}               input trim, stereo, name
/out/<output>/out/{fade,mute,ol,namS,...}      outputs and mix assignment
/tb/…/{tben,lben,tbal,lbal,tbin,lbin}          talkback / listenback
/dev/…/{mon,actv}   /meters (pre, post, rms)   device and metering
```

Many controls have a `…/str` sibling carrying the display string (e.g.
"-6.0 dB"), which gives value laws for free. Each hardware backend
(`CoreDeviceAW.cpp` for PCI, `CoreDeviceMacFWFX.cpp` for FireWire/USB) maps this
same tree onto its own driver.

**2. How that model reaches *this* card.** Only the PCI-424 can show this, so
it is learned on the studio machine: the CueMixAPI slot, bus, channel and value
behind each parameter. `MotuSpy` covers these gaps, not everything.

**What does *not* transfer from the UltraLite:**
- It uses MOTU's FireWire/USB driver, not the PCI HAL plugin, so no slot numbers
  or encodings carry over.
- Its mixer has DSP effects (EQ, dynamics, reverb) the PCI-424 lacks.
- Bus and channel counts, and the right-hand panel, differ.

Use it for behaviour and the parameter model; never for card-level facts.

### Stage 0 — at home, on the M4 + UltraLite mk3 Hybrid

1. **Inventory the binary there first.**
   - Version, architectures, and whether it is stripped.
   - Whether it still contains the PCI backend (`CoreDeviceAW` strings). The
     Mojave build is one binary for every MOTU interface.
   - If the backend is present *and* symbols survived, disassembling it could
     answer most of stage 1 directly, and MotuSpy shrinks further.
2. **An OSC logger.** Discover CueMix FX (Configure OSC Devices shows its host and
   port), dump the full address tree with current values, then log every message
   while each control is moved. Record the address, raw value, `/str` text and
   side effects (does solo in one mix touch others?).
3. **Meters over OSC** (`/meters`, pre/post/rms), with signal. This may give meter
   and clip behaviour without decoding `ReadLevelMeters` first.
4. **Behaviour that needs no card knowledge:**
   - what the MIX and OUTPUT popups change, and how mixes relate to outputs;
   - Talkback / Listenback flow;
   - Copy/Paste mix;
   - Configurations: save/export files and their XML format;
   - the analysis windows with a test tone.
5. **Write it up** as `docs/CUEMIX-MODEL.md`: the parameter tree, value laws and
   behaviours, with every UltraLite-only item marked.

### Then on the studio machine — MotuSpy for the PCI mapping

1. **`MotuSpy` for Mojave** (built: `src/motu-spy/spy.mm`). This is `MotuDump` rebuilt with a 10.14
   deployment target (the HAL plugin there is the same 1.6 73220, and ships
   x86_64). It snapshots every getter (device, channels, options, all 48
   buses × every strip, talkback, SMPTE) plus the driver's prefs blobs as hex,
   about 6,100 `key = value` lines, into `~/Desktop/MotuSpy/NNN-label.txt` with
   a `.diff` against the previous snapshot. Two back-to-back snapshots diff
   empty, so a diff is only what you changed.
2. **Snapshot, change one control in MOTU's CueMix FX, snapshot again, diff**,
   now working down the `CUEMIX-MODEL.md` parameter list instead of exploring
   blind. Each parameter becomes "slot N, bus B, channel C, value V".
3. **Worth adding to MotuSpy: an OSC listener** pointed at CueMix FX on the same
   Mojave boot. Then one snapshot records both sides of a change, the OSC
   parameter and the card call, and pairs them with no guesswork.
4. **Disassemble only where neither answers:** `ReadLevelMeters` layout, the
   Scope audio path.
5. **Decode the `CueMixSettings` blob** from the same diffs, since it is the
   saved form of that state.

Card-level findings go into `docs/CUEMIX-API.md`, the way `CHANNEL-STATE.md`
did for Setup.

## Stages

Each stage lists its decode work first, then what gets built, then how it is
checked.

### 1. Map the mixer model (no UI)
- **On the M4 (stage 0):** the parameter tree, value laws and behaviours, in
  `CUEMIX-MODEL.md`.
- **On the studio machine:** MotuSpy (and its OSC listener) maps each PCI-relevant
  parameter to CueMixAPI calls: what "Mix N" and OUTPUT mean for bus ids,
  volume/trim/pan encodings, solo across buses, balance/width, meters.
- **Check:** every console control has an OSC address, a slot with arguments,
  and an encoding.

### 2. A plain working mixer

**Roughed in (2026-09-12), read-only.** `src/cuemix-fx/` has these pieces:
- One strip per active input, rebuilt live as channels are enabled: 12 with a
  lone HD192, up to 96.
- A MIX popup of every output pair that CueMix accepts as a bus.
- The master fader and mute for the selected mix, plus the fader budget.
- Everything polls the card at 10 Hz, so changes made elsewhere show.
- Hovering a strip names it in the LCD.

Not written yet: any control. Fader dB (20·log10(v/32768)), pan (v−64) and trim
(v−64) displays are placeholders until stage 1 verifies the laws. There are no
meters yet.

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
- **Read side done:** the Classic panel shows the card's talkback and listenback
  sources (4095 = Disabled), TALK / LINK / LISTEN and the dim knobs. The dim
  range is unverified.
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
