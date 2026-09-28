#pragma once

#include "s3g_macro_shred.h"
#include "s3g_break_bus.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace s3g::sample {

// Eight post-playback processors. Effect compatibility was explicitly retired;
// older project sources/playback survive, but their effects load dry/default.
constexpr unsigned kNeonCharacterControls = 12;
using NeonCharacterValues = std::array<float, kNeonCharacterControls>;
struct NeonCharacterControl {
    const char* label = "";
    float initial = 0, minimum = 0, maximum = 100;
    bool logarithmic = false;
    const char* choices = "";
    const char* unit = "%";
};
inline constexpr const char* kNeonCharacterNames[] {"Filter", "Echo", "Space", "Shift", "Vowel", "Punch", "Drive", "Crush"};
inline constexpr unsigned kNeonCharacterCounts[] {5, 9, 5, 5, 4, 4, 8, 6};
// Normalized storage, real-unit display. Index 11 is a common output trim.
inline constexpr NeonCharacterControl kNeonCharacterDefs[8][kNeonCharacterControls] {
    {{"TYPE",0,0,3,false,"LOW PASS|HIGH PASS|BAND PASS|DJ",""},
     {"SWEEP",.72f,0,100}, {"RESONANCE",.12f,0,100}, {"ENV DEPTH",.5f,-100,100}, {"ENV RELEASE",.4f,10,1000,true,""," ms"},
     {},{},{},{},{},{},{"OUT",.75f,-36,12,false,""," dB"}},
    {{"MODE",0,0,3,false,"DIGITAL|TAPE|MULTI TAP|REVERSE",""},
     {"TIME",.638f,5,2000,true,""," ms"}, {"FEEDBACK",.35f,0,95}, {"DAMPING",.35f,0,100},
     {"DIFFUSION",0,0,100}, {"DUCK",0,0,100}, {"GLIDE",.45f,5,1000,true,""," ms"},
     {"CLOCK",0,0,1,false,"FREE|BEATS",""}, {"DIVISION",.625f,0,8,false,"1/32|1/16|1/8 T|1/8|1/8 D|1/4|1/4 D|1/2|1 BAR",""},
     {},{},{"OUT",.75f,-36,12,false,""," dB"}},
    {{"MODE",.5f,0,2,false,"ROOM|PLATE|DIFFUSE",""}, {"SIZE",.45f,0,100},
     {"DECAY",.45f,.15f,15,true,""," s"}, {"PRE DELAY",.1f,0,150,false,""," ms"}, {"DAMPING",.4f,0,100},
     {},{},{},{},{},{},{"OUT",.75f,-36,12,false,""," dB"}},
    {{"MODE",1,0,2,false,"DETUNE|OCTAVE|DUAL",""}, {"PITCH",.75f,-24,24,false,""," st"},
     {"DETUNE",.12f,0,100,false,""," ct"}, {"WINDOW",.43f,20,180,true,""," ms"}, {"BALANCE",.5f,0,100},
     {},{},{},{},{},{},{"OUT",.75f,-36,12,false,""," dB"}},
    {{"VOWEL",0,0,4,false,"",""}, {"THROAT",.5f,-12,12,false,""," st"},
     {"RESONANCE",.4f,0,100}, {"ENV DEPTH",.5f,-100,100}, {},{},{},{},{},{},{},{"OUT",.75f,-36,12,false,""," dB"}},
    {{"PRESS",.42f,0,100}, {"SNAP",.59f,-100,100}, {"RECOVERY",.34f,0,100}, {"BODY",.5f,-100,100},
     {},{},{},{},{},{},{},{"OUT",.75f,-36,12,false,""," dB"}},
    {{"CIRCUIT",0,0,7,false,"SHRED|WOOL|RAT|ZONE A|ZONE B|FUZZ I|FUZZ II|DIODE",""},
     {"DRIVE",.28f,0,100}, {"SHRED",.18f,0,100}, {"FEEDBACK",.12f,0,100},
     {"COLOR",.55f,0,100}, {"REACT",.25f,0,100}, {"TUNE",.65f,0,100}, {"BODY",.65f,0,100},
     {},{},{},{"OUT",.75f,-36,12,false,""," dB"}},
    {{"BITS",.4f,4,24,false,""," bit"}, {"RATE",3.f/7.f,1,128,true,""," x"},
     {"JITTER",0,0,100}, {"DRIVE",0,0,100}, {"TONE",1,100,20000,true,""," Hz"},
     {"FILTER",0,0,1,false,"OFF|LOW PASS",""}, {},{},{},{},{},{"OUT",.75f,-36,12,false,""," dB"}}
};
inline NeonCharacterValues neonCharacterDefaults(unsigned effect) noexcept {
    NeonCharacterValues result {};
    for (unsigned n = 0; n < result.size(); ++n) result[n] = kNeonCharacterDefs[effect % 8][n].initial;
    return result;
}
inline double neonCharacterDisplay(unsigned effect, unsigned n, double unit) noexcept {
    const auto& d = kNeonCharacterDefs[effect % 8][n];
    unit = std::clamp(unit, 0.0, 1.0);
    const double value = d.logarithmic ? d.minimum * std::pow(d.maximum / d.minimum, unit)
        : d.minimum + unit * (d.maximum - d.minimum);
    return *d.choices || (effect % 8 == 7 && n == 0) ? std::round(value) : value;
}
inline double neonCharacterNormalized(unsigned effect, unsigned n, double value) noexcept {
    const auto& d = kNeonCharacterDefs[effect % 8][n];
    value = std::clamp(value, double(d.minimum), double(d.maximum));
    return d.logarithmic ? std::log(value / d.minimum) / std::log(d.maximum / d.minimum)
        : (value - d.minimum) / (d.maximum - d.minimum);
}
// Named pairs deliberately put useful continuous gestures first.
inline constexpr unsigned kNeonCharacterPairs[8][5][2] {
    {{1,2},{3,4},{0,12},{11,12},{11,12}},
    {{1,2},{3,4},{5,6},{7,8},{11,12}},
    {{1,2},{3,4},{0,12},{11,12},{11,12}},
    {{1,2},{3,4},{0,12},{11,12},{11,12}},
    {{0,1},{2,3},{11,12},{11,12},{11,12}},
    {{0,1},{2,3},{11,12},{11,12},{11,12}},
    {{1,4},{2,3},{5,7},{0,6},{11,12}},
    {{0,1},{2,3},{4,5},{11,12},{11,12}}
};
inline constexpr unsigned kNeonCharacterPairCounts[] {4,5,4,4,3,3,5,4};
inline unsigned neonCharacterKnob(unsigned effect, unsigned pair, bool loop, bool beatEcho = false) noexcept {
    effect %= 8;
    const unsigned n = kNeonCharacterPairs[effect][pair % kNeonCharacterPairCounts[effect]][loop ? 0 : 1];
    return effect == 1 && n == 1 && beatEcho ? 8 : n;
}
inline bool neonCharacterControlAllowed(unsigned effect, unsigned n, unsigned choice, bool ambisonic) noexcept {
    return !(ambisonic && (effect % 8 >= 6 || (effect % 8 == 1 && n == 0 && choice == 1)));
}
inline double neonCharacterEchoSeconds(const NeonCharacterValues& values,double tempo) noexcept {
    static constexpr double beats[] {.125,.25,1.0/3,.5,.75,1,1.5,2,4};
    const double seconds = values[7]>=.5 ? beats[static_cast<unsigned>(std::round(std::clamp(values[8],0.f,1.f)*8))]*60/std::clamp(tempo,20.0,400.0)
        : neonCharacterDisplay(1,1,values[1])*.001;
    return std::min(2.0,seconds);
}

