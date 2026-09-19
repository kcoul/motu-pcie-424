#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class ClassicSkin;

// One CueMix level meter: a bar, a held peak, and a clip indicator on top.
//
// Both skins draw the same geometry, taken from MOTU's own `Level.png`
// (docs/CUEMIX-API.md). The sprite is a sheet of three 11 x 183 frames:
//
//   frame 0   bright fill   segments rgb(98,145,235), clip cap amber(255,206,23)
//   frame 1   dim fill      segments rgb(70,118,208)  -- MOTU's RMS fill; the
//                           PCI back end reports no RMS, so it is unused here
//   frame 2   unlit         segments rgb(16,18,76),   clip cap rgb(38,39,92)
//
// and within a frame:
//
//   y  0..11    the clip indicator cap (12 px)
//   y 13..182   the ticked level column (170 px), filled from the bottom
//
// Drawing is therefore: the unlit frame whole, then the bright frame clipped to
// the bar, then a slice of the bright frame at the held peak, then the bright
// frame's cap if the channel has clipped. Values are 0..1, i.e. the card's
// linear level over 32768.
namespace meter {

constexpr int kFrameW   = 11;
constexpr int kFrameH   = 183;
constexpr int kCapH     = 12;    // clip indicator, y 0..11
constexpr int kColumnY  = 13;    // first row of the level column
constexpr int kColumnH  = kFrameH - kColumnY;   // 170
constexpr int kPeakPx   = 2;     // thickness of the held-peak marker

// MOTU's artwork, via the Classic skin. `withCap` draws the clip cap on top;
// input strips want it off, because ChannelStripMixLegacy.png already paints
// its own background where the cap would go and the strip's clip indication is
// the trim LED instead (see clipFrame).
void drawClassic(juce::Graphics&, ClassicSkin&, int x, int y,
                 float level, float peak, bool clip, bool withCap = true);

// Frame of the 3-frame TrimClipIndicator sheet ("off, signal, clip") for a raw
// clip value from the card.
//
// CoreDeviceAW::UpdateLevelMeters turns the card's clip field into a float:
// 1 -> 1.0, 2 -> 0.5, anything else -> 0.0. Against a three-frame indicator
// that is frame = value * 2, so 1 is the clip state and 2 is mere signal
// presence. Derived, not yet seen with signal on a card.
inline int clipFrame(int clipRaw) {
    return clipRaw == 1 ? 2 : (clipRaw == 2 ? 1 : 0);
}

// The Modern skin: same proportions, our own colours.
void drawModern(juce::Graphics&, juce::Rectangle<float> bounds,
                float level, float peak, bool clip);

} // namespace meter
