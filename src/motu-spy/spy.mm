// MotuSpy — snapshot every readable piece of PCI-424 state, so a change made in
// MOTU's own apps on Mojave can be pinned to exact API calls and values.
//
//   1. Type what you are about to change, click Snapshot.
//   2. Change exactly that one thing in CueMix FX / PCI Audio Setup.
//   3. Type what you changed, click Snapshot again.
//
// Each snapshot is ~/Desktop/MotuSpy/NNN-label.txt, one "key = value" per line,
// plus NNN-label.diff against the previous one. Those folders are readable from
// the Sequoia side at /Volumes/Sierra/Users/<user>/Desktop/MotuSpy.
//
// READ-ONLY. Only getters are called, and the CueMix balance/width/mapping
// getters — which do no bounds checking and segfault past the end — are only
// called with a bus and channel a range-checked getter has already accepted.
//
// Built for 10.14 so it runs on the Mojave volume:
//   MACOSX_DEPLOYMENT_TARGET=10.14 tools/build.sh MotuSpy src/common/motu_card.mm src/motu-spy/spy.mm

#import <Cocoa/Cocoa.h>

#include "motu_card.h"

#include <algorithm>
#include <cstdarg>
#include <map>
#include <string>
#include <vector>

namespace {

struct Snapshot {
    std::vector<std::pair<std::string, std::string>> lines;

