#include "MainComponent.h"

#include "ChannelNamesWindow.h"
#include "OptionsWindow.h"

namespace {
// MOTU's original window content area.
constexpr int kWidth  = 602;
constexpr int kHeight = 334;

// Every coordinate below is measured off docs/reference/setup-main-*.png, in
// content-area pixels (window frame less Mojave's 22 px title bar).
constexpr int kLeft      = 16;
constexpr int kRight     = 586;
constexpr int kCtlH      = 21;
constexpr int kGridW     = 570;     // column width is kGridW / columns
constexpr int kPairsPerColumn = 4;
constexpr int kRowPitch  = 25;
constexpr int kFirstRowY = 180;     // centre of the "1-2" row
constexpr int kGridHeight = 120;    // what the window loses when routing is off
constexpr int kDisableItemId = 1000;

juce::Font uiFont(float size = MacLookAndFeel::kFontSize) { return juce::Font(juce::FontOptions(size)); }

void initLabel(juce::Component& parent, juce::Label& l, const juce::String& text,
               juce::Justification j = juce::Justification::centredLeft) {
    l.setText(text, juce::dontSendNotification);
    l.setFont(uiFont());
    l.setJustificationType(j);
    l.setBorderSize({});
    parent.addAndMakeVisible(l);
}

// MOTU's GetStereoChannelDescription, from each channel's CoreAudio category
// ('lccn') and number ('lcnn') names:
//   same category       "Bay" "3" + "Bay" "4"        -> "Bay 3 - 4"
//   different categories "Console L" "" + "Console R" "" -> "Console L  - Console R "
// The double and trailing spaces are MOTU's, from joining empty numbers.
juce::String pairLabel(const juce::String& catL, const juce::String& numL,
                       const juce::String& catR, const juce::String& numR) {
    if (catL == catR)
        return catL + " " + (numL.isNotEmpty() ? numL : "L") + " - " + (numR.isNotEmpty() ? numR : "R");
    return catL + " " + numL + " - " + catR + " " + numR;
}

juce::String bankLetter(int b) { return juce::String::charToString((juce::juce_wchar)('A' + b)); }
}  // namespace

MainComponent::MainComponent() {
    setSize(kWidth, kHeight);

    initLabel(*this, rateLabel_,   "Sample Rate");
    initLabel(*this, clockLabel_,  "Clock Source");
    initLabel(*this, defInLabel_,  "Default Input");
    initLabel(*this, defOutLabel_, "Default Output");
    initLabel(*this, configureLabel_, "Configure Interface");
    initLabel(*this, audiowireLabel_, "Audiowire:");
    initLabel(*this, usageLabel_, {});

    for (auto* c : { &rateBox_, &clockBox_, &defInBox_, &defOutBox_, &interfaceBox_ })
        addAndMakeVisible(*c);
    addAndMakeVisible(topRule_);
    addAndMakeVisible(bottomRule_);
    addAndMakeVisible(routingToggle_);
    addAndMakeVisible(optionsButton_);
    addAndMakeVisible(namesButton_);
    addAndMakeVisible(volumeToggle_);

    rateBox_.onChange = [this] {
        const int i = rateBox_.getSelectedItemIndex();
        if (juce::isPositiveAndBelow(i, (int)rates_.size())
            && !motu::device::setSampleRate(deviceId_, rates_[(size_t)i]))
            if (onError) onError("The sample rate could not be changed.");
        refreshUsage();
    };

    clockBox_.onChange = [this] {
        const int i = clockBox_.getSelectedItemIndex();
        if (juce::isPositiveAndBelow(i, (int)clocks_.size())
            && !motu::device::setClockSource(deviceId_, clocks_[(size_t)i]))
            if (onError) onError("The clock source could not be changed.");
    };

    defInBox_.onChange  = [this] { setDefaultPair(true,  defInBox_.getSelectedItemIndex()); };
    defOutBox_.onChange = [this] { setDefaultPair(false, defOutBox_.getSelectedItemIndex()); };

    interfaceBox_.onChange = [this] {
        const int i = interfaceBox_.getSelectedItemIndex();
        if (!juce::isPositiveAndBelow(i, (int)wires_.size())) return;
        currentWire_ = wires_[(size_t)i];
        rebuildGrid();
    };

    routingToggle_.onClick = [this] { setRoutingEnabled(routingToggle_.getToggleState()); };
    volumeToggle_.onClick  = [this] { setVolumeControlsEnabled(volumeToggle_.getToggleState()); };
    optionsButton_.onClick = [this] { showInterfaceOptions(); };
    namesButton_.onClick = [this] { showChannelNames(); };

    juce::PropertiesFile::Options opts;
    opts.applicationName     = "MOTU PCI Audio Setup";
    opts.filenameSuffix      = ".settings";
    opts.osxLibrarySubFolder = "Application Support";
    prefs_ = std::make_unique<juce::PropertiesFile>(opts);

    connect();
}

