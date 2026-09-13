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
enum CommandIds { kClassic = 0x3001, kModern, kLocate };
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
        juce::MenuBarModel::setMacMainMenu(this);
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
            const int screenW = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userBounds.getWidth();
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

    juce::StringArray getMenuBarNames() override { return { "View" }; }

    juce::PopupMenu getMenuForIndex(int, const juce::String&) override {
        juce::PopupMenu m;
        m.addCommandItem(&commands_, kClassic);
        m.addCommandItem(&commands_, kModern);
        m.addSeparator();
        m.addCommandItem(&commands_, kLocate);
        return m;
    }
    void menuItemSelected(int, int) override {}

    void getAllCommands(juce::Array<juce::CommandID>& ids) override {
        JUCEApplication::getAllCommands(ids);
        ids.addArray({ kClassic, kModern, kLocate });
    }

    void getCommandInfo(juce::CommandID id, juce::ApplicationCommandInfo& info) override {
        switch (id) {
            case kClassic: info.setInfo("Classic Skin", {}, "View", 0);
                           info.setActive(skin_ && skin_->ok()); info.setTicked(classic_); break;
            case kModern:  info.setInfo("Modern Skin", {}, "View", 0); info.setTicked(!classic_); break;
            case kLocate:  info.setInfo("Locate MOTU CueMix FX.app...", {}, "View", 0); break;
            default:       JUCEApplication::getCommandInfo(id, info); break;
        }
    }

    bool perform(const InvocationInfo& info) override {
        switch (info.commandID) {
            case kClassic: if (skin_ && skin_->ok()) showSkin(true); return true;
            case kModern:  showSkin(false); return true;
            case kLocate:  locateOriginal(); return true;
            default:       return JUCEApplication::perform(info);
        }
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
