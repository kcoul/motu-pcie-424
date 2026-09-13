#include "Console.h"

#include "ConsoleLookAndFeel.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr int kPanelWidth = 250;

void styleLabel(juce::Label& l, float size, juce::Colour c, juce::Justification j = juce::Justification::centredLeft) {
    l.setFont(juce::FontOptions(size));
    l.setColour(juce::Label::textColourId, c);
    l.setJustificationType(j);
}

}  // namespace

Console::Console(ConsoleModel& model) : model_(model) {
    viewport_.setViewedComponent(&stripHolder_, false);
    viewport_.setScrollBarsShown(false, true);
    viewport_.setScrollBarThickness(kScrollBar);
    addAndMakeVisible(viewport_);

    styleLabel(lcdTitle_, 18.0f, ConsoleLookAndFeel::accent());
    styleLabel(lcdDetail_, 13.0f, ConsoleLookAndFeel::accent().withAlpha(0.8f));
    styleLabel(lcdBudget_, 12.0f, ConsoleLookAndFeel::accent().withAlpha(0.8f));
    lcdDetail_.setJustificationType(juce::Justification::topLeft);
    lcdBudget_.setJustificationType(juce::Justification::bottomLeft);
    styleLabel(mixLabel_, 14.0f, ConsoleLookAndFeel::text());
    styleLabel(outputLabel_, 12.0f, ConsoleLookAndFeel::dim());
    styleLabel(masterValue_, 12.0f, ConsoleLookAndFeel::text(), juce::Justification::centred);
    styleLabel(banner_, 11.5f, ConsoleLookAndFeel::solo().withAlpha(0.85f), juce::Justification::topLeft);
    banner_.setText("Shows the card's CueMix state live. Controls move, but nothing is sent to the "
                    "card until the encodings are verified (docs/CUEMIX-PLAN.md).", juce::dontSendNotification);

    master_.setNormalisableRange({ 0.0, 32768.0, 1.0, 3.0 });
    master_.setDoubleClickReturnValue(true, 32768);
    master_.onValueChange = [this] { model_.setLocal(ConsoleModel::Param::MasterVolume, (int)master_.getValue()); };
    masterMute_.setColour(juce::TextButton::buttonColourId, ConsoleLookAndFeel::well());
    masterMute_.setColour(juce::TextButton::buttonOnColourId, ConsoleLookAndFeel::mute());
    masterMute_.setColour(juce::TextButton::textColourOnId, juce::Colours::black);
    masterMute_.setTriggeredOnMouseDown(true);
    masterMute_.onClick = [this] { model_.setLocal(ConsoleModel::Param::MasterMute, masterMute_.getToggleState() ? 0 : 1); };

    // Choosing which mix to *view* changes nothing on the card, so it is live.
    mixBox_.onChange = [this] { model_.selectMix(mixBox_.getSelectedItemIndex()); };

    for (juce::Component* c : { (juce::Component*)&lcdTitle_, (juce::Component*)&lcdDetail_,
                                (juce::Component*)&lcdBudget_, (juce::Component*)&mixLabel_,
                                (juce::Component*)&mixBox_, (juce::Component*)&outputLabel_,
                                (juce::Component*)&master_, (juce::Component*)&masterValue_,
                                (juce::Component*)&masterMute_, (juce::Component*)&banner_ })
        addAndMakeVisible(c);

    showInLcd(model_.connected() ? "PCI-424" : "Not connected", model_.error());
    model_.onChange = [this](bool layout) { modelChanged(layout); };
    modelChanged(true);
}

Console::~Console() { model_.onChange = nullptr; }

int Console::idealWidth() const {
    return (int)juce::jmax<size_t>(model_.strips().size(), 1) * Strip::kWidth + kPanelWidth;
}

