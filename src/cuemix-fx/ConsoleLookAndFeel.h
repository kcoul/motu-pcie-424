#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// A dark mixing-console look for the first pass. The classic skin (MOTU's own
// PNGs, docs/ORIGINAL-UI.md) comes later; this only has to read clearly.
class ConsoleLookAndFeel : public juce::LookAndFeel_V4 {
public:
    static juce::Colour panel()   { return juce::Colour(0xff1d2127); }
    static juce::Colour strip()   { return juce::Colour(0xff272c33); }
    static juce::Colour well()    { return juce::Colour(0xff11141a); }
    static juce::Colour text()    { return juce::Colour(0xffd8dde3); }
    static juce::Colour dim()     { return juce::Colour(0xff7d8793); }
    static juce::Colour accent()  { return juce::Colour(0xff4aa3ff); }
    static juce::Colour solo()    { return juce::Colour(0xffffc53d); }
    static juce::Colour mute()    { return juce::Colour(0xffff5d5d); }

    ConsoleLookAndFeel() : LookAndFeel_V4(juce::LookAndFeel_V4::getDarkColourScheme()) {
        setColour(juce::ResizableWindow::backgroundColourId, panel());
        setColour(juce::Label::textColourId, text());
        setColour(juce::Slider::thumbColourId, juce::Colour(0xffe8ecf0));
        setColour(juce::Slider::trackColourId, accent().withAlpha(0.7f));
        setColour(juce::Slider::backgroundColourId, well());
        setColour(juce::Slider::rotarySliderFillColourId, accent());
        setColour(juce::Slider::rotarySliderOutlineColourId, well());
        setColour(juce::ComboBox::backgroundColourId, well());
        setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff3a414a));
        setColour(juce::PopupMenu::backgroundColourId, strip());
        setColour(juce::PopupMenu::highlightedBackgroundColourId, accent());
    }

    void drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                          juce::Slider::SliderStyle style, juce::Slider& s) override {
        if (style != juce::Slider::LinearVertical) {
            LookAndFeel_V4::drawLinearSlider(g, x, y, w, h, pos, 0, 0, style, s);
            return;
        }
        auto track = juce::Rectangle<float>((float)x + (float)w * 0.5f - 2.0f, (float)y, 4.0f, (float)h);
        g.setColour(well());
        g.fillRoundedRectangle(track, 2.0f);
        g.setColour(findColour(juce::Slider::trackColourId));
        g.fillRoundedRectangle(track.withTop(pos), 2.0f);
        auto cap = juce::Rectangle<float>(28.0f, 14.0f).withCentre({ track.getCentreX(), pos });
        g.setColour(juce::Colour(0xff0a0c10));
        g.fillRoundedRectangle(cap.translated(0, 1.5f), 3.0f);
        g.setColour(findColour(juce::Slider::thumbColourId));
        g.fillRoundedRectangle(cap, 3.0f);
        g.setColour(juce::Colour(0xff5a626c));
        g.fillRect(cap.withSizeKeepingCentre(cap.getWidth() - 8.0f, 1.5f));
    }

    // Strip buttons are narrow; the default font scales to the button height.
    juce::Font getTextButtonFont(juce::TextButton&, int height) override {
        return juce::Font(juce::FontOptions(juce::jmin(11.0f, (float)height * 0.55f), juce::Font::bold));
    }

    juce::Slider::SliderLayout getSliderLayout(juce::Slider& s) override {
        juce::Slider::SliderLayout l;
        l.sliderBounds = s.getLocalBounds().reduced(0, s.isVertical() ? 8 : 0);
        return l;
    }
};
