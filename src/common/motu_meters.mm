// motu-meters — exercise ReadLevelMeters (CueMix slot 21) against the card.
//
// This is the first code to use the request/result layouts recovered from
// CoreDeviceAW::UpdateLevelMeters in MOTU's own 2025 CueMix FX. Nothing here
// has been confirmed against a PCI-424 yet, so the point of the run is as much
// to test the decode as to read levels. docs/CUEMIX-API.md has the derivation.
//
// What to look for, in order:
//
//   1. cardType and maxLevelMeters. MOTU picks 24 / 24 / 48 from a table
//      indexed by cardType; nobody has seen which row a PCIe-424 takes.
//   2. Do levels move with signal, and do they sit in 0..32768? That range is
//      the whole basis of the meter law. A number far outside it means the
//      result struct is wrong.
//   3. Which clip values actually occur. The console turns 1 into a full
//      indication and 2 into a half one; which is the held state and which the
//      instantaneous one cannot be told from the binary. Feed something that
//      clips and watch the order they appear in.
//   4. Whether the 40 tail bytes ever come back non-zero. MOTU zeroes them and
//      never reads them, so anything appearing there is undocumented output.
//
// Usage:  open build/MotuMeters.app         (writes /tmp/motu-meters.txt)
//         MOTU_METERS_BUS=4 open ...        (mix index 2 -> bus 4)
//         MOTU_METERS_SECONDS=20 open ...

#import <Cocoa/Cocoa.h>

#include "motu_card.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <vector>

static FILE* gLog = nullptr;
static void P(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static void P(const char* fmt, ...) {
    va_list a;
    va_start(a, fmt); vfprintf(stdout, fmt, a); va_end(a);
    if (gLog) { va_start(a, fmt); vfprintf(gLog, fmt, a); va_end(a); fflush(gLog); }
}

static int envInt(const char* name, int fallback) {
    const char* v = getenv(name);
    if (!v || !*v) return fallback;
    return atoi(v);
}

// The meter law from ValueLegacyLevelMeter: ui = raw / 32768. Plain amplitude,
// so plain 20*log10 -- NOT the fader's 40*log10 (see docs/CUEMIX-API.md).
static double meterDb(int raw) {
    if (raw <= 0) return -INFINITY;
    return 20.0 * std::log10(static_cast<double>(raw) / 32768.0);
}

static void run() {
    gLog = fopen("/tmp/motu-meters.txt", "w");
    // Log before touching CoreAudio: the first call triggers the microphone
    // permission prompt, and if nobody clicks it the process just sits there.
    // A run that stops after this line means the prompt is waiting on screen.
    P("motu-meters starting\n");

    std::string err;
    if (!motu::Card::installRunLoop(&err)) { P("FATAL: %s\n", err.c_str()); return; }
    AudioDeviceID dev = motu::Card::findDevice();
    if (!dev) { P("FATAL: PCI-424 not present\n"); return; }
    motu::Card card = motu::Card::open(dev, &err);
    if (!card) { P("FATAL: %s\n", err.c_str()); return; }

    motu::Exception e;
    motu::CueMix cm = card.cueMix(e);
    if (!cm) { P("FATAL: no CueMix API (%s)\n", e.str().c_str()); return; }

    const unsigned type = card.cardType(e);
    const int maxMeters = card.maxLevelMeters(e);
    P("cardType        = %u%s\n", type, e.raised() ? "  (raised!)" : "");
    P("maxLevelMeters  = %d   (from MOTU's { 24, 24, 48 } table)\n", maxMeters);

    // Strips are the active inputs, exactly as the console builds them.
    std::vector<unsigned> chans;
    const int nActive = card.numActiveInputs(e);
    for (int i = 0; i < nActive && (int)chans.size() < maxMeters; ++i) {
        const int id = card.nthActiveInputID(e, i);
        if (e.raised()) { P("nthActiveInputID(%d) raised: %s\n", i, e.str().c_str()); break; }
        chans.push_back((unsigned)id);
    }
    P("active inputs   = %d, metering %zu of them\n", nActive, chans.size());
    if (chans.empty()) { P("nothing to meter\n"); return; }

    motu::CueMix::LevelMeterRequest req;
    req.bus = (std::uint32_t)envInt("MOTU_METERS_BUS", 0);
    req.numChannels = (std::uint32_t)chans.size();
    for (size_t i = 0; i < chans.size(); ++i) req.channels[i] = chans[i];
    P("bus             = %u  (mix index %u)\n", req.bus, req.bus / 2);
    P("request size    = %zu bytes (expected 200)\n", sizeof req);
    P("results size    = %zu bytes\n", sizeof(motu::CueMix::LevelMeterResults));

    for (size_t i = 0; i < chans.size(); ++i)
        P("  ch %2u  %s\n", chans[i], card.inputDescription(e, (int)chans[i]).c_str());

    const int seconds = envInt("MOTU_METERS_SECONDS", 10);
    const int hz = 20;
    P("\npolling %d Hz for %d s -- feed signal now, and clip something\n\n", hz, seconds);

    std::vector<int> peak(chans.size(), 0);
    std::set<int> clipValues;
    bool tailEverSet = false, everRaised = false;
    int reads = 0, outOfRange = 0;

    for (int frame = 0; frame < seconds * hz; ++frame) {
        motu::CueMix::LevelMeterResults res;
        cm.readLevelMeters(e, req, &res);
        ++reads;
        if (e.raised()) {
            if (!everRaised) P("readLevelMeters raised: %s\n", e.str().c_str());
            everRaised = true;
            break;
        }

        for (size_t i = 0; i < chans.size(); ++i) {
            const int v = res.level[i];
            if (v < 0 || v > 32768) ++outOfRange;
            if (v > peak[i]) peak[i] = v;
            if (res.clip[i]) clipValues.insert(res.clip[i]);
        }
        for (unsigned char b : res.tail) if (b) { tailEverSet = true; break; }

        // One line a second, so the log stays readable but movement is visible.
        if (frame % hz == 0) {
            P("t=%2ds ", frame / hz);
            for (size_t i = 0; i < chans.size() && i < 12; ++i)
                P("%6d%s", res.level[i], res.clip[i] ? "*" : " ");
            P("\n");
        }
        usleep(1000000 / hz);
    }

    P("\n--- summary over %d reads ---\n", reads);
    for (size_t i = 0; i < chans.size(); ++i) {
        const double db = meterDb(peak[i]);
        P("  ch %2u  peak %6d  %s\n", chans[i], peak[i],
          std::isinf(db) ? "-inf dB" : [&]{ static char b[32];
              std::snprintf(b, sizeof b, "%+.1f dB", db); return b; }());
    }
    P("\nlevels outside 0..32768 : %d%s\n", outOfRange,
      outOfRange ? "   <- RESULT STRUCT IS PROBABLY WRONG" : "   (consistent with the decode)");
    P("clip values observed    : ");
    if (clipValues.empty()) P("none (nothing clipped)\n");
    else { for (int v : clipValues) P("%d ", v); P("\n"); }
    P("tail bytes ever set     : %s\n", tailEverSet ? "YES -- undocumented output, dump it" : "no");
    P("\nWrite the answers into docs/CUEMIX-API.md (\"Still needs the card\").\n");
}

// Same shape as motu_dump.mm, and for the same reason: the first CoreAudio
// touch in the process has to happen on the main thread with the main run loop
// actually running, or the HAL plugin registers against the wrong one and the
// card pointer stays NULL. Doing the work straight out of main() before
// [NSApp run] hangs instead.
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
