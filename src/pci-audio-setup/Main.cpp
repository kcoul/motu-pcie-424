// MOTU PCI Audio Setup — replacement for MOTU's 2011 Carbon original.
//
// The original is a fixed 602 x 334 window titled after the card — "PCI-424".
// See docs/ORIGINAL-UI.md for the teardown and docs/reference/ for the shots.

#include <juce_gui_basics/juce_gui_basics.h>

#include "MacLookAndFeel.h"
#include "MainComponent.h"

namespace {
// MOTU titles the window after the card, not after the app: the Mojave
// screenshots show "PCI-424". "MOTU PCI Audio Console" is the menu-bar
// application name.
constexpr auto kWindowTitle = "PCI-424";

// setup-menu-file.png: Save Configuration ⌘S, Load Configuration ⌘O, the
// device list (PCI-424 ⌘1), Refresh ⌘R, Close ⌘W.
enum CommandIds { kSave = 0x2001, kLoad, kDevice, kRefresh, kClose };
}  // namespace

class PCIAudioSetupApplication : public juce::JUCEApplication, private juce::MenuBarModel {
public:
    const juce::String getApplicationName() override    { return "MOTU PCI Audio Setup"; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise(const juce::String& commandLine) override {
        // This runs on the JUCE message thread, and nothing in this process has
        // touched CoreAudio yet (juce_audio_devices is deliberately not linked).
        // So this is the one moment where the HAL can be handed our run loop.
        // Everything downstream depends on it — see motu_card.h.
        std::string err;
        if (!motu::Card::installRunLoop(&err))
            juce::Logger::writeToLog("installRunLoop failed: " + juce::String(err));

        juce::LookAndFeel::setDefaultLookAndFeel(&lnf_);
        mainWindow_ = std::make_unique<MainWindow>(kWindowTitle);

        commands_.registerAllCommandsForTarget(this);
        commands_.setFirstCommandTarget(this);
        mainWindow_->addKeyListener(commands_.getKeyMappings());
        setApplicationCommandManagerToWatch(&commands_);
        juce::MenuBarModel::setMacMainMenu(this);

        // For side-by-side checks against docs/reference/:
        //   open "MOTU PCI Audio Setup.app" --args --interface 2 --options
        auto args = juce::StringArray::fromTokens(commandLine, true);
        if (auto i = args.indexOf("--interface"); i >= 0)
            mainWindow_->console()->selectInterface(args[i + 1].getIntValue());
        if (args.contains("--options"))
            mainWindow_->console()->showInterfaceOptions();
    }

    void shutdown() override {
        juce::MenuBarModel::setMacMainMenu(nullptr);
        mainWindow_.reset();
        juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
    }

    void systemRequestedQuit() override { quit(); }

private:
    class MainWindow : public juce::DocumentWindow {
    public:
        explicit MainWindow(juce::String name)
            : DocumentWindow(name, MacLookAndFeel::windowBackground(),
                             DocumentWindow::minimiseButton | DocumentWindow::closeButton) {
            setUsingNativeTitleBar(true);
            auto* content = new MainComponent();
            content->onError = [](const juce::String& message) {
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                                       "MOTU PCI Audio Setup", message);
            };
            setContentOwned(content, true);
            // The original is not resizable.
            setResizable(false, false);
            centreWithSize(getWidth(), getHeight());
            setVisible(true);
        }

        MainComponent* console() { return dynamic_cast<MainComponent*>(getContentComponent()); }

        void closeButtonPressed() override {
            JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
    };

    // --- MenuBarModel --------------------------------------------------------

    juce::StringArray getMenuBarNames() override { return { "File" }; }

    juce::PopupMenu getMenuForIndex(int, const juce::String&) override {
        juce::PopupMenu m;
        m.addCommandItem(&commands_, kSave);
        m.addCommandItem(&commands_, kLoad);
        m.addSeparator();
        m.addCommandItem(&commands_, kDevice);
        m.addSeparator();
        m.addCommandItem(&commands_, kRefresh);
        m.addSeparator();
        m.addCommandItem(&commands_, kClose);
        return m;
    }

    void menuItemSelected(int, int) override {}

    // --- ApplicationCommandTarget ---------------------------------------------

    void getAllCommands(juce::Array<juce::CommandID>& ids) override {
        JUCEApplication::getAllCommands(ids);
        ids.addArray({ kSave, kLoad, kDevice, kRefresh, kClose });
    }

    void getCommandInfo(juce::CommandID id, juce::ApplicationCommandInfo& info) override {
        const auto cmd = juce::ModifierKeys::commandModifier;
        const bool connected = mainWindow_ && mainWindow_->console()->isConnected();
        switch (id) {
            case kSave:    info.setInfo("Save Configuration", {}, "File", 0);
                           info.addDefaultKeypress('s', cmd); info.setActive(connected); break;
            case kLoad:    info.setInfo("Load Configuration", {}, "File", 0);
                           info.addDefaultKeypress('o', cmd); info.setActive(connected); break;
            case kDevice:  info.setInfo(kWindowTitle, {}, "File", 0);
                           info.addDefaultKeypress('1', cmd);
                           info.setActive(connected); info.setTicked(connected); break;
            case kRefresh: info.setInfo("Refresh", {}, "File", 0);
                           info.addDefaultKeypress('r', cmd); break;
            case kClose:   info.setInfo("Close", {}, "File", 0);
                           info.addDefaultKeypress('w', cmd); break;
            default:       JUCEApplication::getCommandInfo(id, info); break;
        }
    }

    bool perform(const InvocationInfo& info) override {
        if (!mainWindow_) return false;
        switch (info.commandID) {
            case kSave:    mainWindow_->console()->saveConfiguration(); return true;
            case kLoad:    mainWindow_->console()->loadConfiguration(); return true;
            case kDevice:  mainWindow_->toFront(true); return true;
            case kRefresh: mainWindow_->console()->probeAndRefresh(); return true;
            case kClose:   systemRequestedQuit(); return true;
            default:       return JUCEApplication::perform(info);
        }
    }

    juce::ApplicationCommandManager commands_;
    MacLookAndFeel lnf_;
    std::unique_ptr<MainWindow> mainWindow_;
};

START_JUCE_APPLICATION(PCIAudioSetupApplication)
