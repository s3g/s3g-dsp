#pragma once

#include "s3g_ambi_neural_ecology.h"

#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

struct AmbiNeuralEcologyFactoryPresetInfo {
    const char* name;
    const char* description;
};

constexpr std::array<AmbiNeuralEcologyFactoryPresetInfo, 10> kAmbiNeuralEcologyFactoryPresets {{
    { "Listening Cell", "Sixteen recurrent nodes hear locally and regulate their activity." },
    { "Threshold Ring", "A single excitable ring hovers around intermittent oscillation." },
    { "Mutual Nerves", "Two fast and slow rings answer through a cross-wired auditory matrix." },
    { "Bilateral Memory", "Two sixteen-node lobes learn delayed cross-listening relationships." },
    { "Ecology 64", "The complete organism grows an adaptive eight-ear seventh-order auditory body." },
    { "Inhibitory Weather", "Mobile inhibitory ears open gaps in a drifting eight-way field." },
    { "Long Conduction", "Strongly anchored distant ears turn wide propagation into replies." },
    { "Glass Colony", "Fast adaptive ears follow a bright eight-corner spatial lattice." },
    { "Dormant Garden", "Strongly anchored ears slowly reorganize a restrained organism." },
    { "Red Field", "Driven feedback and short propagation approach a bounded unstable edge." },
}};

constexpr uint32_t kAmbiNeuralEcologyFactoryPresetCount
    = static_cast<uint32_t>(kAmbiNeuralEcologyFactoryPresets.size());

inline const AmbiNeuralEcologyFactoryPresetInfo& ambiNeuralEcologyFactoryPresetInfo(uint32_t index)
{
    return kAmbiNeuralEcologyFactoryPresets[
        std::min<uint32_t>(index, kAmbiNeuralEcologyFactoryPresetCount - 1u)];
}

