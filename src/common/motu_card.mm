#include "motu_card.h"

#include <Block.h>
#include <dispatch/dispatch.h>

#include <cstdio>
#include <cstring>

namespace motu {
namespace {

// --- vtable dispatch -------------------------------------------------------
//
// Every method is `result f(this, MOTUException*, args...)`. There are no
// headers, so we cast the slot to the shape the API map says it has. Slot
// numbers below are the indices from docs/HALPLUGIN-API.md; keep them in sync.

inline void* slot(void* obj, int n) { return (*(void***)obj)[n]; }

template <typename Fn>
inline Fn fn(void* obj, int n) { return reinterpret_cast<Fn>(slot(obj, n)); }

using F_void   = void  (*)(void*, void*);
using F_int    = int   (*)(void*, void*);
using F_uint   = unsigned (*)(void*, void*);
using F_ptr    = void* (*)(void*, void*);
using F_i_i    = int   (*)(void*, void*, int);
using F_i_ii   = int   (*)(void*, void*, int, int);
using F_v_i    = void  (*)(void*, void*, int);
using F_v_ib   = void  (*)(void*, void*, int, bool);
using F_v_ii   = void  (*)(void*, void*, int, int);
using F_v_iib  = void  (*)(void*, void*, int, int, bool);
using F_v_iii  = void  (*)(void*, void*, int, int, int);
using F_v_b    = void  (*)(void*, void*, bool);
using F_name   = void  (*)(void*, void*, int, char*, int);
using F_name2  = void  (*)(void*, void*, char*, int);
using F_3ip    = int   (*)(void*, void*, int*, int*, int*);
using F_2ip    = int   (*)(void*, void*, int*, int*);
using F_cfget  = void  (*)(void*, void*, int, bool, CFStringRef*);
using F_cfset  = void  (*)(void*, void*, int, bool, CFStringRef);
using F_v_ip   = void  (*)(void*, void*, unsigned*);
using F_bank   = void  (*)(void*, void*, int, int, char*);
using F_instate  = void (*)(void*, void*, int, unsigned char*, unsigned char*);
using F_outstate = void (*)(void*, void*, int, unsigned char*, int*);
using F_otherop  = int  (*)(void*, void*, bool, int, int*);
using F_meters   = void (*)(void*, void*, const void*, void*);

// Fixed-size C-string out-params. MOTU never documents the required buffer
// size; 256 is comfortably above every string these APIs actually return.
constexpr int kStrBuf = 256;

std::string callName(void* obj, int n, Exception& e, int index) {
    char buf[kStrBuf];
    std::memset(buf, 0, sizeof buf);
    e.reset();
    fn<F_name>(obj, n)(obj, e.raw, index, buf, kStrBuf - 1);
    buf[kStrBuf - 1] = '\0';
    return buf;
}

}  // namespace

// --- Exception -------------------------------------------------------------

void Exception::reset() { std::memset(raw, 0, kSize); }

bool Exception::raised() const {
    for (int i = 0; i < kSize; ++i)
        if (raw[i]) return true;
    return false;
}

int Exception::kind() const { int v; std::memcpy(&v, raw + 0, 4); return v; }
int Exception::code() const { int v; std::memcpy(&v, raw + 4, 4); return v; }
int Exception::line() const { int v; std::memcpy(&v, raw + 8, 4); return v; }

std::string Exception::file() const {
    const char* f = (const char*)raw + 12;
    // Stay inside the record even if the driver ever fills it completely.
    size_t max = kSize - 12, n = 0;
    while (n < max && f[n]) ++n;
    return std::string(f, n);
}

std::string Exception::str() const {
    if (!raised()) return "ok";
    char b[256];
    std::snprintf(b, sizeof b, "kind=%d code=%d at %s:%d",
                  kind(), code(), file().c_str(), line());
    return b;
}

std::string Exception::hex() const {
    char out[32 * 3 + 1];
    char* p = out;
    for (int i = 0; i < 32; ++i) p += std::sprintf(p, "%02x ", raw[i]);
    return std::string(out);
}

// --- CueMix ----------------------------------------------------------------

bool CueMix::inputMute(Exception& e, int ch) const   { e.reset(); return fn<F_i_i>(p_, 2)(p_, e.raw, ch) != 0; }
void CueMix::setInputMute(Exception& e, int ch, bool v) { e.reset(); fn<F_v_ib>(p_, 3)(p_, e.raw, ch, v); }
int  CueMix::inputTrim(Exception& e, int ch) const   { e.reset(); return fn<F_i_i>(p_, 4)(p_, e.raw, ch); }
void CueMix::setInputTrim(Exception& e, int ch, int v) { e.reset(); fn<F_v_ii>(p_, 5)(p_, e.raw, ch, v); }

bool CueMix::solo(Exception& e, int bus, int ch) const   { e.reset(); return fn<F_i_ii>(p_, 6)(p_, e.raw, bus, ch) != 0; }
bool CueMix::mute(Exception& e, int bus, int ch) const   { e.reset(); return fn<F_i_ii>(p_, 7)(p_, e.raw, bus, ch) != 0; }
int  CueMix::volume(Exception& e, int bus, int ch) const { e.reset(); return fn<F_i_ii>(p_, 8)(p_, e.raw, bus, ch); }
int  CueMix::pan(Exception& e, int bus, int ch) const    { e.reset(); return fn<F_i_ii>(p_, 9)(p_, e.raw, bus, ch); }
bool CueMix::faderHasResources(Exception& e, int bus, int ch) const { e.reset(); return fn<F_i_ii>(p_, 10)(p_, e.raw, bus, ch) != 0; }

bool CueMix::busMute(Exception& e, int bus) const   { e.reset(); return fn<F_i_i>(p_, 11)(p_, e.raw, bus) != 0; }
int  CueMix::busVolume(Exception& e, int bus) const { e.reset(); return fn<F_i_i>(p_, 12)(p_, e.raw, bus); }
bool CueMix::busSoloed(Exception& e, int bus) const { e.reset(); return fn<F_i_i>(p_, 13)(p_, e.raw, bus) != 0; }
int  CueMix::busResourceUsage(Exception& e, int bus) const { e.reset(); return fn<F_i_i>(p_, 14)(p_, e.raw, bus); }

void CueMix::setSolo(Exception& e, int bus, int ch, bool v)  { e.reset(); fn<F_v_iib>(p_, 15)(p_, e.raw, bus, ch, v); }
void CueMix::setMute(Exception& e, int bus, int ch, bool v)  { e.reset(); fn<F_v_iib>(p_, 16)(p_, e.raw, bus, ch, v); }
void CueMix::setVolume(Exception& e, int bus, int ch, int v) { e.reset(); fn<F_v_iii>(p_, 17)(p_, e.raw, bus, ch, v); }
void CueMix::setPan(Exception& e, int bus, int ch, int v)    { e.reset(); fn<F_v_iii>(p_, 18)(p_, e.raw, bus, ch, v); }
void CueMix::setBusMute(Exception& e, int bus, bool v)       { e.reset(); fn<F_v_ib>(p_, 19)(p_, e.raw, bus, v); }
void CueMix::setBusVolume(Exception& e, int bus, int v)      { e.reset(); fn<F_v_ii>(p_, 20)(p_, e.raw, bus, v); }

int CueMix::inputBalance(Exception& e, int bus, int ch) const          { e.reset(); return fn<F_i_ii>(p_, 28)(p_, e.raw, bus, ch); }
int CueMix::inputWidth(Exception& e, int bus, int ch) const            { e.reset(); return fn<F_i_ii>(p_, 29)(p_, e.raw, bus, ch); }
int CueMix::inputBalanceWidthPref(Exception& e, int bus, int ch) const { e.reset(); return fn<F_i_ii>(p_, 30)(p_, e.raw, bus, ch); }
int CueMix::inputChannelMapping(Exception& e, int ch) const            { e.reset(); return fn<F_i_i>(p_, 31)(p_, e.raw, ch); }

CueMix::Resources CueMix::resources(Exception& e) const {
    Resources r; e.reset();
    fn<F_3ip>(p_, 22)(p_, e.raw, &r.used, &r.unidentified, &r.max);
    return r;
}

void CueMix::readLevelMeters(Exception& e, const LevelMeterRequest& req,
                             LevelMeterResults* out) const {
    e.reset();
    fn<F_meters>(p_, 21)(p_, e.raw, &req, out);
}

CueMix::PCIUsage CueMix::pciUsage(Exception& e) const {
    PCIUsage u; e.reset();
    fn<F_2ip>(p_, 23)(p_, e.raw, &u.a, &u.b);
    return u;
}

// --- SMPTE -----------------------------------------------------------------

int  SMPTE::frameRate(Exception& e) const            { e.reset(); return fn<F_int>(p_, 3)(p_, e.raw); }
int  SMPTE::frameFormat(Exception& e) const          { e.reset(); return fn<F_int>(p_, 4)(p_, e.raw); }
void SMPTE::setFreewheelTimes(Exception& e, int a, int c) { e.reset(); fn<F_v_ii>(p_, 5)(p_, e.raw, a, c); }
int  SMPTE::freewheelAddressTime(Exception& e) const { e.reset(); return fn<F_int>(p_, 6)(p_, e.raw); }
int  SMPTE::freewheelClockTime(Exception& e) const   { e.reset(); return fn<F_int>(p_, 7)(p_, e.raw); }
int  SMPTE::nthSource(Exception& e, int n) const     { e.reset(); return fn<F_i_i>(p_, 8)(p_, e.raw, n); }
std::string SMPTE::sourceInfo(Exception& e, int id) const { return callName(p_, 9, e, id); }
void SMPTE::setSource(Exception& e, int v)           { e.reset(); fn<F_v_i>(p_, 10)(p_, e.raw, v); }
int  SMPTE::source(Exception& e) const               { e.reset(); return fn<F_int>(p_, 11)(p_, e.raw); }
void SMPTE::setGenerationMode(Exception& e, int v)   { e.reset(); fn<F_v_i>(p_, 12)(p_, e.raw, v); }
int  SMPTE::generationMode(Exception& e) const       { e.reset(); return fn<F_int>(p_, 13)(p_, e.raw); }
int  SMPTE::nthDestination(Exception& e, int n) const{ e.reset(); return fn<F_i_i>(p_, 16)(p_, e.raw, n); }
void SMPTE::setDestination(Exception& e, int v)      { e.reset(); fn<F_v_i>(p_, 18)(p_, e.raw, v); }
int  SMPTE::destination(Exception& e) const          { e.reset(); return fn<F_int>(p_, 19)(p_, e.raw); }
void SMPTE::setOutputLevel(Exception& e, int v)      { e.reset(); fn<F_v_i>(p_, 20)(p_, e.raw, v); }
int  SMPTE::outputLevel(Exception& e) const          { e.reset(); return fn<F_int>(p_, 21)(p_, e.raw); }

// --- Talkback --------------------------------------------------------------

void Talkback::setTalkbackInput(Exception& e, int v)     { e.reset(); fn<F_v_i>(p_, 2)(p_, e.raw, v); }
void Talkback::setListenbackInput(Exception& e, int v)   { e.reset(); fn<F_v_i>(p_, 3)(p_, e.raw, v); }
void Talkback::setTalkbackDimLevel(Exception& e, int v)  { e.reset(); fn<F_v_i>(p_, 4)(p_, e.raw, v); }
void Talkback::setListenbackDimLevel(Exception& e, int v){ e.reset(); fn<F_v_i>(p_, 5)(p_, e.raw, v); }
void Talkback::setTalkbackOutput(Exception& e, int n, int v)   { e.reset(); fn<F_v_ii>(p_, 6)(p_, e.raw, n, v); }
void Talkback::setListenbackOutput(Exception& e, int n, int v) { e.reset(); fn<F_v_ii>(p_, 7)(p_, e.raw, n, v); }
int  Talkback::talkbackInput(Exception& e) const     { e.reset(); return fn<F_int>(p_, 8)(p_, e.raw); }
int  Talkback::listenbackInput(Exception& e) const   { e.reset(); return fn<F_int>(p_, 9)(p_, e.raw); }
int  Talkback::talkbackDimLevel(Exception& e) const  { e.reset(); return fn<F_int>(p_, 10)(p_, e.raw); }
int  Talkback::listenbackDimLevel(Exception& e) const{ e.reset(); return fn<F_int>(p_, 11)(p_, e.raw); }
int  Talkback::talkbackOutput(Exception& e, int n) const   { e.reset(); return fn<F_i_i>(p_, 12)(p_, e.raw, n); }
int  Talkback::listenbackOutput(Exception& e, int n) const { e.reset(); return fn<F_i_i>(p_, 13)(p_, e.raw, n); }
int  Talkback::talkbackEnable(Exception& e) const    { e.reset(); return fn<F_int>(p_, 14)(p_, e.raw); }
int  Talkback::listenbackEnable(Exception& e) const  { e.reset(); return fn<F_int>(p_, 15)(p_, e.raw); }
int  Talkback::talkbackLink(Exception& e) const      { e.reset(); return fn<F_int>(p_, 16)(p_, e.raw); }
void Talkback::setTalkbackEnable(Exception& e, int v)   { e.reset(); fn<F_v_i>(p_, 17)(p_, e.raw, v); }
void Talkback::setListenbackEnable(Exception& e, int v) { e.reset(); fn<F_v_i>(p_, 18)(p_, e.raw, v); }
void Talkback::setTalkbackLink(Exception& e, int v)     { e.reset(); fn<F_v_i>(p_, 19)(p_, e.raw, v); }

// --- Interface -------------------------------------------------------------

int Interface::id(Exception& e) const { e.reset(); return fn<F_int>(p_, 2)(p_, e.raw); }

std::string Interface::name(Exception& e) const {
    char buf[kStrBuf]; std::memset(buf, 0, sizeof buf); e.reset();
    fn<F_name2>(p_, 3)(p_, e.raw, buf, kStrBuf - 1);
    return buf;
}

std::string Interface::versionString(Exception& e) const {
    char buf[kStrBuf]; std::memset(buf, 0, sizeof buf); e.reset();
    fn<F_name2>(p_, 4)(p_, e.raw, buf, kStrBuf - 1);
    return buf;
}

int Interface::numBanks(Exception& e) const { e.reset(); return fn<F_int>(p_, 6)(p_, e.raw); }
int Interface::numChannelsInBank(Exception& e, int bank) const { e.reset(); return fn<F_i_i>(p_, 7)(p_, e.raw, bank); }

std::string Interface::nthBankPersonality(Exception& e, int bank, int n) const {
    char buf[kStrBuf]; std::memset(buf, 0, sizeof buf); e.reset();
    fn<F_bank>(p_, 8)(p_, e.raw, bank, n, buf);
    buf[kStrBuf - 1] = '\0';
    return buf;
}

void Interface::setPersonalityForBank(Exception& e, int bank, int p) { e.reset(); fn<F_v_ii>(p_, 9)(p_, e.raw, bank, p); }
int  Interface::personalityForBank(Exception& e, int bank) const { e.reset(); return fn<F_i_i>(p_, 10)(p_, e.raw, bank); }
bool Interface::inputChannelAvailable(Exception& e, int ch) const  { e.reset(); return fn<F_i_i>(p_, 11)(p_, e.raw, ch) != 0; }
bool Interface::outputChannelAvailable(Exception& e, int ch) const { e.reset(); return fn<F_i_i>(p_, 12)(p_, e.raw, ch) != 0; }

bool Interface::getOption(Exception& e, Option o, int& value) const {
    // An interface that does not implement a selector returns *without raising*
    // and simply leaves the out-param alone, so "no exception" is not enough to
    // tell a real 0 from an absent property. Call twice with two different
    // sentinels: if neither is overwritten, nothing was written.
    int a = 0x5A5A5A5A, b = ~0x5A5A5A5A;
    e.reset();
    fn<F_otherop>(p_, 5)(p_, e.raw, true, (int)o, &a);
    if (e.raised()) return false;
    fn<F_otherop>(p_, 5)(p_, e.raw, true, (int)o, &b);
    if (e.raised()) return false;
    if (a == 0x5A5A5A5A && b == ~0x5A5A5A5A) return false;   // untouched both times
    value = a;
    return true;
}

void Interface::setOption(Exception& e, Option o, int value) const {
    e.reset();
    fn<F_otherop>(p_, 5)(p_, e.raw, false, (int)o, &value);
}

// --- Card ------------------------------------------------------------------

bool Card::installRunLoop(std::string* err) {
    CFRunLoopRef rl = CFRunLoopGetCurrent();
    AudioObjectPropertyAddress a = { 'rnlp', kAudioObjectPropertyScopeGlobal,
                                     kAudioObjectPropertyElementMain };
    OSStatus st = AudioObjectSetPropertyData(kAudioObjectSystemObject, &a, 0, nullptr,
                                             (UInt32)sizeof(CFRunLoopRef), &rl);
    if (st != noErr) {
        if (err) { char b[96]; std::snprintf(b, sizeof b, "set 'rnlp' failed: OSStatus %d", (int)st); *err = b; }
        return false;
    }
    return true;
}

AudioDeviceID Card::findDevice() {
    AudioObjectPropertyAddress a = { kAudioHardwarePropertyDevices,
                                     kAudioObjectPropertyScopeGlobal,
                                     kAudioObjectPropertyElementWildcard };
    UInt32 sz = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &a, 0, nullptr, &sz) != noErr || !sz)
        return 0;
    std::vector<AudioDeviceID> ids(sz / sizeof(AudioDeviceID));
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, 0, nullptr, &sz, ids.data()) != noErr)
        return 0;
    for (AudioDeviceID id : ids) {
        AudioObjectPropertyAddress na = { kAudioDevicePropertyDeviceNameCFString,
                                          kAudioObjectPropertyScopeGlobal,
                                          kAudioObjectPropertyElementWildcard };
        CFStringRef nm = nullptr; UInt32 ns = sizeof nm;
        if (AudioObjectGetPropertyData(id, &na, 0, nullptr, &ns, &nm) == noErr && nm) {
            char b[256] = {0};
            CFStringGetCString(nm, b, sizeof b, kCFStringEncodingUTF8);
            CFRelease(nm);
            if (std::strstr(b, "PCI-424")) return id;
        }
    }
    return 0;
}

