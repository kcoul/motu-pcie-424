#include "Console.h"

#include "ConsoleLookAndFeel.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr int kPanelWidth = 250;
constexpr int kPollHz = 10;
const double kFaderSkew = std::log(0.75) / std::log(0.5);

void styleLabel(juce::Label& l, float size, juce::Colour c, juce::Justification j = juce::Justification::centredLeft) {
    l.setFont(juce::FontOptions(size));
    l.setColour(juce::Label::textColourId, c);
    l.setJustificationType(j);
}

// "HD192:Analog-A" -> { "HD192", "Analog-A" }
std::pair<juce::String, juce::String> splitDescription(const juce::String& d) {
    const int colon = d.indexOfChar(':');
    if (colon < 0) return { {}, d };
    return { d.substring(0, colon), d.substring(colon + 1) };
}
}  // namespace

Console::Console() {
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
    banner_.setText("Read-only first pass: shows the card's CueMix state live. Controls will write "
                    "once their encodings are verified (docs/CUEMIX-PLAN.md).", juce::dontSendNotification);

    master_.setNormalisableRange({ 0.0, 65536.0, 1.0, kFaderSkew });
    master_.setInterceptsMouseClicks(false, false);
    masterMute_.setColour(juce::TextButton::buttonColourId, ConsoleLookAndFeel::well());
    masterMute_.setColour(juce::TextButton::buttonOnColourId, ConsoleLookAndFeel::mute());
    masterMute_.setColour(juce::TextButton::textColourOnId, juce::Colours::black);
    masterMute_.setInterceptsMouseClicks(false, false);

    // Choosing which mix to *view* changes nothing on the card, so it is live.
    mixBox_.onChange = [this] {
        const int i = mixBox_.getSelectedItemIndex();
        if (juce::isPositiveAndBelow(i, (int)buses_.size())) bus_ = buses_[(size_t)i];
        poll();
    };

    for (juce::Component* c : { (juce::Component*)&lcdTitle_, (juce::Component*)&lcdDetail_,
                                (juce::Component*)&lcdBudget_, (juce::Component*)&mixLabel_,
                                (juce::Component*)&mixBox_, (juce::Component*)&outputLabel_,
                                (juce::Component*)&master_, (juce::Component*)&masterValue_,
                                (juce::Component*)&masterMute_, (juce::Component*)&banner_ })
        addAndMakeVisible(c);

    connect();
    startTimerHz(kPollHz);
}

Console::~Console() { stopTimer(); }

void Console::connect() {
    dev_ = motu::Card::findDevice();
    std::string err;
    if (dev_) card_ = motu::Card::open(dev_, &err);
    else err = "No PCI-424 was found. Is MOTUPCIAudio.kext loaded?";
    showInLcd(card_ ? "PCI-424" : "Not connected", card_ ? juce::String() : juce::String(err));
    syncLayout();
}

int Console::idealWidth() const {
    return (int)juce::jmax<size_t>(strips_.size(), 1) * Strip::kWidth + kPanelWidth;
}

void Console::timerCallback() {
    syncLayout();
    poll();
}

juce::String Console::busName(int bus) {
    motu::Exception e;
    const auto [iface, kind] = splitDescription(card_.outputDescription(e, bus));
    const int n = card_.bankRelativeID(e, bus) + 1;
    return iface + ":" + kind + " " + juce::String(n) + "-" + juce::String(n + 1);
}

// Strips are the card's active inputs; mixes are the output pairs CueMix
// accepts as buses. Both are re-read every tick and rebuilt only on change.
void Console::syncLayout() {
    if (!card_) return;
    motu::Exception e;

    std::vector<int> ids;
    for (int n = 0, count = card_.numActiveInputs(e); n < count; ++n) {
        const int id = card_.nthActiveInputID(e, n);
        if (!e.raised()) ids.push_back(id);
    }
    std::vector<int> buses;
    for (int id = 0, count = card_.numOutputs(e); id + 1 < count; id += 2) {
        motu::Exception x;
        if (!card_.outputState(x, id).exists) continue;
        card_.cueMix(x).busVolume(x, id);          // range-checked: raises if not a bus
        if (!x.raised()) buses.push_back(id);
    }

    if (ids != inputIds_) {
        inputIds_ = ids;
        strips_.clear();
        for (int id : inputIds_) {
            const auto [iface, kind] = splitDescription(card_.inputDescription(e, id));
            juce::String name = card_.channelName(e, id, true);
            if (name.isEmpty()) name = kind + " " + juce::String(card_.bankRelativeID(e, id) + 1);
            auto strip = std::make_unique<Strip>(id, iface, name);
            strip->onHover = [this](const Strip& s) {
                showInLcd(s.channelName(), s.interfaceName() + "  input " + juce::String(s.inputId()));
            };
            stripHolder_.addAndMakeVisible(*strip);
            strips_.push_back(std::move(strip));
        }
        stripHolder_.setSize((int)strips_.size() * Strip::kWidth, Strip::kHeight);
        for (size_t i = 0; i < strips_.size(); ++i)
            strips_[i]->setTopLeftPosition((int)i * Strip::kWidth, 0);
    }

    if (buses != buses_) {
        buses_ = buses;
        mixBox_.clear(juce::dontSendNotification);
        for (size_t i = 0; i < buses_.size(); ++i) {
            mixBox_.addItem(busName(buses_[i]), (int)i + 1);
            if (buses_[i] == bus_) mixBox_.setSelectedItemIndex((int)i, juce::dontSendNotification);
        }
        if (mixBox_.getSelectedItemIndex() < 0 && !buses_.empty()) {
            bus_ = buses_.front();
            mixBox_.setSelectedItemIndex(0, juce::dontSendNotification);
        }
    }
}

void Console::poll() {
    if (!card_) return;
    motu::Exception e;
    motu::CueMix cue = card_.cueMix(e);
    if (!cue) return;

    const bool validBus = std::find(buses_.begin(), buses_.end(), bus_) != buses_.end();
    for (auto& s : strips_) {
        StripState st;
        const int id = s->inputId();
        st.trim = cue.inputTrim(e, id);
        st.inputMute = cue.inputMute(e, id);
        if (validBus) {
            st.volume = cue.volume(e, bus_, id);
            st.pan = cue.pan(e, bus_, id);
            st.mute = cue.mute(e, bus_, id);
            st.solo = cue.solo(e, bus_, id);
        }
        s->setState(st);
    }

    if (validBus) {
        const int v = cue.busVolume(e, bus_);
        master_.setValue(v, juce::dontSendNotification);
        masterValue_.setText(volumeText(v), juce::dontSendNotification);
        masterMute_.setToggleState(cue.busMute(e, bus_), juce::dontSendNotification);
        outputLabel_.setText("OUTPUT  " + busName(bus_), juce::dontSendNotification);
    }
    const auto r = cue.resources(e);
    lcdBudget_.setText(juce::String(r.used) + " out of " + juce::String(r.max) + " faders in use\n"
                       + juce::String((int)strips_.size()) + " inputs, " + juce::String((int)buses_.size())
                       + " mixes", juce::dontSendNotification);
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
    outputLabel_.setBounds(panel.removeFromTop(22));

    panel.removeFromTop(8);
    banner_.setBounds(panel.removeFromBottom(64));
    auto masterArea = panel.removeFromLeft(80);
    masterMute_.setBounds(masterArea.removeFromBottom(22).reduced(8, 0));
    masterValue_.setBounds(masterArea.removeFromBottom(20));
    master_.setBounds(masterArea.reduced(18, 0));
}
