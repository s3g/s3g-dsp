#pragma once
#include "s3g_sample_neon_stack.h"
#include "s3g_sample_neon_family.h"
#include "s3g_sample_player.h"

namespace s3g::sample {
// Continuous normalized read head, independent of the layer trajectory. No
// grains, allocations, channel redistribution or source changes on audio.
class NeonLanesPlayer {
public:
    void reset() noexcept { phase_ = 0; weights_.fill(0); cursorCount_ = 0; initialized_ = false; }
    unsigned cursorCount() const noexcept { return cursorCount_; }
    const auto& cursors() const noexcept { return cursors_; }
    float position() const noexcept { return position_; }
    void render(const NeonStack* stack, NeonStackLayer fallback, unsigned selected,
        double start, double end, const NeonFamilySettings& f, double sampleRate,
        float gain, double tune, unsigned direction, bool velocityEnabled,
        const float* positions, const float* velocities, const uint8_t* triggers,
        float* const* output, unsigned frames) noexcept {
        const unsigned count = stack ? std::min<unsigned>(32, stack->count) : 1;
        std::array<NeonStackLayer, 32> layers {};
        unsigned anchor = 32;
        for (unsigned n = 0; n < count; ++n) {
            layers[n] = stack ? stack->layers[n] : fallback;
            if (n == selected) { layers[n].start = start; layers[n].end = end; }
            if (layers[n].asset && layers[n].asset->frameCount() > 1 && layers[n].end > layers[n].start && anchor == 32) anchor = n;
        }
        const double duration = anchor < 32 ? (layers[anchor].asset->frameCount() - 1.)
            * (layers[anchor].end - layers[anchor].start) / layers[anchor].asset->sampleRate : 1;
        const double increment = f[NeonFamily::LaneRate] * std::pow(2., tune / 12.) / std::max(1., duration * sampleRate);
        const float slew = static_cast<float>(1 - std::exp(-1. / (sampleRate * f[NeonFamily::LaneSlew])));
        const double join = f[NeonFamily::LaneJoin];
        const bool reverse = (direction & 1u) != 0, pingpong = direction >= 2;
        cursorCount_ = 0;
        for (unsigned frame = 0; frame < frames; ++frame) {
            if (triggers[frame]) reset();
            const float position = positions[frame];
            if (position < 0) {
                if (position == -1 || position == -2) reset(); // raw audition / stopped
                if (position != -1) for (unsigned ch = 0; ch < 16; ++ch) output[ch][frame] = 0;
                continue; // -3 holds the clock while HOST is stopped.
            }
            for (unsigned ch = 0; ch < 16; ++ch) output[ch][frame] = 0;
            if (anchor == 32) continue;
            const double target = std::clamp<double>(position, 0, 1) * (count - 1);
            unsigned below = 32, above = 32;
            for (unsigned n = 0; n < count; ++n) if (layers[n].asset && layers[n].end > layers[n].start) {
                if (n <= target) below = n;
                if (n >= target && above == 32) above = n;
            }
            if (below == 32) below = above;
            if (above == 32) above = below;
            float mix = above == below ? 0 : static_cast<float>((target - below) / (above - below));
            if (f[NeonFamily::StackJump] != 0) { below = above = mix < .5f ? below : above; mix = 0; }
            const float a = std::cos(mix * 1.57079632679f), b = std::sin(mix * 1.57079632679f);
            double energy = 0, weightedPosition = 0, totalWeight = 0;
            for (unsigned n = 0; n < count; ++n) {
                const float destination = n == below ? a : n == above ? b : 0;
                weights_[n] = initialized_ ? weights_[n] + slew * (destination - weights_[n]) : destination;
                energy += weights_[n] * weights_[n];
                weightedPosition += weights_[n] * weights_[n] * n;
                totalWeight += weights_[n] * weights_[n];
            }
            initialized_ = true;
            position_ = count > 1 && totalWeight > 0 ? static_cast<float>(weightedPosition / totalWeight / (count - 1)) : 0;
            const float level = static_cast<float>(gain * (velocityEnabled ? velocities[frame] : 1) / std::sqrt(std::max(1.e-12, energy)));
            for (unsigned n = 0; n < count; ++n) {
                const auto& layer = layers[n];
                if (!layer.asset || weights_[n] < .00001f) continue;
                const double traversal = pingpong && phase_ > 1 ? 2 - phase_ : phase_;
                const double sourcePhase = reverse ? 1 - traversal : traversal;
                const double at = layer.start + sourcePhase * (layer.end - layer.start);
                for (unsigned ch = 0; ch < layer.asset->channelCount; ++ch) {
                    const auto& samples = layer.asset->channels[ch];
                    float value = read(samples, at);
                    if (!pingpong && join > 0 && phase_ > 1 - join) {
                        const double t = (phase_ - (1 - join)) / join;
                        const double otherPhase = reverse ? 1 - (phase_ - (1 - join)) : phase_ - (1 - join);
                        const float other = read(samples, layer.start + otherPhase * (layer.end - layer.start));
                        value += (other - value) * static_cast<float>(t * t * (3 - 2 * t));
                    }
                    output[ch][frame] += value * weights_[n] * level;
                }
                if (frame + 1 == frames && cursorCount_ < cursors_.size() && weights_[n] > .01f)
                    cursors_[cursorCount_++] = {static_cast<float>(at), 60, static_cast<float>(layer.start),
                        static_cast<float>(layer.end), n + 1u, layer.asset};
            }
            phase_ += increment;
            if (pingpong) { if (phase_ >= 2) phase_ = std::fmod(phase_, 2); }
            else if (phase_ >= 1) phase_ = join + std::fmod(phase_ - 1, 1 - join);
        }
    }
private:
    static float read(const std::vector<float>& samples, double at) noexcept {
        if (samples.empty()) return 0;
        at = std::clamp(at, 0., 1.) * (samples.size() - 1);
        const auto first = static_cast<std::size_t>(at), second = std::min(first + 1, samples.size() - 1);
        return samples[first] + (samples[second] - samples[first]) * static_cast<float>(at - first);
    }
    double phase_ = 0;
    float position_ = 0;
    bool initialized_ = false;
    std::array<float, 32> weights_ {};
    std::array<VoiceCursor, kMaximumVoices> cursors_ {};
    unsigned cursorCount_ = 0;
};
}
