# CueMix on the PCI-424 — slots, encodings, and value laws

Card-level findings for CueMix FX, the way `CHANNEL-STATE.md` did for PCI Audio
Setup.

**Provenance, and how much to trust it.** Everything here was read out of
MOTU's own current CueMix FX binary by disassembly, not from the card:

```
/Applications/CueMix FX.app     1.6 b5003c51d   arm64 + x86_64, NOT stripped
                                26,861 symbols, 8,002 in __TEXT
                                Consoles/CueMix3/Source/Device/CoreDeviceAW.cpp present
/Volumes/macOS Work/…           1.6 88494       a second unstripped universal build
```

MOTU kept building CueMix FX for legacy interfaces on a `Legacy (Mac Dev)`
Jenkins line; the resources in the bundle are dated September 2025. Both builds
carry the complete PCI/AudioWire back end, so the mapping from console control
to `CueMixAPI` slot is recoverable statically. Source line numbers below are
MOTU's own, from the `ThrowerClass` file/line pairs compiled into every wrapper.

This is strong evidence — it is MOTU's shipping code, not inference — but it is
**not the same class of fact as `CHANNEL-STATE.md`**, where the screenshots were
the oracle. Nothing here has yet been confirmed against a real PCI-424. Items
that still need the card are listed at the end.

## Where this sits relative to CoreAudio

Worth being explicit, because the two apps differ:

```
MOTU PCI Audio Setup        sample rate, clock source, Default In/Out
                              -> plain CoreAudio properties ('nsrt','csrc','dch2')
                            channels, banks, options, names
                              -> the card object

CueMix FX                   everything
                              -> the card object, only
```

CoreAudio's role for CueMix is limited to handing over the card pointer:
`'rnlp'` to register the run loop, then `'Mapi'` to get `AudioWireCard*` (see
`HALPLUGIN-API.md`). After that, no CueMix control is a CoreAudio property —
every one is a virtual call on `CueMixAPI`, `TalkbackAPI` or the card itself.

MOTU's console reaches those through one adapter class, `CoreDeviceAW`, which is
the PCI implementation of a hardware-independent `CoreDevice` interface shared
with the FireWire/USB consoles. Our app does not need `CoreDeviceAW` — we call
the same slots directly — but we do need to reproduce what it *does* on the way
through: index doubling, value quantization, stereo mirroring, and commit.

`CoreDeviceAW` instance layout, for reading the disassembly:

| offset | holds |
|---|---|
| `+0x1410` | `Device*` — the console's own model (channel counts, mix-bus count, the `Value` tree) |
| `+0x1420` | `uint32` config-changed bitmask; **bit 4 gates every card write** |
| `+0x1424` | `uint32` currently selected mix index |
| `+0x1428` | `uint8` "stereo settings changed", consumed by `CommitChanges` |
| `+0x1438` | `uint8` transaction flag, set by `BeginTransaction` |
| `+0x1440` | `CueMixAPI*` |

## A mix is a bus index, and the bus id is twice it

The single most useful finding, and it answers the open question about what
"Mix N" in the MIX popup means.

Every `CoreDeviceAW` wrapper takes a **mix index 0–47** and shifts it left by
one before calling the HAL (`lsl w2, w2, #1`). The HAL's bus argument is
therefore always **even**, which is exactly what the read-only bounds probe
found from the other direction: 48 buses at output ids 0, 2, … 94.

```
bus id passed to CueMixAPI  =  mix index * 2          mix index in 0..47
strip channel               =  card-wide input id     channel   in 0..95
```

So `Mix 1` in MOTU's popup is mix index 0 is bus 0 is output pair 1–2. Carry the
mix index 0–47 in the model and double it only at the card boundary, which is
what MOTU does.

## The call table

Every wrapper has the same shape. For the setters:

```c
void CoreDeviceAW::AWSetCueMixVolume(long channel, long mix, long value)
{
    if (this->flags_0x1420 & 0x10) return;        // write gate, see below
    MOTUException exc = {0};                      // 144 bytes, zeroed
    this->cuemix->SetCueMixVolume(&exc, mix * 2, channel, value);
    if (exc.code) throw ThrowerClass{"CoreDeviceAW.cpp", 2396};
}
```

and for the getters the same, plus a bounds check that returns a default
instead of calling.

**Argument order is `(channel, mix)` in MOTU's wrapper and `(bus, channel)` at
the HAL.** The wrapper swaps them. Getting this backwards is silent — both are
small non-negative ints.

### Per-mix, per-channel

