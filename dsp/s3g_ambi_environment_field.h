#pragma once

#include "s3g_ambisonic_speaker_decoder.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace s3g {

constexpr uint32_t kAmbiEnvironmentChannels = 16u;
constexpr uint32_t kAmbiEnvironmentEarlyTaps = 8u;
constexpr uint32_t kAmbiEnvironmentLateLines = 8u;

enum class AmbiEnvironmentProfileId : uint32_t {
    Open = 0u,
    Submerged,
    Cave,
    Cistern,
    Channel,
    Pipe,
    Canopy,
    Porch,
    Room,
    Hangar,
    Canyon,
    Tunnel,
    Depth,
};

struct AmbiEnvironmentProfile {
    float decaySeconds = 0.45f;
    float diffusion = 0.72f;
    float damping = 0.55f;
    float earlyLevel = 0.16f;
    float lateLevel = 0.10f;
    float lowpassHz = 14000.0f;
    float highpassHz = 55.0f;
    float orderRetention = 0.62f;
    float returnGain = 1.0f;
    std::array<float, kAmbiEnvironmentEarlyTaps> earlyGain {};
};

inline float ambiEnvironmentSpaceAmount(float space)
{
    const float normalized = clamp(std::isfinite(space) ? space : 0.0f, 0.0f, 1.0f);
    return std::sqrt(normalized);
}

inline float ambiEnvironmentSizeScale(float size)
{
    const float normalized = clamp(std::isfinite(size) ? size : 0.5f, 0.0f, 1.0f);
    return std::exp2((normalized - 0.5f) * 1.8f);
}

inline float ambiEnvironmentDecayScale(float decay)
{
    const float normalized = clamp(std::isfinite(decay) ? decay : 0.5f, 0.0f, 1.0f);
    return std::exp2((normalized - 0.5f) * 3.5f);
}

