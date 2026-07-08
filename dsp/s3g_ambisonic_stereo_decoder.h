#pragma once

#include "s3g_ambisonic_speaker_decoder.h"
#include "s3g_math.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

constexpr uint32_t kAmbiStereoDecoderMaxOrder = 7;
constexpr uint32_t kAmbiStereoDecoderMaxChannels = 64;
constexpr uint32_t kAmbiStereoDecoderMaxVirtualSpeakers = 32;

enum class AmbiStereoVirtualLayout : uint32_t {
    Quad = 0,
    Cube8 = 1,
    Dodeca12 = 2,
    Dome24 = 3,
    Sphere32 = 4,
};

enum class AmbiStereoMethod : uint32_t {
    XyCardioid = 0,
    OrtfStyle = 1,
    MsCardioid = 2,
    Blumlein = 3,
    SpacedOmni = 4,
    DualShotgun = 5,
    WideCardioid = 6,
    SupercardioidXy = 7,
    HypercardioidXy = 8,
    HeightFocus = 9,
};

enum class AmbiStereoWeighting : uint32_t {
    Projection = 0,
    EnergyNormalized = 1,
    MaxRe = 2,
    Custom = 3,
};

enum class AmbiStereoAutogain : uint32_t {
    Off = 0,
    PowerSqrtN = 1,
    EnergySum = 2,
};

enum class AmbiStereoHeightMode : uint32_t {
    FoldCenter = 0,
    FoldWide = 1,
    Attenuate = 2,
    Diffuse = 3,
};

enum class AmbiStereoRearMode : uint32_t {
    Quieter = 0,
    Narrower = 1,
    WrapWide = 2,
};

struct AmbiStereoParams {
    uint32_t order = 3;
    AmbiStereoVirtualLayout layout = AmbiStereoVirtualLayout::Dome24;
    AmbiStereoMethod method = AmbiStereoMethod::XyCardioid;
    float stereoWidthPercent = 110.0f;
    float micAngleDeg = 90.0f;
    float rotationDeg = 0.0f;
    float directivityPercent = 70.0f;
    float rearRejectPercent = 35.0f;
    float heightFoldPercent = 30.0f;
    float diffuseBlendPercent = 0.0f;
    AmbiStereoWeighting weighting = AmbiStereoWeighting::EnergyNormalized;
    AmbiStereoAutogain autogain = AmbiStereoAutogain::PowerSqrtN;
    float outputGainDb = 0.0f;
    float customWPercent = 100.0f;
    float customO1Percent = 100.0f;
    float customO2Percent = 82.0f;
    float customO3Percent = 64.0f;
    float abSpacingCm = 40.0f;
    float bassMonoHz = 0.0f;
    AmbiStereoHeightMode heightMode = AmbiStereoHeightMode::Attenuate;
    float frontRearBalancePercent = 0.0f;
    AmbiStereoRearMode rearMode = AmbiStereoRearMode::Narrower;
    float micElevationDeg = 0.0f;
    float rotationImagePercent = 100.0f;
};

struct AmbiStereoVirtualPoint {
    float azimuthDeg = 0.0f;
    float elevationDeg = 0.0f;
};

inline float ambiStereoWrapSignedDeg(float value)
{
    while (value > 180.0f) value -= 360.0f;
    while (value < -180.0f) value += 360.0f;
    return value;
}

inline uint32_t ambiStereoVirtualCount(AmbiStereoVirtualLayout layout)
{
    switch (layout) {
    case AmbiStereoVirtualLayout::Quad: return 4;
    case AmbiStereoVirtualLayout::Cube8: return 8;
    case AmbiStereoVirtualLayout::Dodeca12: return 12;
    case AmbiStereoVirtualLayout::Dome24: return 24;
    case AmbiStereoVirtualLayout::Sphere32:
    default: return 32;
    }
}

inline AmbiStereoVirtualPoint ambiStereoRingPoint(uint32_t index, uint32_t count, float startAzimuthDeg, float elevationDeg)
{
    return { ambiStereoWrapSignedDeg(startAzimuthDeg - static_cast<float>(index) * 360.0f / static_cast<float>(std::max(1u, count))), elevationDeg };
}