| `CoreDeviceAW` | slot | `CueMixAPIImpl` call | bounds | out-of-range | cpp |
|---|---|---|---|---|---|
| `AWGetCueMixSolo(ch, mix)` | 6 | `GetCueMixSolo(exc, mix*2, ch)` | ch ≤ 95, mix ≤ 47 | `0` | 2546 |
| `AWGetCueMixMute(ch, mix)` | 7 | `GetCueMixMute(exc, mix*2, ch)` | ch ≤ 95, mix ≤ 47 | **`1`** | 2562 |
| `AWGetCueMixVolume(ch, mix)` | 8 | `GetCueMixVolume(exc, mix*2, ch)` | ch ≤ 95, mix ≤ 47 | `0` | 2578 |
| `AWGetCueMixPan(ch, mix)` | 9 | `GetCueMixPan(exc, mix*2, ch)` | ch ≤ 95, mix ≤ 47 | `64` | 2594 |
| `AWGetCueMixBalance(ch, mix)` | 28 | `GetCueMixInputBalance(exc, mix*2, ch)` | ch ≤ 95, mix ≤ 47 | `64` | 2609 |
| `AWGetCueMixWidth(ch, mix)` | 29 | `GetCueMixInputWidth(exc, mix*2, ch)` | ch ≤ 95, mix ≤ 47 | `64` | 2624 |
| `AWGetCueMixBalanceWidthPref(ch, mix)` | 30 | `GetCueMixInputBalanceWidthPref(exc, mix*2, ch)` | ch ≤ 95, mix ≤ 47 | `0` | 2639 |
| `AWDoesCueMixFaderHaveResources(ch, mix)` | 10 | `DoesCueMixFaderHaveResources(exc, mix*2, ch)` | ch ≤ 95, mix ≤ 47 | `0` | 691 |
| `AWSetCueMixSolo(ch, mix, on)` | 15 | `SetCueMixSolo(exc, mix*2, ch, on)` | **none** | — | 2372 |
| `AWSetCueMixMute(ch, mix, on)` | 16 | `SetCueMixMute(exc, mix*2, ch, on)` | **none** | — | 2384 |
| `AWSetCueMixVolume(ch, mix, v)` | 17 | `SetCueMixVolume(exc, mix*2, ch, v)` | **none** | — | 2396 |
| `AWSetCueMixPan(ch, mix, v)` | 18 | `SetCueMixPan(exc, mix*2, ch, v)` | **none** | — | 2408 |
| `AWSetCueMixBalance(ch, mix, v)` | 24 | `SetCueMixInputBalance(exc, mix*2, ch, v)` | **none** | — | — |
| `AWSetCueMixWidth(ch, mix, v)` | 25 | `SetCueMixInputWidth(exc, mix*2, ch, v)` | **none** | — | — |
| `AWSetCueMixBalanceWidthPref(ch, mix, b)` | 26 | `SetCueMixInputBalanceWidthPref(exc, mix*2, ch, b)` | **none** | — | — |

### Input-scoped — one index, no doubling

| `CoreDeviceAW` | slot | `CueMixAPIImpl` call | bounds | out-of-range | cpp |
|---|---|---|---|---|---|
| `AWGetInputMute(ch)` | 2 | `GetInputMute(exc, ch)` | ch ≤ 95 | `0` | 2516 |
| `AWGetInputTrim(ch)` | 4 | `GetInputTrim(exc, ch)` | ch ≤ 95 | `0` | 2530 |
| `AWGetCueMixInputMapping(ch)` | 31 | `GetCueMixInputChannelMapping(exc, ch)` | ch ≤ 95 | `0` | 2653 |
| `AWSetCueMixInputMute(ch, on)` | 3 | `SetInputMute(exc, ch, on)` | **none** | — | 2348 |
| `AWSetCueMixInputTrim(ch, v)` | 5 | `SetInputTrim(exc, ch, v)` | **none** | — | 2360 |
| `AWSetCueMixInputMapping(ch, m)` | 27 | `SetCueMixInputChannelMapping(exc, ch, m)` | ch ≤ 95 | — | 2465 |

### Bus-scoped — one index, doubled

| `CoreDeviceAW` | slot | `CueMixAPIImpl` call | bounds | out-of-range | cpp |
|---|---|---|---|---|---|
| `AWGetCueMixBusMute(mix)` | 11 | `GetCueMixBusMute(exc, mix*2)` | mix ≤ 47 | `0` | 2667 |
| `AWGetCueMixBusVolume(mix)` | 12 | `GetCueMixBusVolume(exc, mix*2)` | mix ≤ 47 | `0` | 2681 |
| `AWGetCueMixBusResourceUsage(id)` | 14 | `GetCueMixBusResourceUsage(exc, id)` | **id ≤ 95** | `0` | 675 |
| `AWSetCueMixBusMute(mix, on)` | 19 | `SetCueMixBusMute(exc, mix*2, on)` | **none** | — | 2477 |
| `AWSetCueMixBusVolume(mix, v)` | 20 | `SetCueMixBusVolume(exc, mix*2, v)` | **none** | — | 2489 |

`AWGetCueMixResourceUsage(long& used, long& b, long& max)` is slot 22, cpp:709,
and passes all three through unchanged.

**`AWGetCueMixBusResourceUsage` is the one exception to the doubling rule.** It
does *not* shift, and its bound is `id ≤ 95` rather than `mix ≤ 47` — so it takes
a **bus id already**, not a mix index, and will accept odd ones. Every other
bus-scoped wrapper on this class takes a mix index and doubles it. Pass
`mix * 2` here yourself; passing a mix index would silently read the wrong bus.
This is also the likely explanation for `bus 0`'s `usage = 22` recorded in
`CUEMIX-PLAN.md` being hard to line up with the other counts.