inline AmbiEnvironmentProfile ambiEnvironmentProfile(AmbiEnvironmentProfileId id)
{
    switch (id) {
    case AmbiEnvironmentProfileId::Submerged:
        return { 1.35f, 0.94f, 0.78f, 0.07f, 0.34f, 4200.0f, 24.0f, 0.48f, 0.88f,
            { 0.18f, 0.13f, 0.09f, 0.07f, 0.05f, 0.04f, 0.02f, 0.01f } };
    case AmbiEnvironmentProfileId::Cave:
        return { 3.35f, 0.88f, 0.68f, 0.38f, 0.34f, 7200.0f, 34.0f, 0.56f, 0.86f,
            { 0.10f, 0.14f, 0.20f, 0.28f, 0.34f, 0.40f, 0.34f, 0.25f } };
    case AmbiEnvironmentProfileId::Cistern:
        return { 1.55f, 0.82f, 0.26f, 0.48f, 0.29f, 15200.0f, 48.0f, 0.68f, 0.78f,
            { 0.42f, 0.36f, 0.31f, 0.24f, 0.17f, 0.12f, 0.07f, 0.03f } };
    case AmbiEnvironmentProfileId::Channel:
        return { 1.05f, 0.58f, 0.57f, 0.38f, 0.20f, 8600.0f, 44.0f, 0.62f, 0.90f,
            { 0.18f, 0.39f, 0.36f, 0.18f, 0.24f, 0.20f, 0.08f, 0.05f } };
    case AmbiEnvironmentProfileId::Pipe:
        return { 0.92f, 0.36f, 0.43f, 0.48f, 0.21f, 10600.0f, 52.0f, 0.72f, 0.78f,
            { 0.08f, 0.46f, 0.44f, 0.10f, 0.30f, 0.27f, 0.16f, 0.12f } };
    case AmbiEnvironmentProfileId::Canopy:
        return { 0.62f, 0.90f, 0.84f, 0.30f, 0.10f, 5200.0f, 88.0f, 0.54f, 0.94f,
            { 0.36f, 0.31f, 0.29f, 0.24f, 0.18f, 0.11f, 0.05f, 0.02f } };
    case AmbiEnvironmentProfileId::Porch:
        return { 0.82f, 0.72f, 0.52f, 0.38f, 0.16f, 10800.0f, 68.0f, 0.65f, 0.92f,
            { 0.46f, 0.32f, 0.18f, 0.26f, 0.14f, 0.08f, 0.03f, 0.01f } };
    case AmbiEnvironmentProfileId::Room:
        return { 0.96f, 0.86f, 0.47f, 0.42f, 0.21f, 12200.0f, 62.0f, 0.62f, 0.86f,
            { 0.42f, 0.38f, 0.32f, 0.27f, 0.19f, 0.12f, 0.06f, 0.02f } };
    case AmbiEnvironmentProfileId::Hangar:
        return { 3.10f, 0.80f, 0.30f, 0.42f, 0.34f, 14500.0f, 48.0f, 0.68f, 0.78f,
            { 0.10f, 0.14f, 0.18f, 0.25f, 0.32f, 0.37f, 0.31f, 0.22f } };
    case AmbiEnvironmentProfileId::Canyon:
        return { 2.70f, 0.44f, 0.50f, 0.48f, 0.26f, 9200.0f, 42.0f, 0.72f, 0.82f,
            { 0.03f, 0.06f, 0.09f, 0.15f, 0.25f, 0.35f, 0.48f, 0.42f } };
    case AmbiEnvironmentProfileId::Tunnel:
        return { 1.75f, 0.38f, 0.40f, 0.52f, 0.26f, 12000.0f, 52.0f, 0.78f, 0.76f,
            { 0.04f, 0.47f, 0.45f, 0.06f, 0.35f, 0.32f, 0.22f, 0.18f } };
    case AmbiEnvironmentProfileId::Depth:
        return { 1.42f, 0.90f, 0.55f, 0.28f, 0.25f, 10800.0f, 46.0f, 0.66f, 0.86f,
            { 0.34f, 0.31f, 0.27f, 0.22f, 0.17f, 0.12f, 0.07f, 0.03f } };
    case AmbiEnvironmentProfileId::Open:
    default:
        return { 0.38f, 0.76f, 0.58f, 0.14f, 0.065f, 14200.0f, 62.0f, 0.58f, 0.96f,
            { 0.42f, 0.10f, 0.08f, 0.05f, 0.03f, 0.02f, 0.01f, 0.00f } };
    }
}

class AmbiEnvironmentField {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = std::max(1000.0, sampleRate);
        const uint32_t earlyFrames = static_cast<uint32_t>(std::ceil(sampleRate_ * 0.56)) + 4u;
        const uint32_t lateFrames = static_cast<uint32_t>(std::ceil(sampleRate_ * 0.26)) + 4u;
        for (auto& delay : earlyDelay_) delay.assign(earlyFrames, 0.0f);
        for (auto& delay : lateDelay_) delay.assign(lateFrames, 0.0f);

        constexpr std::array<float, kAmbiEnvironmentEarlyTaps> earlyMs {
            7.3f, 13.7f, 23.9f, 39.7f, 67.1f, 103.9f, 173.3f, 281.9f
        };
        constexpr std::array<float, kAmbiEnvironmentLateLines> lateMs {
            43.7f, 53.9f, 61.1f, 71.9f, 83.3f, 97.1f, 109.7f, 127.9f
        };
        for (uint32_t tap = 0u; tap < kAmbiEnvironmentEarlyTaps; ++tap)
            earlyFrames_[tap] = earlyMs[tap] * 0.001f * static_cast<float>(sampleRate_);
        for (uint32_t line = 0u; line < kAmbiEnvironmentLateLines; ++line)
            lateFrames_[line] = lateMs[line] * 0.001f * static_cast<float>(sampleRate_);

