// MOTU PCI Audio Setup — replacement for MOTU's 2011 Carbon original.
//
// The original is a fixed 602 x 334 window titled after the card — "PCI-424".
// See docs/ORIGINAL-UI.md for the teardown and docs/reference/ for the shots.

#include <juce_gui_basics/juce_gui_basics.h>

#include "MainComponent.h"

namespace {
// MOTU titles the window after the card, not after the app: the Mojave
// screenshots show "PCI-424". "MOTU PCI Audio Console" is the menu-bar
// application name. Replaced with the real device name once the card is open.
constexpr auto kWindowTitle = "PCI-424";
}  // namespace

class PCIAudioSetupApplication : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override    { return "MOTU PCI Audio Setup"; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise(const juce::String&) override {
        // This runs on the JUCE message thread, and nothing in this process has
        // touched CoreAudio yet (juce_audio_devices is deliberately not linked).
        // So this is the one moment where the HAL can be handed our run loop.
        // Everything downstream depends on it — see motu_card.h.
        std::string err;
        if (!motu::Card::installRunLoop(&err))
            juce::Logger::writeToLog("installRunLoop failed: " + juce::String(err));

        mainWindow = std::make_unique<MainWindow>(kWindowTitle);
    }

    void shutdown() override { mainWindow.reset(); }
    void systemRequestedQuit() override { quit(); }

private:
    class MainWindow : public juce::DocumentWindow {
    public:
        explicit MainWindow(juce::String name)
            : DocumentWindow(name,
                             juce::Desktop::getInstance().getDefaultLookAndFeel()
                                 .findColour(juce::ResizableWindow::backgroundColourId),
                             DocumentWindow::minimiseButton | DocumentWindow::closeButton) {
            setUsingNativeTitleBar(true);
            setContentOwned(new MainComponent(), true);
            // The original is not resizable.
            setResizable(false, false);
            centreWithSize(getWidth(), getHeight());
            setVisible(true);
        }

        void closeButtonPressed() override {
            JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
    };

    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION(PCIAudioSetupApplication)
