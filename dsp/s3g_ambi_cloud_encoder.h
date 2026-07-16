#pragma once

#include "s3g_ambi_encoder_depth.h"
#include "s3g_ambisonic_speaker_decoder.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

constexpr uint32_t kAmbiCloudEncoderMaxInputs = 64;
constexpr uint32_t kAmbiCloudEncoderMaxClouds = 4;
constexpr uint32_t kAmbiCloudEncoderMaxOrder = 7;
constexpr uint32_t kAmbiCloudEncoderMaxChannels = 64;

enum class AmbiCloudShape : uint32_t {
    Cumulus = 0,
    Stratus = 1,
    Cirrus = 2,
    Lenticular = 3,
    Storm = 4,
};

enum class AmbiCloudForce : uint32_t {
    Calm = 0,
    Advection = 1,
    Shear = 2,
    Convection = 3,
    Turbulence = 4,
};

struct AmbiCloud {
    float azimuthDeg = 0.0f;
    float elevationDeg = 0.0f;
    float distance = 1.0f;
    float gain = 1.0f;
};

struct AmbiCloudEncoderParams {
    uint32_t activeInputs = 64;
    uint32_t activeClouds = 1;
    uint32_t selectedCloud = 0;
    uint32_t order = 3;
    float spread = 0.45f;
    float elevationSpread = 0.35f;
    float jitter = 0.0f;
    float drift = 0.0f;
    float rateHz = 0.035f;
    float decorrelate = 0.0f;
    AmbiCloudShape shape = AmbiCloudShape::Cumulus;
    AmbiCloudForce force = AmbiCloudForce::Calm;
    float selectedAzimuthDeg = 0.0f;
    float selectedElevationDeg = 0.0f;
    float selectedDistance = 1.0f;
    float selectedGain = 1.0f;
    float doppler = 0.0f;
    float air = 0.0f;
    float outputGainDb = -12.0f;
};