    void add(const std::string& key, const std::string& value) { lines.emplace_back(key, value); }
    void add(const std::string& key, long long v) { add(key, std::to_string(v)); }
    void addf(const std::string& key, const char* fmt, ...) {
        char b[512];
        va_list a; va_start(a, fmt); vsnprintf(b, sizeof b, fmt, a); va_end(a);
        add(key, b);
    }
};

std::string k(const char* fmt, ...) {
    char b[256];
    va_list a; va_start(a, fmt); vsnprintf(b, sizeof b, fmt, a); va_end(a);
    return b;
}

std::string hexDump(NSData* d) {
    std::string out;
    const auto* p = (const unsigned char*)d.bytes;
    char b[4];
    for (NSUInteger i = 0; i < d.length; ++i) { snprintf(b, sizeof b, "%02x", p[i]); out += b; }
    return out;
}

void addDevice(Snapshot& s, AudioDeviceID dev) {
    s.addf("device.sampleRate", "%.0f", motu::device::sampleRate(dev));
    s.add("device.clockSource", motu::device::clockSource(dev));
    for (UInt32 c : motu::device::clockSources(dev))
        s.add(k("device.clockSources.%u", c), motu::device::clockSourceName(dev, c));
    for (bool in : { true, false }) {
        const char* dir = in ? "in" : "out";
        const int n = motu::device::channelCount(dev, in);
        s.add(k("device.%s.channels", dir), n);
        UInt32 l = 0, r = 0;
        if (motu::device::preferredStereo(dev, in, l, r)) s.addf(k("device.%s.defaultPair", dir), "%u %u", l, r);
        for (int ch = 1; ch <= n; ++ch)
            s.add(k("device.%s.ch%02d.name", dir, ch), motu::device::channelCategory(dev, in, ch) + " | " +
                                                      motu::device::channelNumber(dev, in, ch));
    }
    UInt32 v = 0;
    if (motu::device::uint32Property(dev, 'Mvol', kAudioObjectPropertyScopeOutput, v)) s.add("device.Mvol", v);
    s.add("device.bytesPerSample", motu::device::bytesPerSample(dev));
}

void addCard(Snapshot& s, motu::Card& card) {
    motu::Exception e;
    s.add("card.activeInputs", card.numActiveInputs(e));
    s.add("card.activeOutputs", card.numActiveOutputs(e));
    s.add("card.smux", card.smuxSetting(e));
    for (int g = 0; g < 4; ++g) s.add(k("card.gestalt.%d", g), card.gestalt(e, g));

    for (int id = 0, n = card.numInputs(e); id < n; ++id) {
        const auto st = card.inputState(e, id);
        if (!st.exists) continue;
        s.addf(k("card.in.%02d", id), "enabled=%d name=\"%s\"", st.enabled, card.channelName(e, id, true).c_str());
    }
    for (int id = 0, n = card.numOutputs(e); id < n; ++id) {
        const auto st = card.outputState(e, id);
        if (!st.exists) continue;
        s.addf(k("card.out.%02d", id), "source=%d name=\"%s\"", st.source, card.channelName(e, id, false).c_str());
    }

    for (int w = 0, n = card.numWires(e); w < n; ++w) {
        if (!card.wireConnected(e, w)) continue;   // an empty wire's name segfaults
        motu::Interface itf = card.wireInterface(e, w);
        s.add(k("wire%d.name", w), card.wireInterfaceName(e, w));
        if (!itf) continue;
        for (int b = 0, nb = itf.numBanks(e); b < nb; ++b)
            s.add(k("wire%d.bank%d.personality", w, b), itf.personalityForBank(e, b));
        for (int o = 0; o <= 8; ++o) {
            int v = 0;
            if (itf.getOption(e, (motu::Interface::Option)o, v)) s.add(k("wire%d.option%d", w, o), v);
        }
    }
}

void addCueMix(Snapshot& s, motu::Card& card) {
    motu::Exception e;
    motu::CueMix cue = card.cueMix(e);
    if (!cue) return;
    const auto r = cue.resources(e);
    s.addf("cuemix.resources", "used=%d unidentified=%d max=%d", r.used, r.unidentified, r.max);

    std::vector<int> chans;
    for (int c = 0; c < 96; ++c) {
        motu::Exception x;
        if (!card.inputState(x, c).exists) continue;
        cue.inputTrim(x, c);
        if (x.raised()) continue;
        chans.push_back(c);
        s.addf(k("cuemix.in%02d", c), "trim=%d mute=%d map=%d", cue.inputTrim(x, c), (int)cue.inputMute(x, c),
               cue.inputChannelMapping(x, c));
    }

    for (int bus = 0; bus < 96; ++bus) {
        motu::Exception x;
        cue.busVolume(x, bus);
        if (x.raised()) continue;           // odd ids and beyond: not a bus
        s.addf(k("cuemix.bus%02d", bus), "vol=%d mute=%d soloed=%d usage=%d", cue.busVolume(x, bus),
               (int)cue.busMute(x, bus), (int)cue.busSoloed(x, bus), cue.busResourceUsage(x, bus));
        for (int c : chans) {
            motu::Exception y;
            const int vol = cue.volume(y, bus, c);
            if (y.raised()) continue;
            s.addf(k("cuemix.bus%02d.ch%02d", bus, c), "vol=%d pan=%d mute=%d solo=%d res=%d bal=%d width=%d pref=%d",
                   vol, cue.pan(y, bus, c), (int)cue.mute(y, bus, c), (int)cue.solo(y, bus, c),
                   (int)cue.faderHasResources(y, bus, c), cue.inputBalance(y, bus, c), cue.inputWidth(y, bus, c),
                   cue.inputBalanceWidthPref(y, bus, c));
        }
    }
}

void addTalkbackAndSMPTE(Snapshot& s, motu::Card& card) {
    motu::Exception e;
    if (motu::Talkback tb = card.talkback(e)) {
        s.addf("talkback", "in=%d dim=%d enable=%d link=%d", tb.talkbackInput(e), tb.talkbackDimLevel(e),
               tb.talkbackEnable(e), tb.talkbackLink(e));
        s.addf("listenback", "in=%d dim=%d enable=%d", tb.listenbackInput(e), tb.listenbackDimLevel(e),
               tb.listenbackEnable(e));
        for (int n = 0; n < 48; ++n) {
            motu::Exception x;
            const int t = tb.talkbackOutput(x, n);
            if (x.raised()) break;
            s.addf(k("talkback.out%02d", n), "talk=%d listen=%d", t, tb.listenbackOutput(x, n));
        }
    }
    if (motu::SMPTE sm = card.smpte(e))
        s.addf("smpte", "rate=%d format=%d source=%d dest=%d gen=%d level=%d freewheel=%d/%d", sm.frameRate(e),
               sm.frameFormat(e), sm.source(e), sm.destination(e), sm.generationMode(e), sm.outputLevel(e),
               sm.freewheelAddressTime(e), sm.freewheelClockTime(e));
}

// Flatten a property-list value into one line per leaf: dictionaries become
// key.sub, arrays key[i], data 32 bytes per line keyed by offset (so a diff
// points straight at the field that moved).
void addPlist(Snapshot& s, const std::string& key, id v) {
    if ([v isKindOfClass:[NSDictionary class]]) {
        NSDictionary* d = v;
        for (NSString* sub in [d.allKeys sortedArrayUsingSelector:@selector(compare:)])
            addPlist(s, key + "." + sub.UTF8String, d[sub]);
    } else if ([v isKindOfClass:[NSArray class]]) {
        NSArray* a = v;
        for (NSUInteger i = 0; i < a.count; ++i) addPlist(s, k("%s[%02lu]", key.c_str(), (unsigned long)i), a[i]);
    } else if ([v isKindOfClass:[NSData class]]) {
        std::string hex = hexDump(v);
        if (hex.empty()) s.add(key, "<empty>");
        for (size_t off = 0; off < hex.size(); off += 64)
            s.add(k("%s@%05zu", key.c_str(), off / 2), hex.substr(off, 64));
    } else {
        NSString* text = [[[v description] componentsSeparatedByCharactersInSet:
                            [NSCharacterSet newlineCharacterSet]] componentsJoinedByString:@" "];
        s.add(key, text.UTF8String);
    }
}

// The per-OS files the driver and CueMix FX save into.
void addPrefs(Snapshot& s) {
    NSString* dir = [NSHomeDirectory() stringByAppendingPathComponent:@"Library/Preferences/com.motu.PCIAudio"];
    for (NSString* f in [[NSFileManager defaultManager] contentsOfDirectoryAtPath:dir error:nil]) {
        NSDictionary* d = [NSDictionary dictionaryWithContentsOfFile:[dir stringByAppendingPathComponent:f]];
        if (d) addPlist(s, std::string("prefs.") + f.UTF8String, d);
    }
    for (NSString* domain in @[ @"com.motu.CueMixFX", @"com.motu.pci.config.console" ]) {
        NSDictionary* app = [[NSUserDefaults standardUserDefaults] persistentDomainForName:domain];
        if (app) addPlist(s, std::string("defaults.") + domain.UTF8String, app);
    }
}

}  // namespace

