#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <map>

// MOTU's own CueMix FX artwork: the "Legacy"/PCI PNG set from the original
// CueMix FX.app bundle, drawn exactly where MOTU drew it. Positions were found
// by matching each sprite against docs/reference/cuemix-console-full.png.
//
// Sprites are sheets of equal frames: e.g. KnobRotation is 8 x 8 frames of
// 26 x 26 (frame 32 = centre), TextButtons 6 x 2 of 82 x 20.
class ClassicSkin {
public:
    // Looks in assets/classic next to the app or in the repo, then in any
    // CueMix FX.app under /Applications, ~/Applications or /Volumes/*/Applications.
    static juce::File findResources(const juce::File& preferred = {});
    static bool isUsable(const juce::File& dir);

    explicit ClassicSkin(const juce::File& dir);
    bool ok() const { return ok_; }
    juce::File directory() const { return dir_; }

    const juce::Image& image(const char* name);

    // Frame `index` of a sheet whose frames are fw x fh, laid out row by row.
    void drawFrame(juce::Graphics&, const char* name, int fw, int fh, int index, int x, int y);
    void drawFrame(juce::Graphics&, const char* name, int fw, int fh, int col, int row, int x, int y);
    void draw(juce::Graphics&, const char* name, int x, int y);
    // Three-slice stretch, keeping `cap` pixels at each end untouched.
    void drawStretchedH(juce::Graphics&, const char* name, int x, int y, int w, int cap);
    void drawStretchedV(juce::Graphics&, const char* name, int x, int y, int h, int cap);
    void tileH(juce::Graphics&, const char* name, int x, int y, int w);

    // MOTU's labels are a heavy condensed sans.
    static juce::Font font(float height);

private:
    juce::File dir_;
    bool ok_ = false;
    std::map<std::string, juce::Image> images_;
};