class AmbiCloudEncoder {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        depth_.prepare(sampleRate_);
        resetScene();
    }

    void reset()
    {
        phase_ = 0.0f;
        seed_ = 0x45d9f3bu;
        depth_.reset();
    }

    void resetScene()
    {
        for (uint32_t i = 0; i < kAmbiCloudEncoderMaxClouds; ++i) {
            const float az = wrapSignedDeg(-45.0f - static_cast<float>(i) * 90.0f);
            clouds_[i] = { az, i == 0u ? 0.0f : 12.0f * (static_cast<float>(i & 1u) * 2.0f - 1.0f), 1.0f, 1.0f };
        }
        syncSelectedFromCloud();
        reset();
    }

    void setParams(AmbiCloudEncoderParams params)
    {
        params.activeInputs = std::clamp<uint32_t>(params.activeInputs, 1u, kAmbiCloudEncoderMaxInputs);
        params.activeClouds = std::clamp<uint32_t>(params.activeClouds, 1u, kAmbiCloudEncoderMaxClouds);
        params.selectedCloud = std::min<uint32_t>(params.selectedCloud, params.activeClouds - 1u);
        params.order = std::clamp<uint32_t>(params.order, 1u, kAmbiCloudEncoderMaxOrder);
        params.spread = clamp(params.spread, 0.0f, 1.0f);
        params.elevationSpread = clamp(params.elevationSpread, 0.0f, 1.0f);
        params.jitter = clamp(params.jitter, 0.0f, 1.0f);
        params.drift = clamp(params.drift, 0.0f, 1.0f);
        params.rateHz = clamp(params.rateHz, 0.001f, 2.0f);
        params.decorrelate = clamp(params.decorrelate, 0.0f, 1.0f);
        params.shape = static_cast<AmbiCloudShape>(std::clamp<uint32_t>(static_cast<uint32_t>(params.shape), 0u, 4u));
        params.force = static_cast<AmbiCloudForce>(std::clamp<uint32_t>(static_cast<uint32_t>(params.force), 0u, 4u));
        params.selectedAzimuthDeg = wrapSignedDeg(params.selectedAzimuthDeg);
        params.selectedElevationDeg = clamp(params.selectedElevationDeg, -90.0f, 90.0f);
        params.selectedDistance = clamp(params.selectedDistance, 0.05f, 8.0f);
        params.selectedGain = clamp(params.selectedGain, 0.0f, 2.0f);
        params.doppler = clamp(params.doppler, 0.0f, 1.0f);
        params.air = clamp(params.air, 0.0f, 1.0f);
        const bool selectedChanged = params.selectedCloud != params_.selectedCloud;
        params_ = params;
        depth_.setParams({ params_.doppler, params_.air });
        if (selectedChanged) {
            syncSelectedFromCloud();
        } else {
            auto& cloud = clouds_[params_.selectedCloud];
            cloud.azimuthDeg = params_.selectedAzimuthDeg;
            cloud.elevationDeg = params_.selectedElevationDeg;
            cloud.distance = params_.selectedDistance;
            cloud.gain = params_.selectedGain;
        }
    }

    AmbiCloudEncoderParams params() const { return params_; }
    const std::array<AmbiCloud, kAmbiCloudEncoderMaxClouds>& clouds() const { return clouds_; }

    Vec3 sourcePositionForDisplay(uint32_t src) const
    {
        const uint32_t cloudIndex = src % std::max<uint32_t>(1u, params_.activeClouds);
        const auto& cloud = clouds_[std::min<uint32_t>(cloudIndex, kAmbiCloudEncoderMaxClouds - 1u)];
        return sourcePosition(src, cloud, true);
    }

    static float displayDistance(float distance)
    {
        return clamp(std::pow(std::max(0.05f, distance), 0.55f), 0.20f, 3.10f);
    }

    void setClouds(std::array<AmbiCloud, kAmbiCloudEncoderMaxClouds> clouds)
    {
        for (auto& cloud : clouds) {
            cloud.azimuthDeg = wrapSignedDeg(cloud.azimuthDeg);
            cloud.elevationDeg = clamp(cloud.elevationDeg, -90.0f, 90.0f);
            cloud.distance = clamp(cloud.distance, 0.05f, 8.0f);
            cloud.gain = clamp(cloud.gain, 0.0f, 2.0f);
        }
        clouds_ = clouds;
        syncSelectedFromCloud();
    }

    template <typename Sample>
    void processBlock(const Sample* const* inputs, Sample* const* outputs, uint32_t inputChannels, uint32_t outputChannels, uint32_t frames)
    {
        if (!outputs || frames == 0u) return;
        outputChannels = std::min<uint32_t>(outputChannels, kAmbiCloudEncoderMaxChannels);
        for (uint32_t ch = 0; ch < outputChannels; ++ch) {
            if (outputs[ch]) std::fill(outputs[ch], outputs[ch] + frames, static_cast<Sample>(0));
        }
        if (!inputs) return;

        const uint32_t ambiChannels = std::min<uint32_t>(ambiChannelsForOrder(params_.order), outputChannels);
        const uint32_t inputCount = std::min<uint32_t>({ inputChannels, params_.activeInputs, kAmbiCloudEncoderMaxInputs });
        const float outGain = dbToGain(params_.outputGainDb);
        const float phaseStart = phase_;
        const float phaseInc = params_.rateHz / static_cast<float>(sampleRate_);
        constexpr uint32_t kMotionChunkFrames = 16;

        for (uint32_t chunkStart = 0; chunkStart < frames; chunkStart += kMotionChunkFrames) {
            const uint32_t chunkFrames = std::min<uint32_t>(kMotionChunkFrames, frames - chunkStart);
            const float chunkPhase = phaseStart + phaseInc * static_cast<float>(chunkStart);
            std::array<std::array<float, kAmbiCloudEncoderMaxChannels>, kAmbiCloudEncoderMaxInputs> basis {};
            std::array<float, kAmbiCloudEncoderMaxInputs> gains {};
            std::array<float, kAmbiCloudEncoderMaxInputs> distances {};
            for (uint32_t src = 0; src < inputCount; ++src) {
                const uint32_t cloudIndex = src % params_.activeClouds;
                const auto& cloud = clouds_[cloudIndex];
                const Vec3 pos = sourcePosition(src, cloud, false, chunkPhase);
                const Vec3 dir = normalize(pos);
                basis[src] = acnSn3dBasis7(dir);
                distances[src] = std::sqrt(pos.x * pos.x + pos.y * pos.y + pos.z * pos.z);
                const float distGain = 1.0f / (0.35f + 0.65f * std::max(0.05f, distances[src]));
                const float cloudNorm = 1.0f / std::sqrt(static_cast<float>(std::max<uint32_t>(1u, (inputCount + params_.activeClouds - 1u) / params_.activeClouds)));
                gains[src] = cloud.gain * distGain * outGain * cloudNorm;
            }

            for (uint32_t frame = chunkStart; frame < chunkStart + chunkFrames; ++frame) {
                for (uint32_t src = 0; src < inputCount; ++src) {
                    const Sample* in = inputs[src];
                    if (!in) continue;
                    float sample = static_cast<float>(in[frame]) * gains[src];
                    if (params_.decorrelate > 0.0001f) {
                        const float sign = ((src + frame) & 1u) ? -1.0f : 1.0f;
                        sample *= 1.0f - params_.decorrelate * 0.08f + sign * params_.decorrelate * 0.015f;
                    }
                    sample = depth_.process(src, sample, distances[src]);
                    if (sample == 0.0f) continue;
                    for (uint32_t ch = 0; ch < ambiChannels; ++ch) {
                        if (outputs[ch]) outputs[ch][frame] = static_cast<Sample>(flushDenormal(static_cast<float>(outputs[ch][frame]) + sample * basis[src][ch]));
                    }
                }
                depth_.advance();
            }
        }
        phase_ += static_cast<float>(frames) / static_cast<float>(sampleRate_) * params_.rateHz;
        if (phase_ > 100000.0f) phase_ -= 100000.0f;
    }