MainComponent::~MainComponent() {
    cancelPendingUpdate();
    // CommitChanges only *queues* the prefs save on this run loop. MOTU's
    // console never flushed, but if we quit (or crash) before the queue runs,
    // a committed change is live on the card yet missing from the prefs file
    // the driver restores from. Seen on 2026-09-12. Flushing is harmless.
    if (card_) {
        motu::Exception e;
        card_.flushPrefs(e);
    }
    motu::device::removeListener(listener_);
    namesWindow_.reset();
    optionsWindow_.reset();
}

juce::String MainComponent::prefsKey(const juce::String& name) const {
    return juce::String(motu::device::deviceUID(deviceId_)) + "." + name;
}

void MainComponent::report(const juce::String& what, const motu::Exception& e) {
    if (!e.raised()) return;
    juce::Logger::writeToLog(what + ": " + juce::String(e.str()));
    if (onError) onError(what + "\n\nThe driver has reported an error.\n" + juce::String(e.str()));
}

// --- reading -----------------------------------------------------------------

void MainComponent::connect() {
    deviceId_ = motu::Card::findDevice();
    if (deviceId_ == 0) {
        if (onError) onError("No PCI-424 was found. Is MOTUPCIAudio.kext loaded?");
        return;
    }
    std::string err;
    card_ = motu::Card::open(deviceId_, &err);
    if (!card_ && onError) onError(juce::String(err));
    routingEnabled_ = prefs_->getBoolValue(prefsKey("EnableRouting"), true);

    // What MOTU's console listens to (sample rate, stream formats, clock source,
    // clock sources, default stereo pair), plus the stream layout, which moves
    // whenever channels are enabled from anywhere. Coalesced into one refresh.
    motu::device::removeListener(listener_);
    listener_ = motu::device::addListener(deviceId_,
        { kAudioDevicePropertyNominalSampleRate, kAudioStreamPropertyAvailablePhysicalFormats,
          kAudioDevicePropertyClockSource, kAudioDevicePropertyClockSources,
          kAudioDevicePropertyPreferredChannelsForStereo, kAudioDevicePropertyStreamConfiguration },
        ^{ triggerAsyncUpdate(); });
    refresh();
}

void MainComponent::refresh() {
    if (deviceId_ == 0) return;
    refreshRates();
    refreshClocks();
    refreshDefaults();
    refreshVolumeControls();
    refreshInterfaces();
}

void MainComponent::probeAndRefresh() {
    if (card_) {
        motu::Exception e;
        card_.probeForInterfaces(e);
        report("Could not probe for interfaces", e);
    }
    refresh();
}

void MainComponent::refreshRates() {
    rates_ = motu::device::availableSampleRates(deviceId_);
    const double current = motu::device::sampleRate(deviceId_);
    rateBox_.clear(juce::dontSendNotification);
    for (size_t i = 0; i < rates_.size(); ++i) {
        rateBox_.addItem(juce::String((int)rates_[i]), (int)i + 1);
        if (juce::approximatelyEqual(rates_[i], current))
            rateBox_.setSelectedItemIndex((int)i, juce::dontSendNotification);
    }
}

