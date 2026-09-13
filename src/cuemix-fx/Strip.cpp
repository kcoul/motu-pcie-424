#include "Strip.h"

#include "ConsoleLookAndFeel.h"

#include <cmath>

namespace {
constexpr int kVolumeMax = 65536;
// Put unity (32768) three quarters of the way up the fader, where consoles do.
const double kFaderSkew = std::log(0.75) / std::log(0.5);

void styleLabel(juce::Label& l, float size, juce::Colour colour) {
    l.setFont(juce::FontOptions(size));
    l.setJustificationType(juce::Justification::centred);
    l.setColour(juce::Label::textColourId, colour);
    l.setInterceptsMouseClicks(false, false);
}

void styleButton(juce::TextButton& b, juce::Colour on) {
    b.setClickingTogglesState(false);
    b.setColour(juce::TextButton::buttonColourId, ConsoleLookAndFeel::well());
    b.setColour(juce::TextButton::buttonOnColourId, on);
    b.setColour(juce::TextButton::textColourOffId, ConsoleLookAndFeel::dim());
    b.setColour(juce::TextButton::textColourOnId, juce::Colours::black);
    b.setInterceptsMouseClicks(false, false);   // read-only first pass
}
}  // namespace

juce::String volumeText(int raw) {
    if (raw <= 0) return "-inf";
    return juce::String(20.0 * std::log10(raw / 32768.0), 1) + " dB";
}

juce::String panText(int raw) {
    const int p = raw - 64;
    return p == 0 ? "C" : (p < 0 ? "L" + juce::String(-p) : "R" + juce::String(p));
}

Strip::Strip(int inputId, const juce::String& interfaceName, const juce::String& channelName)
    : id_(inputId), iface_(interfaceName), name_(channelName) {
    styleButton(inputMute_, ConsoleLookAndFeel::mute());
    styleButton(mute_, ConsoleLookAndFeel::mute());
    styleButton(solo_, ConsoleLookAndFeel::solo());

    trim_.setRange(0, 128, 1);
    pan_.setRange(0, 128, 1);
    fader_.setNormalisableRange({ 0.0, (double)kVolumeMax, 1.0, kFaderSkew });
    for (auto* s : { &trim_, &pan_, &fader_ }) s->setInterceptsMouseClicks(false, false);

    styleLabel(trimValue_, 12.0f, ConsoleLookAndFeel::text());
    styleLabel(panValue_, 12.0f, ConsoleLookAndFeel::text());
    styleLabel(faderValue_, 12.0f, ConsoleLookAndFeel::text());
    styleLabel(nameTop_, 11.0f, ConsoleLookAndFeel::dim());
    styleLabel(nameBottom_, 12.5f, ConsoleLookAndFeel::text());
    nameTop_.setText(iface_, juce::dontSendNotification);
    nameBottom_.setText(name_, juce::dontSendNotification);
    nameBottom_.setMinimumHorizontalScale(0.7f);

    for (juce::Component* c : { (juce::Component*)&inputMute_, (juce::Component*)&trim_, (juce::Component*)&trimValue_,
                                (juce::Component*)&nameTop_, (juce::Component*)&nameBottom_, (juce::Component*)&pan_,
                                (juce::Component*)&panValue_, (juce::Component*)&fader_, (juce::Component*)&faderValue_,
                                (juce::Component*)&solo_, (juce::Component*)&mute_ })
        addAndMakeVisible(c);

    setSize(kWidth, kHeight);
    setState({});
    state_.volume = -1;   // force the first real update through
}

void Strip::setState(const StripState& s) {
    if (s == state_) return;
    state_ = s;
    inputMute_.setToggleState(s.inputMute != 0, juce::dontSendNotification);
    mute_.setToggleState(s.mute != 0, juce::dontSendNotification);
    solo_.setToggleState(s.solo != 0, juce::dontSendNotification);
    trim_.setValue(s.trim, juce::dontSendNotification);
    trimValue_.setText(juce::String(s.trim - 64), juce::dontSendNotification);
    pan_.setValue(s.pan, juce::dontSendNotification);
    panValue_.setText(panText(s.pan), juce::dontSendNotification);
    fader_.setValue(s.volume, juce::dontSendNotification);
    faderValue_.setText(volumeText(s.volume), juce::dontSendNotification);
}

void Strip::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat().reduced(2.0f, 0.0f);
    g.setColour(ConsoleLookAndFeel::strip());
    g.fillRoundedRectangle(r, 4.0f);

    // Name well, and the rule between the input and mix sections.
    g.setColour(ConsoleLookAndFeel::well());
    g.fillRoundedRectangle(nameTop_.getBounds().getUnion(nameBottom_.getBounds()).toFloat().expanded(2.0f, 1.0f), 3.0f);
    g.setColour(juce::Colour(0xff3a414a));
    g.fillRect(6.0f, 150.0f, r.getWidth() - 8.0f, 1.0f);

    g.setColour(ConsoleLookAndFeel::dim());
    g.setFont(juce::FontOptions(10.0f));
    g.drawText("TRIM", 0, 30, getWidth(), 10, juce::Justification::centred);
    g.drawText("PAN", 0, 156, getWidth(), 10, juce::Justification::centred);

    // Meter placeholder, beside the fader: ReadLevelMeters is not decoded yet.
    const auto f = fader_.getBounds();
    g.setColour(ConsoleLookAndFeel::well());
    g.fillRoundedRectangle((float)f.getRight() + 2.0f, (float)f.getY() + 8.0f, 6.0f, (float)f.getHeight() - 16.0f, 2.0f);
}

void Strip::resized() {
    const int w = getWidth();
    inputMute_.setBounds(8, 6, w - 16, 20);
    trim_.setBounds(w / 2 - 21, 40, 42, 42);
    trimValue_.setBounds(0, 82, w, 16);
    nameTop_.setBounds(6, 108, w - 12, 14);
    nameBottom_.setBounds(6, 122, w - 12, 18);
    pan_.setBounds(w / 2 - 21, 166, 42, 42);
    panValue_.setBounds(0, 208, w, 16);
    fader_.setBounds(w / 2 - 22, 226, 36, 164);
    solo_.setBounds(8, 394, w - 16, 20);
    mute_.setBounds(8, 418, w - 16, 20);
    faderValue_.setBounds(0, 444, w, 18);
}
