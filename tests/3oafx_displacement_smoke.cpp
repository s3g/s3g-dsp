#include "s3g_3oafx_displacement.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>

namespace {

bool near(float a, float b, float tolerance = 0.0001f)
{
    return std::abs(a - b) <= tolerance;
}

} // namespace

int main()
{
    if (!near(s3g::threeOafxDisplacementPlaybackPhase(-0.25, s3g::ThreeOafxDisplacementPlaybackMode::Loop), 0.75f)
        || !near(s3g::threeOafxDisplacementPlaybackPhase(1.25, s3g::ThreeOafxDisplacementPlaybackMode::Palindrome), 0.75f)
        || !near(s3g::threeOafxDisplacementPlaybackPhase(1.75, s3g::ThreeOafxDisplacementPlaybackMode::Palindrome), 0.25f)) {
        std::cerr << "loop/palindrome playback phase mapping failed\n";
        return 1;
    }

    constexpr uint32_t frames = 256u;
    s3g::ThreeOafxDisplacement displacement;
    displacement.prepare(48000.0);

    auto score = s3g::makeDefaultThreeOafxDisplacementScore();
    score.sceneCount = 2u;
    score.scenes[1].time = 1.0f;
    for (uint32_t point = 0u; point < s3g::k3OafxVirtualSpeakers; ++point) {
        score.scenes[1].points[point] = score.source[point];
        score.scenes[1].points[point].azimuthDeg = s3g::wrapDisplacementDegrees(
            score.source[point].azimuthDeg + 38.0f + static_cast<float>(point % 3u) * 9.0f);
        score.scenes[1].points[point].elevationDeg = std::clamp(
            score.source[point].elevationDeg * 0.72f + static_cast<float>(static_cast<int>(point % 5u) - 2) * 3.0f,
            -90.0f,
            90.0f);
        score.scenes[1].points[point].radius = 0.55f + static_cast<float>(point % 8u) * 0.27f;
    }
    displacement.setScore(score);

    std::array<std::array<float, frames>, s3g::k3OaChannels> input {};
    std::array<std::array<float, frames>, s3g::k3OaChannels> output {};
    std::array<const float*, s3g::k3OaChannels> inputs {};
    std::array<float*, s3g::k3OaChannels> outputs {};
    for (uint32_t channel = 0u; channel < s3g::k3OaChannels; ++channel) {
        inputs[channel] = input[channel].data();
        outputs[channel] = output[channel].data();
        for (uint32_t frame = 0u; frame < frames; ++frame) {
            input[channel][frame] = 0.025f
                * std::sin(static_cast<float>(frame) * 0.019f * static_cast<float>(channel + 1u));
        }
    }

    s3g::ThreeOafxDisplacementParams params;
    params.amount = 0.0f;
    params.energy = 1.0f;
    params.outputGainDb = 0.0f;
    params.distanceMode = s3g::ThreeOafxDisplacementDistanceMode::Gain;
    displacement.setParams(params);
    displacement.reset();
    displacement.processBlock(inputs.data(), outputs.data(), s3g::k3OaChannels, s3g::k3OaChannels,
                              frames, 0.5, 0.0, s3g::ThreeOafxDisplacementPlaybackMode::Once);

    float identityError = 0.0f;
    for (uint32_t channel = 0u; channel < s3g::k3OaChannels; ++channel) {
        for (uint32_t frame = 0u; frame < frames; ++frame) {
            identityError = std::max(identityError, std::abs(output[channel][frame] - input[channel][frame]));
        }
    }
    if (identityError > 0.0002f) {
        std::cerr << "neutral round trip changed the field: " << identityError << "\n";
        return 1;
    }

    params.amount = 1.0f;
    displacement.setParams(params);
    displacement.reset();
    for (uint32_t block = 0u; block < 3u; ++block) {
        displacement.processBlock(inputs.data(), outputs.data(), s3g::k3OaChannels, s3g::k3OaChannels,
                                  frames, 1.0, 0.0, s3g::ThreeOafxDisplacementPlaybackMode::Once);
    }
    float gainPeak = 0.0f;
    float warpDifference = 0.0f;
    for (uint32_t channel = 0u; channel < s3g::k3OaChannels; ++channel) {
        for (uint32_t frame = 0u; frame < frames; ++frame) {
            const float value = output[channel][frame];
            if (!std::isfinite(value)) {
                std::cerr << "gain mode produced non-finite output\n";
                return 1;
            }
            gainPeak = std::max(gainPeak, std::abs(value));
            warpDifference = std::max(warpDifference, std::abs(value - input[channel][frame]));
        }
    }
    if (gainPeak <= 0.0001f || warpDifference <= 0.0001f) {
        std::cerr << "gain mode did not warp the field\n";
        return 1;
    }

    params.distanceMode = s3g::ThreeOafxDisplacementDistanceMode::Physical;
    params.referenceDistanceMeters = 3.0f;
    displacement.setParams(params);
    displacement.reset();
    float physicalPeak = 0.0f;
    float physicalDifference = 0.0f;
    for (uint32_t block = 0u; block < 8u; ++block) {
        displacement.processBlock(inputs.data(), outputs.data(), s3g::k3OaChannels, s3g::k3OaChannels,
                                  frames, 1.0, 0.0, s3g::ThreeOafxDisplacementPlaybackMode::Once);
        for (uint32_t channel = 0u; channel < s3g::k3OaChannels; ++channel) {
            for (uint32_t frame = 0u; frame < frames; ++frame) {
                const float value = output[channel][frame];
                if (!std::isfinite(value)) {
                    std::cerr << "physical mode produced non-finite output\n";
                    return 1;
                }
                physicalPeak = std::max(physicalPeak, std::abs(value));
                physicalDifference = std::max(physicalDifference, std::abs(value - input[channel][frame]));
            }
        }
    }
    if (physicalPeak <= 0.0001f || physicalDifference <= 0.0001f
        || !near(s3g::threeOafxDisplacementDistanceGain(0.25f), 2.0f)
        || !near(s3g::threeOafxDisplacementDistanceGain(2.0f), 0.5f)) {
        std::cerr << "physical distance interpretation failed\n";
        return 1;
    }

    std::cout << "3OAFX displacement smoke passed\n";
    std::cout << "identity/gain/physical: " << identityError << " / " << gainPeak << " / " << physicalPeak << "\n";
    return 0;
}