        profile_ = ambiEnvironmentProfile(profileId_);
        targetProfile_ = profile_;
        rebuildProfileTargets();
        updateBases(0.0f, 0.0f, true);
        reset();
    }

    void reset()
    {
        for (auto& delay : earlyDelay_) std::fill(delay.begin(), delay.end(), 0.0f);
        for (auto& delay : lateDelay_) std::fill(delay.begin(), delay.end(), 0.0f);
        earlyPosition_ = 0u;
        latePosition_ = 0u;
        inputBus_.fill(0.0f);
        previousInput_.fill(0.0f);
        highpassState_.fill(0.0f);
        lowpassState_.fill(0.0f);
        lateFilter_.fill(0.0f);
        currentFeedback_ = targetFeedback_;
        currentEarlyGain_ = targetProfile_.earlyGain;
        currentDiffusion_ = sizedDiffusion();
        currentDampingCoefficient_ = targetDampingCoefficient_;
        currentLowpassCoefficient_ = targetLowpassCoefficient_;
        currentHighpassPole_ = targetHighpassPole_;
        currentEarlyLevel_ = sizedEarlyLevel();
        currentLateLevel_ = sizedLateLevel();
        currentOrderRetention_ = sizedOrderRetention();
        currentReturnGain_ = targetProfile_.returnGain;
        currentAmount_ = targetAmount_;
        currentSizeScale_ = targetSizeScale_;
        safetyGain_ = 1.0f;
    }

    void setProfile(AmbiEnvironmentProfileId id)
    {
        if (id == profileId_) return;
        profileId_ = id;
        targetProfile_ = ambiEnvironmentProfile(id);
        rebuildProfileTargets();
    }

    void setAmount(float amount)
    {
        targetAmount_ = clamp(std::isfinite(amount) ? amount : 0.0f, 0.0f, 1.0f);
    }

    void setShape(float size, float decay, float damping)
    {
        const float nextSizeScale = ambiEnvironmentSizeScale(size);
        const float nextDecayScale = ambiEnvironmentDecayScale(decay);
        const float nextSize = clamp(std::isfinite(size) ? size : 0.5f, 0.0f, 1.0f);
        const float nextDamping = clamp(std::isfinite(damping) ? damping : 0.5f, 0.0f, 1.0f);
        if (std::fabs(nextSizeScale - targetSizeScale_) < 1.0e-5f
            && std::fabs(nextDecayScale - targetDecayScale_) < 1.0e-5f
            && std::fabs(nextDamping - targetDamping_) < 1.0e-5f) return;
        targetSizeScale_ = nextSizeScale;
        targetDecayScale_ = nextDecayScale;
        targetSize_ = nextSize;
        targetDamping_ = nextDamping;
        rebuildProfileTargets();
    }

    void setMotion(float azimuthDeg, float elevationDeg)
    {
        updateBases(azimuthDeg, elevationDeg, false);
    }

    void beginFrame()
    {
        inputBus_.fill(0.0f);
    }

    void addSource(float sample, Vec3 direction, float send = 1.0f)
    {
        if (!std::isfinite(sample) || !std::isfinite(send)) return;
        const float value = sample * clamp(send, 0.0f, 2.0f);
        if (std::fabs(value) < 1.0e-12f) return;
        direction = normalize(direction);
        inputBus_[0] += value * 0.62f;
        inputBus_[1] += value * direction.x * 0.48f;
        inputBus_[2] += value * direction.y * 0.48f;
        inputBus_[3] += value * direction.z * 0.48f;
    }

    void addHoaFrame(float w, float y, float z, float x, float send = 1.0f)
    {
        if (!std::isfinite(send)) return;
        const float gain = clamp(send, 0.0f, 2.0f);
        const auto clean = [](float value) { return std::isfinite(value) ? value : 0.0f; };
        inputBus_[0] += clean(w) * gain * 0.62f;
        inputBus_[1] += clean(x) * gain * 0.48f;
        inputBus_[2] += clean(y) * gain * 0.48f;
        inputBus_[3] += clean(z) * gain * 0.48f;
    }

    std::array<float, kAmbiEnvironmentChannels> process()
    {
        std::array<float, kAmbiEnvironmentChannels> output {};
        if (earlyDelay_[0].empty() || lateDelay_[0].empty()) return output;

        smoothTargets();
        const float order1 = std::max(0.05f, currentOrderRetention_);
        const float order2 = order1 * order1;
        const float order3 = order2 * order1;
        std::array<float, kAmbiEnvironmentChannels> orderScale {};
        orderScale[0] = 0.78f;
        for (uint32_t channel = 1u; channel < 4u; ++channel) orderScale[channel] = order1;
        for (uint32_t channel = 4u; channel < 9u; ++channel) orderScale[channel] = order2;
        for (uint32_t channel = 9u; channel < kAmbiEnvironmentChannels; ++channel) orderScale[channel] = order3;
        std::array<float, 4> filteredInput {};
        for (uint32_t lane = 0u; lane < filteredInput.size(); ++lane) {
            const float input = std::isfinite(inputBus_[lane]) ? inputBus_[lane] : 0.0f;
            const float highpassed = flushDenormal(input - previousInput_[lane]
                + currentHighpassPole_ * highpassState_[lane]);
            previousInput_[lane] = input;
            highpassState_[lane] = highpassed;
            lowpassState_[lane] += (highpassed - lowpassState_[lane]) * currentLowpassCoefficient_;
            filteredInput[lane] = flushDenormal(lowpassState_[lane]);
            earlyDelay_[lane][earlyPosition_] = filteredInput[lane];
        }

        for (uint32_t tap = 0u; tap < kAmbiEnvironmentEarlyTaps; ++tap) {
            const float gain = currentEarlyGain_[tap] * currentEarlyLevel_;
            if (gain <= 1.0e-7f) continue;
            std::array<float, 4> delayed {};
            for (uint32_t lane = 0u; lane < delayed.size(); ++lane)
                delayed[lane] = readDelay(earlyDelay_[lane], earlyPosition_, earlyFrames_[tap] * currentSizeScale_);
            const auto& direction = earlyDirections_[tap];
            const float value = (delayed[0] * 0.72f
                + (delayed[1] * direction.x + delayed[2] * direction.y + delayed[3] * direction.z) * 0.42f)
                * gain;
            for (uint32_t channel = 0u; channel < kAmbiEnvironmentChannels; ++channel)
                output[channel] += value * earlyBasis_[tap][channel] * orderScale[channel];
        }

        std::array<float, kAmbiEnvironmentLateLines> delayed {};
        for (uint32_t line = 0u; line < kAmbiEnvironmentLateLines; ++line) {
            delayed[line] = readDelay(lateDelay_[line], latePosition_, lateFrames_[line] * currentSizeScale_);
            lateFilter_[line] += (delayed[line] - lateFilter_[line]) * currentDampingCoefficient_;
            delayed[line] = flushDenormal(lateFilter_[line]);
        }

        std::array<float, kAmbiEnvironmentLateLines> mixed {};
        hadamard8(delayed, mixed);
        constexpr std::array<float, kAmbiEnvironmentLateLines> injectionSigns {
            1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f, -1.0f, -1.0f
        };
        for (uint32_t line = 0u; line < kAmbiEnvironmentLateLines; ++line) {
            const auto& direction = lateDirections_[line];
            const float excitation = (filteredInput[0] * 0.48f
                + (filteredInput[1] * direction.x + filteredInput[2] * direction.y
                    + filteredInput[3] * direction.z) * 0.36f)
                * injectionSigns[line] * 0.19f;
            const float feedbackSignal = lerp(delayed[line], mixed[line], currentDiffusion_)
                * currentFeedback_[line];
            lateDelay_[line][latePosition_] = flushDenormal(std::clamp(excitation + feedbackSignal, -2.5f, 2.5f));
            const float value = delayed[line] * currentLateLevel_ * 0.42f;
            for (uint32_t channel = 0u; channel < kAmbiEnvironmentChannels; ++channel)
                output[channel] += value * lateBasis_[line][channel] * orderScale[channel];
        }

        earlyPosition_ = (earlyPosition_ + 1u) % static_cast<uint32_t>(earlyDelay_[0].size());
        latePosition_ = (latePosition_ + 1u) % static_cast<uint32_t>(lateDelay_[0].size());

        float peak = 0.0f;
        const float returnScale = currentAmount_ * currentReturnGain_ * 3.15f;
        for (auto& value : output) {
            value *= returnScale;
            if (!std::isfinite(value)) value = 0.0f;
            peak = std::max(peak, std::fabs(value));
        }
        const float targetSafety = peak > 0.72f ? 0.72f / peak : 1.0f;
        if (targetSafety < safetyGain_) safetyGain_ = targetSafety;
        else safetyGain_ += (targetSafety - safetyGain_) * safetyRelease_;
        for (auto& value : output) value = flushDenormal(value * safetyGain_);
        return output;
    }

