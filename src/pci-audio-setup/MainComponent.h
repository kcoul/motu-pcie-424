#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "motu_card.h"

#include <memory>
#include <vector>

// The console's single window. Fixed 602 x 334 to match MOTU's original; the
// exact placement of controls inside it still has to be captured from Mojave
// (docs/ORIGINAL-UI.md), so this lays them out plainly for now.
class MainComponent : public juce::Component {
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void connect();          // find the card and populate everything
    void refreshRates();
    void refreshClocks();
    void refreshInterfaces();
    void setStatus(const juce::String&, bool isError);

    AudioDeviceID deviceId_ = 0;
    motu::Card card_;

    juce::Label      title_;
    juce::Label      statusLabel_;

    juce::Label      rateLabel_,  clockLabel_;
    juce::ComboBox   rateBox_,    clockBox_;
    std::vector<double> rates_;
    std::vector<UInt32> clocks_;

    juce::Label      interfacesLabel_;
    juce::TextEditor interfaces_;

    juce::Label      usageLabel_;
    juce::TextButton refreshButton_ { "Refresh" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
