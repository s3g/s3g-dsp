#pragma once

#include "s3g_ambisonic_geometry.h"
#include "s3g_math.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

constexpr uint32_t kAmbiMembraneKickChannels = 16u;
constexpr uint32_t kAmbiMembraneKickPatches = 16u;
constexpr uint32_t kAmbiMembraneKickModes = 12u;
// FORMAT values 1-3 retain their historical ambisonic-order meaning. Four is
// the state-compatible direct format: one post-membrane pickup per lane. Five
// folds those same physical pickups onto output lanes one and two.
constexpr uint32_t kAmbiMembraneKickDirectPickups = 4u;
constexpr uint32_t kAmbiMembraneKickStereoDownmix = 5u;

enum class AmbiMembraneShape : uint32_t {
    Circle = 0u,
    Ellipse,
    Square,
    Triangle,
    Irregular,
};

enum class AmbiMembraneStrikeMode : uint32_t {
    Fixed = 0u,
    RandomArea,
    RandomRim,
};

struct AmbiMembraneKickParams {
    uint32_t order = 3u;
    AmbiMembraneShape shape = AmbiMembraneShape::Circle;
    float tuneHz = 43.0f;
    float pitchSweepSemitones = 31.0f;
    float pitchSweepMs = 42.0f;
    float decaySeconds = 1.45f;
    float damping = 0.26f;
    float punch = 0.76f;
    float click = 0.16f;
    float drive = 0.28f;
    float strikeX = 0.18f;
    float strikeY = -0.08f;
    AmbiMembraneStrikeMode strikeMode = AmbiMembraneStrikeMode::Fixed;
    float spatialSpread = 0.72f;
    float membraneDepth = 0.42f;
    float rotationDeg = 0.0f;
    float shapeAmount = 0.72f;
    float velocitySensitivity = 1.0f;
    float noteTracking = 1.0f;
    float outputGainDb = -8.0f;
};

