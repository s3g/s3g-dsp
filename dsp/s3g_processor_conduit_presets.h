#pragma once

#include "s3g_processor_conduit.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace s3g {

struct ProcessorConduitFactoryPresetInfo {
    const char* name;
    const char* description;
};

inline constexpr uint32_t kProcessorConduitFactoryPresetCount = 10u;

inline const ProcessorConduitFactoryPresetInfo&
processorConduitFactoryPresetInfo(uint32_t index)
{
    static constexpr std::array<ProcessorConduitFactoryPresetInfo,
        kProcessorConduitFactoryPresetCount> info {{
        { "OPEN METAL", "Balanced metal sheet with a pedal before the driver." },
        { "DIRECT PA", "Truthful direct coupling feeding a driven PA return." },
        { "PIEZO SHRED", "Shred after the piezo so body transients hit the pedal." },
        { "RETURN RAT", "Rat distortion on the moving-mic return inside the loop." },
        { "THROWN BRONZE", "Tiered bronze with violent mic motion and breakup." },
        { "PORCELAIN ROOM", "Bright porcelain resonances inside a compact chamber." },
        { "STEEL HOWL", "A steel shell poised near a governed sustaining howl." },
        { "SUBMERGED OCT", "Water transmission and a dragged octave-down voice." },
        { "SLOWED SKIN", "Low, smeared membrane speech with a soft post-piezo fuzz." },
        { "BROKEN PA", "Direct vocal return through a fractured post-mic diode path." },
    }};
    return info[std::min<uint32_t>(
        index, kProcessorConduitFactoryPresetCount - 1u)];
}

// Presets and RANDOM preserve the user's audition gain staging exactly. Sonic
// controls may change apparent density and distortion, but INPUT, main MIX,
// and OUT are never reassigned by either operation.
inline ProcessorConduitParams processorConduitPreserveAudition(
    ProcessorConduitParams sonic,
    const ProcessorConduitParams& audition)
{
    sonic.inputGainDb = audition.inputGainDb;
    sonic.mix = audition.mix;
    sonic.outputGainDb = audition.outputGainDb;
    sonic.inputListen = audition.inputListen;
    return sonic;
}