inline AmbiStereoVirtualPoint ambiStereoVirtualPoint(AmbiStereoVirtualLayout layout, uint32_t index)
{
    switch (layout) {
    case AmbiStereoVirtualLayout::Quad:
        return ambiStereoRingPoint(index, 4, -45.0f, 0.0f);
    case AmbiStereoVirtualLayout::Cube8: {
        const uint32_t slot = index % 8u;
        const float az[] = { -45.0f, -135.0f, 135.0f, 45.0f, -45.0f, -135.0f, 135.0f, 45.0f };
        return { az[slot], slot < 4u ? -35.2644f : 35.2644f };
    }
    case AmbiStereoVirtualLayout::Dodeca12: {
        static constexpr AmbiStereoVirtualPoint kPts[] = {
            { -31.717474f, 0.0f }, { -90.0f, -31.717474f }, { -90.0f, 31.717474f },
            { -148.282526f, 0.0f }, { 180.0f, -58.282526f }, { 180.0f, 58.282526f },
            { 148.282526f, 0.0f }, { 90.0f, 31.717474f }, { 90.0f, -31.717474f },
            { 31.717474f, 0.0f }, { 0.0f, 58.282526f }, { 0.0f, -58.282526f },
        };
        return kPts[index % 12u];
    }
    case AmbiStereoVirtualLayout::Dome24:
        if (index < 12u) return ambiStereoRingPoint(index, 12, -30.0f, 0.0f);
        if (index < 20u) return ambiStereoRingPoint(index - 12u, 8, -45.0f, 32.0f);
        return ambiStereoRingPoint(index - 20u, 4, -90.0f, 66.6f);
    case AmbiStereoVirtualLayout::Sphere32:
    default: {
        const float frac = (static_cast<float>(index) + 0.5f) / 32.0f;
        const float z = 1.0f - 2.0f * frac;
        return { ambiStereoWrapSignedDeg(-30.0f - static_cast<float>(index) * 137.507764f),
            std::asin(clamp(z, -1.0f, 1.0f)) * 180.0f / kPi };
    }
    }
}

inline uint32_t ambiStereoOrderForChannel(uint32_t ch)
{
    uint32_t order = 0;
    while ((order + 1u) * (order + 1u) <= ch) {
        ++order;
    }
    return order;
}

inline float ambiStereoMaxReWeight(uint32_t band, uint32_t maxOrder)
{
    if (band == 0u || maxOrder <= 1u) return 1.0f;
    const float theta = kPi / static_cast<float>(2u * maxOrder + 2u);
    return std::max(0.0f, std::cos(static_cast<float>(band) * theta));
}

inline float ambiStereoOrderWeight(uint32_t ch, uint32_t order, const AmbiStereoParams& params)
{
    const uint32_t band = ambiStereoOrderForChannel(ch);
    if (band > order) return 0.0f;
    switch (params.weighting) {
    case AmbiStereoWeighting::MaxRe:
        return ambiStereoMaxReWeight(band, order);
    case AmbiStereoWeighting::Custom:
        if (band == 0u) return clamp(params.customWPercent / 100.0f, 0.0f, 1.0f);
        if (band == 1u) return clamp(params.customO1Percent / 100.0f, 0.0f, 1.0f);
        if (band == 2u) return clamp(params.customO2Percent / 100.0f, 0.0f, 1.0f);
        return clamp(params.customO3Percent / 100.0f, 0.0f, 1.0f);
    case AmbiStereoWeighting::Projection:
    case AmbiStereoWeighting::EnergyNormalized:
    default:
        return 1.0f;
    }
}

inline float ambiStereoMicResponse(float srcAzDeg, float srcElDeg, float micAzDeg, float micElDeg, float shape)
{
    const float delta = (srcAzDeg - micAzDeg) * kPi / 180.0f;
    const float srcEl = srcElDeg * kPi / 180.0f;
    const float micEl = micElDeg * kPi / 180.0f;
    const float dot = std::sin(srcEl) * std::sin(micEl) + std::cos(srcEl) * std::cos(micEl) * std::cos(delta);
    return std::max(0.0f, (1.0f - shape) + shape * (0.5f + 0.5f * dot));
}

inline float ambiStereoDotResponse(float srcAzDeg, float srcElDeg, float micAzDeg, float micElDeg)
{
    const float delta = (srcAzDeg - micAzDeg) * kPi / 180.0f;
    const float srcEl = srcElDeg * kPi / 180.0f;
    const float micEl = micElDeg * kPi / 180.0f;
    return std::sin(srcEl) * std::sin(micEl) + std::cos(srcEl) * std::cos(micEl) * std::cos(delta);
}

