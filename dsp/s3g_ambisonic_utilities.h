#pragma once

#include "s3g_ambisonic_speaker_decoder.h"
#include "s3g_math.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

constexpr uint32_t kAmbiUtilityMaxOrder = 7;
constexpr uint32_t kAmbiUtilityChannels = 64;
constexpr uint32_t kAmbiRotateDirections = 144;

enum class AmbiUtilityWeighting : uint32_t {
    Flat = 0,
    MaxRe = 1,
    InPhase = 2,
    Custom = 3,
};

struct AmbiOrderBandParams {
    uint32_t order = 7;
    AmbiUtilityWeighting weighting = AmbiUtilityWeighting::Flat;
    float blend = 1.0f;
    float outputGainDb = 0.0f;
    std::array<float, kAmbiUtilityMaxOrder + 1u> orderGain {};
};

struct AmbiRotateParams {
    uint32_t order = 7;
    float yawDeg = 0.0f;
    float pitchDeg = 0.0f;
    float rollDeg = 0.0f;
    float spread = 0.0f;
    float tilt = 0.0f;
    float twist = 0.0f;
    float width = 1.0f;
    float outputGainDb = 0.0f;
};

inline float wrapAmbiRotateDeg(float deg)
{
    while (deg > 180.0f) {
        deg -= 360.0f;
    }
    while (deg < -180.0f) {
        deg += 360.0f;
    }
    return deg;
}

inline uint32_t ambiUtilityChannelsForOrder(uint32_t order)
{
    order = std::clamp<uint32_t>(order, 1u, kAmbiUtilityMaxOrder);
    return (order + 1u) * (order + 1u);
}

inline uint32_t ambiUtilityOrderForChannels(uint32_t channels)
{
    channels = std::max<uint32_t>(1u, channels);
    uint32_t order = 1u;
    while (order < kAmbiUtilityMaxOrder && ambiUtilityChannelsForOrder(order + 1u) <= channels) {
        ++order;
    }
    return order;
}

inline uint32_t ambiUtilityOrderForChannel(uint32_t acn)
{
    return std::min<uint32_t>(kAmbiUtilityMaxOrder, static_cast<uint32_t>(std::sqrt(static_cast<float>(acn))));
}

inline float ambiUtilityLegendreP(uint32_t n, float x)
{
    if (n == 0u) return 1.0f;
    if (n == 1u) return x;
    float p0 = 1.0f;
    float p1 = x;
    for (uint32_t order = 2; order <= n; ++order) {
        const float p = ((2.0f * static_cast<float>(order) - 1.0f) * x * p1
            - (static_cast<float>(order) - 1.0f) * p0) / static_cast<float>(order);
        p0 = p1;
        p1 = p;
    }
    return p1;
}

inline float ambiUtilityStandardOrderWeight(AmbiUtilityWeighting weighting, uint32_t order, uint32_t maxOrder)
{
    if (order == 0u || weighting == AmbiUtilityWeighting::Flat || weighting == AmbiUtilityWeighting::Custom) return 1.0f;
    maxOrder = std::max<uint32_t>(1u, maxOrder);
    if (weighting == AmbiUtilityWeighting::MaxRe) {
        const float angle = (137.9f * kPi / 180.0f) / (static_cast<float>(maxOrder) + 1.51f);
        return clamp(ambiUtilityLegendreP(order, std::cos(angle)), 0.0f, 1.0f);
    }
    if (weighting == AmbiUtilityWeighting::InPhase) {
        float value = 1.0f;
        for (uint32_t k = 0; k < order; ++k) {
            value *= static_cast<float>(maxOrder - k) / static_cast<float>(maxOrder + k + 2u);
        }
        return clamp(value, 0.0f, 1.0f);
    }
    return 1.0f;
}

inline AmbiOrderBandParams sanitizeAmbiOrderBandParams(AmbiOrderBandParams params)
{
    params.order = std::clamp<uint32_t>(params.order, 1u, kAmbiUtilityMaxOrder);
    params.weighting = static_cast<AmbiUtilityWeighting>(std::min<uint32_t>(static_cast<uint32_t>(params.weighting), 3u));
    params.blend = clamp(params.blend, 0.0f, 1.0f);
    params.outputGainDb = clamp(params.outputGainDb, -60.0f, 12.0f);
    for (auto& gain : params.orderGain) gain = clamp(gain, 0.0f, 2.0f);
    return params;
}

