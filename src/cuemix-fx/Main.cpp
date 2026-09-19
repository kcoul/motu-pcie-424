// CueMix FX — replacement for MOTU's 2011 Carbon/AwesomeLib console.
//
// Two skins over one live model of the card (read-only for now; see
// docs/CUEMIX-PLAN.md):
//   Classic  MOTU's own artwork, placed as MOTU placed it
//   Modern   our flat controls, same layout

#include <juce_gui_basics/juce_gui_basics.h>

#include "ClassicConsole.h"
#include "ClassicSkin.h"
#include "Console.h"
#include "ConsoleLookAndFeel.h"
#include "ConsoleModel.h"

namespace {
enum CommandIds {
    kClassic = 0x3001, kModern, kLocate, kRevertLocal, kSendToCard,
    // File
    kSaveHardwarePreset = 0x3100, kLoadHardwarePreset, kMix1Return, kHardwareFollowsStereo, kClose,
    // Edit
    kUndo = 0x3200, kRedo, kCopy, kPaste, kClearPeaks,
    // Devices
    kDevice = 0x3300, kFFT, kOscilloscope, kXYPlot, kPhase, kTuner,
    // Configurations
    kCreateConfig = 0x3400, kSaveConfig, kSaveConfigTo, kDeleteConfig, kImportConfig, kExportConfig,
    // Talkback
    kConfigureTalkback = 0x3500, kToggleTalkback, kToggleListenback,
    // Control Surfaces
    kAppFollowsSurface = 0x3600, kShareSurfaces, kSurfacesEnabled, kSurfacesConfigure, kConfigureOSC,
    // Window
    kMinimise = 0x3700, kZoom, kBringAllToFront, kShowConsole,
};

// Peak Hold Time submenu items (plain menu items, not commands).
constexpr int kPeakHoldBase = 0x3900;
const char* const kPeakHoldTimes[] = { "Off", "2 Seconds", "4 Seconds", "10 Seconds", "1 Minute", "5 Minutes", "Infinite" };
// The same list in seconds, from MOTU's own
// LevelMeterView::ConvertPeakHoldTimeEnumToSeconds, where -1 means hold until
// Clear Peaks. MOTU's stored enum is 1-based over this list (its
// PeakHoldTime = 3 is "4 Seconds"); the index used here is 0-based, so it is
// not interchangeable with MOTU's pref value. See docs/CUEMIX-API.md.
const double kPeakHoldSeconds[] = { 0.0, 2.0, 4.0, 10.0, 60.0, 300.0, -1.0 };

using Mods = juce::ModifierKeys;
constexpr int kCmd = Mods::commandModifier, kShift = Mods::shiftModifier, kAlt = Mods::altModifier;

// MOTU's menus exactly as captured on Mojave (docs/ORIGINAL-UI.md):
// name, shortcut, whether MOTU enabled it on a PCI card, and whether ours works yet.
struct MenuCommand {
    int id;
    const char* name;
    juce::juce_wchar key;
    int mods;
    bool enabledOnPCI;
    const char* stage;   // nullptr = implemented; otherwise which plan stage delivers it
};

const MenuCommand kCommands[] = {
    { kSaveHardwarePreset, "Save Hardware Preset...", 's', kCmd | kAlt, false, nullptr },
    { kLoadHardwarePreset, "Load Hardware Preset...", 'o', kCmd | kAlt, false, nullptr },
    { kMix1Return, "Mix 1 Return Includes Computer Output", 0, 0, false, nullptr },
    { kHardwareFollowsStereo, "Hardware Follows Console Stereo Settings", 0, 0, true, nullptr },
    { kClose, "Close", 'w', kCmd, true, nullptr },
    { kUndo, "Undo", 'z', kCmd, false, nullptr },
    { kRedo, "Redo", 'z', kCmd | kShift, false, nullptr },
    { kCopy, "Copy", 'c', kCmd, true, "stage 5 (copy/paste a mix)" },
    { kPaste, "Paste", 'v', kCmd, false, nullptr },
    { kClearPeaks, "Clear Peaks", '\\', kCmd, true, nullptr },
    { kDevice, "PCI-424", '1', kCmd, true, nullptr },
    { kFFT, "FFT Analysis", 0, 0, true, "stage 6 (analysis windows)" },
    { kOscilloscope, "Oscilloscope", 0, 0, true, "stage 6 (analysis windows)" },
    { kXYPlot, "X-Y Plot", 0, 0, true, "stage 6 (analysis windows)" },
    { kPhase, "Phase Analysis", 0, 0, true, "stage 6 (analysis windows)" },
    { kTuner, "Tuner", 0, 0, true, "stage 6 (analysis windows)" },
    { kCreateConfig, "Create New...", 'n', kCmd, true, "stage 5 (configurations)" },
    { kSaveConfig, "Save", 's', kCmd, false, nullptr },
    { kSaveConfigTo, "Save To...", 's', kCmd | kShift, false, nullptr },
    { kDeleteConfig, "Delete...", 0, 0, false, nullptr },
    { kImportConfig, "Import...", 0, 0, true, "stage 5 (configurations)" },
    { kExportConfig, "Export...", 0, 0, false, nullptr },
    { kConfigureTalkback, "Configure Talkback/Listenback...", 't', kCmd | kShift, true, "stage 4 (talkback)" },
    { kToggleTalkback, "Toggle Talkback", 't', kCmd, true, "stage 4 (talkback)" },
    { kToggleListenback, "Toggle Listenback", 'l', kCmd, true, "stage 4 (talkback)" },
    { kAppFollowsSurface, "Application Follows Control Surface", 0, 0, true, "stage 7 (control surfaces)" },
    { kShareSurfaces, "Share Surfaces with Other Applications", 0, 0, true, "stage 7 (control surfaces)" },
    { kSurfacesEnabled, "Enabled", 0, 0, true, "stage 7 (control surfaces)" },
    { kSurfacesConfigure, "Configure...", 0, 0, true, "stage 7 (control surfaces)" },
    { kConfigureOSC, "Configure OSC Devices...", 0, 0, true, "stage 7 (control surfaces)" },
    { kMinimise, "Minimize", 'm', kCmd, true, nullptr },
    { kZoom, "Zoom", 0, 0, false, nullptr },
    { kBringAllToFront, "Bring All to Front", 0, 0, true, nullptr },
    { kShowConsole, "PCI-424", 0, 0, true, nullptr },
};

const MenuCommand* findCommand(int id) {
    for (const auto& c : kCommands)
        if (c.id == id) return &c;
    return nullptr;
}
}

