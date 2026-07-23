#pragma once

#include "s3g_ambisonic_stereo_decoder.h"
#include "s3g_math.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

constexpr uint32_t kAmbiHeadMaxOrder = 7;
constexpr uint32_t kAmbiHeadMaxChannels = 64;
constexpr uint32_t kAmbiHeadMaxVirtualSpeakers = 32;
constexpr uint32_t kAmbiHeadDelaySize = 512;

enum class AmbiHeadProfile : uint32_t {
    SyntheticMedium = 0,
    SyntheticSmall = 1,
    SyntheticLarge = 2,
};

enum class AmbiHeadDecodeMode : uint32_t {
    Direct = 0,
    VirtualField = 1,
};

enum class AmbiHeadMode : uint32_t {
    Binaural = 0,
    Transaural = 1,
};

enum class AmbiHeadXtcMode : uint32_t {
    Feedforward = 0,
    MatrixInverse = 1,
};

struct AmbiHeadParams {
    uint32_t order = 3;
    AmbiStereoVirtualLayout layout = AmbiStereoVirtualLayout::Dome24;
    AmbiHeadDecodeMode decodeMode = AmbiHeadDecodeMode::Direct;
    AmbiStereoWeighting weighting = AmbiStereoWeighting::EnergyNormalized;
    AmbiStereoAutogain autogain = AmbiStereoAutogain::PowerSqrtN;
    AmbiHeadProfile head = AmbiHeadProfile::SyntheticMedium;
    AmbiHeadMode mode = AmbiHeadMode::Binaural;
    float yawDeg = 0.0f;
    float pitchDeg = 0.0f;
    float widthPercent = 100.0f;
    float pinnaPercent = 55.0f;
    float roomPercent = 0.0f;
    float xtcAmountPercent = 80.0f;
    AmbiHeadXtcMode xtcMode = AmbiHeadXtcMode::MatrixInverse;
    float speakerHalfAngleDeg = 30.0f;
    float headWidthCm = 18.0f;
    float xtcDelayTrimMs = 0.0f;
    float xtcHfRolloffHz = 6500.0f;
    float xtcLowProtectHz = 120.0f;
    float stereoPreservePercent = 0.0f;
    float outputGainDb = 0.0f;
};

struct AmbiHeadProfileSpec {
    float widthCm = 18.0f;
    float shadow = 0.55f;
    float pinna = 0.55f;
};

inline AmbiHeadProfileSpec ambiHeadProfileSpec(AmbiHeadProfile profile)
{
    switch (profile) {
    case AmbiHeadProfile::SyntheticSmall: return { 15.5f, 0.42f, 0.42f };
    case AmbiHeadProfile::SyntheticLarge: return { 21.0f, 0.68f, 0.62f };
    case AmbiHeadProfile::SyntheticMedium:
    default: return { 18.0f, 0.54f, 0.55f };
    }
}

inline AmbiStereoVirtualPoint ambiHeadDirectPoint(uint32_t index)
{
    constexpr uint32_t count = kAmbiHeadMaxVirtualSpeakers;
    constexpr float golden = 2.3999632297f;
    const float t = (static_cast<float>(index) + 0.5f) / static_cast<float>(count);
    const float z = 1.0f - 2.0f * t;
    const float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
    const float a = golden * static_cast<float>(index);
    const float x = std::cos(a) * r;
    const float y = std::sin(a) * r;
    AmbiStereoVirtualPoint p;
    p.azimuthDeg = std::atan2(y, x) * 180.0f / kPi;
    p.elevationDeg = std::asin(z) * 180.0f / kPi;
    return p;
}

inline float ambiHeadOnePoleCoef(float hz, double sampleRate)
{
    return std::exp(-2.0f * kPi * std::max(1.0f, hz) / static_cast<float>(std::max(1.0, sampleRate)));
}

