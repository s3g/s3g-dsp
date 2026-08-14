#pragma once

#include "s3g_processor_stack.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

struct ProcessorStackFactoryPresetInfo {
    const char* name;
    const char* description;
};

inline constexpr uint32_t kProcessorStackFactoryPresetCount = 18u;

inline const ProcessorStackFactoryPresetInfo&
processorStackFactoryPresetInfo(uint32_t index)
{
    static constexpr std::array<ProcessorStackFactoryPresetInfo,
        kProcessorStackFactoryPresetCount> info {{
        { "CROOKED STACK", "Shared power-chord stack with playable speaker feedback." },
        { "BARE STACK", "Sparse pick excitation into an exposed cone and short spill." },
        { "ONE FINGER RIFF", "Root, fifth, and octave loading one sagging amplifier." },
        { "JANKY JAB", "Dry angular lead response with nasal focus and reluctant howl." },
        { "SPEAKER COUGH", "Negative return polarity and heavy cone displacement." },
        { "PINCHED WIRE", "Thin wire excitation pulled toward a high feedback partial." },
        { "BROWNOUT FIFTH", "Starved fuzz bias with a slow shared supply recovery." },
        { "ROOM FIGHT", "Wide microphone return with unstable harmonic competition." },
        { "WELDED CHORD", "HAND mode chords fused by amp intermodulation." },
        { "HOWL ON PRESSURE", "Pressure-ready loop poised below a bright howl." },
        { "AMP LEFT ON", "Long governed spill near the safe sustaining boundary." },
        { "BROKEN COMBO", "Small cabinet bark, hard pick, and brittle diode feedback." },
        { "PHRYGIAN BARRAGE", "Triplet Phrygian run through one biting lead string." },
        { "DIMINISHED RAKE", "A fast pendulum diminished rule loading power chords." },
        { "PEDAL TONE PANIC", "Root-pedal harmonic-minor leaps with short gates." },
        { "TRITONE SCRAMBLE", "Deterministic tritone cells tearing across three octaves." },
        { "DECLARED RIFF", "An explicit eight-step scale-degree pattern ready to edit." },
        { "FEEDBACK LANCE", "A narrow self-governed upper partial that stabs through the stack." },
    }};
    return info[std::min<uint32_t>(
        index, kProcessorStackFactoryPresetCount - 1u)];
}