void MainComponent::refreshClocks() {
    clocks_ = motu::device::clockSources(deviceId_);
    const UInt32 current = motu::device::clockSource(deviceId_);
    clockBox_.clear(juce::dontSendNotification);
    for (size_t i = 0; i < clocks_.size(); ++i) {
        auto name = motu::device::clockSourceName(deviceId_, clocks_[i]);
        clockBox_.addItem(name.empty() ? juce::String((int)clocks_[i]) : juce::String(name), (int)i + 1);
        if (clocks_[i] == current)
            clockBox_.setSelectedItemIndex((int)i, juce::dontSendNotification);
    }
}

void MainComponent::refreshDefaults() {
    for (const bool isInput : { true, false }) {
        auto& box = isInput ? defInBox_ : defOutBox_;
        box.clear(juce::dontSendNotification);
        const int n = motu::device::channelCount(deviceId_, isInput);
        for (int ch = 1; ch + 1 <= n; ch += 2) {
            auto cat = [&](int c) { return juce::String(motu::device::channelCategory(deviceId_, isInput, c)); };
            auto num = [&](int c) { return juce::String(motu::device::channelNumber(deviceId_, isInput, c)); };
            box.addItem(pairLabel(cat(ch), num(ch), cat(ch + 1), num(ch + 1)), ch / 2 + 1);
        }
        UInt32 l = 0, r = 0;
        if (motu::device::preferredStereo(deviceId_, isInput, l, r) && l >= 1 && r == l + 1)
            box.setSelectedItemIndex((int)(l - 1) / 2, juce::dontSendNotification);
    }
}

void MainComponent::refreshVolumeControls() {
    UInt32 v = 0;
    const bool has = motu::device::uint32Property(deviceId_, 'Mvol', kAudioObjectPropertyScopeOutput, v);
    volumeToggle_.setEnabled(has);
    volumeToggle_.setToggleState(has && v != 0, juce::dontSendNotification);
}

void MainComponent::refreshInterfaces() {
    const int previous = currentWire_;
    wires_.clear();
    interfaceBox_.clear(juce::dontSendNotification);
    if (!card_) { rebuildGrid(); return; }

    motu::Exception e;
    const int n = card_.numWires(e);
    for (int w = 0; w < n; ++w) {
        // Name only after connected: an empty wire segfaults the driver.
        if (!card_.wireConnected(e, w)) continue;
        wires_.push_back(w);
        interfaceBox_.addItem(juce::String(card_.wireInterfaceName(e, w)), (int)wires_.size());
    }

    currentWire_ = wires_.empty() ? -1 : wires_.front();
    for (size_t i = 0; i < wires_.size(); ++i)
        if (wires_[i] == previous) currentWire_ = previous;
    for (size_t i = 0; i < wires_.size(); ++i)
        if (wires_[i] == currentWire_)
            interfaceBox_.setSelectedItemIndex((int)i, juce::dontSendNotification);

    rebuildGrid();
}

