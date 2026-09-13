# MOTU PCI-424 HALPlugin — C++ API map

Extracted from `/Library/Extensions/MOTUPCIAudio.kext/Contents/PlugIns/HALPlugin.bundle/Contents/MacOS/HALPlugin`
(MOTU 1.6 73220, x86_64 slice, 583 symbols, full C++ names retained).

## How to get the root object  (THE CRITICAL PART)

You MUST first hand the CoreAudio HAL your own run loop, or `'Mapi'` returns
NULL forever. This is the single thing that blocks every naive attempt:

```c
// 1. Give the HAL *this* thread's run loop (main thread).
CFRunLoopRef rl = CFRunLoopGetCurrent();
AudioObjectPropertyAddress rlp = { 'rnlp',            // kAudioHardwarePropertyRunLoop
    kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
AudioObjectSetPropertyData(kAudioObjectSystemObject, &rlp, 0, NULL,
                           sizeof(CFRunLoopRef), &rl);

// 2. Find the device by name, then ask for the card object.
AudioObjectPropertyAddress m = { 'Mapi',
    kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
unsigned char buf[8]; memset(buf,0,8); *(UInt32*)buf = 0x0E;   // version 14
UInt32 sz = 8;
AudioObjectGetPropertyData(dev, &m, 0, NULL, &sz, buf);
void* card = *(void**)buf;      // AudioWireCard*
```

### Why the run loop matters

Disassembly of the `'Mapi'` handler (HALPlugin x86_64 @ 0x5440):

```
cmpl  $0x4d617069, %ebx     ; 'Mapi'?
callq 0x2750                ; PlugIn::fgInstance  (lazy singleton, 0xC0 bytes)
movq  0x20(%rax), %r13      ; the run loop the plugin registered with
callq _CFRunLoopGetCurrent  ; the run loop you are calling from
cmpq  %rax, %r13
je    0x5470                ; MATCH -> continue
movq  $0x0, (%r14)          ; MISMATCH -> return NULL
cmpl  $0xe, (%r14)          ; and the version must be 14 (== engine "Gestalt")
```

By default CoreAudio services HAL notifications on its own internal thread, so
the plugin stores *that* run loop and no call from your main thread can ever
match. Setting `'rnlp'` re-points it (the plugin migrates its
`CFRunLoopRemoveSource`/`AddSource` registrations and re-retains the new one).

**Call `'Mapi'` from the same thread whose run loop you set.**

### Other requirements

- Must be a real `.app` bundle running `NSApplication`; a bare CLI never works.
- Needs `com.apple.security.device.audio-input` in its entitlements.
- Works with **SIP fully enabled** (`csr-active-config = 00000000`). Partial SIP
  disable is NOT required, contrary to the gitflic README.
- Do **not** call AVFoundation (e.g. `AVCaptureDevice requestAccess`) before
  setting the run loop; it spins up audio on another thread first.
- Bundle ID, `CFBundleSignature`, code-signing identity and install path are all
  irrelevant — each was tested and ruled out.

Call convention: `@convention(c) (this, MOTUException* exc, args...)`.
The exception buffer is 144 bytes, zeroed, 8-aligned.

Three of these entry points have argument semantics you cannot guess from the
signature — `GetInputState`, `GetOutputState` and `OtherInterfaceOp`, the last
of which takes its bool as **isGet, not isSet**, and silently writes if you get
it backwards. All three are decoded, with the disassembly, in
`docs/CHANNEL-STATE.md`.

Verified working output on this machine (4 interfaces, all three sub-APIs live):

```
GetNumWires = 4  ->  HD192 / 24I/O-2 / 24I/O-3 / 2408mk3
GetNumInputs = 96 (36 active)   GetSMUXOptionCapable = 1
GetCueMixAPI / GetSMPTEAPI / GetTalkbackAPI  all non-NULL
GetCueMixResourceUsage = 0 used, 180 max
```

Reference implementation: `motu-card-access.mm` (build with the CLT toolchain;
see README). CoreAudio call tracer: `coreaudio-trace.c`.

## AudioWireCardImpl

