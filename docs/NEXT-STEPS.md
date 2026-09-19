# Next steps

State at the end of the 2026-09-18 session.

## Where things stand

**MOTU PCI Audio Setup: feature-complete, not yet in daily use.**
- Every control is wired to the call sequence recovered by disassembling MOTU's
  own console (`docs/CHANNEL-STATE.md`):
  - channel checkboxes, bank personalities, Enable Routing (with its "Disable"
    bank item), Enable Volume Controls, Default In/Out;
  - all three Options panes;
  - Save/Load Configuration, Refresh, live refresh from CoreAudio.
- **Edit Channel Names** replaces MOTU's i386 helper. *Import Names…* brought
  all 60 Mojave names across, and the driver saved them to this OS's prefs.
- Stress-tested with no crashes after the stale-Interface fix.
- MOTU's original icon.

**CueMix FX: console live; meters implemented; writes built but opt-in.**
- **Skins:** Classic is pixel-matched to MOTU's, using MOTU's own sprites
  (`assets/classic/`). Modern has the same layout. Switch in the application menu.
- **Live from the card:** strips = active inputs (12 with a lone HD192, up to
  96), MIX = output-pair buses, master fader and mute, fader budget, and
  talkback state.
- **Menu bar identical to MOTU's.** Unbuilt items say in the LCD which stage
  delivers them.
- **Rename a channel** by double-clicking its name in Classic. That writes the
  same custom name PCI Audio Setup's editor does (new; MOTU never had it).
  CueMix re-reads names every second.
- **Every control is interactive** in both skins, as local values the LCD marks
  "not sent". *Revert to Card Values* drops them. `ConsoleModel::setLocal` is
  the future write path.
- **Meters are implemented** in both skins from MOTU's own artwork and
  ballistics (`docs/CUEMIX-API.md`): bar, held peak with the real hold table,
  and the clip/signal LED. *Clear Peaks* and *Peak Hold Time* work.
- **Writes are implemented but off by default** — *Send Changes to Card* in the
  application menu. Nothing in the write path has touched a card yet, so it is
  opt-in per session; with it off the old "not sent" behaviour is unchanged.
- **The fader, pan and trim readouts now use MOTU's own laws**, including the
  40·log10 fader and its 256-step quantization on the way out.
- **With no card the console still runs**, on a stand-in layout with the meters
  driven by a test signal, so the skins can be worked on away from the studio.
- **Placeholders:** Scope audio, the trim knob's range (its min/max are
  per-channel and unread), and the dim-knob range.
- MOTU's original icon.

**Tools**
- **MotuSpy** is on the Mojave Desktop: snapshot every readable card value,
  change one thing in MOTU's app, snapshot again, and it diffs the two.
- `src/motu-spy/bounds.mm` found the CueMix index ranges, and that the
  balance/width/mapping getters segfault past the end.

## 2026-09-18: CueMix decoded from MOTU's own binary

MOTU's current CueMix FX (`/Applications`, 1.6 b5003c51d, Sep 2025) is
**arm64+x86_64, unstripped, and contains the whole PCI back end**
(`CoreDeviceAW.cpp`, ~120 symbols). `docs/CUEMIX-API.md` is the result:

- every CueMix control mapped to its `CueMixAPI` slot, with argument order,
  bounds, out-of-range defaults and MOTU's own source line numbers;
- **a mix is an index 0–47 and the bus id is `2 × mix`** — the MIX popup
  question is answered;
- **fader dB is 40·log10(v/32768), not 20**, and fader values are quantized to
  multiples of 256 (which is why 24320 and 32256 were observed: `0x5F00`,
  `0x7E00`);
- `ReadLevelMeters`' request/result structs, recovered from
  `UpdateLevelMeters`; `src/common/motu_card.h` now has them and the offsets
  check out (request 200 bytes, results 424 = MOTU's `0x1A8` reservation);
- commit semantics: fader/pan/solo/mute write immediately, and `CommitChanges`
  on this path is only "Hardware Follows Console Stereo Settings".

`docs/CUEMIX-OSC.md` covers the OSC side; `tools/osc-log.py` and
`tools/dis-cuemix.sh` are the tools. Corrections were pushed back into
`README.md`, `CUEMIX-PLAN.md` and `ORIGINAL-UI.md`.

**Do not plan around the UltraLite for PCI facts.** Each back end has its own
parameter dispatch, value classes and meter transport: `ValueFaderFX` is
`sqrt`/square where `ValueLegacyFader` is linear-with-256-quantization, and the
half-clip meter state is produced *only* by `CoreDeviceAW` — there is no `0.5`
immediate anywhere in `LevelMeterSubsystemFX`. See "What does not transfer" in
`CUEMIX-API.md`.

### Two things that will bite immediately

**`tools/build.sh` was picking the wrong certificate**, and the symptom was
apps that refused to start: `open` reported
`NSPOSIXErrorDomain Code=162 "Launchd job spawn failed"`. The cause was
`security find-identity -v -p codesigning | head -1` — this keychain holds
several Apple Development certs from different teams, and the one that sorted
first was the **personal** team's (`O=Kieran Coulter`, `OU=<personal-team-id>`, CN
`<personal-apple-id>`). That one does not launch. `-v` also still lists
*revoked* certs, tagged `CSSMERR_TP_CERT_REVOKED`, and the previous Third Eye
cert is revoked.

