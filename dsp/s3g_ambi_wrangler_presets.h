#pragma once

#include "s3g_ambi_wrangler_encoder.h"

#include <array>
#include <cstdint>

namespace s3g {

struct AmbiWranglerPresetInfo {
    const char* name;
    const char* description;
};

inline constexpr uint32_t kAmbiWranglerFactoryPresetCount = 8;

inline AmbiWranglerPresetInfo ambiWranglerFactoryPresetInfo(uint32_t index)
{
    static constexpr std::array<AmbiWranglerPresetInfo, kAmbiWranglerFactoryPresetCount> kInfo {{
        { "Backwash Logic", "Medium chaotic Benjolin cloud with filtered bloom." },
        { "Glass Register", "Bright small-register points with high spatial separation." },
        { "Low Rope", "Slow low swarm with strong rungler pitch pull." },
        { "Square Weather", "Harder comparator edge with wide moving topology." },
        { "Filter Bloom", "Filter-forward voice field with softer oscillator body." },
        { "Narrow Organism", "Compact center field with coupled oscillator beating." },
        { "Upper Sparks", "Thin high logic pulses distributed above the listener." },
        { "Fault Lattice", "Dense unstable register lattice with clipped drive." },
    }};
    return kInfo[std::min<uint32_t>(index, kAmbiWranglerFactoryPresetCount - 1u)];
}

inline AmbiWranglerParams ambiWranglerFactoryPreset(uint32_t index)
{
    AmbiWranglerParams p {};
    switch (std::min<uint32_t>(index, kAmbiWranglerFactoryPresetCount - 1u)) {
    case 1:
        p.voices = 24; p.baseNote = 52.0f; p.spreadSemitones = 31.0f; p.chaos = 0.42f; p.cross = 0.18f;
        p.rung = 0.62f; p.rungSize = 3; p.threshold = 0.56f; p.color = 0.18f; p.filter = 0.62f;
        p.resonance = 0.38f; p.saturation = 0.18f; p.topologyShape = 14; p.topologyMotion = 2; p.topologyDepth = 0.62f;
        p.centerElevationDeg = 16.0f; p.outputGainDb = -28.0f;
        break;
    case 2:
        p.voices = 18; p.baseNote = 22.0f; p.spreadSemitones = 15.0f; p.chaos = 0.82f; p.cross = 0.34f;
        p.rung = 0.88f; p.rungSize = 5; p.threshold = 0.48f; p.color = 0.66f; p.filter = 0.22f;
        p.resonance = 0.26f; p.saturation = 0.52f; p.topologyShape = 8; p.topologyMotion = 1; p.topologyRateHz = 0.018f;
        p.topologyAmount = 0.66f; p.topologyDepth = 0.48f; p.centerDistance = 1.25f; p.outputGainDb = -25.0f;
        break;
    case 3:
        p.voices = 32; p.baseNote = 39.0f; p.spreadSemitones = 28.0f; p.chaos = 0.72f; p.cross = 0.56f;
        p.rung = 0.78f; p.rungSize = 4; p.threshold = 0.42f; p.color = 0.10f; p.filter = 0.30f;
        p.resonance = 0.12f; p.saturation = 0.64f; p.topologyShape = 19; p.topologyMotion = 3; p.topologyRateHz = 0.045f;
        p.topologyAmount = 0.92f; p.topologyDepth = 0.86f; p.outputGainDb = -29.0f;
        break;
    case 4:
        p.voices = 20; p.baseNote = 34.0f; p.spreadSemitones = 20.0f; p.chaos = 0.46f; p.cross = 0.22f;
        p.rung = 0.58f; p.rungSize = 6; p.threshold = 0.52f; p.color = 0.86f; p.filter = 0.46f;
        p.resonance = 0.54f; p.saturation = 0.28f; p.topologyShape = 11; p.topologyMotion = 2; p.topologyRateHz = 0.024f;
        p.centerDistance = 0.9f; p.outputGainDb = -24.0f;
        break;
    case 5:
        p.voices = 12; p.baseNote = 31.0f; p.spreadSemitones = 9.0f; p.chaos = 0.36f; p.cross = 0.48f;
        p.rung = 0.44f; p.rungSize = 4; p.threshold = 0.50f; p.color = 0.50f; p.filter = 0.38f;
        p.resonance = 0.32f; p.saturation = 0.20f; p.topologyShape = 3; p.topologyMotion = 1; p.topologyAmount = 0.32f;
        p.topologyDepth = 0.18f; p.topologyScale = 0.62f; p.spatialFollow = 0.96f; p.outputGainDb = -22.0f;
        break;
    case 6:
        p.voices = 28; p.baseNote = 60.0f; p.spreadSemitones = 36.0f; p.chaos = 0.64f; p.cross = 0.12f;
        p.rung = 0.92f; p.rungSize = 2; p.threshold = 0.63f; p.color = 0.04f; p.filter = 0.72f;
        p.resonance = 0.18f; p.saturation = 0.30f; p.topologyShape = 22; p.topologyMotion = 4; p.topologyRateHz = 0.072f;
        p.centerElevationDeg = 34.0f; p.outputGainDb = -32.0f;
        break;
    case 7:
        p.voices = 48; p.baseNote = 36.0f; p.spreadSemitones = 42.0f; p.chaos = 0.94f; p.cross = 0.62f;
        p.rung = 0.96f; p.rungSize = 7; p.threshold = 0.45f; p.color = 0.36f; p.filter = 0.50f;
        p.resonance = 0.46f; p.saturation = 0.72f; p.topologyShape = 27; p.topologyMotion = 4; p.topologyRateHz = 0.056f;
        p.topologyDepth = 0.94f; p.topologyCollapse = 0.22f; p.outputGainDb = -34.0f;
        break;
    case 0:
    default:
        break;
    }
    return p;
}

} // namespace s3g
