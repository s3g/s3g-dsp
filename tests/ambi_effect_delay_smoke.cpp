#include "s3g_ambi_effect_delay.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr uint32_t kChannels = s3g::kAmbiEffectDelayMaxChannels;

bool impulseTimingCheck()
{
    constexpr uint32_t frames = 5000u;
    constexpr uint32_t channels = 4u;
    s3g::AmbiEffectDelayParams params {};
    params.order = 1u;
    params.body = s3g::AmbiEffectBody::Icosa12;
    params.timeMs = 50.0f;
    params.feedback = 0.0f;
    params.topologyAmount = 0.0f;
    params.mix = 1.0f;

    s3g::AmbiEffectDelay processor;
    processor.setParams(params);
    processor.prepare(kSampleRate);
    processor.reset();
    std::array<std::array<float, frames>, channels> input {};
    std::array<std::array<float, frames>, channels> output {};
    std::array<float*, channels> in {};
    std::array<float*, channels> out {};
    for (uint32_t ch = 0u; ch < channels; ++ch) {
        in[ch] = input[ch].data();
        out[ch] = output[ch].data();
    }
    input[0][0] = 0.5f;
    processor.process(in.data(), out.data(), channels, channels, frames);

    const uint32_t expected = 2400u;
    float earlyPeak = 0.0f;
    float arrivalPeak = 0.0f;
    for (uint32_t frame = 64u; frame < expected - 16u; ++frame) {
        for (uint32_t ch = 0u; ch < channels; ++ch) {
            earlyPeak = std::max(earlyPeak, std::fabs(output[ch][frame]));
        }
    }
    for (uint32_t frame = expected - 4u; frame <= expected + 4u; ++frame) {
        for (uint32_t ch = 0u; ch < channels; ++ch) {
            arrivalPeak = std::max(arrivalPeak, std::fabs(output[ch][frame]));
        }
    }
    std::printf("Ambi Effect Delay impulse early %.6g arrival %.6g\n",
        earlyPeak, arrivalPeak);
    return earlyPeak < 1.0e-5f && arrivalPeak > 0.20f;
}

bool pickupRelationshipCheck()
{
    float minimumTime = 100000.0f;
    float maximumTime = 0.0f;
    float minimumFeedback = 1.0f;
    float maximumFeedback = 0.0f;
    for (uint32_t node = 0u; node < 12u; ++node) {
        const float sameTime = s3g::ambiEffectPickupDelayMs(
            320.0f, 0.0f, 0.0f, 0.0f, node, 12u);
        const float sameFeedback = s3g::ambiEffectPickupDelayFeedback(
            0.32f, 0.0f, 0.0f, 0.0f, node, 12u);
        if (std::fabs(sameTime - 320.0f) > 1.0e-5f
            || std::fabs(sameFeedback - 0.32f) > 1.0e-6f) return false;
        const float time = s3g::ambiEffectPickupDelayMs(
            320.0f, 0.0f, 0.85f, 0.65f, node, 12u);
        const float feedback = s3g::ambiEffectPickupDelayFeedback(
            0.42f, 0.0f, 0.85f, 0.65f, node, 12u);
        minimumTime = std::min(minimumTime, time);
        maximumTime = std::max(maximumTime, time);
        minimumFeedback = std::min(minimumFeedback, feedback);
        maximumFeedback = std::max(maximumFeedback, feedback);
    }
    const float manualTime = s3g::ambiEffectPickupDelayMs(
        320.0f, 1.0f, 0.0f, 0.0f, 0u, 12u);
    const float manualFeedback = s3g::ambiEffectPickupDelayFeedback(
        0.32f, 1.0f, 0.0f, 0.0f, 0u, 12u);
    std::printf("Ambi Effect Delay relationships %.1f..%.1f ms, %.3f..%.3f fb; manual %.1f/%.3f\n",
        minimumTime, maximumTime, minimumFeedback, maximumFeedback,
        manualTime, manualFeedback);
    return maximumTime > minimumTime * 2.2f
        && maximumFeedback - minimumFeedback > 0.12f
        && manualTime > 900.0f
        && manualFeedback > 0.65f;
}

