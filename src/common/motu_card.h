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

#include <cstdint>
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

    // Stereo input balance/width (slots 24-31). Balance and width are bytes;
    // the preference picks which knob the PAN control drives (BAL / WIDTH).
    int  inputBalance(Exception&, int bus, int ch) const;
    int  inputWidth(Exception&, int bus, int ch) const;
    int  inputBalanceWidthPref(Exception&, int bus, int ch) const;
    int  inputChannelMapping(Exception&, int ch) const;

    // Fader budget. `used` and `max` line up with the driver's CueMixFaders and
    // MaxFaders in ioreg; the middle value is unidentified (observed 22 on a
    // 4-interface / 36-active-input rig).
    struct Resources { int used = -1, unidentified = -1, max = -1; };
    Resources resources(Exception&) const;

    struct PCIUsage { int a = -1, b = -1; };
    PCIUsage pciUsage(Exception&) const;   // observed -1,-1 on PCIe-424

    // Level meters (slot 21). ReadLevelMeters is the one call whose argument
    // shapes are not guessable from the signature, and MOTU's own wrapper
    // (CoreDeviceAW::AWReadLevelMeters) is a bare pass-through -- so these
    // layouts were recovered from its *caller*, CoreDeviceAW::UpdateLevelMeters,
    // in MOTU's 2025 CueMix FX. Derivation in docs/CUEMIX-API.md.
    //
    // NOT YET CONFIRMED AGAINST A CARD. If the first read comes back with
    // nonsense, suspect these before anything else.
    //
    // `bus` is a raw bus id, i.e. mix index * 2 -- MOTU doubles a 0..47 mix
    // index on the way in, exactly as for every other CueMix call.
    //
    // `channels` holds card-wide input ids, and MOTU sends only the strips
    // currently on screen, capped at Card::maxLevelMeters(). Reads are bounded
    // by min(numChannels, maxLevelMeters), so asking for more is pointless.
    static constexpr int kMaxMeters = 48;

    struct LevelMeterRequest {
        std::uint32_t bus = 0;                         // +0x00
        std::uint32_t numChannels = 0;                 // +0x04
        std::uint32_t channels[kMaxMeters] = {};       // +0x08
    };

    struct LevelMeterResults {
        std::int32_t level[kMaxMeters] = {};   // +0x000 linear, full scale 32768
        std::int32_t clip [kMaxMeters] = {};   // +0x0C0 0 = none; 1 and 2 both
                                               //        seen, which is which is
                                               //        unknown without signal
        std::uint8_t tail[0x28] = {};          // +0x180 MOTU zeroes through
                                               //        0x1A0 and never reads
                                               //        it back; unidentified
    };

    void readLevelMeters(Exception&, const LevelMeterRequest&,
                         LevelMeterResults*) const;

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
    // `ch` is relative to the wire (0-23), not a card-wide id.
    bool        inputChannelAvailable(Exception&, int ch) const;
    bool        outputChannelAvailable(Exception&, int ch) const;

    // Every per-interface "Options" pane control goes through MOTU's single
    // OtherInterfaceOp(exc, bool, int selector, int&). Selectors are 0..8; the
    // key each one names is recovered from the driver's jump table:
    enum class Option {
        AnalogMirror     = 0,   // 2408mk3: bank mirrored on the analog outs
        AESOutputSRCMode = 1,   // HD192: "Mirror Analog" (by elimination; see CHANNEL-STATE.md)
        AESInputSteal    = 2,   // HD192: steal inputs
        AESOutputClock   = 3,   // HD192: AES/EBU "Output Clock"
        AESInputSRC      = 4,   // HD192: AES/EBU input rate convert
        PeakHoldTime     = 5,   // HD192 meter time-out; Clip vs Peak/Hold may be crossed
        ClipHoldTime     = 6,   // HD192 meter time-out; see PeakHoldTime
        InputLevels      = 7,   // reference level bitfield, one bit per row, set = -10 dBV
        WordOutRange     = 8,   // word out rate
    };

    // NOTE the flag: MOTU's bool is `isGet`, NOT `isSet`. Passing it wrong does
    // not fail — it silently takes the *write* path and stuffs whatever is in
    // `value` into the pending-changes dictionary. Hence two methods and no
    // exposed bool. Disassembly in docs/HALPLUGIN-API.md.
    //
    // getOption raises "Couldn't find property in OtherInterfaceOp" for a
    // selector this interface does not implement, which is how you enumerate
    // which Options controls an interface should show.
    bool        getOption(Exception&, Option, int& value) const;
    void        setOption(Exception&, Option, int value) const;

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

    // Bit `selector` (0-3) of the card's feature flags. 0 = output cells can be
    // routed from an input (source popups); 0 on a PCI-424.
    int         gestalt(Exception&, int selector) const;
    void        commitChanges(Exception&, bool);
    void        flushPrefs(Exception&);

    // File > Save / Load Configuration. The configuration is the driver's
    // "Configuration" registry property, saved as an XML plist (.mcfg).
    // Load = SetConfiguration + CommitChanges(true), exactly as MOTU's console.
    std::vector<unsigned char> saveConfiguration(Exception&) const;
    bool                       loadConfiguration(Exception&, const std::vector<unsigned char>& xml);
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

    // GetInputState hands back two bytes. Their meaning was pinned by
    // disassembling MOTU's own console (docs/CHANNEL-STATE.md):
    //
    //   exists   the channel is populated. 0 for the ids an interface does not
    //            fill (an HD192 occupies only 12 of its 24 id slots).
    //   enabled  the "Enable Input" checkbox. The driver stores it per *pair*,
    //            and its total equals numActiveInputs(), which is also what the
    //            console's "PCI Use: Ins enabled N" line prints.
    struct InputState { unsigned char exists = 0, enabled = 0; };
    InputState  inputState(Exception&, int id) const;

    int         numOutputs(Exception&) const;
    int         numActiveOutputs(Exception&) const;
    int         nthActiveOutputID(Exception&, int n) const;
    std::string outputDescription(Exception&, int id) const;
    void        setOutputSource(Exception&, int id, int source);

    // GetOutputState: one byte plus an int. The byte parallels
    // InputState::exists. The int is the channel's source: -1 plays the
    // computer's output (the "Enable Output" checkbox), -2 is disabled, and
    // >= 0 routes an input id there (cards whose gestalt allows routing).
    struct OutputState {
        unsigned char exists = 0;
        int source = -2;
        bool enabled() const { return source == -1; }
    };
    static constexpr int kOutputEnabled = -1, kOutputDisabled = -2;
    OutputState outputState(Exception&, int id) const;

    int         bankRelativeID(Exception&, int id) const;
    std::string channelName(Exception&, int id, bool isInput) const;
    void        setChannelName(Exception&, int id, bool isInput, const std::string&);

    unsigned    cardType(Exception&) const;

    // How many level meters the card will report, which MOTU derives from
    // cardType() via a three-entry table { 24, 24, 48 } defaulting to 48
    // (CoreDeviceAW::AWGetMaxNumLevelMeters). Which row a PCIe-424 lands on
    // has not been observed yet -- print cardType() and find out.
    int         maxLevelMeters(Exception&) const;
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

