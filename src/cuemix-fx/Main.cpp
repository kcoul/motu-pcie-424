// CueMix FX — replacement for MOTU's 2011 Carbon/AwesomeLib console.
//
// First pass: the console layout, following the card's active inputs and mix
// buses live, read-only. See docs/CUEMIX-PLAN.md for the stages.

#include <juce_gui_basics/juce_gui_basics.h>

#include "Console.h"
#include "ConsoleLookAndFeel.h"

class CueMixApplication : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override    { return "CueMix FX"; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise(const juce::String&) override {
        // Before anything else touches CoreAudio: the HAL plugin only hands the
        // card pointer to the run loop it was given (motu_card.h).
        std::string err;
        if (!motu::Card::installRunLoop(&err))
            juce::Logger::writeToLog("installRunLoop failed: " + juce::String(err));

        juce::LookAndFeel::setDefaultLookAndFeel(&lnf_);
        window_ = std::make_unique<Window>();
    }

    void shutdown() override {
        window_.reset();
        juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
    }

    void systemRequestedQuit() override { quit(); }

private:
    class Window : public juce::DocumentWindow {
    public:
        Window() : DocumentWindow("PCI-424", ConsoleLookAndFeel::panel(),
                                  DocumentWindow::minimiseButton | DocumentWindow::closeButton) {
            setUsingNativeTitleBar(true);
            auto* console = new Console();
            const int screenW = (int)juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userBounds.getWidth();
            const int height = Console::idealHeight();
            console->setSize(juce::jmin(console->idealWidth(), screenW - 40), height);
            setContentOwned(console, true);
            // Wider shows more strips; the height is the strip's.
            setResizable(true, false);
            centreWithSize(getWidth(), getHeight());
            setVisible(true);
            // The limits apply to the whole window, and the native title bar's
            // height is only known once the window has a peer.
            const int frame = getPeer() != nullptr ? getPeer()->getFrameSize().getTop() : 0;
            setResizeLimits(Strip::kWidth * 4 + 250, height + frame, 10000, height + frame);
            setSize(getWidth(), height + frame);
        }
        void closeButtonPressed() override { JUCEApplication::getInstance()->systemRequestedQuit(); }
    };

    ConsoleLookAndFeel lnf_;
    std::unique_ptr<Window> window_;
};

START_JUCE_APPLICATION(CueMixApplication)
