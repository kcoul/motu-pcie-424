#include "ChannelNamesWindow.h"

#include "MacLookAndFeel.h"
#include "motu_prefs.h"

namespace {

juce::Font uiFont(float size = MacLookAndFeel::kFontSize, int style = juce::Font::plain) {
    return juce::Font(juce::FontOptions(size, style));
}

struct Channel {
    int id = 0;
    juce::String hardware;   // "24I/O-2:Analog-A 17"
    juce::String custom;     // "" when unnamed
};

// One column pair: hardware name, and an editable custom name.
class NameTable : public juce::Component, private juce::TableListBoxModel {
public:
    NameTable(motu::Card card, bool isInput, std::function<void()> changed)
        : card_(card), isInput_(isInput), changed_(std::move(changed)) {
        table_.setModel(this);
        table_.setRowHeight(22);
        table_.setColour(juce::ListBox::backgroundColourId, juce::Colours::white);
        auto& h = table_.getHeader();
        h.addColumn("Channel", 1, 170, 80, 400, juce::TableHeaderComponent::notSortable);
        h.addColumn("Name", 2, 170, 80, 400, juce::TableHeaderComponent::notSortable);
        h.setStretchToFitActive(true);
        addAndMakeVisible(table_);
        reload();
    }

    void reload() {
        channels_.clear();
        motu::Exception e;
        const int n = isInput_ ? card_.numActiveInputs(e) : card_.numActiveOutputs(e);
        for (int i = 0; i < n; ++i) {
            Channel c;
            c.id = isInput_ ? card_.nthActiveInputID(e, i) : card_.nthActiveOutputID(e, i);
            if (e.raised()) break;
            const auto desc = isInput_ ? card_.inputDescription(e, c.id) : card_.outputDescription(e, c.id);
            c.hardware = juce::String(desc) + " " + juce::String(card_.bankRelativeID(e, c.id) + 1);
            c.custom = juce::String(card_.channelName(e, c.id, isInput_));
            channels_.push_back(c);
        }
        table_.updateContent();
        table_.repaint();
    }

    const std::vector<Channel>& channels() const { return channels_; }
    void resized() override { table_.setBounds(getLocalBounds()); }

private:
    int getNumRows() override { return (int)channels_.size(); }

    void paintRowBackground(juce::Graphics& g, int row, int, int, bool selected) override {
        g.fillAll(selected ? MacLookAndFeel::accent().withAlpha(0.18f)
                           : (row % 2 ? juce::Colour(0xfff4f5f7) : juce::Colours::white));
    }

    void paintCell(juce::Graphics& g, int row, int column, int w, int h, bool) override {
        if (column != 1 || !juce::isPositiveAndBelow(row, (int)channels_.size())) return;
        g.setColour(juce::Colour(0xff555555));
        g.setFont(uiFont(14.0f));
        g.drawText(channels_[(size_t)row].hardware, 6, 0, w - 8, h, juce::Justification::centredLeft, true);
    }

    // The Name column is a label that edits on click.
    class NameCell : public juce::Label {
    public:
        explicit NameCell(NameTable& t) : table_(t) {
            setEditable(true, true, false);
            setFont(uiFont(14.0f, juce::Font::bold));
            setColour(juce::Label::textColourId, juce::Colour(0xff1a5fb4));
            setColour(juce::Label::textWhenEditingColourId, juce::Colours::black);
            setColour(juce::Label::backgroundWhenEditingColourId, juce::Colours::white);
            onTextChange = [this] { table_.rename(row_, getText().trim()); };
        }
        void setRow(int row) {
            row_ = row;
            const auto& c = table_.channels_[(size_t)row];
            setText(c.custom, juce::dontSendNotification);
            setTooltip("Click to rename " + c.hardware + ". Empty restores the hardware name.");
        }
        void paint(juce::Graphics& g) override {
            juce::Label::paint(g);
            if (getText().isEmpty() && !isBeingEdited()) {
                g.setColour(juce::Colour(0xffb0b0b0));
                g.setFont(uiFont(13.0f, juce::Font::italic));
                g.drawText("click to name", getLocalBounds().reduced(4, 0), juce::Justification::centredLeft);
            }
        }
    private:
        NameTable& table_;
        int row_ = 0;
    };

