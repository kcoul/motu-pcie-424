// motu-bounds — find the index ranges MOTU's CueMix / Talkback getters accept,
// before MotuSpy sweeps them on Mojave. Getters only; nothing is written.
//
// Some MOTU getters segfault on a bad index instead of raising, so every call
// is announced in /tmp/motu-bounds.txt (flushed) *before* it is made: if the
// process dies, the last line names the call that killed it.

#import <Cocoa/Cocoa.h>

#include "motu_card.h"

#include <cstdarg>
#include <cstdio>
#include <functional>
#include <vector>

static FILE* gLog = nullptr;
static void P(const char* fmt, ...) {
    va_list a;
    va_start(a, fmt); vfprintf(gLog, fmt, a); va_end(a);
    fflush(gLog);
}

// Call f(i) for i = 0.. until it raises, up to `limit`. Returns the count that
// succeeded; prints each exception boundary.
static int sweep(const char* name, int limit, const std::function<int(motu::Exception&, int)>& f) {
    int ok = 0, firstBad = -1;
    std::string bad;
    for (int i = 0; i < limit; ++i) {
        P("  call %s(%d)\r", name, i);
        motu::Exception e;
        f(e, i);
        if (e.raised()) { if (firstBad < 0) { firstBad = i; bad = e.str(); } }
        else if (firstBad < 0) ++ok;
        else { P("\n  %s: gap — %d raises but %d succeeds\n", name, firstBad, i); break; }
    }
    P("\n  %-34s ok 0..%d%s%s\n", name, ok - 1, firstBad >= 0 ? "  first raise: " : "", bad.c_str());
    return ok;
}

static void run() {
    gLog = fopen("/tmp/motu-bounds.txt", "w");
    std::string err;
    motu::Card::installRunLoop(&err);
    AudioDeviceID dev = motu::Card::findDevice();
    motu::Card card = motu::Card::open(dev, &err);
    if (!card) { P("FATAL %s\n", err.c_str()); return; }
    motu::Exception e;
    motu::CueMix cue = card.cueMix(e);
    motu::Talkback tb = card.talkback(e);
    P("numInputs %d active %d, numOutputs %d active %d\n", card.numInputs(e), card.numActiveInputs(e),
      card.numOutputs(e), card.numActiveOutputs(e));
    for (int g = 0; g < 4; ++g) P("gestalt(%d) = %d\n", g, card.gestalt(e, g));

    // Which bus numbers the range-checked getters accept, and how they line
    // up with output ids. Stop well short of 150, where the unchecked
    // balance/width getters were seen to segfault.
    P("\n== valid buses (busVolume raises or not), 0..127 ==\n");
    std::vector<int> valid;
    for (int b = 0; b < 128; ++b) {
        motu::Exception x;
        cue.busVolume(x, b);
        if (!x.raised()) valid.push_back(b);
    }
    int buses = (int)valid.size();
    for (int b : valid) {
        motu::Exception x;
        const auto st = card.outputState(x, b);
        P("  bus %-3d  out.exists=%d out.source=%-3d  %-24s vol=%d mute=%d solo=%d usage=%d\n", b, st.exists,
          st.source, card.outputDescription(x, b).c_str(), cue.busVolume(x, b), (int)cue.busMute(x, b),
          (int)cue.busSoloed(x, b), cue.busResourceUsage(x, b));
    }
    P("\n== bus 0, strips 0..95: volume pan mute solo res | bal width pref ==\n");
    for (int c = 0; c < 96; ++c) {
        motu::Exception x;
        const auto in = card.inputState(x, c);
        if (!in.exists) continue;
        P("  ch %-3d en=%d  vol=%-6d pan=%-6d m=%d s=%d r=%d | bal=%d w=%d pref=%d map=%d trim=%d imute=%d\n", c,
          in.enabled, cue.volume(x, 0, c), cue.pan(x, 0, c), (int)cue.mute(x, 0, c), (int)cue.solo(x, 0, c),
          (int)cue.faderHasResources(x, 0, c), cue.inputBalance(x, 0, c), cue.inputWidth(x, 0, c),
          cue.inputBalanceWidthPref(x, 0, c), cue.inputChannelMapping(x, c), cue.inputTrim(x, c),
          (int)cue.inputMute(x, c));
    }
    P("\n== Talkback ==\n");
    P("  talk in=%d dim=%d en=%d link=%d | listen in=%d dim=%d en=%d\n", tb.talkbackInput(e),
      tb.talkbackDimLevel(e), tb.talkbackEnable(e), tb.talkbackLink(e), tb.listenbackInput(e),
      tb.listenbackDimLevel(e), tb.listenbackEnable(e));
    sweep("talkbackOutput(n)",   96, [&](motu::Exception& x, int i) { return tb.talkbackOutput(x, i); });
    sweep("listenbackOutput(n)", 96, [&](motu::Exception& x, int i) { return tb.listenbackOutput(x, i); });
    P("\nbuses=%d\nDONE\n", buses);
}

int main() {
    @autoreleasepool {
        [NSApplication sharedApplication];
        run();
    }
    return 0;
}
