#include "ConsoleModel.h"

#include <cmath>

namespace {
constexpr int kPollHz = 10;
// Control state at 10 Hz is fine; a meter at 10 Hz looks broken.
constexpr int kMeterHz = 30;

// "HD192:Analog-A" -> { "HD192", "Analog-A" }
std::pair<juce::String, juce::String> splitDescription(const juce::String& d) {
    const int colon = d.indexOfChar(':');
    if (colon < 0) return { {}, d };
    return { d.substring(0, colon), d.substring(colon + 1) };
}
}  // namespace

// MOTU's own laws, recovered from CueMix FX's binary. See docs/CUEMIX-API.md.
//
// The fader coefficient is 40, not 20: DigiVolToDecibelString does
// 40 * log10(raw / 32768), which gives the fader an 84 dB range over its 129
// quantized steps instead of 42 dB.
double volumeDb(int raw) { return raw <= 0 ? -1000.0 : 40.0 * std::log10(raw / 32768.0); }

// Formatting is MOTU's too: "-inf" at or below -90 dB, "0.0" inside +/-0.1 dB,
// one decimal below 40 dB and none above it, and the caller's " dB" suffix
// (ValueLegacyFader::GetUIStringWithHardwareValue).
juce::String volumeText(int raw) {
    const double db = volumeDb(raw);
    if (db <= -90.0) return "-inf dB";
    if (std::abs(db) < 0.1) return "0.0 dB";
    return (std::abs(db) < 40.0 ? juce::String(db, 1) : juce::String(juce::roundToInt(db)))
         + " dB";
}

// The fader writes only multiples of 256, capped at 0x8000: MOTU's
// ValueLegacyFader::ConvertFromUIControlValue masks the value with 0xFF00.
int quantizeVolume(int raw) {
    return juce::jlimit(0, 32768, raw) >= 32768 ? 32768 : (juce::jlimit(0, 32768, raw) & 0xFF00);
}

// Pan and trim read as a signed offset with an explicit '+' when positive
// (ValuePanLegacy / ValueLegacyTrim::GetUIStringWithHardwareValue). Pan is
// centred on 64; trim is *not* offset -- its hardware value is the number
// shown, over a per-channel range we have not read yet.
static juce::String signedText(int n) {
    return (n > 0 ? "+" : "") + juce::String(n);
}

juce::String panText(int raw) { return signedText(raw - 64); }
juce::String trimText(int raw) { return signedText(raw); }

ConsoleModel::ConsoleModel() {
    dev_ = motu::Card::findDevice();
    std::string err;
    if (dev_) card_ = motu::Card::open(dev_, &err);
    else err = "No PCI-424 was found. Is MOTUPCIAudio.kext loaded?";
    error_ = juce::String(err);
    refresh();
    startTimerHz(kPollHz);
    meterTimer_.startTimerHz(kMeterHz);
}

ConsoleModel::~ConsoleModel() { stopTimer(); meterTimer_.stopTimer(); }

// --- Level meters ----------------------------------------------------------
//
// MOTU's ballistics, from LevelMeterView::SetValue(float, unsigned):
//
//   * the bar follows the incoming value immediately -- there is no attack or
//     release on it, because each read already reports the peak since the last
//     one;
//   * a new value at or above the held peak replaces it and restarts the hold
//     at now + peakHoldSeconds;
//   * once the hold has expired (and is not "Infinite"), the peak falls by
//     kPeakDecayPerTick per update, but only after the bar itself has dropped
//     below kPeakDecayFloor.
//
// The two constants are MOTU's (0.015 and 0.005). They are per *update*, and
// MOTU's own update rate is not in the binary, so the visible decay speed is
// the one thing to calibrate by eye against MOTU's console.
static constexpr float kPeakDecayPerTick = 0.015f;
static constexpr float kPeakDecayFloor   = 0.005f;