inline void ambiStereoPickup(const AmbiStereoParams& params, float azDeg, float elDeg, float& left, float& right)
{
    const float width = std::max(0.0f, params.stereoWidthPercent / 100.0f);
    const float angle = params.micAngleDeg * 0.5f;
    const float direct = clamp(params.directivityPercent / 100.0f, 0.0f, 1.0f);
    const float rearReject = clamp(params.rearRejectPercent / 100.0f, 0.0f, 1.0f);
    const float heightFold = clamp(params.heightFoldPercent / 100.0f, 0.0f, 1.0f);
    const float diffuseBlend = clamp(params.diffuseBlendPercent / 100.0f, 0.0f, 1.0f);
    const float micEl = clamp(params.micElevationDeg, -90.0f, 90.0f);
    const float rotationImage = std::max(0.0f, params.rotationImagePercent / 100.0f);
    const float frontRearBalance = clamp(params.frontRearBalancePercent / 100.0f, -1.0f, 1.0f);
    azDeg = ambiStereoWrapSignedDeg(azDeg - params.rotationDeg * rotationImage);
    const float azr = azDeg * kPi / 180.0f;
    const float rear = (1.0f - std::cos(azr)) * 0.5f;
    const float height = std::abs(elDeg) / 90.0f;
    const float verticalMatch = std::cos((elDeg - micEl) * kPi / 180.0f);
    const float base = std::max(0.05f, 1.0f - rearReject * rear);
    const float diffuse = diffuseBlend * 0.7071067812f * base;
    const auto method = params.method;

    if (method == AmbiStereoMethod::XyCardioid) {
        const float l = ambiStereoMicResponse(azDeg, elDeg, angle, micEl, direct);
        const float r = ambiStereoMicResponse(azDeg, elDeg, -angle, micEl, direct);
        left = l * base * (1.0f - diffuseBlend) + diffuse;
        right = r * base * (1.0f - diffuseBlend) + diffuse;
    } else if (method == AmbiStereoMethod::OrtfStyle) {
        const float l = ambiStereoMicResponse(azDeg, elDeg, angle + 10.0f, micEl, std::min(1.0f, direct + 0.18f));
        const float r = ambiStereoMicResponse(azDeg, elDeg, -angle - 10.0f, micEl, std::min(1.0f, direct + 0.18f));
        const float pan = clamp(-std::sin(azr) * 0.35f, -0.35f, 0.35f);
        left = l * base * (1.0f - pan) * (1.0f - diffuseBlend) + diffuse;
        right = r * base * (1.0f + pan) * (1.0f - diffuseBlend) + diffuse;
    } else if (method == AmbiStereoMethod::MsCardioid) {
        const float mid = ambiStereoMicResponse(azDeg, elDeg, 0.0f, micEl, direct);
        const float side = -std::sin(azr) * std::max(0.0f, verticalMatch);
        left = (mid + side) * base * (1.0f - diffuseBlend) + diffuse;
        right = (mid - side) * base * (1.0f - diffuseBlend) + diffuse;
    } else if (method == AmbiStereoMethod::Blumlein) {
        const float el = elDeg * kPi / 180.0f;
        const float mic = micEl * kPi / 180.0f;
        const float l = std::sin(el) * std::sin(mic) + std::cos(el) * std::cos(mic) * std::cos((azDeg - 45.0f) * kPi / 180.0f);
        const float r = std::sin(el) * std::sin(mic) + std::cos(el) * std::cos(mic) * std::cos((azDeg + 45.0f) * kPi / 180.0f);
        left = l * base * (1.0f - diffuseBlend) + diffuse;
        right = r * base * (1.0f - diffuseBlend) + diffuse;
    } else if (method == AmbiStereoMethod::DualShotgun) {
        const float shotgunShape = std::min(1.0f, direct + 0.28f);
        const float tightness = 2.25f + direct * 1.75f;
        const float l = std::pow(ambiStereoMicResponse(azDeg, elDeg, angle, micEl, shotgunShape), tightness);
        const float r = std::pow(ambiStereoMicResponse(azDeg, elDeg, -angle, micEl, shotgunShape), tightness);
        left = l * base * (1.0f - diffuseBlend) + diffuse;
        right = r * base * (1.0f - diffuseBlend) + diffuse;
    } else if (method == AmbiStereoMethod::WideCardioid) {
        const float l = ambiStereoMicResponse(azDeg, elDeg, angle, micEl, direct * 0.42f);
        const float r = ambiStereoMicResponse(azDeg, elDeg, -angle, micEl, direct * 0.42f);
        left = l * base * (1.0f - diffuseBlend) + diffuse;
        right = r * base * (1.0f - diffuseBlend) + diffuse;
    } else if (method == AmbiStereoMethod::SupercardioidXy) {
        const float l = 0.37f + 0.63f * ambiStereoDotResponse(azDeg, elDeg, angle, micEl);
        const float r = 0.37f + 0.63f * ambiStereoDotResponse(azDeg, elDeg, -angle, micEl);
        left = l * base * (1.0f - diffuseBlend) + diffuse;
        right = r * base * (1.0f - diffuseBlend) + diffuse;
    } else if (method == AmbiStereoMethod::HypercardioidXy) {
        const float l = 0.25f + 0.75f * ambiStereoDotResponse(azDeg, elDeg, angle, micEl);
        const float r = 0.25f + 0.75f * ambiStereoDotResponse(azDeg, elDeg, -angle, micEl);
        left = l * base * (1.0f - diffuseBlend) + diffuse;
        right = r * base * (1.0f - diffuseBlend) + diffuse;
    } else if (method == AmbiStereoMethod::HeightFocus) {
        const float vertical = std::pow(std::max(0.0f, 0.5f + 0.5f * std::cos((elDeg - micEl) * kPi / 180.0f)), 2.4f);
        const float l = ambiStereoMicResponse(azDeg, elDeg, angle, micEl, direct) * vertical;
        const float r = ambiStereoMicResponse(azDeg, elDeg, -angle, micEl, direct) * vertical;
        left = l * base * (1.0f - diffuseBlend) + diffuse;
        right = r * base * (1.0f - diffuseBlend) + diffuse;
    } else {
        const float pan = clamp(-std::sin(azr), -1.0f, 1.0f);
        const float theta = (pan + 1.0f) * kPi / 4.0f;
        left = std::cos(theta) * base * (1.0f - diffuseBlend) + diffuse;
        right = std::sin(theta) * base * (1.0f - diffuseBlend) + diffuse;
    }

    float mid = (left + right) * 0.5f;
    float side = (left - right) * 0.5f * width;
    left = mid + side;
    right = mid - side;

    const float rearAmt = rear * std::abs(frontRearBalance);
    const float frontAmt = (1.0f - rear) * std::max(0.0f, -frontRearBalance);
    if (frontRearBalance > 0.0f) {
        if (params.rearMode == AmbiStereoRearMode::Quieter) {
            const float rearGain = 1.0f - rearAmt * 0.85f;
            left *= rearGain;
            right *= rearGain;
        } else if (params.rearMode == AmbiStereoRearMode::Narrower) {
            mid = (left + right) * 0.5f;
            side = (left - right) * 0.5f * (1.0f - rearAmt);
            left = mid + side;
            right = mid - side;
        } else {
            mid = (left + right) * 0.5f;
            side = (left - right) * 0.5f * (1.0f + rearAmt * 0.85f);
            left = mid + side;
            right = mid - side;
        }
    } else if (frontRearBalance < 0.0f) {
        left *= 1.0f - frontAmt * 0.65f;
        right *= 1.0f - frontAmt * 0.65f;
    }

    const float heightAmt = clamp(heightFold * height, 0.0f, 1.0f);
    if (params.heightMode == AmbiStereoHeightMode::FoldCenter) {
        mid = (left + right) * 0.5f;
        left = left * (1.0f - heightAmt) + mid * heightAmt;
        right = right * (1.0f - heightAmt) + mid * heightAmt;
    } else if (params.heightMode == AmbiStereoHeightMode::FoldWide) {
        mid = (left + right) * 0.5f;
        side = (left - right) * 0.5f * (1.0f + heightAmt);
        left = mid + side;
        right = mid - side;
    } else if (params.heightMode == AmbiStereoHeightMode::Attenuate) {
        left *= 1.0f - heightAmt;
        right *= 1.0f - heightAmt;
    } else {
        mid = (left + right) * 0.5f;
        left = left * (1.0f - heightAmt) + (mid + diffuse) * 0.5f * heightAmt;
        right = right * (1.0f - heightAmt) + (mid + diffuse) * 0.5f * heightAmt;
    }
}

