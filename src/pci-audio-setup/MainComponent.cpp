#include "MainComponent.h"

namespace {
// MOTU's original window content area.
constexpr int kWidth  = 602;
constexpr int kHeight = 334;

constexpr int kMargin = 14;
constexpr int kRowH   = 24;
constexpr int kLabelW = 96;

juce::String formatRate(double hz) {
    return (hz >= 1000.0) ? juce::String(hz / 1000.0, (std::fmod(hz, 1000.0) == 0.0) ? 0 : 1) + " kHz"
                          : juce::String((int)hz) + " Hz";
}
}  // namespace

MainComponent::MainComponent() {
    setSize(kWidth, kHeight);

    title_.setText("MOTU PCI Audio Console", juce::dontSendNotification);
    title_.setFont(juce::FontOptions(16.0f, juce::Font::bold));
    addAndMakeVisible(title_);

    statusLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(statusLabel_);

    rateLabel_.setText("Sample Rate", juce::dontSendNotification);
    clockLabel_.setText("Clock Source", juce::dontSendNotification);
    interfacesLabel_.setText("AudioWire", juce::dontSendNotification);
    for (auto* l : { &rateLabel_, &clockLabel_, &interfacesLabel_ })
        addAndMakeVisible(*l);

    addAndMakeVisible(rateBox_);
    addAndMakeVisible(clockBox_);

    interfaces_.setMultiLine(true);
    interfaces_.setReadOnly(true);
    interfaces_.setScrollbarsShown(true);
    interfaces_.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 11.0f, 0));
    addAndMakeVisible(interfaces_);

    usageLabel_.setFont(juce::FontOptions(11.0f));
    addAndMakeVisible(usageLabel_);

    addAndMakeVisible(refreshButton_);
    refreshButton_.onClick = [this] { connect(); };

    rateBox_.onChange = [this] {
        const int i = rateBox_.getSelectedItemIndex();
        if (i >= 0 && i < (int)rates_.size())
            if (!motu::device::setSampleRate(deviceId_, rates_[(size_t)i]))
                setStatus("Could not set sample rate.", true);
    };

    clockBox_.onChange = [this] {
        const int i = clockBox_.getSelectedItemIndex();
        if (i >= 0 && i < (int)clocks_.size())
            if (!motu::device::setClockSource(deviceId_, clocks_[(size_t)i]))
                setStatus("Could not set clock source.", true);
    };

    connect();
}

MainComponent::~MainComponent() = default;

void MainComponent::setStatus(const juce::String& text, bool isError) {
    statusLabel_.setText(text, juce::dontSendNotification);
    statusLabel_.setColour(juce::Label::textColourId,
                           isError ? juce::Colours::orangered : juce::Colours::green.darker(0.4f));
}

void MainComponent::connect() {
    deviceId_ = motu::Card::findDevice();
    if (deviceId_ == 0) {
        setStatus("PCI-424 not found. Is MOTUPCIAudio.kext loaded?", true);
        return;
    }

    refreshRates();
    refreshClocks();

    std::string err;
    card_ = motu::Card::open(deviceId_, &err);
    if (!card_) {
        setStatus(juce::String(err), true);
        interfaces_.setText({});
        return;
    }
    refreshInterfaces();
}

void MainComponent::refreshRates() {
    rates_ = motu::device::availableSampleRates(deviceId_);
    const double current = motu::device::sampleRate(deviceId_);

    rateBox_.clear(juce::dontSendNotification);
    for (size_t i = 0; i < rates_.size(); ++i)
        rateBox_.addItem(formatRate(rates_[i]), (int)i + 1);

    for (size_t i = 0; i < rates_.size(); ++i)
        if (juce::approximatelyEqual(rates_[i], current))
            rateBox_.setSelectedItemIndex((int)i, juce::dontSendNotification);
}

void MainComponent::refreshClocks() {
    clocks_ = motu::device::clockSources(deviceId_);
    const UInt32 current = motu::device::clockSource(deviceId_);

    clockBox_.clear(juce::dontSendNotification);
    for (size_t i = 0; i < clocks_.size(); ++i) {
        auto name = motu::device::clockSourceName(deviceId_, clocks_[i]);
        clockBox_.addItem(name.empty() ? juce::String((int)clocks_[i]) : juce::String(name),
                          (int)i + 1);
    }

    for (size_t i = 0; i < clocks_.size(); ++i)
        if (clocks_[i] == current)
            clockBox_.setSelectedItemIndex((int)i, juce::dontSendNotification);
}

void MainComponent::refreshInterfaces() {
    motu::Exception e;
    juce::StringArray lines;

    const int wires = card_.numWires(e);
    for (int w = 0; w < wires; ++w) {
        if (!card_.wireConnected(e, w)) {
            lines.add(juce::String(w) + ":  -");
            continue;
        }
        // Only ask for the name once we know the wire is live — an out-of-range
        // or empty wire segfaults the driver. See motu_card.h.
        auto name = juce::String(card_.wireInterfaceName(e, w));
        motu::Interface itf = card_.wireInterface(e, w);
        if (!itf) { lines.add(juce::String(w) + ":  " + name); continue; }

        lines.add(juce::String(w) + ":  " + name + "   " + juce::String(itf.versionString(e)));

        const int banks = itf.numBanks(e);
        for (int b = 0; b < banks; ++b) {
            juce::StringArray personalities;
            for (int n = 0; n < 8; ++n) {
                auto p = itf.nthBankPersonality(e, b, n);
                if (p.empty()) break;
                personalities.add(juce::String(p));
            }
            const int active = itf.personalityForBank(e, b);
            lines.add("        bank " + juce::String(b) + ":  "
                      + juce::String(itf.numChannelsInBank(e, b)) + " ch, "
                      + (juce::isPositiveAndBelow(active, personalities.size())
                             ? personalities[active] : juce::String(active))
                      + "   [" + personalities.joinIntoString(", ") + "]");
        }
    }
    interfaces_.setText(lines.joinIntoString("\n"), juce::dontSendNotification);

    const int inActive  = card_.numActiveInputs(e);
    const int outActive = card_.numActiveOutputs(e);
    juce::String usage = juce::String(inActive) + " in / " + juce::String(outActive)
                       + " out active of " + juce::String(card_.numInputs(e));

    motu::CueMix cue = card_.cueMix(e);
    if (cue) {
        auto r = cue.resources(e);
        usage += "        CueMix: " + juce::String(r.used) + " of "
               + juce::String(r.max) + " faders";
    }
    usageLabel_.setText(usage, juce::dontSendNotification);

    setStatus("Connected to PCI-424.", false);
}

void MainComponent::paint(juce::Graphics& g) {
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void MainComponent::resized() {
    auto r = getLocalBounds().reduced(kMargin);

    title_.setBounds(r.removeFromTop(22));
    r.removeFromTop(8);

    auto row = r.removeFromTop(kRowH);
    rateLabel_.setBounds(row.removeFromLeft(kLabelW));
    rateBox_.setBounds(row.removeFromLeft(140));
    row.removeFromLeft(kMargin);
    clockLabel_.setBounds(row.removeFromLeft(kLabelW));
    clockBox_.setBounds(row);

    r.removeFromTop(kMargin);
    interfacesLabel_.setBounds(r.removeFromTop(18));
    interfaces_.setBounds(r.removeFromTop(150));

    r.removeFromTop(6);
    usageLabel_.setBounds(r.removeFromTop(18));

    auto bottom = r.removeFromBottom(kRowH);
    refreshButton_.setBounds(bottom.removeFromRight(90));
    statusLabel_.setBounds(bottom);
}
