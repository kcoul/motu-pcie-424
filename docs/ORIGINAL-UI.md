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

> This holds for the 1.5 / 73220-era binaries above. It is **no longer true of
> CueMix FX**: the 2025 universal build carries a `ViewMacCocoa` renderer, binds
> no Carbon symbols at all (Carbon.framework is a vestigial load command), and
> has no `AwesomeLib/Source/...` paths left in it. MOTU ported AwesomeLib's
> drawing layer to Cocoa. PCI Audio Setup 1.5 was never rebuilt and is still
> i386 PowerPlant, so it stays dead. See `CUEMIX-API.md`.

## Why the originals do not run — what is ruled out

Measured on this Intel iMac19,2 (Sequoia 15.7.4, build 24G517) on 2026-09-18,
with CueMix FX 1.6 b5003c51d and an UltraLite mk3 Hybrid attached.

**CueMix FX runs on Sequoia.** It was up for 36 minutes while this was written,
driving the UltraLite, with its OSC server listening on UDP 63759. So the app
itself is not blocked by the OS, the toolkit, or its own signing.

Every explanation that has been offered for it so far is now eliminated:

| Theory | Verdict | Evidence |
|---|---|---|
| Draws through Carbon, so it cannot survive modern macOS | **wrong for this build** | no `AwesomeLib/…MacCarbon.cpp` paths, a `ViewMacCocoa` renderer instead, and **zero Carbon symbols bound** (Carbon.framework is a vestigial load command) |
| The app is not notarized / not signed | **wrong** | `spctl`: *accepted, source=Notarized Developer ID*; hardened runtime (`flags=0x10000(runtime)`), `Developer ID Application: MOTU (KRCLLMGZ2D)` |
| Library validation blocks the HAL plugin from loading into a hardened app | **wrong** | the PCI `HALPlugin.bundle` is signed by the **same** Team ID `KRCLLMGZ2D`, `codesign -v` reports *valid on disk* and *satisfies its Designated Requirement* |
| The in-process HAL plugin model is dead on modern macOS | **wrong** | `lsof` on the live process shows `MOTUFireWireAudio.kext/…/FWHALPlugin` loaded **into CueMix FX itself**, alongside Apple's own `AppleHDAHALPlugIn`. No MOTU plugin is in `coreaudiod` — these are old-style in-process `AudioHardwarePlugIn`s, and they still work |
| MOTU kexts will not load on Sequoia | **wrong** | `kextstat`: `com.motu.driver.FireWireAudio (1.6 b5003c51d)` is loaded |
| The PCI kext is unsigned or unnotarized | **wrong** | kext and plugin both carry `Developer ID Application: MOTU (KRCLLMGZ2D)` |
| CueMix FX never learned the run-loop trick | **wrong** | it does exactly what we do: `HardwarePnPListener::HardwarePnPListener()` calls `AudioHardwareSetProperty('rnlp', …)` then `AudioHardwareAddPropertyListener('dev#')`, and `HardwarePnPListener::GetAvailableDevices()` fetches `'Mapi'`. `src/common/motu_card.mm:276` already said so |
| Version skew: the 2025 console asks the 2017 plugin for the wrong Gestalt version | **not supported** | `GetAvailableDevices` has two `'Mapi'` fetches, one passing **10** and one passing **14**. The 2017 PCI plugin requires 14 (`cmpl $0xe`), and a version-14 path exists |

The remaining difference is narrow and real: MOTU's console reaches `'Mapi'`
through the **pre-10.6 CoreAudio API** — `AudioDeviceGetProperty` /
`AudioDeviceGetPropertyInfo` / `AudioHardwareSetProperty`, with no `AudioObject*`
import anywhere — while `motu_card.mm` uses the modern `AudioObjectGetPropertyData`
family. Both end at the same plugin. Whether the legacy path still delivers a
custom property to a **10.6-SDK** in-process plugin on Sequoia is the open
question, and it **cannot be settled without the card**: everything above was
measured against the FireWire plugin, which is a 2025 rebuild (`macosx15.4` SDK,
x86_64 + arm64), whereas the PCI plugin is frozen at `macosx10.6`, i386 + x86_64.