// The grid: one popup per bank, then its channel pairs in columns of four.
// HD192 (one 12-channel bank) gives 2 columns; a 24I/O (one 24-channel bank)
// gives 3; the 2408mk3 (three 8-channel banks) gives 3, one under each popup.
void MainComponent::rebuildGrid() {
    for (auto& b : banks_) { removeChildComponent(b.label.get()); removeChildComponent(b.personality.get()); }
    for (auto& r : rows_)  { removeChildComponent(r.label.get()); removeChildComponent(r.in.get()); removeChildComponent(r.out.get()); }
    for (auto& h : headers_) removeChildComponent(h.get());
    banks_.clear(); rows_.clear(); headers_.clear();
    columns_ = 0;

    const bool live = card_ && currentWire_ >= 0;
    audiowireLabel_.setText(live ? "Audiowire: " + juce::String(currentWire_ + 1) : "Audiowire:",
                            juce::dontSendNotification);
    optionsButton_.setEnabled(live);
    namesButton_.setEnabled((bool)card_);
    routingToggle_.setEnabled((bool)card_);
    routingToggle_.setToggleState(routingEnabled_, juce::dontSendNotification);
    setSize(kWidth, routingEnabled_ ? kHeight : kHeight - kGridHeight);

    if (!live) { refreshUsage(); return; }

    motu::Exception e;
    motu::Interface itf = card_.wireInterface(e, currentWire_);
    if (!itf) { report("Could not open the interface", e); refreshUsage(); return; }

    const int firstIdOfWire = currentWire_ * 24;
    int offset = 0;
    const int nBanks = itf.numBanks(e);
    for (int b = 0; b < nBanks; ++b) {
        Bank bank;
        bank.index = b;
        bank.firstColumn = columns_;
        bank.label = std::make_unique<juce::Label>();
        initLabel(*this, *bank.label, "Bank " + bankLetter(b));
        bank.personality = std::make_unique<juce::ComboBox>();
        auto* box = bank.personality.get();
        for (int p = 0; p < 8; ++p) {
            auto name = itf.nthBankPersonality(e, b, p);
            if (name.empty()) break;
            box->addItem(juce::String(name), p + 1);
        }
        // Without routing, a bank is switched as a whole: MOTU adds "Disable".
        const bool bankOn = routingEnabled_ || isBankOn(itf, currentWire_, b);
        if (!routingEnabled_) {
            box->addSeparator();
            box->addItem("Disable", kDisableItemId);
        }
        if (bankOn) box->setSelectedItemIndex(itf.personalityForBank(e, b), juce::dontSendNotification);
        else        box->setSelectedId(kDisableItemId, juce::dontSendNotification);
        box->onChange = [this, b, box] {
            if (box->getSelectedId() == kDisableItemId) setBankDisabled(b);
            else setBankPersonality(b, box->getSelectedId() - 1);
        };
        addAndMakeVisible(*box);

        // A bank reports its slot size; the grid shows only the channels the
        // interface has (an HD192 fills 12 of its 24).
        const int chans = itf.numChannelsInBank(e, b);
        const auto range = bankRange(itf, currentWire_, b);
        int pairs = 0;
        bank.firstRow = rows_.size();
        for (int c = 0; routingEnabled_ && c + 1 < range.count; c += 2) {
            const int id = firstIdOfWire + offset + c;
            PairRow row;
            row.firstId = id;
            row.label = std::make_unique<juce::Label>();
            initLabel(*this, *row.label, juce::String(c + 1) + "-" + juce::String(c + 2));
            row.in  = std::make_unique<juce::ToggleButton>();
            row.out = std::make_unique<juce::ToggleButton>();
            row.in->onClick  = [this, id, t = row.in.get()]  { setPairInputEnabled(id,  t->getToggleState()); };
            row.out->onClick = [this, id, t = row.out.get()] { setPairOutputEnabled(id, t->getToggleState()); };
            addAndMakeVisible(*row.in);
            addAndMakeVisible(*row.out);
            rows_.push_back(std::move(row));
            ++pairs;
        }
        bank.rowCount = (size_t)pairs;
        offset += chans;
        const int bankPairs = range.count / 2;
        columns_ += juce::jmax(1, (bankPairs + kPairsPerColumn - 1) / kPairsPerColumn);
        banks_.push_back(std::move(bank));
    }

    for (int c = 0; routingEnabled_ && c < columns_; ++c)
        for (const auto* text : { "Enable\nInput", "Enable\nOutput" }) {
            auto h = std::make_unique<juce::Label>();
            initLabel(*this, *h, text, juce::Justification::centred);
            h->setFont(uiFont(15.0f));
            headers_.push_back(std::move(h));
        }

    refreshGridState();
    layoutGrid();
}

void MainComponent::refreshGridState() {
    motu::Exception e;
    for (auto& r : rows_) {
        r.in->setToggleState(card_.inputState(e, r.firstId).enabled != 0, juce::dontSendNotification);
        r.out->setToggleState(card_.outputState(e, r.firstId).enabled(), juce::dontSendNotification);
    }
    refreshUsage();
}

