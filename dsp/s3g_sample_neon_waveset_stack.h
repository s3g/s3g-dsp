#pragma once
#include "s3g_sample_neon_stack.h"
#include "s3g_sample_wavesets.h"
#include "s3g_sample_player.h"

namespace s3g::sample {
// Two cycle processors, not 32 running layers. Neighboring layers retain
// their processor as the scan crosses a boundary; the incoming layer starts
// at zero blend weight. All maps are prepared/owned outside the audio thread.
class NeonWavesetStackPlayer {
public:
    void prepare(double sampleRate) noexcept {
        for (auto& engine : engines_) engine.prepare(sampleRate, 16u);
        reset();
    }
    void reset() noexcept {
        for (auto& engine : engines_) engine.setPreparedMap(nullptr);
        maps_.fill(nullptr); layers_.fill(32u); cursorCount_ = 0u;
    }
    unsigned activeVoiceCount() const noexcept {
        return engines_[0].activeVoiceCount() + engines_[1].activeVoiceCount();
    }
    const auto& cursors() const noexcept { return cursors_; }
    unsigned cursorCount() const noexcept { return cursorCount_; }
    void render(WavesetSettings settings, const NeonStack* stack,
        unsigned selected, double start, double end, const float* velocities,
        const float* positions, const uint8_t* retriggers,
        float* const* output, uint32_t frames) noexcept {
        cursorCount_ = 0u;
        for (uint32_t at = 0u; at < frames;) {
            if (positions[at] < 0.0f) {
                if (positions[at] == -2.0f && (layers_[0] != 32u || layers_[1] != 32u)) reset(); // -3 pauses.
                if (positions[at] != -1.0f)
                    for (unsigned ch = 0u; ch < 16u; ++ch) output[ch][at] = 0.0f;
                ++at; continue; // -1 preserves direct CHOP audio.
            }
            if (retriggers[at]) reset();
            const auto blend = neonStackBlend(positions[at], stack ? stack->count : 0u);
            uint32_t size = 1u;
            while (at + size < frames && size < 32u && positions[at + size] >= 0.0f && !retriggers[at + size]) {
                const auto next = neonStackBlend(positions[at + size], stack ? stack->count : 0u);
                if (next.first != blend.first || next.second != blend.second) break;
                ++size;
            }
            for (unsigned ch = 0u; ch < 16u; ++ch) std::fill_n(output[ch] + at, size, 0.0f);
            for (unsigned side = 0u; side < (blend.first == blend.second ? 1u : 2u); ++side) {
                const unsigned layer = side ? blend.second : blend.first, lane = layer % 2u;
                const auto source = stack && layer < stack->count ? stack->layers[layer] : NeonStackLayer {};
                const auto* map = source.wavesets;
                WavesetRenderEvent note;
                bool trigger = maps_[lane] != map || layers_[lane] != layer;
                if (trigger) {
                    engines_[lane].setPreparedMap(map); maps_[lane] = map; layers_[lane] = layer;
                }
                if (!map || map->asset.get() != source.asset) continue;
                settings.start = layer == selected ? start : source.start;
                settings.end = layer == selected ? end : source.end;
                settings.loopStart = settings.start; settings.loopEnd = settings.end;
                // The pad gesture owns duration/envelopes. Cycle engines loop
                // until their blend fades out, including during HOLD release.
                settings.triggerMode = TriggerMode::Gate;
                settings.playMode = settings.playMode == WavesetPlayMode::Reverse
                    || settings.playMode == WavesetPlayMode::ReverseLoop
                    ? WavesetPlayMode::ReverseLoop : WavesetPlayMode::ForwardLoop;
                settings.attackSeconds = 0.0f;
                note.noteId = layer + 1u; note.velocity = velocities[at];
                std::array<float*, 16u> channels {};
                for (unsigned ch = 0u; ch < 16u; ++ch) channels[ch] = scratch_[ch].data();
                engines_[lane].render(settings, trigger ? &note : nullptr, trigger ? 1u : 0u, channels.data(), 16u, size);
                for (uint32_t frame = 0u; frame < size; ++frame) {
                    const auto b = neonStackBlend(positions[at + frame], stack->count);
                    const float gain = blend.first == blend.second ? 1.0f : side ? b.mix : 1.0f - b.mix;
                    for (unsigned ch = 0u; ch < 16u; ++ch) output[ch][at + frame] += scratch_[ch][frame] * gain;
                }
            }
            at += size;
        }
        if (!frames || positions[frames - 1u] < 0.0f) return;
        const auto blend = neonStackBlend(positions[frames - 1u], stack ? stack->count : 0u);
        for (unsigned side = 0u; side < (blend.first == blend.second ? 1u : 2u); ++side) {
            const unsigned layer = side ? blend.second : blend.first, lane = layer % 2u;
            if (!maps_[lane] || layers_[lane] != layer) continue;
            for (unsigned n = 0u; n < engines_[lane].voiceCursorCount() && cursorCount_ < cursors_.size(); ++n) {
                const auto& cursor = engines_[lane].voiceCursors()[n];
                const auto& source = stack->layers[layer];
                cursors_[cursorCount_++] = {cursor.sourcePositionNormalized, cursor.key,
                    static_cast<float>(layer == selected ? start : source.start),
                    static_cast<float>(layer == selected ? end : source.end), cursor.identity, source.asset};
            }
        }
    }
private:
    std::array<SampleWavesetsEngine, 2u> engines_;
    std::array<const WavesetMap*, 2u> maps_ {};
    std::array<unsigned, 2u> layers_ {{32u, 32u}};
    std::array<std::array<float, 32u>, 16u> scratch_ {};
    std::array<VoiceCursor, kMaximumVoices> cursors_ {};
    unsigned cursorCount_ = 0u;
};
} // namespace s3g::sample
