#pragma once

#include "s3g_feedback_shift.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

struct FeedbackShiftPresetInfo {
    const char* name;
    const char* description;
};

constexpr std::array<FeedbackShiftPresetInfo, 8u>
    kFeedbackShiftPresetInfo {{
        { "INIT MATRIX", "Balanced eight-node pedal loop" },
        { "CLOCKED RELAYS", "Gated pulse network with signed returns" },
        { "BREAK SWARM", "Transient repeaters, erosion and break bus" },
        { "DUB CIRCUIT", "Multi-head echoes in a sparse feedback web" },
        { "ERODED METAL", "Resonators and frequency-shifted abrasion" },
        { "FREEZE BRAID", "Decaying time captures with slow movement" },
        { "DRUM BUS RITUAL", "Console glue, folds and asymmetric drive" },
        { "NEGATIVE FIELD", "Cross-coupled inverse routes and ring nodes" },
    }};

constexpr uint32_t kFeedbackShiftPresetCount =
    static_cast<uint32_t>(kFeedbackShiftPresetInfo.size());

inline void feedbackShiftRoute(FeedbackShiftParams& params,
    uint32_t destination, uint32_t source, float gain)
{
    if (destination < kFeedbackShiftChannels
        && source < kFeedbackShiftChannels) {
        params.matrix[destination * kFeedbackShiftChannels + source] = gain;
    }
}