So the ten-minute test at the studio is precise: launch MOTU's CueMix FX with the
card present and find out whether `GetAvailableDevices` returns the PCI device at
all. If it does and the app still exits, the cause is downstream of discovery.

### Architecture constraint

`HALPlugin` is **i386 + x86_64, with no arm64 slice**, and it loads into the
client process. So the replacement apps can only ever be x86_64 — already pinned
in `CMakeLists.txt:15` — and MOTU's own arm64-capable CueMix FX could never
reach a PCI-424 on an Apple Silicon Mac. The PCI control machine has to stay
Intel.

### The Big Sur volume

Checked read-only, because it was mounted: **the Big Sur volume never had the PCI
driver at all.**

```
/Volumes/macOS Big Sur - Data/Library/Extensions
    MOTUFireWireAudio.kext      1.6 88494
    MOTUMicroBookAudio.kext
    (no MOTUPCIAudio.kext)
```

and its `com.motu.CueMixFX.plist` references exactly one engine —
`com_motu_driver_FWA_Engine:00000d2f76`, the same UltraLite mk3 Hybrid serial as
this Sequoia install. There is no PCI-424 state on that volume and never was.

So CueMix FX on Big Sur was driving the UltraLite, which is what it is doing on
Sequoia right now. **Big Sur offers this project nothing that Sequoia does not**,
and nothing about the PCI path can have been broken there, because the PCI driver
was never installed.

What did change on the Sequoia side: a **2025 MOTU package** was installed here,
taking `MOTUFireWireAudio.kext` and `CueMix FX` to `1.6 b5003c51d`. Big Sur still
has the matched `1.6 88494` pair. The 2025 package does **not** ship a PCI driver
— `MOTUPCIAudio.kext` here is still `1.6 73220` from 2017 — so MOTU has dropped
PCI from the current installer, and the version mismatch between a 2025 console
and a 2017 PCI plugin is a property of this machine's install, not a mistake.

## MOTU PCI Audio Setup

| | |
|---|---|
| Original | i386, 10.6 SDK, `LSRequiresCarbon=1`, Metrowerks PowerPlant |
| Bundle ID | `com.motu.pci.config.console` |
| Version | 1.5 (2003–2011) |
| Window title | **the device name — "PCI-424"**, set at runtime |
| Content size | **602 × 334** px, `noGrowDocProc` — fixed size, not resizable |

Both confirmed against `docs/reference/setup-main-*.png`: the window frame
measures 602 px wide and 356 px tall, less Mojave's 22 px title bar = 334.

"MOTU PCI Audio Console" is the *application* name in the menu bar, not the
window title. The window is titled after the card, and the File menu's two
blank `MENU 129` slots are where the device list goes — the screenshot shows
`PCI-424 ⌘1` sitting in them.

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
| 2025 copy | **arm64 + x86_64, 1.6 b5003c51d, not stripped, PCI back end present** — `CUEMIX-API.md` |
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

## Layout — captured

Static extraction could not give us layout, because both apps position every
control in code. That gap is now closed by photography rather than disassembly:
`docs/reference/` holds MOTU's originals running on the Mojave volume.

Verified from those shots, against the live card:

- The main window is one pane, not tabs: Sample Rate / Clock Source on the left
  of row 1–2 and Default Input / Default Output on the right, then a
  *Configure Interface* popup with an `Audiowire: N` readout and an
  *Enable Routing* checkbox, then one *Bank* popup per bank, then the
  Enable Input / Enable Output checkbox grid in pairs, then the *PCI Use* line,
  then *Interface Options…* / *Edit Channel Names…* / *Enable Volume Controls*.
