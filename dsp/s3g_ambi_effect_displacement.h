#pragma once

#include "s3g_ambi_effect_dj_filter.h"
#include "s3g_math.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace s3g {

constexpr uint32_t kAmbiEffectDisplacementMaxScenes = 64u;
constexpr uint32_t kAmbiEffectDisplacementScorePoints = 24u;
constexpr uint32_t kAmbiEffectDisplacementMaxChannels = 64u;
constexpr uint32_t kAmbiEffectDisplacementMaxPickups = 24u;

enum class AmbiEffectDisplacementDistanceMode : uint32_t {
    Gain = 0u,
    Physical = 1u,
};

enum class AmbiEffectDisplacementPlaybackMode : uint32_t {
    Loop = 0u,
    Palindrome = 1u,
    Once = 2u,
};

struct AmbiEffectDisplacementPoint {
    float azimuthDeg = 0.0f;
    float elevationDeg = 0.0f;
    float radius = 1.0f;
};

struct AmbiEffectDisplacementScene {
    float time = 0.0f;
    std::array<AmbiEffectDisplacementPoint,
        kAmbiEffectDisplacementScorePoints> points {};
};

struct AmbiEffectDisplacementScore {
    uint32_t sceneCount = 0u;
    float durationSeconds = 16.0f;
    std::array<AmbiEffectDisplacementPoint,
        kAmbiEffectDisplacementScorePoints> source {};
    std::array<AmbiEffectDisplacementScene,
        kAmbiEffectDisplacementMaxScenes> scenes {};
};

struct AmbiEffectDisplacementParams {
    uint32_t order = 7u;
    AmbiEffectBody body = AmbiEffectBody::Auto;
    float amount = 0.65f;
    float azimuthScale = 1.0f;
    float elevationScale = 1.0f;
    float radiusScale = 1.0f;
    AmbiEffectDisplacementDistanceMode distanceMode =
        AmbiEffectDisplacementDistanceMode::Gain;
    float referenceDistanceMeters = 2.0f;
    float energy = 0.65f;
    float mix = 1.0f;
    float outputGainDb = 0.0f;
    float maskAmount = 0.0f;
    float maskAzimuthDeg = 0.0f;
    float maskElevationDeg = 0.0f;
    float maskWidth = 0.35f;
    float maskCurve = 0.5f;
    float maskDry = 1.0f;
    bool bypass = false;
};

struct AmbiEffectDisplacementBodyGeometry {
    uint32_t count = 0u;
    std::array<AmbiEffectDisplacementPoint,
        kAmbiEffectDisplacementMaxPickups> source {};
    std::array<AmbiEffectDisplacementPoint,
        kAmbiEffectDisplacementMaxPickups> target {};
    std::array<float, kAmbiEffectDisplacementMaxPickups> wetMask {};
};

inline float wrapDisplacementDegrees(float value)
{
    while (value > 180.0f) value -= 360.0f;
    while (value <= -180.0f) value += 360.0f;
    return value;
}

inline AmbiEffectDisplacementPoint ambiEffectDisplacementPointFromVector(
    Vec3 point, float radius = 1.0f)
{
    point = normalize(point);
    const float horizontal = std::sqrt(
        point.x * point.x + point.y * point.y);
    return {
        wrapDisplacementDegrees(
            std::atan2(point.y, point.x) * 180.0f / kPi),
        clamp(std::atan2(point.z, horizontal) * 180.0f / kPi,
            -90.0f, 90.0f),
        radius,
    };
}

inline AmbiEffectDisplacementScore makeDefaultAmbiEffectDisplacementScore()
{
    AmbiEffectDisplacementScore score {};
    score.sceneCount = 1u;
    score.durationSeconds = 16.0f;
    for (uint32_t point = 0u;
        point < kAmbiEffectDisplacementScorePoints; ++point) {
        score.source[point] = ambiEffectDisplacementPointFromVector(
            kAmbisonicSphere24Points[point]);
        score.scenes[0].points[point] = score.source[point];
    }
    score.scenes[0].time = 0.0f;
    return score;
}

