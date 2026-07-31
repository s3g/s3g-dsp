#include "s3g_ambi_effect_dj_filter.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr uint32_t kFrames = 48000u;
constexpr uint32_t kChannels = s3g::kAmbiEffectDjFilterMaxChannels;

struct RenderResult {
    double rms = 0.0;
    float peak = 0.0f;
    bool finite = true;
};

RenderResult renderSine(s3g::AmbiEffectDjFilterParams params, float frequency)
{
    s3g::AmbiEffectDjFilter processor;
    processor.prepare(kSampleRate);
    processor.setParams(params);
    processor.reset();

    std::array<std::vector<float>, kChannels> input;
    std::array<std::vector<float>, kChannels> output;
    std::array<float*, kChannels> inputPointers {};
    std::array<float*, kChannels> outputPointers {};
    for (uint32_t ch = 0u; ch < kChannels; ++ch) {
        input[ch].assign(kFrames, 0.0f);
        output[ch].assign(kFrames, 0.0f);
        inputPointers[ch] = input[ch].data();
        outputPointers[ch] = output[ch].data();
    }
    for (uint32_t frame = 0u; frame < kFrames; ++frame) {
        input[0][frame] = std::sin(
            static_cast<float>(2.0 * s3g::kPi * frequency
                * static_cast<double>(frame) / kSampleRate)) * 0.25f;
    }
    processor.process(inputPointers.data(), outputPointers.data(),
        kChannels, kChannels, kFrames);

    RenderResult result {};
    double energy = 0.0;
    uint64_t samples = 0u;
    const uint32_t start = kFrames / 2u;
    const uint32_t activeChannels = s3g::ambiEffectChannelsForOrder(params.order);
    for (uint32_t ch = 0u; ch < activeChannels; ++ch) {
        for (uint32_t frame = start; frame < kFrames; ++frame) {
            const float value = output[ch][frame];
            result.finite = result.finite && std::isfinite(value);
            result.peak = std::max(result.peak, std::fabs(value));
            energy += static_cast<double>(value) * value;
            ++samples;
        }
    }
    result.rms = std::sqrt(energy / static_cast<double>(std::max<uint64_t>(1u, samples)));
    return result;
}

bool bodyDefaultsCheck()
{
    return s3g::ambiEffectDefaultBodyForOrder(1u)
            == s3g::AmbiEffectBody::Icosa12
        && s3g::ambiEffectDefaultBodyForOrder(2u)
            == s3g::AmbiEffectBody::Icosa12
        && s3g::ambiEffectDefaultBodyForOrder(3u)
            == s3g::AmbiEffectBody::Dodeca20
        && s3g::ambiEffectDefaultBodyForOrder(7u)
            == s3g::AmbiEffectBody::Sphere24
        && s3g::resolveAmbiEffectBody(
               s3g::AmbiEffectBody::Tetra4, 7u)
            == s3g::AmbiEffectBody::Icosa12;
}

bool aedConventionCheck()
{
    const auto left = s3g::directionFromAed(90.0f, 0.0f);
    const auto right = s3g::directionFromAed(-90.0f, 0.0f);
    const auto leftTop = s3g::projectAedDirection(left, 90.0f, 0.0f);
    const auto rightTop = s3g::projectAedDirection(right, 90.0f, 0.0f);
    if (left.y < 0.999f || right.y > -0.999f
        || leftTop.horizontal > -0.999f
        || rightTop.horizontal < 0.999f
        || std::fabs(s3g::aedAzimuthSliderNorm(90.0f) - 0.25f)
            > 1.0e-6f
        || std::fabs(s3g::aedAzimuthSliderNorm(-90.0f) - 0.75f)
            > 1.0e-6f
        || std::fabs(s3g::aedAzimuthFromSliderNorm(0.25f) - 90.0f)
            > 1.0e-6f
        || std::fabs(s3g::aedAzimuthFromSliderNorm(0.75f) + 90.0f)
            > 1.0e-6f) {
        return false;
    }

    s3g::AmbiEffectDjFilterParams params {};
    params.order = 1u;
    params.body = s3g::AmbiEffectBody::Icosa12;
    params.maskAmount = 1.0f;
    params.maskWidth = 0.0f;
    params.maskCurve = 1.0f;

    s3g::AmbiEffectDjFilter processor;
    processor.prepare(kSampleRate);
    processor.setParams(params);
    processor.reset();
    uint32_t leftNode = 0u;
    uint32_t rightNode = 0u;
    for (uint32_t node = 1u; node < processor.activePickupCount(); ++node) {
        if (processor.nodeDirection(node).y
            > processor.nodeDirection(leftNode).y) leftNode = node;
        if (processor.nodeDirection(node).y
            < processor.nodeDirection(rightNode).y) rightNode = node;
    }

    params.maskAzimuthDeg = 90.0f;
    processor.setParams(params);
    processor.reset();
    const float leftAtPositive = processor.nodeWetMask(leftNode);
    const float rightAtPositive = processor.nodeWetMask(rightNode);
    params.maskAzimuthDeg = -90.0f;
    processor.setParams(params);
    processor.reset();
    const float leftAtNegative = processor.nodeWetMask(leftNode);
    const float rightAtNegative = processor.nodeWetMask(rightNode);
    std::printf("AED convention mask +90 L/R %.3f/%.3f; -90 L/R %.3f/%.3f\n",
        leftAtPositive, rightAtPositive,
        leftAtNegative, rightAtNegative);
    return leftAtPositive > rightAtPositive + 0.25f
        && rightAtNegative > leftAtNegative + 0.25f;
}

