# motu-pcie-424

Replacement control software for the **MOTU PCIe-424** card and its AudioWire
interfaces, for macOS versions where MOTU's own apps no longer work.

MOTU's last driver shipped in 2017. The card itself works perfectly on Sequoia —
the kext loads, CoreAudio sees it, audio I/O is fine. Only the control surface
is missing.

Both original apps draw their entire UI through **Carbon** (MOTU's in-house
`AwesomeLib` toolkit), so neither survives on modern macOS. `MOTU PCI Audio
Setup` 1.5 is i386 PowerPlant and cannot launch at all; `CueMix FX` starts and
exits immediately. See `docs/ORIGINAL-UI.md`.

The driver's CoreAudio HAL plugin still exposes MOTU's complete C++ control API.
This project talks to it directly.

## Status

Card access is **solved and verified on macOS Sequoia 15.7.4 with SIP fully
enabled**. `src/common/motu_card.*` wraps the whole API; `motu-dump` exercises it:

```
GetNumWires = 4    HD192 / 24I/O-2 / 24I/O-3 / 2408mk3
96 inputs (36 active), 96 outputs (36 active)
GetCueMixAPI / GetSMPTEAPI / GetTalkbackAPI   all non-NULL
14 clock sources enumerated, 6 sample rates (44.1k–192k)
```

UI work has not started.

## The key trick

`'Mapi'` returns NULL until you hand the CoreAudio HAL your own run loop:

```c
CFRunLoopRef rl = CFRunLoopGetCurrent();
AudioObjectPropertyAddress rlp = { 'rnlp',   // kAudioHardwarePropertyRunLoop
    kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
AudioObjectSetPropertyData(kAudioObjectSystemObject, &rlp, 0, NULL,
                           sizeof(CFRunLoopRef), &rl);
```

The plugin compares the run loop it registered with against
`CFRunLoopGetCurrent()` and returns NULL on mismatch. CoreAudio defaults to its
own internal notification thread, so a main-thread call can never match until
you set `'rnlp'`. See `docs/HALPLUGIN-API.md` for the disassembly and the full
131-method API map.

This is why **SIP can stay fully enabled** — unlike the gitflic project, which
asks you to disable it.

## Plan

Faithful separate replicas of MOTU's originals, not a merged tool.

| App | Framework | Status |
|---|---|---|
| CueMix FX | JUCE | console roughed in, live and read-only; staged plan in `docs/CUEMIX-PLAN.md` |
| MOTU PCI Audio Setup | JUCE | every control wired and stress-tested; not yet in daily use |
| MOTU PCI SMPTE Setup | — | shelved, unused |

C++ and JUCE for both. Neither original used a stock control, so there is no
native look to preserve; both are custom-drawn skins over the same card API, and
sharing one widget set, one build and one language is worth more than matching
whichever toolkit MOTU happened to use in 2011.

JUCE constraint: **never let `AudioDeviceManager` open a device.** These are
control surfaces, not audio clients, and whichever thread reaches the CoreAudio
HAL first wins the run-loop registration — if JUCE's audio thread gets there
first the card pointer stays NULL forever.

## Layout

```
docs/    HALPLUGIN-API.md  API map + how to reach it
         CHANNEL-STATE.md  channel state, options, commit semantics (from MOTU's own console)
         ORIGINAL-UI.md    what MOTU's apps are, and what was captured from Mojave
         CUEMIX-PLAN.md    CueMix FX, stage by stage
         reference/        screenshots of MOTU's originals running on Mojave
src/common/               shared card access (motu_card.h/.mm), prefs reader, motu-dump/probe
src/motu-spy/             MotuSpy (runs on Mojave: snapshot + diff card state) and bounds probe
src/cuemix-fx/            JUCE app
src/pci-audio-setup/      JUCE app
third_party/JUCE          submodule, kcoul/JUCE
tools/   build.sh, coreaudio-trace.c, vtdump.py, entitlements.plist
```

## Building

```sh
git submodule update --init --recursive
tools/build.sh MotuDump src/common/motu_card.mm src/common/motu_dump.mm --run
```

Must be a real `.app` bundle running `NSApplication` and signed with
`com.apple.security.device.audio-input` — a bare CLI never gets the card
pointer. `tools/build.sh` handles bundling and ad-hoc signing.

JUCE builds need CMake, which is **not currently installed** (`brew install cmake`).

## Requirements

- MOTU `MOTUPCIAudio.kext` 1.6 73220 installed in `/Library/Extensions`
- SIP can stay **enabled**; no boot-args or `csr-active-config` changes needed

## Related work

- `github.com/ruslan-kherson/PCI-424` / gitflic.ru — a Swift/AppKit
  reimplementation of PCI Audio Setup by another author, tested only against a
  24I/O, and requiring SIP to be partially disabled. Useful for cross-checking
  API semantics.