// "PCI Use: Ins enabled %ld, Outs enabled %ld, Aprx %.2f MB per sec." — MOTU's
// own format string. The counts are the driver's active channel counts, and
// the rate is (ins + outs + 2) channels of the output stream's sample width.
void MainComponent::refreshUsage() {
    if (!card_) { usageLabel_.setText({}, juce::dontSendNotification); return; }
    motu::Exception e;
    const int ins = card_.numActiveInputs(e), outs = card_.numActiveOutputs(e);
    int bytes = motu::device::bytesPerSample(deviceId_);
    if (bytes <= 0) bytes = 4;
    const int chans = ins + outs > 0 ? ins + outs + 2 : 0;
    const double mb = chans * motu::device::sampleRate(deviceId_) * bytes / 1048576.0;
    usageLabel_.setText("PCI Use: Ins enabled " + juce::String(ins) + ", Outs enabled " + juce::String(outs)
                        + ", Aprx " + juce::String(mb, 2) + " MB per sec.", juce::dontSendNotification);
}

// --- writes ------------------------------------------------------------------
//
// Each follows the sequence MOTU's console uses (docs/CHANNEL-STATE.md): make
// the change, mirror it onto the pair's partner channel, then CommitChanges(true),
// which waits up to 5 s for the driver to acknowledge and queues a prefs save.

void MainComponent::commit(const juce::String& what) {
    motu::Exception e;
    card_.commitChanges(e, true);
    report(what, e);
}

void MainComponent::afterChannelChange() {
    refreshGridState();
    refreshDefaults();
}

void MainComponent::setPairInputEnabled(int firstId, bool on) {
    motu::Exception e;
    card_.setInputEnable(e, firstId, on);
    report("Could not change the input", e);
    card_.setInputEnable(e, firstId + 1, on);
    report("Could not change the input", e);
    commit("Could not apply the input change");
    afterChannelChange();
}

void MainComponent::setPairOutputEnabled(int firstId, bool on) {
    const int source = on ? motu::Card::kOutputEnabled : motu::Card::kOutputDisabled;
    motu::Exception e;
    card_.setOutputSource(e, firstId, source);
    report("Could not change the output", e);
    card_.setOutputSource(e, firstId + 1, source);
    report("Could not change the output", e);
    commit("Could not apply the output change");
    afterChannelChange();
}

void MainComponent::setBankPersonality(int bank, int personality) {
    if (personality < 0 || currentWire_ < 0) return;
    motu::Exception e;
    motu::Interface itf = card_.wireInterface(e, currentWire_);
    if (!itf) return;
    itf.setPersonalityForBank(e, bank, personality);
    report("Could not change the bank", e);
    if (!routingEnabled_) setBankOn(itf, currentWire_, bank, true);
    commit("Could not apply the bank change");
    // The popup that called us is rebuilt, so never inline.
    triggerAsyncUpdate();
}

void MainComponent::setBankDisabled(int bank) {
    motu::Exception e;
    motu::Interface itf = card_.wireInterface(e, currentWire_);
    if (!itf) return;
    setBankOn(itf, currentWire_, bank, false);
    commit("Could not disable the bank");
    triggerAsyncUpdate();
}

MainComponent::BankRange MainComponent::bankRange(motu::Interface& itf, int wire, int bank) {
    motu::Exception e;
    int first = 0;
    for (int b = 0; b < bank; ++b) first += itf.numChannelsInBank(e, b);
    const int n = itf.numChannelsInBank(e, bank);
    int count = 0;
    for (int c = first; c < first + n; ++c)
        if (itf.inputChannelAvailable(e, c) || itf.outputChannelAvailable(e, c)) ++count;
    return { wire * 24 + first, count };
}

// MOTU's SetBankOnInNonRoutingMode: every channel of the bank on or off.
void MainComponent::setBankOn(motu::Interface& itf, int wire, int bank, bool on) {
    const auto r = bankRange(itf, wire, bank);
    motu::Exception e;
    for (int id = r.firstId; id < r.firstId + r.count; ++id) {
        card_.setInputEnable(e, id, on);
        card_.setOutputSource(e, id, on ? motu::Card::kOutputEnabled : motu::Card::kOutputDisabled);
    }
    report("Could not switch the bank", e);
}