inline FeedbackShiftParams feedbackShiftPreset(uint32_t index)
{
    index %= kFeedbackShiftPresetCount;
    auto params = defaultFeedbackShiftParams();
    params.matrix.fill(0.0f);
    params.outputGainDb = -20.0f;

    const auto configure = [&](uint32_t node, FeedbackPedalType pedal,
        float frequency, float regeneration, float amount, float tone,
        float bias, float mix) {
        auto& lane = params.nodes[node];
        lane.pedal = pedal;
        lane.frequencyHz = frequency;
        lane.regeneration = regeneration;
        lane.pedalAmount = amount;
        lane.pedalTone = tone;
        lane.pedalBias = bias;
        lane.pedalMix = mix;
    };

    if (index == 0u) return defaultFeedbackShiftParams();

    switch (index) {
    case 1u:
        params.excite = 0.16f;
        params.drift = 0.04f;
        params.pulseDepth = 0.88f;
        params.pulseSync = 1u;
        params.pulseDivision = 3u;
        params.pulseShape = FeedbackPulseShape::Square;
        for (uint32_t node = 0u; node < kFeedbackShiftChannels; ++node) {
            configure(node, node % 3u == 0u ? FeedbackPedalType::Relay
                    : node % 3u == 1u ? FeedbackPedalType::Filter
                                      : FeedbackPedalType::Degrade,
                (node < 4u ? -1.0f : 1.0f)
                    * (3.0f + static_cast<float>(node * node) * 5.0f),
                0.78f, 0.60f, 0.30f + node * 0.07f,
                node & 1u ? 0.35f : -0.35f, 0.86f);
            feedbackShiftRoute(params, node, node, 0.76f);
            feedbackShiftRoute(params, node,
                (node + 7u) % kFeedbackShiftChannels,
                node & 1u ? -0.28f : 0.32f);
        }
        break;
    case 2u: {
        static constexpr std::array<FeedbackPedalType, 8u> pedals {{
            FeedbackPedalType::Repeater, FeedbackPedalType::Erosion,
            FeedbackPedalType::BreakBus, FeedbackPedalType::Transient,
            FeedbackPedalType::TimeMangler, FeedbackPedalType::Degrade,
            FeedbackPedalType::Fold, FeedbackPedalType::Resonator,
        }};
        params.excite = 0.32f;
        params.drift = 0.18f;
        params.pulseDepth = 0.42f;
        params.pulseSync = 0u;
        params.pulseRate = 0.56f;
        params.pulseShape = FeedbackPulseShape::Random;
        for (uint32_t node = 0u; node < 8u; ++node) {
            configure(node, pedals[node], (node - 3.5f) * 23.0f,
                0.82f, 0.35f + node * 0.06f,
                0.25f + node * 0.075f,
                node & 1u ? 0.52f : -0.46f, 0.82f);
            feedbackShiftRoute(params, node, node, 0.68f);
            feedbackShiftRoute(params, node, (node + 1u) % 8u, 0.34f);
            if ((node & 1u) == 0u)
                feedbackShiftRoute(params, node, (node + 5u) % 8u, -0.22f);
        }
        break;
    }
    case 3u:
        params.excite = 0.12f;
        params.drift = 0.22f;
        params.pulseDepth = 0.18f;
        params.pulseSync = 1u;
        params.pulseDivision = 5u;
        for (uint32_t node = 0u; node < 8u; ++node) {
            configure(node, node % 2u == 0u ? FeedbackPedalType::DrumEcho
                                            : FeedbackPedalType::Wool,
                (node < 4u ? -1.0f : 1.0f) * (0.7f + node * 2.8f),
                0.74f, 0.34f + (node % 3u) * 0.16f,
                0.28f + node * 0.06f, -0.25f + node * 0.07f, 0.74f);
            feedbackShiftRoute(params, node, node, 0.62f);
            feedbackShiftRoute(params, node, (node + 3u) % 8u,
                node & 1u ? -0.22f : 0.27f);
        }
        break;
    case 4u:
        params.excite = 0.22f;
        params.drift = 0.36f;
        params.pulseDepth = 0.16f;
        for (uint32_t node = 0u; node < 8u; ++node) {
            configure(node, node % 2u == 0u ? FeedbackPedalType::Resonator
                                            : FeedbackPedalType::Erosion,
                (node < 4u ? -1.0f : 1.0f)
                    * (18.0f * std::pow(1.8f, static_cast<float>(node))),
                0.86f, 0.64f, 0.18f + node * 0.10f,
                node & 1u ? 0.72f : -0.58f, 0.88f);
            feedbackShiftRoute(params, node, node, 0.74f);
            feedbackShiftRoute(params, node, (node + 2u) % 8u, 0.26f);
        }
        break;
    case 5u:
        params.excite = 0.18f;
        params.drift = 0.28f;
        params.pulseDepth = 0.32f;
        params.pulseSync = 1u;
        params.pulseDivision = 6u;
        params.pulseShape = FeedbackPulseShape::Ramp;
        for (uint32_t node = 0u; node < 8u; ++node) {
            configure(node, node < 5u ? FeedbackPedalType::TimeMangler
                                      : FeedbackPedalType::DrumEcho,
                (node - 3.5f) * 4.2f, 0.79f,
                0.42f + 0.06f * node, 0.42f + 0.025f * node,
                node < 2u ? -0.72f : node < 5u ? 0.0f : 0.42f, 0.78f);
            feedbackShiftRoute(params, node, node, 0.61f);
            feedbackShiftRoute(params, node, (node + 5u) % 8u,
                node & 1u ? -0.24f : 0.29f);
        }
        break;
    case 6u: {
        static constexpr std::array<FeedbackPedalType, 8u> pedals {{
            FeedbackPedalType::DrumBus, FeedbackPedalType::Fold,
            FeedbackPedalType::Rat, FeedbackPedalType::BreakBus,
            FeedbackPedalType::Diode, FeedbackPedalType::DrumBus,
            FeedbackPedalType::Wool, FeedbackPedalType::Transient,
        }};
        params.excite = 0.27f;
        params.pulseDepth = 0.51f;
        params.pulseSync = 1u;
        params.pulseDivision = 2u;
        for (uint32_t node = 0u; node < 8u; ++node) {
            configure(node, pedals[node], (node - 3.5f) * 41.0f,
                0.80f, 0.48f + (node % 4u) * 0.11f,
                0.26f + node * 0.08f,
                node & 1u ? 0.32f : -0.28f, 0.84f);
            feedbackShiftRoute(params, node, node, 0.70f);
            feedbackShiftRoute(params, node, (node + 7u) % 8u, 0.24f);
        }
        break;
    }
    case 7u:
        params.excite = 0.20f;
        params.drift = 0.14f;
        params.pulseDepth = 0.26f;
        params.pulseSync = 0u;
        params.pulseRate = 0.31f;
        params.pulseShape = FeedbackPulseShape::Sine;
        for (uint32_t node = 0u; node < 8u; ++node) {
            configure(node, node % 2u == 0u ? FeedbackPedalType::Phase
                                            : FeedbackPedalType::Filter,
                (node < 4u ? -1.0f : 1.0f)
                    * (2.0f + std::pow(2.2f, static_cast<float>(node))),
                0.89f, 0.55f, 0.20f + node * 0.09f,
                node & 1u ? 0.62f : -0.62f, 0.80f);
            params.nodes[node].mode = node & 1u
                ? FeedbackShiftMode::Ring : FeedbackShiftMode::Frequency;
            feedbackShiftRoute(params, node, node, 0.68f);
            feedbackShiftRoute(params, node, (node + 1u) % 8u, -0.38f);
            feedbackShiftRoute(params, node, (node + 4u) % 8u,
                node & 1u ? 0.22f : -0.20f);
        }
        break;
    default: break;
    }
    return params;
}

