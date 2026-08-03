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

inline bool ambiRotateParamsEqual(const AmbiRotateParams& a, const AmbiRotateParams& b)
{
    return a.order == b.order
        && a.yawDeg == b.yawDeg
        && a.pitchDeg == b.pitchDeg
        && a.rollDeg == b.rollDeg
        && a.spread == b.spread
        && a.tilt == b.tilt
        && a.twist == b.twist
        && a.width == b.width
        && a.outputGainDb == b.outputGainDb;
}

inline bool ambiRotateGeometryParamsEqual(const AmbiRotateParams& a, const AmbiRotateParams& b)
{
    return a.order == b.order
        && a.yawDeg == b.yawDeg
        && a.pitchDeg == b.pitchDeg
        && a.rollDeg == b.rollDeg
        && a.spread == b.spread
        && a.tilt == b.tilt
        && a.twist == b.twist
        && a.width == b.width;
}

inline Vec3 ambiUtilityRotate(Vec3 v, float yawDeg, float pitchDeg, float rollDeg)
{
    const float yaw = yawDeg * kPi / 180.0f;
    const float pitch = pitchDeg * kPi / 180.0f;
    const float roll = rollDeg * kPi / 180.0f;
    const float cosYaw = std::cos(yaw);
    const float sinYaw = std::sin(yaw);
    const float cosPitch = std::cos(pitch);
    const float sinPitch = std::sin(pitch);
    const float cosRoll = std::cos(roll);
    const float sinRoll = std::sin(roll);

    Vec3 out {};
    out.x = v.x * cosYaw - v.y * sinYaw;
    out.y = v.x * sinYaw + v.y * cosYaw;
    out.z = v.z;

    Vec3 p {};
    p.x = out.x * cosPitch + out.z * sinPitch;
    p.y = out.y;
    p.z = -out.x * sinPitch + out.z * cosPitch;

    Vec3 r {};
    r.x = p.x;
    r.y = p.y * cosRoll - p.z * sinRoll;
    r.z = p.y * sinRoll + p.z * cosRoll;
    return normalize(r);
}

// Rotation rebuilds need this basis for every sampling direction. The generic
// generator intentionally favors clarity, but repeats order-only SN3D factors
// and the same sin/cos terms for multiple coefficients. Cache those invariants
// here while retaining its established lower-order compatibility basis.
inline std::array<float, kAmbiUtilityChannels> ambiRotateSn3dBasis7(Vec3 p)
{
    static const std::array<float, kAmbiUtilityChannels> normalization = [] {
        std::array<float, kAmbiUtilityChannels> values {};
        for (uint32_t n = 0u; n <= kAmbiUtilityMaxOrder; ++n) {
            const uint32_t base = n * n;
            for (int m = -static_cast<int>(n); m <= static_cast<int>(n); ++m) {
                const uint32_t absM = static_cast<uint32_t>(std::abs(m));
                values[base + static_cast<uint32_t>(m + static_cast<int>(n))]
                    = std::sqrt((absM == 0u ? 1.0f : 2.0f)
                        * factorialRatio(n - absM, n + absM));
            }
        }
        return values;
    }();

    const Vec3 direction = normalize(p);
    const float az = std::atan2(direction.y, direction.x);
    const float z = clamp(direction.z, -1.0f, 1.0f);
    const float rxy = std::sqrt(std::max(0.0f, 1.0f - z * z));
    float legendre[kAmbiUtilityMaxOrder + 1u][kAmbiUtilityMaxOrder + 1u] {};
    legendre[0][0] = 1.0f;
    for (uint32_t m = 1u; m <= kAmbiUtilityMaxOrder; ++m) {
        legendre[m][m] = static_cast<float>(2u * m - 1u) * rxy
            * legendre[m - 1u][m - 1u];
    }
    for (uint32_t m = 0u; m < kAmbiUtilityMaxOrder; ++m) {
        legendre[m + 1u][m] = static_cast<float>(2u * m + 1u) * z
            * legendre[m][m];
    }
    for (uint32_t m = 0u; m <= kAmbiUtilityMaxOrder; ++m) {
        for (uint32_t n = m + 2u; n <= kAmbiUtilityMaxOrder; ++n) {
            legendre[n][m] = (static_cast<float>(2u * n - 1u) * z
                    * legendre[n - 1u][m]
                - static_cast<float>(n + m - 1u) * legendre[n - 2u][m])
                / static_cast<float>(n - m);
        }
    }

    std::array<float, kAmbiUtilityMaxOrder + 1u> sine {};
    std::array<float, kAmbiUtilityMaxOrder + 1u> cosine {};
    cosine[0] = 1.0f;
    for (uint32_t m = 1u; m <= kAmbiUtilityMaxOrder; ++m) {
        const float angle = static_cast<float>(m) * az;
        sine[m] = std::sin(angle);
        cosine[m] = std::cos(angle);
    }

    std::array<float, kAmbiUtilityChannels> out {};
    for (uint32_t n = 0u; n <= kAmbiUtilityMaxOrder; ++n) {
        const uint32_t base = n * n;
        for (int m = -static_cast<int>(n); m <= static_cast<int>(n); ++m) {
            const uint32_t absM = static_cast<uint32_t>(std::abs(m));
            const uint32_t index = base
                + static_cast<uint32_t>(m + static_cast<int>(n));
            const float trig = m < 0 ? sine[absM]
                : (m > 0 ? cosine[absM] : 1.0f);
            out[index] = normalization[index] * legendre[n][absM] * trig;
        }
    }
    const auto basis3 = acnSn3dBasis(p);
    for (uint32_t i = 0u; i < k3OaChannels; ++i) out[i] = basis3[i];
    return out;
}

