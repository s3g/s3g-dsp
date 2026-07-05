#pragma once

#include "s3g_math.h"

namespace s3g {

class SmoothedValue {
public:
    void reset(float sampleRate, float timeMs, float initial)
    {
        value_ = initial;
        target_ = initial;
        setTime(sampleRate, timeMs);
    }

    void setTime(float sampleRate, float timeMs)
    {
        const float samples = std::max(1.0f, sampleRate * timeMs * 0.001f);
        coefficient_ = 1.0f - std::exp(-1.0f / samples);
    }

    void setTarget(float target)
    {
        target_ = target;
    }

    float next()
    {
        value_ += (target_ - value_) * coefficient_;
        return value_;
    }

    float current() const { return value_; }
    float target() const { return target_; }

private:
    float value_ = 0.0f;
    float target_ = 0.0f;
    float coefficient_ = 1.0f;
};

} // namespace s3g