namespace {
OSStatus noopListener(AudioObjectID, UInt32, const AudioObjectPropertyAddress*, void*) { return noErr; }

// The stock app installs these before it asks for 'Mapi'. Reproducing that
// wakes the plugin up; without them the first fetches come back NULL.
void installListeners(AudioDeviceID d) {
    const struct { AudioObjectPropertySelector sel; AudioObjectPropertyScope scope; } kListeners[] = {
        { 'nsrt', kAudioObjectPropertyScopeGlobal },
        { 'dch2', kAudioObjectPropertyScopeOutput },
        { 'dch2', kAudioObjectPropertyScopeInput  },
        { 'csrc', kAudioObjectPropertyScopeGlobal },
    };
    for (const auto& l : kListeners) {
        AudioObjectPropertyAddress a = { l.sel, l.scope, kAudioObjectPropertyElementMain };
        AudioObjectAddPropertyListener(d, &a, noopListener, nullptr);
    }
}
}  // namespace

Card Card::open(AudioDeviceID dev, std::string* err, double timeoutSeconds) {
    if (!dev) { if (err) *err = "PCI-424 not found in CoreAudio"; return Card(); }

    installListeners(dev);
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.5, false);

    AudioObjectPropertyAddress m = { 'Mapi', kAudioObjectPropertyScopeGlobal,
                                     kAudioObjectPropertyElementMain };
    const double kStep = 0.25;
    const int kAttempts = (int)(timeoutSeconds / kStep) + 1;
    OSStatus st = noErr;

    for (int i = 0; i < kAttempts; ++i) {
        // Re-enumerate each round: the device ID can change while the plugin
        // is still settling, and the stock app looks it up every time too.
        AudioDeviceID d = findDevice();
        unsigned char buf[8];
        std::memset(buf, 0, sizeof buf);
        *(UInt32*)buf = 0x0E;              // engine gestalt version 14
        UInt32 sz = sizeof buf;
        st = AudioObjectGetPropertyData(d, &m, 0, nullptr, &sz, buf);
        if (void* p = *(void**)buf) return Card(p);
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, kStep, false);
    }

    if (err) {
        char b[160];
        std::snprintf(b, sizeof b,
                      "'Mapi' returned NULL after %.1fs (last OSStatus %d). "
                      "Wrong thread, or something touched CoreAudio before installRunLoop().",
                      timeoutSeconds, (int)st);
        *err = b;
    }
    return Card();
}