// The sampling directions and their output bases never change. Keeping one
// process-wide cache avoids regenerating the seventh-order basis for every
// automated matrix update (and once per lane in the grouped processors).
struct AmbiRotateDirectionCache {
    std::array<Vec3, kAmbiRotateDirections> directions {};
    std::array<std::array<float, kAmbiUtilityChannels>, kAmbiRotateDirections> outputBasis {};
    std::array<float, kAmbiRotateDirections> side {};
    std::array<float, kAmbiRotateDirections> frontBack {};

    AmbiRotateDirectionCache()
    {
        const float golden = kPi * (3.0f - std::sqrt(5.0f));
        for (uint32_t i = 0; i < kAmbiRotateDirections; ++i) {
            const float z = 1.0f - 2.0f * (static_cast<float>(i) + 0.5f)
                / static_cast<float>(kAmbiRotateDirections);
            const float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
            const float a = golden * static_cast<float>(i);
            directions[i] = { std::cos(a) * r, std::sin(a) * r, z };
            outputBasis[i] = ambiRotateSn3dBasis7(directions[i]);
            const float az = std::atan2(directions[i].y, directions[i].x);
            side[i] = std::sin(az);
            frontBack[i] = std::cos(az);
        }
    }
};

inline const AmbiRotateDirectionCache& ambiRotateDirectionCache()
{
    static const AmbiRotateDirectionCache cache;
    return cache;
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
        (void)ambiRotateDirectionCache();
        rebuildMatrix();
        current_ = target_;
        smoothing_ = false;
    }

    void setParams(AmbiRotateParams params)
    {
        const AmbiRotateParams next = sanitizeAmbiRotateParams(params);
        if (ambiRotateParamsEqual(params_, next)) return;
        if (ambiRotateGeometryParamsEqual(params_, next)) {
            const float scale = dbToGain(next.outputGainDb) / dbToGain(params_.outputGainDb);
            const uint32_t n = ambiUtilityChannelsForOrder(params_.order);
            for (uint32_t row = 0; row < n; ++row) {
                for (uint32_t col = 0; col < n; ++col) target_[row][col] *= scale;
            }
            params_ = next;
            smoothing_ = true;
            return;
        }
        params_ = next;
        rebuildMatrix();
        smoothing_ = true;
    }

    void reset()
    {
        current_ = target_;
        smoothing_ = false;
    }

    const AmbiRotateParams& params() const { return params_; }

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
            if (smoothing_) {
                for (uint32_t row = 0; row < n; ++row) {
                    for (uint32_t col = 0; col < n; ++col) {
                        current_[row][col] += (target_[row][col] - current_[row][col]) * 0.0009f;
                    }
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
        if (smoothing_) {
            const uint32_t matrixChannels = ambiUtilityChannelsForOrder(params_.order);
            bool settled = true;
            for (uint32_t row = 0; row < matrixChannels && settled; ++row) {
                for (uint32_t col = 0; col < matrixChannels; ++col) {
                    if (std::abs(target_[row][col] - current_[row][col]) > 0.000001f) {
                        settled = false;
                        break;
                    }
                }
            }
            smoothing_ = !settled;
        }
    }

private:
    void rebuildMatrix()
    {
        for (auto& row : target_) row.fill(0.0f);
        const uint32_t n = ambiUtilityChannelsForOrder(params_.order);
        const float out = dbToGain(params_.outputGainDb);
        const float invDirs = 1.0f / static_cast<float>(kAmbiRotateDirections);
        std::array<float, kAmbiUtilityMaxOrder + 1u> widthByOrder {};
        widthByOrder[0] = 1.0f;
        for (uint32_t order = 1u; order <= params_.order; ++order) {
            widthByOrder[order] = std::pow(params_.width,
                static_cast<float>(order) / static_cast<float>(params_.order));
        }
        std::array<float, kAmbiUtilityChannels> rowScale {};
        for (uint32_t row = 0; row < n; ++row) {
            rowScale[row] = invDirs * out
                * widthByOrder[ambiUtilityOrderForChannel(row)];
        }
        const auto& cache = ambiRotateDirectionCache();
        for (uint32_t d = 0; d < kAmbiRotateDirections; ++d) {
            const auto& outBasis = cache.outputBasis[d];
            const float yawOffset = cache.side[d] * params_.spread * 54.0f;
            const float pitchOffset = cache.side[d] * params_.tilt * 42.0f;
            const float rollOffset = cache.frontBack[d] * params_.twist * 90.0f;
            const Vec3 srcDir = ambiUtilityRotate(cache.directions[d],
                -wrapAmbiRotateDeg(params_.yawDeg + yawOffset),
                -clamp(params_.pitchDeg + pitchOffset, -90.0f, 90.0f),
                -wrapAmbiRotateDeg(params_.rollDeg + rollOffset));
            if (params_.order <= 3u) {
                const auto inBasis = acnSn3dBasis(srcDir);
                for (uint32_t row = 0; row < n; ++row) {
                    for (uint32_t col = 0; col < n; ++col) {
                        target_[row][col] += outBasis[row] * inBasis[col] * rowScale[row];
                    }
                }
            } else {
                const auto inBasis = ambiRotateSn3dBasis7(srcDir);
                for (uint32_t row = 0; row < n; ++row) {
                    for (uint32_t col = 0; col < n; ++col) {
                        target_[row][col] += outBasis[row] * inBasis[col] * rowScale[row];
                    }
                }
            }
        }
    }

    AmbiRotateParams params_ {};
    std::array<std::array<float, kAmbiUtilityChannels>, kAmbiUtilityChannels> current_ {};
    std::array<std::array<float, kAmbiUtilityChannels>, kAmbiUtilityChannels> target_ {};
    bool smoothing_ = false;
};

