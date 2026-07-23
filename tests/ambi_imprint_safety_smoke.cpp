#include "s3g_ambi_imprint.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>

namespace {

bool fieldListenerDirectionCheck()
{
    s3g::AmbiFieldListener listener;
    listener.prepare(48000.0);
    listener.setMemorySeconds(0.08f);
    const std::array<s3g::Vec3, 2u> directions {
        s3g::directionFromAed(0.0f, 0.0f),
        s3g::directionFromAed(180.0f, 0.0f)
    };
    listener.setDirections(directions.data(), static_cast<uint32_t>(directions.size()));
    const auto field = s3g::acnSn3dBasis7(directions[0]);
    for (uint32_t frame = 0u; frame < 24000u; ++frame) {
        listener.processFrame(field.data(), 4u);
    }
    if (!(listener.envelope(0u) > listener.envelope(1u) * 1.5f)) {
        std::cerr << "Ambi field listener did not preserve directional contrast: "
                  << listener.envelope(0u) << " / " << listener.envelope(1u) << "\n";
        return false;
    }
    return true;
}

bool fieldListenModeCheck()
{
    constexpr double sampleRate = 48000.0;
    constexpr uint32_t frames = 256u;
    constexpr uint32_t channels = 4u;

    s3g::AmbiImprintDescriptor descriptor;
    descriptor.durationSeconds = 0.1f;
    for (float azimuth : { 0.0f, 180.0f }) {
        s3g::AmbiImprintProfile profile;
        profile.azimuthDeg = azimuth;
        profile.elevationDeg = 0.0f;
        profile.weight = 0.5f;
        profile.directGain = 1.0f;
        profile.late.durationSeconds = 0.1f;
        profile.late.level = 0.0f;
        descriptor.profiles.push_back(profile);
    }

    s3g::AmbiImprintProcessor processor;
    if (!processor.prepare(sampleRate, descriptor)) {
        std::cerr << "Ambi Imprint field-listen processor did not prepare\n";
        return false;
    }
    s3g::AmbiImprintParams params;
    params.order = 1u;
    params.mix = 1.0f;
    params.focus = 1.0f;
    params.width = 1.0f;

    std::array<std::array<float, frames>, channels> input {};
    std::array<std::array<float, frames>, channels> output {};
    std::array<float*, channels> inputs {};
    std::array<float*, channels> outputs {};
    for (uint32_t channel = 0u; channel < channels; ++channel) {
        inputs[channel] = input[channel].data();
        outputs[channel] = output[channel].data();
    }
    const auto sourceBasis =
        s3g::acnSn3dBasis7(s3g::directionFromAed(0.0f, 0.0f));

    auto runMode = [&](s3g::AmbiFieldListenMode mode) {
        params.fieldListenMode = mode;
        processor.setParams(params);
        processor.reset();
        uint64_t sample = 0u;
        for (uint32_t block = 0u; block < 320u; ++block) {
            for (uint32_t frame = 0u; frame < frames; ++frame, ++sample) {
                const float signal = 0.12f * std::sin(
                    2.0 * 3.14159265358979323846 * 220.0
                    * static_cast<double>(sample) / sampleRate);
                for (uint32_t channel = 0u; channel < channels; ++channel) {
                    input[channel][frame] = signal * sourceBasis[channel];
                }
            }
            processor.process(
                inputs.data(), channels, outputs.data(), channels, frames);
        }
        return std::array<float, 2u> {
            processor.fieldListenWeight(0u),
            processor.fieldListenWeight(1u)
        };
    };

    const auto off = runMode(s3g::AmbiFieldListenMode::Off);
    const auto follow = runMode(s3g::AmbiFieldListenMode::Follow);
    const auto counter = runMode(s3g::AmbiFieldListenMode::Counter);
    const auto balance = runMode(s3g::AmbiFieldListenMode::Balance);
    if (std::abs(off[0] - 1.0f) > 0.00001f
        || std::abs(off[1] - 1.0f) > 0.00001f) {
        std::cerr << "Ambi field listen Off altered profile weights: "
                  << off[0] << " / " << off[1] << "\n";
        return false;
    }
    if (!(follow[0] > follow[1] + 0.05f
          && counter[1] > counter[0] + 0.05f
          && balance[1] > balance[0] + 0.05f)) {
        std::cerr << "Ambi field listen modes did not produce distinct directional behavior\n"
                  << "follow " << follow[0] << " / " << follow[1]
                  << ", counter " << counter[0] << " / " << counter[1]
                  << ", balance " << balance[0] << " / " << balance[1] << "\n";
        return false;
    }
    return true;
}

} // namespace

int main()
{
    if (!fieldListenerDirectionCheck() || !fieldListenModeCheck()) return 1;

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
