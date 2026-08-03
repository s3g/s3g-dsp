#include "s3g_ambi_group_rotate.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

uint32_t nextRandom(uint32_t& state)
{
    state ^= state << 13u;
    state ^= state >> 17u;
    state ^= state << 5u;
    return state;
}

float randomUnit(uint32_t& state)
{
    return static_cast<float>(nextRandom(state) >> 8u)
        * (1.0f / 16777216.0f);
}

float randomRange(uint32_t& state, float low, float high)
{
    return low + (high - low) * randomUnit(state);
}

template <uint32_t Groups>
bool runEquivalence(uint32_t& randomState)
{
    constexpr uint32_t channels = Groups * s3g::kAmbiGroupRotateGroupChannels;
    constexpr uint32_t maximumFrames = 257u;
    using Storage = std::array<std::array<float, maximumFrames>, channels>;

    Storage inputs {};
    Storage groupedOutputs {};
    Storage referenceOutputs {};
    std::array<float*, channels> inputPointers {};
    std::array<float*, channels> groupedPointers {};
    std::array<float*, channels> referencePointers {};
    for (uint32_t channel = 0u; channel < channels; ++channel) {
        inputPointers[channel] = inputs[channel].data();
        groupedPointers[channel] = groupedOutputs[channel].data();
        referencePointers[channel] = referenceOutputs[channel].data();
    }

    s3g::AmbiGroupRotateProcessor<Groups> grouped;
    std::array<s3g::AmbiRotate3Processor, Groups> references {};
    for (uint32_t trial = 0u; trial < 64u; ++trial) {
        s3g::AmbiGroupRotateParams params {};
        params.yawDeg = randomRange(randomState, -540.0f, 540.0f);
        params.pitchDeg = randomRange(randomState, -130.0f, 130.0f);
        params.rollDeg = randomRange(randomState, -360.0f, 360.0f);
        params.spread = randomRange(randomState, -1.4f, 1.4f);
        params.tilt = randomRange(randomState, -1.4f, 1.4f);
        params.twist = randomRange(randomState, -1.4f, 1.4f);
        params.width = randomRange(randomState, -0.3f, 1.8f);
        params.outputGainDb = randomRange(randomState, -72.0f, 18.0f);
        const uint32_t frames = 1u + nextRandom(randomState) % maximumFrames;

        for (uint32_t channel = 0u; channel < channels; ++channel) {
            for (uint32_t frame = 0u; frame < frames; ++frame) {
                inputs[channel][frame] = randomRange(randomState, -0.8f, 0.8f);
                groupedOutputs[channel][frame] = 9.0f;
                referenceOutputs[channel][frame] = -9.0f;
            }
        }

        const auto sanitizedParams = s3g::sanitizeAmbiGroupRotateParams(params);
        grouped.setParams(params);
        grouped.reset();
        grouped.process(inputPointers.data(), channels,
            groupedPointers.data(), channels, frames);

        for (uint32_t group = 0u; group < Groups; ++group) {
            references[group].setParams(
                s3g::ambiGroupRotateParamsForGroup(sanitizedParams, group, Groups));
            references[group].reset();
            const uint32_t base = group * s3g::kAmbiGroupRotateGroupChannels;
            references[group].process(inputPointers.data() + base,
                referencePointers.data() + base,
                s3g::kAmbiGroupRotateGroupChannels,
                s3g::kAmbiGroupRotateGroupChannels,
                frames);
        }

        for (uint32_t channel = 0u; channel < channels; ++channel) {
            for (uint32_t frame = 0u; frame < frames; ++frame) {
                const float groupedValue = groupedOutputs[channel][frame];
                const float referenceValue = referenceOutputs[channel][frame];
                if (!std::isfinite(groupedValue)
                    || std::fabs(groupedValue - referenceValue) > 1.0e-6f) {
                    std::cerr << Groups << "-group rotation mismatch at trial "
                              << trial << ", channel " << channel
                              << ", frame " << frame << ": "
                              << groupedValue << " != " << referenceValue << "\n";
                    return false;
                }
            }
        }
    }
    return true;
}

} // namespace

int main()
{
    uint32_t randomState = 0x73a4d92bu;
    if (!runEquivalence<4u>(randomState)
        || !runEquivalence<8u>(randomState)) {
        return 1;
    }
    std::cout << "Ambi Group Rotate randomized equivalence passed\n";
    return 0;
}
