#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Strip.h"
#include "motu_card.h"

#include <memory>
#include <vector>

// CueMix FX's console: one strip per active input on the card, scrolling
// sideways, beside a fixed right-hand panel (LCD, MIX, output, master).
//
// The strip set follows the card. An HD192 alone gives 12 strips; four 24I/Os
// or 2408s give up to 96. Enabling channels in PCI Audio Setup (or anywhere)
// adds and removes strips live.
class Console : public juce::Component, private juce::Timer {
public:
    Console();
    ~Console() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    int idealWidth() const;
    static int idealHeight() { return Strip::kHeight + 2 * 6 + kScrollBar; }
    static constexpr int kScrollBar = 10;

private:
    void timerCallback() override;
    void connect();
    void syncLayout();      // rebuild strips / mixes if the card's layout changed
    void poll();            // refresh every visible value
    void showInLcd(const juce::String& title, const juce::String& detail);
    juce::String busName(int bus);

    AudioDeviceID dev_ = 0;
    motu::Card card_;

    std::vector<int> inputIds_;             // active inputs, card-wide ids
    std::vector<int> buses_;                // mix buses = output pairs by left id
    int bus_ = 0;

    juce::Viewport viewport_;
    juce::Component stripHolder_;
    std::vector<std::unique_ptr<Strip>> strips_;

    // Right-hand panel.
    juce::Label lcdTitle_, lcdDetail_, lcdBudget_;
    juce::Label mixLabel_ { {}, "MIX" }, outputLabel_;
    juce::ComboBox mixBox_;
    juce::Slider master_ { juce::Slider::LinearVertical, juce::Slider::NoTextBox };
    juce::Label masterValue_;
    juce::TextButton masterMute_ { "MUTE" };
    juce::Label banner_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Console)
};