```
  [ 0] AudioWireCardImpl::~AudioWireCardImpl()
  [ 1] AudioWireCardImpl::~AudioWireCardImpl()
  [ 2] AudioWireCardImpl::SetClient(AudioWire::AudioWireCard::Client*)
  [ 3] AudioWireCardImpl::CommitChanges(MOTUException*, bool)
  [ 4] AudioWireCardImpl::CardGestalt(MOTUException*, int)
  [ 5] AudioWireCardImpl::GetCueMixAPI(MOTUException*)
  [ 6] AudioWireCardImpl::GetSMPTEAPI(MOTUException*)
  [ 7] AudioWireCardImpl::GetTalkbackAPI(MOTUException*)
  [ 8] AudioWireCardImpl::CopyConfiguration(MOTUException*)
  [ 9] AudioWireCardImpl::SetConfiguration(MOTUException*, void const*)
  [10] AudioWireCardImpl::FlushPrefs(MOTUException*)
  [11] AudioWireCardImpl::GetNumWires(MOTUException*)
  [12] AudioWireCardImpl::IsWireConnected(MOTUException*, int)
  [13] AudioWireCardImpl::GetWireInterface(MOTUException*, int)
  [14] AudioWireCardImpl::GetWireInterfaceName(MOTUException*, int, char*, int)
  [15] AudioWireCardImpl::ProbeForInterfaces(MOTUException*)
  [16] AudioWireCardImpl::GetNumInputs(MOTUException*)
  [17] AudioWireCardImpl::GetNumActiveInputs(MOTUException*)
  [18] AudioWireCardImpl::GetNthActiveInputID(MOTUException*, int)
  [19] AudioWireCardImpl::GetInputDescription(MOTUException*, int, char*, int)
  [20] AudioWireCardImpl::GetInputState(MOTUException*, int, unsigned char*, unsigned char*)
  [21] AudioWireCardImpl::SetInputEnable(MOTUException*, int, bool)
  [22] AudioWireCardImpl::GetNumOutputs(MOTUException*)
  [23] AudioWireCardImpl::GetNumActiveOutputs(MOTUException*)
  [24] AudioWireCardImpl::GetNthActiveOutputID(MOTUException*, int)
  [25] AudioWireCardImpl::GetOutputDescription(MOTUException*, int, char*, int)
  [26] AudioWireCardImpl::GetOutputState(MOTUException*, int, unsigned char*, int*)
  [27] AudioWireCardImpl::SetOutputSource(MOTUException*, int, int)
  [28] AudioWireCardImpl::GetBankRelativeID(MOTUException*, int)
  [29] AudioWireCardImpl::GetChannelNameCFString(MOTUException*, int, bool, __CFString const**)
  [30] AudioWireCardImpl::SetCustomChannelNameCFString(MOTUException*, int, bool, __CFString const*)
  [31] AudioWireCardImpl::GetCardType(MOTUException*, unsigned int*)
  [32] AudioWireCardImpl::GetSMUXOptionSetting(MOTUException*)
  [33] AudioWireCardImpl::SetSMUXOptionSetting(MOTUException*, int)
  [34] AudioWireCardImpl::GetSMUXOptionCapable(MOTUException*)


```
## AudioWireInterfaceImpl

```
  [ 0] AudioWireInterfaceImpl::~AudioWireInterfaceImpl()
  [ 1] AudioWireInterfaceImpl::~AudioWireInterfaceImpl()
  [ 2] AudioWireInterfaceImpl::GetInterfaceID(MOTUException*)
  [ 3] AudioWireInterfaceImpl::GetInterfaceName(MOTUException*, char*, int)
  [ 4] AudioWireInterfaceImpl::GetInterfaceVersionString(MOTUException*, char*, int)
  [ 5] AudioWireInterfaceImpl::OtherInterfaceOp(MOTUException*, bool, int, int&)
  [ 6] AudioWireInterfaceImpl::GetNumberOfBanks(MOTUException*)
  [ 7] AudioWireInterfaceImpl::GetNumberOfChansInBank(MOTUException*, int)
  [ 8] AudioWireInterfaceImpl::GetNthBankPersonality(MOTUException*, int, int, char*)
  [ 9] AudioWireInterfaceImpl::SetPersonalityForBank(MOTUException*, int, int)
  [10] AudioWireInterfaceImpl::GetPersonalityForBank(MOTUException*, int)
  [11] AudioWireInterfaceImpl::IsInputChannelAvailable(MOTUException*, int)
  [12] AudioWireInterfaceImpl::IsOutputChannelAvailable(MOTUException*, int)


```
## CueMixAPIImpl