### Why the segfaulting getters never bite MOTU

`bounds.mm` found that `GetCueMixInputBalance` / `Width` / `BalanceWidthPref` /
`InputChannelMapping` do no bounds check in the HAL and segfault on a bad index.
The table above is the reason MOTU never hits it: **every one of those is reached
through a wrapper that bound-checks first.** The HAL is unguarded; MOTU's app
guards it. Ours must too — the guard belongs on our side of the call, not in a
comment.

Note also that the **setters do no bounds checking at all**, in MOTU's code or
the HAL's. Validate before writing.

## The write gate, transactions, and commit

Three separate mechanisms, easy to confuse.

**1. `flags & 0x10` — the write gate.** Every single `AWSet*` begins
`if (this->flags_0x1420 & 0x10) return;`, silently. So does
`AWReadLevelMeters`. The bit lives in a `uint32` change-mask at `+0x1420`:

```c
void SetConfigChanged(unsigned m) { flags |= m; }
void ClearConfigChanged()         { flags &= 0x10; }   // clears all BUT bit 4
unsigned GetConfigChanged()       { if (flags & ~0x90) UpdateCachedValues(); return flags; }
void ConfigChanged()              { SetConfigChanged(4); }
```

`ClearConfigChanged` preserving only bit 4 makes it sticky: it is a latched
state, not a change notification, and once set the console runs with a purely
local model and writes nothing. Where it gets set has not been traced yet — see
the open items.

**2. `BeginTransaction` / `EndTransaction` — stereo mirroring and display.**

```c
void BeginTransaction(bool)  { this->inTransaction_0x1438 = 1; }   // the bool is ignored
void EndTransaction()        { this->inTransaction_0x1438 = 0;
                               for (mix : 0..GetNumMixBusses())
                                 for (ch : 0..GetNumInputChannels()) {
                                     UpdateDisplayedPan(ch, mix);
                                     UpdateDisplayedFader(ch, mix);
                                 } }
```

It does **not** batch card writes — those go out immediately. What it suppresses
is the stereo-partner mirroring described below, so a caller that is already
iterating over both channels of a pair does not write each twice. `EndTransaction`
then refreshes every displayed pan and fader value from the card.

**3. `CommitChanges(bool)` — stereo pairing only.** On this back end it is
gated on `flags & 0x10` like the setters, then checks
`this->stereoDirty_0x1428 == 1` and, if so, walks channels in steps of 2 calling
`ChannelStripMap::CreateStereoPair` and a `Device` slot per pair. That flag is
set only by the stereoness parameter. So on the CueMix path `CommitChanges` is
**"Hardware Follows Console Stereo Settings"**, and is *not* how fader, pan,
solo or mute reach the card. Those need no commit at all — unlike PCI Audio
Setup, where `CommitChanges(true)` follows every change.

### Stereo mirroring

When a control is written for a channel whose stereoness parameter reads `1.0`,
the same value is written to the pair partner as well, unless a transaction is
open:

```c
partner = (channel & 1) ? channel - 1 : channel + 1;
if (isStereo && !inTransaction) { ...write partner too... }
```

MOTU derives the parity from the parameter id rather than the channel variable
(`tst w, #0x100` / `#0x10000` — the low bit of the channel field), but it is the
same thing.

## Parameter ids

MOTU's console addresses every control by a packed 32-bit id, dispatched in
`SetValueByParamID(unsigned id, float value, unsigned place)`. This is the tree
the OSC interface publishes, so it is also the bridge between stage 0's OSC
capture and the calls above.

```
 31        24 23        16 15         8 7          0
+------------+------------+------------+------------+
|  section   |     A      |     B      |  selector  |
+------------+------------+------------+------------+
```

`section = id >> 24` picks the scope, and the scope decides what `A` and `B`
mean — they are **not** in the same place for input and per-mix controls:

| section | dispatches to | A (bits 16–23) | B (bits 8–15) |
|---|---|---|---|
| 1 | `SetInputValueByGlobalID` | channel, `< GetNumInputChannels()` | must be `0` |
| 2 | `SetBusInputValueByGlobalID` | mix, `< GetNumMixBusses()` | channel, `< GetNumInputChannels()` |
| 3 | inline | mix, `< GetNumMixBusses()` | — |
| 4 | inline | output | must be `0x0D` |

Selectors, per section:

