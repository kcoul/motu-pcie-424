#include "ClassicConsole.h"

#include <cmath>

namespace {
const juce::Colour kLabel { 0xff3c3c3c };
const juce::Colour kWellText { 0xffffffff };
const juce::Colour kLcd { 0xff37b7ff };

constexpr int kScrollY = 130;

// Where the fader cap's top sits for a level: the scale printed in the strip
// art, measured from the screenshot (tick y for each dB mark, cap top = 216 at 0 dB).
constexpr struct { double db; int y; } kFaderScale[] = {
    { 0, 234 }, { -3, 262 }, { -6, 284 }, { -12, 319 }, { -18, 345 }, { -24, 363 },
    { -30, 374 }, { -36, 383 }, { -48, 393 }, { -60, 399 }, { -1000, 406 },
};

int faderCapTop(int raw) {
    const double db = volumeDb(raw);
    int y = kFaderScale[std::size(kFaderScale) - 1].y;
    for (size_t i = 0; i + 1 < std::size(kFaderScale); ++i) {
        const auto& a = kFaderScale[i];
        const auto& b = kFaderScale[i + 1];
        if (db >= a.db) { y = a.y; break; }
        if (db >= b.db) { y = (int)std::lround(a.y + (a.db - db) / (a.db - b.db) * (b.y - a.y)); break; }
    }
    return 216 + (y - 234);
}

// KnobRotation: 61 usable frames, 4 = fully left, 32 = centre, 60 = fully right.
int panFrame(int raw)  { return 4 + juce::jlimit(0, 56, (int)std::lround(raw * 56.0 / 128.0)); }
// Tentative: 64 shows "0 dB" with the knob at its stop on Mojave.
int trimFrame(int raw) { return 4 + juce::jlimit(0, 56, raw - 64); }

void text(juce::Graphics& g, const juce::String& s, float size, juce::Colour c, int x, int centreY, int w,
          juce::Justification j) {
    g.setFont(ClassicSkin::font(size));
    g.setColour(c);
    g.drawText(s, x, centreY - (int)size, w, (int)size * 2, j, true);
}

void centred(juce::Graphics& g, const juce::String& s, float size, juce::Colour c, int cx, int cy, int w = 80) {
    text(g, s, size, c, cx - w / 2, cy, w, juce::Justification::centred);
}

void left(juce::Graphics& g, const juce::String& s, float size, juce::Colour c, int x, int cy, int w = 120) {
    text(g, s, size, c, x, cy, w, juce::Justification::centredLeft);
}
}  // namespace

ClassicConsole::ClassicConsole(ConsoleModel& model, ClassicSkin& skin) : model_(model), skin_(skin) {
    setOpaque(true);
    // At rest the LCD shows only the fader budget; hovering fills the top.
    lcdTitle_ = model_.connected() ? juce::String() : juce::String("Not connected");
    lcdDetail_ = model_.error();
    model_.onChange = [this](bool) { setScroll(scroll_); repaint(); };
}

ClassicConsole::~ClassicConsole() { model_.onChange = nullptr; }

int ClassicConsole::idealWidth() const {
    return kStripsLeft + (int)juce::jmax<size_t>(model_.strips().size(), 4) * kStripPitch + kPanelWidth;
}

void ClassicConsole::setScroll(int s) {
    scroll_ = juce::jlimit(0, juce::jmax(0, contentWidth() - stripsWidth()), s);
    repaint();
}

void ClassicConsole::paint(juce::Graphics& g) {
    // Background: left cap, tiled body, and the PCI right panel.
    skin_.draw(g, "BackgroundLeftLegacy", 0, 0);
    for (int x = 100; x < panelX(); x += 100) {
        juce::Graphics::ScopedSaveState s(g);
        g.reduceClipRegion(x, 0, panelX() - x, kHeight);
        skin_.draw(g, "BackgroundTileLegacy", x, 0);
    }

    {
        juce::Graphics::ScopedSaveState s(g);
        g.reduceClipRegion(kStripsLeft, 0, stripsWidth(), kHeight);
        const auto& strips = model_.strips();
        for (size_t i = 0; i < strips.size(); ++i) {
            const int x = kStripsLeft + (int)i * kStripPitch - scroll_;
            if (x + kStripPitch < 0 || x > panelX()) continue;
            paintStrip(g, strips[i], x);
        }
    }

    paintScrollBar(g);
    paintPanel(g, panelX());
}