// MOTU's IsBankOnInNonRoutingMode: on if anything is on; uniform if every
// input and output agrees and nothing is routed.
bool MainComponent::isBankOn(motu::Interface& itf, int wire, int bank, bool* uniform) {
    const auto r = bankRange(itf, wire, bank);
    motu::Exception e;
    bool any = false, all = true;
    for (int id = r.firstId; id < r.firstId + r.count; ++id) {
        const auto in = card_.inputState(e, id);
        const auto out = card_.outputState(e, id);
        if (in.exists)  { any |= in.enabled != 0; all &= in.enabled != 0; }
        if (out.exists) { any |= out.enabled();   all &= out.enabled(); }
        if (out.source >= 0 && uniform) *uniform = false;
    }
    if (uniform) *uniform = *uniform && (all || !any);
    return any;
}

void MainComponent::setRoutingEnabled(bool on) {
    if (!card_) return;
    if (!on) {
        // Every wire and every bank, not just the one on screen: a bank left
        // half on is forced all on; a uniform bank is left alone.
        motu::Exception e;
        for (int w : wires_) {
            motu::Interface itf = card_.wireInterface(e, w);
            if (!itf) continue;
            for (int b = 0, n = itf.numBanks(e); b < n; ++b) {
                bool uniform = true;
                const bool any = isBankOn(itf, w, b, &uniform);
                if (!uniform) setBankOn(itf, w, b, any);
            }
        }
    }
    routingEnabled_ = on;
    prefs_->setValue(prefsKey("EnableRouting"), on);
    prefs_->saveIfNeeded();
    commit("Could not apply the routing change");
    triggerAsyncUpdate();
}

void MainComponent::setVolumeControlsEnabled(bool on) {
    if (!motu::device::setUint32Property(deviceId_, 'Mvol', kAudioObjectPropertyScopeOutput, on ? 1 : 0)
        && onError)
        onError("The volume controls setting could not be changed.");
    refreshVolumeControls();
}

void MainComponent::setDefaultPair(bool isInput, int pairIndex) {
    if (pairIndex < 0) return;
    const UInt32 l = (UInt32)pairIndex * 2 + 1;
    if (!motu::device::setPreferredStereo(deviceId_, isInput, l, l + 1) && onError)
        onError("The default channels could not be changed.");
    refreshDefaults();
}

void MainComponent::selectInterface(int index) {
    if (juce::isPositiveAndBelow(index, (int)wires_.size()))
        interfaceBox_.setSelectedItemIndex(index, juce::sendNotificationSync);
}

// --- configuration files -----------------------------------------------------

void MainComponent::saveConfiguration() {
    if (!card_) return;
    chooser_ = std::make_unique<juce::FileChooser>("Save Configuration",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("MOTU PCI Config.mcfg"),
        "*.mcfg");
    chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this](const juce::FileChooser& fc) {
        const auto file = fc.getResult();
        if (file == juce::File()) return;
        motu::Exception e;
        const auto xml = card_.saveConfiguration(e);
        if (xml.empty() || !file.replaceWithData(xml.data(), xml.size())) {
            report("Could not save the configuration", e);
            if (!e.raised() && onError) onError("Could not write " + file.getFullPathName());
        }
    });
}

void MainComponent::loadConfiguration() {
    if (!card_) return;
    chooser_ = std::make_unique<juce::FileChooser>("Load Configuration",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*.mcfg");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this](const juce::FileChooser& fc) {
        const auto file = fc.getResult();
        if (!file.existsAsFile()) return;
        juce::MemoryBlock data;
        if (!file.loadFileAsData(data)) return;
        const auto* bytes = static_cast<const unsigned char*>(data.getData());
        motu::Exception e;
        if (!card_.loadConfiguration(e, std::vector<unsigned char>(bytes, bytes + data.getSize()))) {
            report("Could not load the configuration", e);
            if (!e.raised() && onError) onError(file.getFileName() + " is not a MOTU PCI configuration.");
        }
        triggerAsyncUpdate();
    });
}