void ConsoleModel::applyBallistics(MeterState& m, float level, double now) {
    m.level = juce::jlimit(0.0f, 1.0f, level);

    const size_t i = (size_t)(&m - meters_.data());
    if (i >= peakHoldUntil_.size()) peakHoldUntil_.resize(meters_.size(), 0.0);

    if (m.level >= m.peak) {
        m.peak = m.level;
        peakHoldUntil_[i] = now + (peakHoldSeconds_ < 0.0 ? 1.0e9 : peakHoldSeconds_);
    } else if (peakHoldSeconds_ >= 0.0 && now > peakHoldUntil_[i]
               && m.level <= kPeakDecayFloor) {
        m.peak = juce::jmax(0.0f, m.peak - kPeakDecayPerTick);
    }
}

void ConsoleModel::clearPeaks() {
    for (auto& m : meters_) m.peak = 0.0f;
    std::fill(peakHoldUntil_.begin(), peakHoldUntil_.end(), 0.0);
    if (onMeters) onMeters();
}

// With no card, drive the meters from a test signal so the widget can be built
// and reviewed away from the studio: a slow sweep per strip at different
// phases, with an occasional deliberate clip.
void ConsoleModel::synthesizeMeters(double now) {
    for (size_t i = 0; i < meters_.size(); ++i) {
        const double phase = now * 0.6 + (double)i * 0.45;
        const double env = 0.5 - 0.5 * std::cos(phase);          // 0..1
        const double wobble = 0.85 + 0.15 * std::sin(now * 7.0 + (double)i);
        float level = (float)juce::jlimit(0.0, 1.0, env * wobble);
        applyBallistics(meters_[i], level, now);
        // Clip the loudest strips briefly, so the indicator is exercised.
        meters_[i].clipRaw = level > 0.98f ? 1 : 0;
    }
}

void ConsoleModel::pollMeters() {
    const size_t n = strips_.size();
    if (meters_.size() != n) {
        meters_.assign(n, MeterState{});
        peakHoldUntil_.assign(n, 0.0);
    }
    if (n == 0) return;

    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;

    if (!card_) {
        synthesizeMeters(now);
        if (onMeters) onMeters();
        return;
    }

    motu::Exception e;
    motu::CueMix cue = card_.cueMix(e);
    if (!cue || buses_.empty()) return;

    // MOTU meters only the strips on screen, capped at the card's maximum, and
    // passes the raw bus id (docs/CUEMIX-API.md).
    int maxMeters = card_.maxLevelMeters(e);
    if (e.raised() || maxMeters <= 0) maxMeters = motu::CueMix::kMaxMeters;
    const int count = juce::jmin((int)n, maxMeters, (int)motu::CueMix::kMaxMeters);

    motu::CueMix::LevelMeterRequest req;
    req.bus = (std::uint32_t)buses_[(size_t)mix_];
    req.numChannels = (std::uint32_t)count;
    for (int i = 0; i < count; ++i) req.channels[i] = (std::uint32_t)strips_[(size_t)i].id;

    motu::CueMix::LevelMeterResults res;
    cue.readLevelMeters(e, req, &res);
    if (e.raised()) return;

    for (int i = 0; i < count; ++i) {
        applyBallistics(meters_[(size_t)i], (float)res.level[i] / 32768.0f, now);
        meters_[(size_t)i].clipRaw = res.clip[i];
    }
    if (onMeters) onMeters();
}

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
    if (!card_) {
        if (syncDemoLayout() && onChange) onChange(true);
        return;
    }
    // Names can change from outside (PCI Audio Setup's editor): re-read each second.
    const bool names = (++ticks_ % 10 == 0) && refreshNames();
    const bool layout = syncLayout() || names;
    const bool values = poll();
    if ((layout || values) && onChange) onChange(layout);
}

