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

// The inverse: the level a cap top position stands for (0 at the bottom stop).
int volumeFromCapTop(int top) {
    const int y = top - 216 + 234;
    const size_t n = std::size(kFaderScale);
    if (y >= kFaderScale[n - 1].y) return 0;
    if (y <= kFaderScale[0].y) return 32768;
    for (size_t i = 0; i + 1 < n; ++i) {
        const auto& a = kFaderScale[i];
        const auto& b = kFaderScale[i + 1];
        if (y <= b.y) {
            if (b.db < -999.0) return 0;   // between -60 and the bottom stop
            const double db = a.db + (y - a.y) * (b.db - a.db) / (double)(b.y - a.y);
            return (int)std::lround(32768.0 * std::pow(10.0, db / 20.0));
        }
    }
    return 0;
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
    skin_.drawFrame(g, "HalfButtons", 40, 20, st.stereo ? 0 : 1, 0, x, 79);
    skin_.drawFrame(g, "HalfButtons", 40, 20, st.stereo ? 1 : 0, 1, x + 40, 79);

    skin_.drawStretchedV(g, "ChannelNameWell", x, 100, 28, 6);
    skin_.draw(g, "ChannelNameGlare", x + 2, 101);
    centred(g, info.interfaceName, 11.5f, kWellText, x + 40, 109, 76);
    centred(g, info.channelName, 11.5f, kWellText, x + 40, 120, 76);

    // Mix section.
    skin_.draw(g, "ChannelStripMixLegacy", x, 151);
    skin_.drawFrame(g, "KnobRotation", 26, 26, panFrame(st.pan), x + 10, 162);
    centred(g, "PAN", 11.5f, kLabel, x + 66, 163, 30);
    centred(g, panText(st.pan), 12.0f, kWellText, x + 62, 181, 36);

    // BAL / WIDTH only mean something on a stereo strip; MOTU shows both empty on mono.
    skin_.drawFrame(g, "CircleCheckBox", 10, 10, st.stereo && st.balWidth == 0 ? 1 : 0, x + 4, 193);
    left(g, "BAL", 10.0f, kLabel, x + 16, 198, 24);
    skin_.drawFrame(g, "CircleCheckBox", 10, 10, st.stereo && st.balWidth == 1 ? 1 : 0, x + 40, 193);
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

    // Scope Channel Selection (local only until the scope exists).
    // The group frame's top edge (row 378 of the art) runs through the title;
    // MOTU breaks it behind the text, so clear a gap in the panel's grey first.
    {
        const juce::String title = "Scope Channel Selection";
        // 12 px matches the original title's 98 px width; the line stops at the "S" and "n".
        const int w = (int)std::ceil(juce::GlyphArrangement::getStringWidth(ClassicSkin::font(12.0f), title));
        g.setColour(juce::Colour(218, 217, 217));
        g.fillRect(rx + 168 - w / 2 - 1, 372, w, 12);
        centred(g, title, 12.0f, kLabel, rx + 168, 377, 140);
    }
    left(g, "Left", 11.0f, kLabel, rx + 114, 398, 30);
    left(g, "Right", 11.0f, kLabel, rx + 114, 423, 30);
    const auto& strips = model_.strips();
    for (int i = 0; i < 2; ++i) {
        const int y = 390 + i * 25;
        skin_.drawStretchedH(g, "MenuLong", rx + 145, y, 97, 12);
        const int src = model_.scopeSource(i);
        centred(g, juce::isPositiveAndBelow(src, (int)strips.size()) ? strips[(size_t)src].channelName : juce::String(), 11.0f, kLabel,
                rx + 145 + 42, y + 10, 76);
    }
}

