#pragma once

#include "s3g_low_frequency_synth.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

struct LowFrequencySynthFactoryPresetInfo {
    const char* name;
    const char* description;
};

inline constexpr uint32_t kLowFrequencySynthFactoryPresetCount = 16u;

inline const LowFrequencySynthFactoryPresetInfo&
lowFrequencySynthFactoryPresetInfo(uint32_t index)
{
    static constexpr std::array<LowFrequencySynthFactoryPresetInfo,
        kLowFrequencySynthFactoryPresetCount> info {{
        { "REFERENCE", "Thick modal-sub reference for the direct surface." },
        { "PURE SUB", "The first membrane mode with a restrained surface trace." },
        { "MASS LOADED", "Heavy radial loading and a long, dense modal body." },
        { "COUPLED BODY", "Strong interaction between the paired membrane modes." },
        { "DARK MEMBRANE", "Low cutoff and soft excitation around a full sub core." },
        { "LONG MEMBRANE", "Low damping and an extended release for sustained weight." },
        { "SHORT PLUCK", "Compact modal articulation with a fast amplitude contour." },
        { "GLIDE LINE", "Long portamento through a stable, restrained tone contour." },
        { "PITCH IMPULSE", "Brief positive pitch displacement at each onset." },
        { "SLOW AM", "Slow free-running sinusoidal amplitude modulation." },
        { "QUARTER AM", "Transport-locked quarter-note amplitude modulation." },
        { "EIGHTH AM", "Transport-locked eighth-note amplitude modulation." },
        { "FREE AM", "Shallow free-running sinusoidal amplitude modulation." },
        { "PARALLEL SHRED", "Bass-tuned folded upper branch over the protected low path." },
        { "TUBE WEIGHT", "Coordinated valve, power-stage, sag, and cabinet weight." },
        { "REGENERATED BASS", "Dense membrane weight with governed stereo feedback." },
    }};
    return info[std::min<uint32_t>(
        index, kLowFrequencySynthFactoryPresetCount - 1u)];
}

// Presets are reconstructed from the exact public surface. Engineering
// details that are no longer controls remain at the single canonical engine
// setting used by every new instance.
inline LowFrequencySynthParams lowFrequencySynthFromExposedControls(
    const LowFrequencySynthParams& exposed)
{
    LowFrequencySynthParams p;
    p.outputGainDb = exposed.outputGainDb;
    p.fundamental = exposed.fundamental;
    p.body = exposed.body;
    p.loading = exposed.loading;
    p.coupling = exposed.coupling;
    p.damping = exposed.damping;
    p.excitationPosition = exposed.excitationPosition;
    p.upperModeLevel = exposed.upperModeLevel;
    p.filterCutoffHz = exposed.filterCutoffHz;
    p.filterResonance = exposed.filterResonance;
    p.membraneDrive = exposed.membraneDrive;
    p.processedMix = exposed.processedMix;
    p.valvePreamp = exposed.valvePreamp;
    p.attackSeconds = exposed.attackSeconds;
    p.decaySeconds = exposed.decaySeconds;
    p.sustain = exposed.sustain;
    p.releaseSeconds = exposed.releaseSeconds;
    p.transposeSemitones = exposed.transposeSemitones;
    p.glideMs = exposed.glideMs;
    p.pitchTransientSemitones = exposed.pitchTransientSemitones;
    p.amplitudeMotionClock = exposed.amplitudeMotionClock;
    p.amplitudeMotionDivision = exposed.amplitudeMotionDivision;
    p.amplitudeMotionRateHz = exposed.amplitudeMotionRateHz;
    p.amplitudeMotionDepth = exposed.amplitudeMotionDepth;
    p.amplitudeMotionPosition = exposed.amplitudeMotionPosition;
    p.shred = exposed.shred;
    p.shredFeedback = exposed.shredFeedback;
    p.shredFeedbackToneLevel = exposed.shredFeedbackToneLevel;
    p.shredCircuit = exposed.shredCircuit;
    p.shredColor = exposed.shredColor;
    p.shredMix = exposed.shredMix;
    return p;
}