inline uint32_t feedbackShiftRandomStep(uint32_t& state)
{
    state ^= state << 13u;
    state ^= state >> 17u;
    state ^= state << 5u;
    if (state == 0u) state = 0xa341316cu;
    return state;
}

inline float feedbackShiftRandomUnit(uint32_t& state)
{
    return static_cast<float>(feedbackShiftRandomStep(state) & 0x00ffffffu)
        / 16777215.0f;
}

// Constrained patch randomization: sparse routes, restrained output level and
// bounded regeneration create a playable starting point instead of a maximum-
// energy test case. The seed makes each result reproducible in tests and logs.
inline FeedbackShiftParams randomFeedbackShiftParams(uint32_t seed)
{
    uint32_t random = seed == 0u ? 0x51f15e3du : seed;
    auto params = defaultFeedbackShiftParams();
    params.matrix.fill(0.0f);
    params.excite = 0.10f + feedbackShiftRandomUnit(random) * 0.28f;
    params.drift = feedbackShiftRandomUnit(random) * 0.42f;
    params.pulseDepth = feedbackShiftRandomUnit(random) * 0.72f;
    params.pulseRate = 0.18f + feedbackShiftRandomUnit(random) * 0.58f;
    params.pulseSync = feedbackShiftRandomUnit(random) > 0.35f ? 1u : 0u;
    params.pulseDivision = feedbackShiftRandomStep(random)
        % static_cast<uint32_t>(kFeedbackPulseDivisionBeats.size());
    params.pulseShape = static_cast<FeedbackPulseShape>(
        feedbackShiftRandomStep(random) % kFeedbackPulseShapeCount);
    params.outputGainDb = -24.0f + feedbackShiftRandomUnit(random) * 8.0f;
    for (uint32_t node = 0u; node < kFeedbackShiftChannels; ++node) {
        auto& lane = params.nodes[node];
        lane.mode = feedbackShiftRandomUnit(random) < 0.25f
            ? FeedbackShiftMode::Ring : FeedbackShiftMode::Frequency;
        lane.pedal = static_cast<FeedbackPedalType>(1u
            + feedbackShiftRandomStep(random)
                % (kFeedbackPedalTypeCount - 1u));
        const float magnitude = 0.15f * std::pow(40000.0f,
            feedbackShiftRandomUnit(random));
        lane.frequencyHz = std::min(6000.0f, magnitude)
            * (feedbackShiftRandomUnit(random) < 0.5f ? -1.0f : 1.0f);
        lane.regeneration = 0.58f + feedbackShiftRandomUnit(random) * 0.38f;
        lane.color = 0.12f + feedbackShiftRandomUnit(random) * 0.76f;
        lane.levelDb = -12.0f + feedbackShiftRandomUnit(random) * 8.0f;
        lane.pedalAmount = 0.18f + feedbackShiftRandomUnit(random) * 0.70f;
        lane.pedalTone = 0.10f + feedbackShiftRandomUnit(random) * 0.80f;
        lane.pedalBias = feedbackShiftRandomUnit(random) * 1.5f - 0.75f;
        lane.pedalMix = 0.42f + feedbackShiftRandomUnit(random) * 0.50f;
        for (float& extra : lane.pedalExtra) {
            extra = 0.08f + feedbackShiftRandomUnit(random) * 0.84f;
        }
        feedbackShiftRoute(params, node, node,
            0.48f + feedbackShiftRandomUnit(random) * 0.34f);
        const uint32_t neighbor = (node + 1u
            + feedbackShiftRandomStep(random) % 7u) % 8u;
        feedbackShiftRoute(params, node, neighbor,
            (feedbackShiftRandomUnit(random) < 0.28f ? -1.0f : 1.0f)
                * (0.12f + feedbackShiftRandomUnit(random) * 0.38f));
        if (feedbackShiftRandomUnit(random) < 0.45f) {
            const uint32_t extra = feedbackShiftRandomStep(random) % 8u;
            feedbackShiftRoute(params, node, extra,
                (feedbackShiftRandomUnit(random) < 0.34f ? -1.0f : 1.0f)
                    * (0.08f + feedbackShiftRandomUnit(random) * 0.24f));
        }
    }
    return params;
}

} // namespace s3g
