#pragma once

#include "s3g_ambi_environment_field.h"
#include "s3g_ambi_field_listener.h"
#include "s3g_ambisonic_speaker_decoder.h"
#include "s3g_parameter_surface.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

inline constexpr uint32_t kGeologicalFieldMaxVoices = 64u;
inline constexpr uint32_t kGeologicalFieldMaxChannels = 64u;

struct GeologicalFieldPoint {
    float azimuthDeg = 0.0f;
    float elevationDeg = 0.0f;
    float distance = 1.0f;
};

struct GeologicalFieldParams {
    uint32_t voices = 24u;
    float spread = 0.5f;
    float deviation = 0.15f;
    float motionRateHz = 0.08f;
    float transport = 0.3f;
    float shear = 0.2f;
    float curl = 0.2f;
    float vertical = 0.0f;
    float centerAzimuthDeg = 0.0f;
    float centerElevationDeg = 0.0f;
    float centerDistance = 1.0f;
    float spatialFollow = 0.75f;
    AmbiEnvironmentProfileId environmentProfile =
        AmbiEnvironmentProfileId::Open;
    float environmentAmount = 0.1f;
    float environmentSize = 0.5f;
    float environmentDecay = 0.5f;
    float environmentDamping = 0.5f;
    AmbiFieldListenMode fieldListenMode = AmbiFieldListenMode::Off;
    float fieldListenAmount = 1.0f;
    AmbiFieldListenerResponse fieldListenResponse =
        AmbiFieldListenerResponse::Legacy;
};