void ClassicConsole::paintStrip(juce::Graphics& g, const StripInfo& info, int x) {
    const auto& st = info.state;

    // Input section.
    skin_.drawFrame(g, "TextButtons", 82, 20, st.inputMute ? 1 : 0, 0, x, 3);
    left(g, "MUTE", 11.0f, kLabel, x + 6, 13, 34);

    skin_.draw(g, "KnobWithStringWell", x, 24);
    skin_.drawFrame(g, "KnobRotation", 26, 26, trimFrame(st.trim), x + 10, 35);
    skin_.drawFrame(g, "TrimClipIndicator", 6, 6, 0, x + 44, 33);   // 3 frames: off, signal, clip
    left(g, "TRIM", 11.5f, kLabel, x + 53, 37, 30);
    centred(g, juce::String(st.trim - 64) + " dB", 12.0f, kWellText, x + 62, 54, 36);

    centred(g, "MONO", 10.5f, kLabel, x + 20, 73, 40);
    centred(g, "STEREO", 10.5f, kLabel, x + 61, 73, 40);
    // Mono/stereo is not read from the card yet; MOTU's default is mono.
    // HalfButtons: row 0 lights on the left edge (MONO), row 1 on the right (STEREO).
    skin_.drawFrame(g, "HalfButtons", 40, 20, 1, 0, x, 79);
    skin_.drawFrame(g, "HalfButtons", 40, 20, 0, 1, x + 40, 79);

    skin_.drawStretchedV(g, "ChannelNameWell", x, 100, 28, 6);
    skin_.draw(g, "ChannelNameGlare", x + 2, 101);
    centred(g, info.interfaceName, 11.5f, kWellText, x + 40, 109, 76);
    centred(g, info.channelName, 11.5f, kWellText, x + 40, 120, 76);

    // Mix section.
    skin_.draw(g, "ChannelStripMixLegacy", x, 151);
    skin_.drawFrame(g, "KnobRotation", 26, 26, panFrame(st.pan), x + 10, 162);
    centred(g, "PAN", 11.5f, kLabel, x + 66, 163, 30);
    centred(g, panText(st.pan), 12.0f, kWellText, x + 62, 181, 36);

    skin_.drawFrame(g, "CircleCheckBox", 10, 10, 0, x + 4, 193);
    left(g, "BAL", 10.0f, kLabel, x + 16, 198, 24);
    skin_.drawFrame(g, "CircleCheckBox", 10, 10, 0, x + 40, 193);
    left(g, "WIDTH", 10.0f, kLabel, x + 52, 198, 30);

    skin_.drawFrame(g, "ColorButtons", 24, 17, st.solo ? 1 : 0, 1, x + 1, 300);
    centred(g, "SOLO", 9.5f, kLabel, x + 12, 320, 30);
    skin_.drawFrame(g, "ColorButtons", 24, 17, st.mute ? 1 : 0, 2, x + 1, 327);
    centred(g, "MUTE", 9.5f, kLabel, x + 12, 347, 30);

    skin_.drawFrame(g, "FaderCap", 22, 47, 0, x + 38, faderCapTop(st.volume));
    centred(g, volumeText(st.volume), 12.0f, kWellText, x + 55, 442, 44);
}

void ClassicConsole::paintScrollBar(juce::Graphics& g) {
    const int trackW = stripsWidth() - 37;
    {
        juce::Graphics::ScopedSaveState s(g);
        g.reduceClipRegion(kStripsLeft, kScrollY, trackW, 14);
        skin_.tileH(g, "ScrollBarTile", kStripsLeft, kScrollY, trackW);
    }
    const auto t = thumbBounds();
    skin_.drawStretchedH(g, "ScrollThumb", t.getX(), t.getY(), t.getWidth(), 8);
    skin_.drawFrame(g, "ScrollButtons", 37, 14, 0, 0, panelX() - 37, kScrollY);
}