| section | sel | control | reaches |
|---|---|---|---|
| 1 | 1 | input MUTE | `AWSetCueMixInputMute(ch, v == 1.0f)` |
| 1 | 2 | input TRIM | `AWSetCueMixInputTrim(ch, (int)v)` |
| 1 | 7 | MONO/STEREO | sets `stereoDirty`, then `ApplyStereoness(ch, v)` |
| 2 | 1 | strip SOLO | `AWSetCueMixSolo(ch, mix, v == 1.0f)` |
| 2 | 2 | strip MUTE | `AWSetCueMixMute(ch, mix, v == 1.0f)` |
| 2 | 3 | PAN | `AWSetCueMixPan(ch, mix, (int)v)` |
| 2 | 4 | fader | `AWSetCueMixVolume(ch, mix, (int)v)` |
| 2 | 5 | BALANCE | `AWSetCueMixBalance(ch, mix, (int)v)` |
| 2 | 6 | WIDTH | `AWSetCueMixWidth(ch, mix, (int)v)` |
| 2 | 7 | BAL/WIDTH pref | `AWSetCueMixBalanceWidthPref(ch, mix, v == 1.0f)` |
| 2 | 8 | pan, display units | `ApplyDisplayedPan(ch, mix, v)` |
| 2 | 9 | fader, display units | `ApplyDisplayedFader(ch, mix, v)` |
| 3 | 2 | mix master MUTE | `AWSetCueMixBusMute(mix, v == 1.0f)` |
| 3 | 3 | mix master fader | `AWSetCueMixBusVolume(mix, (int)v)` |
| 4 | 2, 3 | talkback / listenback output | `SetIsTalkbackOutput` / `SetIsListenbackOutput` |

Booleans are `value == 1.0f` exactly, and integers are `fcvtzs` — truncation
toward zero, not rounding.

Two ids matter for metering and are read, not written:

| id | via | meaning |
|---|---|---|
| selector 10, per (mix, channel) | `Device` slot 24 | meter level |
| selector 29, per channel | `Device` slot 15 | clip indicator |

## Value laws

These are the laws the repo had marked "unknown" or "placeholder". Each is a
whole small function in the binary, so they are exact rather than fitted.

### Fader — and it is quantized

`ValueLegacyFader`, raw card units ↔ UI 0…1:

```c
float ConvertToUIControlValue(float raw)    // card -> UI
{   raw = clamp(raw, 0.0f, 32768.0f);
    return clamp(raw / 32768.0f, 0.0f, 1.0f); }

float ConvertFromUIControlValue(float ui)   // UI -> card
{   unsigned v = (unsigned)(ui * 32768.0);
    if (v > 32768) v = 32768;
    return (float)(v & 0xFF00);             // <-- quantized to multiples of 256
}
```

**Fader values the console writes are always multiples of 256**, capped at
`0x8000`. That is 129 distinct positions: `0x0000, 0x0100, … 0x7F00, 0x8000`.
It also explains the defaults already recorded in `CUEMIX-PLAN.md` — 32768 is
`0x8000`, 24320 is `0x5F00`, 32256 is `0x7E00`. All three land on the grid.

### Fader dB readout — 40·log10, not 20

`DigiVolToDecibelString(int raw, char* out, int len)`, whose only caller is
`ValueLegacyFader::GetUIStringWithHardwareValue`:

```c
double dB = 40.0 * log10(raw / 32768.0);        // scvtf d0, w0, #0xf  then  *40.0
if (dB <= -90.0)          strcpy(out, "-inf");
else if (fabs(dB) < 0.1)  strcpy(out, "0.0");
else if (fabs(dB) < 40.0) sprintf(out, "%+.1f", dB);
else                      sprintf(out, "%+.0f", dB);
```

The caller appends `" dB"`. The `len` argument is ignored — MOTU passes 128 and
never bounds the write.

**The coefficient is 40, so `CUEMIX-PLAN.md`'s `20·log10(v/32768)` is wrong by a
factor of two.** On the quantized grid the fader spans:

| raw | dB | shown |
|---|---|---|
| `0x8000` | 0.0 | `0.0 dB` |
| `0x7F00` | −0.14 | `-0.1 dB` |
| `0x4000` | −12.04 | `-12.0 dB` |
| `0x1000` | −36.12 | `-36.1 dB` |
| `0x0100` | −84.29 | `-84 dB` |
| `0x0000` | −∞ | `-inf dB` |

A 0…32768 linear fader with a 40·log10 readout gives an 84 dB range and a
usable taper; 20·log10 would have given 42 dB. Any value at or below raw 184
prints `-inf`, though on the grid only 0 can.

### Pan

`ValuePanLegacy`: `ui = raw / 128`, `raw = (unsigned)(ui * 128)` — so raw 0…128,
centre 64, and no quantization. The readout is the offset from centre with an
explicit `+`:

```c
int n = (int)raw - 64;                      // -64 .. +64
out = (n > 0 ? "+" : "") + NumToUString(n); // "+21", "0", "-64"
```

`CUEMIX-PLAN.md`'s guess of `v − 64` for pan was right. There is one special
case: the string is empty when the value's key is `0x20000` and a flag byte at
`+0xfc` is clear.

### Trim — the range is per channel, and not centred on 64

`ValueLegacyTrim` does **not** use a fixed law. It reads a min and a max float
stored on the value object itself (`+0x24`, `+0x28`):

```c
float ConvertToUIControlValue(float hw)   { return (hw - min) / (max - min); }
float ConvertFromUIControlValue(float ui) { return floorf(min + ui * (max - min)); }
```

and the readout is the hardware value itself, floored, with a `+` when positive
— *not* offset by 64:

```c
int n = (int)floorf(hw);
out = (n > 0 ? "+" : "") + NumToUString(n);
```

