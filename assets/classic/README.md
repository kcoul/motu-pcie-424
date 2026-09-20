# Classic skin sprites

**No artwork ships here.** The Classic skin draws MOTU's own sprites, which are
MOTU's copyright and are not redistributed by this project. The 24 PNGs that
used to sit in this directory were removed on 2026-09-19, before the repository
was made public, and `.gitignore` keeps them out.

**Nothing needs doing to make Classic work.** The app already looks for the
artwork in your own installed copy, in this order:

1. whatever *View > Locate MOTU CueMix FX.app…* was last pointed at;
2. `Contents/Resources/classic` inside our own bundle;
3. `assets/classic` here, if you have put the PNGs back yourself;
4. `/Applications/CueMix FX.app/Contents/Resources`, and the same under
   `~/Applications`;
5. any mounted volume with a `CueMix FX.app` in its `Applications`.

So installing MOTU's CueMix FX — which is worth doing anyway, since it runs
again after a one-line re-sign (`docs/ORIGINAL-UI.md`) — is all that is needed.
`ClassicSkin::isUsable` probes for `BackgroundRightPCI.png`. Without it the
Classic menu item is simply disabled and the Modern skin, which is our own
artwork, is used instead.

The sprites the console actually draws are the Legacy/PCI subset of the 145 PNGs
in `CueMix FX.app/Contents/Resources` (verified against 1.6 b5003c51d; all 24
are present there). The geometry below is this project's own reverse
engineering and is what the drawing code relies on.

Sheets are equal frames laid out row by row:

| sprite | frame | notes |
|---|---|---|
| `KnobLit` | 36 × 32 | the LED ring, six colours (0 = blue); ring centre at 17.5, 17; drawn through a pie mask |
| `KnobRotation` | 26 × 26 | frame 0 = bare base, 4 = full left, 32 = centre, 60 = full right |
| `SmallKnobBlack` | 31 × 31 | same numbering |
| `TextButtons` | 82 × 20 | col 0 off / 1 on / 2-3 hover / 4-5 disabled; row 0 has the grille |
| `HalfButtons` | 40 × 20 | row 0 lights the left edge (MONO), row 1 the right (STEREO) |
| `ColorButtons` | 24 × 17 | col 0 off / 1 on; row 1 red (SOLO), row 2 yellow (MUTE) |
| `TalkbackButton` | 37 × 37 | col 0 off / 1 on |
| `FaderCap` | 22 × 47 | col 0 normal / 1 pressed |
| `CircleCheckBox` | 10 × 10 | off / on |
| `TrimClipIndicator` | 6 × 6 | off / signal / clip |
| `Level` | 11 × 183 | lit, half-lit, unlit meter columns |
| `ScrollButtons` | 37 × 14 | normal / pressed / disabled |