// Shared clocks and coefficients, discrete channel memories. No implicit stereo
// widening, channel exchange, decode/encode, or nonlinear Ambisonic limiting.
class NeonCharacterProcessor {
public:
    bool prepare(double sampleRate) {
        if (!std::isfinite(sampleRate) || sampleRate <= 0 || sampleRate > 768000) return false;
        rate_ = sampleRate; capacity_ = std::max(32u, static_cast<uint32_t>(std::ceil(rate_ * 2.1)) + 8);
        try {
            memory_.resize(static_cast<size_t>(capacity_) * 16);
            for (auto& drive : drive_) drive.prepare(rate_);
        } catch (...) { return false; }
        punch_.prepare(rate_); reset(); return true;
    }
    void unprepare() { std::vector<float>().swap(memory_); for (auto& drive : drive_) drive = MacroShredCore{}; capacity_ = 0; }
    void reset() noexcept {
        write_ = written_ = 0; spaceClock_ = 0; last_ = 99; phase_ = phaseB_ = .5; reversePhase_ = 0;
        holdClock_ = envelope_ = wet_ = 0; delayTime_ = .26 * rate_;
        low_ = {}; held_ = {}; filter_ = {}; vowel_ = {}; tankLow_ = {};
        smoothed_ = {}; ready_ = false; random_ = 0x73a951u; punch_.reset();
        for (auto& drive : drive_) drive.reset();
    }
    void process(float* const* audio, unsigned channels, uint32_t frames, unsigned effect,
        const NeonCharacterValues& values, float amount, bool ambisonic, double tempo = 120) noexcept {
        if (!capacity_ || effect >= 8 || (ambisonic && effect >= 6)) { reset(); return; }
        if (last_ != effect) { reset(); last_ = effect; }
        channels = std::min(channels, 16u);
        if (!channels) return;
        NeonCharacterValues target;
        for (unsigned n = 0; n < target.size(); ++n) target[n] = std::isfinite(values[n]) ? std::clamp(values[n],0.f,1.f) : kNeonCharacterDefs[effect][n].initial;
        const bool first = !ready_;
        if (first) { smoothed_ = target; ready_ = true; }
        const auto v = [&](unsigned n) { return neonCharacterDisplay(effect,n,smoothed_[n]); };
        const double smooth = 1 - std::exp(-1 / (rate_ * .005));
        if (amount <= 0 && wet_ < 1e-8) {
            const float gain = static_cast<float>(std::pow(10.0,neonCharacterDisplay(effect,11,target[11])/20));
            for (unsigned ch = 0; ch < channels; ++ch)
                for (uint32_t n = 0; n < frames; ++n) audio[ch][n] *= gain;
            written_ = 0; return;
        }
        const unsigned mode = static_cast<unsigned>(neonCharacterDisplay(effect,0,target[0]));
        if (effect == 6) {
            MacroShredCoreParams p;
            p.circuit = static_cast<MacroShredCircuit>(mode); p.pressure = target[1]; p.shred = target[2];
            p.feedback = target[3]; p.color = target[4]; p.react = target[5]; p.tune = target[6];
            p.body = target[7]; p.mix = 1; p.outputGainDb = 0;
            for (unsigned ch = 0; ch < channels; ++ch) { drive_[ch].setParams(p); if(first) drive_[ch].reset(); }
        } else if (effect == 5) {
            BreakBusParams p;
            p.press = target[0]; p.snap = target[1] * 2 - 1; p.recovery = target[2];
            p.tilt = target[3] * 2 - 1; p.saturation = p.bite = p.clip = 0;
            p.fieldSafe = true; p.linkMode = BreakBusLinkMode::All;
            punch_.setParams(p);
        }
        // Changes use the same five-millisecond smoothing as the wet gesture.
        for (uint32_t frame = 0; frame < frames; ++frame) {
            for (unsigned n = 0; n < target.size(); ++n) smoothed_[n] += static_cast<float>((target[n] - smoothed_[n]) * smooth);
            wet_ += (std::clamp(std::isfinite(amount) ? amount : 0.f,0.f,1.f) - wet_) * smooth;
            double peak = 0;
            for (unsigned ch = 0; ch < channels; ++ch) peak = std::max(peak, std::abs(double(audio[ch][frame])));
            const double release = effect == 0 ? v(4)*.001 : .12;
            envelope_ += (peak - envelope_) * (1 - std::exp(-1 / (rate_ * (peak > envelope_ ? .002 : release))));
            const double env = std::clamp(envelope_*2,0.0,1.0);
            const double gain = std::pow(10.0, v(11) / 20);
            double cutoff = 1000, resonance = 1;
            if (effect == 0) {
                const double position = std::clamp(double(smoothed_[1]) + (smoothed_[3]*2-1)*env*.5,0.0,1.0);
                cutoff = mode == 3 ? position < .5 ? 20*std::pow(1000.0,position*2)
                    : 20*std::pow(1000.0,(position-.5)*2) : 20*std::pow(1000.0,position);
                resonance = .65 + smoothed_[2]*smoothed_[2]*8;
                filterPosition_ = position;
            }
            double delay = 1;
            if (effect == 1) {
                const double seconds = neonCharacterEchoSeconds(smoothed_,tempo);
                const double desired = std::clamp(seconds*rate_,8.0,double(capacity_-4));
                delayTime_ += (desired-delayTime_) * (1-std::exp(-1/(rate_*v(6)*.001)));
                delay = delayTime_;
                reversePhase_ += 1 / delay; reversePhase_ -= std::floor(reversePhase_);
            }
            if (effect == 3) {
                double semis = v(1), detune = v(2)*.01;
                if (mode == 1) semis = std::round(semis/12)*12;
                if (mode == 0) semis = 0;
                shiftWindow_ = v(3)*.001*rate_;
                phase_ += (1-std::pow(2.0,(semis+detune)/12))/shiftWindow_; phase_ -= std::floor(phase_);
                phaseB_ += (1-std::pow(2.0,(mode == 0 ? -detune : -semis-detune)/12))/shiftWindow_; phaseB_ -= std::floor(phaseB_);
            }
            const bool capture = holdClock_ <= 0;
            if (effect == 7 && capture) {
                random_ ^= random_ << 13; random_ ^= random_ >> 17; random_ ^= random_ << 5;
                holdClock_ += std::max(1.0, v(1)*(1+smoothed_[2]*.45*(double(random_&65535)/32767.5-1)));
            }
            std::array<float,16> punchFrame {};
            if (effect == 5) {
                for (unsigned ch = 0; ch < channels; ++ch) punchFrame[ch] = audio[ch][frame];
                punch_.processFrame(punchFrame.data(),channels);
            }
            // Expensive coefficient work is shared by every component. In
            // particular, a 16-channel field does not calculate 16 identical
            // sets of formants, delay feedback curves, or filter poles.
            const auto filterCoefficients = effect == 0 ? coefficients(cutoff,resonance) : FilterCoefficients{};
            std::array<FilterCoefficients,3> formantCoefficients {};
            if (effect == 4) {
                static constexpr double formants[5][3] {{800,1150,2900},{500,1700,2500},{300,2200,3000},{500,900,2400},{350,650,2200}};
                const double morph = std::clamp(double(smoothed_[0])*4+(smoothed_[3]*2-1)*env*2,0.0,4.0);
                const unsigned firstFormant = std::min(3u,static_cast<unsigned>(morph));
                const double fraction = morph-firstFormant, transpose = std::pow(2.0,v(1)/12);
                for (unsigned n=0;n<3;++n)
                    formantCoefficients[n] = coefficients((formants[firstFormant][n]*(1-fraction)+formants[firstFormant+1][n]*fraction)*transpose,2+smoothed_[2]*10);
            }
            const double dampingPole = effect == 1 ? pole(120*std::pow(150.0,1-smoothed_[3]))
                : effect == 7 ? pole(v(4)) : 0;
            const double bits = effect == 7 ? v(0) : 24;
            const double steps = effect == 7 && capture ? std::pow(2.0,bits-1) : 1;
            const double crushDrive = 1+smoothed_[3]*19;
            const double crushScale = effect == 7 && smoothed_[3] > .000001 ? 1/std::tanh(crushDrive) : 1;
            SpacePlan spacePlan;
            if (effect == 2) spacePlan = planSpace(mode,smoothed_[1],v(2),v(3)*.001,smoothed_[4]);
            for (unsigned ch = 0; ch < channels; ++ch) {
                const float dry = std::isfinite(audio[ch][frame]) ? audio[ch][frame] : 0;
                double processed = dry, stored = dry;
                if (effect == 0) {
                    const auto bands = svf(dry,filterCoefficients,filter_[ch]);
                    processed = mode == 0 ? bands[0] : mode == 1 ? bands[1] : mode == 2 ? bands[2]
                        : filterPosition_ < .5 ? bands[0] : bands[1];
                    if (mode == 3) { const double depth = std::min(1.0,std::abs(filterPosition_-.5)*40); processed = dry+(processed-dry)*depth; }
                } else if (effect == 1) {
                    processed = read(ch,delay);
                    if (mode == 2) processed = .5*processed+.3*read(ch,delay*.75)+.2*read(ch,delay*.5);
                    if (mode == 3) {
                        const double q = std::fmod(reversePhase_+.5,1.0), a = std::sin(pi*reversePhase_);
                        processed = a*a*read(ch,1+2*reversePhase_*delay*.5)+(1-a*a)*read(ch,1+2*q*delay*.5);
                    }
                    const double diffuse = .55*processed+.3*read(ch,delay*.613)+.15*read(ch,delay*.379);
                    processed += (diffuse-processed)*smoothed_[4];
                    low_[ch] += dampingPole*(processed-low_[ch]);
                    double feedback = low_[ch];
                    if (mode == 1 && !ambisonic) feedback = std::tanh(feedback*1.4)/1.4;
                    stored = dry + feedback * smoothed_[2]*.95;
                    processed *= 1/(1+env*smoothed_[5]*8);
                } else if (effect == 2) {
                    processed = space(ch,dry,spacePlan);
                } else if (effect == 3) {
                    memory(ch)[write_] = dry;
                    const double a = shifted(ch,phase_), b = shifted(ch,phaseB_);
                    processed = mode == 1 ? a : a*(1-smoothed_[4])+b*smoothed_[4];
                } else if (effect == 4) {
                    processed = 0;
                    for (unsigned n = 0; n < 3; ++n)
                        processed += svf(dry,formantCoefficients[n],vowel_[ch][n])[2]*(n==0?1.5:n==1?1:.5);
                } else if (effect == 5) processed = punchFrame[ch];
                else if (effect == 6) processed = drive_[ch].processSample(dry);
                else {
                    if (capture) held_[ch] = bits >= 24 ? dry : std::round(std::clamp(double(dry),-1.0,1.0)*steps)/steps;
                    processed = held_[ch];
                    if (smoothed_[3] > .000001) processed = std::tanh(processed*crushDrive)*crushScale;
                    low_[ch] += dampingPole*(processed-low_[ch]);
                    if (target[5] >= .5) processed = low_[ch];
                }
                if (effect != 2) memory(ch)[write_] = static_cast<float>(std::isfinite(stored) ? stored : 0);
                const double out = (dry+(processed-dry)*wet_)*gain;
                audio[ch][frame] = static_cast<float>(std::isfinite(out) ? out : 0);
            }
            write_ = (write_+1)%capacity_; ++spaceClock_; written_ = std::min(written_+1,capacity_); holdClock_ -= 1;
        }
    }
private:
    static constexpr double pi = 3.14159265358979323846;
    struct FilterState { double low = 0, band = 0; };
    struct FilterCoefficients { double g=0,k=1,a=1; };
    FilterCoefficients coefficients(double hz,double q) const noexcept {
        const double g = std::tan(pi*std::clamp(hz,std::min(10.0,rate_*.01),rate_*.42)/rate_), k = 1/q;
        return {g,k,1/(1+g*(g+k))};
    }
    static std::array<double,3> svf(double input,FilterCoefficients c,FilterState& state) noexcept {
        const auto [g,k,a] = c;
        const double bp = a*state.band+g*a*(input-state.low);
        const double lp = state.low+g*a*state.band+g*g*a*(input-state.low);
        state.band = 2*bp-state.band; state.low = 2*lp-state.low;
        return {lp,input-k*bp-lp,bp*k};
    }
    double pole(double hz) const noexcept { return 1-std::exp(-2*pi*std::min(hz,rate_*.42)/rate_); }
    float* memory(unsigned ch) noexcept { return memory_.data()+static_cast<size_t>(ch)*capacity_; }
    double read(unsigned ch,double delay) noexcept {
        delay = std::clamp(delay,1.0,double(capacity_-2)); const auto n = static_cast<uint32_t>(delay);
        if (written_ <= n+1) return 0;
        const auto at = (write_+capacity_-n)%capacity_, before = (at+capacity_-1)%capacity_;
        return memory(ch)[at]+(memory(ch)[before]-memory(ch)[at])*(delay-n);
    }
    double shifted(unsigned ch,double phase) noexcept {
        const double q = std::fmod(phase+.5,1.0), weight = .5-.5*std::cos(2*pi*phase);
        return weight*read(ch,2+phase*shiftWindow_)+(1-weight)*read(ch,2+q*shiftWindow_);
    }
    struct SpaceTap { uint32_t at=0,before=0; double fraction=0,feedback=0; bool valid=false; };
    struct SpacePlan {
        uint32_t stride=0,at=0,preBase=0,preWrite=0;
        double damping=0;
        SpaceTap pre;
        std::array<SpaceTap,6> taps {};
    };
    SpacePlan planSpace(unsigned mode,double size,double decay,double pre,double damping) const noexcept {
        SpacePlan p;
        p.stride = std::max(2u,static_cast<uint32_t>(std::ceil(rate_*.25)));
        p.at = static_cast<uint32_t>(spaceClock_%p.stride);
        p.preBase = p.stride*6;
        const auto preLength=capacity_-p.preBase;
        p.preWrite=static_cast<uint32_t>(spaceClock_%preLength);
        const auto tap = [this](double delay,uint32_t at,uint32_t length) {
            const auto n=static_cast<uint32_t>(delay), pos=(at+length-n)%length;
            return SpaceTap{pos,(pos+length-1)%length,delay-n,0,written_>n+1};
        };
        p.pre=tap(pre*rate_,p.preWrite,preLength);
        p.damping=pole(200*std::pow(80.0,1-damping));
        static constexpr double times[] {.0297,.0371,.0411,.0437,.0051,.0017};
        for (unsigned n=0;n<6;++n) {
            const double stretch = n<4 ? (.4+size*3)*(mode==0?.72:mode==1?1:1.3) : 1;
            const double delay=std::clamp(rate_*times[n]*stretch,1.0,double(p.stride-1));
            p.taps[n]=tap(delay,p.at,p.stride);
            p.taps[n].feedback=n<4 ? std::min(.97,std::pow(.001,delay/(rate_*decay))) : .6;
        }
        return p;
    }
    double space(unsigned ch,double input,const SpacePlan& p) noexcept {
        // Four parallel damped comb tanks into two serial all-pass diffusers.
        // Fractional reads keep SIZE and PRE DELAY changes continuous. Fixed
        // ring lengths and a single read plan preserve all channel relationships.
        auto* data = memory(ch);
        const auto readTap = [](const float* data,const SpaceTap& t) {
            return t.valid ? data[t.at]+(data[t.before]-data[t.at])*t.fraction : 0.0;
        };
        data[p.preBase+p.preWrite] = static_cast<float>(input);
        const double source = readTap(data+p.preBase,p.pre);
        double wet = 0;
        for (unsigned n = 0; n < 6; ++n) {
            const auto offset=n*p.stride;
            const double delayed=readTap(data+offset,p.taps[n]);
            if (n<4) {
                tankLow_[ch][n] += p.damping*(delayed-tankLow_[ch][n]);
                data[offset+p.at] = static_cast<float>(source+tankLow_[ch][n]*p.taps[n].feedback); wet += delayed*.25;
            } else { data[offset+p.at] = static_cast<float>(wet+delayed*.6); wet = delayed-wet*.6; }
        }
        return wet;
    }
    std::vector<float> memory_;
    std::array<MacroShredCore,16> drive_;
    BreakBus punch_;
    std::array<FilterState,16> filter_ {};
    std::array<std::array<FilterState,3>,16> vowel_ {};
    std::array<std::array<double,4>,16> tankLow_ {};
    std::array<double,16> low_ {}, held_ {};
    NeonCharacterValues smoothed_ {};
    double rate_ = 48000, phase_ = .5, phaseB_ = .5, reversePhase_ = 0, delayTime_ = 1, shiftWindow_ = 1;
    double envelope_ = 0, wet_ = 0, holdClock_ = 0, filterPosition_ = .5;
    uint32_t capacity_ = 0, write_ = 0, written_ = 0, random_ = 0x73a951u;
    uint64_t spaceClock_ = 0;
    unsigned last_ = 99; bool ready_ = false;
};
} // namespace s3g::sample
