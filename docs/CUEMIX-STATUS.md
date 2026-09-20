# CueMix FX rebuild — status at back-burner, 2026-09-19

Written when the project was parked, so that picking it up later does not start
with an archaeology session. `CUEMIX-PLAN.md` has the staged plan; this says
where each stage actually stopped.

**Short version:** stages 1–3 are done and the console is genuinely usable
read-only, with both skins finished. Stage 2's write path is *written but has
never touched a card*. Stages 4–7 are barely begun. Feature parity with MOTU is
roughly **half**, and the remaining half is mostly self-contained windows rather
than anything structural.

**The rebuild is no longer urgent.** MOTU's own CueMix FX drives the PCI-424
again after a one-line re-sign (`ORIGINAL-UI.md`), so this is now about
modernizing and about not depending on a patched MOTU binary — not about having
a working mixer at all. See `motu-sequoia-is-the-last-os` in the project memory.

## Size

```
src/cuemix-fx/   2350 lines
  ConsoleModel.cpp/.h   690   card state, polling, local overrides, write path
  ClassicConsole.cpp/.h 593   MOTU's layout, pixel-matched
  Main.cpp              355   window, menu bar, skin switching
  Console.cpp/.h        188   shared console scaffolding
  Strip.cpp/.h          171   one channel strip
  ClassicSkin.cpp/.h    132   sprite loading from assets/classic
  Meter.cpp/.h          128   bar, held peak, clip/signal LED
```

## Stage by stage

### 1. Map the mixer model — **done**

Every CueMix control has a slot, argument order, bounds and value law in
`CUEMIX-API.md`, recovered from MOTU's own 2025 binary rather than by probing. A
mix is an index 0–47 and its bus id is `2 × mix`.

### 2. A plain working mixer — **done read-side; writes untested**

Working:
- one strip per active input, rebuilt live as channels are enabled (12 with a
  lone HD192, up to 96; 68 on the studio rig);
- MIX popup of every output pair, master fader and mute, fader budget;
- everything polls the card at 10 Hz, so changes made elsewhere appear;
- **every control moves**, as a local override the LCD marks "not sent", with
  *Revert to Card Values* to drop them;
- rename a channel by double-clicking it in Classic — new; MOTU never had this.

Value laws are correct as of today: fader is 40·log10(v/32768) quantized to
multiples of 256, pan is v−64, trim is 64…255 → 0…+12 dB.

**The write path is the big caveat.** `ConsoleModel::writeToCard` implements
eight parameters — Trim, InputMute, Volume, Pan, Mute, Solo, MasterVolume,
MasterMute — but it is **off by default** behind *Send Changes to Card*, and
**nothing in it has ever been sent to a card**. That is the single largest piece
of unverified work in the project.

Not written at all: **Stereo**, deliberately — it is the one parameter needing
`CommitChanges` plus MOTU's pair-mirroring. Also BalWidth, the talkback
parameters and the scope selectors, which belong to later stages.

### 3. Meters — **done, and validated against hardware**

Bar, held peak with MOTU's hold table, and the clip/signal LED, drawn from
MOTU's own artwork and ballistics. *Clear Peaks* and *Peak Hold Time* work.

Confirmed on the card 2026-09-19 (`CUEMIX-API.md`):
- `cardType = 2` → **48 meters**, which is *fewer than the rig's 68 active
  inputs*, so a full system cannot meter everything at once — the console will
  eventually have to choose which 48;
- the results tail carries **two stereo bus meters**, so the MIX section gets a
  real bus meter free with the same read — not yet drawn;
- **meters read all-zero unless the audio engine is streaming.** Easiest way in
  the project to misdiagnose a working meter path as broken.

Unverified: the decay rate (`kPeakDecayPerTick` in `ConsoleModel.cpp` — MOTU's
0.015 is per *update* and its update rate is not in the binary), and which clip
value means "clipped" versus "signal present". Both are now cheap to settle
against MOTU's own console.

### 4. Talkback / Listenback — **read-only**

The Classic panel shows the card's talkback and listenback sources (4095 =
Disabled), TALK / LINK / LISTEN and the dim knobs.

Missing: every write, the *Configure Talkback/Listenback…* sheet, and ⌘T / ⌘L —
all three menu items are stubs. The dim-knob range (0–255) is still a guess.

### 5. Configurations and preferences — **not started**

*Create New…*, *Import…* and *Copy* are stubs. Save / Save To / Delete / Export
and *Paste* are not even enabled. Needs MOTU's XML format so files move both
ways. *Hardware Follows Console Stereo Settings* exists as a menu item.

### 6. Analysis windows — **not started**

FFT, Oscilloscope, X-Y Plot, Phase, Tuner: all five are stubs. These need
audio, so they need either a helper process or capture started only after
`installRunLoop` has won — the run-loop trap is the real design constraint here,
not the DSP.

### 7. Control surfaces — **not started**

OSC and Mackie, *Application Follows*, *Share*, *Configure…*: all stubs. Lowest
priority, and only worth doing if actually used.

**Newly possible:** `…_OSCClients` now appears in MOTU's prefs for the PCI
engine, so `tools/osc-log.py` can watch MOTU's own OSC traffic against this
card. That was impossible before the re-sign, since CueMix FX could only serve
an UltraLite.

### 8. Skins — **done**

Classic is pixel-matched using MOTU's own sprites; Modern has the same layout.
Switch in the application menu. The layout model is shared, so a skin is a
look-and-feel swap.

`ClassicSkin.cpp` loads sprites from a **directory at runtime**, which matters
if this ever goes public: it can read from the user's own installed CueMix FX
instead of shipping MOTU's artwork.

## Menu bar parity

The menu bar matches MOTU's exactly, and unbuilt items say in the LCD which
stage delivers them (`Main.cpp:338`).

**Implemented:** Save/Load Hardware Preset, Mix 1 Return, Hardware Follows
Console Stereo Settings, Close, Undo/Redo, Paste, Clear Peaks, device selection,
Save/Save To/Delete/Export configuration, Minimize, Zoom, Bring All to Front.

**Stubbed, by stage:** Copy (5), the five analysis windows (6), Create New… and
Import… (5), Configure Talkback + Toggle Talkback/Listenback (4), the five
control-surface items (7).

## Known-wrong or unverified

- **Nothing has ever been written to the card.** Start there, with a mute —
  audible, unambiguous, trivially reversible — then fader, pan, solo.
- Meter decay rate and clip-LED polarity.
- Dim-knob range (0–255), a guess.
- The MIX bus meter now available in the results tail is not drawn.
- 48 meters versus 68 inputs is unhandled.
- Scope audio is a placeholder.
- The Modern skin can open scrolled a few strips in.

## If picking this up again

1. **Verify trim against MOTU's console** — the law is derived but never
   watched. Ten minutes, and it is also the studio's actual gain-staging need.
2. **First writes**, far safer now that MOTU's console gives independent
   read-back of anything changed.
3. **Meter decay and clip polarity**, side by side.
4. Then stage 4 or 5, whichever is more useful in practice.
