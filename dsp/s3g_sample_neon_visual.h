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
inline unsigned neonLaneViewFirst(unsigned count, float position) noexcept {
    const unsigned center = static_cast<unsigned>(std::lround(
        std::clamp(position, 0.f, 1.f) * (std::max(1u, count) - 1u)));
    return std::min(count > 8 ? count - 8 : 0u, center > 3 ? center - 3 : 0u);
}
}
