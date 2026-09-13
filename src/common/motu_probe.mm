// motu-probe — decode the two channel-state calls and the per-interface
// options call, by reading the card and comparing against MOTU's own UI.
//
// The reference is the screenshot set in docs/reference/: at the moment those
// were taken the console read "PCI Use: Ins enabled 60, Outs enabled 60" with
// the 2408mk3 fully unchecked. Whatever InputState/OutputState mean, the count
// of enabled channels has to agree with that line for the same card state.
//
// Writes /tmp/motu-probe.txt line-buffered, because OtherInterfaceOp is probed
// with selectors MOTU never published and an unknown selector may take the
// process down (GetWireInterfaceName(99) already does).

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

static void probe() {
    gLog = fopen("/tmp/motu-probe.txt", "w");

    std::string err;
    if (!motu::Card::installRunLoop(&err)) { P("FATAL: %s\n", err.c_str()); return; }
    AudioDeviceID dev = motu::Card::findDevice();
    if (!dev) { P("FATAL: PCI-424 not present\n"); return; }
    motu::Card card = motu::Card::open(dev, &err);
    if (!card) { P("FATAL: %s\n", err.c_str()); return; }

    motu::Exception e;
    const int nIn  = card.numInputs(e);
    const int nOut = card.numOutputs(e);

    // The driver's own idea of what is active, to check our decode against.
    P("== driver counts ==\n");
    P("  numInputs %d   numActiveInputs %d\n", nIn, card.numActiveInputs(e));
    P("  numOutputs %d   numActiveOutputs %d\n", nOut, card.numActiveOutputs(e));

    P("\n== every channel, both directions ==\n");
    P("  id   in.exists in.enabled  out.exists out.source   description\n");
    int inEnabled = 0, outEnabled = 0, inAvail = 0;
    for (int id = 0; id < nIn; ++id) {
        motu::Exception ei, eo;
        auto in  = card.inputState(ei, id);
        auto out = card.outputState(eo, id);
        if (in.enabled)  ++inEnabled;
        if (in.exists)   ++inAvail;
        if (out.enabled()) ++outEnabled;
        P("  %3d      %3u      %3u        %3u     %6d   %s%s%s\n",
          id, in.exists, in.enabled, out.exists, out.source,
          card.inputDescription(e, id).c_str(),
          ei.raised() ? "  IN-EXC " : "", eo.raised() ? "  OUT-EXC " : "");
    }
    P("\n  totals: in.enabled=%d  in.exists=%d  out.enabled=%d\n",
      inEnabled, inAvail, outEnabled);
    P("  MOTU console at screenshot time read: Ins enabled 60, Outs enabled 60\n");

    // OtherInterfaceOp, read-only (isSet=false), one interface at a time.
    // Selector numbering is unpublished; this is the map for the Options panes.
    P("\n== OtherInterfaceOp(get) selector sweep ==\n");
    const int wires = card.numWires(e);
    for (int w = 0; w < wires; ++w) {
        if (!card.wireConnected(e, w)) continue;
        auto name = card.wireInterfaceName(e, w);
        motu::Interface itf = card.wireInterface(e, w);
        if (!itf) continue;
        P("\n  wire %d  %s\n", w, name.c_str());
        // Read-only. A selector the interface does not implement raises
        // "Couldn't find property in OtherInterfaceOp", which is exactly how we
        // learn which Options controls belong on which interface's pane.
        static const char* kNames[] = {
            "AnalogMirror", "AESOutputSRCMode", "AESInputSteal", "AESOutputClock",
            "AESInputSRC", "PeakHoldTime", "ClipHoldTime", "InputLevels",
            "WordOutRange" };
        for (int sel = 0; sel <= 8; ++sel) {
            motu::Exception se;
            int v = 0x5A5A5A5A;   // sentinel: proves the driver really wrote it
            const bool ok = itf.getOption(se, (motu::Interface::Option)sel, v);
            if (ok && v != 0x5A5A5A5A)
                P("    %-17s = %d  (0x%x)\n", kNames[sel], v, (unsigned)v);
            else if (ok)
                P("    %-17s   no exception but out-param UNTOUCHED\n", kNames[sel]);
            else
                P("    %-17s   -- not on this interface (%s)\n", kNames[sel], se.str().c_str());
        }
    }
    P("\nDONE\n");
}

int main() {
    @autoreleasepool {
        [NSApplication sharedApplication];
        probe();
    }
    return 0;
}
