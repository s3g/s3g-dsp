#pragma once

#include "s3g_ambi_stochastic_encoder.h"

#include <algorithm>
#include <cstdint>

namespace s3g {

constexpr uint32_t kAmbiStochasticFactoryPresetCount = 13u;

struct AmbiStochasticFactoryPresetInfo {
    const char* name;
    const char* description;
};

inline AmbiStochasticFactoryPresetInfo ambiStochasticFactoryPresetInfo(uint32_t index)
{
    static constexpr AmbiStochasticFactoryPresetInfo kPresets[kAmbiStochasticFactoryPresetCount] {
        { "FREE PERIOD", "Broad autonomous pitch travel with persistent generator walks." },
        { "MICRO POINTS", "Fast, sharply articulated point events and rapid period changes." },
        { "PRESSURE BLOOM", "Slow linked curves with long envelopes and clustered motion." },
        { "HARSH POLYGONS", "Short binary pressure curves, rupture motion, and hard joins." },
        { "SPARSE SIGNALS", "Separated high-register events with deliberate resting fields." },
        { "MARKOV CLUSTERS", "Neighbor-coupled generator choices and converging voice groups." },
        { "METALLIC FLIGHT", "Bright inharmonic curves moving through an orbital field." },
        { "SUBHARMONIC TIDES", "Low, long-period pressure motion with slow distributed swells." },
        { "BINARY FRACTURE", "Extreme binary timing and amplitude walks with scattered motion." },
        { "SLOW CONSTELLATION", "Stable long-form pressure curves across a wide voice field." },
        { "MIDI PRESSURE", "MIDI-gated stochastic voices with a focused response envelope." },
        { "HYBRID FIELD", "MIDI and autonomous time-fields sharing a coupled topology." },
        { "FULL 7OA FIELD", "A 64-voice, seventh-order field demonstrating maximum scale." },
    };
    return kPresets[std::min<uint32_t>(index, kAmbiStochasticFactoryPresetCount - 1u)];
}

inline AmbiStochasticParams ambiStochasticFactoryPreset(uint32_t index)
{
    AmbiStochasticParams p {};
    switch (std::min<uint32_t>(index, kAmbiStochasticFactoryPresetCount - 1u)) {
    case 1u: // MICRO POINTS
        p.voices = 20u;
        p.selection = AmbiStochasticSelection::Walk;
        p.transition = AmbiStochasticTransition::Link;
        p.amplitudeDistribution = AmbiStochasticDistribution::Cauchy;
        p.durationDistribution = AmbiStochasticDistribution::Arcsine;
        p.baseNote = 52.0f;
        p.seedSpreadSemitones = 28.0f;
        p.detuneCents = 15.0f;
        p.frequencyFloorHz = 35.0f;
        p.breakpoints = 24u;
        p.amplitudeStep = 0.72f;
        p.durationStep = 0.88f;
        p.amplitudeRange = 0.76f;
        p.durationRange = 0.96f;
        p.fieldDensity = 0.82f;
        p.neighborTransfer = 0.18f;
        p.selectionMemory = 0.62f;
        p.fieldDurationSeconds = 0.14f;
        p.fieldContrast = 0.72f;
        p.fieldRestSeconds = 0.035f;
        p.macroDurationSeconds = 8.0f;
        p.attackMs = 3.0f;
        p.decayMs = 45.0f;
        p.sustain = 0.42f;
        p.releaseMs = 70.0f;
        p.topologyShape = 6u;
        p.topologyMotion = 8u;
        p.topologyRateHz = 0.18f;
        p.topologyAmount = 0.92f;
        p.topologyDepth = 0.92f;
        p.topologyScale = 1.30f;
        p.topologyTwist = 0.25f;
        p.outputGainDb = -28.0f;
        break;
    case 2u: // PRESSURE BLOOM
        p.voices = 16u;
        p.selection = AmbiStochasticSelection::Tendency;
        p.transition = AmbiStochasticTransition::Vary;
        p.amplitudeDistribution = AmbiStochasticDistribution::Gaussian;
        p.durationDistribution = AmbiStochasticDistribution::Logistic;
        p.baseNote = 36.0f;
        p.seedSpreadSemitones = 14.0f;
        p.detuneCents = 6.0f;
        p.frequencyFloorHz = 16.0f;
        p.breakpoints = 20u;
        p.amplitudeStep = 0.32f;
        p.durationStep = 0.28f;
        p.amplitudeRange = 0.52f;
        p.durationRange = 0.58f;
        p.fieldDensity = 0.88f;
        p.neighborTransfer = 0.66f;
        p.selectionMemory = 0.90f;
        p.fieldDurationSeconds = 3.50f;
        p.fieldContrast = 0.52f;
        p.fieldRestSeconds = 0.80f;
        p.macroDurationSeconds = 42.0f;
        p.attackMs = 600.0f;
        p.decayMs = 1200.0f;
        p.sustain = 0.90f;
        p.releaseMs = 4000.0f;
        p.topologyShape = 11u;
        p.topologyMotion = 2u;
        p.topologyRateHz = 0.018f;
        p.topologyAmount = 0.68f;
        p.topologyDepth = 0.52f;
        p.topologyScale = 1.16f;
        p.topologyCollapse = 0.28f;
        p.topologyTwist = 0.12f;
        p.outputGainDb = -26.0f;
        break;
    case 3u: // HARSH POLYGONS
        p.voices = 24u;
        p.selection = AmbiStochasticSelection::Random;
        p.transition = AmbiStochasticTransition::Merge;
        p.amplitudeDistribution = AmbiStochasticDistribution::Binary;
        p.durationDistribution = AmbiStochasticDistribution::Cauchy;
        p.baseNote = 48.0f;
        p.seedSpreadSemitones = 36.0f;
        p.detuneCents = 28.0f;
        p.frequencyFloorHz = 45.0f;
        p.breakpoints = 8u;
        p.amplitudeStep = 0.95f;
        p.durationStep = 0.90f;
        p.amplitudeRange = 1.0f;
        p.durationRange = 1.0f;
        p.fieldDensity = 0.78f;
        p.neighborTransfer = 0.12f;
        p.selectionMemory = 0.18f;
        p.fieldDurationSeconds = 0.11f;
        p.fieldContrast = 0.88f;
        p.fieldRestSeconds = 0.025f;
        p.macroDurationSeconds = 7.0f;
        p.attackMs = 1.0f;
        p.decayMs = 24.0f;
        p.sustain = 0.50f;
        p.releaseMs = 55.0f;
        p.topologyShape = 5u;
        p.topologyMotion = 10u;
        p.topologyRateHz = 0.32f;
        p.topologyAmount = 1.0f;
        p.topologyDepth = 1.0f;
        p.topologyScale = 1.45f;
        p.topologyTwist = 0.72f;
        p.outputGainDb = -34.0f;
        break;
    case 4u: // SPARSE SIGNALS
        p.voices = 10u;
        p.selection = AmbiStochasticSelection::Series;
        p.transition = AmbiStochasticTransition::Link;
        p.amplitudeDistribution = AmbiStochasticDistribution::Exponential;
        p.durationDistribution = AmbiStochasticDistribution::Exponential;
        p.baseNote = 62.0f;
        p.seedSpreadSemitones = 20.0f;
        p.detuneCents = 3.0f;
        p.frequencyFloorHz = 48.0f;
        p.breakpoints = 12u;
        p.amplitudeStep = 0.42f;
        p.durationStep = 0.74f;
        p.amplitudeRange = 0.62f;
        p.durationRange = 0.86f;
        p.fieldDensity = 0.26f;
        p.neighborTransfer = 0.15f;
        p.selectionMemory = 0.78f;
        p.fieldDurationSeconds = 1.20f;
        p.fieldContrast = 0.90f;
        p.fieldRestSeconds = 0.75f;
        p.macroDurationSeconds = 36.0f;
        p.attackMs = 4.0f;
        p.decayMs = 80.0f;
        p.sustain = 0.52f;
        p.releaseMs = 300.0f;
        p.topologyShape = 9u;
        p.topologyMotion = 15u;
        p.topologyRateHz = 0.055f;
        p.topologyAmount = 0.82f;
        p.topologyDepth = 0.75f;
        p.topologyScale = 1.22f;
        p.outputGainDb = -24.0f;
        break;
    case 5u: // MARKOV CLUSTERS
        p.voices = 32u;
        p.selection = AmbiStochasticSelection::Markov;
        p.transition = AmbiStochasticTransition::Vary;
        p.amplitudeDistribution = AmbiStochasticDistribution::Logistic;
        p.durationDistribution = AmbiStochasticDistribution::Arcsine;
        p.baseNote = 43.0f;
        p.seedSpreadSemitones = 26.0f;
        p.detuneCents = 12.0f;
        p.frequencyFloorHz = 28.0f;
        p.breakpoints = 18u;
        p.amplitudeStep = 0.60f;
        p.durationStep = 0.64f;
        p.amplitudeRange = 0.78f;
        p.durationRange = 0.82f;
        p.fieldDensity = 0.82f;
        p.neighborTransfer = 0.82f;
        p.selectionMemory = 0.88f;
        p.fieldDurationSeconds = 0.42f;
        p.fieldContrast = 0.80f;
        p.fieldRestSeconds = 0.18f;
        p.macroDurationSeconds = 18.0f;
        p.attackMs = 20.0f;
        p.decayMs = 260.0f;
        p.sustain = 0.74f;
        p.releaseMs = 520.0f;
        p.topologyShape = 4u;
        p.topologyMotion = 14u;
        p.topologyRateHz = 0.070f;
        p.topologyAmount = 0.88f;
        p.topologyDepth = 0.86f;
        p.topologyScale = 1.25f;
        p.topologyCollapse = 0.42f;
        p.topologyTwist = 0.18f;
        p.outputGainDb = -30.0f;
        break;
    case 6u: // METALLIC FLIGHT
        p.voices = 18u;
        p.selection = AmbiStochasticSelection::Weight;
        p.transition = AmbiStochasticTransition::Merge;
        p.amplitudeDistribution = AmbiStochasticDistribution::Arcsine;
        p.durationDistribution = AmbiStochasticDistribution::Binary;
        p.baseNote = 67.0f;
        p.seedSpreadSemitones = 34.0f;
        p.detuneCents = 22.0f;
        p.frequencyFloorHz = 75.0f;
        p.breakpoints = 10u;
        p.amplitudeStep = 0.82f;
        p.durationStep = 0.74f;
        p.amplitudeRange = 0.92f;
        p.durationRange = 0.90f;
        p.fieldDensity = 0.80f;
        p.neighborTransfer = 0.34f;
        p.selectionMemory = 0.52f;
        p.fieldDurationSeconds = 0.18f;
        p.fieldContrast = 0.70f;
        p.fieldRestSeconds = 0.06f;
        p.macroDurationSeconds = 12.0f;
        p.attackMs = 2.0f;
        p.decayMs = 90.0f;
        p.sustain = 0.46f;
        p.releaseMs = 120.0f;
        p.topologyShape = 3u;
        p.topologyMotion = 4u;
        p.topologyRateHz = 0.14f;
        p.topologyAmount = 0.94f;
        p.topologyDepth = 0.90f;
        p.topologyScale = 1.34f;
        p.topologyTwist = 0.65f;
        p.outputGainDb = -32.0f;
        break;
    case 7u: // SUBHARMONIC TIDES
        p.voices = 12u;
        p.selection = AmbiStochasticSelection::Tendency;
        p.transition = AmbiStochasticTransition::Vary;
        p.amplitudeDistribution = AmbiStochasticDistribution::Cauchy;
        p.durationDistribution = AmbiStochasticDistribution::Exponential;
        p.baseNote = 28.0f;
        p.seedSpreadSemitones = 12.0f;
        p.detuneCents = 7.0f;
        p.frequencyFloorHz = 10.0f;
        p.breakpoints = 28u;
        p.amplitudeStep = 0.38f;
        p.durationStep = 0.48f;
        p.amplitudeRange = 0.58f;
        p.durationRange = 0.86f;
        p.fieldDensity = 0.78f;
        p.neighborTransfer = 0.70f;
        p.selectionMemory = 0.90f;
        p.fieldDurationSeconds = 1.10f;
        p.fieldContrast = 0.58f;
        p.fieldRestSeconds = 0.90f;
        p.macroDurationSeconds = 52.0f;
        p.attackMs = 90.0f;
        p.decayMs = 650.0f;
        p.sustain = 0.86f;
        p.releaseMs = 1800.0f;
        p.topologyShape = 8u;
        p.topologyMotion = 13u;
        p.topologyRateHz = 0.018f;
        p.topologyAmount = 0.75f;
        p.topologyDepth = 0.64f;
        p.topologyScale = 1.10f;
        p.topologyTwist = -0.20f;
        p.outputGainDb = -24.0f;
        break;
    case 8u: // BINARY FRACTURE
        p.voices = 28u;
        p.selection = AmbiStochasticSelection::Walk;
        p.transition = AmbiStochasticTransition::Merge;
        p.amplitudeDistribution = AmbiStochasticDistribution::Binary;
        p.durationDistribution = AmbiStochasticDistribution::Binary;
        p.baseNote = 55.0f;
        p.seedSpreadSemitones = 40.0f;
        p.detuneCents = 36.0f;
        p.frequencyFloorHz = 55.0f;
        p.breakpoints = 6u;
        p.amplitudeStep = 1.0f;
        p.durationStep = 1.0f;
        p.amplitudeRange = 1.0f;
        p.durationRange = 1.0f;
        p.fieldDensity = 0.70f;
        p.neighborTransfer = 0.50f;
        p.selectionMemory = 0.35f;
        p.fieldDurationSeconds = 0.11f;
        p.fieldContrast = 0.95f;
        p.fieldRestSeconds = 0.04f;
        p.macroDurationSeconds = 9.0f;
        p.attackMs = 2.0f;
        p.decayMs = 35.0f;
        p.sustain = 0.62f;
        p.releaseMs = 80.0f;
        p.topologyShape = 7u;
        p.topologyMotion = 17u;
        p.topologyRateHz = 0.27f;
        p.topologyAmount = 1.0f;
        p.topologyDepth = 1.0f;
        p.topologyScale = 1.48f;
        p.topologyTwist = -0.55f;
        p.outputGainDb = -34.0f;
        break;
    case 9u: // SLOW CONSTELLATION
        p.voices = 40u;
        p.selection = AmbiStochasticSelection::Series;
        p.transition = AmbiStochasticTransition::Link;
        p.amplitudeDistribution = AmbiStochasticDistribution::Gaussian;
        p.durationDistribution = AmbiStochasticDistribution::Gaussian;
        p.baseNote = 45.0f;
        p.seedSpreadSemitones = 30.0f;
        p.detuneCents = 5.0f;
        p.frequencyFloorHz = 22.0f;
        p.breakpoints = 32u;
        p.amplitudeStep = 0.18f;
        p.durationStep = 0.16f;
        p.amplitudeRange = 0.32f;
        p.durationRange = 0.42f;
        p.fieldDensity = 1.0f;
        p.neighborTransfer = 0.88f;
        p.selectionMemory = 1.0f;
        p.fieldDurationSeconds = 4.0f;
        p.fieldContrast = 0.30f;
        p.fieldRestSeconds = 0.25f;
        p.macroDurationSeconds = 80.0f;
        p.attackMs = 1200.0f;
        p.decayMs = 2000.0f;
        p.sustain = 0.95f;
        p.releaseMs = 6000.0f;
        p.topologyShape = 0u;
        p.topologyMotion = 9u;
        p.topologyRateHz = 0.006f;
        p.topologyAmount = 0.60f;
        p.topologyDepth = 0.38f;
        p.topologyScale = 1.50f;
        p.outputGainDb = -32.0f;
        break;
    case 10u: // MIDI PRESSURE
        p.voices = 16u;
        p.mode = AmbiStochasticMode::Midi;
        p.selection = AmbiStochasticSelection::Markov;
        p.transition = AmbiStochasticTransition::Link;
        p.amplitudeDistribution = AmbiStochasticDistribution::Cauchy;
        p.durationDistribution = AmbiStochasticDistribution::Logistic;
        p.baseNote = 48.0f;
        p.seedSpreadSemitones = 7.0f;
        p.detuneCents = 4.0f;
        p.frequencyFloorHz = 18.0f;
        p.breakpoints = 16u;
        p.amplitudeStep = 0.52f;
        p.durationStep = 0.42f;
        p.amplitudeRange = 0.62f;
        p.durationRange = 0.65f;
        p.neighborTransfer = 0.35f;
        p.selectionMemory = 0.78f;
        p.fieldRestSeconds = 0.12f;
        p.macroDurationSeconds = 24.0f;
        p.attackMs = 8.0f;
        p.decayMs = 220.0f;
        p.sustain = 0.72f;
        p.releaseMs = 650.0f;
        p.topologyShape = 10u;
        p.topologyMotion = 0u;
        p.topologyAmount = 0.72f;
        p.topologyDepth = 0.0f;
        p.spatialFollow = 0.94f;
        p.outputGainDb = -24.0f;
        break;
    case 11u: // HYBRID FIELD
        p.voices = 24u;
        p.mode = AmbiStochasticMode::Both;
        p.selection = AmbiStochasticSelection::Weight;
        p.transition = AmbiStochasticTransition::Vary;
        p.amplitudeDistribution = AmbiStochasticDistribution::Gaussian;
        p.durationDistribution = AmbiStochasticDistribution::Cauchy;
        p.baseNote = 50.0f;
        p.seedSpreadSemitones = 24.0f;
        p.detuneCents = 11.0f;
        p.frequencyFloorHz = 32.0f;
        p.breakpoints = 20u;
        p.amplitudeStep = 0.65f;
        p.durationStep = 0.58f;
        p.amplitudeRange = 0.80f;
        p.durationRange = 0.80f;
        p.fieldDensity = 0.68f;
        p.neighborTransfer = 0.72f;
        p.selectionMemory = 0.70f;
        p.fieldDurationSeconds = 0.50f;
        p.fieldContrast = 0.65f;
        p.fieldRestSeconds = 0.16f;
        p.macroDurationSeconds = 20.0f;
        p.attackMs = 15.0f;
        p.decayMs = 300.0f;
        p.sustain = 0.76f;
        p.releaseMs = 700.0f;
        p.topologyShape = 11u;
        p.topologyMotion = 12u;
        p.topologyRateHz = 0.090f;
        p.topologyAmount = 0.86f;
        p.topologyDepth = 0.84f;
        p.topologyScale = 1.25f;
        p.topologyCollapse = 0.15f;
        p.topologyTwist = 0.30f;
        p.outputGainDb = -30.0f;
        break;
    case 12u: // FULL 7OA FIELD
        p.order = 7u;
        p.voices = 64u;
        p.selection = AmbiStochasticSelection::Walk;
        p.transition = AmbiStochasticTransition::Link;
        p.amplitudeDistribution = AmbiStochasticDistribution::Uniform;
        p.durationDistribution = AmbiStochasticDistribution::Logistic;
        p.baseNote = 42.0f;
        p.seedSpreadSemitones = 42.0f;
        p.detuneCents = 18.0f;
        p.frequencyFloorHz = 28.0f;
        p.breakpoints = 24u;
        p.amplitudeStep = 0.54f;
        p.durationStep = 0.56f;
        p.amplitudeRange = 0.72f;
        p.durationRange = 0.82f;
        p.fieldDensity = 0.82f;
        p.neighborTransfer = 0.64f;
        p.selectionMemory = 0.84f;
        p.fieldDurationSeconds = 0.38f;
        p.fieldContrast = 0.64f;
        p.fieldRestSeconds = 0.09f;
        p.macroDurationSeconds = 30.0f;
        p.attackMs = 18.0f;
        p.decayMs = 240.0f;
        p.sustain = 0.80f;
        p.releaseMs = 460.0f;
        p.topologyShape = 11u;
        p.topologyMotion = 1u;
        p.topologyRateHz = 0.028f;
        p.topologyAmount = 0.86f;
        p.topologyDepth = 0.76f;
        p.topologyScale = 1.36f;
        p.topologyTwist = 0.16f;
        p.outputGainDb = -38.0f;
        break;
    case 0u:
    default:
        break;
    }
    return p;
}

} // namespace s3g
