#include "s3g_ambi_effect_gain.h"
#include "s3g_ambi_effect_pitch.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>

namespace {

constexpr uint32_t kFrames = 4096u;
constexpr uint32_t kChannels = s3g::kAmbiEffectDjFilterMaxChannels;

template <typename Processor>
float runProcessor(Processor& processor, bool impulse,
    std::array<std::array<float, kFrames>, kChannels>& input,
    std::array<std::array<float, kFrames>, kChannels>& output)
{
    std::array<float*, kChannels> in {};
    std::array<float*, kChannels> out {};
    for (uint32_t channel = 0u; channel < kChannels; ++channel) {
        input[channel].fill(0.0f);
        output[channel].fill(0.0f);
        in[channel] = input[channel].data();
        out[channel] = output[channel].data();
    }
    if (impulse) input[0][0] = 0.5f;
    else {
        for (uint32_t frame = 0u; frame < kFrames; ++frame) {
            input[0][frame] = 0.15f * std::sin(
                static_cast<float>(frame) * 0.071f);
            input[1][frame] = 0.08f * std::cos(
                static_cast<float>(frame) * 0.037f);
        }
    }
    processor.process(in.data(), out.data(), kChannels, kChannels, kFrames);
    float energy = 0.0f;
    for (const auto& channel : output) {
        for (float value : channel) {
            assert(std::isfinite(value));
            energy += value * value;
        }
    }
    return energy;
}

} // namespace

int main()
{
    assert(s3g::resolveAmbiEffectBody(s3g::AmbiEffectBody::Auto, 1u)
        == s3g::AmbiEffectBody::Icosa12);
    assert(s3g::resolveAmbiEffectBody(s3g::AmbiEffectBody::Auto, 3u)
        == s3g::AmbiEffectBody::Icosa12);
    assert(s3g::resolveAmbiEffectBody(s3g::AmbiEffectBody::Auto, 4u)
        == s3g::AmbiEffectBody::Dodeca20);
    assert(s3g::resolveAmbiEffectBody(s3g::AmbiEffectBody::Tetra4, 1u)
        == s3g::AmbiEffectBody::Icosa12);
    assert(s3g::ambiEffectBodyPickupCount(s3g::AmbiEffectBody::Dodeca20)
        == 20u);
    const auto dodeca = s3g::ambiEffectBodyDirections(
        s3g::AmbiEffectBody::Dodeca20);
    for (const auto& direction : dodeca) {
        const float length = std::sqrt(direction.x * direction.x
            + direction.y * direction.y + direction.z * direction.z);
        assert(std::fabs(length - 1.0f) < 0.0001f);
    }

    std::array<std::array<float, kFrames>, kChannels> input {};
    std::array<std::array<float, kFrames>, kChannels> output {};

    s3g::AmbiEffectPitch pitch;
    s3g::AmbiEffectPitchParams pitchParams {};
    pitchParams.order = 7u;
    pitchParams.mix = 1.0f;
    pitch.setParams(pitchParams);
    pitch.prepare(48000.0);
    pitch.reset();
    const float pitchNeutral = runProcessor(
        pitch, true, input, output);
    assert(std::fabs(output[0][0] - 0.5f) < 0.00001f);
    assert(pitchNeutral > 0.24f && pitchNeutral < 0.26f);
    assert(pitch.activePickupCount() == 20u);

    pitchParams.semitones = 7.0f;
    pitchParams.windowMs = 64.0f;
    pitchParams.glideMs = 10.0f;
    pitchParams.spread = 0.45f;
    pitchParams.deviation = 0.3f;
    pitchParams.pickupPitchTrim[19] = -0.5f;
    pitch.setParams(pitchParams);
    const float pitchShifted = runProcessor(
        pitch, false, input, output);
    assert(pitchShifted > 0.0001f);

    s3g::AmbiEffectGain gain;
    s3g::AmbiEffectGainParams gainParams {};
    gainParams.order = 1u;
    gain.setParams(gainParams);
    gain.prepare(48000.0);
    gain.reset();
    const float gainNeutral = runProcessor(gain, true, input, output);
    assert(std::fabs(output[0][0] - 0.5f) < 0.00001f);
    assert(gainNeutral > 0.24f && gainNeutral < 0.26f);
    assert(gain.activePickupCount() == 12u);

    gainParams.order = 7u;
    gainParams.gainDb = -12.0f;
    gainParams.spread = 0.55f;
    gainParams.deviation = 0.4f;
    gainParams.pickupGainTrim[19] = 0.75f;
    gainParams.maskAmount = 1.0f;
    gainParams.maskDry = 0.0f;
    gainParams.maskCurve = 0.8f;
    gain.setParams(gainParams);
    const float gainField = runProcessor(gain, false, input, output);
    assert(gainField > 0.00001f);
    assert(gain.activePickupCount() == 20u);

    std::cout << "ambi effect pitch/gain smoke ok\n";
    return 0;
}
