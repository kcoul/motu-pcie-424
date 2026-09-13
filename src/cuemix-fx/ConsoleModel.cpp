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

void ConsoleModel::showNotice(const juce::String& title, const juce::String& detail) {
    noticeTitle_ = title;
    noticeDetail_ = detail;
    noticeUntil_ = juce::Time::getMillisecondCounter() + 4000;
    if (onChange) onChange(false);
}

void ConsoleModel::refresh() {
    if (noticeUntil_ != 0 && juce::Time::getMillisecondCounter() > noticeUntil_) {
        noticeUntil_ = 0;
        noticeTitle_ = noticeDetail_ = {};
        if (onChange) onChange(false);
    }
    if (!card_) return;
    // Names can change from outside (PCI Audio Setup's editor): re-read each second.
    const bool names = (++ticks_ % 10 == 0) && refreshNames();
    const bool layout = syncLayout() || names;
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
            s.customName = card_.channelName(e, id, true);
            s.channelName = s.customName.isNotEmpty() ? s.customName
                                                      : kind + " " + juce::String(card_.bankRelativeID(e, id) + 1);
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

namespace {
using P = ConsoleModel::Param;
bool perMix(P p) {
    return p == P::Volume || p == P::Pan || p == P::Mute || p == P::Solo || p == P::BalWidth
        || p == P::MasterVolume || p == P::MasterMute;
}
bool perInput(P p) { return p == P::Trim || p == P::InputMute || p == P::Stereo || perMix(p); }
}  // namespace

bool ConsoleModel::refreshNames() {
    motu::Exception e;
    bool changed = false;
    for (auto& s : strips_) {
        const juce::String custom = card_.channelName(e, s.id, true);
        if (custom == s.customName) continue;
        s.customName = custom;
        if (custom.isNotEmpty()) s.channelName = custom;
        else {
            const juce::String desc = card_.inputDescription(e, s.id);
            s.channelName = desc.fromFirstOccurrenceOf(":", false, false) + " " + juce::String(card_.bankRelativeID(e, s.id) + 1);
        }
        changed = true;
    }
    if (changed) talkback_ = {};   // recompute source names on the next poll
    return changed;
}

void ConsoleModel::renameStrip(int strip, const juce::String& name) {
    if (!card_ || !juce::isPositiveAndBelow(strip, (int)strips_.size())) return;
    motu::Exception e;
    card_.setChannelName(e, strips_[(size_t)strip].id, true, name.trim().toStdString());
    if (!e.raised()) card_.commitChanges(e, true);
    if (e.raised()) { showNotice("Rename failed", juce::String(e.str())); return; }
    refreshNames();
    poll();
    if (onChange) onChange(true);
    showNotice(strips_[(size_t)strip].channelName, name.trim().isEmpty() ? "Hardware name restored" : "Renamed");
}

int ConsoleModel::local(Param p, int bus, int id, int cardValue) const {
    const auto it = overrides_.find({ perMix(p) ? bus : -1, id, (int)p });
    return it != overrides_.end() ? it->second : cardValue;
}

int ConsoleModel::scopeSource(int side) const {
    const int fallback = juce::jmin(side, juce::jmax(0, (int)strips_.size() - 1));
    return local(side == 0 ? Param::ScopeLeft : Param::ScopeRight, -1, -1, fallback);
}

void ConsoleModel::clearLocalChanges() {
    overrides_.clear();
    poll();
    if (onChange) onChange(false);
}

void ConsoleModel::setLocal(Param p, int value, int strip) {
    const bool isStrip = perInput(p) && p != Param::MasterVolume && p != Param::MasterMute;
    if (isStrip && !juce::isPositiveAndBelow(strip, (int)strips_.size())) return;
    const int id = isStrip ? strips_[(size_t)strip].id : -1;
    overrides_[{ perMix(p) ? currentBus() : -1, id, (int)p }] = value;
    if (card_) poll();

    // Say what moved, and that it went nowhere.
    auto onOff = [](int v) { return juce::String(v ? "on" : "off"); };
    juce::String title = isStrip ? strips_[(size_t)strip].channelName : juce::String(), what;
    switch (p) {
        case Param::Trim:         what = "Trim " + juce::String(value - 64) + " dB"; break;
        case Param::InputMute:    what = "Input mute " + onOff(value); break;
        case Param::Stereo:       what = value ? "Stereo" : "Mono"; break;
        case Param::Volume:       what = "Fader " + volumeText(value); break;
        case Param::Pan:          what = "Pan " + panText(value); break;
        case Param::Mute:         what = "Mute " + onOff(value); break;
        case Param::Solo:         what = "Solo " + onOff(value); break;
        case Param::BalWidth:     what = value ? "Pan knob: width" : "Pan knob: balance"; break;
        case Param::MasterVolume: title = mixName(mix_); what = "Master " + volumeText(value); break;
        case Param::MasterMute:   title = mixName(mix_); what = "Master mute " + onOff(value); break;
        case Param::TalkInput:    title = "Talkback"; what = "Input " + talkback_.talkName; break;
        case Param::ListenInput:  title = "Listenback"; what = "Input " + talkback_.listenName; break;
        case Param::TalkDim:      title = "Monitor Dim"; what = "Talkback dim " + juce::String(value); break;
        case Param::ListenDim:    title = "Monitor Dim"; what = "Listenback dim " + juce::String(value); break;
        case Param::Talk:         title = "Talkback"; what = onOff(value); break;
        case Param::Listen:       title = "Listenback"; what = onOff(value); break;
        case Param::Link:         title = "Talkback"; what = "Link " + onOff(value); break;
        case Param::ScopeLeft:
        case Param::ScopeRight:
            title = "Scope " + juce::String(p == Param::ScopeLeft ? "Left" : "Right");
            what = juce::isPositiveAndBelow(value, (int)strips_.size()) ? strips_[(size_t)value].channelName : juce::String();
            break;
    }
    showNotice(title, what + "  (not sent)");
}

bool ConsoleModel::poll() {
    motu::Exception e;
    motu::CueMix cue = card_.cueMix(e);
    if (!cue || buses_.empty()) return false;
    const int bus = buses_[(size_t)mix_];

    bool changed = false;
    for (auto& s : strips_) {
        StripState st;
        st.trim      = local(Param::Trim, bus, s.id, cue.inputTrim(e, s.id));
        st.inputMute = local(Param::InputMute, bus, s.id, cue.inputMute(e, s.id));
        st.stereo    = local(Param::Stereo, bus, s.id, 0);
        st.volume    = local(Param::Volume, bus, s.id, cue.volume(e, bus, s.id));
        st.pan       = local(Param::Pan, bus, s.id, cue.pan(e, bus, s.id));
        st.mute      = local(Param::Mute, bus, s.id, cue.mute(e, bus, s.id));
        st.solo      = local(Param::Solo, bus, s.id, cue.solo(e, bus, s.id));
        st.balWidth  = local(Param::BalWidth, bus, s.id, 0);
        if (st != s.state) { s.state = st; changed = true; }
    }

    const int vol = local(Param::MasterVolume, bus, -1, cue.busVolume(e, bus));
    const bool mute = local(Param::MasterMute, bus, -1, cue.busMute(e, bus)) != 0;
    const auto res = cue.resources(e);
    int faders = 0;
    for (int b : buses_) faders += juce::jmax(0, cue.busResourceUsage(e, b));
    Talkback tb;
    if (motu::Talkback api = card_.talkback(e)) {
        tb.talkInput = api.talkbackInput(e);
        tb.listenInput = api.listenbackInput(e);
        tb.talkDim = api.talkbackDimLevel(e);
        tb.listenDim = api.listenbackDimLevel(e);
        tb.talk = api.talkbackEnable(e) != 0;
        tb.listen = api.listenbackEnable(e) != 0;
        tb.link = api.talkbackLink(e) != 0;
    }
    tb.talkInput   = local(Param::TalkInput, -1, -1, tb.talkInput);
    tb.listenInput = local(Param::ListenInput, -1, -1, tb.listenInput);
    tb.talkDim     = local(Param::TalkDim, -1, -1, tb.talkDim);
    tb.listenDim   = local(Param::ListenDim, -1, -1, tb.listenDim);
    tb.talk        = local(Param::Talk, -1, -1, tb.talk) != 0;
    tb.listen      = local(Param::Listen, -1, -1, tb.listen) != 0;
    tb.link        = local(Param::Link, -1, -1, tb.link) != 0;
    if (tb != talkback_) {
        // Name the sources the way the strips do; anything else is Disabled.
        auto nameOf = [this](int id) -> juce::String {
            for (const auto& st : strips_)
                if (st.id == id) return st.channelName;
            return "Disabled";
        };
        tb.talkName = nameOf(tb.talkInput);
        tb.listenName = nameOf(tb.listenInput);
        talkback_ = tb;
        changed = true;
    }

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