bool openIdentityCheck()
{
    constexpr uint32_t frames = 257u;
    for (uint32_t order = 1u; order <= 7u; ++order) {
        for (uint32_t body = 0u; body <= 5u; ++body) {
            for (uint32_t topology = 0u; topology <= 3u; ++topology) {
                s3g::AmbiEffectDjFilterParams params {};
                params.order = order;
                params.body = static_cast<s3g::AmbiEffectBody>(body);
                params.topology = static_cast<s3g::AmbiEffectTopology>(topology);
                params.filter = 0.5f;
                params.resonance = 1.0f;
                params.topologyAmount = 1.0f;
                params.mix = 1.0f;

                s3g::AmbiEffectDjFilter processor;
                processor.prepare(kSampleRate);
                processor.setParams(params);
                processor.reset();
                std::array<std::array<float, frames>, kChannels> input {};
                std::array<std::array<float, frames>, kChannels> output {};
                std::array<float*, kChannels> inputPointers {};
                std::array<float*, kChannels> outputPointers {};
                for (uint32_t ch = 0u; ch < kChannels; ++ch) {
                    inputPointers[ch] = input[ch].data();
                    outputPointers[ch] = output[ch].data();
                    for (uint32_t frame = 0u; frame < frames; ++frame) {
                        input[ch][frame] = std::sin(
                            static_cast<float>((ch + 1u) * (frame + 3u))
                            * 0.017f) * 0.1f;
                    }
                }
                processor.process(inputPointers.data(), outputPointers.data(),
                    kChannels, kChannels, frames);
                const uint32_t active = s3g::ambiEffectChannelsForOrder(order);
                for (uint32_t ch = 0u; ch < kChannels; ++ch) {
                    for (uint32_t frame = 0u; frame < frames; ++frame) {
                        const float expected = ch < active
                            ? input[ch][frame] : 0.0f;
                        if (output[ch][frame] != expected) {
                            std::fprintf(stderr,
                                "open identity failed order=%u body=%u topology=%u ch=%u frame=%u got=%g expected=%g\n",
                                order, body, topology, ch, frame,
                                output[ch][frame], expected);
                            return false;
                        }
                    }
                }
            }
        }
    }
    return true;
}

bool responseCheck()
{
    s3g::AmbiEffectDjFilterParams params {};
    params.order = 3u;
    params.body = s3g::AmbiEffectBody::Auto;
    params.topology = s3g::AmbiEffectTopology::Local;
    params.topologyAmount = 0.0f;
    params.resonance = 0.0f;
    params.mix = 1.0f;

    params.filter = 0.0f;
    const auto lowLow = renderSine(params, 100.0f);
    const auto lowHigh = renderSine(params, 6000.0f);
    params.filter = 1.0f;
    const auto highLow = renderSine(params, 80.0f);
    const auto highHigh = renderSine(params, 5000.0f);
    std::printf(
        "Ambi Effect DJ response LP 100/6000 Hz %.6g/%.6g; HP 80/5000 Hz %.6g/%.6g\n",
        lowLow.rms, lowHigh.rms, highLow.rms, highHigh.rms);
    return lowLow.finite && lowHigh.finite
        && highLow.finite && highHigh.finite
        && lowLow.rms > lowHigh.rms * 4.0
        && highHigh.rms > highLow.rms * 4.0
        && std::max({ lowLow.peak, lowHigh.peak,
               highLow.peak, highHigh.peak }) < 2.0f;
}

