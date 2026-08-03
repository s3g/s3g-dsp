#include "s3g_ambi_horizon_encoder.h"
#include "s3g_ambi_horizon_presets.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

constexpr uint32_t kFrames = 4096u;

using Buffer = std::array<std::vector<float>, s3g::kAmbiHorizonMaxChannels>;

Buffer render(s3g::AmbiHorizonEncoder& encoder, uint32_t channels)
{
    Buffer buffer {};
    std::array<float*, s3g::kAmbiHorizonMaxChannels> pointers {};
    for (uint32_t channel = 0u; channel < channels; ++channel) {
        buffer[channel].resize(kFrames);
        pointers[channel] = buffer[channel].data();
    }
    encoder.processBlock(pointers.data(), channels, kFrames);
    return buffer;
}

float absoluteEnergy(const Buffer& buffer, uint32_t channels)
{
    float energy = 0.0f;
    for (uint32_t channel = 0u; channel < channels; ++channel) {
        for (const float sample : buffer[channel]) {
            assert(std::isfinite(sample));
            energy += std::abs(sample);
        }
    }
    return energy;
}

float channelRms(const std::vector<float>& channel)
{
    double energy = 0.0;
    for (const float sample : channel) energy += static_cast<double>(sample) * sample;
    return channel.empty() ? 0.0f
        : static_cast<float>(std::sqrt(energy / static_cast<double>(channel.size())));
}

} // namespace

int main()
{
    s3g::AmbiHorizonEncoder encoder;
    encoder.prepare(48000.0);

    auto params = s3g::ambiHorizonFactoryPreset(2u);
    params.order = 7u;
    params.outputGainDb = -12.0f;
    encoder.setParams(params);
    assert(encoder.activeChannels() == 64u);
    assert(encoder.activeEntities() == params.entities);

    const auto first = render(encoder, 64u);
    const float energy = absoluteEnergy(first, 64u);
    assert(energy > 0.001f);

    bool hasLocal = false;
    bool hasBed = false;
    bool hasSignal = false;
    for (uint32_t index = 0u; index < encoder.activeEntities(); ++index) {
        const auto point = encoder.entityTelemetry(index);
        assert(std::isfinite(point.azimuthDeg));
        assert(std::isfinite(point.elevationDeg));
        assert(point.rangeNorm >= 0.0f && point.rangeNorm <= 1.0f);
        hasLocal |= point.layer == s3g::AmbiHorizonLayer::LocalFloor;
        hasBed |= point.layer == s3g::AmbiHorizonLayer::HorizonBed;
        hasSignal |= point.layer == s3g::AmbiHorizonLayer::HorizonSignal;
    }
    assert(hasLocal && hasBed && hasSignal);

    // Reset must reproduce the same autonomous scene exactly.
    encoder.reset();
    const auto resetA = render(encoder, 64u);
    encoder.reset();
    const auto resetB = render(encoder, 64u);
    for (uint32_t channel = 0u; channel < 64u; ++channel) {
        assert(resetA[channel] == resetB[channel]);
    }

    // Parameter sanitation and factory-preset coverage.
    params.order = 99u;
    params.entities = 1u;
    params.rangeKm = 1000.0f;
    params.air = -2.0f;
    params.outputGainDb = 100.0f;
    encoder.setParams(params);
    const auto clean = encoder.params();
    assert(clean.order == 7u);
    assert(clean.entities == 4u);
    assert(clean.rangeKm == 20.0f);
    assert(clean.air == 0.0f);
    assert(clean.outputGainDb == 12.0f);

    for (uint32_t preset = 0u;
         preset < s3g::kAmbiHorizonFactoryPresetCount; ++preset) {
        const auto candidate = s3g::ambiHorizonFactoryPreset(preset);
        assert(candidate.entities >= 4u
            && candidate.entities <= s3g::kAmbiHorizonMaxEntities);
        assert(candidate.rangeKm >= 0.03f && candidate.rangeKm <= 20.0f);
        assert(s3g::kAmbiHorizonPresetInfo[preset].name[0] != '\0');
        encoder.setParams(candidate);
        encoder.reset();
        const auto scene = render(encoder, encoder.activeChannels());
        assert(absoluteEnergy(scene, encoder.activeChannels()) > 0.0001f);
        if (preset == 0u) {
            const float startupRms = channelRms(scene[0]);
            assert(startupRms > 1.0e-5f);
            std::cout << "Horizon default W-channel RMS: "
                      << startupRms << '\n';
        }
    }

    return 0;
}