private:
    static float wrapSignedDeg(float value)
    {
        while (value > 180.0f) value -= 360.0f;
        while (value <= -180.0f) value += 360.0f;
        return value;
    }

    static float fract(float x)
    {
        return x - std::floor(x);
    }

    float noise(uint32_t src, float salt) const
    {
        const float x = std::sin(static_cast<float>(src) * 12.9898f + salt * 78.233f + static_cast<float>(seed_ & 0xffffu) * 0.001f) * 43758.5453f;
        return fract(x) * 2.0f - 1.0f;
    }

    Vec3 sourcePosition(uint32_t src, const AmbiCloud& cloud, bool forDisplay) const
    {
        return sourcePosition(src, cloud, forDisplay, phase_);
    }

    Vec3 sourcePosition(uint32_t src, const AmbiCloud& cloud, bool forDisplay, float phase) const
    {
        const float golden = 137.507764f;
        const float local = static_cast<float>(src / std::max<uint32_t>(1u, params_.activeClouds));
        const float forceRate = params_.force == AmbiCloudForce::Calm ? 0.15f
            : (params_.force == AmbiCloudForce::Advection ? 0.65f
                : (params_.force == AmbiCloudForce::Shear ? 0.95f
                    : (params_.force == AmbiCloudForce::Convection ? 1.25f : 1.70f)));
        const float cycle = phase * forceRate;
        const float driftPhase = (cycle + local * 0.037f) * 360.0f;
        float centerAz = cloud.azimuthDeg;
        float centerEl = cloud.elevationDeg;
        if (params_.force == AmbiCloudForce::Advection) {
            centerAz = wrapSignedDeg(centerAz + cycle * 54.0f);
            centerEl = clamp(centerEl + std::sin((cycle * 0.23f + local * 0.017f) * 2.0f * kPi) * 5.0f, -90.0f, 90.0f);
        } else if (params_.force == AmbiCloudForce::Shear) {
            centerAz = wrapSignedDeg(centerAz + std::sin((cycle * 0.31f + cloud.elevationDeg * 0.003f) * 2.0f * kPi) * 18.0f);
            centerEl = clamp(centerEl + std::sin((cycle * 0.19f + local * 0.013f) * 2.0f * kPi) * 7.0f, -90.0f, 90.0f);
        } else if (params_.force == AmbiCloudForce::Convection) {
            centerAz = wrapSignedDeg(centerAz + std::sin((cycle * 0.21f + local * 0.011f) * 2.0f * kPi) * 10.0f);
            centerEl = clamp(centerEl + std::sin((cycle * 0.37f + local * 0.019f) * 2.0f * kPi) * 16.0f, -90.0f, 90.0f);
        } else if (params_.force == AmbiCloudForce::Turbulence) {
            centerAz = wrapSignedDeg(centerAz
                + std::sin((cycle * 0.41f + local * 0.031f) * 2.0f * kPi) * 14.0f
                + std::sin((cycle * 0.17f + local * 0.071f) * 2.0f * kPi) * 9.0f);
            centerEl = clamp(centerEl
                    + std::sin((cycle * 0.29f + local * 0.023f) * 2.0f * kPi) * 10.0f
                    + std::sin((cycle * 0.53f + local * 0.041f) * 2.0f * kPi) * 5.0f,
                -90.0f, 90.0f);
        }
        float azSpread = params_.spread * 180.0f;
        float elSpread = params_.elevationSpread * 90.0f;
        const float nearSpread = clamp(1.0f / std::sqrt(std::max(0.05f, cloud.distance)), 0.38f, 2.65f);
        azSpread *= nearSpread;
        elSpread *= nearSpread;
        switch (params_.shape) {
        case AmbiCloudShape::Stratus:
            azSpread *= 1.35f;
            elSpread *= 0.28f;
            break;
        case AmbiCloudShape::Cirrus:
            azSpread *= 1.55f;
            elSpread *= 0.45f;
            break;
        case AmbiCloudShape::Lenticular:
            azSpread *= 1.05f;
            elSpread *= 0.18f;
            break;
        case AmbiCloudShape::Storm:
            azSpread *= 0.95f;
            elSpread *= 1.30f;
            break;
        case AmbiCloudShape::Cumulus:
        default:
            break;
        }
        const float azJit = noise(src, 1.7f) * params_.jitter * 45.0f;
        const float elJit = noise(src, 3.1f) * params_.jitter * 22.5f;
        float azMotion = std::sin((local * golden + driftPhase) * kPi / 180.0f);
        float elMotion = std::cos((local * 83.0f - driftPhase * 0.73f) * kPi / 180.0f);
        if (params_.shape == AmbiCloudShape::Cirrus) {
            azMotion = std::sin((local * 47.0f + driftPhase * 0.42f) * kPi / 180.0f) * 0.45f
                + std::sin((local * 9.0f + driftPhase * 0.18f) * kPi / 180.0f) * 0.55f;
            elMotion *= 0.55f;
        } else if (params_.shape == AmbiCloudShape::Lenticular) {
            azMotion = std::sin((local * golden) * kPi / 180.0f);
            elMotion = std::sin((local * golden * 2.0f + driftPhase * 0.12f) * kPi / 180.0f) * 0.35f;
        } else if (params_.shape == AmbiCloudShape::Storm) {
            azMotion = std::sin((local * golden + driftPhase * 1.6f) * kPi / 180.0f) * 0.65f
                + noise(src, 6.2f) * 0.35f;
            elMotion = std::sin((local * 131.0f + driftPhase * 1.1f) * kPi / 180.0f);
        }
        if (params_.force == AmbiCloudForce::Shear) {
            azMotion += std::sin((cloud.elevationDeg + local * 11.0f) * kPi / 180.0f) * 0.35f;
        } else if (params_.force == AmbiCloudForce::Convection) {
            elMotion += std::sin((driftPhase + local * 73.0f) * kPi / 180.0f) * 0.55f;
        } else if (params_.force == AmbiCloudForce::Turbulence) {
            azMotion += std::sin((cycle * 2.7f + local * 0.173f + noise(src, 4.7f) * 0.11f) * 2.0f * kPi) * 0.32f
                + std::sin((cycle * 1.3f + local * 0.097f + noise(src, 8.6f) * 0.17f) * 2.0f * kPi) * 0.18f;
            elMotion += std::sin((cycle * 2.1f + local * 0.137f + noise(src, 8.1f) * 0.13f) * 2.0f * kPi) * 0.30f
                + std::sin((cycle * 1.1f + local * 0.071f + noise(src, 2.8f) * 0.19f) * 2.0f * kPi) * 0.20f;
        }
        const float az = wrapSignedDeg(centerAz + azMotion * azSpread + azJit);
        const float el = clamp(centerEl + elMotion * elSpread + elJit, -90.0f, 90.0f);
        const Vec3 center = directionFromAed(centerAz, centerEl);
        const Vec3 wide = directionFromAed(az, el);
        float baseMotion = 0.0f;
        switch (params_.force) {
        case AmbiCloudForce::Advection: baseMotion = 0.22f; break;
        case AmbiCloudForce::Shear: baseMotion = 0.28f; break;
        case AmbiCloudForce::Convection: baseMotion = 0.34f; break;
        case AmbiCloudForce::Turbulence: baseMotion = 0.42f; break;
        case AmbiCloudForce::Calm:
        default: baseMotion = 0.0f; break;
        }
        const float drift = clamp(baseMotion + params_.drift * (1.0f - baseMotion), 0.0f, 1.0f);
        const Vec3 dir = normalize({
            center.x * (1.0f - drift) + wide.x * drift,
            center.y * (1.0f - drift) + wide.y * drift,
            center.z * (1.0f - drift) + wide.z * drift,
        });
        const float radius = forDisplay ? displayDistance(cloud.distance) : cloud.distance;
        return { dir.x * radius, dir.y * radius, dir.z * radius };
    }

    void syncSelectedFromCloud()
    {
        params_.selectedCloud = std::min<uint32_t>(params_.selectedCloud, std::max<uint32_t>(1u, params_.activeClouds) - 1u);
        const auto& cloud = clouds_[params_.selectedCloud];
        params_.selectedAzimuthDeg = cloud.azimuthDeg;
        params_.selectedElevationDeg = cloud.elevationDeg;
        params_.selectedDistance = cloud.distance;
        params_.selectedGain = cloud.gain;
    }

    double sampleRate_ = 48000.0;
    uint32_t seed_ = 0x45d9f3bu;
    float phase_ = 0.0f;
    AmbiCloudEncoderParams params_ {};
    std::array<AmbiCloud, kAmbiCloudEncoderMaxClouds> clouds_ {};
    AmbiEncoderDepthProcessor<kAmbiCloudEncoderMaxInputs> depth_ {};
};

} // namespace s3g
