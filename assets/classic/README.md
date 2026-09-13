# Classic skin sprites

MOTU's own CueMix FX artwork: the Legacy/PCI subset of the 145 PNGs in
`CueMix FX.app/Contents/Resources` (1.6 73220), copied unmodified. These are
only the sprites `src/cuemix-fx/ClassicConsole.cpp` draws.

The app looks here first (walking up from the build tree), then inside any
original `CueMix FX.app` it can find, or wherever *View > Locate MOTU CueMix
FX.app…* points.

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
