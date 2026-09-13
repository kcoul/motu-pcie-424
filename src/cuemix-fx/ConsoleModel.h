#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "motu_card.h"

#include <functional>
#include <map>
#include <tuple>
#include <vector>

// The CueMix values one input strip shows, for the mix currently selected.
struct StripState {
    int trim = 64, inputMute = 0, stereo = 0;     // per input (stereo is not read yet)
    int volume = 32768, pan = 64;                 // per (mix bus, input)
    int mute = 0, solo = 0, balWidth = 0;         // balWidth: 0 = BAL, 1 = WIDTH (not read yet)
    bool operator==(const StripState& o) const {
        return trim == o.trim && inputMute == o.inputMute && stereo == o.stereo && volume == o.volume
            && pan == o.pan && mute == o.mute && solo == o.solo && balWidth == o.balWidth;
    }
    bool operator!=(const StripState& o) const { return !(*this == o); }
};

struct StripInfo {
    int id = 0;                            // card-wide input id
    juce::String interfaceName;            // "HD192"
    juce::String channelName;              // custom name, or "Analog-A 3"
    StripState state;
};

// Live CueMix state of the card, shared by every skin. Polls at 10 Hz and
// reports whether the layout (strips, mixes) or only values changed.
//
// The strip set is the card's active inputs; the mixes are the output pairs
// CueMix accepts as buses (docs/CUEMIX-PLAN.md).
//
// Nothing is written to the card yet. Controls can still be moved: a moved
// value is kept locally, shown instead of the card's, and reported in the LCD as
// "not sent". Once encodings are verified, setLocal becomes the write path.
class ConsoleModel : private juce::Timer {
public:
    ConsoleModel();
    ~ConsoleModel() override;

    bool connected() const { return (bool)card_; }
    juce::String error() const { return error_; }

    const std::vector<StripInfo>& strips() const { return strips_; }
    int numMixes() const { return (int)buses_.size(); }
    juce::String mixName(int index) const;      // "Mix 1"
    juce::String mixOutputName(int index) const; // "HD192:Analog/AES 1-2"
    int selectedMix() const { return mix_; }
    void selectMix(int index);

    int masterVolume() const { return masterVolume_; }
    bool masterMute() const { return masterMute_; }
    motu::CueMix::Resources resources() const { return resources_; }
    int cueMixFaders() const { return cueMixFaders_; }

    // Talkback / Listenback, read-only. An input of 4095 means Disabled; the dim
    // levels' range is not verified (0-255 assumed for display).
    struct Talkback {
        int talkInput = 4095, listenInput = 4095;
        int talkDim = 0, listenDim = 0;
        bool talk = false, listen = false, link = false;
        juce::String talkName = "Disabled", listenName = "Disabled";
        bool operator!=(const Talkback& o) const {
            return talkInput != o.talkInput || listenInput != o.listenInput || talkDim != o.talkDim
                || listenDim != o.listenDim || talk != o.talk || listen != o.listen || link != o.link;
        }
    };
    const Talkback& talkback() const { return talkback_; }

    // A short message for the LCD, e.g. for menu items not implemented yet.
    // Clears itself after a few seconds.
    void showNotice(const juce::String& title, const juce::String& detail);
    juce::String noticeTitle() const { return noticeTitle_; }
    juce::String noticeDetail() const { return noticeDetail_; }

    // Every control a skin can move.
    enum class Param {
        Trim, InputMute, Stereo,                         // per input
        Volume, Pan, Mute, Solo, BalWidth,               // per input, per mix
        MasterVolume, MasterMute,                        // per mix
        TalkInput, ListenInput, TalkDim, ListenDim, Talk, Listen, Link,
        ScopeLeft, ScopeRight,                           // strip index
    };
    // `strip` is an index into strips(), for the per-input parameters.
    void setLocal(Param, int value, int strip = -1);
    bool hasLocalChanges() const { return !overrides_.empty(); }
    void clearLocalChanges();
    int scopeSource(int side) const;                     // strip index, 0 = Left

    std::function<void(bool layoutChanged)> onChange;

private:
    void timerCallback() override { refresh(); }
    void refresh();
    bool syncLayout();
    bool poll();

    AudioDeviceID dev_ = 0;
    motu::Card card_;
    juce::String error_;

    std::vector<StripInfo> strips_;
    std::vector<int> buses_;
    juce::StringArray busNames_;
    int mix_ = 0;
    int masterVolume_ = 32768;
    bool masterMute_ = false;
    motu::CueMix::Resources resources_;
    int cueMixFaders_ = 0;
    Talkback talkback_;
    juce::String noticeTitle_, noticeDetail_;
    juce::uint32 noticeUntil_ = 0;

    // Local values, keyed by (bus or -1, input id or -1, param).
    std::map<std::tuple<int, int, int>, int> overrides_;
    int local(Param, int bus, int id, int cardValue) const;
    int currentBus() const { return buses_.empty() ? -1 : buses_[(size_t)mix_]; }
};

// Tentative displays. Only 32768 = 0 dB and 64 = centre are observed; the laws
// around them are not verified yet.
juce::String volumeText(int raw);
double volumeDb(int raw);
juce::String panText(int raw);
