// motu-dump — snapshot everything the two replacement apps need to render.
//
// Doubles as the regression test for src/common/motu_card.*: if the card ever
// stops answering, this is the first thing to run.
//
// Must be a bundled .app (see tools/build.sh); a bare CLI never gets the card
// pointer. Writes to stdout and to /tmp/motu-dump.txt, because a bundled app
// launched with `open` has nowhere useful to put stdout.

#import <Cocoa/Cocoa.h>

#include "motu_card.h"

#include <cstdio>

static FILE* gLog = nullptr;
static void P(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static void P(const char* fmt, ...) {
    va_list a;
    va_start(a, fmt); vfprintf(stdout, fmt, a); va_end(a);
    if (gLog) { va_start(a, fmt); vfprintf(gLog, fmt, a); va_end(a); fflush(gLog); }
}

static void dumpAll() {
    gLog = fopen("/tmp/motu-dump.txt", "w");

    std::string err;
    if (!motu::Card::installRunLoop(&err)) { P("FATAL: %s\n", err.c_str()); return; }

    AudioDeviceID dev = motu::Card::findDevice();
    P("== CoreAudio device ==\n");
    P("  deviceID            %u\n", dev);
    if (!dev) { P("FATAL: PCI-424 not present\n"); return; }

    P("  sample rate         %.0f Hz\n", motu::device::sampleRate(dev));
    P("  available rates    ");
    for (double r : motu::device::availableSampleRates(dev)) P(" %.0f", r);
    P("\n");
    UInt32 cs = motu::device::clockSource(dev);
    P("  clock source        %u (%s)\n", cs, motu::device::clockSourceName(dev, cs).c_str());
    for (UInt32 s : motu::device::clockSources(dev))
        P("    %-10u %s\n", s, motu::device::clockSourceName(dev, s).c_str());

    motu::Card card = motu::Card::open(dev, &err);
    if (!card) { P("FATAL: %s\n", err.c_str()); return; }
    P("\n== AudioWireCard @ %p ==\n", card.raw());

    motu::Exception e;
    P("  gestalt(14)         %d\n", card.gestalt(e, 14));
    P("  card type           0x%08x\n", card.cardType(e));
    P("  SMUX capable        %d   setting %d\n", card.smuxCapable(e), card.smuxSetting(e));

    int wires = card.numWires(e);
    P("\n== Interfaces (%d wires) ==\n", wires);
    for (int w = 0; w < wires; ++w) {
        bool up = card.wireConnected(e, w);
        P("  wire %d  connected=%d  %s\n", w, up, up ? card.wireInterfaceName(e, w).c_str() : "-");
        if (!up) continue;
        motu::Interface itf = card.wireInterface(e, w);
        if (!itf) { P("      (no interface object)\n"); continue; }
        P("      id=%d  version=\"%s\"\n", itf.id(e), itf.versionString(e).c_str());
        int banks = itf.numBanks(e);
        for (int b = 0; b < banks; ++b) {
            P("      bank %d: %d ch, personality %d  [", b,
              itf.numChannelsInBank(e, b), itf.personalityForBank(e, b));
            for (int n = 0; n < 8; ++n) {
                std::string p = itf.nthBankPersonality(e, b, n);
                if (p.empty()) break;
                P("%s%s", n ? ", " : "", p.c_str());
            }
            P("]\n");
        }
    }

    int nIn = card.numInputs(e), nAIn = card.numActiveInputs(e);
    int nOut = card.numOutputs(e), nAOut = card.numActiveOutputs(e);
    P("\n== Channels ==\n");
    P("  inputs  %d total, %d active\n", nIn, nAIn);
    P("  outputs %d total, %d active\n", nOut, nAOut);

    P("\n  active inputs:\n");
    for (int n = 0; n < nAIn; ++n) {
        int id = card.nthActiveInputID(e, n);
        P("    [%2d] id=%-3d bank=%-3d  %-28s custom=\"%s\"\n", n, id,
          card.bankRelativeID(e, id), card.inputDescription(e, id).c_str(),
          card.channelName(e, id, true).c_str());
    }
    P("\n  active outputs:\n");
    for (int n = 0; n < nAOut; ++n) {
        int id = card.nthActiveOutputID(e, n);
        P("    [%2d] id=%-3d bank=%-3d  %-28s custom=\"%s\"\n", n, id,
          card.bankRelativeID(e, id), card.outputDescription(e, id).c_str(),
          card.channelName(e, id, false).c_str());
    }

    P("\n== Sub-APIs ==\n");
    motu::CueMix cue = card.cueMix(e);
    motu::SMPTE smpte = card.smpte(e);
    motu::Talkback tb = card.talkback(e);
    P("  CueMix %p   SMPTE %p   Talkback %p\n", cue.raw(), smpte.raw(), tb.raw());

    if (cue) {
        auto r = cue.resources(e);
        auto u = cue.pciUsage(e);
        P("\n  CueMix: %d faders used, %d unidentified, %d max;  PCI usage %d/%d\n",
          r.used, r.unidentified, r.max, u.a, u.b);
    }
    if (smpte) {
        P("\n  SMPTE: rate=%d format=%d source=%d dest=%d genMode=%d outLevel=%d\n",
          smpte.frameRate(e), smpte.frameFormat(e), smpte.source(e),
          smpte.destination(e), smpte.generationMode(e), smpte.outputLevel(e));
        P("         freewheel address=%d clock=%d\n",
          smpte.freewheelAddressTime(e), smpte.freewheelClockTime(e));
    }
    if (tb) {
        P("\n  Talkback: in=%d dim=%d enable=%d link=%d\n",
          tb.talkbackInput(e), tb.talkbackDimLevel(e), tb.talkbackEnable(e), tb.talkbackLink(e));
        P("  Listenback: in=%d dim=%d enable=%d\n",
          tb.listenbackInput(e), tb.listenbackDimLevel(e), tb.listenbackEnable(e));
    }

    // Confirm the recovered exception layout: distinct out-of-range calls should
    // report distinct source lines in the driver, and a valid call none at all.
    P("\n== MOTUException probe ==\n");
    struct Probe { const char* what; void (^run)(motu::Exception&); };
    motu::Card c = card;
    Probe probes[] = {
        { "GetNumWires()            [valid]", ^(motu::Exception& x){ c.numWires(x); } },
        { "GetInputDescription(9999)",        ^(motu::Exception& x){ c.inputDescription(x, 9999); } },
        { "GetOutputDescription(9999)",       ^(motu::Exception& x){ c.outputDescription(x, 9999); } },
        { "IsWireConnected(99)",              ^(motu::Exception& x){ c.wireConnected(x, 99); } },
        // Deliberately NOT probed: GetWireInterfaceName() with an out-of-range
        // wire segfaults the process instead of raising. Range checking is the
        // caller's job — see the warning in motu_card.h.
    };
    for (const auto& pr : probes) {
        motu::Exception x;
        pr.run(x);
        P("  %-34s %s\n", pr.what, x.str().c_str());
        if (x.raised()) P("  %-34s   raw %s\n", "", x.hex().c_str());
    }

    P("\nDONE\n");
    if (gLog) fclose(gLog);
}

@interface Delegate : NSObject <NSApplicationDelegate>
@end
@implementation Delegate
- (void)applicationDidFinishLaunching:(NSNotification*)n {
    // First CoreAudio touch in the process happens here, on the main thread,
    // so the HAL plugin captures the main run loop.
    dispatch_async(dispatch_get_main_queue(), ^{
        dumpAll();
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