inline AmbiEffectDisplacementPoint sanitizeAmbiEffectDisplacementPoint(
    AmbiEffectDisplacementPoint point)
{
    if (!std::isfinite(point.azimuthDeg)) point.azimuthDeg = 0.0f;
    if (!std::isfinite(point.elevationDeg)) point.elevationDeg = 0.0f;
    if (!std::isfinite(point.radius)) point.radius = 1.0f;
    point.azimuthDeg = wrapDisplacementDegrees(point.azimuthDeg);
    point.elevationDeg = clamp(point.elevationDeg, -90.0f, 90.0f);
    point.radius = clamp(point.radius, 0.05f, 3.0f);
    return point;
}

inline AmbiEffectDisplacementScore sanitizeAmbiEffectDisplacementScore(
    AmbiEffectDisplacementScore score)
{
    if (score.sceneCount == 0u) {
        return makeDefaultAmbiEffectDisplacementScore();
    }
    score.sceneCount = std::min<uint32_t>(score.sceneCount,
        kAmbiEffectDisplacementMaxScenes);
    if (!std::isfinite(score.durationSeconds)) score.durationSeconds = 16.0f;
    score.durationSeconds = clamp(score.durationSeconds, 0.05f, 86400.0f);
    for (auto& point : score.source) {
        point = sanitizeAmbiEffectDisplacementPoint(point);
    }
    for (uint32_t scene = 0u; scene < score.sceneCount; ++scene) {
        auto& item = score.scenes[scene];
        if (!std::isfinite(item.time)) item.time = 0.0f;
        item.time = clamp(item.time, 0.0f, 1.0f);
        for (auto& point : item.points) {
            point = sanitizeAmbiEffectDisplacementPoint(point);
        }
    }
    std::sort(score.scenes.begin(),
        score.scenes.begin() + score.sceneCount,
        [](const auto& a, const auto& b) { return a.time < b.time; });
    score.scenes[0].time = 0.0f;
    return score;
}

inline AmbiEffectDisplacementParams sanitizeAmbiEffectDisplacementParams(
    AmbiEffectDisplacementParams params)
{
    params.order = std::clamp<uint32_t>(params.order, 1u, 7u);
    params.body = static_cast<AmbiEffectBody>(std::min<uint32_t>(
        static_cast<uint32_t>(params.body),
        static_cast<uint32_t>(AmbiEffectBody::Sphere24)));
    if (params.body == AmbiEffectBody::Tetra4
        || params.body == AmbiEffectBody::Cube8) {
        params.body = AmbiEffectBody::Icosa12;
    }
    params.amount = clamp(params.amount, 0.0f, 1.0f);
    params.azimuthScale = clamp(params.azimuthScale, 0.0f, 2.0f);
    params.elevationScale = clamp(params.elevationScale, 0.0f, 2.0f);
    params.radiusScale = clamp(params.radiusScale, 0.0f, 2.0f);
    params.distanceMode = static_cast<AmbiEffectDisplacementDistanceMode>(
        std::min<uint32_t>(static_cast<uint32_t>(params.distanceMode), 1u));
    params.referenceDistanceMeters = clamp(
        params.referenceDistanceMeters, 0.5f, 10.0f);
    params.energy = clamp(params.energy, 0.0f, 1.0f);
    params.mix = clamp(params.mix, 0.0f, 1.0f);
    params.outputGainDb = clamp(params.outputGainDb, -60.0f, 12.0f);
    params.maskAmount = clamp(params.maskAmount, 0.0f, 1.0f);
    params.maskAzimuthDeg = clamp(params.maskAzimuthDeg, -180.0f, 180.0f);
    params.maskElevationDeg = clamp(params.maskElevationDeg, -90.0f, 90.0f);
    params.maskWidth = clamp(params.maskWidth, 0.0f, 1.0f);
    params.maskCurve = clamp(params.maskCurve, 0.0f, 1.0f);
    params.maskDry = clamp(params.maskDry, 0.0f, 1.0f);
    return params;
}

inline float ambiEffectDisplacementPlaybackPhase(double phase,
    AmbiEffectDisplacementPlaybackMode mode)
{
    if (!std::isfinite(phase)) return 0.0f;
    switch (mode) {
    case AmbiEffectDisplacementPlaybackMode::Palindrome: {
        double wrapped = std::fmod(phase, 2.0);
        if (wrapped < 0.0) wrapped += 2.0;
        return static_cast<float>(
            wrapped <= 1.0 ? wrapped : 2.0 - wrapped);
    }
    case AmbiEffectDisplacementPlaybackMode::Once:
        return static_cast<float>(std::clamp(phase, 0.0, 1.0));
    case AmbiEffectDisplacementPlaybackMode::Loop:
    default: {
        double wrapped = phase - std::floor(phase);
        if (wrapped < 0.0) wrapped += 1.0;
        return static_cast<float>(wrapped);
    }
    }
}

