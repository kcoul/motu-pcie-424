# Channel state, interface options, and where names live

Everything here was decoded this session by reading the live card and checking
the answers against MOTU's own console in `docs/reference/`. The screenshots are
the oracle: a decode is only accepted here if it reproduces a number or a
control position that MOTU's app displays.

## GetInputState / GetOutputState  (slots 20 and 26)

```
GetInputState (MOTUException*, int id, unsigned char* exists, unsigned char* enabled)
GetOutputState(MOTUException*, int id, unsigned char* exists, int* source)
```

Wrapped as `Card::inputState()` / `Card::outputState()`. **Pinned by
disassembling MOTU's original console** (i386, symbols intact), which reads the
checkbox from the *second* input byte (`0xb3c9` area) and from `source == -1`.

| field | meaning | this rig |
|---|---|---|
| `exists` | the channel is populated (0 for the half of an HD192's 24-id slot it does not fill) | 84 of 96 |
| `enabled` | the **Enable Input** checkbox; stored per pair in the driver | 36 |
| `source` | −1 = **Enable Output** checked, −2 = unchecked, ≥ 0 = routed from that input id | −1 / −2 |

`enabled` totals exactly `numActiveInputs()`, and the console's
**"PCI Use: Ins enabled N, Outs enabled N"** prints `GetNumActiveInputs` /
`GetNumActiveOutputs` directly.

> An earlier revision had the two input bytes the other way round, supported by
> "84 − the 2408mk3's 24 ids = the screenshots' 60". That was a coincidence: the
> Mojave card simply had 60 channels enabled.

### Changing channels

MOTU's console, per click (`AWConfigPane::PaneChanged`, `UpdateInputPair`,
`UpdateOutputPair`):

1. `SetInputEnable(id, on)`, or `SetOutputSource(id, on ? -1 : -2)`.
2. The same for the pair's partner (id+1 for an even id, id−1 for an odd one).
3. `CommitChanges(true)`.

`CommitChanges(sync)` (HAL `0x7710`) sends the pending dictionary to the driver
under `"Set"`, or under `"Config"` with `BoostPriority` for structural changes.
With `sync` it waits up to **5 s** in run-loop mode
`com_motu_driver_PCIAudio_PlugIn_RunLoopMode` for the acknowledgement, then
`QueueSavePrefs()`. It is a no-op when nothing is pending. The console's wrapper
always passes `true` and **never calls `FlushPrefs`**.

Bank personality is `SetPersonalityForBank(bank, n)` then commit. Options panes
GET, modify, SET, commit. Sample rate, clock source and Default In/Out are plain
CoreAudio properties (`nsrt`, `csrc`, `dch2`).

### PCI Use

MOTU's format string (`.rsrc` STR# index 13):

```
PCI Use: Ins enabled %ld, Outs enabled %ld, Aprx %.2f MB per sec.
MB = (ins + outs ? ins + outs + 2 : 0) * rate * bytesPerSample / 2^20
```

`bytesPerSample` = `mBytesPerFrame / mChannelsPerFrame` of the output stream's
physical format: 3 on this card, on Mojave and on Sequoia.

### Channel ids

`id = wire * 24 + channel within the wire`. Banks are contiguous: bank *b*
starts at the sum of `GetNumberOfChansInBank` for the banks before it.

## OtherInterfaceOp  (Interface slot 5) — the Options panes

```
OtherInterfaceOp(MOTUException*, bool isGet, int selector, int& value)
```

**The bool is `isGet`, not `isSet`.** Passing it wrong does not fail: the driver
silently takes the write path and stuffs whatever is in `value` into its pending
dictionary. `Interface::getOption()` / `setOption()` exist so no caller has to
know this. Disassembly at `HALPlugin` x86_64 `0x97e0`:

```
cmpl  $0x8, %ecx        ; selector > 8 -> ThrowException(4, 3, ..., line 80)
testb %r14b, %r14b      ; the bool
jne   0x98b0            ; TRUE  -> CFDictionaryGetValue  == GET
                        ; FALSE -> AddNumberToDictionary == SET
```

Selectors come from the jump table at `0x9884` (9 int32 offsets):

| sel | key | control |
|---|---|---|
| 0 | `AnalogMirror` | 2408mk3 "Bank to mirror on Analog" |
| 1 | `AESOutputSRCMode` | HD192 **Mirror Analog** (the key name is misleading) |
| 2 | `AESInputSteal` | HD192 Steal Inputs |
| 3 | `AESOutputClock` | HD192 Output Clock + Fixed Frequency |
| 4 | `AESInputSRC` | HD192 AES/EBU input Rate Convert |
| 5 | `PeakHoldTime` | HD192 **Clip** Time-out (crossed) |
| 6 | `ClipHoldTime` | HD192 **Peak/Hold** Time-out (crossed) |
| 7 | `InputLevels` | input reference level, +4 dBu / −10 dBV |
| 8 | `WordOutRange` | word out rate |

An interface that does not implement a selector returns **without raising** and
leaves the out-param untouched, so "no exception" cannot be trusted. `getOption`
calls twice with two different sentinels and reports absence honestly.

That absence is the feature: it tells you which controls a pane should show.

| interface | implements |
|---|---|
| HD192 | AESOutputSRCMode, AESInputSteal, AESOutputClock, AESInputSRC, PeakHoldTime, ClipHoldTime |
| 24I/O | InputLevels, WordOutRange |
| 2408mk3 | AnalogMirror, InputLevels, WordOutRange |