```
  [ 0] CueMixAPIImpl::~CueMixAPIImpl()
  [ 1] CueMixAPIImpl::~CueMixAPIImpl()
  [ 2] CueMixAPIImpl::GetInputMute(MOTUException*, int)
  [ 3] CueMixAPIImpl::SetInputMute(MOTUException*, int, bool)
  [ 4] CueMixAPIImpl::GetInputTrim(MOTUException*, int)
  [ 5] CueMixAPIImpl::SetInputTrim(MOTUException*, int, int)
  [ 6] CueMixAPIImpl::GetCueMixSolo(MOTUException*, int, int)
  [ 7] CueMixAPIImpl::GetCueMixMute(MOTUException*, int, int)
  [ 8] CueMixAPIImpl::GetCueMixVolume(MOTUException*, int, int)
  [ 9] CueMixAPIImpl::GetCueMixPan(MOTUException*, int, int)
  [10] CueMixAPIImpl::DoesCueMixFaderHaveResources(MOTUException*, int, int)
  [11] CueMixAPIImpl::GetCueMixBusMute(MOTUException*, int)
  [12] CueMixAPIImpl::GetCueMixBusVolume(MOTUException*, int)
  [13] CueMixAPIImpl::IsCueMixBusSoloed(MOTUException*, int)
  [14] CueMixAPIImpl::GetCueMixBusResourceUsage(MOTUException*, int)
  [15] CueMixAPIImpl::SetCueMixSolo(MOTUException*, int, int, bool)
  [16] CueMixAPIImpl::SetCueMixMute(MOTUException*, int, int, bool)
  [17] CueMixAPIImpl::SetCueMixVolume(MOTUException*, int, int, int)
  [18] CueMixAPIImpl::SetCueMixPan(MOTUException*, int, int, int)
  [19] CueMixAPIImpl::SetCueMixBusMute(MOTUException*, int, bool)
  [20] CueMixAPIImpl::SetCueMixBusVolume(MOTUException*, int, int)
  [21] CueMixAPIImpl::ReadLevelMeters(MOTUException*, AudioWire::AudioWireLevelMeterRequest const*, AudioWire::AudioWireLevelMeterResults*)
  [22] CueMixAPIImpl::GetCueMixResourceUsage(MOTUException*, int*, int*, int*)
  [23] CueMixAPIImpl::GetPCIUsage(MOTUException*, int*, int*)
  [24] CueMixAPIImpl::SetCueMixInputBalance(MOTUException*, int, int, unsigned char)
  [25] CueMixAPIImpl::SetCueMixInputWidth(MOTUException*, int, int, unsigned char)
  [26] CueMixAPIImpl::SetCueMixInputBalanceWidthPref(MOTUException*, int, int, bool)
  [27] CueMixAPIImpl::SetCueMixInputChannelMapping(MOTUException*, int, int)
  [28] CueMixAPIImpl::GetCueMixInputBalance(MOTUException*, int, int)
  [29] CueMixAPIImpl::GetCueMixInputWidth(MOTUException*, int, int)
  [30] CueMixAPIImpl::GetCueMixInputBalanceWidthPref(MOTUException*, int, int)
  [31] CueMixAPIImpl::GetCueMixInputChannelMapping(MOTUException*, int)
  [32] __mh_bundle_header+0x10
  [33] typeinfo name for CueMixAPIImpl
  [34] typeinfo for AudioWire::CueMixAPI


```
## SMPTEAPIImpl

