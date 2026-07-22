#pragma once

#include "s3g_ambi_pulsar_encoder.h"

#include <array>
#include <cstdint>

namespace s3g {

struct AmbiPulsarFactoryPresetInfo {
    const char* name;
    const char* description;
};

constexpr std::array<AmbiPulsarFactoryPresetInfo, 12> kAmbiPulsarFactoryPresets {{
    { "Crossing", "Three luminous trains crossing rhythm and pitch." },
    { "Auditory Sieve", "Sparse residue pattern with displaced formant lanes." },
    { "Low Constellation", "Slow, overlapping low pulsars in a broad field." },
    { "Glass Semaphore", "Short bright pulses distributed between three positions." },
    { "Folded Current", "Dense nonlinear pulsarets with slow orbital motion." },
    { "Impulsion", "Bandlimited impulse trains moving from rhythm into tone." },
    { "Pendular Choir", "Overlapping harmonic pulsarets with meso-scale beating." },
    { "Dust Geometry", "Probabilistic noise pulsarets with a wide spatial trace." },
    { "Axon Choir", "The four neural timescales emerge as a direct spatial source." },
    { "Matrix Organism", "Signed cross-coupling imprints all three captured neural tables." },
    { "Slew Garden", "Slow control paths bend pulsaret envelope and FM trajectories." },
    { "Red Preamp", "Driven cybernetic feedback approaches the unstable edge." },
}};

constexpr uint32_t kAmbiPulsarFactoryPresetCount = static_cast<uint32_t>(kAmbiPulsarFactoryPresets.size());

inline const AmbiPulsarFactoryPresetInfo& ambiPulsarFactoryPresetInfo(uint32_t index)
{
    return kAmbiPulsarFactoryPresets[std::min<uint32_t>(index, kAmbiPulsarFactoryPresetCount - 1u)];
}

inline AmbiPulsarParams ambiPulsarFactoryPreset(uint32_t index)
{
    AmbiPulsarParams p;
    switch (std::min<uint32_t>(index, kAmbiPulsarFactoryPresetCount - 1u)) {
    case 11u: // Red Preamp
        p.emissionHz = 47.0f;
        p.emissionModDepth = 0.18f;
        p.formantScatterSemitones = 0.9f;
        p.lanes[0] = { 105.0f, 2.8f, 0.62f, 0.00f, AmbiPulsarWaveform::Fold };
        p.lanes[1] = { 510.0f, 1.5f, 0.50f, 0.16f, AmbiPulsarWaveform::Saw };
        p.lanes[2] = { 2480.0f, 0.72f, 0.38f, 0.39f, AmbiPulsarWaveform::Impulse };
        p.envelope = AmbiPulsarEnvelope::Tukey;
        p.quality = AmbiPulsarQuality::Ultra;
        p.neuralLevel = 0.72f;
        p.neural.drive = 3.85f;
        p.neural.feedback = 1.08f;
        p.neural.coupling = 0.88f;
        p.neural.hierarchy = 0.46f;
        p.neural.phaseShift = 0.76f;
        p.neural.brownian = 0.24f;
        p.neural.drift = 0.20f;
        p.neural.selfModulation = 0.72f;
        p.neural.audioFeedback = 0.54f;
        p.neuralPulsaretMix = 0.44f;
        p.neuralEnvelopeMix = 0.20f;
        p.neuralFmDepthSemitones = 4.5f;
        p.spatialWidth = 0.88f;
        p.orbitDepth = 0.62f;
        p.outputGainDb = -18.0f;
        break;
    case 10u: // Slew Garden
        p.emissionHz = 6.8f;
        p.emissionModRateHz = 0.021f;
        p.emissionModDepth = 0.42f;
        p.formantModDepthSemitones = 0.0f;
        p.lanes[0] = { 82.0f, 3.9f, 0.82f, 0.00f, AmbiPulsarWaveform::Sine };
        p.lanes[1] = { 267.0f, 2.7f, 0.66f, 0.27f, AmbiPulsarWaveform::Triangle };
        p.lanes[2] = { 930.0f, 1.8f, 0.50f, 0.58f, AmbiPulsarWaveform::Overtone };
        p.envelope = AmbiPulsarEnvelope::Welch;
        p.neuralLevel = 0.16f;
        p.neural.drive = 2.05f;
        p.neural.feedback = 0.82f;
        p.neural.coupling = 0.44f;
        p.neural.hierarchy = 0.90f;
        p.neural.phaseShift = 0.52f;
        p.neural.brownian = 0.38f;
        p.neural.drift = 0.72f;
        p.neural.selfModulation = 0.92f;
        p.neural.audioFeedback = 0.10f;
        p.neuralPulsaretMix = 0.18f;
        p.neuralEnvelopeMix = 0.82f;
        p.neuralFmDepthSemitones = 7.0f;
        p.spatialWidth = 0.94f;
        p.orbitRateHz = -0.008f;
        p.outputGainDb = -15.0f;
        break;
    case 9u: // Matrix Organism
        p.emissionHz = 23.0f;
        p.probability = 0.86f;
        p.sieveModulo = 3u;
        p.sieveResidue = 1u;
        p.lanes[0] = { 156.0f, 1.9f, 0.74f, 0.00f, AmbiPulsarWaveform::Sine };
        p.lanes[1] = { 608.0f, 1.1f, 0.58f, 0.21f, AmbiPulsarWaveform::Overtone };
        p.lanes[2] = { 1940.0f, 0.64f, 0.42f, 0.48f, AmbiPulsarWaveform::Fold };
        p.envelope = AmbiPulsarEnvelope::Tukey;
        p.neuralLevel = 0.28f;
        p.neural.drive = 2.72f;
        p.neural.feedback = 0.94f;
        p.neural.coupling = 0.78f;
        p.neural.hierarchy = 0.64f;
        p.neural.phaseShift = 0.68f;
        p.neural.brownian = 0.22f;
        p.neural.drift = 0.30f;
        p.neural.selfModulation = 0.58f;
        p.neural.audioFeedback = 0.24f;
        p.neuralPulsaretMix = 0.78f;
        p.neuralEnvelopeMix = 0.46f;
        p.neuralFmDepthSemitones = 3.6f;
        p.spatialWidth = 0.76f;
        p.orbitDepth = 0.74f;
        p.outputGainDb = -16.0f;
        break;
    case 8u: // Axon Choir
        p.emissionHz = 3.4f;
        p.emissionModRateHz = 0.013f;
        p.emissionModDepth = 0.28f;
        p.lanes[0] = { 74.0f, 3.4f, 0.46f, 0.00f, AmbiPulsarWaveform::Sine };
        p.lanes[1] = { 148.0f, 2.8f, 0.38f, 0.23f, AmbiPulsarWaveform::Triangle };
        p.lanes[2] = { 296.0f, 2.2f, 0.30f, 0.51f, AmbiPulsarWaveform::Overtone };
        p.envelope = AmbiPulsarEnvelope::Welch;
        p.neuralLevel = 0.82f;
        p.neural.drive = 2.38f;
        p.neural.feedback = 0.91f;
        p.neural.coupling = 0.52f;
        p.neural.hierarchy = 0.74f;
        p.neural.phaseShift = 0.38f;
        p.neural.brownian = 0.16f;
        p.neural.drift = 0.28f;
        p.neural.selfModulation = 0.48f;
        p.neural.audioFeedback = 0.08f;
        p.spatialWidth = 1.0f;
        p.orbitDepth = 0.28f;
        p.outputGainDb = -17.0f;
        break;
    case 1u: // Auditory Sieve
        p.emissionHz = 31.0f;
        p.probability = 0.96f;
        p.sieveModulo = 5u;
        p.sieveResidue = 2u;
        p.laneDistribution = 0.34f;
        p.lanes[0] = { 190.0f, 1.65f, 0.90f, 0.00f, AmbiPulsarWaveform::Overtone };
        p.lanes[1] = { 815.0f, 0.82f, 0.70f, 0.31f, AmbiPulsarWaveform::Triangle };
        p.lanes[2] = { 2370.0f, 0.36f, 0.52f, 0.67f, AmbiPulsarWaveform::Impulse };
        p.envelope = AmbiPulsarEnvelope::Tukey;
        p.spatialWidth = 0.78f;
        p.orbitRateHz = -0.017f;
        p.outputGainDb = -10.0f;
        break;
    case 2u: // Low Constellation
        p.emissionHz = 2.7f;
        p.emissionModRateHz = 0.027f;
        p.emissionModDepth = 0.38f;
        p.formantModRateHz = 0.19f;
        p.formantModDepthSemitones = 1.7f;
        p.formantScatterSemitones = 0.8f;
        p.lanes[0] = { 42.0f, 3.8f, 0.92f, 0.00f, AmbiPulsarWaveform::Sine };
        p.lanes[1] = { 67.0f, 2.7f, 0.76f, 0.23f, AmbiPulsarWaveform::Triangle };
        p.lanes[2] = { 109.0f, 2.2f, 0.62f, 0.52f, AmbiPulsarWaveform::Overtone };
        p.envelope = AmbiPulsarEnvelope::Welch;
        p.spatialWidth = 0.92f;
        p.orbitDepth = 0.66f;
        p.orbitRateHz = 0.008f;
        p.air = 0.28f;
        p.outputGainDb = -13.0f;
        break;
    case 3u: // Glass Semaphore
        p.emissionHz = 54.0f;
        p.laneDistribution = 0.90f;
        p.probability = 0.82f;
        p.burstOn = 7u;
        p.burstOff = 4u;
        p.formantScatterSemitones = 0.65f;
        p.lanes[0] = { 920.0f, 0.24f, 0.82f, 0.00f, AmbiPulsarWaveform::Sine };
        p.lanes[1] = { 1840.0f, 0.20f, 0.67f, 0.12f, AmbiPulsarWaveform::Overtone };
        p.lanes[2] = { 4100.0f, 0.14f, 0.52f, 0.26f, AmbiPulsarWaveform::Impulse };
        p.envelope = AmbiPulsarEnvelope::Percussive;
        p.envelopeEdge = 0.68f;
        p.spatialWidth = 0.70f;
        p.spatialScatter = 0.22f;
        p.orbitRateHz = 0.071f;
        p.outputGainDb = -9.5f;
        break;
    case 4u: // Folded Current
        p.emissionHz = 76.0f;
        p.emissionModRateHz = 0.21f;
        p.emissionModDepth = 0.22f;
        p.formantModRateHz = 3.2f;
        p.formantModDepthSemitones = 2.4f;
        p.lanes[0] = { 118.0f, 4.2f, 0.72f, 0.00f, AmbiPulsarWaveform::Fold };
        p.lanes[1] = { 355.0f, 2.8f, 0.64f, 0.17f, AmbiPulsarWaveform::Fold };
        p.lanes[2] = { 1065.0f, 1.7f, 0.50f, 0.43f, AmbiPulsarWaveform::Saw };
        p.envelope = AmbiPulsarEnvelope::Tukey;
        p.quality = AmbiPulsarQuality::Ultra;
        p.spatialWidth = 0.62f;
        p.orbitDepth = 0.84f;
        p.orbitRateHz = 0.034f;
        p.outputGainDb = -15.0f;
        break;
    case 5u: // Impulsion
        p.emissionHz = 12.0f;
        p.emissionModRateHz = 0.018f;
        p.emissionModDepth = 0.82f;
        p.probability = 1.0f;
        p.lanes[0] = { 96.0f, 0.58f, 0.88f, 0.00f, AmbiPulsarWaveform::Impulse };
        p.lanes[1] = { 430.0f, 0.42f, 0.67f, 0.21f, AmbiPulsarWaveform::Impulse };
        p.lanes[2] = { 1720.0f, 0.28f, 0.48f, 0.49f, AmbiPulsarWaveform::Impulse };
        p.envelope = AmbiPulsarEnvelope::Hann;
        p.quality = AmbiPulsarQuality::Ultra;
        p.spatialWidth = 0.56f;
        p.orbitDepth = 0.42f;
        p.outputGainDb = -9.0f;
        break;
    case 6u: // Pendular Choir
        p.emissionHz = 9.5f;
        p.emissionModRateHz = 0.052f;
        p.emissionModDepth = 0.31f;
        p.formantModRateHz = 0.37f;
        p.formantModDepthSemitones = 3.2f;
        p.lanes[0] = { 144.0f, 3.6f, 0.88f, 0.00f, AmbiPulsarWaveform::Overtone };
        p.lanes[1] = { 216.0f, 3.1f, 0.72f, 0.14f, AmbiPulsarWaveform::Overtone };
        p.lanes[2] = { 324.0f, 2.6f, 0.62f, 0.37f, AmbiPulsarWaveform::Sine };
        p.envelope = AmbiPulsarEnvelope::Welch;
        p.spatialWidth = 0.84f;
        p.orbitDepth = 0.58f;
        p.orbitRateHz = -0.011f;
        p.outputGainDb = -13.0f;
        break;
    case 7u: // Dust Geometry
        p.emissionHz = 83.0f;
        p.probability = 0.34f;
        p.burstOn = 11u;
        p.burstOff = 5u;
        p.sieveModulo = 3u;
        p.sieveResidue = 1u;
        p.laneDistribution = 0.74f;
        p.formantScatterSemitones = 8.5f;
        p.phaseScatter = 1.0f;
        p.lanes[0] = { 580.0f, 0.28f, 0.72f, 0.00f, AmbiPulsarWaveform::Noise };
        p.lanes[1] = { 1460.0f, 0.20f, 0.60f, 0.28f, AmbiPulsarWaveform::Noise };
        p.lanes[2] = { 5200.0f, 0.12f, 0.44f, 0.63f, AmbiPulsarWaveform::Impulse };
        p.envelope = AmbiPulsarEnvelope::Percussive;
        p.envelopeEdge = 0.76f;
        p.spatialWidth = 1.0f;
        p.spatialScatter = 0.52f;
        p.orbitDepth = 0.82f;
        p.orbitRateHz = 0.093f;
        p.air = 0.16f;
        p.outputGainDb = -8.0f;
        break;
    case 0u:
    default:
        break;
    }
    return sanitizeAmbiPulsarParams(p);
}

} // namespace s3g