inline ProcessorStackParams processorStackFactoryPreset(uint32_t index)
{
    ProcessorStackParams params;
    switch (std::min<uint32_t>(
        index, kProcessorStackFactoryPresetCount - 1u)) {
    case 1u: // BARE STACK
        params.mode = ProcessorStackMode::Lead;
        params.wire = 0.60f;
        params.pick = 0.62f;
        params.damping = 0.54f;
        params.circuit = ProcessorStackCircuit::ZoneA;
        params.bite = 0.32f;
        params.stack = 0.68f;
        params.sag = 0.34f;
        params.cone = 0.78f;
        params.feedback = 0.28f;
        params.spill = 0.14f;
        params.outputGainDb = -11.0f;
        break;
    case 2u: // ONE FINGER RIFF
        params.mode = ProcessorStackMode::Power;
        params.shape = 0.76f;
        params.wire = 0.68f;
        params.pick = 0.86f;
        params.damping = 0.46f;
        params.circuit = ProcessorStackCircuit::Rat;
        params.bite = 0.68f;
        params.stack = 0.76f;
        params.sag = 0.72f;
        params.focus = 0.48f;
        params.cone = 0.72f;
        params.feedback = 0.48f;
        params.root = 0.42f;
        params.outputGainDb = -14.0f;
        break;
    case 3u: // JANKY JAB
        params.mode = ProcessorStackMode::Lead;
        params.wire = 0.72f;
        params.pick = 0.94f;
        params.damping = 0.72f;
        params.glideMs = 18.0f;
        params.crooked = 0.92f;
        params.spill = 0.08f;
        params.circuit = ProcessorStackCircuit::ZoneB;
        params.bite = 0.62f;
        params.bias = 0.42f;
        params.stack = 0.70f;
        params.focus = 0.78f;
        params.cone = 0.62f;
        params.feedback = 0.46f;
        params.harmonic = 0.66f;
        params.chaos = 0.58f;
        params.outputGainDb = -13.0f;
        break;
    case 4u: // SPEAKER COUGH
        params.mode = ProcessorStackMode::Power;
        params.shape = 0.38f;
        params.wire = 0.52f;
        params.pick = 0.54f;
        params.damping = 0.64f;
        params.spill = 0.54f;
        params.circuit = ProcessorStackCircuit::Wool;
        params.bite = 0.74f;
        params.stack = 0.58f;
        params.sag = 0.68f;
        params.focus = 0.30f;
        params.cone = 0.98f;
        params.cabinet = 0.74f;
        params.feedback = 0.82f;
        params.proximity = 0.38f;
        params.tracking = 0.28f;
        params.polarity = 0.22f;
        params.chaos = 0.64f;
        params.outputGainDb = -16.0f;
        break;
    case 5u: // PINCHED WIRE
        params.mode = ProcessorStackMode::Lead;
        params.wire = 0.88f;
        params.pick = 0.88f;
        params.damping = 0.58f;
        params.glideMs = 42.0f;
        params.crooked = 0.48f;
        params.circuit = ProcessorStackCircuit::Shred;
        params.bite = 0.70f;
        params.pedalTone = 0.82f;
        params.stack = 0.64f;
        params.focus = 0.86f;
        params.cone = 0.56f;
        params.feedback = 0.76f;
        params.harmonic = 0.88f;
        params.tracking = 0.90f;
        params.root = 0.14f;
        params.pierce = 0.94f;
        params.selfListen = 0.90f;
        params.outputGainDb = -14.0f;
        break;
    case 6u: // BROWNOUT FIFTH
        params.mode = ProcessorStackMode::Power;
        params.shape = 0.44f;
        params.wire = 0.58f;
        params.pick = 0.72f;
        params.circuit = ProcessorStackCircuit::FuzzI;
        params.bite = 0.86f;
        params.pedalTone = 0.42f;
        params.bias = 0.16f;
        params.stack = 0.78f;
        params.sag = 0.94f;
        params.focus = 0.42f;
        params.cone = 0.70f;
        params.feedback = 0.58f;
        params.proximity = 0.62f;
        params.outputGainDb = -16.0f;
        break;
    case 7u: // ROOM FIGHT
        params.mode = ProcessorStackMode::Power;
        params.shape = 0.82f;
        params.wire = 0.66f;
        params.crooked = 0.62f;
        params.spill = 0.68f;
        params.circuit = ProcessorStackCircuit::ZoneA;
        params.bite = 0.64f;
        params.stack = 0.72f;
        params.sag = 0.58f;
        params.cone = 0.82f;
        params.mic = 0.94f;
        params.feedback = 0.84f;
        params.proximity = 0.34f;
        params.tracking = 0.54f;
        params.polarity = 0.64f;
        params.chaos = 0.92f;
        params.outputGainDb = -17.0f;
        break;
    case 8u: // WELDED CHORD
        params.mode = ProcessorStackMode::Hand;
        params.wire = 0.70f;
        params.pick = 0.76f;
        params.damping = 0.42f;
        params.circuit = ProcessorStackCircuit::Rat;
        params.bite = 0.82f;
        params.stack = 0.86f;
        params.sag = 0.82f;
        params.focus = 0.56f;
        params.cone = 0.74f;
        params.feedback = 0.54f;
        params.root = 0.36f;
        params.outputGainDb = -17.0f;
        break;
    case 9u: // HOWL ON PRESSURE
        params.mode = ProcessorStackMode::Lead;
        params.wire = 0.76f;
        params.pick = 0.70f;
        params.damping = 0.34f;
        params.crooked = 0.42f;
        params.spill = 0.48f;
        params.circuit = ProcessorStackCircuit::Diode;
        params.bite = 0.58f;
        params.stack = 0.74f;
        params.sag = 0.56f;
        params.focus = 0.72f;
        params.cone = 0.78f;
        params.feedback = 0.72f;
        params.proximity = 0.80f;
        params.harmonic = 0.72f;
        params.tracking = 0.88f;
        params.pierce = 0.96f;
        params.selfListen = 0.94f;
        params.outputGainDb = -15.0f;
        break;
    case 10u: // AMP LEFT ON
        params.mode = ProcessorStackMode::Power;
        params.shape = 0.68f;
        params.wire = 0.64f;
        params.pick = 0.58f;
        params.damping = 0.30f;
        params.crooked = 0.52f;
        params.spill = 0.92f;
        params.circuit = ProcessorStackCircuit::Wool;
        params.bite = 0.76f;
        params.stack = 0.76f;
        params.sag = 0.66f;
        params.cone = 0.90f;
        params.mic = 0.58f;
        params.feedback = 0.94f;
        params.proximity = 0.72f;
        params.harmonic = 0.52f;
        params.tracking = 0.76f;
        params.chaos = 0.54f;
        params.outputGainDb = -18.0f;
        break;
    case 11u: // BROKEN COMBO
        params.mode = ProcessorStackMode::Lead;
        params.wire = 0.82f;
        params.pick = 1.0f;
        params.damping = 0.72f;
        params.glideMs = 8.0f;
        params.crooked = 0.72f;
        params.spill = 0.24f;
        params.circuit = ProcessorStackCircuit::Diode;
        params.bite = 0.92f;
        params.pedalTone = 0.76f;
        params.bias = 0.34f;
        params.stack = 0.84f;
        params.sag = 0.48f;
        params.focus = 0.88f;
        params.cone = 0.88f;
        params.cabinet = 0.08f;
        params.feedback = 0.66f;
        params.proximity = 0.86f;
        params.harmonic = 0.80f;
        params.root = 0.10f;
        params.chaos = 0.72f;
        params.outputGainDb = -16.0f;
        break;
    case 12u: // PHRYGIAN BARRAGE
        params.mode = ProcessorStackMode::Lead;
        params.wire = 0.78f;
        params.pick = 0.92f;
        params.damping = 0.44f;
        params.glideMs = 9.0f;
        params.crooked = 0.82f;
        params.spill = 0.18f;
        params.arpPattern = ProcessorStackArpPattern::Up;
        params.scale = ProcessorStackScale::Phrygian;
        params.arpRate = ProcessorStackArpRate::SixteenthTriplet;
        params.arpOctaves = 3u;
        params.arpGate = 0.46f;
        params.circuit = ProcessorStackCircuit::Rat;
        params.bite = 0.78f;
        params.stack = 0.76f;
        params.sag = 0.54f;
        params.focus = 0.80f;
        params.cone = 0.70f;
        params.feedback = 0.58f;
        params.harmonic = 0.76f;
        params.tracking = 0.86f;
        params.chaos = 0.42f;
        params.outputGainDb = -15.0f;
        break;
    case 13u: // DIMINISHED RAKE
        params.mode = ProcessorStackMode::Power;
        params.shape = 0.28f;
        params.wire = 0.72f;
        params.pick = 0.86f;
        params.damping = 0.50f;
        params.glideMs = 4.0f;
        params.crooked = 0.74f;
        params.spill = 0.12f;
        params.arpPattern = ProcessorStackArpPattern::Pendulum;
        params.scale = ProcessorStackScale::Diminished;
        params.arpRate = ProcessorStackArpRate::ThirtySecond;
        params.arpOctaves = 2u;
        params.arpGate = 0.52f;
        params.circuit = ProcessorStackCircuit::Shred;
        params.bite = 0.86f;
        params.stack = 0.82f;
        params.sag = 0.70f;
        params.focus = 0.72f;
        params.cone = 0.76f;
        params.feedback = 0.48f;
        params.root = 0.36f;
        params.outputGainDb = -18.0f;
        break;
    case 14u: // PEDAL TONE PANIC
        params.mode = ProcessorStackMode::Lead;
        params.wire = 0.84f;
        params.pick = 0.96f;
        params.damping = 0.58f;
        params.glideMs = 13.0f;
        params.crooked = 0.94f;
        params.spill = 0.08f;
        params.arpPattern = ProcessorStackArpPattern::Pedal;
        params.scale = ProcessorStackScale::HarmonicMinor;
        params.arpRate = ProcessorStackArpRate::Sixteenth;
        params.arpOctaves = 3u;
        params.arpGate = 0.36f;
        params.circuit = ProcessorStackCircuit::ZoneB;
        params.bite = 0.82f;
        params.bias = 0.36f;
        params.stack = 0.80f;
        params.sag = 0.64f;
        params.focus = 0.88f;
        params.cone = 0.68f;
        params.feedback = 0.62f;
        params.harmonic = 0.82f;
        params.chaos = 0.68f;
        params.outputGainDb = -16.0f;
        break;
    case 15u: // TRITONE SCRAMBLE
        params.mode = ProcessorStackMode::Lead;
        params.wire = 0.74f;
        params.pick = 0.90f;
        params.damping = 0.62f;
        params.glideMs = 6.0f;
        params.crooked = 1.0f;
        params.spill = 0.22f;
        params.arpPattern = ProcessorStackArpPattern::Scramble;
        params.scale = ProcessorStackScale::Tritone;
        params.arpRate = ProcessorStackArpRate::ThirtySecond;
        params.arpOctaves = 3u;
        params.arpGate = 0.30f;
        params.circuit = ProcessorStackCircuit::FuzzII;
        params.bite = 0.88f;
        params.pedalTone = 0.72f;
        params.bias = 0.30f;
        params.stack = 0.84f;
        params.sag = 0.60f;
        params.focus = 0.84f;
        params.cone = 0.82f;
        params.feedback = 0.68f;
        params.harmonic = 0.90f;
        params.tracking = 0.92f;
        params.root = 0.08f;
        params.chaos = 0.86f;
        params.outputGainDb = -18.0f;
        break;
    case 16u: // DECLARED RIFF
        params.mode = ProcessorStackMode::Lead;
        params.wire = 0.80f;
        params.pick = 0.88f;
        params.damping = 0.52f;
        params.glideMs = 7.0f;
        params.crooked = 0.78f;
        params.spill = 0.14f;
        params.arpPattern = ProcessorStackArpPattern::Custom;
        params.scale = ProcessorStackScale::Diminished;
        params.arpRate = ProcessorStackArpRate::Sixteenth;
        params.arpGate = 0.44f;
        params.customPatternLength = 8u;
        params.customPattern = {{ 0, 4, 1, 5, -1, 7, 2, 6 }};
        params.circuit = ProcessorStackCircuit::Rat;
        params.bite = 0.80f;
        params.stack = 0.78f;
        params.sag = 0.62f;
        params.focus = 0.80f;
        params.cone = 0.72f;
        params.feedback = 0.56f;
        params.harmonic = 0.78f;
        params.tracking = 0.88f;
        params.chaos = 0.52f;
        params.outputGainDb = -16.0f;
        break;
    case 17u: // FEEDBACK LANCE
        params.mode = ProcessorStackMode::Lead;
        params.wire = 0.76f;
        params.pick = 0.94f;
        params.damping = 0.48f;
        params.glideMs = 11.0f;
        params.crooked = 0.72f;
        params.spill = 0.26f;
        params.circuit = ProcessorStackCircuit::Shred;
        params.bite = 0.78f;
        params.pedalTone = 0.86f;
        params.stack = 0.74f;
        params.sag = 0.52f;
        params.focus = 0.90f;
        params.cone = 0.70f;
        params.cabinet = 0.34f;
        params.mic = 0.42f;
        params.feedback = 0.86f;
        params.proximity = 0.90f;
        params.harmonic = 0.82f;
        params.tracking = 0.96f;
        params.polarity = 0.84f;
        params.root = 0.08f;
        params.chaos = 0.24f;
        params.pierce = 1.0f;
        params.selfListen = 1.0f;
        params.outputGainDb = -17.0f;
        break;
    default:
        break;
    }
    return sanitizeProcessorStackParams(params);
}