bool pickupVariationAndMaskCheck()
{
    constexpr uint32_t frames = 8192u;
    constexpr uint32_t channels = 4u;
    s3g::AmbiEffectDjFilterParams params {};
    params.order = 1u;
    params.body = s3g::AmbiEffectBody::Icosa12;
    params.filter = 0.5f;
    params.topologyAmount = 0.0f;
    params.pickupFilterTrim[0] = -1.0f;
    params.pickupFilterTrim[1] = 1.0f;

    s3g::AmbiEffectDjFilter processor;
    processor.prepare(kSampleRate);
    processor.setParams(params);
    processor.reset();
    std::array<std::array<float, frames>, channels> input {};
    std::array<std::array<float, frames>, channels> output {};
    std::array<float*, channels> inputPointers {};
    std::array<float*, channels> outputPointers {};
    for (uint32_t ch = 0u; ch < channels; ++ch) {
        inputPointers[ch] = input[ch].data();
        outputPointers[ch] = output[ch].data();
    }
    for (uint32_t frame = 0u; frame < frames; ++frame) {
        const double t = static_cast<double>(frame) / kSampleRate;
        const float signal = 0.12f * std::sin(
            static_cast<float>(2.0 * s3g::kPi * 110.0 * t))
            + 0.08f * std::sin(
                static_cast<float>(2.0 * s3g::kPi * 6200.0 * t));
        input[0][frame] = signal;
        input[1][frame] = signal * 0.75f;
        input[2][frame] = signal * -0.35f;
        input[3][frame] = signal * 0.55f;
    }
    processor.process(inputPointers.data(), outputPointers.data(),
        channels, channels, frames);
    double difference = 0.0;
    for (uint32_t ch = 0u; ch < channels; ++ch) {
        for (uint32_t frame = frames / 2u; frame < frames; ++frame) {
            const double delta = output[ch][frame] - input[ch][frame];
            difference += delta * delta;
        }
    }
    float meterPeak = 0.0f;
    for (uint32_t node = 0u; node < 4u; ++node) {
        meterPeak = std::max(meterPeak, processor.nodeLevel(node));
    }

    params.maskAmount = 1.0f;
    params.maskAzimuthDeg = 0.0f;
    params.maskElevationDeg = 0.0f;
    params.maskWidth = 0.0f;
    params.maskDry = 0.0f;
    processor.setParams(params);
    processor.reset();
    float minimumWet = 1.0f;
    float maximumWet = 0.0f;
    for (uint32_t node = 0u; node < 4u; ++node) {
        minimumWet = std::min(minimumWet, processor.nodeWetMask(node));
        maximumWet = std::max(maximumWet, processor.nodeWetMask(node));
    }
    std::array<std::array<float, frames>, channels> maskedOutput {};
    for (uint32_t ch = 0u; ch < channels; ++ch) {
        outputPointers[ch] = maskedOutput[ch].data();
    }
    processor.process(inputPointers.data(), outputPointers.data(),
        channels, channels, frames);
    double maskedEnergy = 0.0;
    double inputEnergy = 0.0;
    for (uint32_t ch = 0u; ch < channels; ++ch) {
        for (uint32_t frame = frames / 2u; frame < frames; ++frame) {
            maskedEnergy += static_cast<double>(maskedOutput[ch][frame])
                * maskedOutput[ch][frame];
            inputEnergy += static_cast<double>(input[ch][frame])
                * input[ch][frame];
        }
    }
    std::printf(
        "Ambi Effect pickup variation %.6g; meter %.6g; mask %.3f..%.3f; dry-zero energy %.6g/%.6g\n",
        difference, meterPeak, minimumWet, maximumWet,
        maskedEnergy, inputEnergy);
    return difference > 0.001
        && meterPeak > 0.001f
        && minimumWet < 0.30f
        && maximumWet > 0.99f
        && maskedEnergy < inputEnergy * 0.92;
}

bool pickupRelationshipCheck()
{
    float minimumFilter = 1.0f;
    float maximumFilter = 0.0f;
    float minimumRes = 1.0f;
    float maximumRes = 0.0f;
    for (uint32_t node = 0u; node < 12u; ++node) {
        const float sameFilter = s3g::ambiEffectPickupFilterPosition(
            0.5f, 0.0f, 0.0f, 0.0f, node, 12u);
        const float sameRes = s3g::ambiEffectPickupResonance(
            0.3f, 0.0f, 0.0f, 0.0f, node, 12u);
        if (std::fabs(sameFilter - 0.5f) > 1.0e-7f
            || std::fabs(sameRes - 0.3f) > 1.0e-7f) return false;
        const float variedFilter = s3g::ambiEffectPickupFilterPosition(
            0.5f, 0.0f, 0.8f, 0.6f, node, 12u);
        const float variedRes = s3g::ambiEffectPickupResonance(
            0.5f, 0.0f, 0.8f, 0.6f, node, 12u);
        minimumFilter = std::min(minimumFilter, variedFilter);
        maximumFilter = std::max(maximumFilter, variedFilter);
        minimumRes = std::min(minimumRes, variedRes);
        maximumRes = std::max(maximumRes, variedRes);
    }
    const float trimmedRes = s3g::ambiEffectPickupResonance(
        0.3f, 0.8f, 0.0f, 0.0f, 0u, 12u);
    const float softCurve = s3g::ambiEffectMaskExponent(0.35f, 0.0f);
    const float currentCurve = s3g::ambiEffectMaskExponent(0.35f, 0.5f);
    const float tightCurve = s3g::ambiEffectMaskExponent(0.35f, 1.0f);
    std::printf("Ambi Effect relationships filter %.3f..%.3f; res %.3f..%.3f; trim %.3f; mask exp %.3f/%.3f/%.3f\n",
        minimumFilter, maximumFilter, minimumRes, maximumRes, trimmedRes,
        softCurve, currentCurve, tightCurve);
    return maximumFilter - minimumFilter > 0.35f
        && maximumRes - minimumRes > 0.20f
        && trimmedRes > 0.69f
        && currentCurve > softCurve * 7.9f
        && tightCurve > currentCurve * 7.9f;
}

