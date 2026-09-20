#include "OptionsWindow.h"

#include "MacLookAndFeel.h"

#include <algorithm>

namespace {
using Option = motu::Interface::Option;

constexpr int kWidth = 432;
constexpr int kCtlH  = 21;

juce::Font uiFont() { return juce::Font(juce::FontOptions(MacLookAndFeel::kFontSize)); }

class Pane : public juce::Component {
public:
    Pane(motu::Card card, int wire) : card_(card), wire_(wire) {
        motu::Exception e;
        const auto current = itf();
        name_ = juce::String(card_.wireInterfaceName(e, wire_));
        if (current) version_ = juce::String(current.versionString(e));

        auto has = [&](Option o) { int v; return current && current.getOption(e, o, v); };
        const bool mirror = has(Option::AnalogMirror);
        if (has(Option::AESInputSteal))            buildHD192();
        else if (mirror || has(Option::InputLevels)) buildReferencePane(mirror);
        // An interface the driver reports no options for at all. MOTU says so
        // rather than showing an empty pane, and so do we.
        else                                       buildEmptyPane();

        footer_ = label(version_, 16, height_ - 12);
        setSize(kWidth, height_);
    }

    juce::String title() const { return name_ + " Options"; }

    void paint(juce::Graphics& g) override {
        g.fillAll(MacLookAndFeel::windowBackground());
        g.setColour(MacLookAndFeel::separator());
        for (int y : rules_) g.fillRect(16, y, kWidth - 32, 1);
    }

private:
    // Never cache the Interface: a commit can make the driver rebuild its
    // interface objects, and a held pointer then crashes on the next read.
    motu::Interface itf() const {
        motu::Exception e;
        return card_.wireInterface(e, wire_);
    }

    int read(Option o) const {
        motu::Exception e;
        int v = 0;
        const auto i = itf();
        return i && i.getOption(e, o, v) ? v : 0;
    }

    // MOTU's PaneChanged: SET the option, then CommitChanges(true).
    void write(Option o, int value) {
        motu::Exception e;
        if (const auto i = itf()) i.setOption(e, o, value);
        if (!e.raised()) card_.commitChanges(e, true);
        if (e.raised())
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, title(),
                                                   juce::String(e.message()));
        refresh();
    }

    juce::Label* label(const juce::String& text, int x, int centreY, int w = 200) {
        auto l = std::make_unique<juce::Label>();
        l->setText(text, juce::dontSendNotification);
        l->setFont(uiFont());
        l->setBorderSize({});
        l->setBounds(x, centreY - 10, w, 20);
        addAndMakeVisible(*l);
        owned_.push_back(std::move(l));
        return static_cast<juce::Label*>(owned_.back().get());
    }

    // A popup whose items stand for arbitrary option values (Mirror Analog
    // skips 6 and 7). With no `values`, item n is value n.
    juce::ComboBox* popup(Option o, const juce::StringArray& items, int x, int centreY, int w,
                          std::vector<int> values = {}) {
        if (values.empty())
            for (int i = 0; i < items.size(); ++i) values.push_back(i);
        auto c = std::make_unique<juce::ComboBox>();
        c->addItemList(items, 1);
        c->setBounds(x, centreY - kCtlH / 2, w, kCtlH);
        auto* raw = c.get();
        c->onChange = [this, o, raw, values] {
            const int i = raw->getSelectedItemIndex();
            if (juce::isPositiveAndBelow(i, (int)values.size())) write(o, values[(size_t)i]);
        };
        addAndMakeVisible(*c);
        refreshers_.push_back([this, o, raw, values] {
            const int v = read(o);
            const auto it = std::find(values.begin(), values.end(), v);
            if (it != values.end())
                raw->setSelectedItemIndex((int)(it - values.begin()), juce::dontSendNotification);
            else
                raw->setText(juce::String(v), juce::dontSendNotification);
        });
        owned_.push_back(std::move(c));
        return raw;
    }

