#include "MacLookAndFeel.h"

namespace {
const juce::Colour kText      { 0xff000000 };
const juce::Colour kDisabled  { 0xffa0a0a0 };
const juce::Colour kBorder    { 0xffb8b8b8 };
const juce::Colour kControl   { 0xffffffff };

juce::Font systemFont(float size = MacLookAndFeel::kFontSize) {
    return juce::Font(juce::FontOptions(size));
}

// The faint drop shadow under Aqua controls: a 1px darker line at the bottom.
void controlBody(juce::Graphics& g, juce::Rectangle<float> r, float radius) {
    g.setColour(juce::Colour(0x22000000));
    g.fillRoundedRectangle(r.translated(0.0f, 0.5f), radius);
    g.setColour(kControl);
    g.fillRoundedRectangle(r, radius);
    g.setColour(kBorder.withAlpha(0.7f));
    g.drawRoundedRectangle(r.reduced(0.25f), radius, 0.5f);
}
}  // namespace

MacLookAndFeel::MacLookAndFeel() {
    setColour(juce::ResizableWindow::backgroundColourId, windowBackground());
    setColour(juce::DocumentWindow::backgroundColourId, windowBackground());
    setColour(juce::Label::textColourId, kText);
    setColour(juce::ComboBox::textColourId, kText);
    setColour(juce::ToggleButton::textColourId, kText);
    setColour(juce::TextButton::textColourOffId, kText);
    setColour(juce::TextButton::textColourOnId, kText);
    setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xfff0f0f0));
    setColour(juce::PopupMenu::textColourId, kText);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, accent());
    setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour(juce::TextEditor::backgroundColourId, kControl);
    setColour(juce::TextEditor::textColourId, kText);
    setColour(juce::TextEditor::outlineColourId, kBorder);
    setColour(juce::TextEditor::focusedOutlineColourId, accent());
    setColour(juce::CaretComponent::caretColourId, kText);
    setColour(juce::AlertWindow::backgroundColourId, windowBackground());
    setColour(juce::AlertWindow::textColourId, kText);
}

juce::Font MacLookAndFeel::getLabelFont(juce::Label& l)          { return l.getFont(); }
juce::Font MacLookAndFeel::getComboBoxFont(juce::ComboBox&)      { return systemFont(); }
juce::Font MacLookAndFeel::getPopupMenuFont()                    { return systemFont(); }
juce::Font MacLookAndFeel::getTextButtonFont(juce::TextButton&, int) { return systemFont(); }

void MacLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool,
                                  int, int, int, int, juce::ComboBox& box) {
    auto r = juce::Rectangle<float>(0.0f, 0.0f, (float)width, (float)height).reduced(0.5f, 1.0f);
    const float radius = 4.0f;
    controlBody(g, r, radius);

    // Blue stepper cap on the right, rounded only on its outer corners.
    const float capW = 16.0f;
    auto cap = r.removeFromRight(capW);
    juce::Path p;
    p.addRoundedRectangle(cap.getX(), cap.getY(), cap.getWidth(), cap.getHeight(),
                          radius, radius, false, true, false, true);
    const auto top = box.isEnabled() ? accent().brighter(0.15f) : kDisabled.brighter(0.4f);
    const auto bot = box.isEnabled() ? accent().darker(0.1f)    : kDisabled;
    g.setGradientFill(juce::ColourGradient(top, 0.0f, cap.getY(), bot, 0.0f, cap.getBottom(), false));
    g.fillPath(p);

    // Up/down chevrons.
    const float cx = cap.getCentreX(), cy = cap.getCentreY();
    juce::Path chev;
    chev.startNewSubPath(cx - 3.0f, cy - 1.5f); chev.lineTo(cx, cy - 4.5f); chev.lineTo(cx + 3.0f, cy - 1.5f);
    chev.startNewSubPath(cx - 3.0f, cy + 1.5f); chev.lineTo(cx, cy + 4.5f); chev.lineTo(cx + 3.0f, cy + 1.5f);
    g.setColour(juce::Colours::white);
    g.strokePath(chev, juce::PathStrokeType(1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void MacLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label) {
    label.setBounds(4, 0, box.getWidth() - 22, box.getHeight());
    label.setFont(systemFont());
    label.setJustificationType(juce::Justification::centredLeft);
    label.setMinimumHorizontalScale(1.0f);   // truncate with an ellipsis, as Aqua does
    label.setColour(juce::Label::textColourId, box.isEnabled() ? kText : kDisabled);
}

void MacLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&,
                                          bool, bool isDown) {
    auto r = b.getLocalBounds().toFloat().reduced(0.5f, 1.0f);
    controlBody(g, r, 4.0f);
    if (isDown) {
        g.setColour(juce::Colour(0x18000000));
        g.fillRoundedRectangle(r, 4.0f);
    }
}

void MacLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool) {
    g.setFont(systemFont());
    g.setColour(b.isEnabled() ? kText : kDisabled);
    g.drawFittedText(b.getButtonText(), b.getLocalBounds(), juce::Justification::centred, 1);
}

void MacLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& b, bool, bool isDown) {
    const bool radio = b.getRadioGroupId() != 0;
    const float box = 14.0f;
    auto bounds = b.getLocalBounds().toFloat();
    const bool hasText = b.getButtonText().isNotEmpty();
    auto tick = hasText ? juce::Rectangle<float>(1.0f, (bounds.getHeight() - box) * 0.5f, box, box)
                        : juce::Rectangle<float>(box, box).withCentre(bounds.getCentre());
    const bool on = b.getToggleState();

    if (radio) {
        if (on) {
            g.setColour(isDown ? accent().darker(0.2f) : accent());
            g.fillEllipse(tick);
            g.setColour(juce::Colours::white);
            g.fillEllipse(tick.withSizeKeepingCentre(5.0f, 5.0f));
        } else {
            g.setColour(juce::Colour(0x22000000));
            g.fillEllipse(tick.translated(0.0f, 0.5f));
            g.setColour(kControl);
            g.fillEllipse(tick);
            g.setColour(kBorder);
            g.drawEllipse(tick.reduced(0.25f), 0.75f);
        }
    } else if (on) {
        g.setColour(isDown ? accent().darker(0.2f) : accent());
        g.fillRoundedRectangle(tick, 3.0f);
        juce::Path check;
        check.startNewSubPath(tick.getX() + 3.5f, tick.getCentreY() + 0.5f);
        check.lineTo(tick.getX() + 6.0f, tick.getBottom() - 3.5f);
        check.lineTo(tick.getRight() - 3.0f, tick.getY() + 3.5f);
        g.setColour(juce::Colours::white);
        g.strokePath(check, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    } else {
        controlBody(g, tick, 3.0f);
        if (isDown) {
            g.setColour(juce::Colour(0x18000000));
            g.fillRoundedRectangle(tick, 3.0f);
        }
    }

    if (hasText) {
        g.setFont(systemFont());
        g.setColour(b.isEnabled() ? kText : kDisabled);
        g.drawText(b.getButtonText(), bounds.withTrimmedLeft(box + 6.0f), juce::Justification::centredLeft, false);
    }
}

void MacLookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int width, int height) {
    g.fillAll(findColour(juce::PopupMenu::backgroundColourId));
    g.setColour(kBorder);
    g.drawRect(0, 0, width, height);
}

void MacLookAndFeel::drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area,
                                       bool isSeparator, bool isActive, bool isHighlighted,
                                       bool isTicked, bool, const juce::String& text,
                                       const juce::String& shortcutKeyText, const juce::Drawable*,
                                       const juce::Colour*) {
    if (isSeparator) {
        g.setColour(separator());
        g.fillRect(area.getX() + 1, area.getCentreY(), area.getWidth() - 2, 1);
        return;
    }
    auto r = area;
    if (isHighlighted && isActive) {
        g.setColour(accent());
        g.fillRect(r);
    }
    const auto colour = !isActive ? kDisabled : (isHighlighted ? juce::Colours::white : kText);
    g.setColour(colour);
    g.setFont(systemFont());

    auto tickArea = r.removeFromLeft(22);
    if (isTicked)
        g.drawText(juce::String::fromUTF8("\xe2\x9c\x93"), tickArea, juce::Justification::centred, false);

    if (shortcutKeyText.isNotEmpty())
        g.drawText(shortcutKeyText, r.removeFromRight(40), juce::Justification::centredRight, false);
    g.drawText(text, r.withTrimmedRight(8), juce::Justification::centredLeft, true);
}

void MacLookAndFeel::getIdealPopupMenuItemSize(const juce::String& text, bool isSeparator, int,
                                               int& idealWidth, int& idealHeight) {
    if (isSeparator) { idealWidth = 50; idealHeight = 11; return; }
    idealHeight = 19;
    idealWidth = juce::GlyphArrangement::getStringWidthInt(systemFont(), text) + 22 + 16;
}