// Group Rotate is permanently split into independent 3OA (16-channel) lanes.
// A compact processor keeps its matrices at the actual order and uses the
// established lower-order basis directly instead of generating 7OA data that
// can never reach an output.
class AmbiRotate3Processor {
public:
    static constexpr uint32_t kChannels = k3OaChannels;

    AmbiRotate3Processor()
    {
        params_.order = 3u;
        (void)ambiRotateDirectionCache();
        rebuildMatrix();
        current_ = target_;
    }

    void setParams(AmbiRotateParams params)
    {
        params.order = 3u;
        const AmbiRotateParams next = sanitizeAmbiRotateParams(params);
        if (ambiRotateParamsEqual(params_, next)) return;
        if (ambiRotateGeometryParamsEqual(params_, next)) {
            const float scale = dbToGain(next.outputGainDb) / dbToGain(params_.outputGainDb);
            for (auto& row : target_) {
                for (auto& coefficient : row) coefficient *= scale;
            }
            params_ = next;
            smoothing_ = true;
            return;
        }
        params_ = next;
        rebuildMatrix();
        smoothing_ = true;
    }

    void reset()
    {
        current_ = target_;
        smoothing_ = false;
    }

    const AmbiRotateParams& params() const { return params_; }

    template <typename Sample>
    void process(Sample** in, Sample** out, uint32_t inChannels, uint32_t outChannels, uint32_t frames)
    {
        const uint32_t requestedChannels = std::min<uint32_t>({ inChannels, outChannels, kChannels });
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
        const uint32_t effectiveOrder = std::min<uint32_t>(3u, ambiUtilityOrderForChannels(highestActive));
        const uint32_t n = std::min<uint32_t>({ requestedChannels, ambiUtilityChannelsForOrder(effectiveOrder), kChannels });
        std::array<double, kChannels> acc {};
        for (uint32_t i = 0; i < frames; ++i) {
            if (smoothing_) {
                for (uint32_t row = 0; row < n; ++row) {
                    for (uint32_t col = 0; col < n; ++col) {
                        current_[row][col] += (target_[row][col] - current_[row][col]) * 0.0009f;
                    }
                }
            }
            acc.fill(0.0);
            for (uint32_t row = 0; row < n; ++row) {
                double sum = 0.0;
                for (uint32_t col = 0; col < n; ++col) {
                    sum += static_cast<double>(in[col] ? in[col][i] : Sample(0))
                        * static_cast<double>(current_[row][col]);
                }
                acc[row] = sum;
            }
            for (uint32_t ch = 0; ch < n; ++ch) {
                out[ch][i] = static_cast<Sample>(std::clamp(acc[ch], -8.0, 8.0));
            }
            for (uint32_t ch = n; ch < outChannels; ++ch) out[ch][i] = Sample(0);
        }
        if (smoothing_) {
            bool settled = true;
            for (uint32_t row = 0; row < kChannels && settled; ++row) {
                for (uint32_t col = 0; col < kChannels; ++col) {
                    if (std::abs(target_[row][col] - current_[row][col]) > 0.000001f) {
                        settled = false;
                        break;
                    }
                }
            }
            smoothing_ = !settled;
        }
    }

private:
    void rebuildMatrix()
    {
        for (auto& row : target_) row.fill(0.0f);
        const float out = dbToGain(params_.outputGainDb);
        const float invDirs = 1.0f / static_cast<float>(kAmbiRotateDirections);
        std::array<float, 4u> widthByOrder {};
        widthByOrder[0] = 1.0f;
        for (uint32_t order = 1u; order <= 3u; ++order) {
            widthByOrder[order] = std::pow(params_.width, static_cast<float>(order) / 3.0f);
        }
        std::array<float, kChannels> rowScale {};
        for (uint32_t row = 0; row < kChannels; ++row) {
            rowScale[row] = invDirs * out
                * widthByOrder[ambiUtilityOrderForChannel(row)];
        }
        const auto& cache = ambiRotateDirectionCache();
        for (uint32_t d = 0; d < kAmbiRotateDirections; ++d) {
            const float yawOffset = cache.side[d] * params_.spread * 54.0f;
            const float pitchOffset = cache.side[d] * params_.tilt * 42.0f;
            const float rollOffset = cache.frontBack[d] * params_.twist * 90.0f;
            const Vec3 srcDir = ambiUtilityRotate(cache.directions[d],
                -wrapAmbiRotateDeg(params_.yawDeg + yawOffset),
                -clamp(params_.pitchDeg + pitchOffset, -90.0f, 90.0f),
                -wrapAmbiRotateDeg(params_.rollDeg + rollOffset));
            const auto inBasis = acnSn3dBasis(srcDir);
            const auto& outBasis = cache.outputBasis[d];
            for (uint32_t row = 0; row < kChannels; ++row) {
                for (uint32_t col = 0; col < kChannels; ++col) {
                    target_[row][col] += outBasis[row] * inBasis[col] * rowScale[row];
                }
            }
        }
    }

    AmbiRotateParams params_ {};
    std::array<std::array<float, kChannels>, kChannels> current_ {};
    std::array<std::array<float, kChannels>, kChannels> target_ {};
    bool smoothing_ = false;
};

} // namespace s3g
