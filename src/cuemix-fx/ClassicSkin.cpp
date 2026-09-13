#include "ClassicSkin.h"

namespace {
const char* kProbe = "BackgroundRightPCI.png";

// Checked in order, stopping at the first hit. Other volumes come last:
// listing /Volumes makes macOS ask for removable-volume access.
juce::File firstUsable(const juce::File& preferred, bool (*usable)(const juce::File&)) {
    juce::Array<juce::File> dirs;
    if (preferred != juce::File()) dirs.add(preferred);
    const auto app = juce::File::getSpecialLocation(juce::File::currentApplicationFile);
    dirs.add(app.getChildFile("Contents/Resources/classic"));
    // Walk up from the build tree to the repo's assets/classic.
    for (auto d = app.getParentDirectory(); d.getParentDirectory() != d; d = d.getParentDirectory())
        if (d.getChildFile("assets/classic").isDirectory()) { dirs.add(d.getChildFile("assets/classic")); break; }
    const juce::String bundle = "CueMix FX.app/Contents/Resources";
    dirs.add(juce::File("/Applications").getChildFile(bundle));
    dirs.add(juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile("Applications").getChildFile(bundle));
    for (const auto& d : dirs)
        if (usable(d)) return d;

    for (const auto& vol : juce::File("/Volumes").findChildFiles(juce::File::findDirectories, false))
        if (usable(vol.getChildFile("Applications").getChildFile(bundle)))
            return vol.getChildFile("Applications").getChildFile(bundle);
    return {};
}
}  // namespace

bool ClassicSkin::isUsable(const juce::File& dir) { return dir.getChildFile(kProbe).existsAsFile(); }

juce::File ClassicSkin::findResources(const juce::File& preferred) {
    return firstUsable(preferred, &ClassicSkin::isUsable);
}

ClassicSkin::ClassicSkin(const juce::File& dir) : dir_(dir), ok_(isUsable(dir)) {}

const juce::Image& ClassicSkin::image(const char* name) {
    auto it = images_.find(name);
    if (it != images_.end()) return it->second;
    auto img = juce::ImageFileFormat::loadFrom(dir_.getChildFile(juce::String(name) + ".png"));
    return images_.emplace(name, img).first->second;
}

void ClassicSkin::drawFrame(juce::Graphics& g, const char* name, int fw, int fh, int col, int row, int x, int y) {
    const auto& img = image(name);
    if (img.isValid()) g.drawImage(img, x, y, fw, fh, col * fw, row * fh, fw, fh);
}

void ClassicSkin::drawFrame(juce::Graphics& g, const char* name, int fw, int fh, int index, int x, int y) {
    const auto& img = image(name);
    if (!img.isValid()) return;
    const int cols = juce::jmax(1, img.getWidth() / fw);
    drawFrame(g, name, fw, fh, index % cols, index / cols, x, y);
}

void ClassicSkin::draw(juce::Graphics& g, const char* name, int x, int y) {
    const auto& img = image(name);
    if (img.isValid()) g.drawImageAt(img, x, y);
}

void ClassicSkin::drawStretchedH(juce::Graphics& g, const char* name, int x, int y, int w, int cap) {
    const auto& img = image(name);
    if (!img.isValid()) return;
    const int iw = img.getWidth(), ih = img.getHeight();
    g.drawImage(img, x, y, cap, ih, 0, 0, cap, ih);
    g.drawImage(img, x + cap, y, w - 2 * cap, ih, cap, 0, iw - 2 * cap, ih);
    g.drawImage(img, x + w - cap, y, cap, ih, iw - cap, 0, cap, ih);
}

void ClassicSkin::drawStretchedV(juce::Graphics& g, const char* name, int x, int y, int h, int cap) {
    const auto& img = image(name);
    if (!img.isValid()) return;
    const int iw = img.getWidth(), ih = img.getHeight();
    g.drawImage(img, x, y, iw, cap, 0, 0, iw, cap);
    g.drawImage(img, x, y + cap, iw, h - 2 * cap, 0, cap, iw, ih - 2 * cap);
    g.drawImage(img, x, y + h - cap, iw, cap, 0, ih - cap, iw, cap);
}

void ClassicSkin::tileH(juce::Graphics& g, const char* name, int x, int y, int w) {
    const auto& img = image(name);
    if (!img.isValid()) return;
    for (int dx = 0; dx < w; dx += img.getWidth()) {
        const int cw = juce::jmin(img.getWidth(), w - dx);
        g.drawImage(img, x + dx, y, cw, img.getHeight(), 0, 0, cw, img.getHeight());
    }
}

juce::Font ClassicSkin::font(float height) {
    return juce::Font(juce::FontOptions("Helvetica Neue", "Condensed Bold", height));
}
