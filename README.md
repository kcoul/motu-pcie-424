# motu-pcie-424

Replacement control software for the **MOTU PCIe-424** card and its AudioWire
interfaces, for macOS versions where MOTU's own apps no longer work.

MOTU's last driver shipped in 2017. On Sequoia the original control apps are
dead ends: `MOTU PCI Audio Setup` and `MOTU PCI SMPTE Setup` are 32-bit i386 and
cannot launch at all, and `CueMix FX` (both 1.6 83634 and 1.6 88494) starts and
exits 0 immediately. The card itself works perfectly — the kext loads, CoreAudio
sees it, audio I/O is fine. Only the control surface is missing.

The driver's CoreAudio HAL plugin still exposes MOTU's complete C++ control API.
This project talks to it directly.

## Status

Card access is **solved and verified on macOS Sequoia with SIP fully enabled**:

```
GetNumWires = 4
  wire 0 HD192   wire 1 24I/O-2   wire 2 24I/O-3   wire 3 2408mk3
GetNumInputs = 96 (36 active)     GetSMUXOptionCapable = 1
GetCueMixAPI / GetSMPTEAPI / GetTalkbackAPI   all non-NULL
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

## Plan

Faithful separate replicas of MOTU's originals, not a merged tool.

| App | Framework | Status |
|---|---|---|
| CueMix FX | JUCE (`kcoul/JUCE`, CMake) | not started |
| MOTU PCI Audio Setup | ObjC++ / AppKit | not started |
| MOTU PCI SMPTE Setup | — | shelved, unused |

C++ only. JUCE for CueMix FX because the original is fully skinned with custom
channel strips and meters; AppKit for PCI Audio Setup because the original is
stock AppKit and should look native.

## Layout

```
docs/    HALPLUGIN-API.md — API map + how to reach it
src/common/              shared card-access code
src/cuemix-fx/           JUCE app
src/pci-audio-setup/     ObjC++/AppKit app
tools/   build.sh, coreaudio-trace.c, vtdump.py, entitlements.plist
```

## Building

```sh
tools/build.sh src/common/motu-card-access.mm MotuCardAccess --run
```

Must be a real `.app` bundle running `NSApplication` and signed with
`com.apple.security.device.audio-input` — a bare CLI never gets the card
pointer. `tools/build.sh` handles bundling and ad-hoc signing.

## Requirements

- MOTU `MOTUPCIAudio.kext` 1.6 73220 installed in `/Library/Extensions`
- SIP can stay **enabled**; no boot-args or `csr-active-config` changes needed
