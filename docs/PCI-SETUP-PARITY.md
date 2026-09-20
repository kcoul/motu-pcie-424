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
| `STR# 1026` | 19 driver error messages — **vestigial, never read**; see below |
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

### 1. Driver error reporting — solved and implemented

**Two of our fields were the wrong way round.** The exception record was decoded
by probing, which cannot distinguish two adjacent small integers. MOTU's app
settles it: it **switches on `+0x04`** to choose the category and **prints
`+0x00` as "ErrorCode"**. We had them swapped.

```
     0x00  int32   errorCode     <- was documented as "kind"
     0x04  int32   domain        <- was documented as "code"
     0x08  int32   line
     0x0c  char[]  file
```

That also explains an anomaly the old notes recorded and could not account for:
a bad *input* index gave "kind=3" and a bad *output* index "kind=2", which are
not plausible as domains. Read correctly, **both are domain 4 (MOTU)** with
error codes 3 and 2 — exactly right for two bad-index calls into one driver.

#### The domain mapping

From `AWConfigPane::LoadConfig()`, a jump table of six indexed by `+0x04`:

```
11035: cmpl $0x5, 0x4(%esi)          ; domain
1103f: ja   0x111d3                  ; > 5 -> unknown
11048: movl 0x4d3(%ebx,%eax,4), %eax ; jump table
11051: jmpl *%eax
```

| domain | category |
|---|---|
| 0 | unknown error |
| 1 | OS error |
| 2 | HAL error |
| 3 | kernel error |
| 4 | MOTU error |
| 5 | Unix error |
| > 5 | unknown error |

Confirmed independently in `AWConfigPane::SaveConfig()`, whose own jump table
(at `ebx+0x5c5`) yields the same six branches in the same order. The literal
pool order — unknown, Unix, MOTU, kernel, OS, HAL — is **not** the enum order,
which is why reading it off the strings would have been wrong.

#### The code is formatted per domain

| category | ErrorCode printed as |
|---|---|
| unknown, Unix, MOTU, OS | `%d` decimal |
| kernel | `%x` **hex** |
| HAL | `%-*.*s` — a **four-character code**, as text |

The HAL case matters: those are `OSStatus` four-char codes and read as gibberish
in decimal. `Exception::errorCodeString()` follows this, falling back to decimal
when the four bytes are not printable.

#### Implemented

`motu::Exception` now has `errorCode()`, `domain()`, `domainKind()`,
`domainName()`, `errorCodeString()` and `message()`, and both apps show MOTU's
sentence. Verified on the card:

```
GetInputDescription(9999)   MOTU error, ErrorCode 3 at AudioWireCardImpl.cpp:470
  raw  03 00 00 00 | 04 00 00 00 | d6 01 00 00 | "AudioWireCardImpl.cp"
       errorCode=3   domain=4      line=470
```

#### Closed: `STR# 1026` is dead code

The 19 driver messages looked like the obvious companion to `errorCode`, and
they are not. **Nothing reads that list.**

Every `GetIndString` call in the binary was checked. All twelve with a literal
list ID use **27545**, the UI vocabulary:

```
UpdateStatusLine        27545[13]   "PCI Use: ... MB per sec."
MakeBankName            27545[6]    "Bank"
AddNonRoutingChannel    27545[9,10,11]   Enable / Input / Output
AddRoutingChannel       27545[9,10,11,12]
AddItemsForBank         27545[14]   "Disable"
```

The only other three are PowerPlant framework internals —
`LAction::GetDescription` and `LApplication::FindCommandStatus` — which take
their list ID from an object field and serve the undo/redo lists (150–157, 220).

The content confirms it. These are **classic Mac OS** messages:

> *"…make sure there is ONE copy of the latest version in the MOTU sub-folder of
> the **Extensions folder**"*
> *"Shut down, check that the card is correctly seated and reboot"*

The fork also carries a `ckid` resource, a CodeWarrior version-control artifact.
`STR# 1026` is a leftover from the OS 9 build that was never stripped when the
app was carried to OS X.

**So there is no second error enum.** The domain + `errorCode` path above is the
whole of MOTU's error reporting on OS X, and we now match it. Do not implement
`STR# 1026`: its text describes a system that has not existed for twenty years,
and a message like *"quit the other app"* would be actively misleading.

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