inline ProcessorConduitParams processorConduitFactoryPreset(
    uint32_t index, const ProcessorConduitParams& audition)
{
    ProcessorConduitParams p;
    switch (std::min<uint32_t>(
        index, kProcessorConduitFactoryPresetCount - 1u)) {
    case 1u: // DIRECT PA
        p.material = ProcessorConduitMaterial::Direct;
        p.driver = 0.30f;
        p.size = 0.48f;
        p.tension = 0.52f;
        p.damping = 0.62f;
        p.pickup = 0.32f;
        p.contact = 0.28f;
        p.feedback = 0.42f;
        p.pedal = ProcessorConduitPedal::Wool;
        p.pedalPosition = ProcessorConduitPedalPosition::PostPiezo;
        p.pedalDrive = 0.30f;
        p.pedalTone = 0.48f;
        p.pedalMix = 0.32f;
        p.paDrive = 0.62f;
        p.micMotion = 0.24f;
        p.chamber = 0.0f;
        p.stereoWidth = 0.54f;
        break;
    case 2u: // PIEZO SHRED
        p.material = ProcessorConduitMaterial::BrightBronze;
        p.driver = 0.56f;
        p.size = 0.44f;
        p.tension = 0.68f;
        p.damping = 0.42f;
        p.pickup = 0.82f;
        p.contact = 0.76f;
        p.feedback = 0.36f;
        p.pedal = ProcessorConduitPedal::Shred;
        p.pedalPosition = ProcessorConduitPedalPosition::PostPiezo;
        p.pedalDrive = 0.82f;
        p.pedalTone = 0.68f;
        p.pedalMix = 0.78f;
        p.paDrive = 0.54f;
        p.micMotion = 0.38f;
        p.chamber = 0.28f;
        p.stereoWidth = 0.82f;
        break;
    case 3u: // RETURN RAT
        p.material = ProcessorConduitMaterial::Direct;
        p.driver = 0.44f;
        p.damping = 0.48f;
        p.pickup = 0.52f;
        p.contact = 0.52f;
        p.feedback = 0.78f;
        p.pedal = ProcessorConduitPedal::Rat;
        p.pedalPosition = ProcessorConduitPedalPosition::PostMic;
        p.pedalDrive = 0.72f;
        p.pedalTone = 0.58f;
        p.pedalMix = 0.70f;
        p.paDrive = 0.72f;
        p.micMotion = 0.68f;
        p.chamber = 0.0f;
        p.stereoWidth = 0.74f;
        break;
    case 4u: // THROWN BRONZE
        p.material = ProcessorConduitMaterial::TieredBronze;
        p.driver = 0.68f;
        p.size = 0.72f;
        p.tension = 0.46f;
        p.damping = 0.24f;
        p.pickup = 0.74f;
        p.contact = 0.72f;
        p.feedback = 0.90f;
        p.pedal = ProcessorConduitPedal::ZoneB;
        p.pedalPosition = ProcessorConduitPedalPosition::PostMic;
        p.pedalDrive = 0.66f;
        p.pedalTone = 0.70f;
        p.pedalMix = 0.64f;
        p.paDrive = 0.84f;
        p.micMotion = 0.98f;
        p.chamber = 0.72f;
        p.stereoWidth = 0.94f;
        break;
    case 5u: // PORCELAIN ROOM
        p.material = ProcessorConduitMaterial::Porcelain;
        p.driver = 0.42f;
        p.size = 0.64f;
        p.tension = 0.60f;
        p.damping = 0.26f;
        p.pickup = 0.66f;
        p.contact = 0.48f;
        p.feedback = 0.54f;
        p.pedal = ProcessorConduitPedal::Diode;
        p.pedalPosition = ProcessorConduitPedalPosition::PreDriver;
        p.pedalDrive = 0.38f;
        p.pedalTone = 0.76f;
        p.pedalMix = 0.36f;
        p.paDrive = 0.48f;
        p.micMotion = 0.34f;
        p.chamber = 0.94f;
        p.stereoWidth = 0.88f;
        break;
    case 6u: // STEEL HOWL
        p.material = ProcessorConduitMaterial::SteelShell;
        p.driver = 0.62f;
        p.size = 0.78f;
        p.tension = 0.56f;
        p.damping = 0.16f;
        p.pickup = 0.88f;
        p.contact = 0.70f;
        p.feedback = 0.88f;
        p.pedal = ProcessorConduitPedal::FuzzII;
        p.pedalPosition = ProcessorConduitPedalPosition::PostPiezo;
        p.pedalDrive = 0.58f;
        p.pedalTone = 0.62f;
        p.pedalMix = 0.52f;
        p.paDrive = 0.78f;
        p.micMotion = 0.72f;
        p.chamber = 0.86f;
        p.stereoWidth = 0.92f;
        break;
    case 7u: // SUBMERGED OCT
        p.material = ProcessorConduitMaterial::Water;
        p.driver = 0.52f;
        p.size = 0.86f;
        p.tension = 0.34f;
        p.damping = 0.44f;
        p.pickup = 0.64f;
        p.contact = 0.66f;
        p.feedback = 0.48f;
        p.pedal = ProcessorConduitPedal::Wool;
        p.pedalPosition = ProcessorConduitPedalPosition::PostPiezo;
        p.pedalDrive = 0.52f;
        p.pedalTone = 0.30f;
        p.pedalMix = 0.56f;
        p.octaveDown = 0.88f;
        p.octaveDrag = 0.92f;
        p.paDrive = 0.58f;
        p.micMotion = 0.44f;
        p.chamber = 0.58f;
        p.stereoWidth = 0.80f;
        break;
    case 8u: // SLOWED SKIN
        p.material = ProcessorConduitMaterial::LooseMembrane;
        p.driver = 0.48f;
        p.size = 0.92f;
        p.tension = 0.18f;
        p.damping = 0.32f;
        p.pickup = 0.56f;
        p.contact = 0.62f;
        p.feedback = 0.58f;
        p.pedal = ProcessorConduitPedal::FuzzI;
        p.pedalPosition = ProcessorConduitPedalPosition::PostPiezo;
        p.pedalDrive = 0.46f;
        p.pedalTone = 0.34f;
        p.pedalMix = 0.48f;
        p.octaveDown = 1.0f;
        p.octaveDrag = 1.0f;
        p.paDrive = 0.54f;
        p.micMotion = 0.50f;
        p.chamber = 0.70f;
        p.stereoWidth = 0.76f;
        break;
    case 9u: // BROKEN PA
        p.material = ProcessorConduitMaterial::Direct;
        p.driver = 0.72f;
        p.size = 0.36f;
        p.tension = 0.62f;
        p.damping = 0.46f;
        p.pickup = 0.42f;
        p.contact = 0.82f;
        p.feedback = 0.86f;
        p.pedal = ProcessorConduitPedal::Diode;
        p.pedalPosition = ProcessorConduitPedalPosition::PostMic;
        p.pedalDrive = 0.92f;
        p.pedalTone = 0.82f;
        p.pedalMix = 0.84f;
        p.paDrive = 0.96f;
        p.micMotion = 0.90f;
        p.chamber = 0.0f;
        p.stereoWidth = 0.88f;
        break;
    default: // OPEN METAL
        break;
    }
    return processorConduitPreserveAudition(p, audition);
}

