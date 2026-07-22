#pragma once

#include "s3g_math.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace s3g {

struct AmbiEncoderDepthParams {
    float doppler = 0.0f;
    float air = 0.0f;
};

inline AmbiEncoderDepthParams sanitizeAmbiEncoderDepthParams(AmbiEncoderDepthParams params)
{
    params.doppler = clamp(params.doppler, 0.0f, 1.0f);
    params.air = clamp(params.air, 0.0f, 1.0f);
    return params;
}

template <uint32_t Sources, uint32_t MaxDelayMs = 200>
class AmbiEncoderDepthProcessor {
public:
    static constexpr uint32_t kMinDelaySamples = 64;
    static constexpr uint32_t kMaxDelaySamples = 19200;

    void prepare(double sampleRate)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        delayCapacity_ = std::clamp<uint32_t>(
            static_cast<uint32_t>(std::ceil(sampleRate_ * static_cast<double>(MaxDelayMs) * 0.001)) + 4u,
            kMinDelaySamples,
            kMaxDelaySamples);
        for (auto& line : delay_) line.assign(delayCapacity_, 0.0f);
        reset();
    }

    void reset()
    {
        writeIndex_ = 0;
        for (auto& line : delay_) std::fill(line.begin(), line.end(), 0.0f);
        smoothedDelay_.fill(0.0f);
        airState_.fill(0.0f);
    }

    void setParams(AmbiEncoderDepthParams params)
    {
        params_ = sanitizeAmbiEncoderDepthParams(params);
    }

    const AmbiEncoderDepthParams& params() const { return params_; }

    float process(uint32_t source, float sample, float distance)
    {
        source = std::min<uint32_t>(source, Sources - 1u);
        distance = clamp(distance, 0.05f, 8.0f);
        // Keep both stateful paths warm even at zero amount. Switching either
        // processor on can then remain continuous instead of exposing an empty
        // delay line or a stale filter state.
        float out = processDoppler(source, sample, distance);
        const float far = clamp((distance - 0.35f) / 7.65f, 0.0f, 1.0f);
        const float cutoff = lerp(19000.0f, 1800.0f, params_.air * std::pow(far, 0.58f));
        const float x = std::exp(-2.0f * kPi * cutoff / static_cast<float>(sampleRate_));
        airState_[source] = out * (1.0f - x) + airState_[source] * x;
        out = lerp(out, airState_[source], params_.air);

        return flushDenormal(out);
    }

    void advance()
    {
        writeIndex_ = (writeIndex_ + 1u) % delayCapacity_;
    }

private:
    float targetDelaySamples(float distance) const
    {
        // These encoders use normalized scene units rather than meters. 20 ms/unit gives
        // audible movement without turning depth into a long echo line.
        const float seconds = distance * 0.020f * params_.doppler;
        return clamp(seconds * static_cast<float>(sampleRate_), 0.0f, static_cast<float>(delayCapacity_ - 4u));
    }

    float processDoppler(uint32_t source, float sample, float distance)
    {
        auto& line = delay_[source];
        if (line.empty()) return sample;
        line[writeIndex_] = sample;

        const float target = targetDelaySamples(distance);
        const float coefficient = 1.0f - std::exp(-1.0f / static_cast<float>(sampleRate_ * 0.005));
        smoothedDelay_[source] += (target - smoothedDelay_[source]) * coefficient;
        const float delay = clamp(smoothedDelay_[source], 0.0f, static_cast<float>(delayCapacity_ - 4u));
        float read = static_cast<float>(writeIndex_) - delay;
        while (read < 0.0f) read += static_cast<float>(delayCapacity_);
        const uint32_t i0 = static_cast<uint32_t>(std::floor(read)) % delayCapacity_;
        const uint32_t i1 = (i0 + 1u) % delayCapacity_;
        const float frac = read - std::floor(read);
        return lerp(line[i0], line[i1], frac);
    }

    double sampleRate_ = 48000.0;
    uint32_t delayCapacity_ = kMinDelaySamples;
    uint32_t writeIndex_ = 0;
    AmbiEncoderDepthParams params_ {};
    std::array<std::vector<float>, Sources> delay_ {};
    std::array<float, Sources> smoothedDelay_ {};
    std::array<float, Sources> airState_ {};
};

} // namespace s3g
