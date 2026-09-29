#pragma once
#include "s3g_sample_player.h"

namespace s3g::sample {

// Drawing math shared by the editor and regressions. Coordinates are source
// positions: zoom/crop transforms are applied only after evaluating the voice.
inline float neonCursorEnvelope(const VoiceCursor& cursor, float phase) noexcept {
    phase = std::clamp(phase, 0.f, 1.f);
    if (cursor.window <= 4)
        return grainWindow(static_cast<GrainEnvelope>(cursor.window), phase, cursor.windowSkew);
    if (cursor.attack > 0 && phase < cursor.attack) return phase / cursor.attack;
    if (cursor.decay > 0 && phase < cursor.attack + cursor.decay)
        return 1 + (cursor.sustain - 1) * (phase - cursor.attack) / cursor.decay;
    if (cursor.release > 0 && phase > 1 - cursor.release)
        return cursor.sustain * (1 - phase) / cursor.release;
    return cursor.sustain;
}
inline double neonCursorSource(const VoiceCursor& cursor, double phase) noexcept {
    const double travel = cursor.reverse ? 1 - phase : phase;
    return cursor.sourceStartNormalized + travel
        * (cursor.sourceEndNormalized - cursor.sourceStartNormalized);
}
struct NeonLaneViewLayout {
    unsigned rows = 0;
    double pitch = 0, height = 0;
    double top(unsigned layer) const noexcept { return layer * pitch; }
};
inline NeonLaneViewLayout neonLaneViewLayout(unsigned count, double height) noexcept {
    const auto rows = std::min(32u, count);
    if (!rows || !std::isfinite(height) || height <= 0) return {};
    const double pitch = height / rows;
    // Keep a readable 14px row at 32 layers in the fixed 510px waveform.
    // Geometry never depends on playback position: no scrolling or paging.
    const double gap = std::min(rows > 16 ? 1.0 : 3.0, pitch * .1);
    return {rows, pitch, pitch - gap};
}
}