inline bool processorStackPresetMatches(const ProcessorStackParams& first,
    const ProcessorStackParams& second, float tolerance = 1.0e-5f)
{
    if (first.mode != second.mode || first.circuit != second.circuit
        || first.arpPattern != second.arpPattern
        || first.scale != second.scale || first.arpRate != second.arpRate
        || first.arpOctaves != second.arpOctaves
        || first.customPatternLength != second.customPatternLength
        || first.customPattern != second.customPattern) {
        return false;
    }
    const auto near = [tolerance](float a, float b) {
        return std::abs(a - b) <= tolerance;
    };
    return near(first.shape, second.shape)
        && near(first.wire, second.wire)
        && near(first.pick, second.pick)
        && near(first.damping, second.damping)
        && near(first.glideMs, second.glideMs)
        && near(first.crooked, second.crooked)
        && near(first.spill, second.spill)
        && near(first.arpGate, second.arpGate)
        && near(first.bite, second.bite)
        && near(first.pedalTone, second.pedalTone)
        && near(first.bias, second.bias)
        && near(first.stack, second.stack)
        && near(first.sag, second.sag)
        && near(first.focus, second.focus)
        && near(first.cone, second.cone)
        && near(first.cabinet, second.cabinet)
        && near(first.mic, second.mic)
        && near(first.feedback, second.feedback)
        && near(first.proximity, second.proximity)
        && near(first.harmonic, second.harmonic)
        && near(first.tracking, second.tracking)
        && near(first.polarity, second.polarity)
        && near(first.root, second.root)
        && near(first.chaos, second.chaos)
        && near(first.pierce, second.pierce)
        && near(first.selfListen, second.selfListen)
        && near(first.outputGainDb, second.outputGainDb);
}

inline int processorStackFactoryPresetIndex(
    const ProcessorStackParams& params)
{
    for (uint32_t index = 0u;
         index < kProcessorStackFactoryPresetCount; ++index) {
        if (processorStackPresetMatches(
                params, processorStackFactoryPreset(index))) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

} // namespace s3g