ClassicConsole::Hit ClassicConsole::hitAt(juce::Point<int> p) const {
    using P = ConsoleModel::Param;
    Hit h;
    const auto make = [&](Kind k, P param, int strip, int value, juce::Rectangle<int> area, int min = 0, int max = 1,
                          int centre = 0) {
        h.kind = k; h.param = param; h.strip = strip; h.value = value; h.area = area;
        h.min = min; h.max = max; h.centre = centre;
    };
    const int rx = panelX();

    if (p.x >= kStripsLeft && p.x < rx && !(p.y >= kScrollY && p.y < kScrollY + 14)) {
        const int i = (p.x - kStripsLeft + scroll_) / kStripPitch;
        const auto& strips = model_.strips();
        if (!juce::isPositiveAndBelow(i, (int)strips.size())) return h;
        const auto& st = strips[(size_t)i].state;
        const int x = kStripsLeft + i * kStripPitch - scroll_;
        const auto local = p - juce::Point<int>(x, 0);
        const auto in = [&](int ax, int ay, int aw, int ah) { return juce::Rectangle<int>(ax, ay, aw, ah).contains(local); };
        const auto at = [&](int ax, int ay, int aw, int ah) { return juce::Rectangle<int>(x + ax, ay, aw, ah); };

        if (in(0, 3, 82, 20))    make(Kind::Toggle, P::InputMute, i, st.inputMute, at(0, 3, 82, 20));
        else if (in(4, 28, 44, 44))  make(Kind::Knob, P::Trim, i, st.trim, at(4, 28, 44, 44), 64, 120, 64);
        else if (in(0, 79, 40, 20))  make(Kind::Toggle, P::Stereo, i, 0, at(0, 79, 40, 20));
        else if (in(40, 79, 40, 20)) make(Kind::Toggle, P::Stereo, i, 1, at(40, 79, 40, 20));
        else if (in(4, 155, 44, 38)) make(Kind::Knob, P::Pan, i, st.pan, at(4, 155, 44, 38), 0, 128, 64);
        else if (in(2, 190, 36, 16)) make(Kind::Toggle, P::BalWidth, i, 0, at(2, 190, 36, 16));
        else if (in(38, 190, 42, 16)) make(Kind::Toggle, P::BalWidth, i, 1, at(38, 190, 42, 16));
        else if (in(0, 298, 28, 24)) make(Kind::Toggle, P::Solo, i, st.solo, at(0, 298, 28, 24));
        else if (in(0, 325, 28, 26)) make(Kind::Toggle, P::Mute, i, st.mute, at(0, 325, 28, 26));
        else if (in(34, 206, 30, 234)) make(Kind::Fader, P::Volume, i, st.volume, at(34, 206, 30, 234), 0, 32768, 32768);
        // Radio pairs (MONO/STEREO, BAL/WIDTH) carry the value they set, so mark them.
        if (h.param == P::Stereo || h.param == P::BalWidth) h.max = -1;
        return h;
    }

    const auto local = p - juce::Point<int>(rx, 0);
    const auto in = [&](int ax, int ay, int aw, int ah) { return juce::Rectangle<int>(ax, ay, aw, ah).contains(local); };
    const auto at = [&](int ax, int ay, int aw, int ah) { return juce::Rectangle<int>(rx + ax, ay, aw, ah); };
    const auto& tb = model_.talkback();

    if (in(15, 206, 30, 234))        make(Kind::Fader, P::MasterVolume, -1, model_.masterVolume(), at(15, 206, 30, 234), 0, 32768, 32768);
    else if (in(56, 425, 30, 34))    make(Kind::Toggle, P::MasterMute, -1, model_.masterMute(), at(56, 425, 30, 34));
    else if (in(111, 214, 59, 21))   make(Kind::Popup, P::TalkInput, -1, tb.talkInput, at(111, 214, 59, 21));
    else if (in(186, 214, 59, 21))   make(Kind::Popup, P::ListenInput, -1, tb.listenInput, at(186, 214, 59, 21));
    else if (in(118, 241, 37, 37))   make(Kind::Toggle, P::Talk, -1, tb.talk, at(118, 241, 37, 37));
    else if (in(161, 241, 37, 37))   make(Kind::Toggle, P::Link, -1, tb.link, at(161, 241, 37, 37));
    else if (in(205, 241, 37, 37))   make(Kind::Toggle, P::Listen, -1, tb.listen, at(205, 241, 37, 37));
    else if (in(116, 285, 37, 37))   make(Kind::Knob, P::TalkDim, -1, tb.talkDim, at(116, 285, 37, 37), 0, 255, 0);
    else if (in(203, 285, 37, 37))   make(Kind::Knob, P::ListenDim, -1, tb.listenDim, at(203, 285, 37, 37), 0, 255, 0);
    else if (in(138, 341, 100, 20))  make(Kind::Popup, P::Volume, -2, model_.selectedMix(), at(138, 341, 100, 20)); // MIX
    else if (in(145, 390, 97, 21))   make(Kind::Popup, P::ScopeLeft, -1, model_.scopeSource(0), at(145, 390, 97, 21));
    else if (in(145, 415, 97, 21))   make(Kind::Popup, P::ScopeRight, -1, model_.scopeSource(1), at(145, 415, 97, 21));
    return h;
}

