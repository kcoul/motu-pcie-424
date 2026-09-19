#include "Meter.h"

#include "ClassicSkin.h"

namespace meter {

// Rows of the column that correspond to a 0..1 level, measured from the bottom.
static int fillRows(float v) {
    return juce::roundToInt(juce::jlimit(0.0f, 1.0f, v) * (float)kColumnH);
}

void drawClassic(juce::Graphics& g, ClassicSkin& skin, int x, int y,
                 float level, float peak, bool clip, bool withCap) {
    // Unlit frame: background, tick marks and (when ours) the dark clip cap.
    {
        juce::Graphics::ScopedSaveState s(g);
        if (!withCap)
            g.reduceClipRegion(x, y + kColumnY, kFrameW, kColumnH);
        skin.drawFrame(g, "Level", kFrameW, kFrameH, 2, 0, x, y);
    }

    const int fill = fillRows(level);
    if (fill > 0) {
        juce::Graphics::ScopedSaveState s(g);
        g.reduceClipRegion(x, y + kFrameH - fill, kFrameW, fill);
        skin.drawFrame(g, "Level", kFrameW, kFrameH, 0, 0, x, y);
    }

    // Held peak, above the bar. Skipped when it coincides with the bar's top,
    // which is the common case while a level is rising.
    const int peakRows = fillRows(peak);
    if (peakRows > fill + kPeakPx) {
        juce::Graphics::ScopedSaveState s(g);
        g.reduceClipRegion(x, y + kFrameH - peakRows, kFrameW, kPeakPx);
        skin.drawFrame(g, "Level", kFrameW, kFrameH, 0, 0, x, y);
    }

    if (clip && withCap) {
        juce::Graphics::ScopedSaveState s(g);
        g.reduceClipRegion(x, y, kFrameW, kCapH);
        skin.drawFrame(g, "Level", kFrameW, kFrameH, 0, 0, x, y);
    }
}

void drawModern(juce::Graphics& g, juce::Rectangle<float> bounds,
                float level, float peak, bool clip) {
    // Same split as MOTU's sprite: a cap on top, the column below.
    const float capH = bounds.getHeight() * ((float)kCapH / (float)kFrameH);
    auto cap = bounds.removeFromTop(capH).reduced(0.0f, 1.0f);
    auto col = bounds.withTrimmedTop(1.0f);

    g.setColour(juce::Colour(0xff101240));
    g.fillRoundedRectangle(col, 2.0f);

    const float h = col.getHeight() * juce::jlimit(0.0f, 1.0f, level);
    if (h > 0.0f) {
        g.setColour(juce::Colour(0xff6291eb));
        g.fillRoundedRectangle(col.withTop(col.getBottom() - h), 2.0f);
    }

    const float ph = col.getHeight() * juce::jlimit(0.0f, 1.0f, peak);
    if (ph > h + 2.0f) {
        g.setColour(juce::Colour(0xff9ec0f5));
        g.fillRect(col.getX(), col.getBottom() - ph, col.getWidth(), 2.0f);
    }

    g.setColour(clip ? juce::Colour(0xffffce17) : juce::Colour(0xff26275c));
    g.fillRoundedRectangle(cap, 2.0f);
}

} // namespace meter
