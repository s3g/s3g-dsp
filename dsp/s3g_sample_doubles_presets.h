#pragma once

#include "s3g_sample_doubles.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g::sample {

struct DoublesFactoryPresetInfo {
    const char* name;
    const char* description;
};

inline constexpr uint32_t kDoublesFactoryPresetCount = 5u;

inline const DoublesFactoryPresetInfo&
doublesFactoryPresetInfo(uint32_t index)
{
    static constexpr std::array<DoublesFactoryPresetInfo,
        kDoublesFactoryPresetCount> info {{
        { "RHYTHMIC SWITCHING",
            "Deck A focus with a one-beat offset and Cut crossfader." },
        { "TRAILING DOUBLE",
            "Deck A focus with a half-beat offset and Sharp crossfader." },
        { "STEPPED PHASE",
            "Centered Blend with one-eighth-beat steps inside the loop." },
        { "GRADUAL PHASE",
            "Centered Blend with a small continuous Deck B drift." },
        { "OFFSET MOTION",
            "One-beat displacement with a small continuous Deck B drift." },
    }};
    return info[std::min<uint32_t>(
        index, kDoublesFactoryPresetCount - 1u)];
}

// Factory presets establish only the two-deck performance relationship.
// Source-specific and mix-safety values in `base` intentionally survive:
// BPM, S/E, cue pre-roll, deck/output levels, and Link. The plug-in applies
// this subset without touching MIDI Receive or either deck's cue state.
inline DoublesSettings doublesFactoryPreset(
    uint32_t index, DoublesSettings base = {})
{
    index = std::min<uint32_t>(index, kDoublesFactoryPresetCount - 1u);
    base.speedSemitones = -7.0;
    base.phaseCents = 0.0;
    base.offsetBeats = 1.0;
    base.phaseStepBeats = 0.25;
    base.livePhaseBeats = 0.0;
    base.loop = false;
    base.crossfader = -1.0;
    base.crossfadeCurve = DoublesCrossfadeCurve::Cut;
    switch (index) {
    case 1u: // TRAILING DOUBLE
        base.offsetBeats = 0.5;
        base.crossfadeCurve = DoublesCrossfadeCurve::Sharp;
        break;
    case 2u: // STEPPED PHASE
        base.offsetBeats = 0.0;
        base.phaseStepBeats = 0.125;
        base.loop = true;
        base.crossfader = 0.0;
        base.crossfadeCurve = DoublesCrossfadeCurve::Blend;
        break;
    case 3u: // GRADUAL PHASE
        base.offsetBeats = 0.0;
        base.phaseCents = 0.35;
        base.loop = true;
        base.crossfader = 0.0;
        base.crossfadeCurve = DoublesCrossfadeCurve::Blend;
        break;
    case 4u: // OFFSET MOTION
        base.phaseCents = 0.35;
        base.loop = true;
        base.crossfader = 0.0;
        base.crossfadeCurve = DoublesCrossfadeCurve::Blend;
        break;
    default: // RHYTHMIC SWITCHING
        break;
    }
    return base;
}

inline int32_t doublesFactoryPresetIndex(const DoublesSettings& settings)
{
    const auto close = [](double left, double right) {
        return std::abs(left - right) <= 1.0e-9;
    };
    for (uint32_t index = 0u;
         index < kDoublesFactoryPresetCount; ++index) {
        const DoublesSettings preset = doublesFactoryPreset(index);
        if (close(settings.speedSemitones, preset.speedSemitones)
            && close(settings.phaseCents, preset.phaseCents)
            && close(settings.offsetBeats, preset.offsetBeats)
            && close(settings.phaseStepBeats, preset.phaseStepBeats)
            && close(settings.livePhaseBeats, preset.livePhaseBeats)
            && settings.loop == preset.loop
            && close(settings.crossfader, preset.crossfader)
            && settings.crossfadeCurve == preset.crossfadeCurve) {
            return static_cast<int32_t>(index);
        }
    }
    return -1;
}

} // namespace s3g::sample