So `CUEMIX-PLAN.md`'s `trim (v−64)` placeholder is the wrong shape entirely.

### Solved 2026-09-19: on a PCI-424 the range is 64…255, shown as 0…+12 dB

`Device424::CreateTrimValue(unsigned int)` (`0x100032c30`) builds the trim Value
and seeds it with a **single 16-byte constant, identical for every channel** —
so despite `ValueLegacyTrim` supporting a per-channel range, Device424 never
varies it:

```
100032c94  adrp x9, 0x10021d000
100032c98  ldr  q0, [x9, #0x330]      ; 00 00 00 00  00 00 00 00  00 00 80 42  00 00 7f 43
100032c9c  stur q0, [x19, #0x1c]      ; -> +0x24 = 64.0 (min), +0x28 = 255.0 (max)
```

The display is not `GetUIStringWithHardwareValue`; a `StringDisplayScalerPrintf`
is transferred onto the Value instead, constructed with `(64.0, 255.0, 0.0,
12.0)` and the format `"%0.0f dB"`. `StringDisplayScalerPrintf::ConvertToString`
(`0x100179168`) computes, with `a,b,c,d` the four floats:

```c
display = c + (hw - a) * (d - c) / (b - a);
```

so for this card:

```c
dB = (hw - 64) * 12.0f / 191.0f;     // hw 64 -> 0 dB, hw 255 -> +12 dB
hw = 64 + dB * 191.0f / 12.0f;
```

| | value |
|---|---|
| hardware range | **64 … 255** (191 steps) |
| displayed range | **0 … +12 dB**, integer dB |
| default | **64**, i.e. 0 dB |
| resolution | ~0.063 dB per step |

`Device424::CreateTrimValue` also calls vtable `+0x78` with `64.0`, which matches
what the card returns: **`GetInputTrim` reads a uniform 64 on all 96 channels**
of the studio rig (HD192, 2×24I/O, 2408mk3, active and inactive alike),
confirmed with `MotuTrim`. So 64 is the shipped default everywhere, not a
per-channel calibration, and a rig that has never touched trim reads 0 dB
across the board.

Note this is a **gain-only** trim: there is no attenuation below 0 dB, so trim
cannot fix a source that is too hot, only lift one that is too quiet.

### Bus assign

`ValueLegacyBusAssign` uses **255 as the "none" sentinel**: `ConvertToUIControlValue`
maps 0 → 255, and `ConvertFromUIControlValue` maps 255 → 0.

## Meters

`ReadLevelMeters` is slot 21 and was the largest gap. `CoreDeviceAW::AWReadLevelMeters`
is a bare pass-through (cpp:396), so the structs are defined by its only caller,
`CoreDeviceAW::UpdateLevelMeters`. Both are recoverable from the stack frame it
builds.

```c
// 200 bytes.
struct AudioWireLevelMeterRequest {
    uint32_t bus;             // +0x00  mix * 2, same doubling as everywhere else
    uint32_t numChannels;     // +0x04  how many entries of channels[] are valid
    uint32_t channels[48];    // +0x08  card-wide input ids to meter
};

// >= 0x1A0 bytes; MOTU reserves 0x1A8 and zeroes 0x1A0 of it.
struct AudioWireLevelMeterResults {
    int32_t level[48];        // +0x000  linear, 0 .. 32768
    int32_t clip[48];         // +0x0C0  0 = none, 1 = clip, 2 = half
    /* +0x180 .. +0x1A0 zeroed by MOTU, never read back — unidentified */
};
```

**Level scaling** comes from `ValueLegacyLevelMeter`, and it is the same 32768
as the fader: `ui = raw / 32768`, `raw = ui * 32768`. So `level[i]` is a linear
amplitude in 1/32768 units, full scale at 32768.

The binary does contain the ordinary amplitude law as a helper —
`LinearToDecibel(float x)` is exactly `20.0f * log10f(x)`, wrapped by
`LinearToDecibelUString` — but its only traced callers are an FX reverb value
and a generic `StringDisplayLinearGain`, neither on the legacy meter path. So
treat `20·log10(level/32768)` as the natural reading of a linear amplitude and
**not** as a recovered fact: the legacy meter's own dB readout has not been
traced. What *is* established is that the meter scale and the fader scale differ
— the fader's readout is 40·log10 — and share only their full-scale value of
32768.

**Clip** is read as an enum, not a boolean, and drives a float:

| `clip[i]` | UI value |
|---|---|
| 1 | 1.0 |
| 2 | 0.5 |
| anything else | 0.0 |

Two states above zero — presumably full clip and a decayed or held indication.
Which is which needs signal, and is the clearest thing to settle at home on the
UltraLite.

**How many meters.** `AWGetMaxNumLevelMeters` (cpp:406) asks the card
`GetCardType(exc, &type)` and indexes a three-entry table:

| `GetCardType` | max meters |
|---|---|
| 0 | 24 |
| 1 | 24 |
| 2 | 48 |
| anything else | 48 |

### Ballistics

`LevelMeterView::SetValue(float, unsigned)` is the whole of MOTU's meter
behaviour:

```c
level = max(level, 0);
if (place >= count) return;
if (bar[place] != level) { bar[place] = level; dirty = true; }   // no smoothing

now = Awesome::GetAbsoluteTime();
if (now <= nextUpdate) return;                  // a global minimum interval

if (level >= peak[place]) {                     // new peak: hold it
    peak[place] = level;
    holdUntil[place] = now + peakHoldSeconds;
    dirty = true;
} else if (now > holdUntil[place] && peakHoldSeconds != -1.0) {
    if (bar[place] <= 0.005f && peak[place] > 0)   // only once the bar is down
        peak[place] -= 0.015f;                     // decay, per update
}
```

Two things worth noting. **The bar has no attack or release** — it follows the
value directly, because each `ReadLevelMeters` already reports the peak since
the previous read. And the peak decays **per update, not per second**: `0.015`
in 0..1 units, gated on the bar having fallen below `0.005`. MOTU's own update
rate is not a constant in the binary, so the visible decay speed is the one
thing to calibrate by eye against MOTU's console. `kPeakDecayPerTick` /
`kPeakDecayFloor` in `src/cuemix-fx/ConsoleModel.cpp` carry both numbers.

The default hold sits in a global double at `0x1002868e8`, which reads **4.0** —
agreeing with the `PeakHoldTime = 3` pref and the table below.

### The artwork, and where the meter goes

`Level.png` is 33 x 183: a sheet of **three 11 x 183 frames**, and within a
frame:

| rows | what |
|---|---|
| y 0..11 | the clip indicator cap, 12 px |
| y 13..182 | the ticked level column, 170 px, filled from the bottom |

| frame | role | segment colour | cap colour |
|---|---|---|---|
| 0 | bright fill | `rgb(98,145,235)` | amber `rgb(255,206,23)` |
| 1 | dim fill — MOTU's RMS; the PCI back end reports no RMS | `rgb(70,118,208)` | amber |
| 2 | unlit | `rgb(16,18,76)` | `rgb(38,39,92)` |

So a meter is: the unlit frame whole, the bright frame clipped to the bar, a
slice of the bright frame at the held peak, and the bright frame's cap when the
channel has clipped.

**Input strips do not draw the cap.** `ChannelStripMixLegacy.png` (81 x 307)
already bakes the unlit column into the strip at x 65..75, y 72 onward — which
puts the `Level` sprite's origin at `(x + 65, 223)`, the same y as the master
meters. The cap area is strip background there, so drawing it would paint over
the artwork. A strip's clip indication is the **trim LED** instead.

### Which clip value means what

`TrimClipIndicator.png` is 18 x 6 — three 6 x 6 frames, "off, signal, clip".
`UpdateLevelMeters` maps the card's clip field to a float as 1 -> 1.0,
2 -> 0.5, anything else -> 0.0. Against a three-frame indicator that is
`frame = value * 2`, which makes:

| card `clip[i]` | UI value | frame | meaning |
|---|---|---|---|
| 0 | 0.0 | 0 | off |
| **2** | 0.5 | 1 | **signal present** |
| **1** | 1.0 | 2 | **clipped** |

That answers the question this document previously listed as needing the card,
by inference rather than observation: the mapping is forced by the frame count
and the 1.0/0.5/0.0 values. `meter::clipFrame` implements it. Still worth
confirming with real signal, because the frame order is read from the artwork
rather than from code.

### Peak hold time

`LevelMeterView::ConvertPeakHoldTimeEnumToSeconds(int)` indexes a table of seven
doubles at `enum - 1`, falling back to `4.0` for anything out of range:

| enum | seconds | menu item |
|---|---|---|
| 1 | 0 | Off |
| 2 | 2 | 2 Seconds |
| 3 | 4 | 4 Seconds |
| 4 | 10 | 10 Seconds |
| 5 | 60 | 1 Minute |
| 6 | 300 | 5 Minutes |
| 7 | **−1** | Infinite |

Seven entries in exactly the order `LocalizableStrings.xml` lists, so the enum is
1-based over that menu. `ORIGINAL-UI.md` records `PeakHoldTime = 3` in the Mojave
prefs and guesses *10 Seconds*; on this table 3 is **4 Seconds**. The
out-of-range fallback of 4.0 s agrees with that being MOTU's default.

`−1` for Infinite means the hold never expires — Clear Peaks (⌘\) is the only
way out.

**The request is built from visible strips only.** `UpdateLevelMeters` walks
input channels in order, asks the `Device` for the meter parameter of
(currentMix, channel), skips the channel if the parameter is absent or reports
false, and otherwise appends it — stopping at `maxNumLevelMeters`. So the
request is not "all 96 channels": it is the strips actually on screen for the
selected mix, capped by card type. The read loop then bounds itself by
`min(request.numChannels, maxNumLevelMeters)`.

Per polled frame MOTU then writes two parameters per channel: the clip float
(from `clip[i]`) and the level (`(float)level[i]`, integer converted, *not* the
0…1 UI value — the `Value` applies `ConvertToUIControlValue` itself).

## What this corrects in the existing docs