    juce::Component* refreshComponentForCell(int row, int column, bool, juce::Component* existing) override {
        if (column != 2) { delete existing; return nullptr; }
        auto* cell = dynamic_cast<NameCell*>(existing);
        if (cell == nullptr) { delete existing; cell = new NameCell(*this); }
        if (juce::isPositiveAndBelow(row, (int)channels_.size())) cell->setRow(row);
        return cell;
    }

    void rename(int row, const juce::String& name) {
        if (!juce::isPositiveAndBelow(row, (int)channels_.size())) return;
        auto& c = channels_[(size_t)row];
        if (name == c.custom) return;
        motu::Exception e;
        card_.setChannelName(e, c.id, isInput_, name.toStdString());
        if (!e.raised()) card_.commitChanges(e, true);
        if (e.raised())
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "MOTU Channel Names",
                                                   "The driver has reported an error.\n" + juce::String(e.str()));
        c.custom = juce::String(card_.channelName(e, c.id, isInput_));
        table_.repaintRow(row);
        if (changed_) changed_();
    }

    motu::Card card_;
    bool isInput_;
    std::function<void()> changed_;
    std::vector<Channel> channels_;
    juce::TableListBox table_;
};

class Pane : public juce::Component {
public:
    Pane(motu::Card card, std::function<void()> changed)
        : card_(card), changed_(std::move(changed)),
          inputs_(card, true, changed_), outputs_(card, false, changed_) {
        for (auto* l : { &devicesLabel_, &inputsLabel_, &outputsLabel_ }) {
            l->setFont(uiFont(13.0f, juce::Font::bold));
            l->setColour(juce::Label::textColourId, juce::Colour(0xff444444));
            addAndMakeVisible(*l);
        }
        device_.setFont(uiFont(14.0f, juce::Font::bold));
        device_.setColour(juce::Label::backgroundColourId, MacLookAndFeel::accent());
        device_.setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(device_);
        addAndMakeVisible(inputs_);
        addAndMakeVisible(outputs_);
        addAndMakeVisible(importButton_);
        importButton_.onClick = [this] { chooseImportSource(); };
        hint_.setFont(uiFont(12.5f));
        hint_.setColour(juce::Label::textColourId, juce::Colour(0xff777777));
        addAndMakeVisible(hint_);
        setSize(820, 640);
    }

