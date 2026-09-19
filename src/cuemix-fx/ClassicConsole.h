#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ClassicSkin.h"
#include "ConsoleModel.h"

#include <optional>

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
    void mouseUp(const juce::MouseEvent&) override { drag_ = {}; dragOffset_ = -1; }
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void resized() override { setScroll(scroll_); }

private:
    void paintStrip(juce::Graphics&, const StripInfo&, const ConsoleModel::MeterState&, int x);
    void paintPanel(juce::Graphics&, int x);
    void paintScrollBar(juce::Graphics&);
    void drawLitArc(juce::Graphics&, int cx, int cy, float fromDeg, float toDeg);

    int panelX() const { return getWidth() - kPanelWidth; }
    int stripsWidth() const { return panelX() - kStripsLeft; }
    int contentWidth() const { return (int)model_.strips().size() * kStripPitch; }
    void setScroll(int);
    juce::Rectangle<int> thumbBounds() const;

    // What a mouse press landed on.
    enum class Kind { None, Toggle, Knob, Fader, Popup };
    struct Hit {
        Kind kind = Kind::None;
        ConsoleModel::Param param = ConsoleModel::Param::Trim;
        int strip = -1;
        int value = 0;            // current value (Toggle/Knob/Fader); set value for radio toggles
        int min = 0, max = 0, centre = 0;
        juce::Rectangle<int> area;
    };
    Hit hitAt(juce::Point<int>) const;
    void showPopup(const Hit&);

    struct Drag { Hit hit; int startY = 0; int startValue = 0; int startCapTop = 0; };
    std::optional<Drag> drag_;

    void beginRename(int strip, juce::Rectangle<int> well);
    std::unique_ptr<juce::TextEditor> nameEditor_;
    int renaming_ = -1;

    ConsoleModel& model_;
    ClassicSkin& skin_;
    int scroll_ = 0;
    int dragOffset_ = -1;
    juce::String lcdTitle_, lcdDetail_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClassicConsole)
};
