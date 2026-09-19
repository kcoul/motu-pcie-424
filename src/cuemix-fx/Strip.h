#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ConsoleModel.h"

#include <functional>

// One input strip: the input section (MUTE, TRIM, name) above the mix section
// (PAN, fader, SOLO, MUTE) — the order of MOTU's console. Moving a control
// reports it through onEdit; the model keeps it locally (not sent to the card yet).
class Strip : public juce::Component {
public:
    static constexpr int kWidth = 84;
    static constexpr int kHeight = 470;

    Strip(int inputId, const juce::String& interfaceName, const juce::String& channelName);

    int inputId() const { return id_; }
    juce::String interfaceName() const { return iface_; }
    juce::String channelName() const { return name_; }

    void setState(const StripState&);
    void setMeter(const ConsoleModel::MeterState& m) { meter_ = m; repaint(); }
    std::function<void(const Strip&)> onHover;
    std::function<void(ConsoleModel::Param, int value)> onEdit;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseEnter(const juce::MouseEvent&) override { if (onHover) onHover(*this); }

private:
    int id_;
    juce::String iface_, name_;
    StripState state_;
    ConsoleModel::MeterState meter_;

    juce::TextButton inputMute_ { "MUTE" }, solo_ { "SOLO" }, mute_ { "MUTE" };
    juce::Slider trim_ { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::Slider pan_  { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::Slider fader_ { juce::Slider::LinearVertical, juce::Slider::NoTextBox };
    juce::Label trimValue_, panValue_, faderValue_, nameTop_, nameBottom_;
};

