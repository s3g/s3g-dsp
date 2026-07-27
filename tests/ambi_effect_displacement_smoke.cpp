#include "s3g_ambi_effect_displacement.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>

namespace {

constexpr uint32_t kFrames = 256u;
constexpr uint32_t kChannels = s3g::kAmbiEffectDisplacementMaxChannels;

bool near(float a, float b, float tolerance = 0.0001f)
{
    return std::abs(a - b) <= tolerance;
}

} // namespace

int main()
{
    if (!near(s3g::ambiEffectDisplacementPlaybackPhase(
                  -0.25, s3g::AmbiEffectDisplacementPlaybackMode::Loop),
              0.75f)
        || !near(s3g::ambiEffectDisplacementPlaybackPhase(
                     1.25, s3g::AmbiEffectDisplacementPlaybackMode::Palindrome),
                 0.75f)
        || !near(s3g::ambiEffectDisplacementPlaybackPhase(
                     2.0, s3g::AmbiEffectDisplacementPlaybackMode::Once),
                 1.0f)) {
        std::cerr << "displacement playback phase mapping failed\n";
        return 1;
    }

    auto score = s3g::makeDefaultAmbiEffectDisplacementScore();
    score.sceneCount = 2u;
    score.scenes[1].time = 1.0f;
    for (uint32_t point = 0u;
        point < s3g::kAmbiEffectDisplacementScorePoints; ++point) {
        score.scenes[1].points[point] = score.source[point];
        score.scenes[1].points[point].azimuthDeg =
            s3g::wrapDisplacementDegrees(score.source[point].azimuthDeg
                + 38.0f + static_cast<float>(point % 3u) * 9.0f);
        score.scenes[1].points[point].elevationDeg = std::clamp(
            score.source[point].elevationDeg * 0.72f
                + static_cast<float>(static_cast<int>(point % 5u) - 2) * 3.0f,
            -90.0f, 90.0f);
        score.scenes[1].points[point].radius =
            0.55f + static_cast<float>(point % 8u) * 0.27f;
    }
    score = s3g::sanitizeAmbiEffectDisplacementScore(score);

    s3g::AmbiEffectDisplacementParams params;
    const struct BodyCase {
        uint32_t order;
        s3g::AmbiEffectBody requested;
        s3g::AmbiEffectBody resolved;
        uint32_t count;
    } bodyCases[] {
        { 1u, s3g::AmbiEffectBody::Auto, s3g::AmbiEffectBody::Icosa12, 12u },
        { 3u, s3g::AmbiEffectBody::Auto, s3g::AmbiEffectBody::Dodeca20, 20u },
        { 4u, s3g::AmbiEffectBody::Auto, s3g::AmbiEffectBody::Sphere24, 24u },
        { 7u, s3g::AmbiEffectBody::Icosa12, s3g::AmbiEffectBody::Icosa12, 12u },
        { 7u, s3g::AmbiEffectBody::Dodeca20, s3g::AmbiEffectBody::Dodeca20, 20u },
        { 7u, s3g::AmbiEffectBody::Sphere24, s3g::AmbiEffectBody::Sphere24, 24u },
    };
    for (const auto& bodyCase : bodyCases) {
        params.order = bodyCase.order;
        params.body = bodyCase.requested;
        const auto geometry = s3g::ambiEffectDisplacementBodyGeometry(
            score, params, 1.0, s3g::AmbiEffectDisplacementPlaybackMode::Once);
        if (s3g::resolveAmbiEffectBody(params.body, params.order)
                != bodyCase.resolved
            || geometry.count != bodyCase.count) {
            std::cerr << "order/body pickup resolution failed\n";
            return 1;
        }
        for (uint32_t point = 0u; point < geometry.count; ++point) {
            if (!std::isfinite(geometry.target[point].azimuthDeg)
                || !std::isfinite(geometry.target[point].elevationDeg)
                || !std::isfinite(geometry.target[point].radius)) {
                std::cerr << "resampled displacement geometry is not finite\n";
                return 1;
            }
        }
    }

    params.order = 7u;
    params.body = s3g::AmbiEffectBody::Sphere24;
    params.maskAmount = 1.0f;
    params.maskWidth = 0.16f;
    params.maskCurve = 0.85f;
    const auto maskedGeometry = s3g::ambiEffectDisplacementBodyGeometry(
        score, params, 1.0, s3g::AmbiEffectDisplacementPlaybackMode::Once);
    const auto [minimumMask, maximumMask] = std::minmax_element(
        maskedGeometry.wetMask.begin(),
        maskedGeometry.wetMask.begin() + maskedGeometry.count);
    if (*maximumMask < 0.85f || *minimumMask > 0.20f) {
        std::cerr << "directional displacement mask did not localize the field\n";
        return 1;
    }

    std::array<std::array<float, kFrames>, kChannels> input {};
    std::array<std::array<float, kFrames>, kChannels> output {};
    std::array<const float*, kChannels> inputs {};
    std::array<float*, kChannels> outputs {};
    for (uint32_t channel = 0u; channel < kChannels; ++channel) {
        inputs[channel] = input[channel].data();
        outputs[channel] = output[channel].data();
        for (uint32_t frame = 0u; frame < kFrames; ++frame) {
            input[channel][frame] = 0.018f
                * std::sin(static_cast<float>(frame) * 0.013f
                    * static_cast<float>(channel + 1u));
        }
    }

    s3g::AmbiEffectDisplacement displacement;
    displacement.prepare(48000.0);
    displacement.setScore(score);

    float worstIdentityError = 0.0f;
    float warpedPeak = 0.0f;
    for (const auto& bodyCase : bodyCases) {
        params = {};
        params.order = bodyCase.order;
        params.body = bodyCase.requested;
        params.amount = 0.0f;
        params.energy = 1.0f;
        displacement.setParams(params);
        displacement.reset();
        const uint32_t activeChannels = (params.order + 1u) * (params.order + 1u);
        displacement.processBlock(inputs.data(), outputs.data(), kChannels,
            kChannels, kFrames, 1.0, 0.0,
            s3g::AmbiEffectDisplacementPlaybackMode::Once);
        for (uint32_t channel = 0u; channel < activeChannels; ++channel) {
            for (uint32_t frame = 0u; frame < kFrames; ++frame) {
                worstIdentityError = std::max(worstIdentityError,
                    std::abs(output[channel][frame] - input[channel][frame]));
            }
        }
        if (worstIdentityError > 0.00025f) {
            std::cerr << "neutral displacement changed the field: "
                      << worstIdentityError << "\n";
            return 1;
        }

        params.amount = 1.0f;
        displacement.setParams(params);
        displacement.reset();
        float difference = 0.0f;
        for (uint32_t block = 0u; block < 3u; ++block) {
            displacement.processBlock(inputs.data(), outputs.data(), kChannels,
                kChannels, kFrames, 1.0, 0.0,
                s3g::AmbiEffectDisplacementPlaybackMode::Once);
        }
        for (uint32_t channel = 0u; channel < activeChannels; ++channel) {
            for (uint32_t frame = 0u; frame < kFrames; ++frame) {
                const float value = output[channel][frame];
                if (!std::isfinite(value)) {
                    std::cerr << "displacement produced non-finite output\n";
                    return 1;
                }
                warpedPeak = std::max(warpedPeak, std::abs(value));
                difference = std::max(
                    difference, std::abs(value - input[channel][frame]));
            }
        }
        if (difference <= 0.00001f) {
            std::cerr << "a pickup body did not displace the field\n";
            return 1;
        }
    }

    params = {};
    params.order = 7u;
    params.body = s3g::AmbiEffectBody::Sphere24;
    params.amount = 1.0f;
    params.distanceMode = s3g::AmbiEffectDisplacementDistanceMode::Physical;
    params.referenceDistanceMeters = 3.0f;
    displacement.setParams(params);
    displacement.reset();
    float physicalPeak = 0.0f;
    for (uint32_t block = 0u; block < 8u; ++block) {
        displacement.processBlock(inputs.data(), outputs.data(), kChannels,
            kChannels, kFrames, 1.0, 0.0,
            s3g::AmbiEffectDisplacementPlaybackMode::Once);
        for (uint32_t channel = 0u; channel < kChannels; ++channel) {
            for (float value : output[channel]) {
                if (!std::isfinite(value)) {
                    std::cerr << "physical displacement is not finite\n";
                    return 1;
                }
                physicalPeak = std::max(physicalPeak, std::abs(value));
            }
        }
    }
    if (physicalPeak <= 0.0001f
        || !near(s3g::ambiEffectDisplacementDistanceGain(0.25f), 2.0f)
        || !near(s3g::ambiEffectDisplacementDistanceGain(2.0f), 0.5f)) {
        std::cerr << "physical distance interpretation failed\n";
        return 1;
    }

    std::cout << "Ambi Effect Displacement smoke passed\n";
    std::cout << "identity/warp/physical: " << worstIdentityError << " / "
              << warpedPeak << " / " << physicalPeak << "\n";
    return 0;
}
