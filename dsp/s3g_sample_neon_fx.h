#pragma once
#include "s3g_sample_neon_character.h"

namespace s3g::sample {
enum class SampleNeonMangleCharacter : uint8_t { Filter, Echo, Space, Shift, Vowel, Punch, Drive, Crush };
inline constexpr bool sampleNeonFxAmbisonicSafe(SampleNeonMangleCharacter fx) noexcept {
    return static_cast<unsigned>(fx) < 6;
}
inline const auto kSampleNeonFxDefaults = [] {
    std::array<NeonCharacterValues,8> values;
    for (unsigned n = 0; n < 8; ++n) values[n] = neonCharacterDefaults(n);
    return values;
}();
inline constexpr const auto& kSampleNeonFxNames = kNeonCharacterNames;
class SampleNeonFx : public NeonCharacterProcessor {
public:
    void process(float* const* audio, unsigned channels, uint32_t frames,
        SampleNeonMangleCharacter character, const NeonCharacterValues& values,
        float amount, bool ambisonic, double tempo = 120) noexcept {
        NeonCharacterProcessor::process(audio,channels,frames,static_cast<unsigned>(character),values,amount,ambisonic,tempo);
    }
};
} // namespace s3g::sample