inline AmbiRotateParams sanitizeAmbiRotateParams(AmbiRotateParams params)
{
    params.order = std::clamp<uint32_t>(params.order, 1u, kAmbiUtilityMaxOrder);
    params.yawDeg = wrapAmbiRotateDeg(params.yawDeg);
    params.pitchDeg = clamp(params.pitchDeg, -90.0f, 90.0f);
    params.rollDeg = wrapAmbiRotateDeg(params.rollDeg);
    params.spread = clamp(params.spread, -1.0f, 1.0f);
    params.tilt = clamp(params.tilt, -1.0f, 1.0f);
    params.twist = clamp(params.twist, -1.0f, 1.0f);
    params.width = clamp(params.width, 0.0f, 1.5f);
    params.outputGainDb = clamp(params.outputGainDb, -60.0f, 12.0f);
    return params;
}

inline Vec3 ambiUtilityRotate(Vec3 v, float yawDeg, float pitchDeg, float rollDeg)
{
    const float yaw = yawDeg * kPi / 180.0f;
    const float pitch = pitchDeg * kPi / 180.0f;
    const float roll = rollDeg * kPi / 180.0f;

    Vec3 out {};
    out.x = v.x * std::cos(yaw) - v.y * std::sin(yaw);
    out.y = v.x * std::sin(yaw) + v.y * std::cos(yaw);
    out.z = v.z;

    Vec3 p {};
    p.x = out.x * std::cos(pitch) + out.z * std::sin(pitch);
    p.y = out.y;
    p.z = -out.x * std::sin(pitch) + out.z * std::cos(pitch);

    Vec3 r {};
    r.x = p.x;
    r.y = p.y * std::cos(roll) - p.z * std::sin(roll);
    r.z = p.y * std::sin(roll) + p.z * std::cos(roll);
    return normalize(r);
}

class AmbiOrderBandProcessor {
public:
    void setParams(AmbiOrderBandParams params)
    {
        params_ = sanitizeAmbiOrderBandParams(params);
        const float out = dbToGain(params_.outputGainDb);
        for (uint32_t ch = 0; ch < kAmbiUtilityChannels; ++ch) {
            const uint32_t order = ambiUtilityOrderForChannel(ch);
            const float custom = params_.orderGain[order];
            const float standard = ambiUtilityStandardOrderWeight(params_.weighting, order, params_.order);
            const float shaped = params_.weighting == AmbiUtilityWeighting::Custom
                ? custom
                : lerp(custom, standard * custom, params_.blend);
            target_[ch] = ch < ambiUtilityChannelsForOrder(params_.order) ? shaped * out : 0.0f;
        }
    }

    void reset()
    {
        current_ = target_;
    }

    template <typename Sample>
    void process(Sample** in, Sample** out, uint32_t inChannels, uint32_t outChannels, uint32_t frames)
    {
        const uint32_t n = std::min<uint32_t>({ inChannels, outChannels, kAmbiUtilityChannels });
        for (uint32_t i = 0; i < frames; ++i) {
            for (uint32_t ch = 0; ch < n; ++ch) {
                current_[ch] += (target_[ch] - current_[ch]) * 0.0015f;
                out[ch][i] = static_cast<Sample>((in[ch] ? in[ch][i] : Sample(0)) * current_[ch]);
            }
            for (uint32_t ch = n; ch < outChannels; ++ch) out[ch][i] = Sample(0);
        }
    }

private:
    AmbiOrderBandParams params_ {};
    std::array<float, kAmbiUtilityChannels> current_ {};
    std::array<float, kAmbiUtilityChannels> target_ {};
};

class AmbiRotateProcessor {
public:
    AmbiRotateProcessor()
    {
        buildDirections();
        rebuildMatrix();
        current_ = target_;
    }

    void setParams(AmbiRotateParams params)
    {
        params_ = sanitizeAmbiRotateParams(params);
        rebuildMatrix();
    }

    void reset()
    {
        current_ = target_;
    }

