#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace s3g {

constexpr float kDenormalGuardThreshold = 1.0e-20f;
constexpr double kDenormalGuardThresholdDouble = 1.0e-30;

inline float flushDenormal(float value)
{
    return std::abs(value) < kDenormalGuardThreshold ? 0.0f : value;
}

inline double flushDenormal(double value)
{
    return std::abs(value) < kDenormalGuardThresholdDouble ? 0.0 : value;
}

template <typename Sample>
inline void clearChannel(Sample* data, uint32_t frames)
{
    if (data && frames > 0) {
        std::fill(data, data + frames, static_cast<Sample>(0));
    }
}

template <typename Buffer>
inline void clearAudioBuffer(const Buffer& output, uint32_t frames)
{
    for (uint32_t ch = 0; ch < output.channel_count; ++ch) {
        if (output.data32) {
            clearChannel(output.data32[ch], frames);
        }
        if (output.data64) {
            clearChannel(output.data64[ch], frames);
        }
    }
}

template <typename Buffer>
inline void clearAudioBufferFromChannel(const Buffer& output, uint32_t firstChannel, uint32_t frames)
{
    for (uint32_t ch = firstChannel; ch < output.channel_count; ++ch) {
        if (output.data32) {
            clearChannel(output.data32[ch], frames);
        }
        if (output.data64) {
            clearChannel(output.data64[ch], frames);
        }
    }
}

class AtomicPeakMeter {
public:
    void reset()
    {
        peak_.store(0.0f, std::memory_order_relaxed);
    }

    void push(float peak, float decay = 0.94f)
    {
        const float previous = peak_.load(std::memory_order_relaxed);
        peak_.store(std::max(previous * decay, peak), std::memory_order_relaxed);
    }

    float read(float decay = 1.0f)
    {
        const float value = peak_.load(std::memory_order_relaxed);
        if (decay < 1.0f) {
            peak_.store(value * decay, std::memory_order_relaxed);
        }
        return value;
    }

private:
    std::atomic<float> peak_ { 0.0f };
};

} // namespace s3g
