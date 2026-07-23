#pragma once

#include "s3g_ambi_ray_encoder.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <type_traits>
#include <vector>

namespace s3g {

enum class AmbiRayBilocationMapMode : uint32_t {
    Linked = 0u,
    MirrorX = 1u,
    MirrorY = 2u,
    Counter = 3u,
};

struct AmbiRayBilocationParams {
    uint32_t order = 3u;
    float sourceX = 0.5f;
    float sourceY = 0.25f;
    float sourceZ = 0.5f;
    float listenerX = 0.5f;
    float listenerY = 0.5f;
    float listenerZ = 0.5f;
    float place = 0.5f;
    float permeability = 0.65f;
    float memorySeconds = 2.0f;
    float separationDeg = 90.0f;
    AmbiRayBilocationMapMode mapMode = AmbiRayBilocationMapMode::Linked;
    float direct = 1.0f;
    float early = 0.72f;
    float late = 0.42f;
    float sizeA = 0.90f;
    float sizeB = 1.20f;
    float scatterA = 0.30f;
    float scatterB = 0.70f;
    float widthA = 0.90f;
    float widthB = 1.15f;
    float airA = 0.12f;
    float airB = 0.48f;
    float movementMs = 60.0f;
    float doppler = 0.50f;
    float outputGainDb = -9.0f;
    bool bypassRoom = false;
    AmbiFieldListenMode fieldListenMode = AmbiFieldListenMode::Off;
};

inline AmbiRayBilocationParams sanitizeAmbiRayBilocationParams(AmbiRayBilocationParams params)
{
    params.order = std::clamp<uint32_t>(params.order, 1u, kAmbiRayMaxOrder);
    params.sourceX = clamp(params.sourceX, 0.0f, 1.0f);
    params.sourceY = clamp(params.sourceY, 0.0f, 1.0f);
    params.sourceZ = clamp(params.sourceZ, 0.0f, 1.0f);
    params.listenerX = clamp(params.listenerX, 0.0f, 1.0f);
    params.listenerY = clamp(params.listenerY, 0.0f, 1.0f);
    params.listenerZ = clamp(params.listenerZ, 0.0f, 1.0f);
    params.place = clamp(params.place, 0.0f, 1.0f);
    params.permeability = clamp(params.permeability, 0.0f, 1.0f);
    params.memorySeconds = clamp(params.memorySeconds, 0.0f, 12.0f);
    params.separationDeg = clamp(params.separationDeg, 0.0f, 180.0f);
    params.mapMode = static_cast<AmbiRayBilocationMapMode>(
        std::min<uint32_t>(static_cast<uint32_t>(params.mapMode), 3u));
    params.direct = clamp(params.direct, 0.0f, 1.5f);
    params.early = clamp(params.early, 0.0f, 1.5f);
    params.late = clamp(params.late, 0.0f, 1.5f);
    params.sizeA = clamp(params.sizeA, 0.5f, 2.0f);
    params.sizeB = clamp(params.sizeB, 0.5f, 2.0f);
    params.scatterA = clamp(params.scatterA, 0.0f, 1.0f);
    params.scatterB = clamp(params.scatterB, 0.0f, 1.0f);
    params.widthA = clamp(params.widthA, 0.0f, 1.5f);
    params.widthB = clamp(params.widthB, 0.0f, 1.5f);
    params.airA = clamp(params.airA, 0.0f, 1.0f);
    params.airB = clamp(params.airB, 0.0f, 1.0f);
    params.movementMs = clamp(params.movementMs, 10.0f, 500.0f);
    params.doppler = clamp(params.doppler, 0.0f, 2.0f);
    params.outputGainDb = clamp(params.outputGainDb, -60.0f, 12.0f);
    params.fieldListenMode = sanitizeAmbiFieldListenMode(params.fieldListenMode);
    return params;
}

struct AmbiRayBilocationGains {
    float a = 0.0f;
    float b = 0.0f;
};

inline AmbiRayBilocationGains ambiRayBilocationGains(float place, float permeability)
{
    place = clamp(place, 0.0f, 1.0f);
    permeability = clamp(permeability, 0.0f, 1.0f);
    const float halfWidth = lerp(0.01f, 0.50f, permeability);
    const float start = 0.5f - halfWidth;
    const float amount = clamp((place - start) / std::max(0.0001f, 2.0f * halfWidth), 0.0f, 1.0f);
    const float smooth = amount * amount * (3.0f - 2.0f * amount);
    return { std::sqrt(std::max(0.0f, 1.0f - smooth)), std::sqrt(std::max(0.0f, smooth)) };
}

inline Vec3 mapAmbiRayBilocationSource(Vec3 source, AmbiRayBilocationMapMode mode)
{
    switch (mode) {
    case AmbiRayBilocationMapMode::MirrorX: source.x = 1.0f - source.x; break;
    case AmbiRayBilocationMapMode::MirrorY: source.y = 1.0f - source.y; break;
    case AmbiRayBilocationMapMode::Counter:
        source.x = 1.0f - source.x;
        source.y = 1.0f - source.y;
        source.z = 1.0f - source.z;
        break;
    case AmbiRayBilocationMapMode::Linked: break;
    }
    return source;
}

class AmbiRayBilocationEncoder {
public:
    bool prepare(double sampleRate,
                 uint32_t maximumFrames,
                 AmbiRayDescriptor descriptorA,
                 AmbiRayDescriptor descriptorB)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        maximumFrames_ = std::max<uint32_t>(1u, maximumFrames);
        descriptorA_ = sanitizeAmbiRayDescriptor(std::move(descriptorA));
        descriptorB_ = sanitizeAmbiRayDescriptor(std::move(descriptorB));
        if (!encoderA_.prepare(sampleRate_, descriptorA_)
            || !encoderB_.prepare(sampleRate_, descriptorB_)) return false;
        floatScratch_.prepare(maximumFrames_);
        doubleScratch_.prepare(maximumFrames_);
        params_ = sanitizeAmbiRayBilocationParams(params_);
        applyEncoderParams();
        reset();
        return true;
    }