int  Card::gestalt(Exception& e, int sel) const { e.reset(); return fn<F_i_i>(p_, 4)(p_, e.raw, sel); }
void Card::commitChanges(Exception& e, bool v)  { e.reset(); fn<F_v_b>(p_, 3)(p_, e.raw, v); }
void Card::flushPrefs(Exception& e)             { e.reset(); fn<F_void>(p_, 10)(p_, e.raw); }
std::vector<unsigned char> Card::saveConfiguration(Exception& e) const {
    e.reset();
    auto cfg = (CFPropertyListRef)fn<F_ptr>(p_, 8)(p_, e.raw);   // +1 retained
    if (!cfg) return {};
    std::vector<unsigned char> out;
    if (CFDataRef xml = CFPropertyListCreateData(kCFAllocatorDefault, cfg, kCFPropertyListXMLFormat_v1_0, 0, nullptr)) {
        out.assign(CFDataGetBytePtr(xml), CFDataGetBytePtr(xml) + CFDataGetLength(xml));
        CFRelease(xml);
    }
    CFRelease(cfg);
    return out;
}

bool Card::loadConfiguration(Exception& e, const std::vector<unsigned char>& xml) {
    e.reset();
    CFDataRef data = CFDataCreate(kCFAllocatorDefault, xml.data(), (CFIndex)xml.size());
    if (!data) return false;
    CFPropertyListRef cfg = CFPropertyListCreateWithData(kCFAllocatorDefault, data, kCFPropertyListImmutable,
                                                         nullptr, nullptr);
    CFRelease(data);
    if (!cfg) return false;
    ((void (*)(void*, void*, CFPropertyListRef))slot(p_, 9))(p_, e.raw, cfg);   // retains
    CFRelease(cfg);
    if (e.raised()) return false;
    commitChanges(e, true);
    return !e.raised();
}