inline AmbiNeuralEcologyParams ambiNeuralEcologyFactoryPreset(uint32_t index)
{
    AmbiNeuralEcologyParams p;
    switch (std::min<uint32_t>(index, kAmbiNeuralEcologyFactoryPresetCount - 1u)) {
    case 1u: // Threshold Ring
        p.nodeSet = AmbiNeuralNodeSet::Ring4;
        p.activity = 0.47f;
        p.drive = 2.64f;
        p.ringFeedback = 1.01f;
        p.matrixCoupling = 0.0f;
        p.hierarchy = 0.0f;
        p.phaseShift = 0.62f;
        p.registerSemitones = 10.0f;
        p.timeSpread = 0.0f;
        p.diversity = 0.42f;
        p.brownian = 0.31f;
        p.drift = 0.36f;
        p.fieldReturn = 0.22f;
        p.propagationMs = 11.0f;
        p.pickupFocus = 0.90f;
        p.auditoryPlasticity = 0.04f;
        p.metabolism = 0.22f;
        p.adaptation = 0.22f;
        p.fieldWidth = 0.58f;
        p.cellWidth = 0.88f;
        p.mobility = 0.52f;
        p.outputGainDb = -13.0f;
        break;
    case 2u: // Mutual Nerves
        p.nodeSet = AmbiNeuralNodeSet::Dual8;
        p.activity = 0.54f;
        p.drive = 2.22f;
        p.ringFeedback = 0.92f;
        p.matrixCoupling = 0.68f;
        p.hierarchy = 0.58f;
        p.phaseShift = 0.44f;
        p.registerSemitones = 5.0f;
        p.timeSpread = 1.28f;
        p.diversity = 0.34f;
        p.brownian = 0.18f;
        p.drift = 0.22f;
        p.selfModulation = 0.64f;
        p.fieldReturn = 0.52f;
        p.propagationMs = 28.0f;
        p.pickupFocus = 0.94f;
        p.listeningMode = AmbiNeuralListeningMode::Cross;
        p.auditoryPlasticity = 0.24f;
        p.metabolism = 0.38f;
        p.adaptation = 0.34f;
        p.fieldWidth = 0.74f;
        p.cellWidth = 0.62f;
        p.mobility = 0.46f;
        p.outputGainDb = -16.0f;
        break;
    case 3u: // Bilateral Memory
        p.nodeSet = AmbiNeuralNodeSet::Pair32;
        p.activity = 0.49f;
        p.drive = 1.88f;
        p.ringFeedback = 0.84f;
        p.matrixCoupling = 0.54f;
        p.hierarchy = 0.70f;
        p.phaseShift = 0.54f;
        p.registerSemitones = -8.0f;
        p.timeSpread = 1.18f;
        p.diversity = 0.22f;
        p.brownian = 0.14f;
        p.drift = 0.28f;
        p.selfModulation = 0.72f;
        p.fieldReturn = 0.64f;
        p.propagationMs = 66.0f;
        p.pickupFocus = 0.82f;
        p.listeningMode = AmbiNeuralListeningMode::Cross;
        p.auditoryPlasticity = 0.17f;
        p.metabolism = 0.28f;
        p.adaptation = 0.22f;
        p.plasticity = 0.08f;
        p.plasticityMode = AmbiNeuralPlasticityMode::Balance;
        p.fieldWidth = 0.94f;
        p.cellWidth = 0.46f;
        p.mobility = 0.24f;
        p.rotationRateHz = -0.006f;
        p.outputGainDb = -21.0f;
        break;
    case 4u: // Ecology 64
        p.order = 7u;
        p.nodeSet = AmbiNeuralNodeSet::Field64;
        p.activity = 0.51f;
        p.drive = 1.92f;
        p.ringFeedback = 0.86f;
        p.matrixCoupling = 0.58f;
        p.hierarchy = 0.62f;
        p.phaseShift = 0.40f;
        p.timeSpread = 1.12f;
        p.diversity = 0.26f;
        p.brownian = 0.16f;
        p.drift = 0.20f;
        p.selfModulation = 0.55f;
        p.fieldReturn = 0.46f;
        p.propagationMs = 38.0f;
        p.pickupFocus = 0.86f;
        p.pickupSet = AmbiNeuralPickupSet::Cube8;
        p.pickupAdapt = 0.48f;
        p.pickupAnchor = 0.24f;
        p.listeningMode = AmbiNeuralListeningMode::Roaming;
        p.auditoryPlasticity = 0.15f;
        p.metabolism = 0.34f;
        p.adaptation = 0.18f;
        p.plasticity = 0.045f;
        p.plasticityMode = AmbiNeuralPlasticityMode::Reinforce;
        p.fieldWidth = 1.0f;
        p.cellWidth = 0.56f;
        p.mobility = 0.38f;
        p.rotationRateHz = 0.009f;
        p.air = 0.18f;
        p.outputGainDb = -25.0f;
        break;
    case 5u: // Inhibitory Weather
        p.nodeSet = AmbiNeuralNodeSet::Field64;
        p.activity = 0.55f;
        p.drive = 2.30f;
        p.ringFeedback = 0.90f;
        p.matrixCoupling = 0.72f;
        p.hierarchy = 0.48f;
        p.phaseShift = 0.72f;
        p.registerSemitones = -4.0f;
        p.timeSpread = 1.34f;
        p.diversity = 0.48f;
        p.brownian = 0.34f;
        p.drift = 0.42f;
        p.selfModulation = 0.61f;
        p.fieldReturn = 0.58f;
        p.propagationMs = 52.0f;
        p.pickupFocus = 0.76f;
        p.pickupSet = AmbiNeuralPickupSet::Cube8;
        p.pickupAdapt = 0.72f;
        p.pickupAnchor = 0.14f;
        p.listeningMode = AmbiNeuralListeningMode::Diffuse;
        p.auditoryPlasticity = 0.32f;
        p.metabolism = 0.42f;
        p.adaptation = 0.25f;
        p.plasticity = 0.24f;
        p.plasticityMode = AmbiNeuralPlasticityMode::Inhibit;
        p.fieldWidth = 0.92f;
        p.cellWidth = 0.72f;
        p.mobility = 0.64f;
        p.rotationRateHz = -0.014f;
        p.air = 0.24f;
        p.outputGainDb = -27.0f;
        break;
    case 6u: // Long Conduction
        p.nodeSet = AmbiNeuralNodeSet::Pair32;
        p.activity = 0.50f;
        p.drive = 1.72f;
        p.ringFeedback = 0.82f;
        p.matrixCoupling = 0.46f;
        p.hierarchy = 0.78f;
        p.phaseShift = 0.36f;
        p.registerSemitones = -13.0f;
        p.timeSpread = 1.52f;
        p.diversity = 0.14f;
        p.brownian = 0.20f;
        p.drift = 0.38f;
        p.selfModulation = 0.82f;
        p.fieldReturn = 0.78f;
        p.propagationMs = 154.0f;
        p.pickupFocus = 0.68f;
        p.pickupSet = AmbiNeuralPickupSet::Cube8;
        p.pickupAdapt = 0.34f;
        p.pickupAnchor = 0.62f;
        p.listeningMode = AmbiNeuralListeningMode::Cross;
        p.auditoryPlasticity = 0.12f;
        p.metabolism = 0.24f;
        p.adaptation = 0.16f;
        p.fieldWidth = 1.0f;
        p.cellWidth = 0.36f;
        p.mobility = 0.18f;
        p.air = 0.42f;
        p.outputGainDb = -22.0f;
        break;
    case 7u: // Glass Colony
        p.nodeSet = AmbiNeuralNodeSet::Field64;
        p.activity = 0.58f;
        p.drive = 2.76f;
        p.ringFeedback = 0.96f;
        p.matrixCoupling = 0.66f;
        p.hierarchy = 0.34f;
        p.phaseShift = 0.82f;
        p.registerSemitones = 22.0f;
        p.timeSpread = 0.72f;
        p.diversity = 0.78f;
        p.brownian = 0.15f;
        p.drift = 0.16f;
        p.selfModulation = 0.42f;
        p.fieldReturn = 0.49f;
        p.propagationMs = 8.5f;
        p.pickupFocus = 0.96f;
        p.pickupSet = AmbiNeuralPickupSet::Cube8;
        p.pickupAdapt = 0.68f;
        p.pickupAnchor = 0.32f;
        p.listeningMode = AmbiNeuralListeningMode::Roaming;
        p.auditoryPlasticity = 0.26f;
        p.metabolism = 0.48f;
        p.adaptation = 0.28f;
        p.plasticity = 0.16f;
        p.plasticityMode = AmbiNeuralPlasticityMode::Reinforce;
        p.fieldWidth = 0.86f;
        p.cellWidth = 0.90f;
        p.mobility = 0.54f;
        p.rotationRateHz = 0.025f;
        p.outputGainDb = -29.0f;
        break;
    case 8u: // Dormant Garden
        p.nodeSet = AmbiNeuralNodeSet::Field64;
        p.activity = 0.39f;
        p.drive = 1.38f;
        p.ringFeedback = 0.74f;
        p.matrixCoupling = 0.40f;
        p.hierarchy = 0.88f;
        p.phaseShift = 0.24f;
        p.registerSemitones = -20.0f;
        p.timeSpread = 1.46f;
        p.diversity = 0.12f;
        p.brownian = 0.42f;
        p.drift = 0.62f;
        p.selfModulation = 0.90f;
        p.fieldReturn = 0.30f;
        p.propagationMs = 92.0f;
        p.pickupFocus = 0.58f;
        p.pickupSet = AmbiNeuralPickupSet::Cube8;
        p.pickupAdapt = 0.24f;
        p.pickupAnchor = 0.78f;
        p.listeningMode = AmbiNeuralListeningMode::Diffuse;
        p.auditoryPlasticity = 0.20f;
        p.metabolism = 0.16f;
        p.adaptation = 0.42f;
        p.plasticity = 0.11f;
        p.plasticityMode = AmbiNeuralPlasticityMode::Balance;
        p.freeze = 0u;
        p.fieldWidth = 0.96f;
        p.cellWidth = 0.28f;
        p.mobility = 0.20f;
        p.rotationRateHz = 0.003f;
        p.outputGainDb = -22.0f;
        break;
    case 9u: // Red Field
        p.nodeSet = AmbiNeuralNodeSet::Cell16;
        p.activity = 0.67f;
        p.drive = 4.12f;
        p.ringFeedback = 1.12f;
        p.matrixCoupling = 0.92f;
        p.hierarchy = 0.52f;
        p.phaseShift = 0.74f;
        p.registerSemitones = 7.0f;
        p.timeSpread = 1.08f;
        p.diversity = 0.62f;
        p.brownian = 0.24f;
        p.drift = 0.24f;
        p.selfModulation = 0.70f;
        p.fieldReturn = 0.86f;
        p.propagationMs = 4.5f;
        p.pickupFocus = 0.92f;
        p.listeningMode = AmbiNeuralListeningMode::Local;
        p.auditoryPlasticity = 0.08f;
        p.metabolism = 0.58f;
        p.adaptation = 0.08f;
        p.plasticity = 0.05f;
        p.plasticityMode = AmbiNeuralPlasticityMode::Prune;
        p.fieldWidth = 0.78f;
        p.cellWidth = 0.68f;
        p.mobility = 0.72f;
        p.outputGainDb = -27.0f;
        break;
    case 0u:
    default: // Listening Cell
        break;
    }
    p.seed ^= 0x9e3779b9u * (index + 1u);
    if (p.seed == 0u) p.seed = 1u;
    return sanitizeAmbiNeuralEcologyParams(p);
}

