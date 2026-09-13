#include "ConsoleModel.h"

#include <cmath>

namespace {
constexpr int kPollHz = 10;

// "HD192:Analog-A" -> { "HD192", "Analog-A" }
std::pair<juce::String, juce::String> splitDescription(const juce::String& d) {
    const int colon = d.indexOfChar(':');
    if (colon < 0) return { {}, d };
    return { d.substring(0, colon), d.substring(colon + 1) };
}
}  // namespace

double volumeDb(int raw) { return raw <= 0 ? -1000.0 : 20.0 * std::log10(raw / 32768.0); }

juce::String volumeText(int raw) {
    return raw <= 0 ? juce::String("-inf") : juce::String(volumeDb(raw), 1) + " dB";
}

juce::String panText(int raw) { return juce::String(raw - 64); }

ConsoleModel::ConsoleModel() {
    dev_ = motu::Card::findDevice();
    std::string err;
    if (dev_) card_ = motu::Card::open(dev_, &err);
    else err = "No PCI-424 was found. Is MOTUPCIAudio.kext loaded?";
    error_ = juce::String(err);
    refresh();
    startTimerHz(kPollHz);
}

ConsoleModel::~ConsoleModel() { stopTimer(); }

juce::String ConsoleModel::mixName(int index) const { return "Mix " + juce::String(index + 1); }

juce::String ConsoleModel::mixOutputName(int index) const {
    return juce::isPositiveAndBelow(index, busNames_.size()) ? busNames_[index] : juce::String();
}

void ConsoleModel::selectMix(int index) {
    if (!juce::isPositiveAndBelow(index, (int)buses_.size()) || index == mix_) return;
    mix_ = index;
    poll();
    if (onChange) onChange(false);
}

void ConsoleModel::refresh() {
    if (!card_) return;
    const bool layout = syncLayout();
    const bool values = poll();
    if ((layout || values) && onChange) onChange(layout);
}

bool ConsoleModel::syncLayout() {
    motu::Exception e;
    std::vector<int> ids;
    for (int n = 0, count = card_.numActiveInputs(e); n < count; ++n) {
        const int id = card_.nthActiveInputID(e, n);
        if (!e.raised()) ids.push_back(id);
    }
    std::vector<int> buses;
    motu::CueMix cue = card_.cueMix(e);
    for (int id = 0, count = card_.numOutputs(e); cue && id + 1 < count; id += 2) {
        motu::Exception x;
        if (!card_.outputState(x, id).exists) continue;
        cue.busVolume(x, id);                     // range-checked: raises if not a bus
        if (!x.raised()) buses.push_back(id);
    }

    bool changed = false;
    if (ids.size() != strips_.size()
        || !std::equal(ids.begin(), ids.end(), strips_.begin(), [](int id, const StripInfo& s) { return id == s.id; })) {
        strips_.clear();
        for (int id : ids) {
            StripInfo s;
            s.id = id;
            const auto [iface, kind] = splitDescription(card_.inputDescription(e, id));
            s.interfaceName = iface;
            s.channelName = card_.channelName(e, id, true);
            if (s.channelName.isEmpty()) s.channelName = kind + " " + juce::String(card_.bankRelativeID(e, id) + 1);
            strips_.push_back(s);
        }
        changed = true;
    }
    if (buses != buses_) {
        const int previousBus = juce::isPositiveAndBelow(mix_, (int)buses_.size()) ? buses_[(size_t)mix_] : 0;
        buses_ = buses;
        busNames_.clear();
        mix_ = 0;
        for (size_t i = 0; i < buses_.size(); ++i) {
            const int bus = buses_[i];
            const auto [iface, kind] = splitDescription(card_.outputDescription(e, bus));
            const int n = card_.bankRelativeID(e, bus) + 1;
            busNames_.add(iface + ":" + kind + " " + juce::String(n) + "-" + juce::String(n + 1));
            if (bus == previousBus) mix_ = (int)i;
        }
        changed = true;
    }
    return changed;
}

bool ConsoleModel::poll() {
    motu::Exception e;
    motu::CueMix cue = card_.cueMix(e);
    if (!cue || buses_.empty()) return false;
    const int bus = buses_[(size_t)mix_];

    bool changed = false;
    for (auto& s : strips_) {
        StripState st;
        st.trim = cue.inputTrim(e, s.id);
        st.inputMute = cue.inputMute(e, s.id);
        st.volume = cue.volume(e, bus, s.id);
        st.pan = cue.pan(e, bus, s.id);
        st.mute = cue.mute(e, bus, s.id);
        st.solo = cue.solo(e, bus, s.id);
        if (st != s.state) { s.state = st; changed = true; }
    }

    const int vol = cue.busVolume(e, bus);
    const bool mute = cue.busMute(e, bus);
    const auto res = cue.resources(e);
    int faders = 0;
    for (int b : buses_) faders += juce::jmax(0, cue.busResourceUsage(e, b));
    if (vol != masterVolume_ || mute != masterMute_ || res.used != resources_.used || res.max != resources_.max
        || faders != cueMixFaders_) {
        masterVolume_ = vol;
        masterMute_ = mute;
        resources_ = res;
        cueMixFaders_ = faders;
        changed = true;
    }
    return changed;
}
