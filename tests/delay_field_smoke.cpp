#include "s3g_delay_field.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

double outputEnergy(const std::array<std::vector<float>, 16u>& output,
                    uint32_t channels, uint32_t firstFrame = 0u)
{
    double result = 0.0;
    for (uint32_t channel = 0u; channel < channels; ++channel)
        for (uint32_t frame = firstFrame; frame < output[channel].size();
             ++frame)
            result += output[channel][frame] * output[channel][frame];
    return result;
}

} // namespace

int main()
{
    constexpr double sampleRate = 1000.0;
    constexpr uint32_t frames = 4000u;
    s3g::DelayField field;
    bool ok = field.prepare(sampleRate, frames);
    std::vector<float> left(frames);
    std::vector<float> right(frames);
    for (uint32_t frame = 0u; frame < frames; ++frame) {
        left[frame] = 0.2f * std::sin(0.047f * frame);
        right[frame] = 0.18f * std::cos(0.061f * frame);
    }
    std::array<std::vector<float>, 16u> output;
    std::array<float*, 16u> pointers {};
    for (uint32_t channel = 0u; channel < output.size(); ++channel) {
        output[channel].assign(frames, 0.0f);
        pointers[channel] = output[channel].data();
    }

    constexpr std::array<s3g::DelayFieldModel, 4u> models {{
        s3g::DelayFieldModel::Shard,
        s3g::DelayFieldModel::Orbit,
        s3g::DelayFieldModel::Cascade,
        s3g::DelayFieldModel::Iterate,
    }};
    constexpr std::array<s3g::FixedBusRingFormat, 4u> formats {{
        s3g::FixedBusRingFormat::StereoRing,
        s3g::FixedBusRingFormat::QuadRing,
        s3g::FixedBusRingFormat::Ring8,
        s3g::FixedBusRingFormat::Direct16,
    }};
    constexpr std::array<uint32_t, 4u> widths {{ 2u, 4u, 8u, 16u }};
    std::array<double, 4u> modelEnergies {};
    for (uint32_t index = 0u; index < models.size(); ++index) {
        auto params = field.params();
        params.model = models[index];
        params.format = formats[index];
        params.outputRotationDegrees = 17.0f;
        params.iterate.delayMs = 70.0f;
        params.iterate.windowMs = 110.0f;
        field.setParams(params);
        field.process(left.data(), right.data(), pointers.data(), frames);
        double energy = 0.0;
        for (uint32_t channel = 0u; channel < output.size(); ++channel) {
            for (float value : output[channel]) {
                ok = ok && std::isfinite(value);
                if (channel < widths[index]) energy += value * value;
                else ok = ok && value == 0.0f;
            }
        }
        modelEnergies[index] = energy;
        ok = ok && energy > 0.01;
    }

    // Direct sound is injected once after the 16-lane delay field, so a
    // stereo fold retains unity-oriented L/R level instead of summing sixteen
    // coherent dry copies.
    auto params = field.params();
    params.model = s3g::DelayFieldModel::Cascade;
    params.format = s3g::FixedBusRingFormat::StereoRing;
    params.outputRotationDegrees = 0.0f;
    params.cascade.dry = 1.0f;
    params.cascade.wet = 0.0f;
    field.setParams(params);
    field.reset();
    std::fill(left.begin(), left.end(), 0.2f);
    std::fill(right.begin(), right.end(), -0.15f);
    field.process(left.data(), right.data(), pointers.data(), frames);
    bool directRouting = true;
    for (uint32_t frame = 0u; frame < frames; ++frame) {
        directRouting = directRouting
            && std::abs(output[0u][frame] - left[frame]) < 1.0e-6f
            && std::abs(output[1u][frame] - right[frame]) < 1.0e-6f;
    }
    for (uint32_t channel = 2u; channel < output.size(); ++channel)
        for (float value : output[channel])
            directRouting = directRouting && value == 0.0f;
    ok = ok && directRouting;

    // Cascade must expose a full first tap and a clearly decaying copy on
    // every source-ring lane. This guards against the former window that
    // zeroed order zero and nearly erased the rest of the cascade.
    params.format = s3g::FixedBusRingFormat::Direct16;
    params.cascade.baseMs = 20.0f;
    params.cascade.stepMs = 30.0f;
    params.cascade.decay = 0.9f;
    params.cascade.damp = 0.0f;
    params.cascade.dry = 0.0f;
    params.cascade.wet = 1.0f;
    params.cascade.gainDb = 0.0f;
    params.cascade.soft = 0.0f;
    field.setParams(params);
    field.reset();
    std::fill(left.begin(), left.end(), 0.0f);
    std::fill(right.begin(), right.end(), 0.0f);
    left[0u] = 0.5f;
    right[0u] = 0.5f;
    field.process(left.data(), right.data(), pointers.data(), frames);
    float minimumCascadePeak = 1.0f;
    for (float peak : field.sourcePeaks())
        minimumCascadePeak = std::min(minimumCascadePeak, peak);
    const double directCascadeEnergy = outputEnergy(output, 16u, 10u);
    ok = ok && minimumCascadePeak > 0.005f
        && directCascadeEnergy > 0.05;

    params.format = s3g::FixedBusRingFormat::StereoRing;
    field.setParams(params);
    field.reset();
    field.process(left.data(), right.data(), pointers.data(), frames);
    const double foldedCascadeEnergy = outputEnergy(output, 2u, 10u);
    ok = ok && foldedCascadeEnergy > 0.05;

    if (!ok) {
        std::cerr << "direct=" << directRouting
                  << " cascade-min-peak=" << minimumCascadePeak
                  << " cascade-energy=" << directCascadeEnergy
                  << " folded-energy=" << foldedCascadeEnergy
                  << " model-energies=" << modelEnergies[0u] << ','
                  << modelEnergies[1u] << ',' << modelEnergies[2u] << ','
                  << modelEnergies[3u] << '\n';
        std::cerr << "Delay Field smoke failed\n";
        return 1;
    }
    std::cout << "Delay Field smoke passed\n";
    return 0;
}
