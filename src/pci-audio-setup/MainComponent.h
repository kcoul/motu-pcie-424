#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "MacLookAndFeel.h"
#include "motu_card.h"

#include <functional>
#include <memory>
#include <vector>

// The console's single window: a fixed 602 x 334 pane laid out to match
// docs/reference/setup-main-*.png pixel for pixel. The channel grid below
// "Configure Interface" is rebuilt whenever a different interface is chosen.
class MainComponent : public juce::Component, private juce::AsyncUpdater {
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // File menu.
    void refresh();
    void probeAndRefresh();             // File > Refresh: re-probe the AudioWire ports first
    void showInterfaceOptions();
    void selectInterface(int index);    // position in the Configure Interface popup
    void saveConfiguration();
    void loadConfiguration();

    bool isConnected() const { return (bool)card_; }
    motu::Card& card() { return card_; }
    AudioDeviceID deviceId() const { return deviceId_; }

    // Called with a short description whenever something fails, so the host
    // window can surface it; MOTU's console used an alert for driver errors.
    std::function<void(const juce::String&)> onError;

private:
    // One Enable Input / Enable Output pair of checkboxes.
    struct PairRow {
        int firstId = 0;                // the pair's left channel id
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<juce::ToggleButton> in, out;
    };
    struct Bank {
        int index = 0;
        int firstColumn = 0;            // grid column its popup sits over
        size_t firstRow = 0, rowCount = 0;
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<juce::ComboBox> personality;
    };

    void handleAsyncUpdate() override { refresh(); }

    void connect();
    void refreshRates();
    void refreshClocks();
    void refreshDefaults();
    void refreshInterfaces();
    void rebuildGrid();
    void refreshGridState();
    void refreshUsage();
    void refreshVolumeControls();
    void layoutGrid();

    void report(const juce::String& what, const motu::Exception&);
    void commit(const juce::String& what);
    void afterChannelChange();

    // --- writes -------------------------------------------------------------
    void setPairInputEnabled(int firstId, bool);
    void setPairOutputEnabled(int firstId, bool);
    void setBankPersonality(int bank, int personality);
    void setDefaultPair(bool isInput, int pairIndex);
    void setVolumeControlsEnabled(bool);
    void setRoutingEnabled(bool);

    // A bank's channel ids, trimmed to the channels the interface really has
    // (MOTU's GetBankChanRange). In non-routing mode a bank is all on or all off.
    struct BankRange { int firstId = 0, count = 0; };
    BankRange bankRange(motu::Interface&, int wire, int bank);
    void setBankOn(motu::Interface&, int wire, int bank, bool on);
    bool isBankOn(motu::Interface&, int wire, int bank, bool* uniform = nullptr);
    void setBankDisabled(int bank);

    juce::String prefsKey(const juce::String& name) const;

    AudioDeviceID deviceId_ = 0;
    motu::Card card_;
    std::vector<int> wires_;            // connected wire indices, popup order
    int currentWire_ = -1;

    juce::Label    rateLabel_, clockLabel_, defInLabel_, defOutLabel_;
    juce::ComboBox rateBox_, clockBox_, defInBox_, defOutBox_;
    std::vector<double> rates_;
    std::vector<UInt32> clocks_;

    Separator      topRule_, bottomRule_;

    juce::Label        configureLabel_, audiowireLabel_;
    juce::ComboBox     interfaceBox_;
    juce::ToggleButton routingToggle_ { "Enable Routing" };

    std::vector<Bank>    banks_;
    std::vector<PairRow> rows_;
    std::vector<std::unique_ptr<juce::Label>> headers_;
    int columns_ = 0;

    juce::Label        usageLabel_;
    juce::TextButton   optionsButton_ { "Interface Options..." };
    juce::TextButton   namesButton_   { "Edit Channel Names..." };
    juce::ToggleButton volumeToggle_  { "Enable Volume Controls" };

    std::unique_ptr<juce::DocumentWindow> optionsWindow_;
    std::unique_ptr<juce::FileChooser> chooser_;

    // Enable Routing is the console's own preference, per device (MOTU kept it
    // in com.motu.pci.config.console, keyed by device UID), default on.
    std::unique_ptr<juce::PropertiesFile> prefs_;
    bool routingEnabled_ = true;

    motu::device::Listener* listener_ = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