    juce::ToggleButton* checkbox(const juce::String& text, int x, int centreY,
                                 std::function<bool()> get, std::function<void(bool)> set) {
        auto t = std::make_unique<juce::ToggleButton>(text);
        t->setBounds(x, centreY - 10, 180, 20);
        auto* raw = t.get();
        t->onClick = [raw, set] { set(raw->getToggleState()); };
        refreshers_.push_back([raw, get] { raw->setToggleState(get(), juce::dontSendNotification); });
        addAndMakeVisible(*t);
        owned_.push_back(std::move(t));
        return raw;
    }

    // Encodings from MOTU's HD192 pane constructor / PaneChanged / UpdateControls.
    void buildHD192() {
        label("AES/EBU Input Options:", 16, 23);
        label("Steal Inputs", 32, 46);
        aesControls_.push_back(popup(Option::AESInputSteal,
              { "None", "In 1-2", "In 3-4", "In 5-6", "In 7-8", "In 9-10", "In 11-12" }, 232, 46, 184));
        aesControls_.push_back(checkbox("Rate Convert", 234, 70,
                 [this] { return read(Option::AESInputSRC) != 0; },
                 [this](bool on) { write(Option::AESInputSRC, on ? 1 : 0); }));
        rules_.push_back(93);

        label("AES/EBU Output Options:", 16, 114);
        // Despite its key name, AESOutputSRCMode is what AES/EBU out carries.
        label("Mirror Analog", 32, 138);
        aesControls_.push_back(popup(Option::AESOutputSRCMode,
              { "Out 1-2", "Out 3-4", "Out 5-6", "Out 7-8", "Out 9-10", "Out 11-12",
                "In 1-2", "In 3-4", "In 5-6", "In 7-8", "In 9-10", "In 11-12" }, 232, 138, 184,
              { 0, 1, 2, 3, 4, 5, 8, 9, 10, 11, 12, 13 }));

        // Output Clock folds a "Fixed Frequency" checkbox into the same value:
        // 0 System, 1 AES Input, 2 AES Word In; a rate is 3-6 fixed, 7-10 not.
        label("Output Clock", 32, 162);
        auto* clock = std::make_unique<juce::ComboBox>().release();
        owned_.emplace_back(clock);
        clock->addItemList({ "System", "AES Input", "AES Word In", "44.1 kHz", "48 kHz", "88.2 kHz", "96 kHz" }, 1);
        clock->setBounds(232, 162 - kCtlH / 2, 184, kCtlH);
        addAndMakeVisible(*clock);
        aesControls_.push_back(clock);
        auto* fixed = checkbox("Fixed Frequency", 234, 186,
                               [this] { const int v = read(Option::AESOutputClock); return v >= 3 && v <= 6; },
                               [this](bool on) {
                                   const int v = read(Option::AESOutputClock);
                                   if (v >= 3) write(Option::AESOutputClock, (v - 3) % 4 + (on ? 3 : 7));
                               });
        clock->onChange = [this, clock, fixed] {
            const int i = clock->getSelectedItemIndex();
            if (i < 0) return;
            write(Option::AESOutputClock, i < 3 ? i : i + (fixed->getToggleState() ? 0 : 4));
        };
        aesControls_.push_back(fixed);
        refreshers_.push_back([this, clock, fixed] {
            const int v = read(Option::AESOutputClock);
            clock->setSelectedItemIndex(v < 3 ? v : 3 + (v - 3) % 4, juce::dontSendNotification);
            fixed->setVisible(v >= 3);
        });
        rules_.push_back(209);

        // The two time-outs are crossed relative to their key names: Clip reads
        // PeakHoldTime and Peak/Hold reads ClipHoldTime. This reproduces the
        // Mojave pane (PeakHoldTime=4 "1 Minute", ClipHoldTime=1 "2 Seconds").
        const juce::StringArray timeouts { "No Delay", "2 Seconds", "4 Seconds", "10 Seconds",
                                           "1 Minute", "5 Minutes", "8 Minutes", "Infinite" };
        label("Meter Options:", 16, 232);
        label("Clip Time-out", 32, 258);
        popup(Option::PeakHoldTime, timeouts, 232, 258, 184);
        label("Peak/Hold Time-out", 32, 282);
        popup(Option::ClipHoldTime, timeouts, 232, 282, 184);

        // AES/EBU cannot carry audio above 96 kHz, and MOTU greys the three AES
        // panes when the card is running faster:
        //
        //   AWGetNominalSampleRate();  cmpl $0x17700, %eax;  setbe %al
        //   EnablePane(0x4e24, al);  EnablePane(0x4e23, al);  EnablePane(0x4e21, al)
        //
        // 96000 is the only sample-rate constant in MOTU's whole binary. The
        // wording is theirs too, STR# 27545 item 15.
        aesWarning_ = label("", 186, 23, 240);
        aesWarning_->setJustificationType(juce::Justification::centredLeft);
        refreshers_.push_back([this] {
            const double rate = motu::device::sampleRate(motu::Card::findDevice());
            const bool available = rate > 0.0 && rate <= 96000.0;
            for (auto* c : aesControls_) c->setEnabled(available);
            aesWarning_->setVisible(!available);
            if (!available)
                aesWarning_->setText("(AES/EBU not available at " + juce::String((int)rate) + " Hz)",
                                     juce::dontSendNotification);
        });

        height_ = 326;
        refresh();
    }