- The grid reflows per interface: HD192 shows 6 pairs in 2 columns, a 24I/O
  shows 12 pairs in 3 columns, and the 2408mk3 shows 4 pairs under each of its
  three bank popups.
- The popup contents are all real card data and all reproduced by our wrapper —
  14 clock sources, 6 sample rates, the Default In/Out channel-pair lists.

### Interface Options panes

All three are now captured. Each is a small titled window (`HD192 Options`,
`24IO Options`, `2408mk3 Options`) ending in a `firmware X.Y hw X.Y` line.

**HD192** (`firmware 1.1 hw 1.1`), three groups divided by rules:

```
AES/EBU Input Options:   Steal Inputs   [None        ]
                         [ ] Rate Convert
AES/EBU Output Options:  Mirror Analog  [Out 1-2     ]
                         Output Clock   [System      ]
Meter Options:           Clip Time-out       [1 Minute ]
                         Peak/Hold Time-out  [2 Seconds]
```

Six controls for exactly the six selectors the HD192 implements. There is
**no output Rate Convert control**, although the old string list suggested one.
See `CHANNEL-STATE.md` for how the controls map to keys.

**24I/O** (`firmware 1.1 hw 1.0`): *Input Reference Level* in **three groups of
eight**: `Analog 1-8`, `9-16`, `17-24`, each with a `+4 dBu` / `-10 dBV` radio
pair. Below that, *Word Out Rate* `[Match system clock]`. Unlike the 2408mk3's
four pairs, it has no ADAT Mode row.

## CueMix FX — captured

The menu bar is `CueMix FX  File  Edit  Devices  Configurations  Talkback
Phones  Control Surfaces  Window`. Menu contents, as shown with a PCI-424
selected. Items in *italics* are greyed out:

| Menu | Items |
|---|---|
| CueMix FX | About CueMix FX… · Services ▸ · Hide CueMix FX ⌘H · Hide Others ⌥⌘H · *Show All* · Quit CueMix FX ⌘Q |
| File | *Save Hardware Preset… ⌥⌘S* · *Load Hardware Preset… ⌥⌘O* · — · Peak Hold Time ▸ · — · *Mix 1 Return Includes Computer Output* · ✓ Hardware Follows Console Stereo Settings · — · Close ⌘W |
| Edit | *Undo ⌘Z* · *Redo ⇧⌘Z* · — · Copy ⌘C · *Paste ⌘V* · — · Clear Peaks ⌘\ · (Start Dictation… is added by the system) |
| Devices | ✓ PCI-424 ⌘1 · FFT Analysis · Oscilloscope · X-Y Plot · Phase Analysis · Tuner |
| Configurations | Create New… ⌘N · *Save ⌘S* · *Save To… ⇧⌘S* · *Delete…* · — · Import… · *Export…* |
| Talkback | Configure Talkback/Listenback… ⇧⌘T · Toggle Talkback ⌘T · Toggle Listenback ⌘L |
| Phones | **opens to nothing** on a PCI-424 |
| Control Surfaces | Application Follows Control Surface · Share Surfaces with Other Applications · CueMix Control Surfaces ▸ (Enabled · Configure…) · — · Configure OSC Devices… |
| Window | Minimize ⌘M · *Zoom* · — · Bring All to Front · — · ✓ PCI-424 |

The PCI File menu is shorter than `LocalizableStrings.xml` suggests. *Save To
File…*, *Load From File…*, *Edit Channel Names…* and *Show Meter in Dock Icon*
are absent, and so is *Factory Defaults* from Configurations. Those items belong
to the FireWire/USB boxes.

Two menus could not be opened on Mojave and are recovered from the strings file
instead:

- **Peak Hold Time ▸** (submenu would not open): `Off`, `2 Seconds`,
  `4 Seconds`, `10 Seconds`, `1 Minute`, `5 Minutes`, `Infinite`. This is the
  order in the strings file. `com.motu.CueMixFX.plist` on Mojave holds
  `PeakHoldTime = 3`. The enum is 1-based over exactly that order, so 3 is
  **4 Seconds** — decoded from `ConvertPeakHoldTimeEnumToSeconds`, see
  `CUEMIX-API.md`. Infinite is carried as −1 seconds.
- **Phones**: the only strings are `Follow Active Mix` and the phones output
  names (`Phones 1-2`, `Phones Out 1/2`). PCI interfaces have no phones bus, so an
  empty menu is almost certainly correct behaviour, not a capture failure. The
  replica should keep the empty menu so the bar matches.

Windows and dialogs:

- **Configure Talkback/Listenback** (`cuemix-talkback-configure.png`) is a
  sheet over the console with a scrolling table (`Output` · `Bus` · `Talk` ·
  `Listen`), one row per output pair (`HD192:Analog/AES 1-2` → `Mix 1`, …),
  two checkboxes per row, and *Done*.
- **Create configuration** is a dark AwesomeLib-skinned panel, not a system
  sheet: *Configuration name:* field, `cancel` / `ok` in lower case.
- The two **Disabled ▾** popups in the Talkback/Listenback cluster list
  `Disabled` followed by every input as `Interface: Name`. The **Scope Channel
  Selection** Left/Right popups list the same inputs *without* the interface
  prefix. The talkback/listenback file names assume the left popup was captured
  first.
- **CueMix Control Surfaces ▸ Configure…** gives *Can't configure control
  surfaces / No compatible surfaces are installed. / Do you want to view the
  help file?* (No / Yes). There is also **MOTU CueMix Mackie Control Surface
  Settings**: *Edit settings for* popup, *Group*, *Group position*, Help… /
  Cancel / OK.
- **OSC Configuration**: headers *Application Follows Control Surface* and
  *Synchronize*, a lavender striped list (`PCI-424` / `port 64184` / `+`),
  and `HOST: <hostname> <any>` with `ok`.
- **Devices ▸ analysis windows**, each titled `PCI-424 - <name>` and resizable:
  FFT Analysis, Oscilloscope, X-Y Plot, Phase Analysis, and the fixed-size,
  fully skinned Tuner. All take their sources from Scope Channel Selection
  (`Console L` / `Console R`). They are audio-rate displays, so they need the
  scope audio path, not the level-meter API.

## Reference index

Everything in `docs/reference/`:

| file | what |
|---|---|
| `setup-main-hd192/24io-2/24io-3/2408mk3.png` | main window, one per interface |
| `setup-popup-*.png` | every popup's contents |
| `setup-options-hd192/24io/2408mk3.png` | the three Options panes |
| `setup-menu-file.png` | the File menu |
| `channel-names-top/scrolled.png` | `MOTU Channel Names` |
| `cuemix-console-full.png` | CueMix FX, all four interfaces, PCI skin |
| `cuemix-menubar.png`, `cuemix-menu-*.png`, `cuemix-submenu-*.png` | every CueMix menu except Phones and Peak Hold Time |
| `cuemix-popup-talkback/listenback-input.png`, `cuemix-popup-scope-left/right.png` | right-panel popups |
| `cuemix-talkback-configure.png`, `cuemix-dialog-create-configuration.png` | talkback sheet, config dialog |
| `cuemix-alert-no-control-surfaces.png`, `cuemix-mackie-settings.png`, `cuemix-osc-configuration.png` | control-surface windows |
| `cuemix-fft-analysis/oscilloscope/xy-plot/phase-analysis/tuner.png` | Devices-menu analysis windows |

Nothing further is needed from Mojave except the **HD192 Options popup lists**
(Steal Inputs, Mirror Analog, Output Clock, Clip / Peak/Hold Time-out). They
would settle the value encodings questioned in `CHANNEL-STATE.md`.