    void setParams(AmbiRayBilocationParams params)
    {
        params_ = sanitizeAmbiRayBilocationParams(params);
    }

    const AmbiRayBilocationParams& params() const { return params_; }
    const AmbiRayDescriptor& descriptorA() const { return descriptorA_; }
    const AmbiRayDescriptor& descriptorB() const { return descriptorB_; }
    float fieldListenActivityA() const { return encoderA_.fieldListenActivity(); }
    float fieldListenActivityB() const { return encoderB_.fieldListenActivity(); }
    float fieldListenWeightA(uint32_t lobe) const
    {
        return encoderA_.fieldListenWeight(lobe);
    }
    float fieldListenWeightB(uint32_t lobe) const
    {
        return encoderB_.fieldListenWeight(lobe);
    }

    void reset()
    {
        applyEncoderParams();
        encoderA_.reset();
        encoderB_.reset();
        currentPlace_ = params_.place;
        currentPermeability_ = params_.permeability;
        currentOutputGain_ = dbToGain(params_.outputGainDb);
        const auto gains = ambiRayBilocationGains(currentPlace_, currentPermeability_);
        returnGateA_ = gains.a > kActiveThreshold ? 1.0f : 0.0f;
        returnGateB_ = gains.b > kActiveThreshold ? 1.0f : 0.0f;
        safetyGain_ = 1.0f;
    }

    uint32_t tailFrames() const
    {
        const uint32_t roomTail = std::max(encoderA_.tailFrames(), encoderB_.tailFrames());
        const uint32_t memoryTail = static_cast<uint32_t>(std::ceil(params_.memorySeconds * sampleRate_));
        return roomTail + memoryTail;
    }