inline std::array<AmbiEffectDisplacementPoint,
    kAmbiEffectDisplacementScorePoints> ambiEffectDisplacementGeometry(
    const AmbiEffectDisplacementScore& score,
    const AmbiEffectDisplacementParams& params, double phase,
    AmbiEffectDisplacementPlaybackMode playback)
{
    auto geometry = score.source;
    if (score.sceneCount == 0u) return geometry;

    const float time = ambiEffectDisplacementPlaybackPhase(phase, playback);
    uint32_t first = 0u;
    uint32_t second = 0u;
    float blend = 0.0f;
    if (time >= score.scenes[score.sceneCount - 1u].time) {
        first = second = score.sceneCount - 1u;
    } else {
        for (uint32_t scene = 0u; scene + 1u < score.sceneCount; ++scene) {
            if (time > score.scenes[scene + 1u].time) continue;
            first = scene;
            second = scene + 1u;
            const float span = std::max(0.000001f,
                score.scenes[second].time - score.scenes[first].time);
            const float u = clamp(
                (time - score.scenes[first].time) / span, 0.0f, 1.0f);
            blend = u * u * (3.0f - 2.0f * u);
            break;
        }
    }

    for (uint32_t point = 0u;
        point < kAmbiEffectDisplacementScorePoints; ++point) {
        const auto& source = score.source[point];
        const auto& a = score.scenes[first].points[point];
        const auto& b = score.scenes[second].points[point];
        const float targetAzimuth = wrapDisplacementDegrees(
            a.azimuthDeg
            + wrapDisplacementDegrees(b.azimuthDeg - a.azimuthDeg) * blend);
        const float targetElevation =
            a.elevationDeg + (b.elevationDeg - a.elevationDeg) * blend;
        const float targetRadius = a.radius + (b.radius - a.radius) * blend;
        geometry[point].azimuthDeg = wrapDisplacementDegrees(
            source.azimuthDeg
            + wrapDisplacementDegrees(targetAzimuth - source.azimuthDeg)
                * params.azimuthScale * params.amount);
        geometry[point].elevationDeg = clamp(
            source.elevationDeg
            + (targetElevation - source.elevationDeg)
                * params.elevationScale * params.amount,
            -90.0f, 90.0f);
        geometry[point].radius = clamp(
            source.radius + (targetRadius - source.radius)
                * params.radiusScale * params.amount,
            0.05f, 3.0f);
    }
    return geometry;
}

inline float ambiEffectDisplacementDistanceGain(float radius)
{
    return clamp(1.0f / std::max(0.05f, radius), 0.25f, 2.0f);
}