    void resized() override {
        auto r = getLocalBounds().reduced(12);
        auto bottom = r.removeFromBottom(26);
        importButton_.setBounds(bottom.removeFromLeft(160));
        bottom.removeFromLeft(12);
        hint_.setBounds(bottom);
        r.removeFromBottom(10);

        auto header = r.removeFromTop(22);
        auto left = r.removeFromLeft(110);
        devicesLabel_.setBounds(header.removeFromLeft(110));
        left.removeFromRight(10);
        device_.setBounds(left.removeFromTop(24));

        const int half = r.getWidth() / 2;
        inputsLabel_.setBounds(header.removeFromLeft(half));
        outputsLabel_.setBounds(header);
        inputs_.setBounds(r.removeFromLeft(half - 5));
        r.removeFromLeft(10);
        outputs_.setBounds(r);
    }

private:
    // Other volumes' prefs files. Listing /Volumes can make macOS ask for
    // access to removable volumes, so this only runs when asked.
    void chooseImportSource() {
        const auto home = juce::File::getSpecialLocation(juce::File::userHomeDirectory).getFullPathName();
        juce::PopupMenu m;
        m.addSectionHeader("Import channel names from");
        sources_.clear();
        for (const auto& path : motu::prefs::findPrefsFiles()) {
            const juce::File f { juce::String(path) };
            if (f.getFullPathName().startsWith(home)) continue;   // this OS's own file
            juce::String label = f.getFileName();
            if (f.getFullPathName().startsWith("/Volumes/"))
                label = f.getFullPathName().fromFirstOccurrenceOf("/Volumes/", false, false).upToFirstOccurrenceOf("/", false, false)
                        + "  (" + f.getFileName() + ", " + f.getLastModificationTime().formatted("%Y-%m-%d") + ")";
            sources_.add(f);
            m.addItem(sources_.size(), label);
        }
        if (sources_.isEmpty()) m.addItem(-1, "No other volumes' MOTU prefs found", false);
        m.addSeparator();
        m.addItem(1000, "Choose File...");
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&importButton_), [this](int result) {
            if (result == 1000) { chooseFile(); return; }
            if (juce::isPositiveAndNotGreaterThan(result, sources_.size()) && result > 0)
                importFrom(sources_[result - 1]);
        });
    }

    void chooseFile() {
        chooser_ = std::make_unique<juce::FileChooser>("Choose a MOTU PCI prefs file (PCI-424.bus*.slot0.plist)",
                                                       juce::File("/Volumes"), "*.plist");
        chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this](const juce::FileChooser& fc) {
            if (fc.getResult().existsAsFile()) importFrom(fc.getResult());
        });
    }

    void importFrom(const juce::File& file) {
        motu::prefs::ChannelNames names;
        std::string err;
        if (!motu::prefs::readChannelNames(file.getFullPathName().toStdString(), names, &err)) {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Import Names", juce::String(err));
            return;
        }
        // Names are indexed by channel id; apply them to channels active here.
        auto plan = std::make_shared<std::vector<std::tuple<bool, int, std::string>>>();
        for (const auto* table : { &inputs_, &outputs_ }) {
            const bool isInput = table == &inputs_;
            const auto& src = isInput ? names.inputs : names.outputs;
            for (const auto& c : table->channels())
                if (juce::isPositiveAndBelow(c.id, (int)src.size()) && !src[(size_t)c.id].empty()
                    && juce::String(src[(size_t)c.id]) != c.custom)
                    plan->emplace_back(isInput, c.id, src[(size_t)c.id]);
        }
        if (plan->empty()) {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Import Names",
                "Nothing to import: that file has no names for the channels active here, or they already match.");
            return;
        }
        int ins = 0;
        for (auto& p : *plan) ins += std::get<0>(p);
        const auto message = "Apply " + juce::String(ins) + " input and " + juce::String((int)plan->size() - ins)
                             + " output names from\n" + file.getFullPathName()
                             + "?\n\nChannels that already have a different custom name will be renamed.";
        juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                         .withIconType(juce::MessageBoxIconType::QuestionIcon)
                                         .withTitle("Import Names").withMessage(message)
                                         .withButton("Import").withButton("Cancel"),
                                     [this, plan](int button) {
            if (button != 1) return;
            motu::Exception e;
            for (auto& [isInput, id, name] : *plan) card_.setChannelName(e, id, isInput, name);
            card_.commitChanges(e, true);
            inputs_.reload();
            outputs_.reload();
            if (changed_) changed_();
        });
    }

    motu::Card card_;
    std::function<void()> changed_;
    juce::Label devicesLabel_ { {}, "DEVICES" }, inputsLabel_ { {}, "INPUTS" }, outputsLabel_ { {}, "OUTPUTS" };
    juce::Label device_ { {}, "  PCI-424" };
    NameTable inputs_, outputs_;
    juce::TextButton importButton_ { "Import Names..." };
    juce::Label hint_ { {}, "Click a name to edit. An empty name restores the hardware name." };
    juce::Array<juce::File> sources_;
    std::unique_ptr<juce::FileChooser> chooser_;
};

class Window : public juce::DocumentWindow {
public:
    Window(std::unique_ptr<Pane> pane, std::function<void()> onClose)
        : DocumentWindow("MOTU Channel Names", MacLookAndFeel::windowBackground(), DocumentWindow::closeButton),
          onClose_(std::move(onClose)) {
        setUsingNativeTitleBar(true);
        setContentOwned(pane.release(), true);
        setResizable(true, false);
        setResizeLimits(560, 360, 2000, 2000);
    }
    void closeButtonPressed() override { if (onClose_) juce::MessageManager::callAsync(onClose_); }

private:
    std::function<void()> onClose_;
};
}  // namespace

namespace ChannelNamesWindow {

std::unique_ptr<juce::DocumentWindow> create(juce::Component& owner, motu::Card card,
                                             std::function<void()> onNamesChanged, std::function<void()> onClose) {
    auto w = std::make_unique<Window>(std::make_unique<Pane>(card, std::move(onNamesChanged)), std::move(onClose));
    w->centreAroundComponent(&owner, w->getWidth(), w->getHeight());
    w->setVisible(true);
    return w;
}

}  // namespace ChannelNamesWindow
