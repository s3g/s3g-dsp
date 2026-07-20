#pragma once

#include "s3g_3oafx.h"
#include "s3g_ambisonic_speaker_decoder.h"
#include "s3g_math.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace s3g {

constexpr uint32_t k3OafxDisplacementMaxScenes = 64u;

enum class ThreeOafxDisplacementDistanceMode : uint32_t {
    Gain = 0,
    Physical = 1,
};

enum class ThreeOafxDisplacementPlaybackMode : uint32_t {
    Loop = 0,
    Palindrome = 1,
    Once = 2,
};

struct ThreeOafxDisplacementPoint {
    float azimuthDeg = 0.0f;
    float elevationDeg = 0.0f;
    float radius = 1.0f;
};

struct ThreeOafxDisplacementScene {
    float time = 0.0f;
    std::array<ThreeOafxDisplacementPoint, k3OafxVirtualSpeakers> points {};
};

struct ThreeOafxDisplacementScore {
    uint32_t sceneCount = 0u;
    float durationSeconds = 16.0f;
    std::array<ThreeOafxDisplacementPoint, k3OafxVirtualSpeakers> source {};
    std::array<ThreeOafxDisplacementScene, k3OafxDisplacementMaxScenes> scenes {};
};

struct ThreeOafxDisplacementParams {
    float amount = 0.65f;
    float azimuthScale = 1.0f;
    float elevationScale = 1.0f;
    float radiusScale = 1.0f;
    ThreeOafxDisplacementDistanceMode distanceMode = ThreeOafxDisplacementDistanceMode::Gain;
    float referenceDistanceMeters = 2.0f;
    float energy = 0.65f;
    float outputGainDb = 0.0f;
    bool bypass = false;
};

inline float wrapDisplacementDegrees(float value)
{
    while (value > 180.0f) value -= 360.0f;
    while (value <= -180.0f) value += 360.0f;
    return value;
}

inline ThreeOafxDisplacementPoint displacementPointFromVector(Vec3 point)
{
    point = normalize(point);
    const float horizontal = std::sqrt(point.x * point.x + point.y * point.y);
    return {
        wrapDisplacementDegrees(std::atan2(point.y, point.x) * 180.0f / kPi),
        clamp(std::atan2(point.z, horizontal) * 180.0f / kPi, -90.0f, 90.0f),
        1.0f,
    };
}

inline ThreeOafxDisplacementScore makeDefaultThreeOafxDisplacementScore()
{
    ThreeOafxDisplacementScore score {};
    score.sceneCount = 1u;
    score.durationSeconds = 16.0f;
    for (uint32_t point = 0; point < k3OafxVirtualSpeakers; ++point) {
        score.source[point] = displacementPointFromVector(k3OafxPoints[point]);
        score.scenes[0].points[point] = score.source[point];
    }
    score.scenes[0].time = 0.0f;
    return score;
}

inline ThreeOafxDisplacementPoint sanitizeThreeOafxDisplacementPoint(ThreeOafxDisplacementPoint point)
{
    if (!std::isfinite(point.azimuthDeg)) point.azimuthDeg = 0.0f;
    if (!std::isfinite(point.elevationDeg)) point.elevationDeg = 0.0f;
    if (!std::isfinite(point.radius)) point.radius = 1.0f;
    point.azimuthDeg = wrapDisplacementDegrees(point.azimuthDeg);
    point.elevationDeg = clamp(point.elevationDeg, -90.0f, 90.0f);
    point.radius = clamp(point.radius, 0.05f, 3.0f);
    return point;
}