```
  [ 0] SMPTEAPIImpl::~SMPTEAPIImpl()
  [ 1] SMPTEAPIImpl::~SMPTEAPIImpl()
  [ 2] SMPTEAPIImpl::SetFrameRate(MOTUException*, AudioWire::MOTUCoreAudio::FrameRate, AudioWire::MOTUCoreAudio::FrameFormat, char*, int)
  [ 3] SMPTEAPIImpl::GetFrameRate(MOTUException*)
  [ 4] SMPTEAPIImpl::GetFrameFormat(MOTUException*)
  [ 5] SMPTEAPIImpl::SetFreewheelTimes(MOTUException*, int, int)
  [ 6] SMPTEAPIImpl::GetFreewheelAddressTime(MOTUException*)
  [ 7] SMPTEAPIImpl::GetFreewheelClockTime(MOTUException*)
  [ 8] SMPTEAPIImpl::GetNthSMPTESource(MOTUException*, int)
  [ 9] SMPTEAPIImpl::GetSMPTESourceInfo(MOTUException*, int, char*, int)
  [10] SMPTEAPIImpl::SetSMPTESource(MOTUException*, int)
  [11] SMPTEAPIImpl::GetSMPTESource(MOTUException*)
  [12] SMPTEAPIImpl::SetSMPTEGenerationMode(MOTUException*, AudioWire::SMPTEGeneratorMode)
  [13] SMPTEAPIImpl::GetSMPTEGenerationMode(MOTUException*)
  [14] SMPTEAPIImpl::StartGeneratingSMPTE(MOTUException*, unsigned long long)
  [15] SMPTEAPIImpl::StopGeneratingSMPTE(MOTUException*)
  [16] SMPTEAPIImpl::GetNthSMPTEDestination(MOTUException*, int)
  [17] SMPTEAPIImpl::GetSMPTEDestinationInfo(MOTUException*, int, unsigned char*, char*, int)
  [18] SMPTEAPIImpl::SetSMPTEDestination(MOTUException*, int)
  [19] SMPTEAPIImpl::GetSMPTEDestination(MOTUException*)
  [20] SMPTEAPIImpl::SetSMPTEOutputLevel(MOTUException*, int)
  [21] SMPTEAPIImpl::GetSMPTEOutputLevel(MOTUException*)
  [22] __mh_bundle_header+0x10
  [23] typeinfo name for SMPTEAPIImpl
  [24] typeinfo for AudioWire::SMPTEAPI


```
## TalkbackAPIImpl

```
  [ 0] TalkbackAPIImpl::~TalkbackAPIImpl()
  [ 1] TalkbackAPIImpl::~TalkbackAPIImpl()
  [ 2] TalkbackAPIImpl::SetTalkbackInput(MOTUException*, int)
  [ 3] TalkbackAPIImpl::SetListenbackInput(MOTUException*, int)
  [ 4] TalkbackAPIImpl::SetTalkbackDimLevel(MOTUException*, int)
  [ 5] TalkbackAPIImpl::SetListenbackDimLevel(MOTUException*, int)
  [ 6] TalkbackAPIImpl::SetTalkbackOutput(MOTUException*, int, int)
  [ 7] TalkbackAPIImpl::SetListenbackOutput(MOTUException*, int, int)
  [ 8] TalkbackAPIImpl::GetTalkbackInput(MOTUException*)
  [ 9] TalkbackAPIImpl::GetListenbackInput(MOTUException*)
  [10] TalkbackAPIImpl::GetTalkbackDimLevel(MOTUException*)
  [11] TalkbackAPIImpl::GetListenbackDimLevel(MOTUException*)
  [12] TalkbackAPIImpl::GetTalkbackOutput(MOTUException*, int)
  [13] TalkbackAPIImpl::GetListenbackOutput(MOTUException*, int)
  [14] TalkbackAPIImpl::GetTalkbackEnable(MOTUException*)
  [15] TalkbackAPIImpl::GetListenbackEnable(MOTUException*)
  [16] TalkbackAPIImpl::GetTalkbackLink(MOTUException*)
  [17] TalkbackAPIImpl::SetTalkbackEnable(MOTUException*, int)
  [18] TalkbackAPIImpl::SetListenbackEnable(MOTUException*, int)
  [19] TalkbackAPIImpl::SetTalkbackLink(MOTUException*, int)
  [20] __mh_bundle_header+0x10
  [21] typeinfo name for TalkbackAPIImpl
  [22] typeinfo for AudioWire::TalkbackAPI
```