inline float ambiEffectDisplacementDot(Vec3 a, Vec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Vec3 ambiEffectDisplacementBlendDirection(Vec3 a, Vec3 b, float amount)
{
    amount = clamp(amount, 0.0f, 1.0f);
    Vec3 result {
        a.x + (b.x - a.x) * amount,
        a.y + (b.y - a.y) * amount,
        a.z + (b.z - a.z) * amount,
    };
    if (std::fabs(result.x) + std::fabs(result.y) + std::fabs(result.z)
        < 0.00001f) {
        result = amount < 0.5f ? a : b;
    }
    return normalize(result);
}

inline AmbiEffectDisplacementBodyGeometry ambiEffectDisplacementBodyGeometry(
    const AmbiEffectDisplacementScore& score,
    const AmbiEffectDisplacementParams& unsanitized, double phase,
    AmbiEffectDisplacementPlaybackMode playback)
{
    const auto params = sanitizeAmbiEffectDisplacementParams(unsanitized);
    const auto body = resolveAmbiEffectBody(params.body, params.order);
    const auto scoreTarget = ambiEffectDisplacementGeometry(
        score, params, phase, playback);
    AmbiEffectDisplacementBodyGeometry result {};
    result.count = ambiEffectBodyPickupCount(body);
    result.wetMask.fill(1.0f);

    if (body == AmbiEffectBody::Sphere24) {
        for (uint32_t point = 0u; point < result.count; ++point) {
            result.source[point] = score.source[point];
            result.target[point] = scoreTarget[point];
        }
    } else {
        const auto bodyDirections = ambiEffectBodyDirections(body);
        for (uint32_t point = 0u; point < result.count; ++point) {
            const Vec3 query = bodyDirections[point];
            Vec3 displacement {};
            float logRadius = 0.0f;
            float weightSum = 0.0f;
            for (uint32_t sample = 0u;
                sample < kAmbiEffectDisplacementScorePoints; ++sample) {
                const Vec3 scoreSource = directionFromAed(
                    score.source[sample].azimuthDeg,
                    score.source[sample].elevationDeg);
                const Vec3 scoreDestination = directionFromAed(
                    scoreTarget[sample].azimuthDeg,
                    scoreTarget[sample].elevationDeg);
                const float weight = std::exp(10.0f
                    * (ambiEffectDisplacementDot(query, scoreSource) - 1.0f));
                displacement.x += weight
                    * (scoreDestination.x - scoreSource.x);
                displacement.y += weight
                    * (scoreDestination.y - scoreSource.y);
                displacement.z += weight
                    * (scoreDestination.z - scoreSource.z);
                const float ratio = scoreTarget[sample].radius
                    / std::max(0.05f, score.source[sample].radius);
                logRadius += weight * std::log(std::max(0.05f, ratio));
                weightSum += weight;
            }
            const float inverse = 1.0f / std::max(0.000001f, weightSum);
            const Vec3 destination = normalize({
                query.x + displacement.x * inverse,
                query.y + displacement.y * inverse,
                query.z + displacement.z * inverse,
            });
            result.source[point] =
                ambiEffectDisplacementPointFromVector(query);
            result.target[point] = ambiEffectDisplacementPointFromVector(
                destination, clamp(std::exp(logRadius * inverse), 0.05f, 3.0f));
        }
    }

    if (params.maskAmount > 0.00001f) {
        const Vec3 maskDirection = directionFromAed(
            params.maskAzimuthDeg, params.maskElevationDeg);
        float maximum = 0.000001f;
        std::array<float, kAmbiEffectDisplacementMaxPickups> alignment {};
        for (uint32_t point = 0u; point < result.count; ++point) {
            const Vec3 source = directionFromAed(
                result.source[point].azimuthDeg,
                result.source[point].elevationDeg);
            alignment[point] = clamp(
                (ambiEffectDisplacementDot(source, maskDirection) + 1.0f)
                    * 0.5f,
                0.0f, 1.0f);
            maximum = std::max(maximum, alignment[point]);
        }
        const float exponent = ambiEffectMaskExponent(
            params.maskWidth, params.maskCurve);
        for (uint32_t point = 0u; point < result.count; ++point) {
            const float directional = std::pow(
                clamp(alignment[point] / maximum, 0.0f, 1.0f), exponent);
            result.wetMask[point] = lerp(
                1.0f, directional, params.maskAmount);
            const Vec3 source = directionFromAed(
                result.source[point].azimuthDeg,
                result.source[point].elevationDeg);
            const Vec3 target = directionFromAed(
                result.target[point].azimuthDeg,
                result.target[point].elevationDeg);
            result.target[point] = ambiEffectDisplacementPointFromVector(
                ambiEffectDisplacementBlendDirection(
                    source, target, result.wetMask[point]),
                lerp(result.source[point].radius,
                    result.target[point].radius, result.wetMask[point]));
        }
    }
    return result;
}

class AmbiEffectDisplacement {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = std::clamp(
            std::isfinite(sampleRate) ? sampleRate : 48000.0,
            1000.0, 768000.0);
        score_ = sanitizeAmbiEffectDisplacementScore(score_);
        prepareDelayBuffers();
        rebuildSourceProjection();
        reset();
    }

    void reset()
    {
        for (auto& buffer : delayBuffers_) {
            std::fill(buffer.begin(), buffer.end(), 0.0f);
        }
        delayWrite_ = 0u;
        airState_.fill(0.0f);
        nodeLevel_.fill(0.0f);
        outputGainSmoothed_ = dbToGain(params_.outputGainDb);
        targetsInitialized_ = false;
    }

    void setScore(AmbiEffectDisplacementScore score)
    {
        score_ = sanitizeAmbiEffectDisplacementScore(score);
        rebuildSourceProjection();
        targetsInitialized_ = false;
    }

    const AmbiEffectDisplacementScore& score() const { return score_; }

    void setParams(AmbiEffectDisplacementParams params)
    {
        const auto next = sanitizeAmbiEffectDisplacementParams(params);
        const bool projectionChanged = next.order != params_.order
            || resolveAmbiEffectBody(next.body, next.order)
                != resolveAmbiEffectBody(params_.body, params_.order);
        params_ = next;
        if (projectionChanged) {
            rebuildSourceProjection();
            targetsInitialized_ = false;
        }
    }

    AmbiEffectDisplacementParams params() const { return params_; }

    AmbiEffectBody resolvedBody() const
    {
        return resolveAmbiEffectBody(params_.body, params_.order);
    }

    uint32_t activePickupCount() const { return sourceProjection_.count; }

    float nodeLevel(uint32_t point) const
    {
        return point < nodeLevel_.size() ? nodeLevel_[point] : 0.0f;
    }

    template <typename Sample>
    void processBlock(const Sample* const* inputs, Sample* const* outputs,
        uint32_t inputChannels, uint32_t outputChannels, uint32_t frames,
        double phaseStart, double phaseIncrement,
        AmbiEffectDisplacementPlaybackMode playback)
    {
        if (!outputs || frames == 0u) return;
        const uint32_t activeChannels = ambiEffectChannelsForOrder(params_.order);
        const uint32_t inChannels = std::min<uint32_t>(
            inputChannels, kAmbiEffectDisplacementMaxChannels);
        const uint32_t outChannels = std::min<uint32_t>(
            outputChannels, kAmbiEffectDisplacementMaxChannels);
        if (params_.bypass) {
            for (uint32_t channel = 0u; channel < outChannels; ++channel) {
                if (!outputs[channel]) continue;
                if (channel < inChannels && inputs && inputs[channel]) {
                    std::copy(inputs[channel], inputs[channel] + frames,
                        outputs[channel]);
                } else {
                    std::fill(outputs[channel], outputs[channel] + frames,
                        Sample(0));
                }
            }
            for (uint32_t channel = outChannels;
                channel < outputChannels; ++channel) {
                if (outputs[channel]) {
                    std::fill(outputs[channel], outputs[channel] + frames,
                        Sample(0));
                }
            }
            return;
        }

        const double targetPhase = phaseStart + phaseIncrement
            * static_cast<double>(frames - 1u);
        buildTargetState(targetPhase, playback);
        if (!targetsInitialized_) snapTargets();
        const bool physical = params_.distanceMode
            == AmbiEffectDisplacementDistanceMode::Physical;
        const float outputTarget = dbToGain(params_.outputGainDb);
        const float levelCoefficient = 1.0f - std::exp(
            -1.0f / static_cast<float>(sampleRate_ * 0.055));

        std::array<float, kAmbiEffectDisplacementMaxChannels> inputFrame {};
        std::array<float, kAmbiEffectDisplacementMaxPickups> pickup {};
        std::array<float, kAmbiEffectDisplacementMaxPickups> moved {};
        for (uint32_t frame = 0u; frame < frames; ++frame) {
            const float alpha = static_cast<float>(frame + 1u)
                / static_cast<float>(frames);
            for (uint32_t channel = 0u;
                channel < kAmbiEffectDisplacementMaxChannels; ++channel) {
                inputFrame[channel] = channel < inChannels && inputs
                    && inputs[channel]
                    ? static_cast<float>(inputs[channel][frame]) : 0.0f;
            }
            for (uint32_t point = 0u;
                point < sourceProjection_.count; ++point) {
                float value = 0.0f;
                for (uint32_t channel = 0u;
                    channel < activeChannels; ++channel) {
                    value += sourceProjection_.decode[point][channel]
                        * inputFrame[channel];
                }
                pickup[point] = flushDenormal(value);
                nodeLevel_[point] += (std::fabs(value) - nodeLevel_[point])
                    * levelCoefficient;

                const float gain = lerp(
                    currentGain_[point], targetGain_[point], alpha);
                const float dryScale = lerp(
                    currentDryScale_[point], targetDryScale_[point], alpha);
                float feed = flushDenormal(value * gain * dryScale);
                if (physical) {
                    const float coefficient = lerp(
                        currentAirCoefficient_[point],
                        targetAirCoefficient_[point], alpha);
                    airState_[point] += coefficient
                        * (feed - airState_[point]);
                    feed = flushDenormal(airState_[point]);
                    auto& delay = delayBuffers_[point];
                    if (!delay.empty()) {
                        delay[delayWrite_] = feed;
                        feed = readDelay(delay, lerp(
                            currentDelaySamples_[point],
                            targetDelaySamples_[point], alpha));
                    }
                }
                moved[point] = feed;
            }

            const float outputGain = lerp(
                outputGainSmoothed_, outputTarget, alpha);
            for (uint32_t channel = 0u; channel < outChannels; ++channel) {
                if (!outputs[channel]) continue;
                if (channel >= activeChannels) {
                    outputs[channel][frame] = Sample(0);
                    continue;
                }
                float source = 0.0f;
                float target = 0.0f;
                for (uint32_t point = 0u;
                    point < sourceProjection_.count; ++point) {
                    source += sourceProjection_.encode[channel][point]
                        * pickup[point];
                    target += lerp(currentEncode_[channel][point],
                        targetEncode_[channel][point], alpha) * moved[point];
                }
                const float original = channel < inChannels
                    ? inputFrame[channel] : 0.0f;
                const float value = (original
                    + (target - source) * params_.mix) * outputGain;
                outputs[channel][frame] = static_cast<Sample>(
                    std::isfinite(value) ? flushDenormal(value) : 0.0f);
            }
            delayWrite_ = delaySize_ > 0u
                ? (delayWrite_ + 1u) % delaySize_ : 0u;
        }

        currentEncode_ = targetEncode_;
        currentGain_ = targetGain_;
        currentAirCoefficient_ = targetAirCoefficient_;
        currentDelaySamples_ = targetDelaySamples_;
        currentDryScale_ = targetDryScale_;
        outputGainSmoothed_ = outputTarget;
        targetsInitialized_ = true;
        for (uint32_t channel = outChannels;
            channel < outputChannels; ++channel) {
            if (outputs[channel]) {
                std::fill(outputs[channel], outputs[channel] + frames,
                    Sample(0));
            }
        }
    }

private:
    struct Projection {
        uint32_t count = 0u;
        std::array<Vec3, kAmbiEffectDisplacementMaxPickups> directions {};
        std::array<std::array<float,
            kAmbiEffectDisplacementMaxChannels>,
            kAmbiEffectDisplacementMaxPickups> decode {};
        std::array<std::array<float,
            kAmbiEffectDisplacementMaxPickups>,
            kAmbiEffectDisplacementMaxChannels> encode {};
    };

    using SquareMatrix = std::array<std::array<double,
        kAmbiEffectDisplacementMaxPickups>,
        kAmbiEffectDisplacementMaxPickups>;

    static bool invert(const SquareMatrix& input, uint32_t count,
        SquareMatrix& inverse)
    {
        std::array<std::array<double,
            kAmbiEffectDisplacementMaxPickups * 2u>,
            kAmbiEffectDisplacementMaxPickups> augmented {};
        for (uint32_t row = 0u; row < count; ++row) {
            for (uint32_t column = 0u; column < count; ++column) {
                augmented[row][column] = input[row][column];
            }
            augmented[row][count + row] = 1.0;
        }
        for (uint32_t column = 0u; column < count; ++column) {
            uint32_t pivot = column;
            for (uint32_t row = column + 1u; row < count; ++row) {
                if (std::fabs(augmented[row][column])
                    > std::fabs(augmented[pivot][column])) pivot = row;
            }
            if (std::fabs(augmented[pivot][column]) < 1.0e-12) return false;
            if (pivot != column) std::swap(
                augmented[pivot], augmented[column]);
            const double scale = 1.0 / augmented[column][column];
            for (uint32_t item = 0u; item < count * 2u; ++item) {
                augmented[column][item] *= scale;
            }
            for (uint32_t row = 0u; row < count; ++row) {
                if (row == column) continue;
                const double factor = augmented[row][column];
                for (uint32_t item = 0u; item < count * 2u; ++item) {
                    augmented[row][item] -= factor
                        * augmented[column][item];
                }
            }
        }
        for (uint32_t row = 0u; row < count; ++row) {
            for (uint32_t column = 0u; column < count; ++column) {
                inverse[row][column] = augmented[row][count + column];
            }
        }
        return true;
    }

    static void buildProjection(Projection& projection,
        const std::array<Vec3, kAmbiEffectDisplacementMaxPickups>& directions,
        uint32_t count, uint32_t order)
    {
        projection = {};
        projection.count = count;
        projection.directions = directions;
        const uint32_t channels = ambiEffectChannelsForOrder(order);
        for (uint32_t point = 0u; point < count; ++point) {
            const auto basis = acnSn3dBasis7(directions[point]);
            double norm = 0.0;
            for (uint32_t channel = 0u; channel < channels; ++channel) {
                norm += static_cast<double>(basis[channel]) * basis[channel];
            }
            norm = std::max(1.0, norm);
            for (uint32_t channel = 0u; channel < channels; ++channel) {
                projection.decode[point][channel] = static_cast<float>(
                    static_cast<double>(basis[channel]) / norm);
            }
        }
        SquareMatrix gram {};
        for (uint32_t row = 0u; row < count; ++row) {
            for (uint32_t column = 0u; column < count; ++column) {
                for (uint32_t channel = 0u; channel < channels; ++channel) {
                    gram[row][column] += static_cast<double>(
                        projection.decode[row][channel])
                        * projection.decode[column][channel];
                }
                if (row == column) gram[row][column] += 1.0e-5;
            }
        }
        SquareMatrix inverse {};
        if (!invert(gram, count, inverse)) return;
        for (uint32_t channel = 0u; channel < channels; ++channel) {
            for (uint32_t point = 0u; point < count; ++point) {
                double value = 0.0;
                for (uint32_t row = 0u; row < count; ++row) {
                    value += static_cast<double>(
                        projection.decode[row][channel])
                        * inverse[row][point];
                }
                projection.encode[channel][point] =
                    static_cast<float>(value);
            }
        }
    }

    void rebuildSourceProjection()
    {
        const auto body = resolveAmbiEffectBody(params_.body, params_.order);
        const uint32_t count = ambiEffectBodyPickupCount(body);
        std::array<Vec3, kAmbiEffectDisplacementMaxPickups> directions {};
        if (body == AmbiEffectBody::Sphere24) {
            for (uint32_t point = 0u; point < count; ++point) {
                directions[point] = directionFromAed(
                    score_.source[point].azimuthDeg,
                    score_.source[point].elevationDeg);
            }
        } else {
            directions = ambiEffectBodyDirections(body);
        }
        buildProjection(sourceProjection_, directions, count, params_.order);
    }

    static float projectionEnergy(const Projection& projection,
        uint32_t channels)
    {
        double energy = 0.0;
        for (uint32_t channel = 0u; channel < channels; ++channel) {
            for (uint32_t point = 0u; point < projection.count; ++point) {
                const double value = projection.encode[channel][point];
                energy += value * value;
            }
        }
        return static_cast<float>(energy);
    }

    static uint32_t orderForAcn(uint32_t channel)
    {
        return std::min<uint32_t>(7u, static_cast<uint32_t>(
            std::floor(std::sqrt(static_cast<float>(channel)))));
    }

    void buildTargetState(double phase,
        AmbiEffectDisplacementPlaybackMode playback)
    {
        const auto geometry = ambiEffectDisplacementBodyGeometry(
            score_, params_, phase, playback);
        std::array<Vec3, kAmbiEffectDisplacementMaxPickups> directions {};
        for (uint32_t point = 0u; point < geometry.count; ++point) {
            directions[point] = directionFromAed(
                geometry.target[point].azimuthDeg,
                geometry.target[point].elevationDeg);
        }
        Projection targetProjection {};
        buildProjection(targetProjection, directions,
            geometry.count, params_.order);
        targetEncode_ = targetProjection.encode;

        const uint32_t channels = ambiEffectChannelsForOrder(params_.order);
        const float sourceEnergy = projectionEnergy(
            sourceProjection_, channels);
        const float targetEnergy = projectionEnergy(targetProjection, channels);
        const float correction = clamp(std::sqrt(sourceEnergy
            / std::max(0.000001f, targetEnergy)), 0.5f, 2.0f);
        const float energyGain = lerp(1.0f, correction, params_.energy);
        for (uint32_t point = 0u; point < geometry.count; ++point) {
            const float radius = geometry.target[point].radius;
            targetGain_[point] = ambiEffectDisplacementDistanceGain(radius);
            targetDryScale_[point] = lerp(
                params_.maskDry, 1.0f, geometry.wetMask[point]);
            const bool physical = params_.distanceMode
                == AmbiEffectDisplacementDistanceMode::Physical;
            const float extraMeters = physical
                ? std::max(0.0f, radius - 1.0f)
                    * params_.referenceDistanceMeters
                : 0.0f;
            targetDelaySamples_[point] = clamp(
                extraMeters / 343.0f * static_cast<float>(sampleRate_),
                0.0f,
                static_cast<float>(delaySize_ > 4u ? delaySize_ - 4u : 0u));
            const float cutoff = physical
                ? clamp(20000.0f * std::exp(-0.075f * extraMeters),
                    1800.0f, 20000.0f)
                : 20000.0f;
            targetAirCoefficient_[point] = clamp(
                1.0f - std::exp(-2.0f * kPi * cutoff
                    / static_cast<float>(sampleRate_)),
                0.0001f, 1.0f);
            for (uint32_t channel = 0u; channel < channels; ++channel) {
                float depth = 1.0f;
                if (physical && radius > 1.0f) {
                    depth = std::pow(1.0f / radius,
                        0.22f * static_cast<float>(orderForAcn(channel)));
                }
                targetEncode_[channel][point] *= depth * energyGain;
            }
        }
    }

    void snapTargets()
    {
        currentEncode_ = targetEncode_;
        currentGain_ = targetGain_;
        currentAirCoefficient_ = targetAirCoefficient_;
        currentDelaySamples_ = targetDelaySamples_;
        currentDryScale_ = targetDryScale_;
        targetsInitialized_ = true;
    }

    void prepareDelayBuffers()
    {
        constexpr double maximumPropagationSeconds = 0.075;
        delaySize_ = std::max<uint32_t>(8u,
            static_cast<uint32_t>(
                std::ceil(sampleRate_ * maximumPropagationSeconds)) + 4u);
        for (auto& buffer : delayBuffers_) {
            buffer.assign(delaySize_, 0.0f);
        }
    }

    float readDelay(const std::vector<float>& buffer, float samples) const
    {
        if (buffer.empty()) return 0.0f;
        float position = static_cast<float>(delayWrite_) - samples;
        while (position < 0.0f) position += static_cast<float>(delaySize_);
        const uint32_t first = static_cast<uint32_t>(std::floor(position))
            % delaySize_;
        const uint32_t second = (first + 1u) % delaySize_;
        const float fraction = position - std::floor(position);
        return buffer[first] + (buffer[second] - buffer[first]) * fraction;
    }

    double sampleRate_ = 48000.0;
    AmbiEffectDisplacementParams params_ {};
    AmbiEffectDisplacementScore score_ =
        makeDefaultAmbiEffectDisplacementScore();
    Projection sourceProjection_ {};
    std::array<std::array<float, kAmbiEffectDisplacementMaxPickups>,
        kAmbiEffectDisplacementMaxChannels> currentEncode_ {};
    std::array<std::array<float, kAmbiEffectDisplacementMaxPickups>,
        kAmbiEffectDisplacementMaxChannels> targetEncode_ {};
    std::array<float, kAmbiEffectDisplacementMaxPickups> currentGain_ {};
    std::array<float, kAmbiEffectDisplacementMaxPickups> targetGain_ {};
    std::array<float, kAmbiEffectDisplacementMaxPickups>
        currentAirCoefficient_ {};
    std::array<float, kAmbiEffectDisplacementMaxPickups>
        targetAirCoefficient_ {};
    std::array<float, kAmbiEffectDisplacementMaxPickups>
        currentDelaySamples_ {};
    std::array<float, kAmbiEffectDisplacementMaxPickups>
        targetDelaySamples_ {};
    std::array<float, kAmbiEffectDisplacementMaxPickups> currentDryScale_ {};
    std::array<float, kAmbiEffectDisplacementMaxPickups> targetDryScale_ {};
    std::array<float, kAmbiEffectDisplacementMaxPickups> airState_ {};
    std::array<float, kAmbiEffectDisplacementMaxPickups> nodeLevel_ {};
    std::array<std::vector<float>, kAmbiEffectDisplacementMaxPickups>
        delayBuffers_ {};
    uint32_t delaySize_ = 0u;
    uint32_t delayWrite_ = 0u;
    float outputGainSmoothed_ = 1.0f;
    bool targetsInitialized_ = false;
};

} // namespace s3g