// Spatial infrastructure shared by geology-driven instruments. It deliberately
// contains no source synthesis: agents provide aperiodic force, fracture, flow,
// and contact signals, while this class only positions and projects them.
class GeologicalField {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = std::max(1000.0, sampleRate);
        environment_.prepare(sampleRate_);
        listener_.prepare(sampleRate_);
        listener_.setMemorySeconds(0.82f);
        listener_.setExtendedAnalysisEnabled(true);
        const auto& directions = ambiFieldListenerCubeDirections();
        listener_.setDirections(directions.data(),
            static_cast<uint32_t>(directions.size()));
        reset();
    }

    void reset()
    {
        motionPhase_ = 0.0f;
        environment_.setProfile(params_.environmentProfile);
        environment_.setAmount(
            ambiEnvironmentSpaceAmount(params_.environmentAmount));
        environment_.setShape(params_.environmentSize,
            params_.environmentDecay, params_.environmentDamping);
        environment_.reset();
        listener_.reset();
        for (uint32_t voice = 0u; voice < kGeologicalFieldMaxVoices;
            ++voice) {
            points_[voice] = basePoint(voice);
            targets_[voice] = points_[voice];
            directions_[voice] = directionFromAed(
                points_[voice].azimuthDeg, points_[voice].elevationDeg);
            bases_[voice] = acnSn3dBasis7(directions_[voice]);
        }
    }

    void setParams(GeologicalFieldParams params)
    {
        params.voices = std::clamp<uint32_t>(
            params.voices, 1u, kGeologicalFieldMaxVoices);
        params.spread = finiteClamp(params.spread, params_.spread, 0.0f, 1.0f);
        params.deviation = finiteClamp(
            params.deviation, params_.deviation, 0.0f, 1.0f);
        params.motionRateHz = finiteClamp(
            params.motionRateHz, params_.motionRateHz, 0.0001f, 3.0f);
        params.transport = finiteClamp(
            params.transport, params_.transport, 0.0f, 1.5f);
        params.shear = finiteClamp(params.shear, params_.shear, 0.0f, 1.5f);
        params.curl = finiteClamp(params.curl, params_.curl, 0.0f, 1.5f);
        params.vertical = finiteClamp(
            params.vertical, params_.vertical, -1.0f, 1.0f);
        params.centerAzimuthDeg = finiteClamp(params.centerAzimuthDeg,
            params_.centerAzimuthDeg, -180.0f, 180.0f);
        params.centerElevationDeg = finiteClamp(params.centerElevationDeg,
            params_.centerElevationDeg, -90.0f, 90.0f);
        params.centerDistance = finiteClamp(
            params.centerDistance, params_.centerDistance, 0.15f, 2.0f);
        params.spatialFollow = finiteClamp(
            params.spatialFollow, params_.spatialFollow, 0.0f, 1.0f);
        params.environmentAmount = finiteClamp(params.environmentAmount,
            params_.environmentAmount, 0.0f, 1.0f);
        params.environmentSize = finiteClamp(params.environmentSize,
            params_.environmentSize, 0.0f, 1.0f);
        params.environmentDecay = finiteClamp(params.environmentDecay,
            params_.environmentDecay, 0.0f, 1.0f);
        params.environmentDamping = finiteClamp(params.environmentDamping,
            params_.environmentDamping, 0.0f, 1.0f);
        params.fieldListenMode = sanitizeAmbiFieldListenMode(
            params.fieldListenMode);
        params.fieldListenAmount = finiteClamp(params.fieldListenAmount,
            params_.fieldListenAmount, 0.0f, 1.0f);
        params.fieldListenResponse = sanitizeAmbiFieldListenerResponse(
            params.fieldListenResponse);
        params_ = params;
        environment_.setProfile(params_.environmentProfile);
        environment_.setAmount(
            ambiEnvironmentSpaceAmount(params_.environmentAmount));
        environment_.setShape(params_.environmentSize,
            params_.environmentDecay, params_.environmentDamping);
    }

    const GeologicalFieldParams& params() const { return params_; }

    void setParameterSurfaceGlideMs(float glideMs)
    {
        parameterSurfaceGlideMs_ = finiteClamp(
            glideMs, parameterSurfaceGlideMs_, 0.0f, 60000.0f);
    }

    void setParameterSurfaceVoiceMembership(
        const std::array<float, kGeologicalFieldMaxVoices>& membership)
    {
        surfaceMembership_ = membership;
        surfaceVoiceLimit_ = 1u;
        for (uint32_t voice = 0u; voice < surfaceMembership_.size(); ++voice) {
            surfaceMembership_[voice] = finiteClamp(
                surfaceMembership_[voice], 0.0f, 0.0f, 1.0f);
            if (surfaceMembership_[voice] > 1.0e-6f) {
                surfaceVoiceLimit_ = voice + 1u;
            }
        }
        surfaceMembershipEnabled_ = true;
    }

    void clearParameterSurfaceVoiceMembership()
    {
        surfaceMembership_.fill(1.0f);
        surfaceMembershipEnabled_ = false;
        surfaceVoiceLimit_ = params_.voices;
    }

    uint32_t processingVoiceCount() const
    {
        return surfaceMembershipEnabled_
            ? std::max(params_.voices, surfaceVoiceLimit_) : params_.voices;
    }

    float voiceRenderGain(uint32_t voice) const
    {
        if (voice >= kGeologicalFieldMaxVoices) return 0.0f;
        if (!surfaceMembershipEnabled_) {
            return voice < params_.voices ? 1.0f : 0.0f;
        }
        return surfaceMembership_[voice];
    }

    float voiceMass() const
    {
        if (!surfaceMembershipEnabled_) {
            return static_cast<float>(std::max<uint32_t>(1u, params_.voices));
        }
        return std::max(1.0f,
            parameterSurfaceVoiceMass(surfaceMembership_));
    }

    GeologicalFieldPoint point(uint32_t voice) const
    {
        return points_[std::min<uint32_t>(
            voice, kGeologicalFieldMaxVoices - 1u)];
    }

    Vec3 direction(uint32_t voice) const
    {
        return directions_[std::min<uint32_t>(
            voice, kGeologicalFieldMaxVoices - 1u)];
    }

    const std::array<float, kGeologicalFieldMaxChannels>& basis(
        uint32_t voice) const
    {
        return bases_[std::min<uint32_t>(
            voice, kGeologicalFieldMaxVoices - 1u)];
    }

    float distanceGain(uint32_t voice) const
    {
        return 1.0f / std::max(0.48f, point(voice).distance);
    }

    void update(float deltaSeconds, const float* activity = nullptr)
    {
        const float dt = std::max(0.0f, deltaSeconds);
        motionPhase_ += dt * params_.motionRateHz;
        motionPhase_ -= std::floor(motionPhase_);
        const uint32_t voices = processingVoiceCount();
        const Vec3 listenedDirection = listener_.preferredDirection(
            params_.fieldListenMode);
        const float listenActivity = listener_.activity()
            * params_.fieldListenAmount;
        const float glideSeconds = parameterSurfaceGlideMs_ * 0.001f;
        const float followRate = (0.42f + (1.0f - params_.spatialFollow)
            * 8.2f) / (1.0f + glideSeconds * 0.35f);
        const float follow = 1.0f - std::exp(-dt * followRate);

        for (uint32_t voice = 0u; voice < voices; ++voice) {
            const float identity = hash01(voice * 0x9e3779b9u + 0x51f15eedu);
            const float identityB = hash01(voice * 0x85ebca6bu + 0x7f4a7c15u);
            const float phase = motionPhase_ + identity;
            const float event = activity
                ? clamp(std::isfinite(activity[voice]) ? activity[voice] : 0.0f,
                    0.0f, 2.0f) : 0.0f;
            const float angularSpread = 10.0f + params_.spread * 168.0f;
            float azimuth = params_.centerAzimuthDeg
                + hashSigned(voice * 0x27d4eb2du + 17u) * angularSpread;
            float elevation = params_.centerElevationDeg
                + hashSigned(voice * 0x165667b1u + 31u)
                    * (8.0f + params_.spread * 36.0f);
            azimuth += std::sin((phase * 2.0f + identityB) * kPi)
                    * params_.transport * (8.0f + event * 12.0f)
                + std::sin((phase * 3.0f + identity) * kPi)
                    * params_.curl * 18.0f;
            elevation += std::cos((phase * 1.7f + identityB) * kPi)
                    * params_.shear * 10.0f
                + params_.vertical * (12.0f + event * 18.0f);
            float distance = params_.centerDistance
                * (0.72f + identityB * 0.52f)
                * (1.0f + std::sin((phase + identity) * kPi * 2.0f)
                    * params_.deviation * 0.32f);

            if (listenActivity > 0.0001f) {
                const Vec3 targetDirection = directionFromAed(
                    azimuth, clamp(elevation, -88.0f, 88.0f));
                const float steer = listenActivity * (0.04f + event * 0.06f);
                const Vec3 steered {
                    lerp(targetDirection.x, listenedDirection.x, steer),
                    lerp(targetDirection.y, listenedDirection.y, steer),
                    lerp(targetDirection.z, listenedDirection.z, steer),
                };
                const auto steeredPoint = pointFromDirection(steered, distance);
                azimuth = steeredPoint.azimuthDeg;
                elevation = steeredPoint.elevationDeg;
            }

            targets_[voice] = {
                wrapDegrees(azimuth),
                clamp(elevation, -88.0f, 88.0f),
                clamp(distance, 0.20f, 2.60f),
            };
            points_[voice].azimuthDeg = wrapDegrees(
                points_[voice].azimuthDeg + wrapDegrees(
                    targets_[voice].azimuthDeg - points_[voice].azimuthDeg)
                    * follow);
            points_[voice].elevationDeg +=
                (targets_[voice].elevationDeg - points_[voice].elevationDeg)
                    * follow;
            points_[voice].distance +=
                (targets_[voice].distance - points_[voice].distance) * follow;
            directions_[voice] = directionFromAed(
                points_[voice].azimuthDeg, points_[voice].elevationDeg);
            bases_[voice] = acnSn3dBasis7(directions_[voice]);
        }
        environment_.setMotion(params_.centerAzimuthDeg,
            params_.centerElevationDeg);
    }

    void beginEnvironmentFrame() { environment_.beginFrame(); }

    void addEnvironmentSource(float sample, uint32_t voice, float send)
    {
        environment_.addSource(sample, direction(voice), send);
    }

    std::array<float, kAmbiEnvironmentChannels> processEnvironment()
    {
        return environment_.process();
    }

    void processListenerFrame(const float* hoa, uint32_t channels)
    {
        listener_.processFrame(hoa, channels);
    }

    float listenerDrive(uint32_t voice) const
    {
        if (params_.fieldListenMode == AmbiFieldListenMode::Off) return 0.0f;
        const auto score = listener_.score(direction(voice),
            params_.fieldListenMode);
        float response = listener_.preference(direction(voice),
            params_.fieldListenMode) - 0.5f;
        switch (params_.fieldListenResponse) {
        case AmbiFieldListenerResponse::Excite:
            response += score.novelty * 0.42f + score.roughness * 0.26f;
            break;
        case AmbiFieldListenerResponse::Settle:
            response -= score.roughness * 0.34f
                + score.relativeEnergy * 0.18f;
            break;
        case AmbiFieldListenerResponse::Imprint:
            response += score.charge * 0.34f
                - score.habituation * 0.18f;
            break;
        case AmbiFieldListenerResponse::Legacy:
        default:
            break;
        }
        return clamp(response * listener_.activity()
                * params_.fieldListenAmount,
            -1.0f, 1.0f);
    }

