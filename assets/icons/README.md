# App icons

MOTU's original 512 px application icons, extracted unmodified from the
Mojave bundles (`CueMix FX.app` → `ConsoleIcon.icns`,
`MOTU PCI Audio Setup.app` → `CnslIconSdw.icns`) with `iconutil`. JUCE builds
the `.icns` from these via `ICON_BIG`.

These are MOTU's copyright and are included so the replacements are recognisable
to the people already using the originals. See the notices in `LICENSE`.

## Original alternatives

`tools/mkicon.m` draws an original pair with CoreGraphics — a dark tile with the
gold edge connector every PCI card has, routing lanes for Setup and faders for
CueMix. Swap them in if MOTU's icons ever need to come out, or simply to look
less like MOTU's own apps in the Dock:

```sh
clang -fobjc-arc \
  -isysroot $(ls -d /Library/Developer/CommandLineTools/SDKs/MacOSX*.sdk | tail -1) \
  -framework Foundation -framework CoreGraphics -framework ImageIO \
  -framework CoreServices -o /tmp/mkicon tools/mkicon.m
/tmp/mkicon assets/icons/pci-audio-setup.png assets/icons/cuemix-fx.png
```

Nothing else changes: both are 1024 px PNGs read through the same `ICON_BIG`.
