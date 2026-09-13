#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ConsoleModel.h"
#include "Strip.h"

#include <memory>
#include <vector>

// The Modern skin: CueMix FX's layout drawn with our own flat controls. One
// strip per active input, scrolling sideways, beside a fixed right panel.
class Console : public juce::Component {
public:
    explicit Console(ConsoleModel&);
    ~Console() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    int idealWidth() const;
    static int idealHeight() { return Strip::kHeight + 2 * 6 + kScrollBar; }
    static constexpr int kScrollBar = 10;

private:
    void modelChanged(bool layout);
    void showInLcd(const juce::String& title, const juce::String& detail);

    ConsoleModel& model_;
    juce::Viewport viewport_;
    juce::Component stripHolder_;
    std::vector<std::unique_ptr<Strip>> strips_;

    juce::Label lcdTitle_, lcdDetail_, lcdBudget_;
    juce::Label mixLabel_ { {}, "MIX" }, outputLabel_;
    juce::ComboBox mixBox_;
    juce::Slider master_ { juce::Slider::LinearVertical, juce::Slider::NoTextBox };
    juce::Label masterValue_;
    juce::TextButton masterMute_ { "MUTE" };
    juce::Label banner_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Console)
};