Fixed: selection moved to **`tools/sign-identity.sh`**, shared by `build.sh`
and `CMakeLists.txt` so the two cannot drift — `CMakeLists.txt` had the same
bug, and it signs the two real JUCE apps. It matches on the certificate's
**organization**, preferring `O=Third Eye Technologies, Inc` (`OU=BNT9H4C5D7`),
skips revoked entries, honours `MOTU_SIGN_ORG` / `MOTU_SIGN_IDENTITY`, and falls
back to ad-hoc. Note the CN is not a reliable selector: the personal-team cert carries
the Apple ID email while the Third Eye cert carries the legal name
(`Apple Development: <LEGAL NAME> (<cert id>)`), both under the same
`UID=<shared-apple-uid>`. No provisioning profile is needed — the Third Eye wildcard
profile on this machine makes no difference either way.

Verified 2026-09-18: Third Eye cert launches, ad-hoc launches, personal-team
cert fails, and **the cdhash is now identical across rebuilds**, so the TCC
microphone grant sticks instead of re-prompting. That is the whole reason to
prefer a certificate over ad-hoc.

**Do not add hardened runtime (`--options runtime`) without also adding
`com.apple.security.cs.disable-library-validation`.** Hardening enables library
validation, and the card only works because MOTU's `HALPlugin` — signed by MOTU,
a different team — is loaded into our process. The failure would present as the
card pointer being NULL again, which is a long way from the cause.

**A pending microphone prompt looks exactly like a hang.** Every tool blocks on
its first CoreAudio call until the dialog is answered, having already created an
empty log file. Confirmed on 2026-09-18 with the known-good `MotuProbe` as a
control. If a probe produces nothing, look for the dialog before debugging.

## Next session: the studio, in this order

The order matters — the first two are cheap and change what the rest is worth.

**1. Does MOTU's own CueMix FX see the card? (~10 min)**
Launch `/Applications/CueMix FX.app` with the PCI-424 present and watch what it
does. `ORIGINAL-UI.md` now rules out, with evidence, every explanation offered so
far: Carbon, notarization, library validation, kext loading, the run-loop trick,
and Gestalt version skew. The app runs fine on Sequoia here driving an UltraLite.
So this is a clean experiment with a real payoff: **if it sees the card, we get a
live oracle beside our app on Sequoia and the Mojave round-trip becomes
optional.** Capture Console output filtered on `CueMix` either way.