void Console::modelChanged(bool layout) {
    const auto& strips = model_.strips();
    if (layout) {
        strips_.clear();
        for (const auto& info : strips) {
            auto strip = std::make_unique<Strip>(info.id, info.interfaceName, info.channelName);
            const int index = (int)strips_.size();
            strip->onEdit = [this, index](ConsoleModel::Param p, int v) { model_.setLocal(p, v, index); };
            strip->onHover = [this](const Strip& s) {
                showInLcd(s.channelName(), s.interfaceName() + "  input " + juce::String(s.inputId()));
            };
            stripHolder_.addAndMakeVisible(*strip);
            strips_.push_back(std::move(strip));
        }
        stripHolder_.setSize((int)strips_.size() * Strip::kWidth, Strip::kHeight);
        for (size_t i = 0; i < strips_.size(); ++i) strips_[i]->setTopLeftPosition((int)i * Strip::kWidth, 0);

        mixBox_.clear(juce::dontSendNotification);
        for (int i = 0; i < model_.numMixes(); ++i) mixBox_.addItem(model_.mixName(i) + "  " + model_.mixOutputName(i), i + 1);
    }
    mixBox_.setSelectedItemIndex(model_.selectedMix(), juce::dontSendNotification);
    for (size_t i = 0; i < strips_.size() && i < strips.size(); ++i) strips_[i]->setState(strips[i].state);

    master_.setValue(model_.masterVolume(), juce::dontSendNotification);
    masterValue_.setText(volumeText(model_.masterVolume()), juce::dontSendNotification);
    masterMute_.setToggleState(model_.masterMute(), juce::dontSendNotification);
    if (model_.noticeTitle().isNotEmpty()) showInLcd(model_.noticeTitle(), model_.noticeDetail());
    const auto& tb = model_.talkback();
    outputLabel_.setText("OUTPUT  " + model_.mixOutputName(model_.selectedMix()) + "\nTALK " + tb.talkName
                         + (tb.talk ? " (on)" : "") + "   LISTEN " + tb.listenName + (tb.listen ? " (on)" : ""),
                         juce::dontSendNotification);
    const auto r = model_.resources();
    lcdBudget_.setText(juce::String(r.used) + " out of " + juce::String(r.max) + " faders in use\n"
                       + juce::String((int)strips.size()) + " inputs, " + juce::String(model_.numMixes()) + " mixes",
                       juce::dontSendNotification);
}

void Console::showInLcd(const juce::String& title, const juce::String& detail) {
    lcdTitle_.setText(title, juce::dontSendNotification);
    lcdDetail_.setText(detail, juce::dontSendNotification);
}

void Console::paint(juce::Graphics& g) {
    g.fillAll(ConsoleLookAndFeel::panel());
    auto panel = getLocalBounds().removeFromRight(kPanelWidth).reduced(10);
    g.setColour(juce::Colours::black);
    g.fillRoundedRectangle(panel.removeFromTop(120).toFloat(), 6.0f);   // the LCD
}

void Console::resized() {
    auto r = getLocalBounds();
    auto panel = r.removeFromRight(kPanelWidth).reduced(10);
    viewport_.setBounds(r.reduced(4, 6));

    auto lcd = panel.removeFromTop(120).reduced(10, 8);
    lcdTitle_.setBounds(lcd.removeFromTop(24));
    lcdBudget_.setBounds(lcd.removeFromBottom(36));
    lcdDetail_.setBounds(lcd);

    panel.removeFromTop(14);
    auto mix = panel.removeFromTop(26);
    mixLabel_.setBounds(mix.removeFromLeft(40));
    mixBox_.setBounds(mix);
    outputLabel_.setBounds(panel.removeFromTop(34));

    panel.removeFromTop(8);
    banner_.setBounds(panel.removeFromBottom(64));
    auto masterArea = panel.removeFromLeft(80);
    masterMute_.setBounds(masterArea.removeFromBottom(22).reduced(8, 0));
    masterValue_.setBounds(masterArea.removeFromBottom(20));
    master_.setBounds(masterArea.reduced(18, 0));
}
