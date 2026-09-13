#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

// The CueMix values one input strip shows, for the mix currently selected.
struct StripState {
    int trim = 64, inputMute = 0;          // per input
    int volume = 32768, pan = 64;          // per (mix bus, input)
    int mute = 0, solo = 0;
    bool operator==(const StripState& o) const {
        return trim == o.trim && inputMute == o.inputMute && volume == o.volume && pan == o.pan
            && mute == o.mute && solo == o.solo;
    }
};

// One input strip: the input section (MUTE, TRIM, name) above the mix section
// (PAN, fader, SOLO, MUTE) — the order of MOTU's console. First pass: it shows
// the card's state and does not write (docs/CUEMIX-PLAN.md, stage 1).
class Strip : public juce::Component {
public:
    static constexpr int kWidth = 84;
    static constexpr int kHeight = 470;

    Strip(int inputId, const juce::String& interfaceName, const juce::String& channelName);

    int inputId() const { return id_; }
    juce::String interfaceName() const { return iface_; }
    juce::String channelName() const { return name_; }

    void setState(const StripState&);
    std::function<void(const Strip&)> onHover;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseEnter(const juce::MouseEvent&) override { if (onHover) onHover(*this); }

private:
    int id_;
    juce::String iface_, name_;
    StripState state_;

    juce::TextButton inputMute_ { "MUTE" }, solo_ { "SOLO" }, mute_ { "MUTE" };
    juce::Slider trim_ { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::Slider pan_  { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::Slider fader_ { juce::Slider::LinearVertical, juce::Slider::NoTextBox };
    juce::Label trimValue_, panValue_, faderValue_, nameTop_, nameBottom_;
};

// Tentative displays. Only 32768 = unity and 64 = centre are observed; the
// laws around them are not verified yet.
juce::String volumeText(int raw);
juce::String panText(int raw);