inline float ambiNeuralEcologyRandomUnit(uint32_t& seed)
{
    seed += 0x9e3779b9u;
    uint32_t value = seed;
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return static_cast<float>(value & 0x00ffffffu) / static_cast<float>(0x01000000u);
}

inline float ambiNeuralEcologyRandomRange(uint32_t& seed, float low, float high)
{
    return low + (high - low) * ambiNeuralEcologyRandomUnit(seed);
}

inline AmbiNeuralEcologyParams randomizeAmbiNeuralEcologyParams(
    const AmbiNeuralEcologyParams& current,
    uint32_t& seed,
    bool genomeAValid = false,
    bool genomeBValid = false)
{
    auto p = sanitizeAmbiNeuralEcologyParams(current);

    // Order, population, auditory aperture, score transport, and output trim are
    // performer decisions. Randomization changes the ecology inside that frame.
    p.activity = ambiNeuralEcologyRandomRange(seed, 0.46f, 0.68f);
    p.drive = ambiNeuralEcologyRandomRange(seed, 1.55f, 3.55f);
    p.ringFeedback = ambiNeuralEcologyRandomRange(seed, 0.68f, 1.08f);
    p.matrixCoupling = ambiNeuralEcologyRandomRange(seed, 0.16f, 0.86f);
    p.hierarchy = ambiNeuralEcologyRandomRange(seed, 0.16f, 0.92f);
    p.phaseShift = ambiNeuralEcologyRandomRange(seed, 0.0f, 0.88f);
    p.registerSemitones = p.nodeSet == AmbiNeuralNodeSet::Field64
        ? ambiNeuralEcologyRandomRange(seed, 12.0f, 30.0f)
        : ambiNeuralEcologyRandomRange(seed, -14.0f, 22.0f);
    p.timeSpread = ambiNeuralEcologyRandomRange(seed, 0.42f, 1.52f);
    p.diversity = ambiNeuralEcologyRandomRange(seed, 0.04f, 0.86f);
    p.brownian = ambiNeuralEcologyRandomRange(seed, 0.0f, 0.48f);
    p.drift = ambiNeuralEcologyRandomRange(seed, 0.0f, 0.58f);
    p.selfModulation = ambiNeuralEcologyRandomRange(seed, 0.08f, 0.92f);
    p.fieldReturn = ambiNeuralEcologyRandomRange(seed, 0.18f, 0.82f);
    p.propagationMs = std::pow(2.0f,
        ambiNeuralEcologyRandomRange(seed, std::log2(2.0f), std::log2(155.0f)));
    p.pickupFocus = ambiNeuralEcologyRandomRange(seed, 0.45f, 1.0f);
    p.pickupAdapt = ambiNeuralEcologyRandomRange(seed, 0.08f, 0.82f);
    p.pickupAnchor = ambiNeuralEcologyRandomRange(seed, 0.06f, 0.86f);
    p.listeningMode = static_cast<AmbiNeuralListeningMode>(
        static_cast<uint32_t>(ambiNeuralEcologyRandomUnit(seed) * 4.0f) % 4u);
    p.auditoryPlasticity = ambiNeuralEcologyRandomRange(seed, 0.02f, 0.46f);
    p.metabolism = ambiNeuralEcologyRandomRange(seed, 0.12f, 0.70f);
    p.adaptation = ambiNeuralEcologyRandomRange(seed, 0.04f, 0.56f);
    p.plasticity = ambiNeuralEcologyRandomRange(seed, 0.0f, 0.28f);
    p.plasticityMode = static_cast<AmbiNeuralPlasticityMode>(
        static_cast<uint32_t>(ambiNeuralEcologyRandomUnit(seed) * 4.0f) % 4u);
    p.genomeMorph = genomeAValid && genomeBValid ? ambiNeuralEcologyRandomUnit(seed) : 0.0f;
    p.heredity = genomeAValid || genomeBValid
        ? ambiNeuralEcologyRandomRange(seed, 0.0f, 0.62f) : 0.0f;
    p.mutationDepth = ambiNeuralEcologyRandomRange(seed, 0.12f, 0.82f);
    p.freeze = 0u;
    p.centerAzimuthDeg = ambiNeuralEcologyRandomRange(seed, -45.0f, 45.0f);
    p.centerElevationDeg = ambiNeuralEcologyRandomRange(seed, -24.0f, 28.0f);
    p.centerDistance = ambiNeuralEcologyRandomRange(seed, 0.70f, 1.55f);
    p.fieldWidth = ambiNeuralEcologyRandomRange(seed, 0.42f, 1.0f);
    p.cellWidth = ambiNeuralEcologyRandomRange(seed, 0.18f, 0.94f);
    p.mobility = ambiNeuralEcologyRandomRange(seed, 0.08f, 0.78f);
    p.spatialInertia = ambiNeuralEcologyRandomRange(seed, 0.46f, 0.96f);
    p.rotationRateHz = ambiNeuralEcologyRandomRange(seed, -0.045f, 0.045f);
    p.air = ambiNeuralEcologyRandomRange(seed, 0.0f, 0.45f);
    p.doppler = ambiNeuralEcologyRandomUnit(seed) < 0.74f
        ? 0.0f : ambiNeuralEcologyRandomRange(seed, 0.03f, 0.32f);

    p.seed = 1u + static_cast<uint32_t>(ambiNeuralEcologyRandomUnit(seed) * 65534.0f);
    return sanitizeAmbiNeuralEcologyParams(p);
}

} // namespace s3g
