#pragma once

#include "s3g_processor_lowform.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace s3g {

struct LowformFactoryPresetInfo {
    const char* name;
    const char* description;
};

inline constexpr uint32_t kLowformFactoryPresetCount = 21u;

inline const LowformFactoryPresetInfo&
lowformFactoryPresetInfo(uint32_t index)
{
    static constexpr std::array<LowformFactoryPresetInfo,
        kLowformFactoryPresetCount> info {{
        { "INIT LOWFORM", "Balanced three-layer Processor Lowform starting point." },
        { "PURE FOUNDATION", "Phase-stable low fundamental with restrained pressure body." },
        { "MODAL MASS", "Loaded modal body above a protected sine foundation." },
        { "SWARM FLOOR", "Wide detuned body with a centered low anchor." },
        { "PRESSURE LINE", "Articulated pressure engine with pitch punch and glide." },
        { "CORRODED SUB", "Clock-tracked digital debris around a stable fundamental." },
        { "WIRE TEETH", "Wire texture, bright filtering, and controlled upper aggression." },
        { "RING CURRENT", "Ring texture animated around a compact Swarm body." },
        { "SLOW CURRENT", "Three slow modulators moving cutoff, width, and texture." },
        { "LOCKED EIGHTH", "Transport-synchronized movement for repeated bass patterns." },
        { "TUBE COLUMN", "Modal weight through the coordinated bass amplifier circuit." },
        { "PARALLEL SHRED", "Protected lows below a folded stereo Shred branch." },
        { "RAT PRESSURE", "Pressure body through the bass Rat circuit." },
        { "SOFT MAX", "Dense modern sub controlled by dynamics and maximization." },
        { "SHORT PLUCK", "Fast modal articulation with a descending filter contour." },
        { "DEEP SPACE", "Wide upper body and texture while the foundation stays mono." },
        { "ACID BURROW", "Ambi Acid-derived saw/pulse ladder over a protected foundation." },
        { "RAVE DIVIDE", "Octave-divider sub, short-delay smear, and parallel drive." },
        { "PHASE METAL", "Two-operator phase bass with feedback and bounded folding." },
        { "THROAT SHIFT", "Focused vowel resonators over a driven glottal source." },
        { "SYNC TEAR", "Hard-synchronized bass with a fast ratio sweep and ring edge." },
    }};
    return info[std::min<uint32_t>(
        index, kLowformFactoryPresetCount - 1u)];
}

