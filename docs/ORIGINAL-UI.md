# What the original apps actually are

Everything here was recovered statically from MOTU's own bundles on the Mojave
volume (`/Volumes/Sierra`, mislabelled — it is 10.14.6). Nothing here required
booting it. It exists so the replicas can be faithful, and so we know exactly
what still has to be captured by eye.

## Both apps are MOTU "AwesomeLib" apps

MOTU built an in-house cross-platform UI toolkit, `AwesomeLib`, and drew both
consoles with it. The build paths are still in the binaries:

```
AwesomeLib/Source/UI/DrawingContextMacCarbon.cpp
AwesomeLib/Source/UI/ViewMacCarbon.cpp
AwesomeLib/Source/UI/ViewWindowMacCarbon.cpp
AwesomeLib/Source/UI/PngImageMacCarbon.cpp
AwesomeLib/Source/UI/TextLayoutMac.cpp
AwesomeLib/Source/UI/OSTextEditMac.cpp
Consoles/CueMix3/Source/Device/CoreDeviceAW.cpp      <- the PCI/AudioWire back end
Consoles/CueMix3/Source/Main.cpp
```

Every renderer is `...MacCarbon`. **Both apps draw through Carbon**, which is why
neither survives on modern macOS regardless of architecture, and why "port it to
AppKit so it looks native" is the wrong instinct — neither app ever used a
single stock control.

## MOTU PCI Audio Setup

| | |
|---|---|
| Original | i386, 10.6 SDK, `LSRequiresCarbon=1`, Metrowerks PowerPlant |
| Bundle ID | `com.motu.pci.config.console` |
| Version | 1.5 (2003–2011) |
| Window title | **MOTU PCI Audio Console** (not "Setup") |
| Content size | **602 × 334** px, `noGrowDocProc` — fixed size, not resizable |

The `.rsrc` holds only two `PPob` (PowerPlant object) resources: `128 "Console
Main Window"` and `130 "Splash"`. **`PPob` 128 is 155 bytes** — a bare
`MTWN`/`MTPN` window-and-pane shell. There is no declarative layout to recover:
every control is created in code.

Menus are tiny. `MENU 129 "File"` is the whole File menu:

```
Save Configuration  ⌘S
Load Configuration  ⌘O
-
-
Refresh             ⌘R
-
Close               ⌘W
```

Control inventory, from strings in the binary:

- **Sample Rate**, **Clock Source** — these are CoreAudio device properties, not
  MOTU API calls (`kAudioDevicePropertyNominalSampleRate`, `…ClockSource`).
- **Default Input**, **Default Output** — `In 1-2` … `In 11-12`, `Out 1-2` … `Out 11-12`
- **Enable Routing**, **Enable Volume Controls**
- **PCI Use:**, **AudioWire:** — usage meters
- **Configure Interface** / **Interface Options…** / *No options available for this interface.*
- **Edit Channel Names…** — launches the bundled `MOTU Channel Names.app`
- Per-interface option panes: **HD192 Options** (Clip Time-out, Peak/Hold
  Time-out, Meter Options, AES/EBU Input Options, AES/EBU Output Options, Rate
  Convert, Steal Inputs, AES Word In, Fixed Frequency / Match system clock,
  Type I / Type II), **Input Reference Level**, **Mirror Analog** / *Bank to
  mirror on Analog:*, **Word Out Rate:**
- Error strings: *The driver has reported a {HAL, kernel, MOTU, Unix, OS,
  unknown} error.* + *Error Code: * — these map onto the `kind` field of the
  exception record (see `motu_card.h`).

## CueMix FX

| | |
|---|---|
| Mojave copy | i386, 1.6 **73220**, Cocoa shell (`NSPrincipalClass=NSApplication`) |
| Bundle ID | `com.motu.CueMixFX` |
| Nibs | `CueMixFXCocoa.nib` + `CueMix.nib` — **menu bar only**, 4–6 KB |

The Cocoa nib supplies the menu bar and nothing else; the console is AwesomeLib
drawing PNGs into a Carbon view. 145 PNG skin assets ship in `Resources/`.

`English.lproj/LocalizableStrings.xml` (744 lines) is the complete UI string
spec — menus, dialogs, labels, error text — in MOTU's own `<TEXT name=…>` form.
Copy behaviour from it rather than guessing.

### The assets that matter for a PCIe-424

The skin has separate art for the modern FireWire/USB boxes and for the older
PCI rigs. Ours is the "PCI"/"Legacy" set:

| Asset | Size | Role |
|---|---|---|
| `BackgroundRightPCI.png` | 267 × 473 | right-hand panel, PCI variant |
| `ChannelStripMMPCI.png` | 90 × 250 | mix/master strip, PCI variant |
| `BackgroundLeftLegacy.png` | 100 × 473 | left panel |
| `BackgroundRightLegacy.png` | 267 × 473 | right panel |
| `BackgroundTileLegacy.png` | 100 × 473 | repeating body tile |
| `ChannelStripMixLegacy.png` | 81 × 307 | mix strip |
| `ChannelStripMMLegacy.png` | 81 × 250 | mix master strip |
| `LCDBackgroundLegacy.png` | 243 × 112 | LCD readout |
| `SoloLightLegacy.png` | 126 × 37 | solo indicator |
| `FaderBody.png` / `FaderCap.png` | 81 × 250 / 44 × 47 | fader |
| `KnobRotation.png` | 208 × 208 | knob sprite sheet |
| `MeterBig/Small/Bus.png` | 203×78 / 120×67 / 62×60 | meters |

The 473 px-tall backgrounds and 250 px strips pin the console's vertical
geometry; the EQ/Dynamics/Reverb/Tuner art belongs to FX-capable interfaces and
is dead weight for a PCIe-424.

## What is NOT recoverable without running the originals

Static extraction gives us strings, assets, asset geometry, menu structure and
the main window size. It cannot give us **layout** — both apps position every
control in code, and we do not have that code.

So this is the list to capture from Mojave, by eye:

1. `MOTU PCI Audio Console` main window, full-size screenshot — where each of
   the controls above sits inside the 602 × 334 content area.
2. Every **Interface Options** pane, one per interface type (HD192, 24I/O,
   2408mk3), since ours has three distinct ones.
3. `MOTU Channel Names` window.
4. CueMix FX: the console with all four interfaces attached — channel strip
   order, how 96 inputs are paged/tabbed, the bus selector, and the
   right-hand panel contents in the PCI variant.
5. CueMix FX menus, and the Talkback/Listenback panel.

Screenshots into `docs/reference/`.