inline float processorConduitRandomUnit(uint32_t& state)
{
    if (state == 0u) state = 0x6d2b79f5u;
    state ^= state << 13u;
    state ^= state >> 17u;
    state ^= state << 5u;
    return static_cast<float>(state & 0x00ffffffu) / 16777215.0f;
}

inline ProcessorConduitParams processorConduitRandomParams(
    const ProcessorConduitParams& audition, uint32_t seed)
{
    ProcessorConduitParams p = audition;
    auto random = [&seed]() { return processorConduitRandomUnit(seed); };
    p.material = static_cast<ProcessorConduitMaterial>(
        std::min<uint32_t>(static_cast<uint32_t>(random()
                * static_cast<float>(kProcessorConduitMaterialCount)),
            kProcessorConduitMaterialCount - 1u));
    p.driver = 0.22f + random() * 0.68f;
    p.size = 0.12f + random() * 0.82f;
    p.tension = 0.12f + random() * 0.80f;
    p.damping = 0.10f + random() * 0.76f;
    p.pickup = 0.08f + random() * 0.86f;
    p.contact = 0.20f + random() * 0.76f;
    p.feedback = 0.10f + random() * 0.78f;
    p.pedal = static_cast<ProcessorConduitPedal>(
        std::min<uint32_t>(static_cast<uint32_t>(random()
                * static_cast<float>(kProcessorConduitPedalCount)),
            kProcessorConduitPedalCount - 1u));
    p.pedalPosition = static_cast<ProcessorConduitPedalPosition>(
        std::min<uint32_t>(static_cast<uint32_t>(random()
                * static_cast<float>(kProcessorConduitPedalPositionCount)),
            kProcessorConduitPedalPositionCount - 1u));
    p.pedalDrive = 0.18f + random() * 0.78f;
    p.pedalTone = 0.12f + random() * 0.82f;
    p.pedalMix = 0.16f + random() * 0.78f;
    p.octaveDown = random() < 0.42f ? 0.0f : 0.18f + random() * 0.82f;
    p.octaveDrag = 0.24f + random() * 0.76f;
    p.paDrive = 0.22f + random() * 0.74f;
    p.micMotion = 0.12f + random() * 0.86f;
    p.chamber = random();
    p.stereoWidth = 0.22f + random() * 0.78f;
    return processorConduitPreserveAudition(p, audition);
}

} // namespace s3g