inline LowFrequencySynthParams lowFrequencySynthFactoryPreset(uint32_t index)
{
    LowFrequencySynthParams p;
    switch (std::min<uint32_t>(
        index, kLowFrequencySynthFactoryPresetCount - 1u)) {
    case 1u: // PURE SUB
        p.fundamental = 1.0f;
        p.body = 0.12f;
        p.loading = 0.72f;
        p.coupling = 0.18f;
        p.damping = 0.68f;
        p.excitationPosition = 0.18f;
        p.upperModeLevel = 0.08f;
        p.processedMix = 0.10f;
        p.membraneDrive = 0.08f;
        p.valvePreamp = 0.18f;
        p.outputGainDb = -7.0f;
        break;
    case 2u: // MASS LOADED
        p.fundamental = 0.96f;
        p.body = 0.86f;
        p.loading = 1.0f;
        p.coupling = 0.52f;
        p.damping = 0.20f;
        p.excitationPosition = 0.22f;
        p.upperModeLevel = 0.20f;
        p.filterCutoffHz = 720.0f;
        p.membraneDrive = 0.24f;
        p.processedMix = 0.42f;
        p.valvePreamp = 0.46f;
        p.releaseSeconds = 0.62f;
        p.outputGainDb = -10.0f;
        break;
    case 3u: // COUPLED BODY
        p.fundamental = 0.92f;
        p.body = 0.90f;
        p.loading = 0.76f;
        p.coupling = 0.92f;
        p.damping = 0.30f;
        p.excitationPosition = 0.42f;
        p.upperModeLevel = 0.24f;
        p.filterCutoffHz = 1100.0f;
        p.processedMix = 0.50f;
        p.outputGainDb = -11.0f;
        break;
    case 4u: // DARK MEMBRANE
        p.fundamental = 0.98f;
        p.body = 0.76f;
        p.loading = 0.90f;
        p.coupling = 0.46f;
        p.damping = 0.34f;
        p.excitationPosition = 0.12f;
        p.upperModeLevel = 0.14f;
        p.filterCutoffHz = 260.0f;
        p.filterResonance = 0.16f;
        p.processedMix = 0.54f;
        p.valvePreamp = 0.52f;
        p.outputGainDb = -10.0f;
        break;
    case 5u: // LONG MEMBRANE
        p.body = 0.88f;
        p.loading = 0.86f;
        p.coupling = 0.62f;
        p.damping = 0.08f;
        p.excitationPosition = 0.28f;
        p.releaseSeconds = 1.80f;
        p.filterCutoffHz = 760.0f;
        p.outputGainDb = -12.0f;
        break;
    case 6u: // SHORT PLUCK
        p.fundamental = 0.90f;
        p.body = 0.68f;
        p.loading = 0.64f;
        p.coupling = 0.56f;
        p.damping = 0.76f;
        p.excitationPosition = 0.58f;
        p.attackSeconds = 0.001f;
        p.decaySeconds = 0.075f;
        p.sustain = 0.24f;
        p.releaseSeconds = 0.08f;
        p.pitchTransientSemitones = 3.0f;
        p.upperModeLevel = 0.26f;
        p.filterCutoffHz = 720.0f;
        p.filterResonance = 0.22f;
        p.membraneDrive = 0.22f;
        p.processedMix = 0.46f;
        p.outputGainDb = -11.0f;
        break;
    case 7u: // GLIDE LINE
        p.glideMs = 230.0f;
        p.body = 0.74f;
        p.loading = 0.88f;
        p.upperModeLevel = 0.20f;
        p.filterCutoffHz = 640.0f;
        p.filterResonance = 0.16f;
        p.processedMix = 0.38f;
        p.outputGainDb = -10.0f;
        break;
    case 8u: // PITCH IMPULSE
        p.attackSeconds = 0.001f;
        p.pitchTransientSemitones = 8.0f;
        p.body = 0.80f;
        p.damping = 0.34f;
        p.upperModeLevel = 0.24f;
        p.filterCutoffHz = 850.0f;
        p.processedMix = 0.40f;
        p.outputGainDb = -11.0f;
        break;
    case 9u: // SLOW AM
        p.fundamental = 0.94f;
        p.body = 0.78f;
        p.upperModeLevel = 0.20f;
        p.filterCutoffHz = 720.0f;
        p.filterResonance = 0.18f;
        p.membraneDrive = 0.20f;
        p.processedMix = 0.38f;
        p.amplitudeMotionClock = static_cast<float>(MotionClock::Free);
        p.amplitudeMotionRateHz = 0.42f;
        p.amplitudeMotionDepth = 0.78f;
        p.outputGainDb = -11.0f;
        break;
    case 10u: // QUARTER AM
        p.filterCutoffHz = 800.0f;
        p.filterResonance = 0.18f;
        p.upperModeLevel = 0.20f;
        p.membraneDrive = 0.20f;
        p.processedMix = 0.40f;
        p.amplitudeMotionClock = static_cast<float>(MotionClock::Transport);
        p.amplitudeMotionDivision = 7.0f;
        p.amplitudeMotionDepth = 0.68f;
        p.outputGainDb = -11.0f;
        break;
    case 11u: // EIGHTH AM
        p.filterCutoffHz = 950.0f;
        p.filterResonance = 0.22f;
        p.upperModeLevel = 0.24f;
        p.membraneDrive = 0.24f;
        p.processedMix = 0.44f;
        p.amplitudeMotionClock = static_cast<float>(MotionClock::Transport);
        p.amplitudeMotionDivision = 10.0f;
        p.amplitudeMotionDepth = 0.78f;
        p.outputGainDb = -12.0f;
        break;
    case 12u: // FREE AM
        p.filterCutoffHz = 700.0f;
        p.filterResonance = 0.16f;
        p.upperModeLevel = 0.18f;
        p.processedMix = 0.36f;
        p.amplitudeMotionClock = static_cast<float>(MotionClock::Free);
        p.amplitudeMotionRateHz = 0.32f;
        p.amplitudeMotionDepth = 0.58f;
        p.outputGainDb = -10.0f;
        break;
    case 13u: // PARALLEL SHRED
        p.fundamental = 0.96f;
        p.body = 0.84f;
        p.loading = 0.86f;
        p.coupling = 0.58f;
        p.upperModeLevel = 0.32f;
        p.filterCutoffHz = 820.0f;
        p.filterResonance = 0.18f;
        p.membraneDrive = 0.42f;
        p.processedMix = 0.48f;
        p.valvePreamp = 0.42f;
        p.shred = 0.62f;
        p.shredFeedback = 0.18f;
        p.shredCircuit = static_cast<float>(BassShredCircuit::Rat);
        p.shredColor = 0.42f;
        p.shredMix = 0.48f;
        p.outputGainDb = -15.0f;
        break;
    case 14u: // TUBE WEIGHT
        p.fundamental = 0.98f;
        p.body = 0.76f;
        p.loading = 0.90f;
        p.upperModeLevel = 0.18f;
        p.membraneDrive = 0.18f;
        p.processedMix = 0.34f;
        p.valvePreamp = 0.86f;
        p.outputGainDb = -14.0f;
        break;
    case 15u: // REGENERATED BASS
        p.fundamental = 1.0f;
        p.body = 1.0f;
        p.loading = 1.0f;
        p.coupling = 0.78f;
        p.damping = 0.14f;
        p.excitationPosition = 0.34f;
        p.upperModeLevel = 0.36f;
        p.filterCutoffHz = 900.0f;
        p.filterResonance = 0.24f;
        p.membraneDrive = 0.56f;
        p.processedMix = 0.54f;
        p.valvePreamp = 0.62f;
        p.shred = 0.74f;
        p.shredFeedback = 0.58f;
        p.shredFeedbackToneLevel = 0.82f;
        p.shredCircuit = static_cast<float>(BassShredCircuit::ZoneB);
        p.shredColor = 0.54f;
        p.shredMix = 0.62f;
        p.outputGainDb = -18.0f;
        break;
    case 0u:
    default:
        break;
    }
    return lowFrequencySynthFromExposedControls(p);
}

