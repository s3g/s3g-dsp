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

// Each preset is a paired ecology rather than one static effects patch. Scene
// A remains the authored foothold; Scene B is the structurally related state
// reached by MORPH or its selected driver.
constexpr std::array<FeedbackShiftPresetInfo, 9u>
    kFeedbackShiftPresetInfo {{
        { "COLD START", "Sparse metal opens into a tense coupled body" },
        { "ZERO WEATHER", "Sub-Hz beating blooms into breathing instability" },
        { "RUSTED RING", "Dark shift cells become bright asymmetric rings" },
        { "IMPACT CAVITY", "Hit-driven clicks expand into resonant chambers" },
        { "EXTERNAL TEAR", "Eight external lanes move from clear to ruptured" },
        { "HARSH WALL", "Restrained abrasion grows into a dense signed wall" },
        { "MICRO CUTS", "Short metallic cuts scatter through a pulse driver" },
        { "MILLION SPLICES", "Hyper-dense virtual patches tear into sparse holes" },
        { "SUB FRACTURE", "Slow sub pulses split through changing fracture circuits" },
    }};

constexpr uint32_t kFeedbackShiftPresetCount =
    static_cast<uint32_t>(kFeedbackShiftPresetInfo.size());

inline void feedbackShiftRoute(std::array<float,
    kFeedbackShiftMatrixCells>& matrix, uint32_t destination,
    uint32_t source, float gain)
{
    if (destination < kFeedbackShiftChannels
        && source < kFeedbackShiftChannels) {
        matrix[destination * kFeedbackShiftChannels + source] = gain;
    }
}

inline void feedbackShiftConfigureScenes(FeedbackShiftParams& params,
    uint32_t node, float frequencyA, float frequencyB, float regenA,
    float regenB, float colorA, float colorB, float bodyA, float bodyB)
{
    auto& a = params.nodes[node];
    auto& b = params.sceneBNodes[node];
    a.frequencyHz = frequencyA;
    a.regeneration = regenA;
    a.color = colorA;
    a.body = bodyA;
    b = a;
    b.frequencyHz = frequencyB;
    b.regeneration = regenB;
    b.color = colorB;
    b.body = bodyB;
    b.levelDb = -7.5f;
}