inline ThreeOafxDisplacementScore sanitizeThreeOafxDisplacementScore(ThreeOafxDisplacementScore score)
{
    if (score.sceneCount == 0u) return makeDefaultThreeOafxDisplacementScore();
    score.sceneCount = std::min<uint32_t>(score.sceneCount, k3OafxDisplacementMaxScenes);
    if (!std::isfinite(score.durationSeconds)) score.durationSeconds = 16.0f;
    score.durationSeconds = clamp(score.durationSeconds, 0.05f, 86400.0f);
    for (auto& point : score.source) point = sanitizeThreeOafxDisplacementPoint(point);
    for (uint32_t scene = 0; scene < score.sceneCount; ++scene) {
        auto& item = score.scenes[scene];
        if (!std::isfinite(item.time)) item.time = 0.0f;
        item.time = clamp(item.time, 0.0f, 1.0f);
        for (auto& point : item.points) point = sanitizeThreeOafxDisplacementPoint(point);
    }
    std::sort(score.scenes.begin(), score.scenes.begin() + score.sceneCount,
              [](const auto& a, const auto& b) { return a.time < b.time; });
    score.scenes[0].time = 0.0f;
    return score;
}

inline ThreeOafxDisplacementParams sanitizeThreeOafxDisplacementParams(ThreeOafxDisplacementParams params)
{
    params.amount = clamp(params.amount, 0.0f, 1.0f);
    params.azimuthScale = clamp(params.azimuthScale, 0.0f, 2.0f);
    params.elevationScale = clamp(params.elevationScale, 0.0f, 2.0f);
    params.radiusScale = clamp(params.radiusScale, 0.0f, 2.0f);
    params.distanceMode = static_cast<ThreeOafxDisplacementDistanceMode>(
        std::min<uint32_t>(static_cast<uint32_t>(params.distanceMode), 1u));
    params.referenceDistanceMeters = clamp(params.referenceDistanceMeters, 0.5f, 10.0f);
    params.energy = clamp(params.energy, 0.0f, 1.0f);
    params.outputGainDb = clamp(params.outputGainDb, -60.0f, 12.0f);
    return params;
}

inline float threeOafxDisplacementPlaybackPhase(double phase, ThreeOafxDisplacementPlaybackMode mode)
{
    if (!std::isfinite(phase)) return 0.0f;
    switch (mode) {
    case ThreeOafxDisplacementPlaybackMode::Palindrome: {
        double wrapped = std::fmod(phase, 2.0);
        if (wrapped < 0.0) wrapped += 2.0;
        return static_cast<float>(wrapped <= 1.0 ? wrapped : 2.0 - wrapped);
    }
    case ThreeOafxDisplacementPlaybackMode::Once:
        return static_cast<float>(std::clamp(phase, 0.0, 1.0));
    case ThreeOafxDisplacementPlaybackMode::Loop:
    default: {
        double wrapped = phase - std::floor(phase);
        if (wrapped < 0.0) wrapped += 1.0;
        return static_cast<float>(wrapped);
    }
    }
}

inline std::array<ThreeOafxDisplacementPoint, k3OafxVirtualSpeakers>
threeOafxDisplacementGeometry(const ThreeOafxDisplacementScore& score,
                             const ThreeOafxDisplacementParams& params,
                             double phase,
                             ThreeOafxDisplacementPlaybackMode playback)
{
    std::array<ThreeOafxDisplacementPoint, k3OafxVirtualSpeakers> geometry = score.source;
    if (score.sceneCount == 0u) return geometry;

    const float time = threeOafxDisplacementPlaybackPhase(phase, playback);
    uint32_t first = 0u;
    uint32_t second = 0u;
    float blend = 0.0f;
    if (time >= score.scenes[score.sceneCount - 1u].time) {
        first = second = score.sceneCount - 1u;
    } else {
        for (uint32_t scene = 0u; scene + 1u < score.sceneCount; ++scene) {
            if (time <= score.scenes[scene + 1u].time) {
                first = scene;
                second = scene + 1u;
                const float span = std::max(0.000001f, score.scenes[second].time - score.scenes[first].time);
                const float u = clamp((time - score.scenes[first].time) / span, 0.0f, 1.0f);
                blend = u * u * (3.0f - 2.0f * u);
                break;
            }
        }
    }

    for (uint32_t point = 0u; point < k3OafxVirtualSpeakers; ++point) {
        const auto& source = score.source[point];
        const auto& a = score.scenes[first].points[point];
        const auto& b = score.scenes[second].points[point];
        ThreeOafxDisplacementPoint target {};
        target.azimuthDeg = wrapDisplacementDegrees(
            a.azimuthDeg + wrapDisplacementDegrees(b.azimuthDeg - a.azimuthDeg) * blend);
        target.elevationDeg = a.elevationDeg + (b.elevationDeg - a.elevationDeg) * blend;
        target.radius = a.radius + (b.radius - a.radius) * blend;

        geometry[point].azimuthDeg = wrapDisplacementDegrees(
            source.azimuthDeg
            + wrapDisplacementDegrees(target.azimuthDeg - source.azimuthDeg)
                * params.azimuthScale * params.amount);
        geometry[point].elevationDeg = clamp(
            source.elevationDeg
                + (target.elevationDeg - source.elevationDeg) * params.elevationScale * params.amount,
            -90.0f,
            90.0f);
        geometry[point].radius = clamp(
            source.radius + (target.radius - source.radius) * params.radiusScale * params.amount,
            0.05f,
            3.0f);
    }
    return geometry;
}