inline float ambiHeadBasisValue(uint32_t ch, float azDeg, float elDeg)
{
    const float az = azDeg * kPi / 180.0f;
    const float el = elDeg * kPi / 180.0f;
    const float ca = std::cos(az);
    const float sa = std::sin(az);
    const float c2a = std::cos(2.0f * az);
    const float s2a = std::sin(2.0f * az);
    const float c3a = std::cos(3.0f * az);
    const float s3a = std::sin(3.0f * az);
    const float ce = std::cos(el);
    const float se = std::sin(el);
    const float ce2 = ce * ce;
    const float ce3 = ce2 * ce;
    const float se2 = se * se;
    switch (ch) {
    case 0: return 1.0f;
    case 1: return sa * ce;
    case 2: return se;
    case 3: return ca * ce;
    case 4: return 0.8660254038f * s2a * ce2;
    case 5: return 0.8660254038f * sa * std::sin(2.0f * el);
    case 6: return 0.5f * (3.0f * se2 - 1.0f);
    case 7: return 0.8660254038f * ca * std::sin(2.0f * el);
    case 8: return 0.8660254038f * c2a * ce2;
    case 9: return 0.7905694150f * s3a * ce3;
    case 10: return 1.9364916731f * s2a * se * ce2;
    case 11: return 0.6123724357f * sa * ce * (5.0f * se2 - 1.0f);
    case 12: return 0.5f * se * (5.0f * se2 - 3.0f);
    case 13: return 0.6123724357f * ca * ce * (5.0f * se2 - 1.0f);
    case 14: return 1.9364916731f * c2a * se * ce2;
    case 15: return 0.7905694150f * c3a * ce3;
    default: break;
    }

    const uint32_t band = ambiStereoOrderForChannel(ch);
    const float foldedAz = static_cast<float>((ch + band * 3u) % 8u) * kPi * 0.25f;
    return std::pow(std::max(0.0f, ce), static_cast<float>(std::min<uint32_t>(band, 7u)))
        * std::cos(static_cast<float>(band) * (az - foldedAz));
}

class AmbiHeadDecoder {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        reset();
        updateParams(params_);
    }

    void reset()
    {
        for (auto& b : speakerDelay_) b.fill(0.0f);
        leftDelay_.fill(0.0f);
        rightDelay_.fill(0.0f);
        leftLp_.fill(0.0f);
        rightLp_.fill(0.0f);
        xtcLbuf_.fill(0.0f);
        xtcRbuf_.fill(0.0f);
        xtcHfL_ = xtcHfR_ = xtcLowL_ = xtcLowR_ = 0.0f;
        delayPos_ = 0;
        xtcPos_ = 0;
    }

    void updateParams(AmbiHeadParams params)
    {
        params.order = std::clamp<uint32_t>(params.order, 1u, kAmbiHeadMaxOrder);
        params.layout = static_cast<AmbiStereoVirtualLayout>(std::min<uint32_t>(static_cast<uint32_t>(params.layout), 4u));
        params.decodeMode = static_cast<AmbiHeadDecodeMode>(std::min<uint32_t>(static_cast<uint32_t>(params.decodeMode), 1u));
        params.weighting = static_cast<AmbiStereoWeighting>(std::min<uint32_t>(static_cast<uint32_t>(params.weighting), 3u));
        params.autogain = static_cast<AmbiStereoAutogain>(std::min<uint32_t>(static_cast<uint32_t>(params.autogain), 2u));
        params.head = static_cast<AmbiHeadProfile>(std::min<uint32_t>(static_cast<uint32_t>(params.head), 2u));
        params.mode = static_cast<AmbiHeadMode>(std::min<uint32_t>(static_cast<uint32_t>(params.mode), 1u));
        params.yawDeg = clamp(params.yawDeg, -180.0f, 180.0f);
        params.pitchDeg = clamp(params.pitchDeg, -90.0f, 90.0f);
        params.widthPercent = clamp(params.widthPercent, 0.0f, 200.0f);
        params.pinnaPercent = clamp(params.pinnaPercent, 0.0f, 100.0f);
        params.roomPercent = clamp(params.roomPercent, 0.0f, 100.0f);
        params.xtcAmountPercent = clamp(params.xtcAmountPercent, 0.0f, 140.0f);
        params.xtcMode = static_cast<AmbiHeadXtcMode>(std::min<uint32_t>(static_cast<uint32_t>(params.xtcMode), 1u));
        params.speakerHalfAngleDeg = clamp(params.speakerHalfAngleDeg, 10.0f, 60.0f);
        params.headWidthCm = clamp(params.headWidthCm, 12.0f, 24.0f);
        params.xtcDelayTrimMs = clamp(params.xtcDelayTrimMs, -0.5f, 0.5f);
        params.xtcHfRolloffHz = clamp(params.xtcHfRolloffHz, 1000.0f, 16000.0f);
        params.xtcLowProtectHz = clamp(params.xtcLowProtectHz, 20.0f, 500.0f);
        params.stereoPreservePercent = clamp(params.stereoPreservePercent, 0.0f, 100.0f);
        params.outputGainDb = clamp(params.outputGainDb, -24.0f, 12.0f);
        params_ = params;
        rebuildTables();
    }

    const AmbiHeadParams& params() const { return params_; }

    void processFrame(const float* input, float& left, float& right)
    {
        const uint32_t chCount = std::min<uint32_t>(kAmbiHeadMaxChannels, ambiChannelsForOrder(params_.order));
        const uint32_t vcount = virtualPointCount();
        float earL = 0.0f;
        float earR = 0.0f;
        for (uint32_t spk = 0; spk < vcount; ++spk) {
            float v = 0.0f;
            for (uint32_t ch = 0; ch < chCount; ++ch) {
                v += input[ch] * coeffs_[spk][ch];
            }
            auto& delay = speakerDelay_[spk];
            delay[delayPos_] = v;
            const float dl = readDelay(delay, leftDelay_[spk]);
            const float dr = readDelay(delay, rightDelay_[spk]);
            float l = dl * leftGain_[spk];
            float r = dr * rightGain_[spk];
            leftLp_[spk] = leftLp_[spk] * leftLpCoef_[spk] + l * (1.0f - leftLpCoef_[spk]);
            rightLp_[spk] = rightLp_[spk] * rightLpCoef_[spk] + r * (1.0f - rightLpCoef_[spk]);
            l = l * (1.0f - leftShadow_[spk]) + leftLp_[spk] * leftShadow_[spk];
            r = r * (1.0f - rightShadow_[spk]) + rightLp_[spk] * rightShadow_[spk];
            earL += l;
            earR += r;
        }
        delayPos_ = (delayPos_ + 1u) & (kAmbiHeadDelaySize - 1u);

        const float room = params_.roomPercent / 100.0f;
        if (room > 0.0f) {
            const float mid = (earL + earR) * 0.5f;
            earL = earL * (1.0f - room * 0.35f) + mid * room * 0.35f;
            earR = earR * (1.0f - room * 0.35f) + mid * room * 0.35f;
        }

        if (params_.mode == AmbiHeadMode::Transaural) {
            applyTransaural(earL, earR);
        }

        const float out = dbToGain(params_.outputGainDb);
        left = earL * out;
        right = earR * out;
    }