// --- options -----------------------------------------------------------------

void MainComponent::showInterfaceOptions() {
    if (!card_ || currentWire_ < 0) return;
    optionsWindow_ = OptionsWindow::create(*this, card_, currentWire_, [this] { optionsWindow_.reset(); });
}

void MainComponent::showChannelNames() {
    if (!card_) return;
    if (namesWindow_) { namesWindow_->toFront(true); return; }
    // Names change the Default In/Out labels (they come from CoreAudio's names).
    namesWindow_ = ChannelNamesWindow::create(*this, card_, [this] { refreshDefaults(); },
                                              [this] { namesWindow_.reset(); });
}

// --- layout ------------------------------------------------------------------

void MainComponent::paint(juce::Graphics& g) {
    g.fillAll(MacLookAndFeel::windowBackground());
}

void MainComponent::resized() {
    auto rowAt = [](int centreY) { return centreY - kCtlH / 2; };

    rateLabel_.setBounds  (kLeft, rowAt(26), 100, kCtlH);
    rateBox_.setBounds    (116,   rowAt(26), 177, kCtlH);
    clockLabel_.setBounds (kLeft, rowAt(53), 100, kCtlH);
    clockBox_.setBounds   (116,   rowAt(53), 177, kCtlH);
    defInLabel_.setBounds (309,   rowAt(26), 100, kCtlH);
    defInBox_.setBounds   (409,   rowAt(26), 177, kCtlH);
    defOutLabel_.setBounds(309,   rowAt(53), 100, kCtlH);
    defOutBox_.setBounds  (409,   rowAt(53), 177, kCtlH);

    topRule_.setBounds(kLeft, 69, kRight - kLeft, 3);

    configureLabel_.setBounds(kLeft, rowAt(89), 130, kCtlH);
    interfaceBox_.setBounds  (146,   rowAt(89), 147, kCtlH);
    audiowireLabel_.setBounds(313,   rowAt(89), 120, kCtlH);
    routingToggle_.setBounds (456,   rowAt(89), 130, kCtlH);

    layoutGrid();

    const int dy = routingEnabled_ ? 0 : -kGridHeight;
    usageLabel_.setBounds(kLeft, rowAt(279) + dy, kRight - kLeft, kCtlH);
    bottomRule_.setBounds(kLeft, 289 + dy, kRight - kLeft, 3);

    optionsButton_.setBounds(kLeft, rowAt(310) + dy, 179, kCtlH);
    namesButton_.setBounds  (212,   rowAt(310) + dy, 180, kCtlH);
    volumeToggle_.setBounds (410,   rowAt(310) + dy, 176, kCtlH);
}

void MainComponent::layoutGrid() {
    if (columns_ == 0) return;
    const int colW = kGridW / juce::jmax(2, columns_);
    auto colX = [&](int c) { return kLeft + c * colW; };

    for (auto& b : banks_) {
        const int x = colX(b.firstColumn);
        b.label->setBounds(x, 122 - kCtlH / 2, 52, kCtlH);
        b.personality->setBounds(x + 52, 122 - kCtlH / 2, colW - 57, kCtlH);
    }

    for (int c = 0; c < columns_ && (size_t)(c * 2 + 1) < headers_.size(); ++c) {
        headers_[(size_t)c * 2]->setBounds    (colX(c) + 47,  141, 50, 28);
        headers_[(size_t)c * 2 + 1]->setBounds(colX(c) + 98,  141, 50, 28);
    }

    // Rows fill each bank's columns top to bottom.
    for (const auto& b : banks_)
        for (size_t k = 0; k < b.rowCount; ++k) {
            auto& row = rows_[b.firstRow + k];
            const int x = colX(b.firstColumn + (int)k / kPairsPerColumn);
            const int y = kFirstRowY + (int)(k % kPairsPerColumn) * kRowPitch;
            row.label->setBounds(x + 13, y - 10, 45, 20);
            row.in->setBounds (x + 71 - 10,  y - 10, 20, 20);
            row.out->setBounds(x + 123 - 10, y - 10, 20, 20);
        }
}