private:
    float sizedDiffusion() const
    {
        return clamp(targetProfile_.diffusion + (targetSize_ - 0.5f) * 0.26f, 0.05f, 0.98f);
    }

    float sizedEarlyLevel() const
    {
        return targetProfile_.earlyLevel * std::exp2((0.5f - targetSize_) * 1.10f);
    }

    float sizedLateLevel() const
    {
        return targetProfile_.lateLevel * std::exp2((targetSize_ - 0.5f) * 1.25f);
    }

    float sizedOrderRetention() const
    {
        return clamp(targetProfile_.orderRetention * std::exp2((0.5f - targetSize_) * 0.55f), 0.08f, 0.96f);
    }

    static void hadamard8(const std::array<float, 8>& input, std::array<float, 8>& output)
    {
        const float a0 = input[0] + input[1];
        const float a1 = input[0] - input[1];
        const float a2 = input[2] + input[3];
        const float a3 = input[2] - input[3];
        const float a4 = input[4] + input[5];
        const float a5 = input[4] - input[5];
        const float a6 = input[6] + input[7];
        const float a7 = input[6] - input[7];
        const float b0 = a0 + a2;
        const float b1 = a1 + a3;
        const float b2 = a0 - a2;
        const float b3 = a1 - a3;
        const float b4 = a4 + a6;
        const float b5 = a5 + a7;
        const float b6 = a4 - a6;
        const float b7 = a5 - a7;
        constexpr float scale = 0.35355339059f;
        output = { (b0 + b4) * scale, (b1 + b5) * scale, (b2 + b6) * scale,
            (b3 + b7) * scale, (b0 - b4) * scale, (b1 - b5) * scale,
            (b2 - b6) * scale, (b3 - b7) * scale };
    }

    static float readDelay(const std::vector<float>& delay, uint32_t writePosition, float delayFrames)
    {
        const float size = static_cast<float>(delay.size());
        float readPosition = static_cast<float>(writePosition) - delayFrames;
        while (readPosition < 0.0f) readPosition += size;
        const uint32_t first = static_cast<uint32_t>(readPosition) % static_cast<uint32_t>(delay.size());
        const uint32_t second = (first + 1u) % static_cast<uint32_t>(delay.size());
        const float fraction = readPosition - std::floor(readPosition);
        return lerp(delay[first], delay[second], fraction);
    }

    void rebuildProfileTargets()
    {
        for (uint32_t line = 0u; line < kAmbiEnvironmentLateLines; ++line) {
            const float seconds = lateFrames_[line] * targetSizeScale_ / static_cast<float>(sampleRate_);
            targetFeedback_[line] = clamp(std::pow(0.001f,
                seconds / std::max(0.08f, targetProfile_.decaySeconds * targetDecayScale_)), 0.0f, 0.9972f);
        }
        const float effectiveDamping = clamp(targetProfile_.damping + (targetDamping_ - 0.5f) * 0.86f,
            0.02f, 0.98f);
        const float dampingCutoff = 620.0f + 18380.0f * std::pow(1.0f - effectiveDamping, 2.1f);
        const float toneScale = std::exp2((0.5f - targetDamping_) * 2.0f);
        targetDampingCoefficient_ = onePoleCoefficient(dampingCutoff);
        targetLowpassCoefficient_ = onePoleCoefficient(targetProfile_.lowpassHz * toneScale);
        targetHighpassPole_ = std::exp(-2.0f * kPi * targetProfile_.highpassHz / static_cast<float>(sampleRate_));
        profileAlpha_ = 1.0f - std::exp(-1.0f / static_cast<float>(sampleRate_ * 0.220));
        amountAlpha_ = 1.0f - std::exp(-1.0f / static_cast<float>(sampleRate_ * 0.075));
        safetyRelease_ = 1.0f - std::exp(-1.0f / static_cast<float>(sampleRate_ * 0.180));
    }

    void smoothTargets()
    {
        currentAmount_ += (targetAmount_ - currentAmount_) * amountAlpha_;
        currentSizeScale_ += (targetSizeScale_ - currentSizeScale_) * profileAlpha_;
        currentDiffusion_ += (sizedDiffusion() - currentDiffusion_) * profileAlpha_;
        currentDampingCoefficient_ += (targetDampingCoefficient_ - currentDampingCoefficient_) * profileAlpha_;
        currentLowpassCoefficient_ += (targetLowpassCoefficient_ - currentLowpassCoefficient_) * profileAlpha_;
        currentHighpassPole_ += (targetHighpassPole_ - currentHighpassPole_) * profileAlpha_;
        currentEarlyLevel_ += (sizedEarlyLevel() - currentEarlyLevel_) * profileAlpha_;
        currentLateLevel_ += (sizedLateLevel() - currentLateLevel_) * profileAlpha_;
        currentOrderRetention_ += (sizedOrderRetention() - currentOrderRetention_) * profileAlpha_;
        currentReturnGain_ += (targetProfile_.returnGain - currentReturnGain_) * profileAlpha_;
        for (uint32_t tap = 0u; tap < kAmbiEnvironmentEarlyTaps; ++tap)
            currentEarlyGain_[tap] += (targetProfile_.earlyGain[tap] - currentEarlyGain_[tap]) * profileAlpha_;
        for (uint32_t line = 0u; line < kAmbiEnvironmentLateLines; ++line)
            currentFeedback_[line] += (targetFeedback_[line] - currentFeedback_[line]) * profileAlpha_;
    }

    void updateBases(float azimuthDeg, float elevationDeg, bool force)
    {
        if (!std::isfinite(azimuthDeg)) azimuthDeg = 0.0f;
        if (!std::isfinite(elevationDeg)) elevationDeg = 0.0f;
        if (!force && std::fabs(azimuthDeg - basisAzimuthDeg_) < 0.05f
            && std::fabs(elevationDeg - basisElevationDeg_) < 0.05f) return;
        basisAzimuthDeg_ = azimuthDeg;
        basisElevationDeg_ = elevationDeg;
        constexpr std::array<std::array<float, 2>, kAmbiEnvironmentEarlyTaps> earlyAed {{
            { 0.0f, -38.0f }, { 88.0f, 2.0f }, { -92.0f, 4.0f }, { 178.0f, 19.0f },
            { 43.0f, 34.0f }, { -47.0f, -28.0f }, { 133.0f, 14.0f }, { -137.0f, -18.0f }
        }};
        constexpr std::array<std::array<float, 2>, kAmbiEnvironmentLateLines> lateAed {{
            { 45.0f, 35.264f }, { -45.0f, 35.264f }, { 135.0f, 35.264f }, { -135.0f, 35.264f },
            { 45.0f, -35.264f }, { -45.0f, -35.264f }, { 135.0f, -35.264f }, { -135.0f, -35.264f }
        }};
        for (uint32_t tap = 0u; tap < kAmbiEnvironmentEarlyTaps; ++tap) {
            earlyDirections_[tap] = directionFromAed(earlyAed[tap][0] + azimuthDeg,
                clamp(earlyAed[tap][1] + elevationDeg * 0.24f, -88.0f, 88.0f));
            earlyBasis_[tap] = acnSn3dBasis7(earlyDirections_[tap]);
        }
        for (uint32_t line = 0u; line < kAmbiEnvironmentLateLines; ++line) {
            lateDirections_[line] = directionFromAed(lateAed[line][0] + azimuthDeg,
                clamp(lateAed[line][1] + elevationDeg * 0.18f, -88.0f, 88.0f));
            lateBasis_[line] = acnSn3dBasis7(lateDirections_[line]);
        }
    }

    float onePoleCoefficient(float cutoffHz) const
    {
        const float hz = clamp(cutoffHz, 20.0f, static_cast<float>(sampleRate_ * 0.45));
        return clamp(1.0f - std::exp(-2.0f * kPi * hz / static_cast<float>(sampleRate_)), 0.0001f, 1.0f);
    }

    double sampleRate_ = 48000.0;
    AmbiEnvironmentProfileId profileId_ = AmbiEnvironmentProfileId::Open;
    AmbiEnvironmentProfile profile_ {};
    AmbiEnvironmentProfile targetProfile_ {};
    std::array<std::vector<float>, 4> earlyDelay_;
    std::array<std::vector<float>, kAmbiEnvironmentLateLines> lateDelay_;
    std::array<float, kAmbiEnvironmentEarlyTaps> earlyFrames_ {};
    std::array<float, kAmbiEnvironmentLateLines> lateFrames_ {};
    std::array<float, 4> inputBus_ {};
    std::array<float, 4> previousInput_ {};
    std::array<float, 4> highpassState_ {};
    std::array<float, 4> lowpassState_ {};
    std::array<float, kAmbiEnvironmentLateLines> lateFilter_ {};
    std::array<float, kAmbiEnvironmentEarlyTaps> currentEarlyGain_ {};
    std::array<float, kAmbiEnvironmentLateLines> currentFeedback_ {};
    std::array<float, kAmbiEnvironmentLateLines> targetFeedback_ {};
    std::array<Vec3, kAmbiEnvironmentEarlyTaps> earlyDirections_ {};
    std::array<Vec3, kAmbiEnvironmentLateLines> lateDirections_ {};
    std::array<std::array<float, kAmbiSpeakerDecoderMaxChannels>, kAmbiEnvironmentEarlyTaps> earlyBasis_ {};
    std::array<std::array<float, kAmbiSpeakerDecoderMaxChannels>, kAmbiEnvironmentLateLines> lateBasis_ {};
    uint32_t earlyPosition_ = 0u;
    uint32_t latePosition_ = 0u;
    float basisAzimuthDeg_ = 0.0f;
    float basisElevationDeg_ = 0.0f;
    float targetAmount_ = 0.0f;
    float currentAmount_ = 0.0f;
    float targetSizeScale_ = 1.0f;
    float currentSizeScale_ = 1.0f;
    float targetSize_ = 0.5f;
    float targetDecayScale_ = 1.0f;
    float targetDamping_ = 0.5f;
    float targetDampingCoefficient_ = 0.4f;
    float currentDampingCoefficient_ = 0.4f;
    float targetLowpassCoefficient_ = 0.8f;
    float currentLowpassCoefficient_ = 0.8f;
    float targetHighpassPole_ = 0.99f;
    float currentHighpassPole_ = 0.99f;
    float currentDiffusion_ = 0.72f;
    float currentEarlyLevel_ = 0.16f;
    float currentLateLevel_ = 0.10f;
    float currentOrderRetention_ = 0.62f;
    float currentReturnGain_ = 1.0f;
    float profileAlpha_ = 0.0001f;
    float amountAlpha_ = 0.0002f;
    float safetyGain_ = 1.0f;
    float safetyRelease_ = 0.0001f;
};

} // namespace s3g
