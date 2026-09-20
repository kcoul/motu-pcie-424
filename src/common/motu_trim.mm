// motu-trim — read GetInputTrim across every input on the card.
//
// Strictly read-only. SetInputTrim is deliberately never called: this runs on a
// working studio rig, and trim is part of its gain staging.
//
// The open question (docs/CUEMIX-API.md, "Trim — the range is per channel") is
// that ValueLegacyTrim scales against a min/max pair held on the Value object,
// not on the card, and nobody has traced where those come from for an AudioWire
// channel. This tool supplies the other half of that: what the card itself
// actually reports, per channel, on a real rig.
//
// What to look for:
//
//   1. Is every channel the same value? A uniform 64 everywhere suggests a
//      default nobody has moved, and tells us nothing about the range.
//   2. Do different interfaces report differently? HD192 / 24I/O / 2408mk3 have
//      different analogue front ends, so a per-interface pattern would say the
//      range is a property of the interface, not the channel.
//   3. Does the getter raise past the end of the active set, and where?
//
// Usage:  open build/MotuTrim.app          (writes /tmp/motu-trim.txt)

#import <Cocoa/Cocoa.h>

#include "motu_card.h"

#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

static FILE* gLog = nullptr;
static void P(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static void P(const char* fmt, ...) {
    va_list a;
    va_start(a, fmt); vfprintf(stdout, fmt, a); va_end(a);
    if (gLog) { va_start(a, fmt); vfprintf(gLog, fmt, a); va_end(a); fflush(gLog); }
}

static void run() {
    gLog = fopen("/tmp/motu-trim.txt", "w");
    P("motu-trim starting\n");

    std::string err;
    if (!motu::Card::installRunLoop(&err)) { P("FATAL: %s\n", err.c_str()); return; }
    AudioDeviceID dev = motu::Card::findDevice();
    if (!dev) { P("FATAL: PCI-424 not present\n"); return; }
    motu::Card card = motu::Card::open(dev, &err);
    if (!card) { P("FATAL: %s\n", err.c_str()); return; }

    motu::Exception e;
    motu::CueMix cm = card.cueMix(e);
    if (!cm) { P("FATAL: no CueMix API (%s)\n", e.str().c_str()); return; }

    const int nTotal  = card.numInputs(e);
    const int nActive = card.numActiveInputs(e);
    P("inputs: %d total, %d active\n\n", nTotal, nActive);

    // Active channels first, with their names, since those are the ones with
    // gear on them and the ones whose trim anyone would ever touch.
    P("--- active inputs ---\n");
    std::map<int, std::set<int>> byValue;          // trim value -> channel ids
    std::map<std::string, std::set<int>> byIface;  // interface  -> trim values

    for (int i = 0; i < nActive; ++i) {
        const int ch = card.nthActiveInputID(e, i);
        if (e.raised()) { P("nthActiveInputID(%d) raised: %s\n", i, e.str().c_str()); break; }

        const int trim = cm.inputTrim(e, ch);
        if (e.raised()) {
            P("  ch %2d  GetInputTrim raised: %s\n", ch, e.str().c_str());
            e.reset();
            continue;
        }

        const std::string desc = card.inputDescription(e, ch);
        const std::string name = card.channelName(e, ch, true);
        P("  ch %2d  trim %4d   %-28s %s\n", ch, trim, desc.c_str(), name.c_str());

        byValue[trim].insert(ch);
        const std::string iface = desc.substr(0, desc.find(':'));
        byIface[iface].insert(trim);
    }

    // Then the inactive tail, values only -- if the getter answers for channels
    // with nothing patched to them, that is worth knowing before we trust it.
    P("\n--- inactive inputs (values only) ---\n");
    int inactiveRaised = 0, inactiveRead = 0;
    std::set<int> activeIds;
    for (int i = 0; i < nActive; ++i) {
        const int ch = card.nthActiveInputID(e, i);
        if (!e.raised()) activeIds.insert(ch);
        e.reset();
    }
    for (int ch = 0; ch < nTotal; ++ch) {
        if (activeIds.count(ch)) continue;
        const int trim = cm.inputTrim(e, ch);
        if (e.raised()) { ++inactiveRaised; e.reset(); continue; }
        ++inactiveRead;
        byValue[trim].insert(ch);
    }
    P("  %d answered, %d raised\n", inactiveRead, inactiveRaised);

    P("\n--- distinct trim values across the card ---\n");
    for (const auto& [v, chans] : byValue) {
        P("  trim %4d  on %2zu channel%s: ", v, chans.size(), chans.size() == 1 ? "" : "s");
        int printed = 0;
        for (int c : chans) { if (printed++ == 12) { P("..."); break; } P("%d ", c); }
        P("\n");
    }

    P("\n--- by interface (active only) ---\n");
    for (const auto& [iface, vals] : byIface) {
        P("  %-12s ", iface.c_str());
        for (int v : vals) P("%d ", v);
        P("\n");
    }

    P("\n");
    if (byValue.size() == 1)
        P("Every channel reports the same value. That is a default, not a range:\n"
          "the min/max still have to come from MOTU's console or its binary.\n");
    else
        P("Values differ across channels -- compare against what MOTU's own\n"
          "console shows for the same channels to pin the scaling.\n");
}

// Same shape as the other probes: first CoreAudio touch must be on the main
// thread with the main run loop running, or the card pointer stays NULL.
@interface Delegate : NSObject <NSApplicationDelegate>
@end
@implementation Delegate
- (void)applicationDidFinishLaunching:(NSNotification*)n {
    dispatch_async(dispatch_get_main_queue(), ^{
        run();
        [NSApp terminate:nil];
    });
}
@end

int main() {
    @autoreleasepool {
        NSApplication* app = [NSApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyAccessory];
        Delegate* d = [Delegate new];
        [app setDelegate:d];
        [app run];
    }
    return 0;
}