juce::Rectangle<int> ClassicConsole::thumbBounds() const {
    const int trackW = stripsWidth() - 37;
    const int total = juce::jmax(1, contentWidth());
    const int w = juce::jlimit(34, trackW, trackW * stripsWidth() / total);
    const int range = juce::jmax(1, contentWidth() - stripsWidth());
    const int x = kStripsLeft + (trackW - w) * scroll_ / range;
    return { x, kScrollY, w, 14 };
}

void ClassicConsole::paintPanel(juce::Graphics& g, int rx) {
    skin_.draw(g, "BackgroundRightPCI", rx, 0);

    // LCD: hovered control, then the fader budget. "Sequencer" is what the card
    // reports in use beyond CueMix's own mixes (tentative).
    const bool notice = model_.noticeTitle().isNotEmpty();
    left(g, notice ? model_.noticeTitle() : lcdTitle_, 18.0f, kLcd, rx + 27, 30, 210);
    left(g, notice ? model_.noticeDetail() : lcdDetail_, 11.0f, kLcd, rx + 27, 47, 210);
    const auto r = model_.resources();
    const int cuemix = model_.cueMixFaders();
    left(g, juce::String(r.used) + " out of " + juce::String(r.max) + " faders in use", 11.5f, kLcd, rx + 27, 110, 210);
    left(g, "Sequencer using " + juce::String(juce::jmax(0, r.used - cuemix)) + " faders", 11.5f, kLcd, rx + 27, 121, 210);
    left(g, "CueMix using " + juce::String(cuemix) + " faders", 11.5f, kLcd, rx + 27, 132, 210);

    // DSP use: PCI cards have no DSP, so the meter sits at its first segment.
    left(g, "DSP", 11.5f, kLabel, rx + 110, 174, 30);
    g.setColour(juce::Colour(0xff8fd4ff));
    g.fillRect(rx + 132, 172, 2, 8);

    // Output and master strip.
    left(g, "OUTPUT", 11.5f, kLabel, rx + 3, 173, 60);
    skin_.drawStretchedH(g, "ChannelNameWell", rx + 1, 184, 88, 8);
    {
        juce::Graphics::ScopedSaveState s(g);
        g.reduceClipRegion(rx + 3, 184, 83, 18);
        left(g, model_.mixOutputName(model_.selectedMix()), 11.0f, kWellText, rx + 5, 193, 200);
    }
    skin_.draw(g, "ChannelStripMMPCI", rx, 208);
    // The master meters' scale is not in the strip art; it is Level's unlit column.
    skin_.drawFrame(g, "Level", 11, 183, 2, 0, rx + 48, 223);
    skin_.drawFrame(g, "Level", 11, 183, 2, 0, rx + 72, 223);
    skin_.drawFrame(g, "FaderCap", 22, 47, 0, rx + 19, faderCapTop(model_.masterVolume()));
    centred(g, volumeText(model_.masterVolume()), 12.0f, kWellText, rx + 27, 442, 44);
    skin_.drawFrame(g, "ColorButtons", 24, 17, model_.masterMute() ? 1 : 0, 2, rx + 59, 428);
    centred(g, "MUTE", 9.5f, kLabel, rx + 70, 448, 30);

    // Talkback / Listenback, as the card reports them (read-only).
    const auto& tb = model_.talkback();
    const std::pair<int, juce::String> sources[] = { { 111, tb.talkName }, { 186, tb.listenName } };
    for (auto& [px, name] : sources) {
        skin_.draw(g, "MenuShort", rx + px, 214);
        juce::Graphics::ScopedSaveState s(g);
        g.reduceClipRegion(rx + px + 3, 214, 44, 21);
        left(g, name, 11.0f, kLabel, rx + px + 6, 224, 60);
    }
    const struct { int px; const char* label; bool on; } buttons[] = {
        { 118, "TALK", tb.talk }, { 161, "LINK", tb.link }, { 205, "LISTEN", tb.listen } };
    for (auto& b : buttons) {
        skin_.drawFrame(g, "TalkbackButton", 37, 37, b.on ? 1 : 0, 0, rx + b.px, 241);
        centred(g, b.label, 9.5f, kLabel, rx + b.px + 18, 259, 36);
    }
    // Dim knobs: frame 4-60 across an assumed 0-255 range.
    auto dimFrame = [](int v) { return 4 + juce::jlimit(0, 56, v * 56 / 255); };
    skin_.drawFrame(g, "SmallKnobBlack", 31, 31, dimFrame(tb.talkDim), rx + 119, 288);
    skin_.drawFrame(g, "SmallKnobBlack", 31, 31, dimFrame(tb.listenDim), rx + 206, 288);
    centred(g, "MONITOR", 9.0f, kLabel, rx + 177, 298, 50);
    centred(g, "DIM", 9.0f, kLabel, rx + 177, 306, 50);

    // Mix selector.
    left(g, "MIX", 15.0f, kLabel, rx + 114, 350, 30);
    left(g, model_.mixName(model_.selectedMix()), 12.0f, kWellText, rx + 144, 351, 70);

    // Scope Channel Selection (placeholder sources: the first two inputs).
    centred(g, "Scope Channel Selection", 10.5f, kLabel, rx + 167, 378, 140);
    left(g, "Left", 11.0f, kLabel, rx + 114, 398, 30);
    left(g, "Right", 11.0f, kLabel, rx + 114, 423, 30);
    const auto& strips = model_.strips();
    for (int i = 0; i < 2; ++i) {
        const int y = 390 + i * 25;
        skin_.drawStretchedH(g, "MenuLong", rx + 145, y, 97, 12);
        centred(g, i < (int)strips.size() ? strips[(size_t)i].channelName : juce::String(), 11.0f, kLabel,
                rx + 145 + 42, y + 10, 76);
    }
}

