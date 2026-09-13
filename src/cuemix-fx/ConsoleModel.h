#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "motu_card.h"

#include <functional>
#include <vector>

// The CueMix values one input strip shows, for the mix currently selected.
struct StripState {
    int trim = 64, inputMute = 0;          // per input
    int volume = 32768, pan = 64;          // per (mix bus, input)
    int mute = 0, solo = 0;
    bool operator==(const StripState& o) const {
        return trim == o.trim && inputMute == o.inputMute && volume == o.volume && pan == o.pan
            && mute == o.mute && solo == o.solo;
    }
    bool operator!=(const StripState& o) const { return !(*this == o); }
};

struct StripInfo {
    int id = 0;                            // card-wide input id
    juce::String interfaceName;            // "HD192"
    juce::String channelName;              // custom name, or "Analog-A 3"
    StripState state;
};

// Live CueMix state of the card, shared by every skin. Polls at 10 Hz and
// reports whether the layout (strips, mixes) or only values changed.
//
// The strip set is the card's active inputs; the mixes are the output pairs
// CueMix accepts as buses (docs/CUEMIX-PLAN.md). Read-only for now.
class ConsoleModel : private juce::Timer {
public:
    ConsoleModel();
    ~ConsoleModel() override;

    bool connected() const { return (bool)card_; }
    juce::String error() const { return error_; }

    const std::vector<StripInfo>& strips() const { return strips_; }
    int numMixes() const { return (int)buses_.size(); }
    juce::String mixName(int index) const;      // "Mix 1"
    juce::String mixOutputName(int index) const; // "HD192:Analog/AES 1-2"
    int selectedMix() const { return mix_; }
    void selectMix(int index);

    int masterVolume() const { return masterVolume_; }
    bool masterMute() const { return masterMute_; }
    motu::CueMix::Resources resources() const { return resources_; }
    int cueMixFaders() const { return cueMixFaders_; }

    std::function<void(bool layoutChanged)> onChange;

private:
    void timerCallback() override { refresh(); }
    void refresh();
    bool syncLayout();
    bool poll();

    AudioDeviceID dev_ = 0;
    motu::Card card_;
    juce::String error_;

    std::vector<StripInfo> strips_;
    std::vector<int> buses_;
    juce::StringArray busNames_;
    int mix_ = 0;
    int masterVolume_ = 32768;
    bool masterMute_ = false;
    motu::CueMix::Resources resources_;
    int cueMixFaders_ = 0;
};

// Tentative displays. Only 32768 = 0 dB and 64 = centre are observed; the laws
// around them are not verified yet.
juce::String volumeText(int raw);
double volumeDb(int raw);
juce::String panText(int raw);