@interface Spy : NSObject <NSApplicationDelegate>
@property (strong) NSWindow* window;
@property (strong) NSTextField* label;
@property (strong) NSTextView* log;
@end

@implementation Spy {
    motu::Card _card;
    AudioDeviceID _dev;
    std::map<std::string, std::string> _previous;
    int _count;
}

- (void)applicationDidFinishLaunching:(NSNotification*)n {
    // First CoreAudio touch, on the main thread: the HAL must get this run loop.
    std::string err;
    motu::Card::installRunLoop(&err);
    _dev = motu::Card::findDevice();
    if (_dev) _card = motu::Card::open(_dev, &err);

    self.window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 640, 420)
                                              styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                                        NSWindowStyleMaskResizable
                                                backing:NSBackingStoreBuffered defer:NO];
    self.window.title = @"MotuSpy";
    NSView* v = self.window.contentView;

    self.label = [NSTextField textFieldWithString:@"baseline"];
    self.label.frame = NSMakeRect(12, 382, 480, 24);
    self.label.autoresizingMask = NSViewWidthSizable | NSViewMinYMargin;
    self.label.placeholderString = @"What did you just change?";
    [v addSubview:self.label];

    NSButton* b = [NSButton buttonWithTitle:@"Snapshot" target:self action:@selector(snapshot:)];
    b.frame = NSMakeRect(500, 380, 128, 28);
    b.autoresizingMask = NSViewMinXMargin | NSViewMinYMargin;
    b.keyEquivalent = @"\r";
    [v addSubview:b];

    NSScrollView* sv = [[NSScrollView alloc] initWithFrame:NSMakeRect(12, 12, 616, 360)];
    sv.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    sv.hasVerticalScroller = YES;
    self.log = [[NSTextView alloc] initWithFrame:sv.bounds];
    self.log.editable = NO;
    self.log.font = [NSFont userFixedPitchFontOfSize:11];
    self.log.autoresizingMask = NSViewWidthSizable;
    sv.documentView = self.log;
    [v addSubview:sv];

    [self.window center];
    [self.window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];

    [self say:_card ? @"Connected to PCI-424. Click Snapshot for a baseline.\n"
                    : [NSString stringWithFormat:@"No card: %s\n", err.c_str()]];

    // --auto: two snapshots into ~/Desktop/MotuSpy and quit (a self-test).
    if ([[NSProcessInfo processInfo].arguments containsObject:@"--auto"]) {
        self.label.stringValue = @"auto baseline"; [self snapshot:nil];
        self.label.stringValue = @"auto repeat";   [self snapshot:nil];
        [NSApp terminate:nil];
    }
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)a { return YES; }