void ClassicConsole::mouseMove(const juce::MouseEvent& e) {
    const auto p = e.getPosition();
    juce::String title, detail;
    if (p.x >= kStripsLeft && p.x < panelX() && p.y > kScrollY + 14) {
        const int i = (p.x - kStripsLeft + scroll_) / kStripPitch;
        const auto& strips = model_.strips();
        if (juce::isPositiveAndBelow(i, (int)strips.size())) {
            title = strips[(size_t)i].channelName;
            detail = strips[(size_t)i].interfaceName + "  input " + juce::String(strips[(size_t)i].id);
        }
    }
    if (title != lcdTitle_ || detail != lcdDetail_) {
        lcdTitle_ = title;
        lcdDetail_ = detail;
        repaint(panelX(), 0, kPanelWidth, 150);
    }
}

void ClassicConsole::mouseDown(const juce::MouseEvent& e) {
    dragOffset_ = -1;
    const auto t = thumbBounds();
    if (t.contains(e.getPosition())) { dragOffset_ = e.x - t.getX(); return; }
    // Scroll buttons: left half steps left, right half right.
    if (e.y >= kScrollY && e.y < kScrollY + 14 && e.x >= panelX() - 37 && e.x < panelX())
        setScroll(scroll_ + (e.x < panelX() - 18 ? -kStripPitch : kStripPitch));
}

void ClassicConsole::mouseDrag(const juce::MouseEvent& e) {
    if (dragOffset_ < 0) return;
    const auto t = thumbBounds();
    const int trackW = stripsWidth() - 37 - t.getWidth();
    if (trackW <= 0) return;
    const int x = e.x - dragOffset_ - kStripsLeft;
    setScroll(x * juce::jmax(0, contentWidth() - stripsWidth()) / trackW);
}

void ClassicConsole::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) {
    const float d = std::abs(w.deltaX) > std::abs(w.deltaY) ? w.deltaX : w.deltaY;
    setScroll(scroll_ - (int)std::lround(d * 400.0f));
}