bool olaTimeMoveCheck()
{
    constexpr uint32_t warmFrames = 12000u;
    constexpr uint32_t moveFrames = 4096u;
    constexpr uint32_t channels = 4u;
    s3g::AmbiEffectDelayParams params {};
    params.order = 1u;
    params.body = s3g::AmbiEffectBody::Icosa12;
    params.timeMs = 20.0f;
    params.feedback = 0.0f;
    params.topologyAmount = 0.0f;
    params.mix = 1.0f;
    s3g::AmbiEffectDelay processor;
    processor.setParams(params);
    processor.prepare(kSampleRate);
    processor.reset();

    std::array<std::array<float, warmFrames>, channels> warmInput {};
    std::array<std::array<float, warmFrames>, channels> warmOutput {};
    std::array<float*, channels> in {};
    std::array<float*, channels> out {};
    for (uint32_t ch = 0u; ch < channels; ++ch) {
        in[ch] = warmInput[ch].data();
        out[ch] = warmOutput[ch].data();
    }
    for (uint32_t frame = 0u; frame < warmFrames; ++frame) {
        warmInput[0][frame] = 0.2f * std::sin(static_cast<float>(
            2.0 * s3g::kPi * 440.0 * frame / kSampleRate));
    }
    processor.process(in.data(), out.data(), channels, channels, warmFrames);

    params.timeMs = 160.0f;
    processor.setParams(params);
    std::array<std::array<float, moveFrames>, channels> moveInput {};
    std::array<std::array<float, moveFrames>, channels> moveOutput {};
    for (uint32_t ch = 0u; ch < channels; ++ch) {
        in[ch] = moveInput[ch].data();
        out[ch] = moveOutput[ch].data();
    }
    for (uint32_t frame = 0u; frame < moveFrames; ++frame) {
        moveInput[0][frame] = 0.2f * std::sin(static_cast<float>(
            2.0 * s3g::kPi * 440.0
                * (warmFrames + frame) / kSampleRate));
    }
    processor.process(in.data(), out.data(), channels, channels, moveFrames);
    float previous = warmOutput[0][warmFrames - 1u];
    float maximumStep = 0.0f;
    double energy = 0.0;
    for (float sample : moveOutput[0]) {
        if (!std::isfinite(sample)) return false;
        maximumStep = std::max(maximumStep, std::fabs(sample - previous));
        energy += static_cast<double>(sample) * sample;
        previous = sample;
    }
    std::printf("Ambi Effect Delay OLA move max step %.6g energy %.6g\n",
        maximumStep, energy);
    return maximumStep < 0.08f && energy > 1.0;
}

bool automationSafetyCheck()
{
    constexpr uint32_t frames = 128u;
    s3g::AmbiEffectDelay processor;
    s3g::AmbiEffectDelayParams initial {};
    processor.setParams(initial);
    processor.prepare(96000.0);
    std::array<std::array<double, frames>, kChannels> input {};
    std::array<std::array<double, frames>, kChannels> output {};
    std::array<double*, kChannels> in {};
    std::array<double*, kChannels> out {};
    for (uint32_t ch = 0u; ch < kChannels; ++ch) {
        in[ch] = input[ch].data();
        out[ch] = output[ch].data();
    }
    float peak = 0.0f;
    for (uint32_t block = 0u; block < 240u; ++block) {
        s3g::AmbiEffectDelayParams params {};
        params.order = 1u + block % 7u;
        params.body = static_cast<s3g::AmbiEffectBody>(block % 5u);
        params.topology = static_cast<s3g::AmbiEffectTopology>(block % 4u);
        params.timeMs = 5.0f + static_cast<float>(block % 100u) * 19.9f;
        params.feedback = static_cast<float>(block % 12u) * 0.08f;
        params.tone = static_cast<float>(block % 11u) / 10.0f;
        params.spread = static_cast<float>(block % 9u) / 8.0f;
        params.deviation = static_cast<float>(block % 7u) / 6.0f;
        params.topologyAmount = static_cast<float>(block % 11u) / 10.0f;
        params.roamingRateHz = 2.0f;
        params.mix = static_cast<float>(block % 13u) / 12.0f;
        params.maskAmount = static_cast<float>(block % 11u) / 10.0f;
        params.maskAzimuthDeg = -180.0f + static_cast<float>(block % 37u) * 10.0f;
        params.maskElevationDeg = -90.0f + static_cast<float>(block % 19u) * 10.0f;
        params.maskWidth = static_cast<float>(block % 11u) / 10.0f;
        params.maskCurve = static_cast<float>(block % 9u) / 8.0f;
        params.maskDry = static_cast<float>(block % 13u) / 12.0f;
        for (uint32_t node = 0u;
            node < s3g::kAmbiEffectDelayMaxPickups; ++node) {
            params.pickupTimeTrim[node] = static_cast<float>(
                static_cast<int>((block + node) % 21u) - 10) / 10.0f;
            params.pickupFeedbackTrim[node] = static_cast<float>(
                static_cast<int>((block + node * 3u) % 21u) - 10) / 10.0f;
        }
        processor.setParams(params);
        for (uint32_t ch = 0u; ch < kChannels; ++ch) {
            for (uint32_t frame = 0u; frame < frames; ++frame) {
                input[ch][frame] = ch == 0u
                    ? std::sin(static_cast<double>(block * frames + frame)
                        * 0.071) * 0.12 : 0.0;
            }
        }
        processor.process(in.data(), out.data(), kChannels, kChannels, frames);
        for (uint32_t ch = 0u; ch < kChannels; ++ch) {
            for (double value : output[ch]) {
                if (!std::isfinite(value)) return false;
                peak = std::max(peak, static_cast<float>(std::fabs(value)));
            }
        }
    }
    std::printf("Ambi Effect Delay automation peak %.6g\n", peak);
    return peak < 5.0f;
}

} // namespace

int main()
{
    if (!impulseTimingCheck()) {
        std::fprintf(stderr, "Ambi Effect Delay impulse timing failed\n");
        return 1;
    }
    if (!pickupRelationshipCheck()) {
        std::fprintf(stderr, "Ambi Effect Delay pickup relationships failed\n");
        return 1;
    }
    if (!olaTimeMoveCheck()) {
        std::fprintf(stderr, "Ambi Effect Delay OLA time move failed\n");
        return 1;
    }
    if (!automationSafetyCheck()) {
        std::fprintf(stderr, "Ambi Effect Delay automation safety failed\n");
        return 1;
    }
    return 0;
}