void Card::probeForInterfaces(Exception& e)     { e.reset(); fn<F_void>(p_, 15)(p_, e.raw); }

int  Card::numWires(Exception& e) const { e.reset(); return fn<F_int>(p_, 11)(p_, e.raw); }
bool Card::wireConnected(Exception& e, int w) const { e.reset(); return fn<F_i_i>(p_, 12)(p_, e.raw, w) != 0; }
std::string Card::wireInterfaceName(Exception& e, int w) const { return callName(p_, 14, e, w); }

Interface Card::wireInterface(Exception& e, int w) const {
    e.reset();
    return Interface(((void* (*)(void*, void*, int))slot(p_, 13))(p_, e.raw, w));
}

int Card::numInputs(Exception& e) const        { e.reset(); return fn<F_int>(p_, 16)(p_, e.raw); }
int Card::numActiveInputs(Exception& e) const  { e.reset(); return fn<F_int>(p_, 17)(p_, e.raw); }
int Card::nthActiveInputID(Exception& e, int n) const { e.reset(); return fn<F_i_i>(p_, 18)(p_, e.raw, n); }
std::string Card::inputDescription(Exception& e, int id) const { return callName(p_, 19, e, id); }
void Card::setInputEnable(Exception& e, int id, bool v) { e.reset(); fn<F_v_ib>(p_, 21)(p_, e.raw, id, v); }

