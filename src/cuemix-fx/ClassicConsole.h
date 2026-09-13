#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ClassicSkin.h"
#include "ConsoleModel.h"

// The Classic skin: MOTU's CueMix FX console for PCI cards, drawn from MOTU's
// own sprites at the positions measured off docs/reference/cuemix-console-full.png.
// Everything is painted (no child controls), as AwesomeLib did.
class ClassicConsole : public juce::Component {
public:
    ClassicConsole(ConsoleModel&, ClassicSkin&);
    ~ClassicConsole() override;

    static constexpr int kHeight = 473;       // the background art's height
    static constexpr int kStripPitch = 82;
    static constexpr int kStripsLeft = 11;
    static constexpr int kPanelWidth = 267;   // BackgroundRightPCI
    int idealWidth() const;

    void paint(juce::Graphics&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void resized() override { setScroll(scroll_); }

private:
    void paintStrip(juce::Graphics&, const StripInfo&, int x);
    void paintPanel(juce::Graphics&, int x);
    void paintScrollBar(juce::Graphics&);

    int panelX() const { return getWidth() - kPanelWidth; }
    int stripsWidth() const { return panelX() - kStripsLeft; }
    int contentWidth() const { return (int)model_.strips().size() * kStripPitch; }
    void setScroll(int);
    juce::Rectangle<int> thumbBounds() const;

    ConsoleModel& model_;
    ClassicSkin& skin_;
    int scroll_ = 0;
    int dragOffset_ = -1;
    juce::String lcdTitle_, lcdDetail_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClassicConsole)
};