    template <typename Sample>
    void process(const Sample* input, Sample** outputs, uint32_t outputChannels, uint32_t frames)
    {
        if (!outputs || frames == 0u) return;
        outputChannels = std::min<uint32_t>(outputChannels, kAmbiRayMaxChannels);
        auto& scratch = scratchFor<Sample>();
        uint32_t offset = 0u;
        while (offset < frames) {
            const uint32_t chunk = std::min<uint32_t>(maximumFrames_, frames - offset);
            processChunk(input ? input + offset : nullptr, outputs, outputChannels, offset, chunk, scratch);
            offset += chunk;
        }
    }

private:
    template <typename Sample>
    struct Scratch {
        std::vector<Sample> inputA;
        std::vector<Sample> inputB;
        std::vector<float> gainA;
        std::vector<float> gainB;
        std::array<std::vector<Sample>, kAmbiRayMaxChannels> branchA;
        std::array<std::vector<Sample>, kAmbiRayMaxChannels> branchB;
        std::array<Sample*, kAmbiRayMaxChannels> pointersA {};
        std::array<Sample*, kAmbiRayMaxChannels> pointersB {};

        void prepare(uint32_t frames)
        {
            inputA.assign(frames, Sample {});
            inputB.assign(frames, Sample {});
            gainA.assign(frames, 0.0f);
            gainB.assign(frames, 0.0f);
            for (uint32_t channel = 0u; channel < kAmbiRayMaxChannels; ++channel) {
                branchA[channel].assign(frames, Sample {});
                branchB[channel].assign(frames, Sample {});
                pointersA[channel] = branchA[channel].data();
                pointersB[channel] = branchB[channel].data();
            }
        }
    };

    template <typename Sample>
    Scratch<Sample>& scratchFor()
    {
        if constexpr (std::is_same_v<Sample, double>) return doubleScratch_;
        else return floatScratch_;
    }

    void applyEncoderParams()
    {
        const Vec3 sourceA { params_.sourceX, params_.sourceY, params_.sourceZ };
        const Vec3 sourceB = mapAmbiRayBilocationSource(sourceA, params_.mapMode);
        auto makeParams = [&](bool branchB) {
            AmbiRayEncoderParams result;
            result.order = params_.order;
            result.sourceX = branchB ? sourceB.x : sourceA.x;
            result.sourceY = branchB ? sourceB.y : sourceA.y;
            result.sourceZ = branchB ? sourceB.z : sourceA.z;
            result.listenerX = params_.listenerX;
            result.listenerY = params_.listenerY;
            result.listenerZ = params_.listenerZ;
            result.direct = params_.direct;
            result.early = params_.early;
            result.late = params_.late;
            result.size = branchB ? params_.sizeB : params_.sizeA;
            result.scatter = branchB ? params_.scatterB : params_.scatterA;
            result.width = branchB ? params_.widthB : params_.widthA;
            result.air = branchB ? params_.airB : params_.airA;
            result.movementMs = params_.movementMs;
            result.doppler = params_.doppler;
            result.outputGainDb = 0.0f;
            result.bypassRoom = params_.bypassRoom;
            result.fieldListenMode = params_.fieldListenMode;
            return result;
        };
        encoderA_.setParams(makeParams(false));
        encoderB_.setParams(makeParams(true));
        encoderA_.setOutputYawDegrees(-params_.separationDeg * 0.5f);
        encoderB_.setOutputYawDegrees(params_.separationDeg * 0.5f);
    }