Card::InputState Card::inputState(Exception& e, int id) const {
    InputState st;
    e.reset();
    fn<F_instate>(p_, 20)(p_, e.raw, id, &st.exists, &st.enabled);
    return st;
}

int Card::numOutputs(Exception& e) const       { e.reset(); return fn<F_int>(p_, 22)(p_, e.raw); }
int Card::numActiveOutputs(Exception& e) const { e.reset(); return fn<F_int>(p_, 23)(p_, e.raw); }
int Card::nthActiveOutputID(Exception& e, int n) const { e.reset(); return fn<F_i_i>(p_, 24)(p_, e.raw, n); }
std::string Card::outputDescription(Exception& e, int id) const { return callName(p_, 25, e, id); }
void Card::setOutputSource(Exception& e, int id, int src) { e.reset(); fn<F_v_ii>(p_, 27)(p_, e.raw, id, src); }

Card::OutputState Card::outputState(Exception& e, int id) const {
    OutputState st;
    e.reset();
    fn<F_outstate>(p_, 26)(p_, e.raw, id, &st.exists, &st.source);
    return st;
}

int Card::bankRelativeID(Exception& e, int id) const { e.reset(); return fn<F_i_i>(p_, 28)(p_, e.raw, id); }

std::string Card::channelName(Exception& e, int id, bool isInput) const {
    CFStringRef s = nullptr;
    e.reset();
    fn<F_cfget>(p_, 29)(p_, e.raw, id, isInput, &s);
    if (!s) return {};
    char b[256] = {0};
    CFStringGetCString(s, b, sizeof b, kCFStringEncodingUTF8);
    CFRelease(s);
    return b;
}

