# Channel state, interface options, and where names live

Everything here was decoded this session by reading the live card and checking
the answers against MOTU's own console in `docs/reference/`. The screenshots are
the oracle: a decode is only accepted here if it reproduces a number or a
control position that MOTU's app displays.

## GetInputState / GetOutputState  (slots 20 and 26)

```
GetInputState (MOTUException*, int id, unsigned char* enabled, unsigned char* active)
GetOutputState(MOTUException*, int id, unsigned char* enabled, int* source)
```

Wrapped as `Card::inputState()` / `Card::outputState()`.

| field | meaning | this rig |
|---|---|---|
| `enabled` | the **Enable Input / Enable Output** checkbox in the console's grid | 84 of 96 |
| `active` | channel is in the driver's current stream configuration | 36 |
| `source` | routing source `setOutputSource` writes | −1 active, −2 inactive |

`active` totals **exactly** `numActiveInputs()`, which is how it was identified.

`enabled` is 0 only for ids 12–23 — the half of the HD192's 24-id slot that a
12-channel HD192 does not populate — so it doubles as "this channel exists".
96 ids − 12 unpopulated = 84.

### Cross-check against the console

`docs/reference/setup-main-*.png` read **"PCI Use: Ins enabled 60, Outs enabled
60"** with the 2408mk3's grid fully unchecked. The 2408mk3 owns 24 ids, and
84 − 24 = **60**. That is the arithmetic that identifies `enabled` as the
checkbox, and it is why the two counts must never be conflated.

The same line's "Aprx 15.39 MB per sec" is reproduced exactly by

```
(ins + outs + 2) * rate * 3 bytes / 1 MiB
  = (60 + 60 + 2) * 44100 * 3 / 1048576 = 15.393
```

The `+ 2` is unexplained — one data point, so this is a fit, not a proof.

### Open question

`enabled` is 84 but `active` is 36 on this rig, and a 24-channel 24I/O bank
reports only 8 active. The likely explanation is that `enabled` is the desired
configuration and `active` is what the driver has actually committed, i.e. the
gap is what `CommitChanges` exists to close. Not yet confirmed — do not wire a
commit button to this until it is.

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
| 1 | `AESOutputSRCMode` | HD192 AES/EBU output rate convert |
| 2 | `AESInputSteal` | HD192 "Steal Inputs" |
| 3 | `AESOutputClock` | HD192 fixed frequency / match system clock |
| 4 | `AESInputSRC` | HD192 AES/EBU input rate convert |
| 5 | `PeakHoldTime` | meter peak/hold time-out |
| 6 | `ClipHoldTime` | meter clip time-out |
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

`InputLevels` is therefore a bitfield, one bit per pair, set = −10 dBV. Only the
all-zero case has been observed, so the bit order is still unverified.

**ADAT Mode Type I / Type II** is *not* an OtherInterfaceOp selector. Type II is
S/MUX, so it is the card-level `GetSMUXOptionSetting` / `SetSMUXOptionSetting`.

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

Consequences for the port:

- Channel names will not appear until they are set on *this* OS, and the
  replacement "Edit Channel Names" window must write them.
- Note the filename carries the **PCI bus number**, so the same card produces a
  different prefs file in a different slot. The Big Sur volume has four
  (`bus6`, `bus7`, `bus8`, `bus19`) from exactly that.
- Importing the Mojave names is a plain plist copy of `InputNames` /
  `OutputNames`, and would be a kindness.