| Document | Said | Actually |
|---|---|---|
| `CUEMIX-PLAN.md` | fader dB is `20·log10(v/32768)` | `40·log10(v/32768)`, formatted `%+.1f` / `%+.0f`, `-inf`, `0.0` |
| `CUEMIX-PLAN.md` | `trim (v−64)` | per-channel min/max; readout is `floor(hw)` signed |
| `CUEMIX-PLAN.md` | `ReadLevelMeters` not wrapped, structs unknown | structs above |
| `CUEMIX-PLAN.md` | "what Mix N maps to — unknown" | mix index 0–47, bus id = 2 × mix |
| `CUEMIX-PLAN.md` | CueMix binary is "i386, stripped" | 2025 universal builds, unstripped, PCI back end included |
| `ORIGINAL-UI.md` | every renderer is `…MacCarbon`, so it cannot survive modern macOS | true of the 1.5/73220-era builds; the 2025 CueMix FX carries `ViewMacCocoa`, binds **no** Carbon symbols, and runs on Sequoia today. PCI Audio Setup 1.5 is still i386 PowerPlant and still dead |
| `README.md` | CueMix FX "starts and exits immediately" because it draws through Carbon | it does not: 1.6 b5003c51d runs on Sequoia 15.7.4 right now with an UltraLite mk3 Hybrid. Whatever ends it on a PCI-424 box is **device discovery**, not the UI toolkit |
| `CUEMIX-PLAN.md` | balance/width getters segfault past the end | they do; MOTU bound-checks every index in the wrapper before calling |
| `CUEMIX-PLAN.md` | pan `v − 64` | confirmed exactly, with a `+` prefix when positive |
| `ORIGINAL-UI.md` | `PeakHoldTime = 3` "would be *10 Seconds*" | 3 is **4 Seconds**; the enum is 1-based over the strings-file order |

## Still needs the card, or signal

- **Where `flags & 0x10` gets set.** Until that is traced, we know writes are
  gated but not by what. Our own code will not have the gate; the risk is only
  that it encodes a condition we ought to respect.
- ~~**`GetCardType` on a PCIe-424**~~ — **answered 2026-09-19: `cardType = 2`,
  so `maxLevelMeters = 48`.** Read off the studio card (HD192 + 2×24I/O +
  2408mk3). Note 48 is *fewer than the rig's 68 active inputs*, so a full PCI
  system cannot meter every input at once and the console must choose which 48.
- ~~**Trim's min/max for an AudioWire channel**~~ — **answered 2026-09-19:
  64…255 hardware, 0…+12 dB displayed, default 64.** From
  `Device424::CreateTrimValue` plus `StringDisplayScalerPrintf::ConvertToString`,
  and confirmed against the card. See "Solved 2026-09-19" above.
- **Which clip state is 1 and which is 2** — now answered by inference (see
  "Which clip value means what"): 1 is clipped, 2 is signal present. Worth one
  confirmation with real signal, since the frame order comes from the artwork.
  It has to be the PCI card: the half value is produced only by
  `CoreDeviceAW::UpdateLevelMeters`, and `LevelMeterSubsystemFX` — what a
  FireWire/USB interface runs through — contains no `0.5` immediate anywhere in
  its ~11 KB of code, so an UltraLite cannot show this state at all.
- ~~**The 32 bytes at `+0x180` of the results struct.**~~ — **largely answered
  2026-09-19: they carry two stereo bus meters.** See "The results tail is bus
  metering" below.
- **Whether `DoesCueMixFaderHaveResources` ever refuses**, and where MOTU calls it.
- **Everything about writes, end to end.** All of the above is MOTU's intent as
  compiled; none of it has moved a fader on a real PCI-424 yet.

## The results tail is bus metering

Measured 2026-09-19 on the studio PCIe-424 with `MotuMeters`, bus 0.

MOTU's own code zeroes `results + 0x180` and never reads it back, so it was
recorded here as unidentified. The card fills it anyway. Of the 40 bytes, **four
`u32` slots are live and the remaining 24 are always zero**:

| offset | contents |
|---|---|
| `+0x180` | mix input-sum meter, **left** |
| `+0x184` | mix input-sum meter, **right** |
| `+0x188`–`+0x18F` | always zero |
| `+0x190` | mix **output** meter, left |
| `+0x194` | mix **output** meter, right |
| `+0x198`–`+0x1A7` | always zero |

The identification is from correlation, not from the binary. With only the
Andromeda feeding the card (channels 34/35), `+0x180`/`+0x184` tracked those two
channel levels to within a couple of counts on every read — the small difference
is consistent with being sampled at a slightly different instant, not with being
a different quantity:

```
t=13s   ch34 12090  ch35 11589   |  +180 12090   +184 11588
t=22s   ch34 12532  ch35 12394   |  +190 12532   +184 12394
t=37s   ch34 11114  ch35 11605   |  +180 11113   +184 11607
```

`+0x190`/`+0x194` is a different signal. It was **already live while every input
channel read zero**, decaying from 23560, during host playback to outputs 1–2;
once the synth came in it sat consistently *above* the input sum (23643 against
11113). So it is the bus after everything summed into it, host playback
included, whereas `+0x180` is the input contribution alone.

