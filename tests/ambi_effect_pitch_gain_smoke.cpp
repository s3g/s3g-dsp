#include "s3g_ambi_effect_gain.h"
#include "s3g_ambi_effect_pitch.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>

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
        output[channel].fill(std::numeric_limits<float>::quiet_NaN());
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
    const auto require = [](bool condition, const char* message) {
        if (condition) return;
        std::cerr << "ambi effect pitch/gain smoke failed: "
                  << message << '\n';
        std::exit(1);
    };
    assert(s3g::resolveAmbiEffectBody(s3g::AmbiEffectBody::Auto, 1u)
        == s3g::AmbiEffectBody::Icosa12);
    assert(s3g::resolveAmbiEffectBody(s3g::AmbiEffectBody::Auto, 3u)
        == s3g::AmbiEffectBody::Dodeca20);
    assert(s3g::resolveAmbiEffectBody(s3g::AmbiEffectBody::Auto, 4u)
        == s3g::AmbiEffectBody::Sphere24);
    assert(s3g::resolveAmbiEffectBody(s3g::AmbiEffectBody::Tetra4, 1u)
        == s3g::AmbiEffectBody::Icosa12);
    assert(s3g::ambiEffectBodyPickupCount(s3g::AmbiEffectBody::Dodeca20)
        == 20u);
    const auto dodeca = s3g::ambiEffectBodyDirections(
        s3g::AmbiEffectBody::Dodeca20);
    for (uint32_t point = 0u; point < 20u; ++point) {
        const auto& direction = dodeca[point];
        const float length = std::sqrt(direction.x * direction.x
            + direction.y * direction.y + direction.z * direction.z);
        assert(std::fabs(length - 1.0f) < 0.0001f);
    }
    assert(s3g::ambiEffectBodyPickupCount(s3g::AmbiEffectBody::Sphere24)
        == 24u);
    const auto sphere = s3g::ambiEffectBodyDirections(
        s3g::AmbiEffectBody::Sphere24);
    for (const auto& direction : sphere) {
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
    require(pitch.outputPeak() > 0.49f && pitch.outputPeak() < 0.51f,
        "pitch peak was not fused into processing");
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
    require(gain.outputPeak() > 0.49f && gain.outputPeak() < 0.51f,
        "gain peak was not fused into processing");
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

    // An order change crossfades the decoder/encoder matrices. Once that
    // transition has converged, channels above the new order must snap to
    // exact silence instead of keeping the expensive 64-channel transition
    // path alive indefinitely at float precision.
    s3g::AmbiEffectGain orderTransitionGain;
    s3g::AmbiEffectGainParams orderTransitionParams {};
    orderTransitionParams.order = 7u;
    orderTransitionParams.gainDb = -6.0f;
    orderTransitionGain.setParams(orderTransitionParams);
    constexpr uint32_t kTransitionFrames = 512u;
    orderTransitionGain.prepare(44100.0);
    orderTransitionGain.reset();
    std::array<float*, kChannels> transitionInput {};
    std::array<float*, kChannels> transitionOutput {};
    for (uint32_t channel = 0u; channel < kChannels; ++channel) {
        for (uint32_t frame = 0u; frame < kFrames; ++frame) {
            input[channel][frame] = channel == 0u
                ? 0.15f * std::sin(static_cast<float>(frame) * 0.071f)
                : channel == 1u
                    ? 0.08f * std::cos(static_cast<float>(frame) * 0.037f)
                    : channel == 40u ? 0.25f : 0.0f;
        }
        output[channel].fill(std::numeric_limits<float>::quiet_NaN());
        transitionInput[channel] = input[channel].data();
        transitionOutput[channel] = output[channel].data();
    }
    orderTransitionParams.order = 5u;
    orderTransitionGain.setParams(orderTransitionParams);
    for (uint32_t block = 0u; block < 128u; ++block) {
        orderTransitionGain.process(transitionInput.data(),
            transitionOutput.data(), kChannels, kChannels,
            kTransitionFrames);
    }
    require(output[40][kTransitionFrames - 1u] == 0.0f,
        "7OA to 5OA did not silence channels above the new order");
    require(!orderTransitionGain.matrixTransitionActive(),
        "7OA to 5OA matrix transition did not converge");

    s3g::AmbiEffectGain orderReferenceGain;
    orderReferenceGain.setParams(orderTransitionParams);
    orderReferenceGain.prepare(44100.0);
    orderReferenceGain.reset();
    std::array<std::array<float, kFrames>, kChannels> referenceOutput {};
    std::array<float*, kChannels> referenceOutputPointers {};
    for (uint32_t channel = 0u; channel < kChannels; ++channel)
        referenceOutputPointers[channel] = referenceOutput[channel].data();
    orderReferenceGain.process(transitionInput.data(),
        referenceOutputPointers.data(), kChannels, kChannels,
        kTransitionFrames);
    float settledOrderDifference = 0.0f;
    for (uint32_t channel = 0u; channel < 36u; ++channel) {
        for (uint32_t frame = 0u; frame < kTransitionFrames; ++frame) {
            settledOrderDifference = std::max(settledOrderDifference,
                std::fabs(output[channel][frame]
                    - referenceOutput[channel][frame]));
        }
    }
    require(settledOrderDifference < 1.0e-7f,
        "settled 5OA output differs from a fresh 5OA processor");

    s3g::AmbiEffectGain reverseOrderTransitionGain;
    orderTransitionParams.order = 5u;
    reverseOrderTransitionGain.setParams(orderTransitionParams);
    reverseOrderTransitionGain.prepare(44100.0);
    reverseOrderTransitionGain.reset();
    orderTransitionParams.order = 7u;
    reverseOrderTransitionGain.setParams(orderTransitionParams);
    for (uint32_t block = 0u; block < 128u; ++block) {
        reverseOrderTransitionGain.process(transitionInput.data(),
            transitionOutput.data(), kChannels, kChannels,
            kTransitionFrames);
    }
    require(!reverseOrderTransitionGain.matrixTransitionActive(),
        "5OA to 7OA matrix transition did not converge");
    s3g::AmbiEffectGain reverseOrderReferenceGain;
    reverseOrderReferenceGain.setParams(orderTransitionParams);
    reverseOrderReferenceGain.prepare(44100.0);
    reverseOrderReferenceGain.reset();
    reverseOrderReferenceGain.process(transitionInput.data(),
        referenceOutputPointers.data(), kChannels, kChannels,
        kTransitionFrames);
    float settledReverseOrderDifference = 0.0f;
    for (uint32_t channel = 0u; channel < kChannels; ++channel) {
        for (uint32_t frame = 0u; frame < kTransitionFrames; ++frame) {
            settledReverseOrderDifference = std::max(
                settledReverseOrderDifference,
                std::fabs(output[channel][frame]
                    - referenceOutput[channel][frame]));
        }
    }
    require(settledReverseOrderDifference < 1.0e-7f,
        "settled 7OA output differs from a fresh 7OA processor");

    std::cout << "ambi effect pitch/gain smoke ok\n";
    return 0;
}