This reproduces `docs/reference/setup-options-2408mk3.png` exactly — that pane
shows Bank-to-mirror, Input Reference Level and Word Out Rate, and nothing else.
Its three live values agree too: `AnalogMirror=0` = "Bank A", `WordOutRange=0` =
"Match system clock", `InputLevels=0` = all pairs +4 dBu.

`InputLevels` is a bitfield with **set = −10 dBV**, **bit n = row n** (least
significant bit = the first row). One bit per radio row: 4 bits (pairs
`1-2`…`7-8`) on the 2408mk3, 3 bits (`1-8`, `9-16`, `17-24`) on a 24I/O. The
Mojave prefs hold `InputLevels = 7` for both 24I/Os, which is the all −10 dBV
pane in `setup-options-24io.png`.

### Encodings, from MOTU's pane code

All confirmed by disassembly, and reproduced exactly by our HD192 pane against
`setup-options-hd192.png`:

| control | selector | values |
|---|---|---|
| Steal Inputs | 2 | 0 None, 1 In 1-2 … 6 In 11-12 |
| Rate Convert | 4 | 0 / 1 |
| Mirror Analog | 1 | 0–5 Out 1-2 … Out 11-12; 8–13 In 1-2 … In 11-12 (6, 7 unused) |
| Output Clock | 3 | 0 System, 1 AES Input, 2 AES Word In; 44.1 / 48 / 88.2 / 96 kHz = 3–6 with Fixed Frequency, 7–10 without |
| Clip Time-out | **5** | 0 No Delay, 1 2 s, 2 4 s, 3 10 s, 4 1 min, 5 5 min, 6 8 min, 7 Infinite |
| Peak/Hold Time-out | **6** | same list |
| Bank to mirror on Analog | 0 | 0 Bank A, 1 B, 2 C |
| Word Out Rate | 8 | 0 Match system clock, 1 44.1/48, 2 88.2/96 |

The Mojave values (`PeakHoldTime = 4`, `ClipHoldTime = 1`) show as Clip
*1 Minute* and Peak/Hold *2 Seconds*, which is what proves the crossing.

MOTU's own write path for Word Out Rate and Bank to mirror passes the 1-based
popup item without subtracting 1 (`0x173b2`, `0x1781a`), which looks like a bug
in the original. We write the value the GET path displays.

**ADAT Mode** on the 2408mk3 is the card-level `SMUXOptionSetting`, inverted:
non-zero shows **Type I**, zero shows **Type II** (both volumes store
`SMUXOptionEnable = 0`, and Mojave shows Type II).

## Custom channel names are not on the card

`GetChannelNameCFString` returns empty for every channel on this machine, while
the Mojave screenshots show "Console L", "Bay 3", "Supernova L" and so on.

That is correct behaviour, not a bug. Custom names live in the **per-OS driver
preference file**, not in card NVRAM:

```
~/Library/Preferences/com.motu.PCIAudio/PCI-424.bus<N>.slot0.plist
```

with keys `InputNames` / `OutputNames` (96 entries each), alongside
`InputChannels` / `OutputChannels` (the enable arrays), `SampleRate`,
`ClockSource`, `Interfaces`, `PreferredInput` / `PreferredOutput`, `Talkback`
and `SMPTE`. The Sequoia copy has 96 empty name slots; the names were typed on
another boot volume and stay there.

Names are **UTF-16LE `<data>` blobs with a BOM**, not plist strings:

```
InputNames[0] = ff fe 43 00 6f 00 6e 00 73 00 6f 00 6c 00 65 00 20 00 4c 00
                         C     o     n     s     o     l     e           L
```

so a naive plist read shows them as empty. Decoding those blobs off the Mojave
volume is the whole of the migration.

### The enable array is per *pair*

`InputChannels` holds **48** entries, not 96 — one per channel pair, matching
the console's grid, which is drawn in pairs (`1-2`, `3-4`, …). The Mojave copy
has **30 of 48 set = 60 channels**, which is the `Ins enabled 60` the
screenshots show, independently of the `GetInputState` decode above.

Sequoia's copy has 19 pairs set; the live card has 36 channels enabled.

### Interface options *are* in the prefs, per OS

An earlier revision of this file said the `Interfaces` array held only `Type`
and that options were card state shared across volumes. **That was wrong.** Each
`Interfaces[n]` entry is:

```
{ Type = 192 | 2410 | 2403,
  Banks = [ personality per bank ],
  DeviceSpecific = { <OtherInterfaceOp key> = value, ... } }
```

and `DeviceSpecific` holds exactly the keys `getOption` finds on that interface.

The two volumes disagree, and the card follows whichever OS is booted. On
Mojave both 24I/Os have `InputLevels = 7` (the pane shows −10 dBV). On Sequoia
the plist and the live `getOption` both say `0`. The driver loads
`DeviceSpecific` into its pending dictionary at start-up, which is the dictionary
`OtherInterfaceOp`'s GET reads.

So a Mojave Options screenshot verifies **layout and encoding**, not the values
you will read on Sequoia. Compare it against the *Mojave* plist. The HD192 and
2408mk3 happen to hold the same values on both volumes.

Consequences for the port:

- Channel names will not appear until they are set on *this* OS, and the
  replacement "Edit Channel Names" window must write them.
- Note the filename carries the **PCI bus number**, so the same card produces a
  different prefs file in a different slot. The Big Sur volume has four
  (`bus6`, `bus7`, `bus8`, `bus19`) from exactly that.
- Importing the Mojave names is a plain plist copy of `InputNames` /
  `OutputNames`, and would be a kindness. The same importer could offer
  `Interfaces[n].DeviceSpecific`: the Mojave setup has the 24I/Os at −10 dBV,
  and Sequoia has them at +4 dBu.
