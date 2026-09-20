# PCI Audio Setup — parity with MOTU's original

Milestone 1 is making our PCI Audio Setup **bulletproof**, and that needs a
definitive list of what MOTU's own app did, not an impression of it.

This is the other half of the Mojave screenshots in `docs/reference/`. Those
record what the original *looked like* while running; this records what is
*inside it*, read out of the shipped binary — which matters because the app can
no longer run at all to be inspected.

## Where the original is, and why it cannot run

MOTU's `MOTU PCI Audio Setup` 1.5 ships **inside the kext**:

```
/Library/Extensions/MOTUPCIAudio.kext/Contents/MacOS/MOTU PCI Audio Setup.app
    Contents/MacOS/MOTU PCI Audio Setup          Mach-O i386
    Contents/Resources/MOTU PCI Audio Setup.rsrc classic resource fork
    Contents/Resources/MOTU Channel Names.app    the i386 helper we replaced
```

It is **i386**, and macOS removed 32-bit execution in Catalina. Unlike CueMix FX
— which was only ever blocked by a signing policy and now runs again after a
re-sign (`ORIGINAL-UI.md`) — there is no workaround here and never will be. Our
rebuild is the only way to configure this card on a modern OS.

> `/Applications/MOTU PCI Audio Setup.app` is **not** this app. That bundle is
> 64-bit, ad-hoc signed, built with the macOS 26.5 SDK, bundle id
> `com.motu.pci.config.console` — the third-party Swift/AppKit reimplementation
> listed under "Related work" in `README.md`. Do not mistake it for MOTU's.

## What the resource fork gives, and what it does not

Parsed with a small reader (26 resource types). Useful:

| type | contents |
|---|---|
| `MENU` | **File**: Save Configuration ⌘S, Load Configuration ⌘O, Refresh ⌘R, Close ⌘W |
| `WIND` | `128 "Console Main Window"`, `130 "Splash"` |
| `STR# 1026` | the 19 driver error messages, verbatim |
| `STR# 27545` | the UI vocabulary and two format strings |
| `DITL`/`ALRT` | almost entirely PowerPlant boilerplate |

**The controls are not in the resource fork.** `PPob 128` is 155 bytes and only
instantiates two custom classes, `MTWN` and `MTPN` — MOTU's own AwesomeLib
window and pane. The window is built in code, so the inventory below comes from
the binary's string table instead.

### STR# 27545 — the vocabulary

```
Internal   Control Track   ADAT   SMPTE   Video   Bank   From Computer   None
Enable   Disable   Input   Output   Source
PCI Use: Ins enabled %ld, Outs enabled %ld, Aprx %.2f MB per sec.
(AES/EBU not available at %d Hz)
```

The first format string is the MB/sec readout whose `+ 2` is still an open
question in `CUEMIX-API.md`. The second is a **conditional warning we do not
implement** — see the gaps below.

## Control inventory, and where we stand

Extracted from the i386 binary and matched against `src/pci-audio-setup/`.

**Present** — every one of these is implemented:

| | |
|---|---|
| Clock | Clock Source, Sample Rate, Output Clock, Match system clock, Fixed Frequency, Word Out Rate, Rate Convert |
| Routing | Enable Routing, Enable Volume Controls, Mirror Analog, Steal Inputs, Bank to mirror on Analog |
| Defaults | Default Input, Default Output |
| Options | Interface Options, Configure Interface, `<name>` Options, Input Reference Level, Type I / Type II |
| AES | AES/EBU Input Options, AES/EBU Output Options, AES Input, AES Word In |
| Meters | Meter Options, Clip Time-out, Peak/Hold Time-out, Infinite, No Delay |
| Files | Save/Load Configuration (`MOTU PCI Config.mcfg`), Refresh |
| Status | PCI Use |
| Extra | **Edit Channel Names** — ours replaces MOTU's i386 helper |

"HD192 Options" is not a literal in our source but is produced correctly:
`OptionsWindow.cpp:31` builds `name_ + " Options"`.

## Gaps worth closing

### 1. Driver error reporting — the real one

MOTU translated a driver exception into a sentence. We print the raw fields:

```
ours:   The driver has reported an error.  kind=3 code=4 at AudioWireCardImpl.cp:470
MOTU's: The driver has reported a MOTU error.
        an inactive input was specified in a call to the MOTU PCI Audio driver
        ErrorCode 4 in File AudioWireCardImpl.cp line 470
```

Everything needed is already carried on `motu::Exception` (`kind()`, `code()`,
`file()`, `line()`); only the mapping is missing. Two parts:

**a. Category, from `kind`.** Six exist, and they are *not* formatted alike:

| category | ErrorCode printed as |
|---|---|
| unknown, Unix, MOTU, OS | `%d` decimal |
| kernel | `%x` **hex** |
| HAL | `%-*.*s` — a **four-character code**, as text |

The HAL case matters: those are `OSStatus` four-char codes, unreadable as
integers.

**b. Message, from `code`.** `STR# 1026` holds all 19, e.g. *"the MOTU PCI Audio
card is currently in use by another application. Quit the other app and try
again"* — newly relevant now that a working CueMix FX can hold the card.

**Still unknown: the numbering of `kind`.** The six literals are pooled in the
binary as unknown, Unix, MOTU, kernel, OS, HAL, but pool order is not
necessarily enum order. Observed on real hardware:
`GetInputDescription(9999)` → `kind=3 code=4`, `GetOutputDescription(9999)` →
`kind=2 code=4`. Both are bad-index errors from the same driver, so a naive
"kind = domain" reading does not obviously hold, and `code=4` does not line up
with `STR# 1026`'s entries 10/11 for inactive input/output either. **Trace this
in the i386 binary before implementing** — guessing would produce confidently
wrong error messages, which is worse than the raw fields we print now.

### 2. Conditional and failure messages we do not show

- `(AES/EBU not available at %d Hz)` — shown against the AES options when the
  current rate cannot carry AES/EBU.
- `No options available for this interface.` — the empty case for an interface
  with nothing to configure.
- `Can not load the selected configuration because there are multiple AudioWire
  cards installed.`
- `Version mismatch. The application can not communicate with the driver.`
- `This application is not compatible with the MOTU 828 or MOTU 896.` — of no
  use to us; we are PCI-only by construction.

## Method

```sh
# resource fork: menus, string lists, dialogs
python3 rsrc.py "/Library/Extensions/MOTUPCIAudio.kext/Contents/MacOS/\
MOTU PCI Audio Setup.app/Contents/Resources/MOTU PCI Audio Setup.rsrc"

# control labels, which live in code rather than resources
strings -a -t x ".../Contents/MacOS/MOTU PCI Audio Setup"
```

Both are read-only and need no hardware.