inline LowformParams lowformFactoryPreset(uint32_t index)
{
    LowformParams p;
    switch (std::min<uint32_t>(
        index, kLowformFactoryPresetCount - 1u)) {
    case 1u: // PURE FOUNDATION
        p.foundationLevel = 1.01f;
        p.foundationWave = static_cast<float>(BassFoundationWave::Sine);
        p.foundationReturn = 0.94f;
        p.bodyLevel = 0.18f;
        p.bodyControlA = 0.18f;
        p.bodyControlB = 0.16f;
        p.cutoffHz = 520.0f;
        p.foundationFilter = 0.0f;
        p.filterEnvelopeOctaves = 0.4f;
        p.tube = 0.12f;
        p.maximizer = 0.16f;
        p.outputGainDb = -9.0f;
        break;
    case 2u: // MODAL MASS
        p.bodyEngine = static_cast<float>(BassBodyEngine::Modal);
        p.foundationLevel = 1.03f;
        p.bodyLevel = 0.77f;
        p.bodyControlA = 0.72f;
        p.bodyControlB = 0.44f;
        p.bodyControlC = 0.62f;
        p.bodyControlD = 0.42f;
        p.bodyControlE = 0.54f;
        p.bodyWidth = 0.39f;
        p.textureWidth = 0.39f;
        p.cutoffHz = 680.0f;
        p.filterEnvelopeOctaves = 1.1f;
        p.filterDecaySeconds = 0.46f;
        p.releaseSeconds = 0.72f;
        p.tube = 0.34f;
        p.outputGainDb = -14.0f;
        break;
    case 3u: // SWARM FLOOR
        p.bodyEngine = static_cast<float>(BassBodyEngine::Swarm);
        p.bodyLevel = 0.76f;
        p.bodyControlA = 0.58f;
        p.bodyControlB = 0.66f;
        p.bodyControlC = 0.74f;
        p.bodyControlD = 0.34f;
        p.bodyControlE = 0.68f;
        p.bodyWidth = 0.94f;
        p.textureWidth = 0.94f;
        p.cutoffHz = 940.0f;
        p.resonance = 0.12f;
        p.foundationReturn = 0.84f;
        p.outputGainDb = -16.0f;
        break;
    case 4u: // PRESSURE LINE
        p.voiceMode = static_cast<float>(BassVoiceMode::Mono);
        p.glideMs = 148.0f;
        p.pitchPunchSemitones = 7.0f;
        p.punchTimeMs = 42.0f;
        p.bodyControlA = 0.64f;
        p.bodyControlB = 0.74f;
        p.bodyControlC = 0.82f;
        p.bodyControlD = 0.48f;
        p.bodyControlE = 0.83f;
        p.cutoffHz = 760.0f;
        p.filterEnvelopeOctaves = 2.2f;
        p.filterDecaySeconds = 0.14f;
        p.outputGainDb = -14.0f;
        break;
    case 5u: // CORRODED SUB
        p.textureMode = static_cast<float>(BassTextureMode::Corrode);
        p.textureLevel = 0.46f;
        p.textureColor = 0.34f;
        p.textureTrack = 0.92f;
        p.bodyLevel = 0.48f;
        p.cutoffHz = 1250.0f;
        p.textureFilter = 0.74f;
        p.mods[2] = { static_cast<float>(BassModShape::Random), 0.58f,
            0.34f, static_cast<float>(BassModTarget::TextureColor) };
        p.outputGainDb = -15.0f;
        break;
    case 6u: // WIRE TEETH
        p.textureMode = static_cast<float>(BassTextureMode::Wire);
        p.textureLevel = 0.52f;
        p.textureColor = 0.72f;
        p.bodyControlA = 0.68f;
        p.bodyControlB = 0.64f;
        p.bodyControlC = 0.62f;
        p.bodyControlD = 0.70f;
        p.bodyControlE = 0.70f;
        p.cutoffHz = 1800.0f;
        p.filterDrive = 0.76f;
        p.modalDrive = 0.64f;
        p.dynamicsBite = 0.08f;
        p.shred = 0.15f;
        p.dynamics = 0.42f;
        p.outputGainDb = -16.0f;
        break;
    case 7u: // RING CURRENT
        p.bodyEngine = static_cast<float>(BassBodyEngine::Swarm);
        p.bodyLevel = 0.62f;
        p.bodyWidth = 0.65f;
        p.textureWidth = 0.65f;
        p.textureMode = static_cast<float>(BassTextureMode::Ring);
        p.textureLevel = 0.34f;
        p.textureColor = 0.42f;
        p.cutoffHz = 1120.0f;
        p.mods[0] = { static_cast<float>(BassModShape::Sine), 0.31f,
            0.28f, static_cast<float>(BassModTarget::TextureLevel) };
        p.outputGainDb = -15.0f;
        break;
    case 8u: // SLOW CURRENT
        p.textureMode = static_cast<float>(BassTextureMode::Noise);
        p.textureLevel = 0.18f;
        p.mods[0] = { static_cast<float>(BassModShape::Sine), 0.22f,
            0.46f, static_cast<float>(BassModTarget::Cutoff) };
        p.mods[1] = { static_cast<float>(BassModShape::Triangle), 0.16f,
            0.53f, static_cast<float>(BassModTarget::Width) };
        p.mods[2] = { static_cast<float>(BassModShape::Random), 0.11f,
            0.30f, static_cast<float>(BassModTarget::TextureLevel) };
        p.bodyWidth = 0.57f;
        p.textureWidth = 0.57f;
        p.outputGainDb = -14.0f;
        break;
    case 9u: // LOCKED EIGHTH
        p.motionClock = static_cast<float>(BassMotionClock::Transport);
        p.mods[0] = { static_cast<float>(BassModShape::Steps), 8.0f / 15.0f,
            0.48f, static_cast<float>(BassModTarget::Cutoff) };
        p.mods[1] = { static_cast<float>(BassModShape::Square), 8.0f / 15.0f,
            0.22f, static_cast<float>(BassModTarget::Amp) };
        p.mods[2] = { static_cast<float>(BassModShape::Triangle), 6.0f / 15.0f,
            0.22f, static_cast<float>(BassModTarget::BodyMorph) };
        p.cutoffHz = 620.0f;
        p.filterEnvelopeOctaves = 0.7f;
        p.outputGainDb = -14.0f;
        break;
    case 10u: // TUBE COLUMN
        p.bodyEngine = static_cast<float>(BassBodyEngine::Modal);
        p.bodyLevel = 0.72f;
        p.bodyControlA = 0.54f;
        p.bodyControlB = 0.38f;
        p.bodyControlC = 0.68f;
        p.bodyControlD = 0.36f;
        p.bodyControlE = 0.56f;
        p.tube = 0.68f;
        p.dynamics = 0.36f;
        p.saturation = 0.22f;
        p.foundationReturn = 0.82f;
        p.outputGainDb = -16.0f;
        break;
    case 11u: // PARALLEL SHRED
        p.bodyEngine = static_cast<float>(BassBodyEngine::Swarm);
        p.bodyLevel = 0.70f;
        p.bodyWidth = 0.65f;
        p.textureWidth = 0.65f;
        p.shred = 0.68f;
        p.shredFeedback = 0.18f;
        p.shredMix = 0.52f;
        p.shredColor = 0.44f;
        p.foundationReturn = 0.90f;
        p.outputGainDb = -18.0f;
        break;
    case 12u: // RAT PRESSURE
        p.bodyControlA = 0.72f;
        p.bodyControlB = 0.70f;
        p.bodyControlC = 0.72f;
        p.bodyControlD = 0.58f;
        p.bodyControlE = 0.86f;
        p.shredCircuit = static_cast<float>(BassShredCircuit::Rat);
        p.shred = 0.70f;
        p.shredFeedback = 0.08f;
        p.shredMix = 0.46f;
        p.shredColor = 0.38f;
        p.filterDrive = 0.47f;
        p.modalDrive = 0.58f;
        p.dynamicsBite = 0.07f;
        p.outputGainDb = -17.0f;
        break;
    case 13u: // SOFT MAX
        p.bodyEngine = static_cast<float>(BassBodyEngine::Swarm);
        p.bodyLevel = 0.68f;
        p.bodyControlA = 0.44f;
        p.bodyControlB = 0.48f;
        p.bodyControlC = 0.58f;
        p.bodyControlD = 0.62f;
        p.bodyControlE = 0.54f;
        p.cutoffHz = 880.0f;
        p.tube = 0.28f;
        p.dynamics = 0.62f;
        p.saturation = 0.28f;
        p.maximizer = 0.66f;
        p.outputGainDb = -16.0f;
        break;
    case 14u: // SHORT PLUCK
        p.bodyEngine = static_cast<float>(BassBodyEngine::Modal);
        p.bodyControlA = 0.48f;
        p.bodyControlB = 0.72f;
        p.bodyControlC = 0.24f;
        p.bodyControlD = 0.04f;
        p.bodyControlE = 0.76f;
        p.attackSeconds = 0.0008f;
        p.decaySeconds = 0.075f;
        p.sustain = 0.18f;
        p.releaseSeconds = 0.09f;
        p.pitchPunchSemitones = 4.0f;
        p.punchTimeMs = 26.0f;
        p.filterEnvelopeOctaves = 3.4f;
        p.filterDecaySeconds = 0.07f;
        p.cutoffHz = 380.0f;
        p.outputGainDb = -12.0f;
        break;
    case 15u: // DEEP SPACE
        p.bodyEngine = static_cast<float>(BassBodyEngine::Modal);
        p.bodyLevel = 0.78f;
        p.bodyControlA = 0.62f;
        p.bodyControlB = 0.62f;
        p.bodyControlC = 0.76f;
        p.bodyControlD = 0.46f;
        p.bodyControlE = 0.42f;
        p.bodyWidth = 1.0f;
        p.textureWidth = 1.0f;
        p.textureMode = static_cast<float>(BassTextureMode::Noise);
        p.textureLevel = 0.20f;
        p.textureColor = 0.28f;
        p.foundationReturn = 0.92f;
        p.releaseSeconds = 1.1f;
        p.outputGainDb = -17.0f;
        break;
    case 16u: // ACID BURROW
        p.voiceMode = static_cast<float>(BassVoiceMode::Mono);
        p.glideMs = 82.0f;
        p.foundationLevel = 0.69f;
        p.foundationReturn = 0.90f;
        p.bodyEngine = static_cast<float>(BassBodyEngine::Acid);
        p.bodyLevel = 0.82f;
        p.bodyControlA = 0.16f;
        p.bodyControlB = 0.78f;
        p.bodyControlC = 0.46f;
        p.bodyControlD = 0.91f;
        p.bodyControlE = 0.50f;
        p.bodyWidth = 0.31f;
        p.textureWidth = 0.31f;
        p.filterType = static_cast<float>(BassFilterType::Low12);
        p.cutoffHz = 310.0f;
        p.resonance = 0.32f;
        p.filterDrive = 0.65f;
        p.keyTrack = 0.18f;
        p.bodyFilter = 0.18f;
        p.filterEnvelopeOctaves = 3.25f;
        p.filterDecaySeconds = 0.185f;
        p.modalDrive = 0.38f;
        p.dynamicsBite = 0.05f;
        p.shred = 0.09f;
        p.tube = 0.18f;
        p.saturation = 0.16f;
        p.outputGainDb = -16.0f;
        break;
    case 17u: // RAVE DIVIDE
        p.voiceMode = static_cast<float>(BassVoiceMode::Mono);
        p.glideMs = 68.0f;
        p.foundationLevel = 0.52f;
        p.foundationReturn = 0.92f;
        p.bodyEngine = static_cast<float>(BassBodyEngine::Rave);
        p.bodyLevel = 0.86f;
        p.bodyControlA = 0.58f;
        p.bodyControlB = 0.76f;
        p.bodyControlC = 0.62f;
        p.bodyControlD = 0.10f;
        p.bodyControlE = 0.72f;
        p.bodyWidth = 0.68f;
        p.filterType = static_cast<float>(BassFilterType::Low24);
        p.cutoffHz = 460.0f;
        p.resonance = 0.24f;
        p.filterDrive = 0.52f;
        p.keyTrack = 0.32f;
        p.foundationFilter = 0.06f;
        p.bodyFilter = 1.0f;
        p.filterEnvelopeOctaves = 2.7f;
        p.filterDecaySeconds = 0.16f;
        p.attackSeconds = 0.0015f;
        p.decaySeconds = 0.20f;
        p.sustain = 0.72f;
        p.releaseSeconds = 0.18f;
        p.tube = 0.16f;
        p.saturation = 0.10f;
        p.outputGainDb = -17.0f;
        break;
    case 18u: // PHASE METAL
        p.foundationLevel = 0.66f;
        p.foundationReturn = 0.90f;
        p.bodyEngine = static_cast<float>(BassBodyEngine::Phase);
        p.bodyLevel = 0.80f;
        p.bodyControlA = 0.28f;
        p.bodyControlB = 0.46f;
        p.bodyControlC = 0.58f;
        p.bodyControlD = 0.32f;
        p.bodyControlE = 0.44f;
        p.bodyWidth = 0.42f;
        p.filterType = static_cast<float>(BassFilterType::Low24);
        p.cutoffHz = 1080.0f;
        p.resonance = 0.15f;
        p.filterDrive = 0.24f;
        p.filterEnvelopeOctaves = 1.6f;
        p.filterDecaySeconds = 0.22f;
        p.mods[0] = { static_cast<float>(BassModShape::Triangle), 0.34f,
            0.18f, static_cast<float>(BassModTarget::BodyControlC) };
        p.outputGainDb = -16.0f;
        break;
    case 19u: // THROAT SHIFT
        p.foundationLevel = 0.74f;
        p.foundationReturn = 0.94f;
        p.bodyEngine = static_cast<float>(BassBodyEngine::Throat);
        p.bodyLevel = 0.88f;
        p.bodyControlA = 0.28f;
        p.bodyControlB = 0.50f;
        p.bodyControlC = 0.74f;
        p.bodyControlD = 0.36f;
        p.bodyControlE = 0.43f;
        p.bodyWidth = 0.58f;
        p.filterType = static_cast<float>(BassFilterType::Low12);
        p.cutoffHz = 4200.0f;
        p.resonance = 0.08f;
        p.filterDrive = 0.16f;
        p.bodyFilter = 0.42f;
        p.filterEnvelopeOctaves = 0.55f;
        p.filterDecaySeconds = 0.30f;
        p.mods[0] = { static_cast<float>(BassModShape::Sine), 0.29f,
            0.24f, static_cast<float>(BassModTarget::BodyMorph) };
        p.outputGainDb = -17.0f;
        break;
    case 20u: // SYNC TEAR
        p.voiceMode = static_cast<float>(BassVoiceMode::Mono);
        p.glideMs = 52.0f;
        p.foundationLevel = 0.68f;
        p.foundationReturn = 0.92f;
        p.bodyEngine = static_cast<float>(BassBodyEngine::Sync);
        p.bodyLevel = 0.84f;
        p.bodyControlA = 0.62f;
        p.bodyControlB = 0.36f;
        p.bodyControlC = 0.68f;
        p.bodyControlD = 0.38f;
        p.bodyControlE = 0.24f;
        p.bodyWidth = 0.34f;
        p.filterType = static_cast<float>(BassFilterType::Low24);
        p.cutoffHz = 720.0f;
        p.resonance = 0.20f;
        p.filterDrive = 0.38f;
        p.filterEnvelopeOctaves = 2.1f;
        p.filterDecaySeconds = 0.15f;
        p.attackSeconds = 0.001f;
        p.decaySeconds = 0.18f;
        p.sustain = 0.76f;
        p.releaseSeconds = 0.16f;
        p.outputGainDb = -16.0f;
        break;
    case 0u:
    default:
        break;
    }
    return p;
}

} // namespace s3g