private:
    static float finiteClamp(float value, float fallback, float low, float high)
    {
        return clamp(std::isfinite(value) ? value : fallback, low, high);
    }

    static uint32_t hash(uint32_t value)
    {
        value ^= value >> 16u;
        value *= 0x7feb352du;
        value ^= value >> 15u;
        value *= 0x846ca68bu;
        value ^= value >> 16u;
        return value;
    }

    static float hash01(uint32_t value)
    {
        return static_cast<float>(hash(value) & 0x00ffffffu)
            / static_cast<float>(0x01000000u);
    }

    static float hashSigned(uint32_t value) { return hash01(value) * 2.0f - 1.0f; }

    static float wrapDegrees(float value)
    {
        while (value > 180.0f) value -= 360.0f;
        while (value < -180.0f) value += 360.0f;
        return value;
    }

    GeologicalFieldPoint basePoint(uint32_t voice) const
    {
        const float azimuth = params_.centerAzimuthDeg
            + hashSigned(voice * 0x9e3779b9u + 7u)
                * (10.0f + params_.spread * 168.0f);
        const float elevation = params_.centerElevationDeg
            + hashSigned(voice * 0x85ebca6bu + 13u)
                * (8.0f + params_.spread * 36.0f);
        const float distance = params_.centerDistance
            * (0.72f + hash01(voice * 0xc2b2ae35u + 29u) * 0.52f);
        return { wrapDegrees(azimuth), clamp(elevation, -88.0f, 88.0f),
            clamp(distance, 0.20f, 2.60f) };
    }

    static GeologicalFieldPoint pointFromDirection(Vec3 direction,
        float distance)
    {
        direction = normalize(direction);
        return {
            wrapDegrees(std::atan2(direction.y, direction.x)
                * 180.0f / kPi),
            std::asin(clamp(direction.z, -1.0f, 1.0f)) * 180.0f / kPi,
            distance,
        };
    }

    GeologicalFieldParams params_ {};
    std::array<GeologicalFieldPoint, kGeologicalFieldMaxVoices> points_ {};
    std::array<GeologicalFieldPoint, kGeologicalFieldMaxVoices> targets_ {};
    std::array<Vec3, kGeologicalFieldMaxVoices> directions_ {};
    std::array<std::array<float, kGeologicalFieldMaxChannels>,
        kGeologicalFieldMaxVoices> bases_ {};
    std::array<float, kGeologicalFieldMaxVoices> surfaceMembership_ {};
    AmbiEnvironmentField environment_ {};
    AmbiFieldListener listener_ {};
    double sampleRate_ = 48000.0;
    float motionPhase_ = 0.0f;
    float parameterSurfaceGlideMs_ = 0.0f;
    uint32_t surfaceVoiceLimit_ = 1u;
    bool surfaceMembershipEnabled_ = false;
};

} // namespace s3g