class CueMixApplication : public juce::JUCEApplication, private juce::MenuBarModel {
public:
    const juce::String getApplicationName() override    { return "CueMix FX"; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise(const juce::String& commandLine) override {
        // Before anything else touches CoreAudio: the HAL plugin only hands the
        // card pointer to the run loop it was given (motu_card.h).
        std::string err;
        if (!motu::Card::installRunLoop(&err))
            juce::Logger::writeToLog("installRunLoop failed: " + juce::String(err));

        juce::PropertiesFile::Options opts;
        opts.applicationName = "CueMix FX";
        opts.filenameSuffix = ".settings";
        opts.osxLibrarySubFolder = "Application Support";
        prefs_ = std::make_unique<juce::PropertiesFile>(opts);

        juce::LookAndFeel::setDefaultLookAndFeel(&lnf_);
        model_ = std::make_unique<ConsoleModel>();
        loadSkin(juce::File(prefs_->getValue("ClassicSkinPath")));

        auto args = juce::StringArray::fromTokens(commandLine, true);
        bool classic = prefs_->getValue("Skin", "Classic") == "Classic";
        if (args.contains("--modern")) classic = false;
        if (args.contains("--classic")) classic = true;

        window_ = std::make_unique<Window>();
        showSkin(classic && skin_ && skin_->ok());

        commands_.registerAllCommandsForTarget(this);
        commands_.setFirstCommandTarget(this);
        window_->addKeyListener(commands_.getKeyMappings());
        setApplicationCommandManagerToWatch(&commands_);
        // MOTU's menu bar has no View menu, so the skin choice lives in the
        // application menu, keeping the rest of the bar identical to theirs.
        juce::PopupMenu appExtras;
        appExtras.addCommandItem(&commands_, kClassic);
        appExtras.addCommandItem(&commands_, kModern);
        appExtras.addCommandItem(&commands_, kLocate);
        appExtras.addSeparator();
        appExtras.addCommandItem(&commands_, kSendToCard);
        appExtras.addCommandItem(&commands_, kRevertLocal);
        juce::MenuBarModel::setMacMainMenu(this, &appExtras);
    }

    void shutdown() override {
        juce::MenuBarModel::setMacMainMenu(nullptr);
        window_.reset();
        model_.reset();
        juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
    }

    void systemRequestedQuit() override { quit(); }

private:
    class Window : public juce::DocumentWindow {
    public:
        Window() : DocumentWindow("PCI-424", ConsoleLookAndFeel::panel(),
                                  DocumentWindow::minimiseButton | DocumentWindow::closeButton) {
            setUsingNativeTitleBar(true);
        }
        void closeButtonPressed() override { JUCEApplication::getInstance()->systemRequestedQuit(); }

        // Resizable in width only; the height is the skin's.
        void setConsole(juce::Component* console, int idealWidth, int height) {
            const int screenW = (int)juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userBounds.getWidth();
            console->setSize(juce::jmin(idealWidth, (int)screenW - 2), height);
            setContentOwned(console, true);
            setResizable(true, false);
            if (!isVisible()) { centreWithSize(getWidth(), getHeight()); setVisible(true); }
            // The resize limits apply to the whole window, title bar included,
            // but setSize() sizes the content. Mixing the two up makes macOS squash
            // the content vertically to fit.
            const int frame = getPeer() != nullptr ? getPeer()->getFrameSize().getTop() : 0;
            setResizeLimits(600, height + frame, 10000, height + frame);
            setContentComponentSize(getContentComponent()->getWidth(), height);
        }
    };

    void loadSkin(const juce::File& preferred) {
        const auto dir = ClassicSkin::findResources(preferred);
        skin_ = dir != juce::File() ? std::make_unique<ClassicSkin>(dir) : nullptr;
    }

    void showSkin(bool classic) {
        classic_ = classic;
        prefs_->setValue("Skin", classic ? "Classic" : "Modern");
        if (classic) {
            auto* c = new ClassicConsole(*model_, *skin_);
            window_->setConsole(c, c->idealWidth(), ClassicConsole::kHeight);
        } else {
            auto* c = new Console(*model_);
            window_->setConsole(c, c->idealWidth(), Console::idealHeight());
        }
        commands_.commandStatusChanged();
    }

    void locateOriginal() {
        chooser_ = std::make_unique<juce::FileChooser>("Locate MOTU's CueMix FX.app", juce::File("/Applications"), "*.app");
        chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [this](const juce::FileChooser& fc) {
            auto dir = fc.getResult();
            if (dir.getFileExtension() == ".app") dir = dir.getChildFile("Contents/Resources");
            if (!ClassicSkin::isUsable(dir)) {
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "CueMix FX",
                    "That doesn't contain MOTU's CueMix FX artwork (BackgroundRightPCI.png).");
                return;
            }
            prefs_->setValue("ClassicSkinPath", dir.getFullPathName());
            loadSkin(dir);
            showSkin(true);
        });
    }

    // --- menus ---------------------------------------------------------------

    juce::StringArray getMenuBarNames() override {
        return { "File", "Edit", "Devices", "Configurations", "Talkback", "Phones", "Control Surfaces", "Window" };
    }

    juce::PopupMenu getMenuForIndex(int, const juce::String& name) override {
        juce::PopupMenu m;
        auto add = [&](std::initializer_list<int> ids) { for (int id : ids) m.addCommandItem(&commands_, id); };
        if (name == "File") {
            add({ kSaveHardwarePreset, kLoadHardwarePreset });
            m.addSeparator();
            juce::PopupMenu peak;
            const int current = prefs_->getIntValue("PeakHoldTime", 1);
            for (int i = 0; i < (int)std::size(kPeakHoldTimes); ++i)
                peak.addItem(kPeakHoldBase + i, kPeakHoldTimes[i], true, i == current);
            m.addSubMenu("Peak Hold Time", peak);
            m.addSeparator();
            add({ kMix1Return, kHardwareFollowsStereo });
            m.addSeparator();
            add({ kClose });
        } else if (name == "Edit") {
            add({ kUndo, kRedo });
            m.addSeparator();
            add({ kCopy, kPaste });
            m.addSeparator();
            add({ kClearPeaks });
        } else if (name == "Devices") {
            add({ kDevice, kFFT, kOscilloscope, kXYPlot, kPhase, kTuner });
        } else if (name == "Configurations") {
            add({ kCreateConfig, kSaveConfig, kSaveConfigTo, kDeleteConfig });
            m.addSeparator();
            add({ kImportConfig, kExportConfig });
        } else if (name == "Talkback") {
            add({ kConfigureTalkback, kToggleTalkback, kToggleListenback });
        } else if (name == "Phones") {
            // Empty on PCI cards, exactly as MOTU's is: they have no phones bus.
        } else if (name == "Control Surfaces") {
            add({ kAppFollowsSurface, kShareSurfaces });
            juce::PopupMenu surfaces;
            surfaces.addCommandItem(&commands_, kSurfacesEnabled);
            surfaces.addCommandItem(&commands_, kSurfacesConfigure);
            m.addSubMenu("CueMix Control Surfaces", surfaces);
            m.addSeparator();
            add({ kConfigureOSC });
        } else if (name == "Window") {
            add({ kMinimise, kZoom });
            m.addSeparator();
            add({ kBringAllToFront });
            m.addSeparator();
            add({ kShowConsole });
        }
        return m;
    }

    void menuItemSelected(int id, int) override {
        if (id >= kPeakHoldBase && id < kPeakHoldBase + (int)std::size(kPeakHoldTimes)) {
            const int sel = id - kPeakHoldBase;
            prefs_->setValue("PeakHoldTime", sel);
            model_->setPeakHoldSeconds(kPeakHoldSeconds[sel]);
            model_->showNotice("Peak Hold Time", kPeakHoldTimes[sel]);
        }
    }

    void getAllCommands(juce::Array<juce::CommandID>& ids) override {
        JUCEApplication::getAllCommands(ids);
        ids.addArray({ kClassic, kModern, kLocate, kRevertLocal, kSendToCard });
        for (const auto& c : kCommands) ids.add(c.id);
    }

    void getCommandInfo(juce::CommandID id, juce::ApplicationCommandInfo& info) override {
        switch (id) {
            case kClassic: info.setInfo("Classic Skin", {}, "View", 0);
                           info.setActive(skin_ && skin_->ok()); info.setTicked(classic_); return;
            case kModern:  info.setInfo("Modern Skin", {}, "View", 0); info.setTicked(!classic_); return;
            case kLocate:  info.setInfo("Locate MOTU CueMix FX.app...", {}, "View", 0); return;
            // Controls move without writing to the card yet; this drops those values.
            case kRevertLocal: info.setInfo("Revert to Card Values", {}, "View", 0); return;
            case kSendToCard:
                // Unverified write path (docs/CUEMIX-API.md): opt in per session.
                info.setInfo("Send Changes to Card", "Write moved controls to the card", "View", 0);
                info.setTicked(model_ != nullptr && model_->writesEnabled());
                return;
            default: break;
        }
        if (const auto* c = findCommand(id)) {
            info.setInfo(c->name, {}, "CueMix", 0);
            if (c->key != 0) info.addDefaultKeypress(c->key, juce::ModifierKeys(c->mods));
            info.setActive(c->enabledOnPCI);
            if (id == kHardwareFollowsStereo) info.setTicked(prefs_->getBoolValue("HardwareFollowsStereo", true));
            if (id == kDevice || id == kShowConsole) info.setTicked(model_ && model_->connected());
            return;
        }
        JUCEApplication::getCommandInfo(id, info);
    }

    bool perform(const InvocationInfo& info) override {
        switch (info.commandID) {
            case kClassic: if (skin_ && skin_->ok()) showSkin(true); return true;
            case kModern:  showSkin(false); return true;
            case kLocate:  locateOriginal(); return true;
            case kRevertLocal: model_->clearLocalChanges(); return true;
            case kSendToCard:
                model_->setWritesEnabled(!model_->writesEnabled());
                commands_.commandStatusChanged();
                return true;
            case kClearPeaks: model_->clearPeaks(); return true;
            case kClose:   systemRequestedQuit(); return true;
            case kMinimise: window_->setMinimised(true); return true;
            case kDevice:
            case kShowConsole:
            case kBringAllToFront: window_->toFront(true); return true;
            case kHardwareFollowsStereo:
                prefs_->setValue("HardwareFollowsStereo", !prefs_->getBoolValue("HardwareFollowsStereo", true));
                commands_.commandStatusChanged();
                return true;
            default: break;
        }
        if (const auto* c = findCommand(info.commandID)) {
            if (c->stage != nullptr) model_->showNotice(juce::String(c->name).upToFirstOccurrenceOf("...", false, false),
                                                        "Not yet: " + juce::String(c->stage));
            return true;
        }
        return JUCEApplication::perform(info);
    }

    juce::ApplicationCommandManager commands_;
    ConsoleLookAndFeel lnf_;
    std::unique_ptr<juce::PropertiesFile> prefs_;
    std::unique_ptr<ConsoleModel> model_;
    std::unique_ptr<ClassicSkin> skin_;
    std::unique_ptr<Window> window_;
    std::unique_ptr<juce::FileChooser> chooser_;
    bool classic_ = true;
};

START_JUCE_APPLICATION(CueMixApplication)
