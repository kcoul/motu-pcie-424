// MOTU PCIe-424 control API — C++ wrapper over the driver's CoreAudio HAL plugin.
//
// MOTUPCIAudio.kext ships a CoreAudio HAL plugin that still exports MOTU's full
// C++ control API. There are no headers; every entry point is reached by vtable
// slot. docs/HALPLUGIN-API.md has the complete map and the disassembly that
// pins the calling convention:
//
//     result method(this, MOTUException* exc, args...)
//
// THREADING (the whole reason this is hard): the plugin stores the run loop it
// was registered with and compares it against CFRunLoopGetCurrent() on every
// 'Mapi' fetch, handing back NULL on a mismatch. CoreAudio defaults to its own
// internal notification thread, so you must call Card::installRunLoop() from
// your UI thread *before anything else in the process touches CoreAudio*, and
// then make every call below from that same thread.
//
// In particular, do not let JUCE's AudioDeviceManager, AVFoundation, or any
// other audio machinery start first — whichever thread reaches the HAL first
// wins the registration and the card pointer stays NULL forever.
//
// RANGE CHECKING IS THE CALLER'S JOB. The driver validates some arguments and
// not others, and the difference is not predictable from the signature:
//
//   GetInputDescription(9999)   raises  (kind=3 code=4 AudioWireCardImpl.cpp:470)
//   GetOutputDescription(9999)  raises  (kind=2 code=4 AudioWireCardImpl.cpp:573)
//   IsWireConnected(99)         returns silent garbage, no exception
//   GetWireInterfaceName(99)    SEGFAULTS the whole process
//
// Never pass an index you did not get from numWires() / nthActiveInputID() /
// nthActiveOutputID().

#pragma once

#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>

#include <string>
#include <vector>

namespace motu {

// MOTU's exception record: a 144-byte buffer the caller supplies, which the
// plugin fills in when a call fails. Undocumented, but recovered by probing
// (see tools' MOTUException section and docs/HALPLUGIN-API.md):
//
//     0x00  int32   kind       error domain
//     0x04  int32   code       domain-specific code
//     0x08  int32   line       source line inside the driver
//     0x0c  char[]  file       NUL-terminated source file, e.g. "AudioWireCardImpl.cp"
//
// A successful call leaves the buffer untouched, so `raised()` is just "did the
// plugin write anything".
struct Exception {
    static constexpr int kSize = 144;
    alignas(8) unsigned char raw[kSize];

    Exception() { reset(); }
    void reset();

    bool raised() const;
    int  kind() const;
    int  code() const;
    int  line() const;
    std::string file() const;

    // "kind=3 code=4 at AudioWireCardImpl.cp:470", or "ok".
    std::string str() const;
    std::string hex() const;   // first 32 bytes, for decoding new fields
};

// ---------------------------------------------------------------------------

class CueMix {
public:
    explicit CueMix(void* p = nullptr) : p_(p) {}
    explicit operator bool() const { return p_ != nullptr; }
    void* raw() const { return p_; }

    // Per-input strip (not per bus).
    bool inputMute(Exception&, int ch) const;
    void setInputMute(Exception&, int ch, bool);
    int  inputTrim(Exception&, int ch) const;
    void setInputTrim(Exception&, int ch, int);

    // Per (bus, channel) fader.
    bool solo(Exception&, int bus, int ch) const;
    bool mute(Exception&, int bus, int ch) const;
    int  volume(Exception&, int bus, int ch) const;
    int  pan(Exception&, int bus, int ch) const;
    void setSolo(Exception&, int bus, int ch, bool);
    void setMute(Exception&, int bus, int ch, bool);
    void setVolume(Exception&, int bus, int ch, int);
    void setPan(Exception&, int bus, int ch, int);
    bool faderHasResources(Exception&, int bus, int ch) const;

    // Per bus.
    bool busMute(Exception&, int bus) const;
    int  busVolume(Exception&, int bus) const;
    bool busSoloed(Exception&, int bus) const;
    int  busResourceUsage(Exception&, int bus) const;
    void setBusMute(Exception&, int bus, bool);
    void setBusVolume(Exception&, int bus, int);

    // Fader budget. `used` and `max` line up with the driver's CueMixFaders and
    // MaxFaders in ioreg; the middle value is unidentified (observed 22 on a
    // 4-interface / 36-active-input rig).
    struct Resources { int used = -1, unidentified = -1, max = -1; };
    Resources resources(Exception&) const;

    struct PCIUsage { int a = -1, b = -1; };
    PCIUsage pciUsage(Exception&) const;   // observed -1,-1 on PCIe-424

private:
    void* p_;
};

// ---------------------------------------------------------------------------

class SMPTE {
public:
    explicit SMPTE(void* p = nullptr) : p_(p) {}
    explicit operator bool() const { return p_ != nullptr; }
    void* raw() const { return p_; }

    int  frameRate(Exception&) const;
    int  frameFormat(Exception&) const;
    int  freewheelAddressTime(Exception&) const;
    int  freewheelClockTime(Exception&) const;
    void setFreewheelTimes(Exception&, int address, int clock);

    int         source(Exception&) const;
    void        setSource(Exception&, int);
    int         nthSource(Exception&, int n) const;
    std::string sourceInfo(Exception&, int id) const;