    template <typename Sample>
    void processChunk(const Sample* input,
                      Sample** outputs,
                      uint32_t outputChannels,
                      uint32_t outputOffset,
                      uint32_t frames,
                      Scratch<Sample>& scratch)
    {
        applyEncoderParams();
        const float controlAlpha = 1.0f - std::exp(-1.0f / static_cast<float>(sampleRate_ * 0.025));
        for (uint32_t frame = 0u; frame < frames; ++frame) {
            currentPlace_ += (params_.place - currentPlace_) * controlAlpha;
            currentPermeability_ += (params_.permeability - currentPermeability_) * controlAlpha;
            const auto gains = ambiRayBilocationGains(currentPlace_, currentPermeability_);
            float value = input ? static_cast<float>(input[frame]) : 0.0f;
            if (!std::isfinite(value)) value = 0.0f;
            scratch.gainA[frame] = gains.a;
            scratch.gainB[frame] = gains.b;
            scratch.inputA[frame] = static_cast<Sample>(value * gains.a);
            scratch.inputB[frame] = static_cast<Sample>(value * gains.b);
        }
        encoderA_.process(scratch.inputA.data(), scratch.pointersA.data(), outputChannels, frames);
        encoderB_.process(scratch.inputB.data(), scratch.pointersB.data(), outputChannels, frames);

        const float attack = 1.0f - std::exp(-1.0f / static_cast<float>(sampleRate_ * 0.010));
        const float releaseSeconds = std::max(0.005f, params_.memorySeconds);
        const float release = params_.memorySeconds <= 0.0f
            ? 1.0f
            : 1.0f - std::exp(-1.0f / static_cast<float>(sampleRate_ * releaseSeconds));
        const float safetyRelease = 1.0f - std::exp(-1.0f / static_cast<float>(sampleRate_ * 0.120));
        for (uint32_t frame = 0u; frame < frames; ++frame) {
            currentOutputGain_ += (dbToGain(params_.outputGainDb) - currentOutputGain_) * controlAlpha;
            const float targetGateA = scratch.gainA[frame] > kActiveThreshold ? 1.0f : 0.0f;
            const float targetGateB = scratch.gainB[frame] > kActiveThreshold ? 1.0f : 0.0f;
            returnGateA_ += (targetGateA - returnGateA_) * (targetGateA > returnGateA_ ? attack : release);
            returnGateB_ += (targetGateB - returnGateB_) * (targetGateB > returnGateB_ ? attack : release);

            for (uint32_t channel = 0u; channel < outputChannels; ++channel) {
                frame_[channel] = static_cast<float>(scratch.pointersA[channel][frame]) * returnGateA_
                    + static_cast<float>(scratch.pointersB[channel][frame]) * returnGateB_;
            }
            float peak = 0.0f;
            for (uint32_t channel = 0u; channel < outputChannels; ++channel) {
                frame_[channel] *= currentOutputGain_;
                if (!std::isfinite(frame_[channel])) frame_[channel] = 0.0f;
                peak = std::max(peak, std::abs(frame_[channel]));
            }
            const float safetyTarget = peak > 0.98f ? 0.98f / peak : 1.0f;
            if (safetyTarget < safetyGain_) safetyGain_ = safetyTarget;
            else safetyGain_ += (safetyTarget - safetyGain_) * safetyRelease;
            for (uint32_t channel = 0u; channel < outputChannels; ++channel) {
                if (outputs[channel]) outputs[channel][outputOffset + frame] = static_cast<Sample>(
                    std::clamp(frame_[channel] * safetyGain_, -8.0f, 8.0f));
            }
        }
    }

    static constexpr float kActiveThreshold = 0.0005f;
    double sampleRate_ = 48000.0;
    uint32_t maximumFrames_ = 1u;
    AmbiRayDescriptor descriptorA_ {};
    AmbiRayDescriptor descriptorB_ {};
    AmbiRayBilocationParams params_ {};
    AmbiRayEncoder encoderA_ {};
    AmbiRayEncoder encoderB_ {};
    Scratch<float> floatScratch_ {};
    Scratch<double> doubleScratch_ {};
    std::array<float, kAmbiRayMaxChannels> frame_ {};
    float currentPlace_ = 0.5f;
    float currentPermeability_ = 0.65f;
    float currentOutputGain_ = 0.35f;
    float returnGateA_ = 1.0f;
    float returnGateB_ = 1.0f;
    float safetyGain_ = 1.0f;
};

} // namespace s3g
