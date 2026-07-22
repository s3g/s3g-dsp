#include "s3g_ambi_ray_bilocation_encoder.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

float renderPeak(s3g::AmbiRayBilocationEncoder& encoder, uint32_t frames, uint32_t impulseFrame)
{
    std::vector<float> input(frames, 0.0f);
    if (impulseFrame < frames) input[impulseFrame] = 0.5f;
    std::array<std::vector<float>, s3g::kAmbiRayMaxChannels> output;
    std::array<float*, s3g::kAmbiRayMaxChannels> pointers {};
    for (uint32_t channel = 0u; channel < output.size(); ++channel) {
        output[channel].assign(frames, 0.0f);
        pointers[channel] = output[channel].data();
    }
    encoder.process(input.data(), pointers.data(), static_cast<uint32_t>(pointers.size()), frames);
    float peak = 0.0f;
    for (const auto& channel : output) {
        for (float value : channel) {
            if (!std::isfinite(value)) return -1.0f;
            peak = std::max(peak, std::abs(value));
        }
    }
    return peak;
}

std::array<float, 4u> renderFirstOrderEnergy(s3g::AmbiRayBilocationEncoder& encoder, uint32_t frames)
{
    std::vector<float> input(frames, 0.0f);
    input[0] = 0.5f;
    std::array<std::vector<float>, s3g::kAmbiRayMaxChannels> output;
    std::array<float*, s3g::kAmbiRayMaxChannels> pointers {};
    for (uint32_t channel = 0u; channel < output.size(); ++channel) {
        output[channel].assign(frames, 0.0f);
        pointers[channel] = output[channel].data();
    }
    encoder.process(input.data(), pointers.data(), static_cast<uint32_t>(pointers.size()), frames);
    std::array<float, 4u> energy {};
    for (uint32_t channel = 0u; channel < energy.size(); ++channel)
        for (float value : output[channel]) energy[channel] += std::abs(value);
    return energy;
}

} // namespace

int main()
{
    const auto hardA = s3g::ambiRayBilocationGains(0.25f, 0.0f);
    const auto hardB = s3g::ambiRayBilocationGains(0.75f, 0.0f);
    const auto permeable = s3g::ambiRayBilocationGains(0.25f, 1.0f);
    if (hardA.a < 0.999f || hardA.b > 0.001f || hardB.a > 0.001f || hardB.b < 0.999f
        || permeable.a < 0.1f || permeable.b < 0.1f) {
        std::cerr << "Bilocation membership topology failed\n";
        return 1;
    }

    const s3g::Vec3 source { 0.2f, 0.3f, 0.4f };
    const auto mirrored = s3g::mapAmbiRayBilocationSource(source, s3g::AmbiRayBilocationMapMode::MirrorX);
    const auto counter = s3g::mapAmbiRayBilocationSource(source, s3g::AmbiRayBilocationMapMode::Counter);
    if (std::abs(mirrored.x - 0.8f) > 0.0001f || std::abs(mirrored.y - 0.3f) > 0.0001f
        || std::abs(counter.x - 0.8f) > 0.0001f || std::abs(counter.y - 0.7f) > 0.0001f
        || std::abs(counter.z - 0.6f) > 0.0001f) {
        std::cerr << "Bilocation position mapping failed\n";
        return 1;
    }

    s3g::AmbiRayBilocationEncoder encoder;
    auto roomA = s3g::makeDefaultAmbiRayDescriptor();
    auto roomB = roomA;
    roomB.durationSeconds = 4.0f;
    for (auto& cell : roomB.cells) {
        cell.late.decaySeconds = 3.0f;
        cell.late.damping = 0.75f;
    }
    if (!encoder.prepare(48000.0, 256u, roomA, roomB)) {
        std::cerr << "Bilocation encoder did not prepare\n";
        return 1;
    }
    s3g::AmbiRayBilocationParams params;
    params.order = 3u;
    params.place = 0.5f;
    params.permeability = 1.0f;
    params.separationDeg = 120.0f;
    params.outputGainDb = -12.0f;
    encoder.setParams(params);
    encoder.reset();
    const float peak = renderPeak(encoder, 8192u, 0u);
    if (!(peak > 0.0001f) || peak > 0.981f) {
        std::cerr << "Bilocation render/safety failed: " << peak << "\n";
        return 1;
    }
    if (encoder.tailFrames() <= 48000u) {
        std::cerr << "Bilocation tail does not include dual room memory\n";
        return 1;
    }

    params.place = 0.0f;
    params.permeability = 0.0f;
    params.direct = 1.0f;
    params.early = 0.0f;
    params.late = 0.0f;
    params.separationDeg = 0.0f;
    encoder.setParams(params);
    encoder.reset();
    const auto centredEnergy = renderFirstOrderEnergy(encoder, 4096u);
    params.separationDeg = 180.0f;
    encoder.setParams(params);
    encoder.reset();
    const auto separatedEnergy = renderFirstOrderEnergy(encoder, 4096u);
    if (!(centredEnergy[3] > centredEnergy[1] * 4.0f)
        || !(separatedEnergy[1] > separatedEnergy[3] * 4.0f)) {
        std::cerr << "Bilocation ambisonic separation rotation failed: "
                  << centredEnergy[1] << " / " << centredEnergy[3] << " -> "
                  << separatedEnergy[1] << " / " << separatedEnergy[3] << "\n";
        return 1;
    }

    std::cout << "Ambi Ray Bilocation Encoder smoke passed\n";
    return 0;
}