private:
    float readDelay(const std::array<float, kAmbiHeadDelaySize>& delay, float samples) const
    {
        const float clamped = clamp(samples, 0.0f, static_cast<float>(kAmbiHeadDelaySize - 3u));
        const uint32_t i0 = static_cast<uint32_t>(std::floor(clamped));
        const float frac = clamped - static_cast<float>(i0);
        const uint32_t a = (delayPos_ + kAmbiHeadDelaySize - i0) & (kAmbiHeadDelaySize - 1u);
        const uint32_t b = (a + kAmbiHeadDelaySize - 1u) & (kAmbiHeadDelaySize - 1u);
        return delay[a] * (1.0f - frac) + delay[b] * frac;
    }

    void rebuildTables()
    {
        for (auto& row : coeffs_) row.fill(0.0f);
        leftGain_.fill(0.0f);
        rightGain_.fill(0.0f);
        leftDelay_.fill(0.0f);
        rightDelay_.fill(0.0f);
        leftShadow_.fill(0.0f);
        rightShadow_.fill(0.0f);
        leftLpCoef_.fill(0.0f);
        rightLpCoef_.fill(0.0f);

        const auto spec = ambiHeadProfileSpec(params_.head);
        const uint32_t chCount = std::min<uint32_t>(kAmbiHeadMaxChannels, ambiChannelsForOrder(params_.order));
        const uint32_t vcount = virtualPointCount();
        const float width = params_.widthPercent / 100.0f;
        const float pinna = clamp(params_.pinnaPercent / 100.0f, 0.0f, 1.0f) * spec.pinna;
        const float headM = params_.headWidthCm * 0.01f;

        for (uint32_t spk = 0; spk < vcount; ++spk) {
            auto pt = virtualPoint(spk);
            pt.azimuthDeg = ambiStereoWrapSignedDeg(pt.azimuthDeg - params_.yawDeg);
            pt.elevationDeg = clamp(pt.elevationDeg - params_.pitchDeg, -90.0f, 90.0f);

            float sumsq = 0.0f;
            std::array<float, kAmbiHeadMaxChannels> basis {};
            for (uint32_t ch = 0; ch < chCount; ++ch) {
                const float weighted = ambiHeadBasisValue(ch, pt.azimuthDeg, pt.elevationDeg)
                    * ambiStereoOrderWeight(ch, params_.order, asStereoParams());
                basis[ch] = weighted;
                sumsq += weighted * weighted;
            }
            const float norm = params_.weighting == AmbiStereoWeighting::EnergyNormalized
                ? 1.0f / std::sqrt(std::max(0.000000001f, sumsq))
                : 1.0f;
            const float autoGain = params_.autogain == AmbiStereoAutogain::PowerSqrtN ? 1.0f / std::sqrt(static_cast<float>(chCount))
                : params_.autogain == AmbiStereoAutogain::EnergySum ? 1.0f / static_cast<float>(std::max(1u, chCount))
                : 1.0f;
            const float scale = norm * autoGain / std::sqrt(static_cast<float>(vcount));
            for (uint32_t ch = 0; ch < chCount; ++ch) coeffs_[spk][ch] = basis[ch] * scale;

            const float az = pt.azimuthDeg * kPi / 180.0f;
            const float side = std::sin(az);
            const float front = std::max(0.0f, std::cos(az));
            const float elev = std::abs(pt.elevationDeg) / 90.0f;
            const float itd = headM * side / 343.0f * static_cast<float>(sampleRate_);
            leftDelay_[spk] = std::max(0.0f, -itd);
            rightDelay_[spk] = std::max(0.0f, itd);

            const float lateral = std::abs(side);
            const float nearBoost = 0.18f * lateral * width;
            const float farCut = spec.shadow * lateral * (0.25f + 0.30f * width);
            leftGain_[spk] = clamp(1.0f + nearBoost * side - farCut * std::max(0.0f, -side), 0.05f, 1.4f);
            rightGain_[spk] = clamp(1.0f - nearBoost * side - farCut * std::max(0.0f, side), 0.05f, 1.4f);

            const float cutoff = 2600.0f + 7600.0f * front + 1800.0f * (1.0f - elev) + 1600.0f * (1.0f - pinna);
            leftLpCoef_[spk] = ambiHeadOnePoleCoef(cutoff * (side < 0.0f ? 0.55f : 1.0f), sampleRate_);
            rightLpCoef_[spk] = ambiHeadOnePoleCoef(cutoff * (side > 0.0f ? 0.55f : 1.0f), sampleRate_);
            leftShadow_[spk] = clamp((side < 0.0f ? spec.shadow * lateral : 0.05f * lateral) + pinna * elev * 0.20f, 0.0f, 0.85f);
            rightShadow_[spk] = clamp((side > 0.0f ? spec.shadow * lateral : 0.05f * lateral) + pinna * elev * 0.20f, 0.0f, 0.85f);
        }

        xtcDelaySamples_ = clamp(params_.headWidthCm * 0.01f * std::sin(params_.speakerHalfAngleDeg * kPi / 180.0f) / 343.0f
                * static_cast<float>(sampleRate_) + params_.xtcDelayTrimMs * 0.001f * static_cast<float>(sampleRate_),
            0.0f, static_cast<float>(kAmbiHeadDelaySize - 3u));
        xtcHfCoef_ = ambiHeadOnePoleCoef(params_.xtcHfRolloffHz, sampleRate_);
        xtcLowCoef_ = ambiHeadOnePoleCoef(params_.xtcLowProtectHz, sampleRate_);
    }

    AmbiStereoParams asStereoParams() const
    {
        AmbiStereoParams p;
        p.order = params_.order;
        p.weighting = params_.weighting;
        return p;
    }

    uint32_t virtualPointCount() const
    {
        return params_.decodeMode == AmbiHeadDecodeMode::Direct
            ? kAmbiHeadMaxVirtualSpeakers
            : ambiStereoVirtualCount(params_.layout);
    }

    AmbiStereoVirtualPoint virtualPoint(uint32_t index) const
    {
        return params_.decodeMode == AmbiHeadDecodeMode::Direct
            ? ambiHeadDirectPoint(index)
            : ambiStereoVirtualPoint(params_.layout, index);
    }

    void applyTransaural(float& l, float& r)
    {
        xtcLbuf_[xtcPos_] = l;
        xtcRbuf_[xtcPos_] = r;
        const uint32_t i0 = static_cast<uint32_t>(std::floor(xtcDelaySamples_));
        const float frac = xtcDelaySamples_ - static_cast<float>(i0);
        const uint32_t a = (xtcPos_ + kAmbiHeadDelaySize - i0) & (kAmbiHeadDelaySize - 1u);
        const uint32_t b = (a + kAmbiHeadDelaySize - 1u) & (kAmbiHeadDelaySize - 1u);
        const float dl = xtcLbuf_[a] * (1.0f - frac) + xtcLbuf_[b] * frac;
        const float dr = xtcRbuf_[a] * (1.0f - frac) + xtcRbuf_[b] * frac;
        xtcPos_ = (xtcPos_ + 1u) & (kAmbiHeadDelaySize - 1u);

        xtcHfL_ = xtcHfL_ * xtcHfCoef_ + dl * (1.0f - xtcHfCoef_);
        xtcHfR_ = xtcHfR_ * xtcHfCoef_ + dr * (1.0f - xtcHfCoef_);
        xtcLowL_ = xtcLowL_ * xtcLowCoef_ + xtcHfL_ * (1.0f - xtcLowCoef_);
        xtcLowR_ = xtcLowR_ * xtcLowCoef_ + xtcHfR_ * (1.0f - xtcLowCoef_);

        const float cancelL = xtcHfL_ - xtcLowL_;
        const float cancelR = xtcHfR_ - xtcLowR_;
        const float amount = params_.xtcAmountPercent / 100.0f;
        const float preserve = params_.stereoPreservePercent / 100.0f;
        const float inL = l;
        const float inR = r;
        l = inL - amount * cancelR;
        r = inR - amount * cancelL;
        if (params_.xtcMode == AmbiHeadXtcMode::MatrixInverse) {
            const float inv = 1.0f / std::max(0.50f, 1.0f - amount * amount * 0.25f);
            l *= inv;
            r *= inv;
        }
        l = l * (1.0f - preserve) + inL * preserve;
        r = r * (1.0f - preserve) + inR * preserve;
        const float peak = std::max(std::abs(l), std::abs(r));
        if (peak > 0.98f) {
            const float g = 0.98f / std::max(0.000000001f, peak);
            l *= g;
            r *= g;
        }
    }

    double sampleRate_ = 48000.0;
    AmbiHeadParams params_ {};
    uint32_t delayPos_ = 0;
    uint32_t xtcPos_ = 0;
    std::array<std::array<float, kAmbiHeadMaxChannels>, kAmbiHeadMaxVirtualSpeakers> coeffs_ {};
    std::array<std::array<float, kAmbiHeadDelaySize>, kAmbiHeadMaxVirtualSpeakers> speakerDelay_ {};
    std::array<float, kAmbiHeadMaxVirtualSpeakers> leftGain_ {};
    std::array<float, kAmbiHeadMaxVirtualSpeakers> rightGain_ {};
    std::array<float, kAmbiHeadMaxVirtualSpeakers> leftDelay_ {};
    std::array<float, kAmbiHeadMaxVirtualSpeakers> rightDelay_ {};
    std::array<float, kAmbiHeadMaxVirtualSpeakers> leftShadow_ {};
    std::array<float, kAmbiHeadMaxVirtualSpeakers> rightShadow_ {};
    std::array<float, kAmbiHeadMaxVirtualSpeakers> leftLpCoef_ {};
    std::array<float, kAmbiHeadMaxVirtualSpeakers> rightLpCoef_ {};
    std::array<float, kAmbiHeadMaxVirtualSpeakers> leftLp_ {};
    std::array<float, kAmbiHeadMaxVirtualSpeakers> rightLp_ {};
    std::array<float, kAmbiHeadDelaySize> xtcLbuf_ {};
    std::array<float, kAmbiHeadDelaySize> xtcRbuf_ {};
    float xtcDelaySamples_ = 0.0f;
    float xtcHfCoef_ = 0.0f;
    float xtcLowCoef_ = 0.0f;
    float xtcHfL_ = 0.0f;
    float xtcHfR_ = 0.0f;
    float xtcLowL_ = 0.0f;
    float xtcLowR_ = 0.0f;
};

} // namespace s3g
