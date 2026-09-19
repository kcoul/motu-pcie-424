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
    juce::String customName;               // "" when the channel has no custom name
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

    // --- Level meters ------------------------------------------------------
    //
    // One per strip, in strips() order. Units are MOTU's: level and peak are
    // 0..1, i.e. the card's linear value over 32768 (ValueLegacyLevelMeter).
    //
    // `clipRaw` is the card's own clip field, which is an enum and not a
    // boolean: 0 is none, and both 1 and 2 occur. MOTU renders 1 fully lit and
    // 2 half lit, but which of them is the instantaneous state and which the
    // held one is the single thing that needs the card and real signal to
    // settle (docs/CUEMIX-API.md).
    struct MeterState {
        float level = 0.0f;
        float peak = 0.0f;
        int clipRaw = 0;
        bool clip() const { return clipRaw != 0; }
    };
    const std::vector<MeterState>& meters() const { return meters_; }

    // Peak hold, in seconds; -1 holds forever (MOTU's "Infinite"). The values
    // come from ConvertPeakHoldTimeEnumToSeconds: 0, 2, 4, 10, 60, 300, -1,
    // defaulting to 4.
    void setPeakHoldSeconds(double s) { peakHoldSeconds_ = s; }
    double peakHoldSeconds() const { return peakHoldSeconds_; }
    void clearPeaks();

    // With no card there is nothing to meter, so the meters run off a built-in
    // test signal instead. That makes the widget developable and reviewable
    // away from the studio; it is never used when a card is present.
    bool metersAreSynthetic() const { return !connected(); }

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

    // --- Writing to the card -----------------------------------------------
    //
    // OFF by default, deliberately. The slots, argument order and encodings in
    // docs/CUEMIX-API.md were read out of MOTU's own binary and are not yet
    // confirmed against a PCI-424, so the first session with a card should
    // check that reads look right before letting anything write. With writes
    // off, a moved control stays local and the LCD says "not sent", which is
    // the behaviour this app has had all along.
    //
    // Nothing here needs CommitChanges: on this back end fader, pan, solo and
    // mute reach the card immediately, and CommitChanges only pushes stereo
    // pairing. Stereo is therefore still not written.
    void setWritesEnabled(bool on);
    bool writesEnabled() const { return writes_; }
    bool hasLocalChanges() const { return !overrides_.empty(); }
    void clearLocalChanges();
    int scopeSource(int side) const;                     // strip index, 0 = Left

    // Renaming *is* written to the card: it is the same driver call PCI Audio
    // Setup's Edit Channel Names uses (verified safe), so both apps agree. An
    // empty name restores the hardware name.
    void renameStrip(int strip, const juce::String& name);

    std::function<void(bool layoutChanged)> onChange;
    // Fired on every meter tick, which is faster than onChange.
    std::function<void()> onMeters;

private:
    void timerCallback() override { refresh(); }

    // Meters run on their own, faster timer: the control state only needs
    // 10 Hz, but a meter at 10 Hz looks broken.
    class MeterTimer : public juce::Timer {
    public:
        explicit MeterTimer(ConsoleModel& m) : model_(m) {}
        void timerCallback() override { model_.pollMeters(); }
    private:
        ConsoleModel& model_;
    };
    MeterTimer meterTimer_ { *this };
    void pollMeters();
    void applyBallistics(MeterState&, float newLevel, double now);
    void synthesizeMeters(double now);

    std::vector<MeterState> meters_;
    std::vector<double> peakHoldUntil_;
    double peakHoldSeconds_ = 4.0;
    void refresh();
    bool syncLayout();
    bool syncDemoLayout();
    bool poll();
    bool refreshNames();
    int ticks_ = 0;

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
    bool writeToCard(Param, int value, int id);
    bool writes_ = false;
    int currentBus() const { return buses_.empty() ? -1 : buses_[(size_t)mix_]; }
};

// Displays, following MOTU's own laws (docs/CUEMIX-API.md): the fader is
// 40*log10(raw/32768) formatted the way DigiVolToDecibelString formats it, pan
// is raw-64 with a '+' when positive, trim is its hardware value with a '+'
// when positive. Recovered from MOTU's binary, not yet checked against a card.
juce::String volumeText(int raw);
double volumeDb(int raw);
int quantizeVolume(int raw);      // multiples of 256, capped at 0x8000
juce::String panText(int raw);
juce::String trimText(int raw);