void Card::setChannelName(Exception& e, int id, bool isInput, const std::string& n) {
    CFStringRef s = CFStringCreateWithCString(kCFAllocatorDefault, n.c_str(), kCFStringEncodingUTF8);
    e.reset();
    fn<F_cfset>(p_, 30)(p_, e.raw, id, isInput, s);
    if (s) CFRelease(s);
}

int Card::maxLevelMeters(Exception& e) const {
    const unsigned t = cardType(e);
    if (e.raised()) return 0;
    static const int kByCardType[3] = { 24, 24, 48 };
    return t < 3 ? kByCardType[t] : 48;
}

unsigned Card::cardType(Exception& e) const {
    unsigned t = 0; e.reset();
    fn<F_v_ip>(p_, 31)(p_, e.raw, &t);
    return t;
}

int  Card::smuxSetting(Exception& e) const { e.reset(); return fn<F_int>(p_, 32)(p_, e.raw); }
void Card::setSmuxSetting(Exception& e, int v) { e.reset(); fn<F_v_i>(p_, 33)(p_, e.raw, v); }
int  Card::smuxCapable(Exception& e) const { e.reset(); return fn<F_int>(p_, 34)(p_, e.raw); }

CueMix   Card::cueMix(Exception& e) const   { e.reset(); return CueMix(fn<F_ptr>(p_, 5)(p_, e.raw)); }
SMPTE    Card::smpte(Exception& e) const    { e.reset(); return SMPTE(fn<F_ptr>(p_, 6)(p_, e.raw)); }
Talkback Card::talkback(Exception& e) const { e.reset(); return Talkback(fn<F_ptr>(p_, 7)(p_, e.raw)); }

// --- device properties -----------------------------------------------------