// A deliberately compact membrane instrument rather than a bank of panned
// oscillators. Twelve shared modes are sampled at sixteen points on the
// membrane. Each point has its own ACN/SN3D radiation basis, so a strike bends
// and decays as one body while different parts of that body occupy different
// directions in the sound field.
class AmbiMembraneKick {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = std::isfinite(sampleRate)
            ? std::clamp(sampleRate, 8000.0, 768000.0) : 48000.0;
        radiationInitialized_ = false;
        rebuildShape();
        reset();
    }

    void reset()
    {
        for (auto& mode : modes_) mode = {};
        ageSamples_ = inactiveAge();
        triggerTuneHz_ = params_.tuneHz;
        actualStrikeX_ = params_.strikeX;
        actualStrikeY_ = params_.strikeY;
        clickEnvelope_ = 0.0f;
        clickLow_ = 0.0f;
        limiterGain_ = 1.0f;
        smoothedOutputGain_ = dbToLinear(params_.outputGainDb);
        smoothedDirectMix_ = params_.order
                == kAmbiMembraneKickDirectPickups
            ? 1.0f : 0.0f;
        smoothedStereoMix_ = params_.order
                == kAmbiMembraneKickStereoDownmix
            ? 1.0f : 0.0f;
        if (radiationInitialized_) {
            patchBasis_ = targetPatchBasis_;
            smoothedSpatialNormalization_ = targetSpatialNormalization_;
        }
        activity_ = 0.0f;
        rng_ = 0x6d2b79f5u;
        strikeRng_ = 0x91e10da5u;
    }

    void setParams(AmbiMembraneKickParams params)
    {
        sanitize(params);
        const bool shapeChanged = params.shape != params_.shape
            || params.shapeAmount != params_.shapeAmount;
        const bool radiationChanged = shapeChanged
            || params.spatialSpread != params_.spatialSpread
            || params.membraneDepth != params_.membraneDepth
            || params.rotationDeg != params_.rotationDeg;
        params_ = params;
        if (!active()) {
            smoothedDirectMix_ = params_.order
                    == kAmbiMembraneKickDirectPickups
                ? 1.0f : 0.0f;
            smoothedStereoMix_ = params_.order
                    == kAmbiMembraneKickStereoDownmix
                ? 1.0f : 0.0f;
        }
        if (params_.strikeMode == AmbiMembraneStrikeMode::Fixed
            || ageSamples_ == inactiveAge()) {
            actualStrikeX_ = params_.strikeX;
            actualStrikeY_ = params_.strikeY;
        }
        if (shapeChanged) rebuildShape();
        else if (radiationChanged) rebuildRadiation();
    }

    AmbiMembraneKickParams params() const { return params_; }

    void trigger(float velocity = 1.0f, int midiNote = 36)
    {
        selectStrikePosition();
        velocity = finiteClamp(velocity, 1.0f, 0.0f, 1.0f);
        // A convex strike curve preserves playable dynamics after the local
        // membrane saturation. VELOCITY interpolates from a fixed force to
        // this fully expressive response.
        const float expressiveVelocity = velocity * velocity;
        const float velocityGain = 1.0f - params_.velocitySensitivity
            + params_.velocitySensitivity * expressiveVelocity;
        const float trackedSemitones = static_cast<float>(midiNote - 36)
            * params_.noteTracking;
        triggerTuneHz_ = std::clamp(params_.tuneHz
                * std::pow(2.0f, trackedSemitones / 12.0f),
            20.0f, 180.0f);
        ageSamples_ = 0u;
        clickEnvelope_ = velocityGain * params_.click;

        const float strikeRadius = std::min(0.98f,
            std::sqrt(actualStrikeX_ * actualStrikeX_
                + actualStrikeY_ * actualStrikeY_));
        const float strikeTheta = std::atan2(
            actualStrikeY_, actualStrikeX_);
        for (uint32_t index = 0u; index < kAmbiMembraneKickModes; ++index) {
            const float shape = modeShape(index, strikeRadius, strikeTheta);
            const float ratio = modeRatio(index);
            const float spectralFalloff = 1.0f
                / std::pow(std::max(1.0f, ratio), 1.12f + params_.damping * 0.72f);
            const float centerWeight = index == 0u
                ? 1.0f + params_.punch * 0.86f : 0.42f + std::fabs(shape) * 0.72f;
            const float force = velocityGain * spectralFalloff * centerWeight
                * (index == 0u ? 1.0f : shape);
            auto& mode = modes_[index];
            if (std::fabs(mode.amplitude) < 1.0e-5f) {
                mode.phase = 0.0;
            }
            mode.amplitude = std::clamp(mode.amplitude + force, -2.0f, 2.0f);
        }
    }

    void processFrame(float* output, uint32_t outputChannels)
    {
        if (!output || outputChannels == 0u) return;
        outputChannels = std::min(outputChannels, kAmbiMembraneKickChannels);
        std::fill(output, output + outputChannels, 0.0f);

        const uint32_t ambisonicOrder = std::min(params_.order, 3u);
        const uint32_t ambisonicChannels = std::min(outputChannels,
            (ambisonicOrder + 1u) * (ambisonicOrder + 1u));
        const float ageSeconds = ageSamples_ < inactiveAge()
            ? static_cast<float>(ageSamples_ / sampleRate_) : 1000.0f;
        const float sweepSeconds = params_.pitchSweepMs * 0.001f;
        const float sweepEnvelope = std::exp(-ageSeconds
            / std::max(0.001f, sweepSeconds));
        const float sweptFundamental = triggerTuneHz_ * std::pow(2.0f,
            params_.pitchSweepSemitones * sweepEnvelope / 12.0f);

        std::array<float, kAmbiMembraneKickModes> modalSamples {};
        float maximumAmplitude = 0.0f;
        for (uint32_t index = 0u; index < kAmbiMembraneKickModes; ++index) {
            auto& mode = modes_[index];
            const float ratio = modeRatio(index);
            const float frequency = std::clamp(sweptFundamental * ratio,
                10.0f, static_cast<float>(sampleRate_ * 0.43));
            mode.phase += 6.28318530717958647692
                * static_cast<double>(frequency) / sampleRate_;
            if (mode.phase >= 6.28318530717958647692) {
                mode.phase -= 6.28318530717958647692;
            }
            const float highMode = std::max(0.0f, ratio - 1.0f);
            const float modeDecay = params_.decaySeconds
                / (1.0f + highMode * (0.10f + params_.damping * 1.34f));
            const float decayCoefficient = std::exp(-1.0f
                / std::max(1.0f, modeDecay * static_cast<float>(sampleRate_)));
            mode.amplitude *= decayCoefficient;
            if (std::fabs(mode.amplitude) < 1.0e-9f) mode.amplitude = 0.0f;
            modalSamples[index] = std::sin(mode.phase) * mode.amplitude;
            maximumAmplitude = std::max(maximumAmplitude,
                std::fabs(mode.amplitude));
        }

        const float white = randomSigned();
        const float clickCoefficient = std::exp(-1.0f
            / std::max(1.0, sampleRate_ * 0.0065));
        clickEnvelope_ *= clickCoefficient;
        clickLow_ += (white - clickLow_) * 0.11f;
        const float click = (white - clickLow_) * clickEnvelope_;

        const float spatialSmoothing = 1.0f - std::exp(-1.0f
            / std::max(1.0, sampleRate_ * 0.012));
        smoothedSpatialNormalization_ += (targetSpatialNormalization_
            - smoothedSpatialNormalization_) * spatialSmoothing;
        for (uint32_t patch = 0u;
             patch < kAmbiMembraneKickPatches; ++patch) {
            for (uint32_t channel = 0u;
                 channel < kAmbiMembraneKickChannels; ++channel) {
                patchBasis_[patch][channel] +=
                    (targetPatchBasis_[patch][channel]
                        - patchBasis_[patch][channel]) * spatialSmoothing;
            }
        }

        const float driveGain = 1.0f + params_.drive * 8.0f;
        const float normalization = smoothedSpatialNormalization_;
        constexpr float directPickupNormalization = 0.22f;
        constexpr float stereoFoldNormalization = 0.25f;
        std::array<float, kAmbiMembraneKickPatches> pickupSamples {};
        std::array<float, 2u> stereoSamples {};
        for (uint32_t patch = 0u; patch < kAmbiMembraneKickPatches; ++patch) {
            float sample = 0.0f;
            for (uint32_t mode = 0u; mode < kAmbiMembraneKickModes; ++mode) {
                sample += modalSamples[mode] * modeShapes_[patch][mode];
            }
            const float dx = patchPositions_[patch][0] - actualStrikeX_;
            const float dy = patchPositions_[patch][1] - actualStrikeY_;
            const float strikeDistance = std::sqrt(dx * dx + dy * dy);
            const float localClick = click * std::exp(-strikeDistance * 3.8f);
            sample = softSat((sample + localClick * 0.82f) * driveGain)
                / softSat(driveGain);
            pickupSamples[patch] = sample * directPickupNormalization;
            stereoSamples[0u] += pickupSamples[patch]
                * stereoPanGains_[patch][0u]
                * stereoFoldNormalization;
            stereoSamples[1u] += pickupSamples[patch]
                * stereoPanGains_[patch][1u]
                * stereoFoldNormalization;
            sample *= normalization;
            for (uint32_t channel = 0u;
                 channel < ambisonicChannels; ++channel) {
                output[channel] += sample * patchBasis_[patch][channel];
            }
        }

        const float directTarget = params_.order
                == kAmbiMembraneKickDirectPickups
            ? 1.0f : 0.0f;
        smoothedDirectMix_ += (directTarget - smoothedDirectMix_)
            * spatialSmoothing;
        const float stereoTarget = params_.order
                == kAmbiMembraneKickStereoDownmix
            ? 1.0f : 0.0f;
        smoothedStereoMix_ += (stereoTarget - smoothedStereoMix_)
            * spatialSmoothing;
        const float ambisonicMix = std::max(0.0f,
            1.0f - smoothedDirectMix_ - smoothedStereoMix_);
        for (uint32_t channel = 0u; channel < outputChannels; ++channel) {
            const float stereoSample = channel < stereoSamples.size()
                ? stereoSamples[channel] : 0.0f;
            output[channel] = output[channel] * ambisonicMix
                + pickupSamples[channel] * smoothedDirectMix_
                + stereoSample * smoothedStereoMix_;
        }

        const float outputTarget = dbToLinear(params_.outputGainDb);
        const float outputSmoothing = 1.0f - std::exp(-1.0f
            / std::max(1.0, sampleRate_ * 0.012));
        smoothedOutputGain_ += (outputTarget - smoothedOutputGain_)
            * outputSmoothing;
        float peak = 0.0f;
        for (uint32_t channel = 0u; channel < outputChannels; ++channel) {
            peak = std::max(peak, std::fabs(output[channel]
                * smoothedOutputGain_));
        }
        const float desiredLimiter = peak > 0.96f ? 0.96f / peak : 1.0f;
        if (desiredLimiter < limiterGain_) {
            limiterGain_ = desiredLimiter;
        } else {
            limiterGain_ += (desiredLimiter - limiterGain_)
                * (1.0f - std::exp(-1.0f
                    / std::max(1.0, sampleRate_ * 0.090)));
        }
        const float finalGain = smoothedOutputGain_ * limiterGain_;
        peak = 0.0f;
        for (uint32_t channel = 0u; channel < outputChannels; ++channel) {
            output[channel] = flushDenormal(output[channel] * finalGain);
            peak = std::max(peak, std::fabs(output[channel]));
        }

        activity_ += (peak - activity_) * (peak > activity_ ? 0.16f : 0.0018f);
        if (maximumAmplitude <= 1.0e-8f && clickEnvelope_ <= 1.0e-8f) {
            activity_ = activity_ < 1.0e-8f ? 0.0f : activity_;
        }
        if (ageSamples_ < inactiveAge()) ++ageSamples_;
    }

    void processBlock(float* const* outputs, uint32_t outputChannels,
        uint32_t frames)
    {
        if (!outputs || frames == 0u) return;
        outputChannels = std::min(outputChannels, kAmbiMembraneKickChannels);
        std::array<float, kAmbiMembraneKickChannels> frame {};
        for (uint32_t sample = 0u; sample < frames; ++sample) {
            processFrame(frame.data(), outputChannels);
            for (uint32_t channel = 0u; channel < outputChannels; ++channel) {
                if (outputs[channel]) outputs[channel][sample] = frame[channel];
            }
        }
    }

    float activity() const { return activity_; }

    bool active() const
    {
        if (clickEnvelope_ > 1.0e-7f) return true;
        for (const auto& mode : modes_) {
            if (std::fabs(mode.amplitude) > 1.0e-7f) return true;
        }
        return false;
    }

    float modeRatio(uint32_t index) const
    {
        index = std::min(index, kAmbiMembraneKickModes - 1u);
        constexpr std::array<std::array<float, kAmbiMembraneKickModes>, 5u>
            ratios {{
                {{ 1.000f, 1.593f, 1.593f, 2.135f, 2.295f, 2.295f,
                   2.653f, 2.653f, 2.918f, 2.918f, 3.500f, 3.600f }},
                {{ 1.000f, 1.470f, 1.735f, 2.090f, 2.180f, 2.455f,
                   2.545f, 2.830f, 2.900f, 3.180f, 3.410f, 3.790f }},
                {{ 1.000f, 1.581f, 1.581f, 2.000f, 2.236f, 2.236f,
                   2.550f, 2.550f, 2.915f, 2.915f, 3.000f, 3.162f }},
                {{ 1.000f, 1.528f, 1.528f, 2.000f, 2.082f, 2.082f,
                   2.517f, 2.517f, 2.646f, 3.000f, 3.055f, 3.055f }},
                {{ 1.000f, 1.421f, 1.786f, 2.041f, 2.267f, 2.491f,
                   2.612f, 2.847f, 3.011f, 3.196f, 3.437f, 3.821f }},
            }};
        const uint32_t shape = std::min<uint32_t>(
            static_cast<uint32_t>(params_.shape), 4u);
        return ratios[0u][index] + (ratios[shape][index] - ratios[0u][index])
            * params_.shapeAmount;
    }

    std::array<float, 2u> patchPosition(uint32_t patch) const
    {
        return patchPositions_[std::min(patch,
            kAmbiMembraneKickPatches - 1u)];
    }

    std::array<float, 2u> actualStrikePosition() const
    {
        return { actualStrikeX_, actualStrikeY_ };
    }