inline float threeOafxDisplacementDistanceGain(float radius)
{
    return clamp(1.0f / std::max(0.05f, radius), 0.25f, 2.0f);
}

class ThreeOafxDisplacement {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        AmbiSpeakerDecoderParams decoderParams {};
        decoderParams.activeSpeakers = k3OafxVirtualSpeakers;
        decoderParams.order = 3u;
        decoderParams.mode = AmbiSpeakerDecoderMode::Mmd;
        decoderParams.layout = AmbiSpeakerLayoutPreset::Sphere24;
        decoderParams.weighting = AmbiSpeakerDecoderWeighting::MaxRe;
        decoderParams.regularization = 0.018f;
        decoderParams.width = 1.0f;
        decoderParams.energy = 1.0f;
        decoderParams.outputGainDb = 0.0f;
        decoder_.prepare(sampleRate_);
        decoder_.setParams(decoderParams);

        score_ = sanitizeThreeOafxDisplacementScore(score_);
        rebuildSpatialMatrices();
        constexpr double maximumPropagationSeconds = 0.075;
        delaySize_ = std::max<uint32_t>(8u,
            static_cast<uint32_t>(std::ceil(sampleRate_ * maximumPropagationSeconds)) + 4u);
        for (auto& buffer : delayBuffers_) buffer.assign(delaySize_, 0.0f);
        reset();
    }

    void reset()
    {
        for (auto& buffer : delayBuffers_) std::fill(buffer.begin(), buffer.end(), 0.0f);
        delayWrite_ = 0u;
        airState_.fill(0.0f);
        currentEncode_ = baseEncode_;
        for (uint32_t point = 0u; point < k3OafxVirtualSpeakers; ++point) {
            currentGain_[point] = 1.0f;
            currentAirCoefficient_[point] = 1.0f;
            currentDelaySamples_[point] = 0.0f;
        }
        outputGainSmoothed_ = dbToLinear(params_.outputGainDb);
    }

    void setScore(ThreeOafxDisplacementScore score)
    {
        score_ = sanitizeThreeOafxDisplacementScore(score);
        rebuildSpatialMatrices();
        currentEncode_ = baseEncode_;
    }

    const ThreeOafxDisplacementScore& score() const { return score_; }

    void setParams(ThreeOafxDisplacementParams params)
    {
        params_ = sanitizeThreeOafxDisplacementParams(params);
    }

    ThreeOafxDisplacementParams params() const { return params_; }

    void processBlock(const float* const* inputs,
                      float* const* outputs,
                      uint32_t inputChannels,
                      uint32_t outputChannels,
                      uint32_t frames,
                      double phaseStart,
                      double phaseIncrement,
                      ThreeOafxDisplacementPlaybackMode playback)
    {
        if (!outputs || frames == 0u) return;
        const uint32_t inChannels = std::min<uint32_t>(inputChannels, k3OaChannels);
        const uint32_t outChannels = std::min<uint32_t>(outputChannels, k3OaChannels);
        for (uint32_t channel = outChannels; channel < outputChannels; ++channel) {
            if (outputs[channel]) std::fill(outputs[channel], outputs[channel] + frames, 0.0f);
        }
        if (outChannels == 0u) return;

        if (params_.bypass) {
            for (uint32_t channel = 0u; channel < outChannels; ++channel) {
                if (!outputs[channel]) continue;
                if (channel < inChannels && inputs && inputs[channel]) {
                    std::copy(inputs[channel], inputs[channel] + frames, outputs[channel]);
                } else {
                    std::fill(outputs[channel], outputs[channel] + frames, 0.0f);
                }
            }
            return;
        }

        const double targetPhase = phaseStart + phaseIncrement * static_cast<double>(frames > 0u ? frames - 1u : 0u);
        buildTargetState(targetPhase, playback);
        const auto& decode = decoder_.matrix();
        const bool physical = params_.distanceMode == ThreeOafxDisplacementDistanceMode::Physical;
        const float outputTarget = dbToLinear(params_.outputGainDb);

        for (uint32_t frame = 0u; frame < frames; ++frame) {
            const float alpha = static_cast<float>(frame + 1u) / static_cast<float>(frames);
            float inputFrame[k3OaChannels] {};
            for (uint32_t channel = 0u; channel < inChannels; ++channel) {
                if (inputs && inputs[channel]) inputFrame[channel] = inputs[channel][frame];
            }

            float virtualFeeds[k3OafxVirtualSpeakers] {};
            for (uint32_t point = 0u; point < k3OafxVirtualSpeakers; ++point) {
                float decoded = 0.0f;
                for (uint32_t channel = 0u; channel < k3OaChannels; ++channel) {
                    decoded += decode[point][channel] * inputFrame[channel];
                }
                const float gain = currentGain_[point] + (targetGain_[point] - currentGain_[point]) * alpha;
                float feed = flushDenormal(decoded * gain);
                if (physical) {
                    const float coefficient = currentAirCoefficient_[point]
                        + (targetAirCoefficient_[point] - currentAirCoefficient_[point]) * alpha;
                    airState_[point] += coefficient * (feed - airState_[point]);
                    feed = flushDenormal(airState_[point]);
                    auto& delay = delayBuffers_[point];
                    if (!delay.empty()) {
                        delay[delayWrite_] = feed;
                        const float samples = currentDelaySamples_[point]
                            + (targetDelaySamples_[point] - currentDelaySamples_[point]) * alpha;
                        feed = readDelay(delay, samples);
                    }
                }
                virtualFeeds[point] = feed;
            }

            const float outputGain = outputGainSmoothed_ + (outputTarget - outputGainSmoothed_) * alpha;
            for (uint32_t channel = 0u; channel < outChannels; ++channel) {
                if (!outputs[channel]) continue;
                float value = 0.0f;
                for (uint32_t point = 0u; point < k3OafxVirtualSpeakers; ++point) {
                    const float encoder = currentEncode_[channel][point]
                        + (targetEncode_[channel][point] - currentEncode_[channel][point]) * alpha;
                    value += encoder * virtualFeeds[point];
                }
                outputs[channel][frame] = std::isfinite(value) ? flushDenormal(value * outputGain) : 0.0f;
            }
            delayWrite_ = delaySize_ > 0u ? (delayWrite_ + 1u) % delaySize_ : 0u;
        }

        currentEncode_ = targetEncode_;
        currentGain_ = targetGain_;
        currentAirCoefficient_ = targetAirCoefficient_;
        currentDelaySamples_ = targetDelaySamples_;
        outputGainSmoothed_ = outputTarget;
    }

