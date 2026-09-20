# App icons

**Original artwork, not MOTU's.** Earlier revisions of this directory held
MOTU's own icons, extracted from the Mojave bundles. Those were replaced on
2026-09-19 so the repository can be published without redistributing MOTU's
copyrighted material.

Both are 1024 px and share one family: a dark rounded tile with the gold edge
connector every PCI card has, and a motif above it saying which app it is.

| file | motif |
|---|---|
| `pci-audio-setup.png` | three routing lanes with nodes — configuration |
| `cuemix-fx.png` | four faders at different levels — mixing |

Regenerate them with `tools/mkicon.m`, which draws both with CoreGraphics:

```sh
clang -fobjc-arc \
  -isysroot $(ls -d /Library/Developer/CommandLineTools/SDKs/MacOSX*.sdk | tail -1) \
  -framework Foundation -framework CoreGraphics -framework ImageIO \
  -framework CoreServices -o /tmp/mkicon tools/mkicon.m
/tmp/mkicon assets/icons/pci-audio-setup.png assets/icons/cuemix-fx.png
```

JUCE builds the `.icns` from these via `ICON_BIG`.