**Caution on scale.** Across two runs the input pair never set bit 14 (OR =
`0x3FFF`) while the output pair reached `0x7FFF`. That is suggestive of a
half-scale input meter, but it is a bitwise OR over observed values, not a
measured ceiling, and the loudest input seen was only ~12900. **Do not encode a
16383 cap** until something drives an input past half scale and the value is
watched. Until then treat both as the usual 0..32768.

### Consequence for our console

Two things follow for `src/cuemix-fx`:

- there is a **real bus meter available for the MIX section**, free with the same
  read, rather than something we would have to sum ourselves;
- **the whole call returns zeros unless the card's audio engine is running.**
  See below — this is the single easiest way to misread the meter path as broken.

## Meters need a running audio engine

`ReadLevelMeters` succeeds, raises nothing, and returns an all-zero struct —
levels, clip and tail alike — whenever the card's engine is idle:

```
IOAudioEngineState                = 0
IOAudioEngineNumActiveUserClients = 0
```

Confirmed 2026-09-19 by accident: a capture taken while synths were playing into
the inputs read zero throughout, because nothing held the CoreAudio device open.
Starting playback and repeating the run, with the same signal, produced levels
immediately. Feeding the inputs is not enough; a client has to be streaming.

To check before blaming the decode:

```sh
ioreg -c com_motu_driver_PCIAudio_Engine -r -w0 | grep -E "IOAudioEngineState|NumActiveUserClients"
```

`MOTU PCI Audio Setup` does not stream, so opening our own console alone will not
light the meters either. Any DAW or system playback routed to the card will.

## What does not transfer from a FireWire/USB interface

`CUEMIX-PLAN.md`'s stage 0 rests on "CueMix FX keeps a hardware-independent
parameter tree, so learn the model at home on the UltraLite". The tree is
shared; **almost nothing hanging off it is.** Each back end brings its own
parameter dispatch, its own value classes, and its own meter transport:

| | PCI-424 (`CoreDeviceAW`) | UltraLite mk3 etc. (`CoreDeviceMacFWFX`) |
|---|---|---|
| param-id dispatch | `SetValueByParamID` + `Set{Input,BusInput,Talkback}ValueByGlobalID` | its own `SetValueByParamID`, no `…ByGlobalID` split |
| fader law | `ValueLegacyFader` — `raw/32768`, quantized to multiples of 256 | `ValueFaderFX` — **`ui = sqrt(raw)`, `raw = ui²`** |
| pan / trim | `ValuePanLegacy`, `ValueLegacyTrim` | `ValuePanFX`, `ValueTrimFX` |
| meters | `AudioWireLevelMeterRequest/Results` via `CueMixAPI` slot 21 | `LevelMeterPacketRequest/Results` via `GetNextLevelMeterPacket`, driven by `LevelMeterSubsystemFX` |
| meter range | base `Device::GetLevelMeterRange` (returns 0; no AW override) | `DeviceUltraLiteMk3::GetLevelMeterRange` override |
| half-clip state | produced here | **never produced** |

So the selector numbers, value laws, struct layouts and clip semantics in this
document are PCI-only, and none of them can be confirmed or refined on a
FireWire/USB box.

What *is* genuinely shared, and therefore worth learning anywhere:

- **OSC.** It lives in `DeviceListManager` / `DeviceController`
  (`GetOSCDeviceSpecList`, `RebuildOSCClientsMenu`, `OSCZeroConf`), not in any
  back end, and `ORIGINAL-UI.md`'s PCI menu capture shows *Configure OSC
  Devices…* present. **OSC is available on a PCI-424**, so there is no need to
  use another interface as a proxy for it.
- **The view layer** — `LevelMeterView` holds parallel level / RMS / clip
  `Value*` arrays and routes by pointer identity; `SetPeakHoldTime` and
  `ConvertPeakHoldTimeEnumToSeconds` are shared.

## Reproducing this

The binary is on this machine and unstripped, so any of this can be re-checked.
`tools/dis-cuemix.sh` wraps the mechanics:

```sh
tools/dis-cuemix.sh syms  CoreDeviceAW          # list the class, demangled
tools/dis-cuemix.sh fn    AWSetCueMixVolume     # disassemble by name
tools/dis-cuemix.sh range 100037974 100037a88   # or by address
tools/dis-cuemix.sh calls DigiVolToDecibelString
```

Set `CUEMIX_APP` to read a different copy (the `/Volumes/macOS Work` build is a
useful second opinion) and `CUEMIX_ARCH=x86_64` for the other slice.

Two traps worth knowing, both of which cost time here:

- `llvm-objdump --disassemble-symbols` **does not work** on these functions.
  The `CoreDeviceAW` methods are local (`t`) symbols and objdump reports them
  missing. Disassemble by address range instead, taking the stop address from
  the next entry of `nm -n`.
- When picking that next address, **compare as strings**. awk reads a
  zero-padded hex address containing `e` — `100001e94` — as scientific
  notation, so a bare `$1 > start` silently returns a symbol from the wrong end
  of the binary and the disassembly looks plausible but belongs to another
  function.