inline bool lowFrequencySynthParamsEqual(const LowFrequencySynthParams& a,
    const LowFrequencySynthParams& b, float tolerance = 1.0e-6f)
{
    const auto close = [tolerance](float x, float y) {
        return std::fabs(x - y) <= tolerance;
    };
    return close(a.outputGainDb, b.outputGainDb)
        && close(a.fundamental, b.fundamental)
        && close(a.body, b.body)
        && close(a.loading, b.loading)
        && close(a.coupling, b.coupling)
        && close(a.damping, b.damping)
        && close(a.excitationPosition, b.excitationPosition)
        && close(a.upperModeLevel, b.upperModeLevel)
        && close(a.filterCutoffHz, b.filterCutoffHz)
        && close(a.filterResonance, b.filterResonance)
        && close(a.membraneDrive, b.membraneDrive)
        && close(a.processedMix, b.processedMix)
        && close(a.valvePreamp, b.valvePreamp)
        && close(a.attackSeconds, b.attackSeconds)
        && close(a.decaySeconds, b.decaySeconds)
        && close(a.sustain, b.sustain)
        && close(a.releaseSeconds, b.releaseSeconds)
        && close(a.transposeSemitones, b.transposeSemitones)
        && close(a.glideMs, b.glideMs)
        && close(a.pitchTransientSemitones, b.pitchTransientSemitones)
        && close(a.amplitudeMotionClock, b.amplitudeMotionClock)
        && close(a.amplitudeMotionDivision, b.amplitudeMotionDivision)
        && close(a.amplitudeMotionRateHz, b.amplitudeMotionRateHz)
        && close(a.amplitudeMotionDepth, b.amplitudeMotionDepth)
        && close(a.amplitudeMotionPosition, b.amplitudeMotionPosition)
        && close(a.shred, b.shred)
        && close(a.shredFeedback, b.shredFeedback)
        && close(a.shredFeedbackToneLevel, b.shredFeedbackToneLevel)
        && close(a.shredCircuit, b.shredCircuit)
        && close(a.shredColor, b.shredColor)
        && close(a.shredMix, b.shredMix);
}

inline int32_t lowFrequencySynthFactoryPresetIndex(
    const LowFrequencySynthParams& params)
{
    for (uint32_t index = 0u;
         index < kLowFrequencySynthFactoryPresetCount; ++index) {
        if (lowFrequencySynthParamsEqual(
                params, lowFrequencySynthFactoryPreset(index))) {
            return static_cast<int32_t>(index);
        }
    }
    return -1;
}

} // namespace s3g