    template <typename Sample>
    void process(Sample** in, Sample** out, uint32_t inChannels, uint32_t outChannels, uint32_t frames)
    {
        const uint32_t requestedChannels = std::min<uint32_t>({ inChannels, outChannels, ambiUtilityChannelsForOrder(params_.order), kAmbiUtilityChannels });
        uint32_t highestActive = 0u;
        float blockPeak = 0.0f;
        for (uint32_t ch = 0; ch < requestedChannels; ++ch) {
            if (!in[ch]) continue;
            float channelPeak = 0.0f;
            for (uint32_t i = 0; i < frames; ++i) {
                channelPeak = std::max(channelPeak, std::abs(static_cast<float>(in[ch][i])));
            }
            if (channelPeak > 0.0000003f) {
                highestActive = ch + 1u;
                blockPeak = std::max(blockPeak, channelPeak);
            }
        }
        if (blockPeak <= 0.0000003f) {
            for (uint32_t ch = 0; ch < outChannels; ++ch) {
                if (out[ch]) std::fill(out[ch], out[ch] + frames, Sample {});
            }
            return;
        }
        const uint32_t effectiveOrder = std::min<uint32_t>(params_.order, ambiUtilityOrderForChannels(highestActive));
        const uint32_t n = std::min<uint32_t>({ requestedChannels, ambiUtilityChannelsForOrder(effectiveOrder), kAmbiUtilityChannels });
        std::array<double, kAmbiUtilityChannels> acc {};
        for (uint32_t i = 0; i < frames; ++i) {
            for (uint32_t row = 0; row < n; ++row) {
                for (uint32_t col = 0; col < n; ++col) {
                    current_[row][col] += (target_[row][col] - current_[row][col]) * 0.0009f;
                }
            }
            acc.fill(0.0);
            for (uint32_t row = 0; row < n; ++row) {
                double sum = 0.0;
                for (uint32_t col = 0; col < n; ++col) {
                    sum += static_cast<double>(in[col] ? in[col][i] : Sample(0)) * static_cast<double>(current_[row][col]);
                }
                acc[row] = sum;
            }
            for (uint32_t ch = 0; ch < n; ++ch) out[ch][i] = static_cast<Sample>(std::clamp(acc[ch], -8.0, 8.0));
            for (uint32_t ch = n; ch < outChannels; ++ch) out[ch][i] = Sample(0);
        }
    }

private:
    void buildDirections()
    {
        const float golden = kPi * (3.0f - std::sqrt(5.0f));
        for (uint32_t i = 0; i < kAmbiRotateDirections; ++i) {
            const float z = 1.0f - 2.0f * (static_cast<float>(i) + 0.5f) / static_cast<float>(kAmbiRotateDirections);
            const float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
            const float a = golden * static_cast<float>(i);
            dirs_[i] = { std::cos(a) * r, std::sin(a) * r, z };
        }
    }

    void rebuildMatrix()
    {
        for (auto& row : target_) row.fill(0.0f);
        const uint32_t n = ambiUtilityChannelsForOrder(params_.order);
        const float out = dbToGain(params_.outputGainDb);
        const float invDirs = 1.0f / static_cast<float>(kAmbiRotateDirections);
        for (uint32_t d = 0; d < kAmbiRotateDirections; ++d) {
            const auto outBasis = acnSn3dBasis7(dirs_[d]);
            const float az = std::atan2(dirs_[d].y, dirs_[d].x);
            const float side = std::sin(az);
            const float frontBack = std::cos(az);
            const float yawOffset = side * params_.spread * 54.0f;
            const float pitchOffset = side * params_.tilt * 42.0f;
            const float rollOffset = frontBack * params_.twist * 90.0f;
            const Vec3 srcDir = ambiUtilityRotate(dirs_[d],
                -wrapAmbiRotateDeg(params_.yawDeg + yawOffset),
                -clamp(params_.pitchDeg + pitchOffset, -90.0f, 90.0f),
                -wrapAmbiRotateDeg(params_.rollDeg + rollOffset));
            const auto inBasis = acnSn3dBasis7(srcDir);
            for (uint32_t row = 0; row < n; ++row) {
                const uint32_t order = ambiUtilityOrderForChannel(row);
                const float width = order == 0u ? 1.0f : std::pow(params_.width, static_cast<float>(order) / static_cast<float>(params_.order));
                for (uint32_t col = 0; col < n; ++col) {
                    target_[row][col] += outBasis[row] * inBasis[col] * invDirs * out * width;
                }
            }
        }
    }

    AmbiRotateParams params_ {};
    std::array<Vec3, kAmbiRotateDirections> dirs_ {};
    std::array<std::array<float, kAmbiUtilityChannels>, kAmbiUtilityChannels> current_ {};
    std::array<std::array<float, kAmbiUtilityChannels>, kAmbiUtilityChannels> target_ {};
};

} // namespace s3g