// Channels as CoreAudio numbers them: 1-based, active channels only, in stream
// order. These are what Default Input / Default Output list and select.
int         channelCount(AudioDeviceID, bool isInput);
std::string channelName(AudioDeviceID, bool isInput, int channel);
// 'lccn' / 'lcnn': "HD192:Analog-A" + "1", or a custom name + "". MOTU's
// Default Input/Output labels are built from these two, not from channelName.
std::string channelCategory(AudioDeviceID, bool isInput, int channel);
std::string channelNumber(AudioDeviceID, bool isInput, int channel);
// From the first output stream's physical format; 0 if unavailable.
int         bytesPerSample(AudioDeviceID);
bool        preferredStereo(AudioDeviceID, bool isInput, UInt32& left, UInt32& right);
bool        setPreferredStereo(AudioDeviceID, bool isInput, UInt32 left, UInt32 right);

std::string deviceUID(AudioDeviceID);

// Calls `fn` on the main queue when any of `selectors` changes, in either
// scope. Returns a token for removeListener.
struct Listener;
Listener* addListener(AudioDeviceID, const std::vector<AudioObjectPropertySelector>& selectors,
                      void (^fn)(void));
void removeListener(Listener*);

// MOTU's private device properties ('Mvol' and friends) are plain UInt32s.
// Returns false if the device does not have the property.
bool uint32Property(AudioDeviceID, AudioObjectPropertySelector, AudioObjectPropertyScope, UInt32& value);
bool setUint32Property(AudioDeviceID, AudioObjectPropertySelector, AudioObjectPropertyScope, UInt32 value);

}  // namespace device
}  // namespace motu