inline FeedbackShiftParams feedbackShiftPreset(uint32_t index)
{
    index %= kFeedbackShiftPresetCount;
    auto params = defaultFeedbackShiftParams();
    params.matrix.fill(0.0f);
    params.sceneBMatrix.fill(0.0f);
    params.outputGainDb = -18.0f;
    params.morph = 0.0f;
    params.morphDepth = 1.0f;
    params.morphSource = FeedbackMorphSource::Manual;
    params.auxMix = 0.0f;
    params.auxGrainMix = 0.0f;
    params.spliceAmount = 0.58f;
    params.spliceRate = 0.62f;
    params.spliceContrast = 0.76f;
    params.spliceSpace = 0.24f;

    const auto connectPair = [&](uint32_t node, float selfA, float selfB,
                                 uint32_t offset, float crossA, float crossB) {
        feedbackShiftRoute(params.matrix, node, node, selfA);
        feedbackShiftRoute(params.sceneBMatrix, node, node, selfB);
        feedbackShiftRoute(params.matrix, node,
            (node + offset) % kFeedbackShiftChannels, crossA);
        feedbackShiftRoute(params.sceneBMatrix, node,
            (node + offset) % kFeedbackShiftChannels, crossB);
    };

    for (uint32_t node = 0u; node < kFeedbackShiftChannels; ++node) {
        params.nodes[node].pedal = FeedbackPedalType::Bypass;
        params.nodes[node].levelDb = -6.0f;
        params.sceneBNodes[node].levelDb = -7.5f;
        params.auxSend[node] = 0.18f;
        params.sceneBAuxSend[node] = 0.38f;
    }

    switch (index) {
    case 0u:
        params.spliceAmount = 0.44f;
        params.spliceRate = 0.58f;
        params.spliceContrast = 0.68f;
        params.spliceSpace = 0.20f;
        params.governorRest = 0.18f;
        for (uint32_t node = 0u; node < kFeedbackShiftChannels; ++node) {
            const float sign = node < 4u ? -1.0f : 1.0f;
            const float magnitude = 2.0f * std::pow(2.35f,
                static_cast<float>(node % 4u));
            feedbackShiftConfigureScenes(params, node,
                sign * magnitude, sign * magnitude * 9.0f,
                0.50f + node * 0.012f, 0.76f + (node % 3u) * 0.035f,
                -0.28f + node * 0.04f, 0.12f + node * 0.10f,
                0.12f + node * 0.035f, 0.52f + node * 0.055f);
            connectPair(node, 0.60f, 0.88f, 7u,
                node & 1u ? -0.08f : 0.10f,
                node & 1u ? -0.28f : 0.31f);
        }
        break;
    case 1u: {
        static constexpr std::array<float, 8u> frequencies {{
            -0.08f, -0.18f, -0.42f, -0.96f,
             0.08f,  0.18f,  0.42f,  0.96f,
        }};
        params.excite = 0.08f;
        params.drift = 0.04f;
        params.governorReflex = 0.68f;
        params.governorSensitivity = 0.36f;
        params.governorRecovery = 0.82f;
        params.governorRest = 0.42f;
        params.spliceAmount = 0.36f;
        params.spliceRate = 0.50f;
        params.spliceContrast = 0.58f;
        params.spliceSpace = 0.46f;
        params.morphSource = FeedbackMorphSource::Lfo;
        params.morphRate = 0.18f;
        params.morphDepth = 0.72f;
        params.morphInertia = 0.68f;
        params.outputGainDb = -23.0f;
        for (uint32_t node = 0u; node < 8u; ++node) {
            feedbackShiftConfigureScenes(params, node,
                frequencies[node], frequencies[(node + 3u) % 8u] * 4.0f,
                0.62f, 0.88f - (node % 3u) * 0.025f,
                -0.38f, node & 1u ? 0.68f : 0.42f,
                0.38f, 0.82f);
            connectPair(node, 0.54f, 0.84f, 1u,
                node & 1u ? -0.07f : 0.08f,
                node & 1u ? -0.23f : 0.26f);
        }
        break;
    }
    case 2u:
        params.drift = 0.18f;
        params.spliceAmount = 0.66f;
        params.spliceRate = 0.68f;
        params.spliceContrast = 0.86f;
        params.spliceSpace = 0.26f;
        params.governorRest = 0.28f;
        params.morphSource = FeedbackMorphSource::Chaos;
        params.morphRate = 0.30f;
        params.morphDepth = 0.84f;
        for (uint32_t node = 0u; node < 8u; ++node) {
            params.nodes[node].mode = node & 1u
                ? FeedbackShiftMode::Ring : FeedbackShiftMode::Frequency;
            const float sign = node & 1u ? -1.0f : 1.0f;
            feedbackShiftConfigureScenes(params, node,
                sign * (12.0f + node * 17.0f),
                sign * (0.7f + node * 3.1f),
                0.56f, 0.82f, -0.62f, 0.78f,
                0.22f, 0.72f);
            connectPair(node, 0.56f, 0.82f, 3u,
                node & 1u ? -0.12f : 0.14f,
                node & 1u ? 0.33f : -0.30f);
        }
        break;
    case 3u:
        params.excite = 0.04f;
        params.spliceAmount = 0.82f;
        params.spliceRate = 0.76f;
        params.spliceContrast = 0.92f;
        params.spliceSpace = 0.58f;
        params.morphSource = FeedbackMorphSource::Edge;
        params.morphDepth = 0.92f;
        params.morphInertia = 0.22f;
        params.governorReflex = 0.56f;
        params.governorSensitivity = 0.72f;
        params.governorRecovery = 0.24f;
        params.governorRest = 0.55f;
        for (uint32_t node = 0u; node < 8u; ++node) {
            params.nodes[node].exciterSource = FeedbackExciterSource::Hit;
            feedbackShiftConfigureScenes(params, node,
                (static_cast<float>(node) - 3.5f) * 52.0f,
                (static_cast<float>(node) - 3.5f) * 7.0f,
                0.46f, 0.84f, -0.18f, 0.54f,
                0.05f + node * 0.025f, 0.70f + node * 0.032f);
            connectPair(node, 0.42f, 0.86f, 5u, 0.04f,
                node & 1u ? -0.34f : 0.36f);
        }
        break;
    case 4u:
        params.excite = 0.0f;
        params.spliceAmount = 0.76f;
        params.spliceRate = 0.72f;
        params.spliceContrast = 0.94f;
        params.spliceSpace = 0.34f;
        params.governorRest = 0.35f;
        params.morphSource = FeedbackMorphSource::Envelope;
        params.morphDepth = 0.78f;
        params.morphInertia = 0.16f;
        for (uint32_t node = 0u; node < 8u; ++node) {
            params.nodes[node].exciterSource = FeedbackExciterSource::External;
            feedbackShiftConfigureScenes(params, node,
                (static_cast<float>(node) - 3.5f) * 32.0f,
                (node < 4u ? -1.0f : 1.0f)
                    * (0.14f + static_cast<float>(node % 4u) * 0.31f),
                0.24f, 0.81f, -0.12f, 0.72f,
                0.10f, 0.66f);
            connectPair(node, 0.26f, 0.78f, 7u, 0.0f,
                node & 1u ? -0.30f : 0.32f);
        }
        break;
    case 5u:
        params.excite = 0.19f;
        params.drift = 0.15f;
        params.spliceAmount = 0.52f;
        params.spliceRate = 0.64f;
        params.spliceContrast = 0.78f;
        params.spliceSpace = 0.08f;
        params.governorReflex = 0.16f;
        params.governorSensitivity = 0.20f;
        params.governorRecovery = 0.76f;
        params.governorRest = 0.22f;
        params.morphSource = FeedbackMorphSource::Chaos;
        params.morphRate = 0.25f;
        params.morphDepth = 0.66f;
        params.outputGainDb = -25.0f;
        params.auxPress = 0.72f;
        params.auxSaturation = 0.74f;
        params.auxFold = 0.48f;
        params.auxClip = 0.44f;
        params.auxMix = 0.10f;
        for (uint32_t node = 0u; node < 8u; ++node) {
            feedbackShiftConfigureScenes(params, node,
                (node < 4u ? -1.0f : 1.0f) * (0.2f + node * 0.37f),
                (static_cast<float>(node) - 3.5f) * 19.0f,
                0.60f, 0.89f, -0.26f, 0.88f,
                0.38f, 0.90f);
            connectPair(node, 0.58f, 0.92f, 1u,
                node & 1u ? -0.12f : 0.14f,
                node & 1u ? -0.38f : 0.40f);
            feedbackShiftRoute(params.sceneBMatrix, node,
                (node + 4u) % 8u, node & 1u ? 0.22f : -0.24f);
            params.sceneBAuxSend[node] = 0.62f + 0.04f * (node % 4u);
        }
        break;
    case 6u:
        params.excite = 0.12f;
        params.spliceAmount = 0.88f;
        params.spliceRate = 0.80f;
        params.spliceContrast = 0.96f;
        params.spliceSpace = 0.52f;
        params.governorRest = 0.50f;
        params.morphSource = FeedbackMorphSource::Pulse;
        params.morphRate = 0.48f;
        params.morphDepth = 1.0f;
        params.morphInertia = 0.03f;
        params.morphSync = 1u;
        params.morphDivision = 2u;
        params.morphShape = FeedbackPulseShape::Random;
        params.auxGrainSize = 0.04f;
        params.auxGrainDensity = 0.82f;
        params.auxGrainScatter = 0.88f;
        params.auxGrainPitch = 0.28f;
        params.auxGrainEdge = 0.24f;
        params.auxGrainCoherence = 0.12f;
        params.auxGrainLaneDrift = 0.94f;
        params.auxGrainMix = 0.38f;
        params.auxGrainSpacing = 0.23f;
        params.auxGrainShape = FeedbackGrainShape::Decay;
        for (uint32_t node = 0u; node < 8u; ++node) {
            feedbackShiftConfigureScenes(params, node,
                (static_cast<float>(node) - 3.5f) * 7.0f,
                (static_cast<float>(node) - 3.5f) * 83.0f,
                0.38f, 0.79f, -0.36f, 0.66f,
                0.02f + 0.02f * node, 0.34f + 0.06f * node);
            connectPair(node, 0.34f, 0.80f, 3u, 0.0f,
                node & 1u ? -0.34f : 0.37f);
        }
        break;
    case 7u:
        params.excite = 0.16f;
        params.drift = 0.02f;
        params.spliceAmount = 1.0f;
        params.spliceRate = 0.90f;
        params.spliceContrast = 1.0f;
        params.spliceSpace = 0.42f;
        params.governorReflex = 0.72f;
        params.governorSensitivity = 0.64f;
        params.governorRecovery = 0.18f;
        params.governorRest = 0.22f;
        params.morphSource = FeedbackMorphSource::Edge;
        params.morphDepth = 0.88f;
        params.morphInertia = 0.0f;
        params.outputGainDb = -27.0f;
        params.auxPress = 0.46f;
        params.auxSaturation = 0.58f;
        params.auxFold = 0.26f;
        params.auxClip = 0.18f;
        params.auxMix = 0.08f;
        for (uint32_t node = 0u; node < 8u; ++node) {
            params.nodes[node].mode = node & 1u
                ? FeedbackShiftMode::Ring : FeedbackShiftMode::Frequency;
            feedbackShiftConfigureScenes(params, node,
                (node < 4u ? -1.0f : 1.0f)
                    * (0.04f + 0.19f * static_cast<float>(node % 4u)),
                (static_cast<float>(node) - 3.5f) * 430.0f,
                0.34f + 0.04f * static_cast<float>(node % 3u),
                0.91f - 0.025f * static_cast<float>(node % 3u),
                node & 1u ? -0.82f : 0.76f,
                node & 1u ? 0.94f : -0.88f,
                0.02f + 0.015f * static_cast<float>(node),
                0.88f - 0.045f * static_cast<float>(node));
            connectPair(node, 0.28f, 0.86f, 3u,
                node & 1u ? -0.06f : 0.07f,
                node & 1u ? 0.48f : -0.46f);
            feedbackShiftRoute(params.sceneBMatrix, node,
                (node + 5u) % 8u, node & 1u ? -0.31f : 0.34f);
        }
        break;
    case 8u:
        params.excite = 0.02f;
        params.spliceAmount = 0.72f;
        params.spliceRate = 0.38f;
        params.spliceContrast = 0.90f;
        params.spliceSpace = 0.48f;
        params.subBassTune = 0.30f;
        params.subBassShape = 0.38f;
        params.subBassDrive = 0.62f;
        params.subBassDecay = 0.66f;
        params.subBassSustain = 0.08f;
        params.governorReflex = 0.54f;
        params.governorSensitivity = 0.48f;
        params.governorRecovery = 0.30f;
        params.governorRest = 0.38f;
        params.morphSource = FeedbackMorphSource::Edge;
        params.morphDepth = 0.82f;
        params.morphInertia = 0.04f;
        params.outputGainDb = -24.0f;
        for (uint32_t node = 0u; node < 8u; ++node) {
            params.nodes[node].exciterSource = node == 0u || node == 4u
                ? FeedbackExciterSource::SubBass
                : FeedbackExciterSource::Off;
            params.nodes[node].pedal = FeedbackPedalType::Fracture;
            params.nodes[node].pedalAmount = 0.60f
                + 0.04f * static_cast<float>(node % 4u);
            params.nodes[node].pedalTone = 0.28f
                + 0.09f * static_cast<float>(node % 5u);
            params.nodes[node].pedalBias = node & 1u ? -0.36f : 0.31f;
            params.nodes[node].pedalMix = 0.78f;
            params.nodes[node].pedalExtra[0u] = 0.62f;
            params.nodes[node].pedalExtra[1u] = 0.46f;
            params.nodes[node].pedalExtra[2u] = static_cast<float>(
                node % kFractureProcessorCount)
                / static_cast<float>(kFractureProcessorCount - 1u);
            feedbackShiftConfigureScenes(params, node,
                (node < 4u ? -1.0f : 1.0f)
                    * (0.06f + 0.23f * static_cast<float>(node % 4u)),
                (static_cast<float>(node) - 3.5f) * 72.0f,
                0.46f, 0.84f, -0.58f, 0.72f,
                0.08f + 0.025f * static_cast<float>(node),
                0.72f - 0.035f * static_cast<float>(node));
            connectPair(node, 0.42f, 0.82f, 4u,
                node & 1u ? -0.14f : 0.16f,
                node & 1u ? 0.38f : -0.40f);
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

// RANDOM is now a safe performance operation: preserve the user's Scene A,
// source choices, inserts, output topology, and manual morph position while
// mutating only Scene B into a related but more energetic destination.
inline FeedbackShiftParams randomFeedbackShiftParams(uint32_t seed,
    const FeedbackShiftParams& sceneA)
{
    uint32_t random = seed == 0u ? 0x51f15e3du : seed;
    auto params = sceneA;
    params.sceneBMatrix.fill(0.0f);
    for (uint32_t node = 0u; node < kFeedbackShiftChannels; ++node) {
        auto& b = params.sceneBNodes[node];
        b = params.nodes[node];
        const float magnitude = 0.08f * std::pow(60000.0f,
            feedbackShiftRandomUnit(random));
        b.frequencyHz = std::min(6000.0f, magnitude)
            * (feedbackShiftRandomUnit(random) < 0.5f ? -1.0f : 1.0f);
        b.regeneration = 0.68f + feedbackShiftRandomUnit(random) * 0.25f;
        b.color = feedbackShiftRandomUnit(random) * 1.7f - 0.75f;
        b.body = 0.28f + feedbackShiftRandomUnit(random) * 0.68f;
        b.levelDb = -12.0f + feedbackShiftRandomUnit(random) * 6.0f;
        params.sceneBAuxSend[node] = 0.10f
            + feedbackShiftRandomUnit(random) * 0.76f;
        feedbackShiftRoute(params.sceneBMatrix, node, node,
            0.62f + feedbackShiftRandomUnit(random) * 0.30f);
        const uint32_t neighbor = (node + 1u
            + feedbackShiftRandomStep(random) % 7u) % 8u;
        feedbackShiftRoute(params.sceneBMatrix, node, neighbor,
            (feedbackShiftRandomUnit(random) < 0.34f ? -1.0f : 1.0f)
                * (0.14f + feedbackShiftRandomUnit(random) * 0.30f));
        if (feedbackShiftRandomUnit(random) < 0.55f) {
            const uint32_t extra = feedbackShiftRandomStep(random) % 8u;
            feedbackShiftRoute(params.sceneBMatrix, node, extra,
                (feedbackShiftRandomUnit(random) < 0.5f ? -1.0f : 1.0f)
                    * (0.08f + feedbackShiftRandomUnit(random) * 0.22f));
        }
    }
    return params;
}

inline FeedbackShiftParams randomFeedbackShiftParams(uint32_t seed)
{
    return randomFeedbackShiftParams(seed, defaultFeedbackShiftParams());
}

// Randomize exactly one authored ecology. Shared node identity, sources,
// inserts, morph controls, output topology, and the opposite scene remain
// untouched. The existing generator supplies the constrained destination
// distribution; Scene A randomization copies only its continuous scene data
// back into A.
inline FeedbackShiftParams randomFeedbackShiftScene(uint32_t seed,
    const FeedbackShiftParams& current, bool sceneB)
{
    const auto generated = randomFeedbackShiftParams(seed, current);
    if (sceneB) return generated;

    auto result = current;
    for (uint32_t node = 0u; node < kFeedbackShiftChannels; ++node) {
        result.nodes[node].frequencyHz
            = generated.sceneBNodes[node].frequencyHz;
        result.nodes[node].regeneration
            = generated.sceneBNodes[node].regeneration;
        result.nodes[node].color = generated.sceneBNodes[node].color;
        result.nodes[node].body = generated.sceneBNodes[node].body;
        result.nodes[node].levelDb = generated.sceneBNodes[node].levelDb;
    }
    result.matrix = generated.sceneBMatrix;
    result.auxSend = generated.sceneBAuxSend;
    return result;
}

} // namespace s3g