    // Some interfaces have nothing to configure. MOTU shows a sentence instead
    // of an empty window; the string is its own, from the binary.
    void buildEmptyPane() {
        auto* text = label("No options available for this interface.", 16, 40, kWidth - 32);
        text->setJustificationType(juce::Justification::centred);
        height_ = 96;
    }

    // 24I/O and 2408mk3: Input Reference Level radios and Word Out Rate, plus
    // Bank to mirror and ADAT Mode on the 2408mk3.
    void buildReferencePane(bool mirror) {
        int y = 24;
        if (mirror) {
            auto text = label("Audio is always sent to the Analog outputs of the 2408, even "
                              "if Analog is not selected on any of the banks.", 16, 38, 400);
            text->setBounds(16, 16, 400, 38);
            text->setJustificationType(juce::Justification::topLeft);
            auto text2 = label("You may choose which bank will be mirrored on the Analog outs.  "
                               "Note that selecting Analog on a bank overrides this setting.", 16, 78, 400);
            text2->setBounds(16, 55, 350, 54);
            text2->setJustificationType(juce::Justification::topLeft);

            label("Bank to mirror on Analog:", 32, 126);
            popup(Option::AnalogMirror, { "Bank A", "Bank B", "Bank C" }, 216, 126, 100);
            y = 161;
        }

        label("Input Reference Level", 32, y);
        // One bit per radio row, set = -10 dBV, least significant bit = first row.
        const juce::StringArray rows = mirror
            ? juce::StringArray { "Analog 1-2", "Analog 3-4", "Analog 5-6", "Analog 7-8" }
            : juce::StringArray { "Analog 1-8", "Analog 9-16", "Analog 17-24" };
        for (int i = 0; i < rows.size(); ++i) {
            const int cy = y + 20 + i * 20;
            label(rows[i], 48, cy, 100);
            radioPair(1000 + i, 156, 257, cy, "+4 dBu", "-10 dBV",
                      [this, i] { return (read(Option::InputLevels) >> i) & 1; },
                      [this, i](int on) {
                          const int v = read(Option::InputLevels);
                          write(Option::InputLevels, on ? (v | (1 << i)) : (v & ~(1 << i)));
                      });
        }
        y += 20 + rows.size() * 20;

        if (mirror)
            radioPair(1100, 156, 257, y - 1, "Type I", "Type II",
                      // SMUX setting non-zero is Type I, zero is Type II.
                      [this] { motu::Exception e; return card_.smuxSetting(e) != 0 ? 0 : 1; },
                      [this](int typeII) {
                          motu::Exception e;
                          card_.setSmuxSetting(e, typeII ? 0 : 1);
                          if (!e.raised()) card_.commitChanges(e, true);
                          refresh();
                      });
        if (mirror) label("ADAT Mode", 48, y - 1, 100);

        // Measured: Word Out Rate sits 41 px above the bottom on both panes,
        // and the 24I/O keeps the gap where the 2408mk3 has its ADAT row.
        const int wordY = mirror ? 282 : 127;
        label("Word Out Rate:", 16, wordY, 120);
        popup(Option::WordOutRange,
              { "Match system clock", "44.1/48 (divide 88.2 or 96 by 2)",
                "88.2/96 (multiply 44.1 or 48 by 2)" }, 132, wordY, 284);

        height_ = wordY + 41;
        refresh();
    }