private:
    using DecodeMatrix = std::array<std::array<float, kAmbiSpeakerDecoderMaxChannels>, kAmbiSpeakerDecoderMaxSpeakers>;
    using EncodeMatrix = std::array<std::array<float, k3OafxVirtualSpeakers>, k3OaChannels>;
    using SquareMatrix = std::array<std::array<double, k3OaChannels>, k3OaChannels>;

    static float dbToLinear(float decibels)
    {
        return std::pow(10.0f, decibels / 20.0f);
    }

    static uint32_t orderForAcn(uint32_t channel)
    {
        return std::min<uint32_t>(3u, static_cast<uint32_t>(std::floor(std::sqrt(static_cast<float>(channel)))));
    }

    void rebuildSpatialMatrices()
    {
        const auto& decode = decoder_.matrix();
        EncodeMatrix rawBase {};
        for (uint32_t point = 0u; point < k3OafxVirtualSpeakers; ++point) {
            const auto source = score_.source[point];
            const auto basis = acnSn3dBasis(directionFromAed(source.azimuthDeg, source.elevationDeg));
            for (uint32_t channel = 0u; channel < k3OaChannels; ++channel) {
                rawBase[channel][point] = basis[channel] / static_cast<float>(k3OafxVirtualSpeakers);
            }
        }

        SquareMatrix roundTrip {};
        for (uint32_t row = 0u; row < k3OaChannels; ++row) {
            for (uint32_t column = 0u; column < k3OaChannels; ++column) {
                for (uint32_t point = 0u; point < k3OafxVirtualSpeakers; ++point) {
                    roundTrip[row][column] += static_cast<double>(rawBase[row][point])
                        * static_cast<double>(decode[point][column]);
                }
            }
        }
        correction_ = {};
        if (!invert(roundTrip, correction_)) {
            for (uint32_t channel = 0u; channel < k3OaChannels; ++channel) correction_[channel][channel] = 1.0;
        }
        multiplyCorrection(rawBase, baseEncode_);
        targetEncode_ = baseEncode_;
        baseEncodeEnergy_ = matrixEnergy(baseEncode_);
    }

    void buildTargetState(double phase, ThreeOafxDisplacementPlaybackMode playback)
    {
        const auto geometry = threeOafxDisplacementGeometry(score_, params_, phase, playback);
        EncodeMatrix raw {};
        for (uint32_t point = 0u; point < k3OafxVirtualSpeakers; ++point) {
            const auto& displaced = geometry[point];
            const auto basis = acnSn3dBasis(directionFromAed(displaced.azimuthDeg, displaced.elevationDeg));
            const float radius = displaced.radius;
            const float gain = threeOafxDisplacementDistanceGain(radius);
            targetGain_[point] = gain;

            const bool physical = params_.distanceMode == ThreeOafxDisplacementDistanceMode::Physical;
            const float extraMeters = physical
                ? std::max(0.0f, radius - 1.0f) * params_.referenceDistanceMeters
                : 0.0f;
            targetDelaySamples_[point] = clamp(
                extraMeters / 343.0f * static_cast<float>(sampleRate_),
                0.0f,
                static_cast<float>(delaySize_ > 4u ? delaySize_ - 4u : 0u));
            const float cutoff = physical
                ? clamp(20000.0f * std::exp(-0.075f * extraMeters), 1800.0f, 20000.0f)
                : 20000.0f;
            targetAirCoefficient_[point] = clamp(
                1.0f - std::exp(-2.0f * kPi * cutoff / static_cast<float>(sampleRate_)),
                0.0001f,
                1.0f);

            for (uint32_t channel = 0u; channel < k3OaChannels; ++channel) {
                float depth = 1.0f;
                if (physical && radius > 1.0f) {
                    depth = std::pow(1.0f / radius, 0.22f * static_cast<float>(orderForAcn(channel)));
                }
                raw[channel][point] = basis[channel] * depth / static_cast<float>(k3OafxVirtualSpeakers);
            }
        }

        multiplyCorrection(raw, targetEncode_);
        const float targetEnergy = matrixEnergy(targetEncode_);
        const float correction = clamp(
            std::sqrt(baseEncodeEnergy_ / std::max(0.000001f, targetEnergy)),
            0.5f,
            2.0f);
        const float energyGain = 1.0f + (correction - 1.0f) * params_.energy;
        for (auto& channel : targetEncode_) {
            for (float& coefficient : channel) coefficient *= energyGain;
        }
    }

    void multiplyCorrection(const EncodeMatrix& raw, EncodeMatrix& corrected) const
    {
        corrected = {};
        for (uint32_t row = 0u; row < k3OaChannels; ++row) {
            for (uint32_t point = 0u; point < k3OafxVirtualSpeakers; ++point) {
                double value = 0.0;
                for (uint32_t column = 0u; column < k3OaChannels; ++column) {
                    value += correction_[row][column] * static_cast<double>(raw[column][point]);
                }
                corrected[row][point] = static_cast<float>(value);
            }
        }
    }

    static float matrixEnergy(const EncodeMatrix& matrix)
    {
        double energy = 0.0;
        for (const auto& row : matrix) {
            for (float value : row) energy += static_cast<double>(value) * static_cast<double>(value);
        }
        return static_cast<float>(energy);
    }

    static bool invert(const SquareMatrix& input, SquareMatrix& inverse)
    {
        std::array<std::array<double, k3OaChannels * 2u>, k3OaChannels> augmented {};
        for (uint32_t row = 0u; row < k3OaChannels; ++row) {
            for (uint32_t column = 0u; column < k3OaChannels; ++column) {
                augmented[row][column] = input[row][column];
            }
            augmented[row][k3OaChannels + row] = 1.0;
        }
        for (uint32_t column = 0u; column < k3OaChannels; ++column) {
            uint32_t pivot = column;
            double best = std::fabs(augmented[column][column]);
            for (uint32_t row = column + 1u; row < k3OaChannels; ++row) {
                const double candidate = std::fabs(augmented[row][column]);
                if (candidate > best) {
                    best = candidate;
                    pivot = row;
                }
            }
            if (best < 1.0e-12) return false;
            if (pivot != column) std::swap(augmented[pivot], augmented[column]);
            const double inversePivot = 1.0 / augmented[column][column];
            for (double& value : augmented[column]) value *= inversePivot;
            for (uint32_t row = 0u; row < k3OaChannels; ++row) {
                if (row == column) continue;
                const double factor = augmented[row][column];
                if (std::fabs(factor) < 1.0e-15) continue;
                for (uint32_t item = 0u; item < k3OaChannels * 2u; ++item) {
                    augmented[row][item] -= factor * augmented[column][item];
                }
            }
        }
        for (uint32_t row = 0u; row < k3OaChannels; ++row) {
            for (uint32_t column = 0u; column < k3OaChannels; ++column) {
                inverse[row][column] = augmented[row][k3OaChannels + column];
            }
        }
        return true;
    }

    float readDelay(const std::vector<float>& buffer, float samples) const
    {
        if (buffer.empty()) return 0.0f;
        float position = static_cast<float>(delayWrite_) - samples;
        while (position < 0.0f) position += static_cast<float>(delaySize_);
        const uint32_t first = static_cast<uint32_t>(std::floor(position)) % delaySize_;
        const uint32_t second = (first + 1u) % delaySize_;
        const float fraction = position - std::floor(position);
        return buffer[first] + (buffer[second] - buffer[first]) * fraction;
    }

    double sampleRate_ = 48000.0;
    ThreeOafxDisplacementParams params_ {};
    ThreeOafxDisplacementScore score_ = makeDefaultThreeOafxDisplacementScore();
    AmbiSpeakerDecoder decoder_ {};
    SquareMatrix correction_ {};
    EncodeMatrix baseEncode_ {};
    EncodeMatrix currentEncode_ {};
    EncodeMatrix targetEncode_ {};
    float baseEncodeEnergy_ = 1.0f;
    std::array<float, k3OafxVirtualSpeakers> currentGain_ {};
    std::array<float, k3OafxVirtualSpeakers> targetGain_ {};
    std::array<float, k3OafxVirtualSpeakers> currentAirCoefficient_ {};
    std::array<float, k3OafxVirtualSpeakers> targetAirCoefficient_ {};
    std::array<float, k3OafxVirtualSpeakers> currentDelaySamples_ {};
    std::array<float, k3OafxVirtualSpeakers> targetDelaySamples_ {};
    std::array<float, k3OafxVirtualSpeakers> airState_ {};
    std::array<std::vector<float>, k3OafxVirtualSpeakers> delayBuffers_ {};
    uint32_t delaySize_ = 0u;
    uint32_t delayWrite_ = 0u;
    float outputGainSmoothed_ = 1.0f;
};

} // namespace s3g
