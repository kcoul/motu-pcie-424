#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Light Mojave-era Aqua controls, to match MOTU's console as it appears in
// docs/reference/. The original never used stock controls (PowerPlant drew
// through Carbon), so what we match is the screenshot, not an API.
class MacLookAndFeel : public juce::LookAndFeel_V4 {
public:
    MacLookAndFeel();

    // JUCE sizes fonts by pixel height, not points: 15.5 px is the 13 pt system font.
    static constexpr float kFontSize = 15.5f;
    static juce::Colour windowBackground() { return juce::Colour(0xffececec); }
    static juce::Colour separator()        { return juce::Colour(0xffc8c8c8); }
    static juce::Colour accent()           { return juce::Colour(0xff3b8bf5); }

    juce::Font getLabelFont(juce::Label&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    juce::Font getTextButtonFont(juce::TextButton&, int) override;

    void drawComboBox(juce::Graphics&, int width, int height, bool isButtonDown,
                      int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
    void positionComboBoxText(juce::ComboBox&, juce::Label&) override;

    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&,
                              bool isHighlighted, bool isDown) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool isHighlighted, bool isDown) override;

    void drawToggleButton(juce::Graphics&, juce::ToggleButton&,
                          bool isHighlighted, bool isDown) override;

    void drawPopupMenuBackground(juce::Graphics&, int width, int height) override;
    void drawPopupMenuItem(juce::Graphics&, const juce::Rectangle<int>& area,
                           bool isSeparator, bool isActive, bool isHighlighted,
                           bool isTicked, bool hasSubMenu, const juce::String& text,
                           const juce::String& shortcutKeyText, const juce::Drawable* icon,
                           const juce::Colour* textColour) override;
    void getIdealPopupMenuItemSize(const juce::String& text, bool isSeparator,
                                   int standardMenuItemHeight, int& idealWidth,
                                   int& idealHeight) override;
    int getPopupMenuBorderSize() override { return 5; }
};

// A horizontal hairline, as between the console's sections.
class Separator : public juce::Component {
public:
    void paint(juce::Graphics& g) override {
        g.setColour(MacLookAndFeel::separator());
        g.fillRect(0, getHeight() / 2, getWidth(), 1);
    }
};