namespace device {
namespace {
template <typename T>
bool getProp(AudioDeviceID d, AudioObjectPropertySelector sel, T* out) {
    AudioObjectPropertyAddress a = { sel, kAudioObjectPropertyScopeGlobal,
                                     kAudioObjectPropertyElementMain };
    UInt32 sz = sizeof(T);
    return AudioObjectGetPropertyData(d, &a, 0, nullptr, &sz, out) == noErr;
}

template <typename T>
std::vector<T> getArray(AudioDeviceID d, AudioObjectPropertySelector sel) {
    AudioObjectPropertyAddress a = { sel, kAudioObjectPropertyScopeGlobal,
                                     kAudioObjectPropertyElementMain };
    UInt32 sz = 0;
    if (AudioObjectGetPropertyDataSize(d, &a, 0, nullptr, &sz) != noErr || !sz) return {};
    std::vector<T> v(sz / sizeof(T));
    if (AudioObjectGetPropertyData(d, &a, 0, nullptr, &sz, v.data()) != noErr) return {};
    v.resize(sz / sizeof(T));
    return v;
}
}  // namespace

double sampleRate(AudioDeviceID d) {
    Float64 r = 0;
    return getProp(d, kAudioDevicePropertyNominalSampleRate, &r) ? (double)r : 0.0;
}

bool setSampleRate(AudioDeviceID d, double hz) {
    AudioObjectPropertyAddress a = { kAudioDevicePropertyNominalSampleRate,
                                     kAudioObjectPropertyScopeGlobal,
                                     kAudioObjectPropertyElementMain };
    Float64 r = (Float64)hz;
    return AudioObjectSetPropertyData(d, &a, 0, nullptr, sizeof r, &r) == noErr;
}

std::vector<double> availableSampleRates(AudioDeviceID d) {
    auto ranges = getArray<AudioValueRange>(d, kAudioDevicePropertyAvailableNominalSampleRates);
    std::vector<double> out;
    for (const auto& r : ranges) {
        out.push_back(r.mMinimum);
        if (r.mMaximum != r.mMinimum) out.push_back(r.mMaximum);
    }
    return out;
}

UInt32 clockSource(AudioDeviceID d) {
    UInt32 s = 0;
    return getProp(d, kAudioDevicePropertyClockSource, &s) ? s : 0;
}

bool setClockSource(AudioDeviceID d, UInt32 s) {
    AudioObjectPropertyAddress a = { kAudioDevicePropertyClockSource,
                                     kAudioObjectPropertyScopeGlobal,
                                     kAudioObjectPropertyElementMain };
    return AudioObjectSetPropertyData(d, &a, 0, nullptr, sizeof s, &s) == noErr;
}

std::vector<UInt32> clockSources(AudioDeviceID d) {
    return getArray<UInt32>(d, kAudioDevicePropertyClockSources);
}

std::string clockSourceName(AudioDeviceID d, UInt32 id) {
    CFStringRef nm = nullptr;
    AudioValueTranslation t = { &id, sizeof id, &nm, sizeof nm };
    AudioObjectPropertyAddress a = { kAudioDevicePropertyClockSourceNameForIDCFString,
                                     kAudioObjectPropertyScopeGlobal,
                                     kAudioObjectPropertyElementMain };
    UInt32 sz = sizeof t;
    if (AudioObjectGetPropertyData(d, &a, 0, nullptr, &sz, &t) != noErr || !nm) return {};
    char b[256] = {0};
    CFStringGetCString(nm, b, sizeof b, kCFStringEncodingUTF8);
    CFRelease(nm);
    return b;
}

namespace {
AudioObjectPropertyScope scopeOf(bool isInput) {
    return isInput ? kAudioObjectPropertyScopeInput : kAudioObjectPropertyScopeOutput;
}

std::string toUtf8(CFStringRef s) {
    if (!s) return {};
    char b[512] = {0};
    CFStringGetCString(s, b, sizeof b, kCFStringEncodingUTF8);
    return b;
}
}  // namespace

int channelCount(AudioDeviceID d, bool isInput) {
    AudioObjectPropertyAddress a = { kAudioDevicePropertyStreamConfiguration, scopeOf(isInput),
                                     kAudioObjectPropertyElementMain };
    UInt32 sz = 0;
    if (AudioObjectGetPropertyDataSize(d, &a, 0, nullptr, &sz) != noErr || !sz) return 0;
    std::vector<unsigned char> buf(sz);
    if (AudioObjectGetPropertyData(d, &a, 0, nullptr, &sz, buf.data()) != noErr) return 0;
    const auto* list = (const AudioBufferList*)buf.data();
    int n = 0;
    for (UInt32 i = 0; i < list->mNumberBuffers; ++i) n += (int)list->mBuffers[i].mNumberChannels;
    return n;
}

std::string channelName(AudioDeviceID d, bool isInput, int channel) {
    AudioObjectPropertyAddress a = { kAudioObjectPropertyElementName, scopeOf(isInput), (UInt32)channel };
    CFStringRef nm = nullptr; UInt32 sz = sizeof nm;
    if (AudioObjectGetPropertyData(d, &a, 0, nullptr, &sz, &nm) != noErr || !nm) return {};
    std::string s = toUtf8(nm);
    CFRelease(nm);
    return s;
}

namespace {
std::string elementString(AudioDeviceID d, AudioObjectPropertySelector sel, bool isInput, int channel) {
    AudioObjectPropertyAddress a = { sel, scopeOf(isInput), (UInt32)channel };
    CFStringRef nm = nullptr; UInt32 sz = sizeof nm;
    if (AudioObjectGetPropertyData(d, &a, 0, nullptr, &sz, &nm) != noErr || !nm) return {};
    std::string s = toUtf8(nm);
    CFRelease(nm);
    return s;
}
}  // namespace

std::string channelCategory(AudioDeviceID d, bool isInput, int channel) {
    return elementString(d, kAudioObjectPropertyElementCategoryName, isInput, channel);
}

std::string channelNumber(AudioDeviceID d, bool isInput, int channel) {
    return elementString(d, kAudioObjectPropertyElementNumberName, isInput, channel);
}

int bytesPerSample(AudioDeviceID d) {
    AudioObjectPropertyAddress a = { kAudioDevicePropertyStreams, kAudioObjectPropertyScopeOutput,
                                     kAudioObjectPropertyElementMain };
    UInt32 sz = 0;
    if (AudioObjectGetPropertyDataSize(d, &a, 0, nullptr, &sz) != noErr || sz < sizeof(AudioStreamID)) return 0;
    std::vector<AudioStreamID> streams(sz / sizeof(AudioStreamID));
    if (AudioObjectGetPropertyData(d, &a, 0, nullptr, &sz, streams.data()) != noErr) return 0;
    AudioObjectPropertyAddress f = { kAudioStreamPropertyPhysicalFormat, kAudioObjectPropertyScopeGlobal,
                                     kAudioObjectPropertyElementMain };
    AudioStreamBasicDescription asbd {};
    UInt32 fs = sizeof asbd;
    if (AudioObjectGetPropertyData(streams[0], &f, 0, nullptr, &fs, &asbd) != noErr
        || asbd.mChannelsPerFrame == 0) return 0;
    return (int)(asbd.mBytesPerFrame / asbd.mChannelsPerFrame);
}

bool preferredStereo(AudioDeviceID d, bool isInput, UInt32& left, UInt32& right) {
    AudioObjectPropertyAddress a = { kAudioDevicePropertyPreferredChannelsForStereo, scopeOf(isInput),
                                     kAudioObjectPropertyElementMain };
    UInt32 ch[2] = { 0, 0 }; UInt32 sz = sizeof ch;
    if (AudioObjectGetPropertyData(d, &a, 0, nullptr, &sz, ch) != noErr) return false;
    left = ch[0]; right = ch[1];
    return true;
}

bool setPreferredStereo(AudioDeviceID d, bool isInput, UInt32 left, UInt32 right) {
    AudioObjectPropertyAddress a = { kAudioDevicePropertyPreferredChannelsForStereo, scopeOf(isInput),
                                     kAudioObjectPropertyElementMain };
    UInt32 ch[2] = { left, right };
    return AudioObjectSetPropertyData(d, &a, 0, nullptr, sizeof ch, ch) == noErr;
}

std::string deviceUID(AudioDeviceID d) {
    AudioObjectPropertyAddress a = { kAudioDevicePropertyDeviceUID, kAudioObjectPropertyScopeGlobal,
                                     kAudioObjectPropertyElementMain };
    CFStringRef s = nullptr; UInt32 sz = sizeof s;
    if (AudioObjectGetPropertyData(d, &a, 0, nullptr, &sz, &s) != noErr || !s) return {};
    std::string out = toUtf8(s);
    CFRelease(s);
    return out;
}

struct Listener {
    AudioDeviceID device;
    std::vector<AudioObjectPropertyAddress> addresses;
    AudioObjectPropertyListenerBlock block;
};

Listener* addListener(AudioDeviceID d, const std::vector<AudioObjectPropertySelector>& selectors,
                      void (^fn)(void)) {
    auto* l = new Listener { d, {}, nil };
    l->block = Block_copy(^(UInt32, const AudioObjectPropertyAddress*) { fn(); });
    for (auto sel : selectors)
        for (auto scope : { kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyScopeInput,
                            kAudioObjectPropertyScopeOutput }) {
            AudioObjectPropertyAddress a = { sel, scope, kAudioObjectPropertyElementMain };
            if (!AudioObjectHasProperty(d, &a)) continue;
            if (AudioObjectAddPropertyListenerBlock(d, &a, dispatch_get_main_queue(), l->block) == noErr)
                l->addresses.push_back(a);
        }
    return l;
}

void removeListener(Listener* l) {
    if (!l) return;
    for (auto& a : l->addresses)
        AudioObjectRemovePropertyListenerBlock(l->device, &a, dispatch_get_main_queue(), l->block);
    Block_release(l->block);
    delete l;
}

bool uint32Property(AudioDeviceID d, AudioObjectPropertySelector sel,
                    AudioObjectPropertyScope scope, UInt32& value) {
    AudioObjectPropertyAddress a = { sel, scope, kAudioObjectPropertyElementMain };
    if (!AudioObjectHasProperty(d, &a)) return false;
    UInt32 v = 0, sz = sizeof v;
    if (AudioObjectGetPropertyData(d, &a, 0, nullptr, &sz, &v) != noErr) return false;
    value = v;
    return true;
}

bool setUint32Property(AudioDeviceID d, AudioObjectPropertySelector sel,
                       AudioObjectPropertyScope scope, UInt32 value) {
    AudioObjectPropertyAddress a = { sel, scope, kAudioObjectPropertyElementMain };
    return AudioObjectSetPropertyData(d, &a, 0, nullptr, sizeof value, &value) == noErr;
}

}  // namespace device
}  // namespace motu