**2. Record the facts the decode needs and the binary cannot give (~20 min)**
- `cardType()` → decides 24 vs 48 meters (MOTU's table is `{24,24,48}`).
- `GetInputTrim` values, and where trim's per-channel min/max come from — the
  trim knob cannot be drawn correctly until this is known.
- `GetCueMixResourceUsage`'s middle int.
- Confirm the doubling: read mix *n* and bus *2n* and check they agree, and that
  `AWGetCueMixBusResourceUsage` really takes a raw bus id (it is the one wrapper
  that does not double).

**3. Meters, with signal (~30 min)** — two tools, in this order.

`build/MotuMeters.app` first, because it is the honest test of the decode: it
prints cardType, maxLevelMeters, per-channel level and clip, flags anything
outside 0..32768, and reports whether the 40 unidentified tail bytes ever come
back set.

Then CueMix FX itself, which now *draws* meters from that same data. Compare it
side by side with MOTU's console if step 1 went well. Two things to watch:

- **the decay speed.** MOTU's 0.015-per-update is per *update*, and its update
  rate is not in the binary, so ours may drift fast or slow. `kPeakDecayPerTick`
  in `src/cuemix-fx/ConsoleModel.cpp` is the knob.
- **the clip LED.** `docs/CUEMIX-API.md` argues 1 = clipped and 2 = signal
  present, from the three-frame indicator and the 1.0/0.5/0.0 mapping. Clip
  something deliberately and check the LED does not read backwards.

```sh
tools/build.sh MotuMeters src/common/motu_card.mm src/common/motu_meters.mm
MOTU_METERS_SECONDS=20 MOTU_METERS_BUS=0 open build/MotuMeters.app
# answer the microphone prompt, then:
cat /tmp/motu-meters.txt
```

**4. First writes (~30 min)** — the code is written; this is about turning it
on carefully. Read first, confirm the console matches MOTU's, *then* enable
**Send Changes to Card** and move one control on one strip of one mix.

The write path drops its local override as soon as the card accepts, so a
control that snaps back is telling you the write did not land. Confirm three
ways: our readback, a `MotuSpy` diff, and by ear. Start with a mute — it is
audible, unambiguous and trivially reversible — then a fader, then pan and solo.

Stereo is deliberately still not written: it is the one parameter that needs
`CommitChanges` and MOTU's pair-mirroring.

**5. OSC against the card, if time (~20 min)** — `tools/osc-log.py` is written
and tested end to end. `osc-log.py prefs` will show the PCI engine key;
`listen --advertise` then one pass through *Configure OSC Devices…* starts the
stream. The cheapest confirmation of the value laws is reading a `/cdf` and its
`/cdf/str` sibling together: if 16384 prints as `-12.0 dB`, the 40·log10 law is
confirmed by MOTU's own formatter.

### Not worth a trip

**Big Sur.** Checked read-only while it was mounted: that volume never had
`MOTUPCIAudio.kext` at all, and its CueMix prefs reference only the UltraLite
(`com_motu_driver_FWA_Engine:00000d2f76`). CueMix FX on Big Sur was driving the
UltraLite, which is what it is doing on Sequoia now. There is no PCI capability
there to restore and nothing we could have broken in that path. Details in
`ORIGINAL-UI.md`.

**Windows 10.** A macOS driver; nothing to learn.

## Small known issues

- The Modern skin can open scrolled a few strips in.
- The dim-knob range (0–255) and the "Sequencer using N faders" figure are
  guesses.
- `ORIGINAL-UI.md` predates the corrections in `CHANNEL-STATE.md`.

## Later

- Put PCI Audio Setup into daily use, including at the HD192-only studio, then
  install both apps to `/Applications` once trusted.
- CueMix stages 3–8: meters, talkback writes, configurations, analysis windows,
  control surfaces, and Modern skin polish.

## Still to capture from Mojave

Much less than before: the slots, argument order, value laws and meter structs
all came out of MOTU's own current binary instead (`docs/CUEMIX-API.md`), so
MotuSpy drops from the instrument of discovery to a confirmation tool.

- Confirmation diffs for the writes in step 4, if MOTU's CueMix FX turns out not
  to see the card on Sequoia (step 1 decides this).
- The `CueMixSettings` blob's layout, which is still only visible as a diff.

## Build

```sh
git submodule update --init --recursive
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
  cmake -B build/cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release
DEVELOPER_DIR=/Library/Developer/CommandLineTools cmake --build build/cmake -j8
open "build/cmake/src/pci-audio-setup/PCIAudioSetup_artefacts/Release/MOTU PCI Audio Setup.app"
open "build/cmake/src/cuemix-fx/CueMixFX_artefacts/Release/CueMix FX.app"
```

`DEVELOPER_DIR` is required: Xcode's licence is unaccepted, so CMake must be
pointed at the Command Line Tools toolchain.

Back-end regression check, any time the card seems wrong:

```sh
tools/build.sh MotuDump  src/common/motu_card.mm src/common/motu_dump.mm  --run
tools/build.sh MotuProbe src/common/motu_card.mm src/common/motu_probe.mm && \
  open build/MotuProbe.app && cat /tmp/motu-probe.txt
```

### Code signing

> **Updated 2026-09-18.** The certificate route is the right one and is back in
> use, but the identity must be chosen by organization, not by list position —
> see "Two things that will bite immediately" above. No provisioning profile is
> required.

Ad-hoc signing gives every build a new cdhash, so TCC treats each rebuild as a
new app and re-prompts for the microphone. `tools/build.sh` and `motu_sign()`
use the `Third Eye Technologies` Apple Development identity when the keychain
has a non-revoked one, and fall back to ad-hoc otherwise.

A grant made by an *earlier* build is stored with that build's requirement,
so a correctly signed build still gets *"Failed to match existing code
requirement"* in the `com.apple.TCC` log and prompts once more. Allowing it then
rewrites the grant against the certificate. MotuProbe was re-granted on
2026-09-12, and a byte-identical relaunch then went through without a prompt.
MotuDump and the PCI Audio Setup app may each prompt one more time. If one keeps
prompting after that, check the log:

```sh
log show --last 10m --style compact \
  --predicate 'subsystem == "com.apple.TCC" AND eventMessage CONTAINS "zenbox"'
```

If `security find-identity -v -p codesigning` reports **0 valid identities**
while an `Apple Development` cert is present, the WWDR intermediate has expired
(the 2013 one died 2023-02-07). Install the current G3:

```sh
curl -O https://www.apple.com/certificateauthority/AppleWWDRCAG3.cer
security import AppleWWDRCAG3.cer -k ~/Library/Keychains/login.keychain-db
```

## Open questions

- The `+ 2` in the console's MB/sec formula (confirmed in MOTU's code, meaning unknown).
- `GetPCIUsage` returns −1/−1 on this card; may be FireWire-only.
- `GetCueMixResourceUsage`'s middle int, and whether bus 0's `usage=22` is the same count.
