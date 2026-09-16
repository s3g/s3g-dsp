#pragma once

#include "s3g_ambisonic_speaker_decoder.h"

namespace s3g {

// Windows encoder specialization. The compatibility basis already replaces
// its first 16 canonical entries with acnSn3dBasis(). Consumers requesting at
// most 16 channels need exactly those entries. Higher-order output is unchanged.
// The caller must not consume the zeroed suffix when channels <= 16.
inline std::array<float, kAmbiSpeakerDecoderMaxChannels>
windowsEncoderBasis(Vec3 direction, uint32_t channels)
{
    if (channels > k3OaChannels) return acnSn3dBasis7(direction);
    std::array<float, kAmbiSpeakerDecoderMaxChannels> result {};
    const auto lower = acnSn3dBasis(direction);
    std::copy(lower.begin(), lower.end(), result.begin());
    return result;
}

} // namespace s3g