    void radioPair(int group, int xA, int xB, int centreY, const juce::String& a, const juce::String& b,
                   std::function<int()> get, std::function<void(int)> set) {
        auto ba = std::make_unique<juce::ToggleButton>(a);
        auto bb = std::make_unique<juce::ToggleButton>(b);
        for (auto* t : { ba.get(), bb.get() }) {
            t->setRadioGroupId(group, juce::dontSendNotification);
            t->setClickingTogglesState(true);
            addAndMakeVisible(*t);
        }
        ba->setBounds(xA - 7, centreY - 10, 95, 20);
        bb->setBounds(xB - 7, centreY - 10, 95, 20);
        auto* ra = ba.get(); auto* rb = bb.get();
        ra->onClick = [set, ra] { if (ra->getToggleState()) set(0); };
        rb->onClick = [set, rb] { if (rb->getToggleState()) set(1); };
        refreshers_.push_back([get, ra, rb] {
            const int v = get();
            (v ? rb : ra)->setToggleState(true, juce::dontSendNotification);
        });
        owned_.push_back(std::move(ba));
        owned_.push_back(std::move(bb));
    }

    void refresh() { for (auto& r : refreshers_) r(); }

    motu::Card card_;
    int wire_;
    juce::String name_, version_;
    int height_ = 168;
    std::vector<int> rules_;
    std::vector<std::unique_ptr<juce::Component>> owned_;
    std::vector<std::function<void()>> refreshers_;
    juce::Label* footer_ = nullptr;

    // AES/EBU is unavailable above 96 kHz; these are greyed and the warning
    // shown when that happens. See buildHD192.
    std::vector<juce::Component*> aesControls_;
    juce::Label* aesWarning_ = nullptr;
};

class Window : public juce::DocumentWindow {
public:
    Window(std::unique_ptr<Pane> pane, std::function<void()> onClose)
        : DocumentWindow(pane->title(), MacLookAndFeel::windowBackground(), DocumentWindow::closeButton),
          onClose_(std::move(onClose)) {
        setUsingNativeTitleBar(true);
        setContentOwned(pane.release(), true);
        setResizable(false, false);
    }
    void closeButtonPressed() override {
        if (onClose_) juce::MessageManager::callAsync(onClose_);
    }

private:
    std::function<void()> onClose_;
};
}  // namespace

namespace OptionsWindow {

std::unique_ptr<juce::DocumentWindow> create(juce::Component& owner, motu::Card card, int wire,
                                             std::function<void()> onClose) {
    auto pane = std::make_unique<Pane>(card, wire);
    auto w = std::make_unique<Window>(std::move(pane), std::move(onClose));
    w->centreAroundComponent(&owner, w->getWidth(), w->getHeight());
    w->setVisible(true);
    return w;
}

}  // namespace OptionsWindow
