#pragma once

#include "s3g_mc_to_quad.h"
#include "s3g_mc_to_stereo.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

constexpr uint32_t kRingOutputChannels = 8u;

enum class RingOutputFormat : uint32_t {
    Direct8 = 0u,
    QuadRing,
    StereoRing,
    Count,
};

constexpr uint32_t kRingOutputFormatCount =
    static_cast<uint32_t>(RingOutputFormat::Count);

inline RingOutputFormat sanitizeRingOutputFormat(uint32_t value)
{
    return static_cast<RingOutputFormat>(std::min<uint32_t>(
        value, kRingOutputFormatCount - 1u));
}

inline float sanitizeRingOutputRotation(float degrees)
{
    return std::clamp(std::isfinite(degrees) ? degrees : 0.0f,
        -180.0f, 180.0f);
}

inline const char* ringOutputFormatName(RingOutputFormat format)
{
    switch (format) {
    case RingOutputFormat::Direct8: return "8CH DIRECT";
    case RingOutputFormat::QuadRing: return "QUAD RING";
    case RingOutputFormat::StereoRing: return "STEREO RING";
    case RingOutputFormat::Count: break;
    }
    return "8CH DIRECT";
}

// Cached gain projection for instruments that always publish an eight-channel
// host bus but may render a quad or stereo monitor format into its first
// channels. Configuration is allocation-free and safe on the audio thread.
class RingOutputMixdown {
public:
    void configure(RingOutputFormat format, float rotationDegrees)
    {
        format = sanitizeRingOutputFormat(static_cast<uint32_t>(format));
        rotationDegrees = sanitizeRingOutputRotation(rotationDegrees);
        if (configured_ && format == format_
            && rotationDegrees == rotationDegrees_) return;

        format_ = format;
        rotationDegrees_ = rotationDegrees;
        configured_ = true;
        if (format_ == RingOutputFormat::Direct8) return;

        McStereoParams params {};
        params.inputChannels = kRingOutputChannels;
        params.rotationDegrees = rotationDegrees_;
        params.layout = McStereoLayout::RingProjection;
        params.autogain = McStereoAutogain::PowerSqrtN;
        params.outputGainDb = 0.0f;
        if (format_ == RingOutputFormat::QuadRing) {
            makeMcToQuadGains(quadGains_.data(), kRingOutputChannels,
                params);
        } else {
            makeMcToStereoGains(stereoGains_.data(), kRingOutputChannels,
                params);
        }
    }

    RingOutputFormat format() const { return format_; }
    float rotationDegrees() const { return rotationDegrees_; }

    void processFrame(const float* direct, float* output) const
    {
        if (!direct || !output) return;
        if (format_ == RingOutputFormat::Direct8) {
            std::copy_n(direct, kRingOutputChannels, output);
            return;
        }

        std::fill_n(output, kRingOutputChannels, 0.0f);
        if (format_ == RingOutputFormat::QuadRing) {
            for (uint32_t channel = 0u; channel < kRingOutputChannels;
                 ++channel) {
                const float value = direct[channel];
                output[0] += value * quadGains_[channel].left;
                output[1] += value * quadGains_[channel].right;
                output[2] += value * quadGains_[channel].rightBack;
                output[3] += value * quadGains_[channel].leftBack;
            }
            return;
        }

        for (uint32_t channel = 0u; channel < kRingOutputChannels;
             ++channel) {
            const float value = direct[channel];
            output[0] += value * stereoGains_[channel].left;
            output[1] += value * stereoGains_[channel].right;
        }
    }

private:
    bool configured_ = false;
    RingOutputFormat format_ = RingOutputFormat::Direct8;
    float rotationDegrees_ = 0.0f;
    std::array<McQuadChannelGains,
        kMcToStereoMaxInputChannels> quadGains_ {};
    std::array<McStereoChannelGains,
        kMcToStereoMaxInputChannels> stereoGains_ {};
};

} // namespace s3g