void ClassicConsole::showPopup(const Hit& h) {
    using P = ConsoleModel::Param;
    juce::PopupMenu m;
    const auto& strips = model_.strips();
    const bool mix = h.strip == -2;
    const bool source = h.param == P::TalkInput || h.param == P::ListenInput;
    if (mix) {
        for (int i = 0; i < model_.numMixes(); ++i)
            m.addItem(i + 1, model_.mixName(i) + "   " + model_.mixOutputName(i), true, i == h.value);
    } else {
        if (source) { m.addItem(4095 + 1, "Disabled", true, h.value == 4095); m.addSeparator(); }
        for (size_t i = 0; i < strips.size(); ++i) {
            const int value = source ? strips[i].id : (int)i;
            m.addItem(value + 1, strips[i].interfaceName + ": " + strips[i].channelName, true, value == h.value);
        }
    }
    const auto screen = localAreaToGlobal(h.area);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(screen).withMinimumWidth(h.area.getWidth()),
                    [this, h, mix](int result) {
        if (result <= 0) return;
        if (mix) model_.selectMix(result - 1);
        else model_.setLocal(h.param, result - 1);
    });
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
    drag_ = {};
    dragOffset_ = -1;
    const auto t = thumbBounds();
    if (t.contains(e.getPosition())) { dragOffset_ = e.x - t.getX(); return; }
    // Scroll buttons: left half steps left, right half right.
    if (e.y >= kScrollY && e.y < kScrollY + 14 && e.x >= panelX() - 37 && e.x < panelX()) {
        setScroll(scroll_ + (e.x < panelX() - 18 ? -kStripPitch : kStripPitch));
        return;
    }

    const auto h = hitAt(e.getPosition());
    switch (h.kind) {
        case Kind::None: break;
        case Kind::Toggle:
            // Radio pairs set their own value; everything else flips.
            model_.setLocal(h.param, h.max == -1 ? h.value : (h.value ? 0 : 1), h.strip);
            break;
        case Kind::Popup:
            showPopup(h);
            break;
        case Kind::Knob:
            drag_ = Drag { h, e.y, h.value, 0 };
            break;
        case Kind::Fader: {
            const int top = faderCapTop(h.value);
            // Grab the cap where it is; a click elsewhere on the track jumps to it.
            if (e.y < top || e.y > top + 47) {
                const int newTop = juce::jlimit(216, 388, e.y - 23);
                model_.setLocal(h.param, volumeFromCapTop(newTop), h.strip);
                drag_ = Drag { h, e.y, 0, newTop };
            } else {
                drag_ = Drag { h, e.y, h.value, top };
            }
            break;
        }
    }
}

void ClassicConsole::mouseDrag(const juce::MouseEvent& e) {
    if (drag_) {
        const auto& h = drag_->hit;
        if (h.kind == Kind::Knob) {
            // About 150 px of travel for the full sweep, like MOTU's knobs.
            const double perPixel = (h.max - h.min) / 150.0;
            const int v = juce::jlimit(h.min, h.max, drag_->startValue + (int)std::lround((drag_->startY - e.y) * perPixel));
            model_.setLocal(h.param, v, h.strip);
        } else if (h.kind == Kind::Fader) {
            const int top = juce::jlimit(216, 388, drag_->startCapTop + (e.y - drag_->startY));
            model_.setLocal(h.param, volumeFromCapTop(top), h.strip);
        }
        return;
    }
    if (dragOffset_ < 0) return;
    const auto t = thumbBounds();
    const int trackW = stripsWidth() - 37 - t.getWidth();
    if (trackW <= 0) return;
    const int x = e.x - dragOffset_ - kStripsLeft;
    setScroll(x * juce::jmax(0, contentWidth() - stripsWidth()) / trackW);
}

void ClassicConsole::mouseDoubleClick(const juce::MouseEvent& e) {
    // Double-click returns a knob or fader to its default.
    const auto h = hitAt(e.getPosition());
    if (h.kind == Kind::Knob || h.kind == Kind::Fader) model_.setLocal(h.param, h.centre, h.strip);
}

void ClassicConsole::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) {
    const float d = std::abs(w.deltaX) > std::abs(w.deltaY) ? w.deltaX : w.deltaY;
    setScroll(scroll_ - (int)std::lround(d * 400.0f));
}
