#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

enum class FixedBusRingFormat : uint32_t {
    Direct16 = 0u,
    Ring8,
    QuadRing,
    StereoRing,
    Count,
};

constexpr uint32_t kFixedBusRingFormatCount =
    static_cast<uint32_t>(FixedBusRingFormat::Count);

inline FixedBusRingFormat sanitizeFixedBusRingFormat(uint32_t value)
{
    return static_cast<FixedBusRingFormat>(std::min<uint32_t>(value,
        kFixedBusRingFormatCount - 1u));
}

inline uint32_t fixedBusRingActiveChannels(FixedBusRingFormat format)
{
    switch (format) {
    case FixedBusRingFormat::Direct16: return 16u;
    case FixedBusRingFormat::Ring8: return 8u;
    case FixedBusRingFormat::QuadRing: return 4u;
    case FixedBusRingFormat::StereoRing: return 2u;
    case FixedBusRingFormat::Count: break;
    }
    return 16u;
}

inline const char* fixedBusRingFormatName(FixedBusRingFormat format)
{
    switch (format) {
    case FixedBusRingFormat::Direct16: return "16CH DIRECT";
    case FixedBusRingFormat::Ring8: return "8CH RING";
    case FixedBusRingFormat::QuadRing: return "QUAD RING";
    case FixedBusRingFormat::StereoRing: return "STEREO RING";
    case FixedBusRingFormat::Count: break;
    }
    return "16CH DIRECT";
}

inline float sanitizeFixedBusRingRotation(float degrees)
{
    return std::clamp(std::isfinite(degrees) ? degrees : 0.0f,
        -180.0f, 180.0f);
}

// Renders a complete sixteen-lane source ring into a selected 2/4/8/16-channel
// ring on a stable sixteen-channel host port. Every source lane always
// contributes, including in stereo. Rotation is an equal-power fractional
// output rotation; inactive host lanes are zero. Source gains remain at unity
// through down-folds so the energy of decorrelated delayed copies is retained.
// Coherent direct sound is intentionally injected after this renderer by
// DelayField rather than duplicated across the sixteen source lanes.
template <uint32_t HostChannels = 16u>
class FixedBusRingRenderer {
public:
    static_assert(HostChannels >= 16u,
        "the fixed ring renderer requires a sixteen-channel host bus");

    void configure(FixedBusRingFormat format, float rotationDegrees)
    {
        format_ = sanitizeFixedBusRingFormat(static_cast<uint32_t>(format));
        rotationDegrees_ = sanitizeFixedBusRingRotation(rotationDegrees);
    }

    FixedBusRingFormat format() const { return format_; }
    float rotationDegrees() const { return rotationDegrees_; }
    uint32_t activeChannels() const
    {
        return fixedBusRingActiveChannels(format_);
    }

    void processFrame(const float* activeInput, float* hostOutput) const
    {
        if (!activeInput || !hostOutput) return;
        std::fill_n(hostOutput, HostChannels, 0.0f);
        const uint32_t outputChannels = activeChannels();
        const float offset = rotationDegrees_ / 360.0f
            * static_cast<float>(outputChannels);
        constexpr float foldGain = 1.0f;
        constexpr float kHalfPi = 1.57079632679489661923f;
        for (uint32_t source = 0u; source < HostChannels; ++source) {
            float position = static_cast<float>(source)
                / static_cast<float>(HostChannels)
                * static_cast<float>(outputChannels) + offset;
            position -= std::floor(position
                / static_cast<float>(outputChannels))
                * static_cast<float>(outputChannels);
            const uint32_t first = static_cast<uint32_t>(std::floor(position))
                % outputChannels;
            const uint32_t second = (first + 1u) % outputChannels;
            const float fraction = position - std::floor(position);
            const float firstGain = std::cos(fraction * kHalfPi) * foldGain;
            const float secondGain = std::sin(fraction * kHalfPi) * foldGain;
            const float value = std::isfinite(activeInput[source])
                ? activeInput[source] : 0.0f;
            hostOutput[first] += value * firstGain;
            hostOutput[second] += value * secondGain;
        }
    }

private:
    FixedBusRingFormat format_ = FixedBusRingFormat::Direct16;
    float rotationDegrees_ = 0.0f;
};

} // namespace s3g