class AmbiStereoDecoder {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        reset();
        setParams(params_);
    }

    void reset()
    {
        abLeft_.fill(0.0f);
        abRight_.fill(0.0f);
        abPos_ = 0;
        bassLeft_ = 0.0f;
        bassRight_ = 0.0f;
    }

    void setParams(AmbiStereoParams params)
    {
        params.order = std::clamp<uint32_t>(params.order, 1u, kAmbiStereoDecoderMaxOrder);
        params.layout = static_cast<AmbiStereoVirtualLayout>(std::min<uint32_t>(static_cast<uint32_t>(params.layout), 4u));
        params.method = static_cast<AmbiStereoMethod>(std::min<uint32_t>(static_cast<uint32_t>(params.method), 9u));
        params.stereoWidthPercent = clamp(params.stereoWidthPercent, 0.0f, 200.0f);
        params.micAngleDeg = clamp(params.micAngleDeg, 20.0f, 140.0f);
        params.rotationDeg = clamp(params.rotationDeg, -180.0f, 180.0f);
        params.directivityPercent = clamp(params.directivityPercent, 0.0f, 100.0f);
        params.rearRejectPercent = clamp(params.rearRejectPercent, 0.0f, 100.0f);
        params.heightFoldPercent = clamp(params.heightFoldPercent, 0.0f, 100.0f);
        params.diffuseBlendPercent = clamp(params.diffuseBlendPercent, 0.0f, 100.0f);
        params.weighting = static_cast<AmbiStereoWeighting>(std::min<uint32_t>(static_cast<uint32_t>(params.weighting), 3u));
        params.autogain = static_cast<AmbiStereoAutogain>(std::min<uint32_t>(static_cast<uint32_t>(params.autogain), 2u));
        params.outputGainDb = clamp(params.outputGainDb, -24.0f, 24.0f);
        params.abSpacingCm = clamp(params.abSpacingCm, 0.0f, 120.0f);
        params.bassMonoHz = clamp(params.bassMonoHz, 0.0f, 300.0f);
        params.heightMode = static_cast<AmbiStereoHeightMode>(std::min<uint32_t>(static_cast<uint32_t>(params.heightMode), 3u));
        params.frontRearBalancePercent = clamp(params.frontRearBalancePercent, -100.0f, 100.0f);
        params.rearMode = static_cast<AmbiStereoRearMode>(std::min<uint32_t>(static_cast<uint32_t>(params.rearMode), 2u));
        params.micElevationDeg = clamp(params.micElevationDeg, -90.0f, 90.0f);
        params.rotationImagePercent = clamp(params.rotationImagePercent, 0.0f, 200.0f);
        params_ = params;
        rebuildCoefficients();
    }

    AmbiStereoParams params() const { return params_; }
    const std::array<float, kAmbiStereoDecoderMaxChannels>& leftCoeffs() const { return leftCoeffs_; }
    const std::array<float, kAmbiStereoDecoderMaxChannels>& rightCoeffs() const { return rightCoeffs_; }

    void processFrame(const float* input, float& left, float& right)
    {
        const uint32_t channels = ambiChannelsForOrder(params_.order);
        left = 0.0f;
        right = 0.0f;
        for (uint32_t ch = 0; ch < channels; ++ch) {
            const float x = input ? input[ch] : 0.0f;
            left += x * leftCoeffs_[ch];
            right += x * rightCoeffs_[ch];
        }

        if (params_.method == AmbiStereoMethod::SpacedOmni && abDelaySamples_ > 0.0f) {
            const uint32_t delayA = static_cast<uint32_t>(std::floor(abDelaySamples_));
            const uint32_t readA = (abPos_ + kAbBufferSize - delayA) % kAbBufferSize;
            const uint32_t readB = (readA + kAbBufferSize - 1u) % kAbBufferSize;
            const float frac = abDelaySamples_ - std::floor(abDelaySamples_);
            const float delayedL = abLeft_[readA] * (1.0f - frac) + abLeft_[readB] * frac;
            const float delayedR = abRight_[readA] * (1.0f - frac) + abRight_[readB] * frac;
            abLeft_[abPos_] = left;
            abRight_[abPos_] = right;
            abPos_ = (abPos_ + 1u) % kAbBufferSize;
            const float mid = (left + right) * 0.5f;
            float side = (left - right) * 0.5f;
            const float delayedSide = (delayedL - delayedR) * 0.5f;
            const float sideMix = std::min(0.70f, params_.abSpacingCm / 120.0f * 0.70f);
            side = side * (1.0f - sideMix) + delayedSide * sideMix;
            left = mid + side;
            right = mid - side;
        } else {
            abLeft_[abPos_] = left;
            abRight_[abPos_] = right;
            abPos_ = (abPos_ + 1u) % kAbBufferSize;
        }

        if (params_.bassMonoHz > 0.0f) {
            bassLeft_ = bassLeft_ * bassA_ + left * (1.0f - bassA_);
            bassRight_ = bassRight_ * bassA_ + right * (1.0f - bassA_);
            const float bassMono = (bassLeft_ + bassRight_) * 0.5f;
            left = left - bassLeft_ + bassMono;
            right = right - bassRight_ + bassMono;
        }

        const float gain = autogain_ * dbToGain(params_.outputGainDb);
        left = clamp(left * gain, -4.0f, 4.0f);
        right = clamp(right * gain, -4.0f, 4.0f);
    }

