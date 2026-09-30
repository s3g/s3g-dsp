#pragma once

#include "s3g_sample_neon_fx.h"

namespace s3g::sample {

inline constexpr double kDeckCueFadeSeconds = .005;

// Changing a Character resets its delay/filter memories. Reach dry before
// doing that, then fade in the new processor. Keep the outgoing settings alive
// during the fade, including when a held/toggled Punch returns to the saved FX.
// One processor and one preallocated scratch buffer: no second effect bank or
// allocation in process(), and one shared envelope for every field component.
class DeckCharacterFx {
public:
    bool prepare(double rate, unsigned frames) {
        if (!processor_.prepare(rate)) return false;
        try { for (auto& channel : dry_) channel.resize(frames); }
        catch (...) { return false; }
        fadeFrames_ = std::max(1u, static_cast<unsigned>(std::ceil(.005 * rate)));
        maximum_ = frames;
        reset();
        return true;
    }
    void reset() noexcept {
        processor_.reset();
        effect_ = 99; position_ = 0; amount_ = 0; values_ = {};
    }
    void process(float* const* audio, unsigned channels, unsigned frames,
        SampleNeonMangleCharacter character, const NeonCharacterValues& values,
        float amount, bool ambisonic, double tempo) noexcept {
        if (!audio || frames > maximum_) return;
        channels = std::min(channels, 16u);
        const auto desired = static_cast<unsigned>(character);
        for (unsigned offset = 0; offset < frames;) {
            if (effect_ == 99 || (effect_ != desired && position_ == 0)) {
                processor_.reset(); effect_ = desired;
            }
            const bool leaving = effect_ != desired;
            if (!leaving) { values_ = values; amount_ = amount; }
            const unsigned count = leaving ? std::min(frames - offset, position_) : frames - offset;
            const bool blending = leaving || position_ < fadeFrames_;
            std::array<float*, 16> chunk {};
            for (unsigned ch = 0; ch < channels; ++ch) {
                chunk[ch] = audio[ch] + offset;
                if (blending) std::copy_n(chunk[ch], count, dry_[ch].data());
            }
            processor_.process(chunk.data(), channels, count,
                static_cast<SampleNeonMangleCharacter>(effect_), values_, amount_, ambisonic, tempo);
            if (blending) for (unsigned n = 0; n < count; ++n) {
                const float wet = static_cast<float>(position_) / fadeFrames_;
                for (unsigned ch = 0; ch < channels; ++ch)
                    chunk[ch][n] = dry_[ch][n] + wet * (chunk[ch][n] - dry_[ch][n]);
                if (leaving) --position_;
                else if (position_ < fadeFrames_) ++position_;
            }
            offset += count;
        }
    }
private:
    SampleNeonFx processor_;
    std::array<std::vector<float>, 16> dry_;
    NeonCharacterValues values_ {};
    unsigned effect_ = 99, position_ = 0, fadeFrames_ = 1, maximum_ = 0;
    float amount_ = 0;
};

} // namespace s3g::sample