private:
    struct ModeState {
        double phase = 0.0;
        float amplitude = 0.0f;
    };

    static constexpr uint64_t inactiveAge()
    {
        return static_cast<uint64_t>(-1);
    }

    static float finiteClamp(float value, float fallback,
        float minimum, float maximum)
    {
        return std::clamp(std::isfinite(value) ? value : fallback,
            minimum, maximum);
    }

    static float dbToLinear(float db)
    {
        return db <= -120.0f ? 0.0f : std::pow(10.0f, db * 0.05f);
    }

    static void sanitize(AmbiMembraneKickParams& params)
    {
        params.order = std::clamp<uint32_t>(
            params.order, 1u, kAmbiMembraneKickStereoDownmix);
        params.shape = static_cast<AmbiMembraneShape>(std::min<uint32_t>(
            static_cast<uint32_t>(params.shape), 4u));
        params.tuneHz = finiteClamp(params.tuneHz, 43.0f, 25.0f, 90.0f);
        params.pitchSweepSemitones = finiteClamp(
            params.pitchSweepSemitones, 31.0f, 0.0f, 48.0f);
        params.pitchSweepMs = finiteClamp(
            params.pitchSweepMs, 42.0f, 5.0f, 250.0f);
        params.decaySeconds = finiteClamp(
            params.decaySeconds, 1.45f, 0.08f, 6.0f);
        params.damping = finiteClamp(params.damping, 0.26f, 0.0f, 1.0f);
        params.punch = finiteClamp(params.punch, 0.76f, 0.0f, 1.0f);
        params.click = finiteClamp(params.click, 0.16f, 0.0f, 1.0f);
        params.drive = finiteClamp(params.drive, 0.28f, 0.0f, 1.0f);
        params.strikeX = finiteClamp(params.strikeX, 0.18f, -1.0f, 1.0f);
        params.strikeY = finiteClamp(params.strikeY, -0.08f, -1.0f, 1.0f);
        const float strikeRadius = std::sqrt(params.strikeX * params.strikeX
            + params.strikeY * params.strikeY);
        if (strikeRadius > 0.98f) {
            params.strikeX *= 0.98f / strikeRadius;
            params.strikeY *= 0.98f / strikeRadius;
        }
        params.strikeMode = static_cast<AmbiMembraneStrikeMode>(
            std::min<uint32_t>(
                static_cast<uint32_t>(params.strikeMode), 2u));
        params.spatialSpread = finiteClamp(
            params.spatialSpread, 0.72f, 0.0f, 1.0f);
        params.membraneDepth = finiteClamp(
            params.membraneDepth, 0.42f, 0.0f, 1.0f);
        params.rotationDeg = finiteClamp(
            params.rotationDeg, 0.0f, -180.0f, 180.0f);
        params.shapeAmount = finiteClamp(
            params.shapeAmount, 0.72f, 0.0f, 1.0f);
        params.velocitySensitivity = finiteClamp(
            params.velocitySensitivity, 1.0f, 0.0f, 1.0f);
        params.noteTracking = finiteClamp(
            params.noteTracking, 1.0f, 0.0f, 1.0f);
        params.outputGainDb = finiteClamp(
            params.outputGainDb, -8.0f, -60.0f, 6.0f);
    }

    static std::array<uint32_t, 3u> modeIdentity(uint32_t mode)
    {
        // angular order, radial order, sine variant
        constexpr std::array<std::array<uint32_t, 3u>,
            kAmbiMembraneKickModes> identities {{
                {{ 0u, 1u, 0u }}, {{ 1u, 1u, 0u }}, {{ 1u, 1u, 1u }},
                {{ 0u, 2u, 0u }}, {{ 2u, 1u, 0u }}, {{ 2u, 1u, 1u }},
                {{ 1u, 2u, 0u }}, {{ 1u, 2u, 1u }},
                {{ 3u, 1u, 0u }}, {{ 3u, 1u, 1u }},
                {{ 0u, 3u, 0u }}, {{ 4u, 1u, 0u }},
            }};
        return identities[std::min(mode, kAmbiMembraneKickModes - 1u)];
    }

    static float modeShape(uint32_t mode, float radius, float theta)
    {
        const auto identity = modeIdentity(mode);
        const uint32_t angular = identity[0u];
        const uint32_t radial = identity[1u];
        const bool sineVariant = identity[2u] != 0u;
        radius = std::clamp(radius, 0.0f, 1.0f);
        float radialShape = 0.0f;
        if (radial == 1u) {
            radialShape = std::cos(radius * 1.57079632679489661923f);
        } else {
            radialShape = std::cos(
                radius * (static_cast<float>(radial) - 0.5f)
                * 3.14159265358979323846f);
        }
        if (angular == 0u) return radialShape;
        const float angularShape = sineVariant
            ? std::sin(static_cast<float>(angular) * theta)
            : std::cos(static_cast<float>(angular) * theta);
        return radialShape * angularShape
            * std::pow(std::max(0.03f, radius),
                std::min(2.0f, static_cast<float>(angular) * 0.52f));
    }

    void rebuildShape()
    {
        constexpr float pi = 3.14159265358979323846f;
        for (uint32_t patch = 0u; patch < kAmbiMembraneKickPatches; ++patch) {
            const uint32_t ring = patch / 4u;
            const uint32_t spoke = patch % 4u;
            const float radius = 0.18f + static_cast<float>(ring) * 0.22f;
            const float theta = static_cast<float>(spoke) * pi * 0.5f
                + static_cast<float>(ring & 1u) * pi * 0.25f;
            float x = radius * std::cos(theta);
            float y = radius * std::sin(theta);
            float shapedX = x;
            float shapedY = y;
            const float amount = params_.shapeAmount;
            switch (params_.shape) {
            case AmbiMembraneShape::Ellipse:
                shapedX = x * (1.0f + amount * 0.42f);
                shapedY = y * (1.0f - amount * 0.30f);
                break;
            case AmbiMembraneShape::Square: {
                const float maximum = std::max(0.001f,
                    std::max(std::fabs(std::cos(theta)),
                        std::fabs(std::sin(theta))));
                const float squareScale = 1.0f / maximum;
                shapedX = x * (1.0f + (squareScale - 1.0f) * amount);
                shapedY = y * (1.0f + (squareScale - 1.0f) * amount);
                break;
            }
            case AmbiMembraneShape::Triangle: {
                const float local = std::remainder(theta, 2.0f * pi / 3.0f);
                const float boundary = 0.5f / std::max(0.51f,
                    std::cos(pi / 3.0f - std::fabs(local)));
                const float triangleScale = 1.0f + (boundary - 1.0f) * amount;
                shapedX = x * triangleScale;
                shapedY = y * triangleScale;
                break;
            }
            case AmbiMembraneShape::Irregular: {
                const float wobble = 1.0f + amount
                    * (0.17f * std::sin(theta * 3.0f + 0.7f)
                        + 0.09f * std::sin(theta * 5.0f - 0.4f));
                shapedX = x * wobble + amount * y * y * 0.10f;
                shapedY = y * wobble - amount * x * 0.08f;
                break;
            }
            case AmbiMembraneShape::Circle:
            default:
                break;
            }
            patchPositions_[patch] = { shapedX, shapedY };
            const float stereoPan = std::clamp(
                shapedX * 0.5f + 0.5f, 0.0f, 1.0f)
                * 1.57079632679489661923f;
            stereoPanGains_[patch] = {
                std::cos(stereoPan), std::sin(stereoPan)
            };
            const float shapedRadius = std::min(1.0f,
                std::sqrt(shapedX * shapedX + shapedY * shapedY));
            const float shapedTheta = std::atan2(shapedY, shapedX);
            for (uint32_t mode = 0u; mode < kAmbiMembraneKickModes; ++mode) {
                modeShapes_[patch][mode] = modeShape(
                    mode, shapedRadius, shapedTheta);
            }

        }
        rebuildRadiation();
    }

    void rebuildRadiation()
    {
        for (uint32_t patch = 0u; patch < kAmbiMembraneKickPatches; ++patch) {
            const float shapedX = patchPositions_[patch][0u];
            const float shapedY = patchPositions_[patch][1u];
            const float shapedRadius = std::min(1.0f,
                std::sqrt(shapedX * shapedX + shapedY * shapedY));
            const float azimuth = params_.rotationDeg
                + shapedX * 150.0f * params_.spatialSpread;
            const float elevation = shapedY * 68.0f * params_.spatialSpread
                + (1.0f - shapedRadius) * params_.membraneDepth * 34.0f;
            targetPatchBasis_[patch] = acnSn3dBasis(directionFromAed(
                azimuth, std::clamp(elevation, -89.0f, 89.0f)));
        }
        targetSpatialNormalization_ = 0.052f / std::sqrt(
            0.32f + params_.spatialSpread * 0.68f);
        if (!radiationInitialized_) {
            patchBasis_ = targetPatchBasis_;
            smoothedSpatialNormalization_ = targetSpatialNormalization_;
            radiationInitialized_ = true;
        }
    }

    float randomSigned()
    {
        rng_ ^= rng_ << 13u;
        rng_ ^= rng_ >> 17u;
        rng_ ^= rng_ << 5u;
        return static_cast<float>(rng_ & 0x00ffffffu)
            * (2.0f / 16777215.0f) - 1.0f;
    }

    float strikeRandomUnit()
    {
        strikeRng_ ^= strikeRng_ << 13u;
        strikeRng_ ^= strikeRng_ >> 17u;
        strikeRng_ ^= strikeRng_ << 5u;
        return static_cast<float>(strikeRng_ & 0x00ffffffu)
            / 16777215.0f;
    }

    void selectStrikePosition()
    {
        if (params_.strikeMode == AmbiMembraneStrikeMode::Fixed) {
            actualStrikeX_ = params_.strikeX;
            actualStrikeY_ = params_.strikeY;
            return;
        }
        constexpr float twoPi = 6.28318530717958647692f;
        const float angle = strikeRandomUnit() * twoPi;
        const float radialUnit = strikeRandomUnit();
        const float radius = params_.strikeMode
                == AmbiMembraneStrikeMode::RandomRim
            ? 0.68f + radialUnit * 0.28f
            : std::sqrt(radialUnit) * 0.94f;
        actualStrikeX_ = std::cos(angle) * radius;
        actualStrikeY_ = std::sin(angle) * radius;
    }

    double sampleRate_ = 48000.0;
    AmbiMembraneKickParams params_ {};
    std::array<ModeState, kAmbiMembraneKickModes> modes_ {};
    std::array<std::array<float, 2u>, kAmbiMembraneKickPatches>
        patchPositions_ {};
    std::array<std::array<float, 2u>, kAmbiMembraneKickPatches>
        stereoPanGains_ {};
    std::array<std::array<float, kAmbiMembraneKickModes>,
        kAmbiMembraneKickPatches> modeShapes_ {};
    std::array<std::array<float, kAmbiMembraneKickChannels>,
        kAmbiMembraneKickPatches> patchBasis_ {};
    std::array<std::array<float, kAmbiMembraneKickChannels>,
        kAmbiMembraneKickPatches> targetPatchBasis_ {};
    uint64_t ageSamples_ = inactiveAge();
    float triggerTuneHz_ = 43.0f;
    float actualStrikeX_ = 0.18f;
    float actualStrikeY_ = -0.08f;
    float clickEnvelope_ = 0.0f;
    float clickLow_ = 0.0f;
    float limiterGain_ = 1.0f;
    float smoothedOutputGain_ = 1.0f;
    float smoothedSpatialNormalization_ = 0.052f;
    float targetSpatialNormalization_ = 0.052f;
    float smoothedDirectMix_ = 0.0f;
    float smoothedStereoMix_ = 0.0f;
    float activity_ = 0.0f;
    bool radiationInitialized_ = false;
    uint32_t rng_ = 0x6d2b79f5u;
    uint32_t strikeRng_ = 0x91e10da5u;
};

} // namespace s3g