    int         destination(Exception&) const;
    void        setDestination(Exception&, int);
    int         nthDestination(Exception&, int n) const;

    int  generationMode(Exception&) const;
    void setGenerationMode(Exception&, int);
    int  outputLevel(Exception&) const;
    void setOutputLevel(Exception&, int);

private:
    void* p_;
};

// ---------------------------------------------------------------------------

class Talkback {
public:
    explicit Talkback(void* p = nullptr) : p_(p) {}
    explicit operator bool() const { return p_ != nullptr; }
    void* raw() const { return p_; }

    int  talkbackInput(Exception&) const;
    int  listenbackInput(Exception&) const;
    int  talkbackDimLevel(Exception&) const;
    int  listenbackDimLevel(Exception&) const;
    int  talkbackOutput(Exception&, int n) const;
    int  listenbackOutput(Exception&, int n) const;
    int  talkbackEnable(Exception&) const;
    int  listenbackEnable(Exception&) const;
    int  talkbackLink(Exception&) const;

    void setTalkbackInput(Exception&, int);
    void setListenbackInput(Exception&, int);
    void setTalkbackDimLevel(Exception&, int);
    void setListenbackDimLevel(Exception&, int);
    void setTalkbackOutput(Exception&, int n, int);
    void setListenbackOutput(Exception&, int n, int);
    void setTalkbackEnable(Exception&, int);
    void setListenbackEnable(Exception&, int);
    void setTalkbackLink(Exception&, int);

private:
    void* p_;
};

// ---------------------------------------------------------------------------

// One AudioWire interface (HD192, 24I/O, 2408mk3, ...).
class Interface {
public:
    explicit Interface(void* p = nullptr) : p_(p) {}
    explicit operator bool() const { return p_ != nullptr; }

    int         id(Exception&) const;
    std::string name(Exception&) const;
    std::string versionString(Exception&) const;
    int         numBanks(Exception&) const;
    int         numChannelsInBank(Exception&, int bank) const;
    std::string nthBankPersonality(Exception&, int bank, int n) const;
    int         personalityForBank(Exception&, int bank) const;
    void        setPersonalityForBank(Exception&, int bank, int personality);
    bool        inputChannelAvailable(Exception&, int ch) const;
    bool        outputChannelAvailable(Exception&, int ch) const;

private:
    void* p_;
};

// ---------------------------------------------------------------------------

class Card {
public:
    // Hand the CoreAudio HAL this thread's run loop. Call once, on the UI
    // thread, before any other CoreAudio use in the process. Returns false and
    // fills `err` if the HAL rejects it.
    static bool installRunLoop(std::string* err = nullptr);

    // Locate the PCI-424 CoreAudio device. 0 if absent.
    static AudioDeviceID findDevice();

    // Fetch the AudioWireCard pointer via 'Mapi'. Retries for `timeoutSeconds`
    // because the plugin can need a moment after the listeners are installed.
    // Must run on the installRunLoop() thread.
    static Card open(AudioDeviceID, std::string* err = nullptr,
                     double timeoutSeconds = 10.0);

    Card() = default;
    explicit operator bool() const { return p_ != nullptr; }
    void* raw() const { return p_; }

    int         gestalt(Exception&, int selector) const;
    void        commitChanges(Exception&, bool);
    void        flushPrefs(Exception&);
    void        probeForInterfaces(Exception&);

    int         numWires(Exception&) const;
    bool        wireConnected(Exception&, int wire) const;
    std::string wireInterfaceName(Exception&, int wire) const;
    Interface   wireInterface(Exception&, int wire) const;

    int         numInputs(Exception&) const;
    int         numActiveInputs(Exception&) const;
    int         nthActiveInputID(Exception&, int n) const;
    std::string inputDescription(Exception&, int id) const;
    void        setInputEnable(Exception&, int id, bool);

    int         numOutputs(Exception&) const;
    int         numActiveOutputs(Exception&) const;
    int         nthActiveOutputID(Exception&, int n) const;
    std::string outputDescription(Exception&, int id) const;
    void        setOutputSource(Exception&, int id, int source);

    int         bankRelativeID(Exception&, int id) const;
    std::string channelName(Exception&, int id, bool isInput) const;
    void        setChannelName(Exception&, int id, bool isInput, const std::string&);

    unsigned    cardType(Exception&) const;
    int         smuxCapable(Exception&) const;
    int         smuxSetting(Exception&) const;
    void        setSmuxSetting(Exception&, int);

    CueMix      cueMix(Exception&) const;
    SMPTE       smpte(Exception&) const;
    Talkback    talkback(Exception&) const;

private:
    explicit Card(void* p) : p_(p) {}
    void* p_ = nullptr;
};

// ---------------------------------------------------------------------------
// Settings that live on the CoreAudio device rather than in the MOTU API.
// MOTU PCI Audio Setup's "Sample Rate" and "Clock Source" popups are these.

namespace device {

double              sampleRate(AudioDeviceID);
bool                setSampleRate(AudioDeviceID, double);
std::vector<double> availableSampleRates(AudioDeviceID);

UInt32                   clockSource(AudioDeviceID);
bool                     setClockSource(AudioDeviceID, UInt32);
std::vector<UInt32>      clockSources(AudioDeviceID);
std::string              clockSourceName(AudioDeviceID, UInt32);

}  // namespace device
}  // namespace motu