private:
    static constexpr uint32_t kAbBufferSize = 4096;

    void rebuildCoefficients()
    {
        leftCoeffs_.fill(0.0f);
        rightCoeffs_.fill(0.0f);
        const uint32_t channels = ambiChannelsForOrder(params_.order);
        const uint32_t vcount = ambiStereoVirtualCount(params_.layout);
        const bool normalizeRows = params_.weighting != AmbiStereoWeighting::Projection;
        for (uint32_t spk = 0; spk < vcount; ++spk) {
            const auto pt = ambiStereoVirtualPoint(params_.layout, spk);
            float pickL = 0.0f;
            float pickR = 0.0f;
            ambiStereoPickup(params_, pt.azimuthDeg, pt.elevationDeg, pickL, pickR);
            const auto basis = acnSn3dBasis7(directionFromAed(pt.azimuthDeg, pt.elevationDeg));
            float sumSq = 0.0f;
            for (uint32_t ch = 0; ch < channels; ++ch) {
                const float weighted = basis[ch] * ambiStereoOrderWeight(ch, params_.order, params_);
                sumSq += weighted * weighted;
            }
            const float norm = normalizeRows ? 1.0f / std::sqrt(std::max(0.000000001f, sumSq)) : 1.0f;
            const float scale = norm / std::sqrt(static_cast<float>(vcount));
            for (uint32_t ch = 0; ch < channels; ++ch) {
                const float weighted = basis[ch] * ambiStereoOrderWeight(ch, params_.order, params_) * scale;
                leftCoeffs_[ch] += weighted * pickL;
                rightCoeffs_[ch] += weighted * pickR;
            }
        }

        if (params_.autogain == AmbiStereoAutogain::PowerSqrtN) {
            autogain_ = 1.0f / std::sqrt(static_cast<float>(channels));
        } else if (params_.autogain == AmbiStereoAutogain::EnergySum) {
            autogain_ = 1.0f / static_cast<float>(std::max(1u, channels));
        } else {
            autogain_ = 1.0f;
        }
        bassA_ = params_.bassMonoHz > 0.0f ? std::exp(-2.0f * kPi * params_.bassMonoHz / static_cast<float>(sampleRate_)) : 0.0f;
        abDelaySamples_ = std::min<float>(static_cast<float>(kAbBufferSize - 2u),
            params_.abSpacingCm * 0.01f / 343.0f * static_cast<float>(sampleRate_));
    }

    double sampleRate_ = 48000.0;
    AmbiStereoParams params_ {};
    std::array<float, kAmbiStereoDecoderMaxChannels> leftCoeffs_ {};
    std::array<float, kAmbiStereoDecoderMaxChannels> rightCoeffs_ {};
    std::array<float, kAbBufferSize> abLeft_ {};
    std::array<float, kAbBufferSize> abRight_ {};
    uint32_t abPos_ = 0;
    float bassLeft_ = 0.0f;
    float bassRight_ = 0.0f;
    float bassA_ = 0.0f;
    float abDelaySamples_ = 0.0f;
    float autogain_ = 1.0f;
};

} // namespace s3g