bool automationSafetyCheck()
{
    constexpr uint32_t frames = 128u;
    s3g::AmbiEffectDjFilter processor;
    processor.prepare(96000.0);
    std::array<std::array<double, frames>, kChannels> input {};
    std::array<std::array<double, frames>, kChannels> output {};
    std::array<double*, kChannels> inputPointers {};
    std::array<double*, kChannels> outputPointers {};
    for (uint32_t ch = 0u; ch < kChannels; ++ch) {
        inputPointers[ch] = input[ch].data();
        outputPointers[ch] = output[ch].data();
    }

    float peak = 0.0f;
    for (uint32_t block = 0u; block < 200u; ++block) {
        s3g::AmbiEffectDjFilterParams params {};
        params.order = 1u + block % 7u;
        params.body = static_cast<s3g::AmbiEffectBody>(block % 6u);
        params.topology = static_cast<s3g::AmbiEffectTopology>(block % 4u);
        params.filter = static_cast<float>(block % 21u) / 20.0f;
        params.resonance = static_cast<float>(block % 11u) / 10.0f;
        params.spread = static_cast<float>(block % 9u) / 8.0f;
        params.deviation = static_cast<float>(block % 7u) / 6.0f;
        params.topologyAmount = 1.0f;
        params.roamingRateHz = 2.0f;
        params.mix = 1.0f;
        for (uint32_t node = 0u;
            node < s3g::kAmbiEffectDjFilterMaxPickups; ++node) {
            params.pickupFilterTrim[node] = static_cast<float>(
                static_cast<int>((block + node) % 21u) - 10) / 10.0f;
            params.pickupResonanceTrim[node] = static_cast<float>(
                static_cast<int>((block + node * 3u) % 21u) - 10) / 10.0f;
        }
        params.maskAmount = static_cast<float>(block % 11u) / 10.0f;
        params.maskAzimuthDeg = -180.0f
            + static_cast<float>(block % 37u) * 10.0f;
        params.maskElevationDeg = -90.0f
            + static_cast<float>(block % 19u) * 10.0f;
        params.maskWidth = static_cast<float>(block % 11u) / 10.0f;
        params.maskCurve = static_cast<float>(block % 9u) / 8.0f;
        params.maskDry = static_cast<float>(block % 13u) / 12.0f;
        processor.setParams(params);
        for (uint32_t ch = 0u; ch < kChannels; ++ch) {
            for (uint32_t frame = 0u; frame < frames; ++frame) {
                input[ch][frame] = ch == 0u
                    ? std::sin(static_cast<double>(block * frames + frame)
                        * 0.071) * 0.2 : 0.0;
            }
        }
        processor.process(inputPointers.data(), outputPointers.data(),
            kChannels, kChannels, frames);
        for (uint32_t ch = 0u; ch < kChannels; ++ch) {
            for (double value : output[ch]) {
                if (!std::isfinite(value)) return false;
                peak = std::max(peak, static_cast<float>(std::fabs(value)));
            }
        }
    }
    std::printf("Ambi Effect DJ automation peak %.6g\n", peak);
    return peak < 4.0f;
}

} // namespace

int main()
{
    if (!aedConventionCheck()) {
        std::fprintf(stderr, "AED direction convention failed\n");
        return 1;
    }
    if (!bodyDefaultsCheck()) {
        std::fprintf(stderr, "Ambi Effect body defaults failed\n");
        return 1;
    }
    if (!openIdentityCheck()) return 1;
    if (!responseCheck()) {
        std::fprintf(stderr, "Ambi Effect DJ response failed\n");
        return 1;
    }
    if (!pickupVariationAndMaskCheck()) {
        std::fprintf(stderr,
            "Ambi Effect pickup variation or directional mask failed\n");
        return 1;
    }
    if (!pickupRelationshipCheck()) {
        std::fprintf(stderr, "Ambi Effect pickup relationships failed\n");
        return 1;
    }
    if (!automationSafetyCheck()) {
        std::fprintf(stderr, "Ambi Effect DJ automation safety failed\n");
        return 1;
    }
    return 0;
}
