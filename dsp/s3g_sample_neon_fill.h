#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace s3g::sample {
constexpr std::array<float, 5> kNeonFillBeats {{.25f, .5f, 1, 2, 4}};
constexpr std::array<float, 6> kNeonFillRepeats {{1, .5f, .25f, .125f, .0625f, .03125f}};
struct NeonFillSettings {
    unsigned buffer = 2, repeat = 2;
    float breakup = .5f;
};
struct NeonFillEvent { uint32_t frame = 0; bool held = false; };

// Two preallocated tapes: freeze one by swapping roles, never copying a take
// on the audio thread. The other records what is actually heard, so another
// grab can deconstruct the previous fill. All reads/gates are channel-linked.
class NeonFill {
public:
    bool prepare(double sampleRate) {
        if (!std::isfinite(sampleRate) || sampleRate < 1000 || sampleRate > 768000) return false;
        rate_ = sampleRate; capacity_ = static_cast<unsigned>(std::ceil(sampleRate * 4));
        try { for (auto& tape : tapes_) for (auto& channel : tape.audio) channel.assign(capacity_, 0); }
        catch (...) { unprepare(); return false; }
        reset(); return true;
    }
    void unprepare() { reset(); for (auto& t : tapes_) for (auto& c : t.audio) std::vector<float>().swap(c); capacity_ = 0; }
    void reset() noexcept {
        for (auto& t : tapes_) t.write = t.valid = 0;
        rolling_ = 0; held_ = false; wet_ = 0; phase_ = length_ = 0; serial_ = 0;
        seed_ = 0x931765a1;
    }
    bool active() const noexcept { return held_ || wet_ > 0; }
    double availableSeconds() const noexcept { return tapes_[rolling_].valid / rate_; }
    double capturedSeconds() const noexcept { return captured_ / rate_; }
    void render(float* const* audio, unsigned channels, unsigned frames, double tempo,
        float masterGain, const NeonFillSettings& settings, const NeonFillEvent* events, unsigned count) noexcept {
        if (!capacity_ || !audio || channels > 32) return;
        const double beat = rate_ * 60 / std::clamp(tempo, 20., 999.);
        const float gain = std::max(.000001f, masterGain);
        const float fade = static_cast<float>(1 / std::max(1., rate_ * .005));
        unsigned event = 0;
        for (unsigned frame = 0; frame < frames; ++frame) {
            while (event < count && events[event].frame <= frame) {
                const bool press = events[event++].held;
                if (press && !held_) {
                    auto& source = tapes_[rolling_];
                    captured_ = std::min(source.valid, static_cast<unsigned>(std::round(beat * kNeonFillBeats[std::min(4u, settings.buffer)])));
                    if (captured_ >= 2) {
                        origin_ = (source.write + capacity_ - captured_) % capacity_;
                        rolling_ ^= 1u; tapes_[rolling_].valid = tapes_[rolling_].write = 0;
                        held_ = true; phase_ = length_ = 0; serial_ = 0;
                    }
                } else if (!press) held_ = false;
            }
            wet_ = std::clamp(wet_ + (held_ ? fade : -fade), 0.f, 1.f);
            if (active() && (length_ == 0 || phase_ >= length_)) {
                const float chaos = std::clamp(settings.breakup, 0.f, 1.f);
                length_ = std::clamp(static_cast<unsigned>(std::round(beat * kNeonFillRepeats[std::min(5u, settings.repeat)])), 2u, captured_);
                if (serial_ && random() < chaos) length_ = std::max(2u, length_ / (random() < .5f ? 2u : 4u));
                const unsigned chunks = std::max(1u, captured_ / length_);
                start_ = captured_ - length_;
                if (serial_ && random() < chaos) start_ = std::min(chunks - 1, static_cast<unsigned>(random() * chunks)) * length_;
                reverse_ = serial_ && random() < chaos * .4f;
                silent_ = serial_ && random() < chaos * .25f;
                phase_ = 0; ++serial_;
            }
            const auto read = (origin_ + start_ + (reverse_ ? length_ - 1 - phase_ : phase_)) % capacity_;
            const double edge = std::max(1., std::min(rate_ * .003, length_ * .2));
            const float window = length_ && !silent_ ? static_cast<float>(std::clamp(
                std::min<double>(phase_, length_ - 1 - phase_) / edge, 0., 1.)) : 0;
            auto& tape = tapes_[rolling_];
            for (unsigned ch = 0; ch < channels; ++ch) {
                const float dry = audio[ch][frame];
                const float frozen = wet_ > 0 ? tapes_[rolling_ ^ 1u].audio[ch][read] * gain * window : 0;
                const float result = wet_ >= 1 ? frozen : wet_ > 0 ? dry + wet_ * (frozen - dry) : dry;
                audio[ch][frame] = result;
                tape.audio[ch][tape.write] = result / gain;
            }
            tape.write = (tape.write + 1) % capacity_; tape.valid = std::min(capacity_, tape.valid + 1);
            if (active()) ++phase_;
        }
    }
private:
    float random() noexcept { seed_ ^= seed_ << 13; seed_ ^= seed_ >> 17; seed_ ^= seed_ << 5; return float(seed_ >> 8) / 16777216.f; }
    struct Tape { std::array<std::vector<float>, 32> audio; unsigned write = 0, valid = 0; };
    std::array<Tape, 2> tapes_;
    double rate_ = 48000;
    unsigned capacity_ = 0, rolling_ = 0, origin_ = 0, captured_ = 0;
    unsigned start_ = 0, phase_ = 0, length_ = 0, serial_ = 0;
    uint32_t seed_ = 0x931765a1;
    bool held_ = false, reverse_ = false, silent_ = false;
    float wet_ = 0;
};
}