// With no card there is nothing to enumerate, so build a stand-in layout: the
// console then draws, and the meters run off the test signal. It exists so the
// skins can be worked on and reviewed away from the studio, and it is never
// used when a card is present -- `connected()` stays false and the LCD says so.
bool ConsoleModel::syncDemoLayout() {
    if (!strips_.empty()) return false;
    static const char* const kDemo[][2] = {
        { "HD192",   "Analog 1" }, { "HD192",   "Analog 2" },
        { "HD192",   "Analog 3" }, { "HD192",   "Analog 4" },
        { "24I/O-2", "Analog 1" }, { "24I/O-2", "Analog 2" },
        { "24I/O-2", "Analog 3" }, { "24I/O-2", "Analog 4" },
        { "2408mk3", "AES 1" },    { "2408mk3", "AES 2" },
        { "2408mk3", "ADAT 1" },   { "2408mk3", "ADAT 2" },
    };
    for (int i = 0; i < (int)(sizeof kDemo / sizeof kDemo[0]); ++i) {
        StripInfo s;
        s.id = i;
        s.interfaceName = kDemo[i][0];
        s.channelName = kDemo[i][1];
        s.state.volume = 24320 + (i % 5) * 1024;    // on the 256 grid, as the card would be
        s.state.pan = 64 + (i % 3 - 1) * 24;
        strips_.push_back(s);
    }
    buses_.clear();
    for (int b = 0; b < 12; ++b) buses_.push_back(b * 2);   // bus id = mix * 2
    busNames_.clear();
    for (int b = 0; b < 12; ++b) busNames_.add("Analog " + juce::String(b * 2 + 1) + "-" + juce::String(b * 2 + 2));
    return true;
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

void ConsoleModel::setWritesEnabled(bool on) {
    writes_ = on;
    showNotice("Send Changes to Card", on ? "on  (writes are unverified)" : "off");
}

// Each case is one line of docs/CUEMIX-API.md. `bus` is a raw bus id -- the
// even output-pair id MOTU's wrapper produces by doubling a 0..47 mix index --
// which is what buses_ already holds.
//
// The fader is quantized on the way out because MOTU's is: the console can only
// produce multiples of 256, so sending anything else would put the card in a
// state MOTU's own app could never reach.
bool ConsoleModel::writeToCard(Param p, int value, int id) {
    if (!writes_ || !card_ || buses_.empty()) return false;
    motu::Exception e;
    motu::CueMix cue = card_.cueMix(e);
    if (!cue) return false;
    const int bus = buses_[(size_t)mix_];

    switch (p) {
        case Param::Trim:         cue.setInputTrim(e, id, value); break;
        case Param::InputMute:    cue.setInputMute(e, id, value != 0); break;
        case Param::Volume:       cue.setVolume(e, bus, id, quantizeVolume(value)); break;
        case Param::Pan:          cue.setPan(e, bus, id, value); break;
        case Param::Mute:         cue.setMute(e, bus, id, value != 0); break;
        case Param::Solo:         cue.setSolo(e, bus, id, value != 0); break;
        case Param::MasterVolume: cue.setBusVolume(e, bus, quantizeVolume(value)); break;
        case Param::MasterMute:   cue.setBusMute(e, bus, value != 0); break;
        // Stereo needs CommitChanges and the pair-mirroring MOTU does in
        // SetInputValueByGlobalID; balance/width, talkback and the scope
        // selectors are left for their own stages.
        default: return false;
    }
    if (e.raised()) {
        showNotice("Card refused the write", e.str());
        return false;
    }
    return true;
}

void ConsoleModel::setLocal(Param p, int value, int strip) {
    const bool isStrip = perInput(p) && p != Param::MasterVolume && p != Param::MasterMute;
    if (isStrip && !juce::isPositiveAndBelow(strip, (int)strips_.size())) return;
    const int id = isStrip ? strips_[(size_t)strip].id : -1;
    const auto key = std::tuple<int, int, int>{ perMix(p) ? currentBus() : -1, id, (int)p };
    overrides_[key] = value;

    // A successful write drops the local override, so the value shown from then
    // on is the card's own -- if the card did not take it, the control snaps
    // back rather than lying.
    const bool sent = writeToCard(p, value, id);
    if (sent) overrides_.erase(key);
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
    showNotice(title, what + (sent ? "" : "  (not sent)"));
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
