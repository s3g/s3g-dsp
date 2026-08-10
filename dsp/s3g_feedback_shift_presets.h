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

constexpr std::array<FeedbackShiftPresetInfo, 11u>
    kFeedbackShiftPresetInfo {{
        { "INIT MATRIX", "Balanced loop with a quiet common source return" },
        { "CLOCKED RELAYS", "Gated relays meet a tight post-grain shadow" },
        { "BREAK SWARM", "Transient captures break into scattered grains" },
        { "DUB CIRCUIT", "Echo nodes bloom through a dark parallel return" },
        { "ERODED METAL", "Resonant abrasion splits into pitched grains" },
        { "FREEZE BRAID", "Long grains braid reverse and frozen memories" },
        { "DRUM BUS RITUAL", "Mostly dry AUX glue reinforces driven nodes" },
        { "NEGATIVE FIELD", "Inverse routes meet a restrained dark return" },
        { "ZERO BREACH", "Near-zero recursion with a guarded AUX injection" },
        { "HARSH WALL", "Pressed wall density recirculates without collapse" },
        { "MICRO GRAINS", "Tiny shared-clock fragments diverge across lanes" },
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

    const auto configureAux = [&](float press, float saturation, float fold,
        float clip, float size, float density, float scatter, float pitch,
        float edge, float coherence, float laneDrift, float grainMix,
        float tilt, float returnLevel) {
        params.auxPress = press;
        params.auxSaturation = saturation;
        params.auxFold = fold;
        params.auxClip = clip;
        params.auxGrainSize = size;
        params.auxGrainDensity = density;
        params.auxGrainScatter = scatter;
        params.auxGrainPitch = pitch;
        params.auxGrainEdge = edge;
        params.auxGrainCoherence = coherence;
        params.auxGrainLaneDrift = laneDrift;
        params.auxGrainMix = grainMix;
        params.auxTilt = tilt;
        params.auxMix = returnLevel;
    };
    const auto configureAuxSends = [&](std::array<float,
        kFeedbackShiftChannels> sends) {
        params.auxSend = sends;
    };

    if (index == 0u) {
        params = defaultFeedbackShiftParams();
        configureAux(0.22f, 0.12f, 0.0f, 0.0f,
            0.54f, 0.20f, 0.08f, 0.0f, 0.78f,
            0.90f, 0.15f, 0.08f, 0.0f, 0.06f);
        configureAuxSends({{
            0.28f, 0.24f, 0.20f, 0.16f,
            0.16f, 0.20f, 0.24f, 0.28f,
        }});
        return params;
    }

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
                0.75f, 0.60f, 0.30f + node * 0.07f,
                node & 1u ? 0.35f : -0.35f, 0.86f);
            feedbackShiftRoute(params, node, node, 0.72f);
            feedbackShiftRoute(params, node,
                (node + 7u) % kFeedbackShiftChannels,
                node & 1u ? -0.24f : 0.28f);
        }
        configureAux(0.25f, 0.18f, 0.06f, 0.08f,
            0.13f, 0.66f, 0.16f, 0.0f, 0.72f,
            0.75f, 0.35f, 0.28f, 0.08f, 0.10f);
        configureAuxSends({{
            1.00f, 0.25f, 0.75f, 0.20f,
            1.00f, 0.25f, 0.75f, 0.20f,
        }});
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
                0.78f, 0.35f + node * 0.06f,
                0.25f + node * 0.075f,
                node & 1u ? 0.52f : -0.46f, 0.82f);
            feedbackShiftRoute(params, node, node, 0.64f);
            feedbackShiftRoute(params, node, (node + 1u) % 8u, 0.30f);
            if ((node & 1u) == 0u)
                feedbackShiftRoute(params, node, (node + 5u) % 8u, -0.22f);
        }
        params.nodes[0u].pedalExtra[0u] = 1.0f;  // alternate
        params.nodes[0u].pedalExtra[1u] = 0.74f; // sensitivity
        params.nodes[0u].pedalExtra[2u] = 0.62f; // crossfade
        params.nodes[0u].pedalExtra[3u] = 0.61f; // upward capture drift
        params.nodes[0u].pedalExtra[4u] = 0.79f; // positive neighbor link
        params.nodes[4u].pedalExtra[0u] = 0.5f;  // freeze
        params.nodes[4u].pedalExtra[1u] = 0.67f;
        params.nodes[4u].pedalExtra[2u] = 0.70f;
        params.nodes[4u].pedalExtra[3u] = 0.43f; // downward capture drift
        params.nodes[4u].pedalExtra[4u] = 0.23f; // inverse neighbor link
        configureAux(0.34f, 0.28f, 0.10f, 0.12f,
            0.24f, 0.62f, 0.68f, 0.18f, 0.50f,
            0.38f, 0.78f, 0.48f, 0.10f, 0.12f);
        configureAuxSends({{
            0.90f, 0.45f, 0.70f, 0.35f,
            0.85f, 0.40f, 0.65f, 0.30f,
        }});
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
                0.71f, 0.34f + (node % 3u) * 0.16f,
                0.28f + node * 0.06f, -0.25f + node * 0.07f, 0.74f);
            feedbackShiftRoute(params, node, node, 0.59f);
            feedbackShiftRoute(params, node, (node + 3u) % 8u,
                node & 1u ? -0.22f : 0.27f);
        }
        configureAux(0.26f, 0.34f, 0.04f, 0.08f,
            0.64f, 0.27f, 0.22f, -0.08f, 0.84f,
            0.68f, 0.42f, 0.22f, -0.16f, 0.16f);
        configureAuxSends({{
            1.00f, 0.22f, 0.85f, 0.18f,
            0.72f, 0.16f, 0.62f, 0.14f,
        }});
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
                0.82f, 0.64f, 0.18f + node * 0.10f,
                node & 1u ? 0.72f : -0.58f, 0.88f);
            feedbackShiftRoute(params, node, node, 0.68f);
            feedbackShiftRoute(params, node, (node + 2u) % 8u, 0.23f);
        }
        configureAux(0.32f, 0.45f, 0.31f, 0.18f,
            0.30f, 0.47f, 0.36f, 0.16f, 0.56f,
            0.32f, 0.70f, 0.36f, 0.22f, 0.10f);
        configureAuxSends({{
            0.82f, 0.48f, 0.90f, 0.42f,
            0.76f, 0.52f, 0.68f, 0.38f,
        }});
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
                (node - 3.5f) * 4.2f, 0.75f,
                0.42f + 0.06f * node, 0.42f + 0.025f * node,
                node < 2u ? -0.72f : node < 5u ? 0.0f : 0.42f, 0.78f);
            feedbackShiftRoute(params, node, node, 0.57f);
            feedbackShiftRoute(params, node, (node + 5u) % 8u,
                node & 1u ? -0.24f : 0.29f);
            if (node < 5u) {
                params.nodes[node].pedalExtra[0u] = node == 0u ? 0.0f
                    : node == 3u ? 1.0f : 0.5f;
                params.nodes[node].pedalExtra[1u] = 0.55f
                    + static_cast<float>(node) * 0.045f;
                params.nodes[node].pedalExtra[2u] = 0.66f;
                params.nodes[node].pedalExtra[3u] = 0.42f
                    + static_cast<float>(node) * 0.04f;
                params.nodes[node].pedalExtra[4u] = node & 1u
                    ? 0.24f : 0.76f;
            }
        }
        configureAux(0.22f, 0.24f, 0.08f, 0.10f,
            0.76f, 0.21f, 0.74f, -0.24f, 0.90f,
            0.22f, 0.84f, 0.52f, -0.08f, 0.09f);
        configureAuxSends({{
            0.88f, 0.75f, 0.62f, 0.90f,
            0.68f, 0.55f, 0.44f, 0.32f,
        }});
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
                0.76f, 0.48f + (node % 4u) * 0.11f,
                0.26f + node * 0.08f,
                node & 1u ? 0.32f : -0.28f, 0.84f);
            feedbackShiftRoute(params, node, node, 0.65f);
            feedbackShiftRoute(params, node, (node + 7u) % 8u, 0.21f);
        }
        configureAux(0.56f, 0.50f, 0.18f, 0.24f,
            0.48f, 0.24f, 0.12f, 0.0f, 0.76f,
            0.85f, 0.20f, 0.08f, -0.10f, 0.17f);
        configureAuxSends({{
            0.80f, 0.48f, 0.56f, 0.72f,
            0.45f, 0.82f, 0.38f, 0.66f,
        }});
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
                0.84f, 0.55f, 0.20f + node * 0.09f,
                node & 1u ? 0.62f : -0.62f, 0.80f);
            params.nodes[node].mode = node & 1u
                ? FeedbackShiftMode::Ring : FeedbackShiftMode::Frequency;
            feedbackShiftRoute(params, node, node, 0.62f);
            feedbackShiftRoute(params, node, (node + 1u) % 8u, -0.34f);
            feedbackShiftRoute(params, node, (node + 4u) % 8u,
                node & 1u ? 0.22f : -0.20f);
        }
        configureAux(0.24f, 0.30f, 0.12f, 0.10f,
            0.42f, 0.36f, 0.44f, -0.14f, 0.68f,
            0.30f, 0.76f, 0.30f, -0.20f, 0.08f);
        configureAuxSends({{
            0.82f, 0.35f, 0.72f, 0.28f,
            0.64f, 0.30f, 0.56f, 0.24f,
        }});
        break;
    case 8u: {
        static constexpr std::array<float, 8u> frequencies {{
            -0.16f, -0.43f, -1.30f, -4.10f,
             0.16f,  0.43f,  1.30f,  4.10f,
        }};
        params.excite = 0.08f;
        params.drift = 0.055f;
        params.pulseDepth = 0.10f;
        params.pulseSync = 0u;
        params.pulseRate = 0.18f;
        params.pulseShape = FeedbackPulseShape::Sine;
        params.outputGainDb = -26.0f;
        for (uint32_t node = 0u; node < 8u; ++node) {
            configure(node, node == 2u || node == 6u
                    ? FeedbackPedalType::Filter
                    : FeedbackPedalType::Bypass,
                frequencies[node], 1.02f + 0.010f * node,
                0.62f, 0.28f + 0.07f * node,
                node & 1u ? 0.22f : -0.22f, 0.76f);
            params.nodes[node].color = 0.72f + 0.035f * node;
            params.nodes[node].levelDb = -9.0f;
            feedbackShiftRoute(params, node, node, 0.48f);
            feedbackShiftRoute(params, node, (node + 7u) % 8u,
                node & 1u ? -0.16f : 0.18f);
            if (node % 3u == 0u) {
                feedbackShiftRoute(params, node, (node + 4u) % 8u,
                    -0.11f);
            }
        }
        configureAux(0.46f, 0.48f, 0.18f, 0.24f,
            0.52f, 0.38f, 0.32f, -0.10f, 0.74f,
            0.72f, 0.30f, 0.16f, 0.06f, 0.04f);
        configureAuxSends({{
            0.22f, 0.12f, 0.18f, 0.10f,
            0.22f, 0.12f, 0.18f, 0.10f,
        }});
        break;
    }
    case 9u:
        params.excite = 0.22f;
        params.drift = 0.16f;
        params.pulseDepth = 0.05f;
        params.outputGainDb = -25.0f;
        for (uint32_t node = 0u; node < 8u; ++node) {
            configure(node, node & 1u ? FeedbackPedalType::Wool
                                      : FeedbackPedalType::Bypass,
                (node < 4u ? -1.0f : 1.0f) * (0.18f + node * 0.31f),
                0.86f, 0.68f, 0.54f, node & 1u ? 0.28f : -0.22f, 0.72f);
            params.nodes[node].color = 0.70f + 0.03f * node;
            feedbackShiftRoute(params, node, node, 0.61f);
            feedbackShiftRoute(params, node, (node + 7u) % 8u,
                node & 1u ? -0.14f : 0.17f);
        }
        configureAux(0.90f, 0.94f, 0.82f, 0.74f,
            0.46f, 0.58f, 0.50f, -0.08f, 0.78f,
            0.18f, 0.88f, 0.32f, 0.12f, 0.14f);
        configureAuxSends({{
            0.76f, 0.64f, 0.82f, 0.68f,
            0.88f, 0.72f, 0.94f, 0.78f,
        }});
        break;
    case 10u:
        params.excite = 0.18f;
        params.drift = 0.23f;
        params.pulseDepth = 0.0f;
        params.outputGainDb = -21.0f;
        for (uint32_t node = 0u; node < 8u; ++node) {
            configure(node, node % 3u == 0u ? FeedbackPedalType::Relay
                    : node % 3u == 1u ? FeedbackPedalType::Fold
                                      : FeedbackPedalType::Filter,
                (node - 3.5f) * 8.0f, 0.73f,
                0.38f + 0.04f * node, 0.22f + 0.07f * node,
                node & 1u ? 0.42f : -0.38f, 0.68f);
            feedbackShiftRoute(params, node, node, 0.57f);
            feedbackShiftRoute(params, node, (node + 3u) % 8u,
                node & 1u ? -0.20f : 0.24f);
        }
        configureAux(0.34f, 0.32f, 0.20f, 0.28f,
            0.05f, 0.78f, 0.92f, 0.42f, 0.30f,
            0.05f, 1.0f, 0.82f, 0.24f, 0.16f);
        configureAuxSends({{
            1.00f, 0.18f, 0.72f, 0.26f,
            0.90f, 0.14f, 0.64f, 0.22f,
        }});
        break;
    default: break;
    }
    params.motionRate = 0.20f
        + 0.035f * static_cast<float>(index % 7u);
    for (uint32_t node = 0u; node < kFeedbackShiftChannels; ++node) {
        if ((node + index) % 3u != 0u) continue;
        auto& lane = params.nodes[node];
        lane.motionSource = static_cast<FeedbackMotionSource>(
            1u + (node + index) % (kFeedbackMotionSourceCount - 1u));
        lane.motionTarget = static_cast<FeedbackMotionTarget>(
            (node * 2u + index) % kFeedbackMotionTargetCount);
        lane.motionDepth = (node & 1u ? -1.0f : 1.0f)
            * (0.12f + 0.025f * static_cast<float>(index % 6u));
        lane.motionSlew = 0.28f
            + 0.07f * static_cast<float>((node + index) % 6u);
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
    params.motionRate = 0.14f + feedbackShiftRandomUnit(random) * 0.58f;
    params.pulseDepth = feedbackShiftRandomUnit(random) * 0.72f;
    params.pulseRate = 0.18f + feedbackShiftRandomUnit(random) * 0.58f;
    params.pulseSync = feedbackShiftRandomUnit(random) > 0.35f ? 1u : 0u;
    params.pulseDivision = feedbackShiftRandomStep(random)
        % static_cast<uint32_t>(kFeedbackPulseDivisionBeats.size());
    params.pulseShape = static_cast<FeedbackPulseShape>(
        feedbackShiftRandomStep(random) % kFeedbackPulseShapeCount);
    params.outputGainDb = -24.0f + feedbackShiftRandomUnit(random) * 8.0f;
    params.auxPress = 0.18f + feedbackShiftRandomUnit(random) * 0.52f;
    params.auxSaturation = feedbackShiftRandomUnit(random) * 0.68f;
    params.auxFold = feedbackShiftRandomUnit(random) * 0.36f;
    params.auxClip = feedbackShiftRandomUnit(random) * 0.42f;
    params.auxGrainSize = 0.12f + feedbackShiftRandomUnit(random) * 0.62f;
    params.auxGrainDensity = 0.22f + feedbackShiftRandomUnit(random) * 0.68f;
    params.auxGrainScatter = feedbackShiftRandomUnit(random) * 0.82f;
    params.auxGrainPitch = feedbackShiftRandomUnit(random) * 1.4f - 0.7f;
    params.auxGrainEdge = 0.24f + feedbackShiftRandomUnit(random) * 0.70f;
    params.auxGrainCoherence = 0.08f
        + feedbackShiftRandomUnit(random) * 0.87f;
    params.auxGrainLaneDrift = 0.20f
        + feedbackShiftRandomUnit(random) * 0.80f;
    params.auxGrainMix = 0.10f + feedbackShiftRandomUnit(random) * 0.55f;
    params.auxTilt = feedbackShiftRandomUnit(random) * 0.7f - 0.35f;
    params.auxMix = 0.04f + feedbackShiftRandomUnit(random) * 0.13f;
    for (uint32_t node = 0u; node < kFeedbackShiftChannels; ++node) {
        auto& lane = params.nodes[node];
        params.auxSend[node] = 0.08f
            + feedbackShiftRandomUnit(random) * 0.84f;
        lane.mode = feedbackShiftRandomUnit(random) < 0.25f
            ? FeedbackShiftMode::Ring : FeedbackShiftMode::Frequency;
        const float sourceChoice = feedbackShiftRandomUnit(random);
        lane.exciterSource = sourceChoice < 0.56f
            ? FeedbackExciterSource::NoiseHit
            : sourceChoice < 0.72f ? FeedbackExciterSource::Noise
            : sourceChoice < 0.87f ? FeedbackExciterSource::Hit
                                   : FeedbackExciterSource::Tone;
        lane.exciterGainDb = -12.0f
            + feedbackShiftRandomUnit(random) * 15.0f;
        lane.motionSource = feedbackShiftRandomUnit(random) < 0.20f
            ? FeedbackMotionSource::Off
            : static_cast<FeedbackMotionSource>(1u
                + feedbackShiftRandomStep(random)
                    % (kFeedbackMotionSourceCount - 1u));
        lane.motionTarget = static_cast<FeedbackMotionTarget>(
            feedbackShiftRandomStep(random) % kFeedbackMotionTargetCount);
        lane.motionDepth = (feedbackShiftRandomUnit(random) * 2.0f - 1.0f)
            * (0.18f + feedbackShiftRandomUnit(random) * 0.52f);
        lane.motionSlew = 0.12f
            + feedbackShiftRandomUnit(random) * 0.78f;
        lane.pedal = static_cast<FeedbackPedalType>(1u
            + feedbackShiftRandomStep(random)
                % (kFeedbackPedalTypeCount - 1u));
        const float magnitude = 0.15f * std::pow(40000.0f,
            feedbackShiftRandomUnit(random));
        lane.frequencyHz = std::min(6000.0f, magnitude)
            * (feedbackShiftRandomUnit(random) < 0.5f ? -1.0f : 1.0f);
        const bool volatileShift = feedbackShiftRandomUnit(random) < 0.24f;
        lane.regeneration = volatileShift
            ? 0.98f + feedbackShiftRandomUnit(random) * 0.20f
            : 0.58f + feedbackShiftRandomUnit(random) * 0.38f;
        lane.color = volatileShift
            ? 0.68f + feedbackShiftRandomUnit(random) * 0.32f
            : 0.12f + feedbackShiftRandomUnit(random) * 0.76f;
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

// The title-band RANDOM action is allowed to rebuild the sound-generating
// ecology, but output topology remains a user/host routing decision.
inline FeedbackShiftParams randomFeedbackShiftParams(uint32_t seed,
    const FeedbackShiftParams& preservedOutput)
{
    auto params = randomFeedbackShiftParams(seed);
    params.outputGainDb = preservedOutput.outputGainDb;
    params.outputMode = preservedOutput.outputMode;
    params.outputRotationDeg = preservedOutput.outputRotationDeg;
    return params;
}

} // namespace s3g
