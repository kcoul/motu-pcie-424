# CueMix FX's OSC interface

App-level, not card-level: OSC lives in `DeviceListManager` / `DeviceController`,
not in any hardware back end, so **it works on a PCI-424 as well as on a
FireWire/USB interface**. `ORIGINAL-UI.md`'s PCI menu capture shows *Configure
OSC Devices…* present. Card-level findings are in `CUEMIX-API.md`.

Everything below was verified against CueMix FX 1.6 b5003c51d running on macOS
Sequoia 15.7.4 with an UltraLite mk3 Hybrid. The tool is `tools/osc-log.py`.

## The model

**CueMix FX is the server.** It advertises itself on Bonjour:

```
$ tools/osc-log.py discover
  MOTU UltraLite mk3 Hybrid CueMix FX OSC on zendevs-iMac.lan
      CueMix FX server
      -> zendevs-iMac.local. port 63759
```

The service type is `_osc._udp`; the instance name is
`<device name> CueMix FX OSC on <host>`; the port is the `OSCUDPPort` pref.

**It also browses `_osc._udp` for clients** (`OSCZeroConf::DoTimerCallback`,
`GetClientList`) and lists what it finds under *Control Surfaces > Configure OSC
Devices…*.

**A client is identified by its Bonjour service name, not by an address.**
`OSCServer::AddOSCClient(IpEndpointName, protocol, name, FileSpecification, …)`
stores the name plus a **layout description file**, and
`OSCServer::ResolveIpEndpoint` looks the name up again at send time.

**There is no wire handshake.** Unsolicited messages get no reply — verified:
`osc-log.py probe` sends eight plausible addresses to the live server and gets
silence on all of them. Until a client has been added in that dialog, nothing is
pushed. The choice persists in preferences, so it is one-time per machine.

So the sequence is:

1. `tools/osc-log.py listen --advertise "motu-pcie-424 logger"`
2. In CueMix FX: *Control Surfaces > Configure OSC Devices…*, pick that name,
   give it `TouchOSC-iPad.layout_description`, enable it.
3. Values start arriving.

## Two address spaces

This is the thing to know before reading any capture. Widgets in a layout carry
both a host-tree address and, optionally, a client-facing `routing` address:

```xml
<TEXT name="solo0" type="float" blank="0" routing="/mix/solo/1/1">/bin/fvEB+0/fvInCS+0/solo</TEXT>
```

`OSCDevice::ApplyRoutingHostToClient` / `ApplyRoutingClientToHost` rewrite
between them, so **what appears on the wire depends on the layout in force**. A
capture made with the TouchOSC layout shows `/mix/solo/1/1`, not
`/bin/0/0/solo`. `tools/osc-log.py tree` prints both columns.

## The host tree

From the shipped `TouchOSC-iPad.layout_description` (six pages: `/mixes`,
`/inputs`, `/outputs`, `/channel_dynamics`, `/eq`, `/reverb`):

| address | meaning | PCI? |
|---|---|---|
| `/in/<ch>/in/namS` | input name | yes |
| `/in/<ch>/in/trm`, `…/trm/str` | input trim + readout | yes |
| `/in/<ch>/in/st` | mono/stereo | yes |
| `/in/<ch>/in/psi` | phase invert | check |
| `/bin/<bus>/<ch>/solo`, `/mute` | per-mix strip solo, mute | yes |
| `/bin/<bus>/<ch>/cdp` | pan | yes |
| `/bin/<bus>/<ch>/cdf`, `…/cdf/str` | fader + readout | yes |
| `/bin/<bus>/<ch>/poflS` | pre/post fader level (meter) | yes |
| `/bus/<bus>/mix/name`, `/bonm` | mix name, output name | yes |
| `/bus/<bus>/mix/mute`, `/fade`, `/fade/str` | mix master | yes |
| `/bus/<bus>/mix/blS` | mix master meter | yes |
| `/tb/0/0/tben`, `lben`, `tbal`, `lbal`, `tbin`, `lbin` | talkback / listenback | yes |
| `/dev/0/0/actv`, `/mon` | device active, monitor level | yes |
| `/in/<ch>/eq/*`, `/dyn/*`, `/rvs/*`, `/bus/<bus>/rsr/*` | EQ, dynamics, reverb | **no** — FX boxes only |

Two index placeholders are resolved by the app at send time, from per-page
preferences:

```
fvEB+N      first visible enabled bus + N       <page>_OSCFirstVisibleEnabledBus
fvInCS+N    first visible input strip + N       <page>_OSCFirstVisibleInputChannelStrip
fvOutCS+N   first visible output strip + N      <page>_OSCFirstVisibleOutputChannelStrip
```

**`/str` appended to a numeric address gives its display string** — the same text
the console shows, e.g. `"-6.0 dB"`. That is the cheapest possible confirmation
of the value laws decoded in `CUEMIX-API.md`: if `/cdf` reads 16384 and
`/cdf/str` reads `-12.0 dB`, the 40·log10 law is confirmed against MOTU's own
formatting.

## Meters

Meter streams are **subscriptions, per page and per kind**, stored in prefs and
visible with `osc-log.py prefs`:

```
client state  [com_motu_driver_FWA_Engine:00000d2f76_MOTU UltraLite mk3 …]
  layout       '[Application]/TouchOSC-iPad.layout_description'
  current page '/mixes'
  meter subscriptions:
    /inputs              /pre     on
    /inputs              /post    on
    /inputs              /rms     off
    /mixes               /meters  on
    /outputs             /meters  on
```

`/meters` is the subscription SELECT in the layout; `/pre`, `/post`, `/rms` are
the kinds. `osc-log.py listen --meters` reports messages/second so the update
rate can be measured, and `--quiet-meters` keeps them out of the console while
still recording them to `--csv`.

## Preferences layout

```
<engine>:<serial>_OSCClients            { "0": "<client service name>", numElements: N }
<engine>:<serial>_<client name>         { OSCLayoutDescriptionFilename, OSCCurrentPage,
                                          <page>_OSCFirstVisible*, <page>_meters_/<kind> }
<engine>:<serial>                       { OSCUDPPort }
```

`numElements: 0` means no client is active even if a stale `"0"` entry remains —
which is the current state on this machine, and why nothing is being pushed.

The `<engine>` prefix identifies the driver family, so on a PCI-424 expect
`com_motu_driver_PCIAudio_Engine:…` rather than `com_motu_driver_FWA_Engine:…`.
`osc-log.py prefs` prints the engine keys present, which is the quickest way to
confirm which driver CueMix FX has actually bound to.

## What OSC is and is not good for here

Good for: confirming value laws through `/str`, watching meter rate and
behaviour, and having MOTU's own app report its view of the card as an
independent oracle beside ours.

Not a substitute for the card: the OSC tree is shared across back ends, but the
parameter ids, value laws and meter transports behind it are per-back-end — see
"What does not transfer from a FireWire/USB interface" in `CUEMIX-API.md`. A
capture made at home against an UltraLite tells you the protocol and the address
shapes, and nothing reliable about PCI numbers.