- (void)say:(NSString*)text {
    [self.log.textStorage appendAttributedString:[[NSAttributedString alloc] initWithString:text attributes:@{
        NSFontAttributeName: [NSFont userFixedPitchFontOfSize:11] }]];
    [self.log scrollToEndOfDocument:nil];
}

- (void)snapshot:(id)sender {
    if (!_card) { [self say:@"No card.\n"]; return; }
    Snapshot s;
    addDevice(s, _dev);
    addCard(s, _card);
    addCueMix(s, _card);
    addTalkbackAndSMPTE(s, _card);
    addPrefs(s);

    NSString* dir = [NSHomeDirectory() stringByAppendingPathComponent:@"Desktop/MotuSpy"];
    [[NSFileManager defaultManager] createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil];
    if (_count == 0)   // continue numbering after earlier sessions
        for (NSString* f in [[NSFileManager defaultManager] contentsOfDirectoryAtPath:dir error:nil])
            _count = std::max(_count, f.intValue);
    ++_count;

    NSString* label = self.label.stringValue.length ? self.label.stringValue : @"snapshot";
    NSCharacterSet* bad = [[NSCharacterSet alphanumericCharacterSet] invertedSet];
    NSString* slug = [[label componentsSeparatedByCharactersInSet:bad] componentsJoinedByString:@"-"];
    NSString* base = [dir stringByAppendingPathComponent:[NSString stringWithFormat:@"%03d-%@", _count, slug]];

    std::string text = "# " + std::string(label.UTF8String) + "\n";
    std::map<std::string, std::string> now;
    for (auto& [key, value] : s.lines) { text += key + " = " + value + "\n"; now[key] = value; }
    [@(text.c_str()) writeToFile:[base stringByAppendingString:@".txt"] atomically:YES
                        encoding:NSUTF8StringEncoding error:nil];

    std::string diff = "# " + std::string(label.UTF8String) + "\n";
    int changes = 0;
    if (!_previous.empty()) {
        for (auto& [key, value] : now) {
            auto it = _previous.find(key);
            if (it == _previous.end())      { diff += "+ " + key + " = " + value + "\n"; ++changes; }
            else if (it->second != value)   { diff += "~ " + key + " = " + it->second + "  ->  " + value + "\n"; ++changes; }
        }
        for (auto& [key, value] : _previous)
            if (!now.count(key)) { diff += "- " + key + " = " + value + "\n"; ++changes; }
        [@(diff.c_str()) writeToFile:[base stringByAppendingString:@".diff"] atomically:YES
                            encoding:NSUTF8StringEncoding error:nil];
    }
    _previous = std::move(now);

    [self say:[NSString stringWithFormat:@"\n#%03d %@ — %zu values, %d changed\n", _count, label, s.lines.size(), changes]];
    if (changes) [self say:@(diff.substr(diff.find('\n') + 1).c_str())];
    self.label.stringValue = @"";
    [self.window makeFirstResponder:self.label];
}
@end

int main() {
    @autoreleasepool {
        NSApplication* app = [NSApplication sharedApplication];
        Spy* spy = [Spy new];
        app.delegate = spy;
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        [app run];
    }
    return 0;
}
