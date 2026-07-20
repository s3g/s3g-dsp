#include "s3g_ambi_imprint.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>

int main()
{
    constexpr double sampleRate = 48000.0;
    constexpr uint32_t frames = 256u;
    constexpr uint32_t channels = 4u;

    s3g::AmbiImprintDescriptor descriptor;
    descriptor.durationSeconds = 0.2f;
    s3g::AmbiImprintProfile profile;
    profile.weight = 1.0f;
    profile.directGain = 1.0f;
    profile.late.durationSeconds = 0.2f;
    profile.late.level = 0.0f;
    for (uint32_t tap = 0u; tap < 128u; ++tap) {
        s3g::AmbiImprintReflection reflection;
        reflection.delayMs = 2.0f + static_cast<float>(tap);
        reflection.gain = 1.0f;
        profile.earlyReflections.push_back(reflection);
    }
    descriptor.profiles.push_back(profile);

    s3g::AmbiImprintProcessor processor;
    if (!processor.prepare(sampleRate, descriptor)) {
        std::cerr << "Ambi Imprint safety processor did not prepare\n";
        return 1;
    }
    const float expectedReflectionScale = 1.0f / 128.0f;
    if (std::abs(processor.directTapScale() - 1.0f) > 0.00001f
        || std::abs(processor.reflectionTapScale() - expectedReflectionScale) > 0.00001f) {
        std::cerr << "Ambi Imprint sparse response budget failed: "
                  << processor.directTapScale() << " / " << processor.reflectionTapScale() << "\n";
        return 1;
    }

    s3g::AmbiImprintParams params;
    params.order = 1u;
    params.mix = 1.0f;
    params.focus = 0.0f;
    params.outputGainDb = 0.0f;
    processor.setParams(params);
    processor.reset();

    std::array<std::array<float, frames>, channels> input {};
    std::array<std::array<float, frames>, channels> output {};
    std::array<float*, channels> inputs {};
    std::array<float*, channels> outputs {};
    for (uint32_t channel = 0u; channel < channels; ++channel) {
        inputs[channel] = input[channel].data();
        outputs[channel] = output[channel].data();
    }

    float echoPeak = 0.0f;
    uint64_t sample = 0u;
    for (uint32_t block = 0u; block < 300u; ++block) {
        for (uint32_t frame = 0u; frame < frames; ++frame, ++sample) {
            input[0][frame] = 0.95f * std::sin(
                2.0 * 3.14159265358979323846 * 30.0 * static_cast<double>(sample) / sampleRate);
            for (uint32_t channel = 1u; channel < channels; ++channel) input[channel][frame] = 0.0f;
        }
        processor.process(inputs.data(), channels, outputs.data(), channels, frames);
        for (const auto& channel : output) {
            for (float value : channel) {
                if (!std::isfinite(value)) {
                    std::cerr << "Ambi Imprint dense echo produced non-finite output\n";
                    return 1;
                }
                echoPeak = std::max(echoPeak, std::abs(value));
            }
        }
    }
    if (echoPeak > s3g::kAmbiImprintSafetyCeiling + 0.001f) {
        std::cerr << "Ambi Imprint linked wet limiter exceeded its ceiling: " << echoPeak << "\n";
        return 1;
    }

    processor.reset();
    float finalDcPeak = 0.0f;
    for (uint32_t block = 0u; block < 400u; ++block) {
        input[0].fill(1.0f);
        for (uint32_t channel = 1u; channel < channels; ++channel) input[channel].fill(0.0f);
        processor.process(inputs.data(), channels, outputs.data(), channels, frames);
        if (block >= 396u) {
            for (const auto& channel : output) {
                for (float value : channel) finalDcPeak = std::max(finalDcPeak, std::abs(value));
            }
        }
    }
    if (!std::isfinite(finalDcPeak) || finalDcPeak > 0.002f) {
        std::cerr << "Ambi Imprint wet high-pass did not reject DC: " << finalDcPeak << "\n";
        return 1;
    }

    params.mix = 0.0f;
    processor.setParams(params);
    processor.reset();
    float dryImpulsePeak = 0.0f;
    for (uint32_t block = 0u; block < 6u; ++block) {
        for (auto& channel : input) channel.fill(0.0f);
        if (block == 0u) input[0][0] = 1.0f;
        processor.process(inputs.data(), channels, outputs.data(), channels, frames);
        for (const auto& channel : output) {
            for (float value : channel) dryImpulsePeak = std::max(dryImpulsePeak, std::abs(value));
        }
    }
    if (std::abs(dryImpulsePeak - 1.0f) > 0.0001f) {
        std::cerr << "Ambi Imprint wet protection changed the dry path: " << dryImpulsePeak << "\n";
        return 1;
    }

    std::cout << "Ambi Imprint safety smoke passed\n";
    std::cout << "reflection scale / echo peak / DC tail: "
              << processor.reflectionTapScale() << " / " << echoPeak << " / " << finalDcPeak << "\n";
    return 0;
}
