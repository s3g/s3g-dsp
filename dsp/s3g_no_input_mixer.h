#pragma once

#include "s3g_math.h"
#include "s3g_matrix_flow_shapes.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

constexpr uint32_t kNoInputMixerChannels = 8u;
constexpr uint32_t kNoInputMixerInsertSlots = 3u;
constexpr uint32_t kNoInputMixerMatrixCells =
    kNoInputMixerChannels * kNoInputMixerChannels;

enum class NoInputDistortionType : uint32_t {
    Bypass = 0u,
    Wool,
    Rat,
    ZoneA,
    ZoneB,
    FuzzI,
    FuzzII,
    Diode,
    Ring,
    Count,
};

constexpr uint32_t kNoInputDistortionTypeCount =
    static_cast<uint32_t>(NoInputDistortionType::Count);

inline const char* noInputDistortionName(NoInputDistortionType type)
{
    switch (type) {
    case NoInputDistortionType::Bypass: return "BYPASS";
    case NoInputDistortionType::Wool: return "WOOL";
    case NoInputDistortionType::Rat: return "RAT";
    case NoInputDistortionType::ZoneA: return "ZONE A";
    case NoInputDistortionType::ZoneB: return "ZONE B";
    case NoInputDistortionType::FuzzI: return "FUZZ I";
    case NoInputDistortionType::FuzzII: return "FUZZ II";
    case NoInputDistortionType::Diode: return "DIODE";
    case NoInputDistortionType::Ring: return "RING";
    case NoInputDistortionType::Count: break;
    }
    return "BYPASS";
}

struct NoInputInsertParams {
    NoInputDistortionType type = NoInputDistortionType::Bypass;
    float gain = 0.35f;
    float tone = 0.50f;
    float bias = 0.0f;
    float levelDb = 0.0f;
    uint32_t bypass = 0u;
};

struct NoInputAuxParams {
    NoInputInsertParams effect {};
    float feedback = 0.24f;
    float returnGain = 0.18f;
};

struct NoInputLaneParams {
    float body = 0.50f;
    float loss = 0.38f;
    float levelDb = -3.0f;
    uint32_t mute = 0u;
    float lowDb = 0.0f;
    float midFrequencyHz = 850.0f;
    float midGainDb = 0.0f;
    float highDb = 0.0f;
    std::array<float, 2u> auxSend {{ 0.0f, 0.0f }};
    std::array<NoInputInsertParams, kNoInputMixerInsertSlots> inserts {};
};

struct NoInputMixerParams {
    float outputGainDb = -18.0f;
    float ceilingDb = -1.0f;
    uint32_t limiterEnabled = 1u;
    uint32_t dcBlockEnabled = 1u;
    float feedback = 0.82f;
    float coupling = 0.42f;
    float phase = 0.34f;
    float drift = 0.18f;
    float formant = 0.30f;
    float agency = 0.28f;
    float space = 0.10f;
    float variance = 0.12f;
    float internalTone = 0.0f;
    float houseTone = -0.08f;
    float flow = 0.42f;
    float spread = 0.36f;
    float vortex = 0.0f;
    float motion = 0.0f;
    MatrixFlowShape motionShape = MatrixFlowShape::Flow;
    float motionRate = 0.15f;
    float motionPhase = 0.0f;
    uint32_t quality = 1u;
    uint32_t seed = 0x5455444fu;
    std::array<NoInputAuxParams, 2u> aux {};
    std::array<float, kNoInputMixerMatrixCells> matrix {};
    std::array<NoInputLaneParams, kNoInputMixerChannels> lanes {};
};

inline NoInputMixerParams defaultNoInputMixerParams()
{
    NoInputMixerParams params;
    params.aux[0].effect.type = NoInputDistortionType::Diode;
    params.aux[0].effect.gain = 0.18f;
    params.aux[0].effect.tone = 0.34f;
    params.aux[0].feedback = 0.28f;
    params.aux[0].returnGain = 0.16f;
    params.aux[1].effect.type = NoInputDistortionType::Ring;
    params.aux[1].effect.gain = 0.22f;
    params.aux[1].effect.tone = 0.58f;
    params.aux[1].feedback = 0.18f;
    params.aux[1].returnGain = 0.12f;
    for (uint32_t destination = 0u;
         destination < kNoInputMixerChannels; ++destination) {
        for (uint32_t source = 0u;
             source < kNoInputMixerChannels; ++source) {
            params.matrix[destination * kNoInputMixerChannels + source] =
                destination == source ? 0.94f : 0.0f;
        }
        const uint32_t previous =
            (destination + kNoInputMixerChannels - 1u)
            % kNoInputMixerChannels;
        params.matrix[destination * kNoInputMixerChannels + previous] =
            (destination & 1u) == 0u ? 0.12f : -0.10f;

        auto& lane = params.lanes[destination];
        lane.body = 0.28f + 0.055f * static_cast<float>(destination);
        lane.loss = 0.31f + 0.025f * static_cast<float>(destination % 3u);
        lane.levelDb = -4.5f;
        lane.auxSend[0] = 0.08f + 0.015f
            * static_cast<float>(destination % 4u);
        lane.auxSend[1] = 0.04f + 0.012f
            * static_cast<float>((destination + 2u) % 4u);
        lane.midFrequencyHz = 420.0f
            * std::pow(1.24f, static_cast<float>(destination));

        lane.inserts[0].type = NoInputDistortionType::Wool;
        lane.inserts[0].gain = 0.26f + 0.018f * static_cast<float>(destination);
        lane.inserts[0].tone = 0.38f + 0.045f * static_cast<float>(destination % 4u);
        lane.inserts[0].levelDb = -8.0f;
        lane.inserts[1].type = NoInputDistortionType::Rat;
        lane.inserts[1].gain = 0.24f;
        lane.inserts[1].tone = 0.58f;
        lane.inserts[1].levelDb = -5.0f;
        lane.inserts[1].bypass = 1u;
        lane.inserts[2].type = NoInputDistortionType::ZoneA;
        lane.inserts[2].gain = 0.20f;
        lane.inserts[2].tone = 0.52f;
        lane.inserts[2].levelDb = -6.0f;
        lane.inserts[2].bypass = 1u;
    }
    return params;
}

inline NoInputInsertParams sanitizeNoInputInsertParams(
    NoInputInsertParams params)
{
    const uint32_t type = std::min<uint32_t>(
        static_cast<uint32_t>(params.type),
        kNoInputDistortionTypeCount - 1u);
    params.type = static_cast<NoInputDistortionType>(type);
    params.gain = clamp(std::isfinite(params.gain) ? params.gain : 0.0f,
        0.0f, 1.0f);
    params.tone = clamp(std::isfinite(params.tone) ? params.tone : 0.5f,
        0.0f, 1.0f);
    params.bias = clamp(std::isfinite(params.bias) ? params.bias : 0.0f,
        -1.0f, 1.0f);
    params.levelDb = clamp(
        std::isfinite(params.levelDb) ? params.levelDb : 0.0f,
        -24.0f, 12.0f);
    params.bypass = params.bypass != 0u ? 1u : 0u;
    return params;
}

inline NoInputMixerParams sanitizeNoInputMixerParams(
    NoInputMixerParams params)
{
    params.outputGainDb = clamp(
        std::isfinite(params.outputGainDb) ? params.outputGainDb : -18.0f,
        -60.0f, 6.0f);
    params.ceilingDb = clamp(
        std::isfinite(params.ceilingDb) ? params.ceilingDb : -1.0f,
        -18.0f, 0.0f);
    params.limiterEnabled = params.limiterEnabled != 0u ? 1u : 0u;
    params.dcBlockEnabled = params.dcBlockEnabled != 0u ? 1u : 0u;
    params.feedback = clamp(
        std::isfinite(params.feedback) ? params.feedback : 0.82f,
        0.0f, 1.25f);
    params.coupling = clamp(
        std::isfinite(params.coupling) ? params.coupling : 0.42f,
        0.0f, 1.25f);
    params.phase = clamp(std::isfinite(params.phase) ? params.phase : 0.34f,
        0.0f, 1.0f);
    params.drift = clamp(std::isfinite(params.drift) ? params.drift : 0.18f,
        0.0f, 1.0f);
    params.formant = clamp(
        std::isfinite(params.formant) ? params.formant : 0.30f,
        0.0f, 1.0f);
    params.agency = clamp(
        std::isfinite(params.agency) ? params.agency : 0.28f,
        0.0f, 1.0f);
    params.space = clamp(
        std::isfinite(params.space) ? params.space : 0.10f,
        0.0f, 1.0f);
    params.variance = clamp(
        std::isfinite(params.variance) ? params.variance : 0.12f,
        0.0f, 1.0f);
    params.internalTone = clamp(
        std::isfinite(params.internalTone) ? params.internalTone : 0.0f,
        -1.0f, 1.0f);
    params.houseTone = clamp(
        std::isfinite(params.houseTone) ? params.houseTone : -0.08f,
        -1.0f, 1.0f);
    params.flow = clamp(std::isfinite(params.flow) ? params.flow : 0.42f,
        0.0f, 1.0f);
    params.spread = clamp(
        std::isfinite(params.spread) ? params.spread : 0.36f,
        0.0f, 1.0f);
    params.vortex = clamp(
        std::isfinite(params.vortex) ? params.vortex : 0.0f,
        -1.0f, 1.0f);
    params.motion = clamp(
        std::isfinite(params.motion) ? params.motion : 0.0f,
        0.0f, 1.0f);
    params.motionShape = matrixFlowShapeFromIndex(
        static_cast<uint32_t>(params.motionShape));
    params.motionRate = clamp(
        std::isfinite(params.motionRate) ? params.motionRate : 0.15f,
        0.0f, 1.0f);
    params.motionPhase = clamp(
        std::isfinite(params.motionPhase) ? params.motionPhase : 0.0f,
        0.0f, 1.0f);
    params.quality = std::min<uint32_t>(params.quality, 2u);
    if (params.seed == 0u) params.seed = 1u;
    for (float& value : params.matrix) {
        value = clamp(std::isfinite(value) ? value : 0.0f, -1.0f, 1.0f);
    }
    for (auto& lane : params.lanes) {
        lane.body = clamp(std::isfinite(lane.body) ? lane.body : 0.5f,
            0.0f, 1.0f);
        lane.loss = clamp(std::isfinite(lane.loss) ? lane.loss : 0.38f,
            0.0f, 1.0f);
        lane.levelDb = clamp(
            std::isfinite(lane.levelDb) ? lane.levelDb : -3.0f,
            -60.0f, 12.0f);
        lane.mute = lane.mute != 0u ? 1u : 0u;
        lane.lowDb = clamp(std::isfinite(lane.lowDb) ? lane.lowDb : 0.0f,
            -18.0f, 18.0f);
        lane.midFrequencyHz = clamp(
            std::isfinite(lane.midFrequencyHz)
                ? lane.midFrequencyHz : 850.0f,
            80.0f, 8000.0f);
        lane.midGainDb = clamp(
            std::isfinite(lane.midGainDb) ? lane.midGainDb : 0.0f,
            -18.0f, 18.0f);
        lane.highDb = clamp(
            std::isfinite(lane.highDb) ? lane.highDb : 0.0f,
            -18.0f, 18.0f);
        for (float& send : lane.auxSend) {
            send = clamp(std::isfinite(send) ? send : 0.0f,
                0.0f, 1.0f);
        }
        for (auto& insert : lane.inserts) {
            insert = sanitizeNoInputInsertParams(insert);
        }
    }
    for (auto& aux : params.aux) {
        aux.effect = sanitizeNoInputInsertParams(aux.effect);
        aux.effect.bypass = 0u;
        aux.effect.levelDb = 0.0f;
        aux.feedback = clamp(
            std::isfinite(aux.feedback) ? aux.feedback : 0.24f,
            0.0f, 0.96f);
        aux.returnGain = clamp(
            std::isfinite(aux.returnGain) ? aux.returnGain : 0.18f,
            0.0f, 1.0f);
    }
    return params;
}

inline float noInputWrapPhase(float phase)
{
    phase = std::fmod(phase, 1.0f);
    return phase < 0.0f ? phase + 1.0f : phase;
}

constexpr float kNoInputMixerMotionRangeDb = 30.0f;

inline float noInputMixerMotionRateHz(float normalizedRate)
{
    normalizedRate = clamp(std::isfinite(normalizedRate)
        ? normalizedRate : 0.0f, 0.0f, 1.0f);
    return 0.05f * std::pow(100.0f, normalizedRate);
}

inline float noInputMixerMotionGainScale(float generatedWeight,
    float activePeakWeight, uint32_t activeRouteCount, float depth)
{
    depth = clamp(std::isfinite(depth) ? depth : 0.0f, 0.0f, 1.0f);
    if (depth <= 1.0e-6f) return 1.0f;
    generatedWeight = clamp(std::isfinite(generatedWeight)
        ? generatedWeight : 0.0f, 0.0f, 1.0f);
    activePeakWeight = clamp(std::isfinite(activePeakWeight)
        ? activePeakWeight : 0.0f, 0.0f, 1.0f);
    const float focusedWeight = activeRouteCount > 1u
        ? generatedWeight / std::max(1.0e-6f, activePeakWeight)
        : generatedWeight;
    const float perceptualWeight = std::pow(
        clamp(focusedWeight, 0.0f, 1.0f), 1.35f);
    const float attenuationDb = -(1.0f - perceptualWeight)
        * depth * kNoInputMixerMotionRangeDb;
    return dbToGain(attenuationDb);
}

// The same generated-flow vocabulary used by the s3g Group Matrix family.
// The caller multiplies these weights by the hand-patched matrix, so a closed
// crosspoint stays closed while an active circuit can move inside its ceiling.
inline std::array<float, kNoInputMixerMatrixCells>
noInputMixerMotionWeights(const NoInputMixerParams& rawParams, float phase)
{
    const auto params = sanitizeNoInputMixerParams(rawParams);
    std::array<float, kNoInputMixerMatrixCells> weights {};
    const float phaseForShape = params.motionShape == MatrixFlowShape::Hold
        ? params.motionPhase
        : noInputWrapPhase(phase + params.motionPhase);
    const bool swirlShape = params.motionShape == MatrixFlowShape::Swirl;
    const float flowAmount = swirlShape
        ? std::max(0.24f, params.flow) : params.flow;
    const float width = 0.045f + params.spread * 1.25f
        + flowAmount * 0.58f;
    const float angle = phaseForShape * 2.0f * kPi;

    if (params.motionShape == MatrixFlowShape::Pulse) {
        for (uint32_t destination = 0u; destination < 8u; ++destination) {
            for (uint32_t source = 0u; source < 8u; ++source) {
                const float offset = static_cast<float>(source + destination)
                    * 0.125f * params.spread;
                weights[destination * 8u + source] =
                    matrixFlowPulse(phaseForShape + offset);
            }
        }
        return weights;
    }

    if (params.motionShape == MatrixFlowShape::Chase) {
        const float chaseWidth = 0.35f + params.spread * 2.4f;
        const float position = phaseForShape * 8.0f;
        for (uint32_t source = 0u; source < 8u; ++source) {
            float sum = 0.0f;
            const float center = std::fmod(
                static_cast<float>(source) + position, 8.0f);
            for (uint32_t destination = 0u; destination < 8u;
                 ++destination) {
                const float value = matrixFlowRingWeight(
                    8u, destination, center, chaseWidth);
                weights[destination * 8u + source] = value;
                sum += value;
            }
            if (sum > 1.0e-6f) {
                for (uint32_t destination = 0u; destination < 8u;
                     ++destination) {
                    weights[destination * 8u + source] /= sum;
                }
            }
        }
        return weights;
    }

    if (params.motionShape == MatrixFlowShape::Scatter) {
        const float step = phaseForShape * 12.0f;
        const uint32_t seedA = static_cast<uint32_t>(std::floor(step));
        const uint32_t seedB = seedA + 1u;
        const float interpolation = 0.5f - 0.5f
            * std::cos((step - std::floor(step)) * kPi);
        const float threshold = 0.74f - params.spread * 0.42f;
        for (uint32_t source = 0u; source < 8u; ++source) {
            float sum = 0.0f;
            for (uint32_t destination = 0u; destination < 8u;
                 ++destination) {
                const float a = matrixFlowHash01(source, destination, seedA);
                const float b = matrixFlowHash01(source, destination, seedB);
                const float hash = lerp(a, b, interpolation);
                const float value = 0.04f + std::max(0.0f,
                    (hash - threshold) / std::max(0.05f, 1.0f - threshold));
                weights[destination * 8u + source] = value;
                sum += value;
            }
            if (sum > 1.0e-6f) {
                for (uint32_t destination = 0u; destination < 8u;
                     ++destination) {
                    weights[destination * 8u + source] /= sum;
                }
            }
        }
        return weights;
    }

    const float vortex = params.vortex + (swirlShape ? 1.35f : 0.0f);
    const float orbitX = std::cos(angle) * flowAmount
        * (swirlShape ? 1.12f : 1.0f);
    const float orbitY = std::sin(angle) * flowAmount
        * (swirlShape ? 1.12f : 1.0f);
    for (uint32_t source = 0u; source < 8u; ++source) {
        const float sourceAngle = static_cast<float>(source) * kPi * 0.25f;
        float centerX = std::cos(sourceAngle);
        float centerY = std::sin(sourceAngle);
        const float swirlX = -centerY * vortex * flowAmount;
        const float swirlY = centerX * vortex * flowAmount;
        const float sourcePhase = angle + sourceAngle;
        const float swirlAmplitude = swirlShape ? 0.54f : 0.20f;
        centerX = centerX * (1.0f - 0.32f * flowAmount) + swirlX
            + orbitX * 0.46f + std::cos(sourcePhase) * flowAmount
                * swirlAmplitude;
        centerY = centerY * (1.0f - 0.32f * flowAmount) + swirlY
            + orbitY * 0.46f + std::sin(sourcePhase) * flowAmount
                * swirlAmplitude;
        float sum = 0.0f;
        for (uint32_t destination = 0u; destination < 8u;
             ++destination) {
            const float destinationAngle = static_cast<float>(destination)
                * kPi * 0.25f;
            const float dx = std::cos(destinationAngle) - centerX;
            const float dy = std::sin(destinationAngle) - centerY;
            const float value = std::exp(-(dx * dx + dy * dy)
                / std::max(0.001f, width * width));
            weights[destination * 8u + source] = value;
            sum += value;
        }
        if (sum > 1.0e-6f) {
            for (uint32_t destination = 0u; destination < 8u;
                 ++destination) {
                weights[destination * 8u + source] /= sum;
            }
        }
    }
    return weights;
}

constexpr uint32_t kNoInputMixerFactoryPresetCount = 10u;

inline const char* noInputMixerFactoryPresetName(uint32_t index)
{
    static constexpr std::array<const char*,
        kNoInputMixerFactoryPresetCount> names {{
        "INIT",
        "CIRCUIT LATTICE",
        "RAIN FOREST",
        "WOOL RING",
        "RAT CAGE",
        "ZONE WEB",
        "NEGATIVE SPACE",
        "RELAY BLOOM",
        "OPEN HOUSE",
        "MOBILE CIRCUIT",
    }};
    return names[std::min<uint32_t>(index,
        kNoInputMixerFactoryPresetCount - 1u)];
}

inline NoInputMixerParams noInputMixerFactoryPreset(uint32_t index)
{
    index = std::min<uint32_t>(index,
        kNoInputMixerFactoryPresetCount - 1u);
    auto params = defaultNoInputMixerParams();
    if (index == 0u) return params;

    params.matrix.fill(0.0f);
    const auto route = [&](uint32_t destination, uint32_t source,
        float gain) {
        params.matrix[destination * kNoInputMixerChannels + source] = gain;
    };

    switch (index) {
    case 1u: { // Circuit Lattice: signed, phase-bearing mutual excitation.
        params.seed = 0x4c415454u;
        params.feedback = 0.91f;
        params.coupling = 0.68f;
        params.phase = 0.72f;
        params.drift = 0.36f;
        params.formant = 0.44f;
        params.agency = 0.62f;
        params.motion = 0.34f;
        params.motionShape = MatrixFlowShape::Swirl;
        params.vortex = 0.42f;
        for (uint32_t lane = 0u; lane < kNoInputMixerChannels; ++lane) {
            route(lane, lane, 0.92f + 0.008f * static_cast<float>(lane));
            route(lane, (lane + 7u) % 8u,
                (lane & 1u) == 0u ? 0.28f : -0.24f);
            route(lane, (lane + 3u) % 8u,
                (lane % 3u) == 0u ? -0.14f : 0.11f);
            auto& voice = params.lanes[lane];
            voice.body = 0.18f + 0.086f * static_cast<float>(lane);
            voice.loss = 0.27f + 0.035f * static_cast<float>(lane % 4u);
            voice.inserts[0].type = (lane & 1u) == 0u
                ? NoInputDistortionType::Diode
                : NoInputDistortionType::Ring;
            voice.inserts[0].gain = 0.28f + 0.025f * (lane % 3u);
            voice.inserts[0].tone = 0.34f + 0.07f * (lane % 5u);
            voice.inserts[0].levelDb = -7.5f;
        }
        break;
    }
    case 2u: { // Rain Forest: long resonant bodies with sparse cross-strikes.
        params.seed = 0x5241494eu;
        params.feedback = 0.88f;
        params.coupling = 0.48f;
        params.phase = 0.58f;
        params.drift = 0.52f;
        params.formant = 0.24f;
        params.agency = 0.54f;
        params.space = 0.36f;
        params.motion = 0.28f;
        params.motionShape = MatrixFlowShape::Scatter;
        for (uint32_t lane = 0u; lane < kNoInputMixerChannels; ++lane) {
            route(lane, lane, 0.96f);
            route(lane, (lane + 5u) % 8u,
                (lane & 1u) == 0u ? 0.18f : -0.16f);
            auto& voice = params.lanes[lane];
            voice.body = 0.12f + 0.105f * static_cast<float>(lane);
            voice.loss = 0.18f + 0.022f * static_cast<float>(lane % 3u);
            voice.lowDb = 2.5f - 0.5f * static_cast<float>(lane % 3u);
            voice.highDb = -2.0f + 0.6f * static_cast<float>(lane % 4u);
            voice.inserts[0].type = NoInputDistortionType::Diode;
            voice.inserts[0].gain = 0.17f;
            voice.inserts[0].tone = 0.28f + 0.05f * (lane % 4u);
            voice.inserts[0].levelDb = -5.5f;
        }
        break;
    }
    case 3u: { // Wool Ring: compressed walls feeding ring-modulated returns.
        params.seed = 0x4d554646u;
        params.feedback = 0.90f;
        params.coupling = 0.57f;
        params.phase = 0.31f;
        params.drift = 0.22f;
        params.formant = 0.37f;
        params.motion = 0.26f;
        params.motionShape = MatrixFlowShape::Chase;
        for (uint32_t lane = 0u; lane < kNoInputMixerChannels; ++lane) {
            route(lane, lane, 0.94f);
            route(lane, (lane + 1u) % 8u,
                (lane & 1u) == 0u ? 0.22f : -0.18f);
            auto& voice = params.lanes[lane];
            voice.body = 0.24f + 0.067f * static_cast<float>(lane);
            voice.loss = 0.34f;
            voice.inserts[0].type = NoInputDistortionType::Wool;
            voice.inserts[0].gain = 0.46f + 0.025f * (lane % 3u);
            voice.inserts[0].tone = 0.24f + 0.085f * (lane % 5u);
            voice.inserts[0].levelDb = -11.0f;
            voice.inserts[1].type = NoInputDistortionType::Ring;
            voice.inserts[1].gain = 0.20f + 0.025f * (lane % 4u);
            voice.inserts[1].tone = 0.43f;
            voice.inserts[1].levelDb = -4.0f;
            voice.inserts[1].bypass = 0u;
        }
        break;
    }
    case 4u: { // Rat Cage: bright hard-clipped cells in a directed ring.
        params.seed = 0x52415443u;
        params.feedback = 0.93f;
        params.coupling = 0.63f;
        params.phase = 0.46f;
        params.drift = 0.15f;
        params.formant = 0.31f;
        params.agency = 0.42f;
        params.motion = 0.18f;
        params.motionShape = MatrixFlowShape::Pulse;
        for (uint32_t lane = 0u; lane < kNoInputMixerChannels; ++lane) {
            route(lane, lane, 0.91f);
            route(lane, (lane + 7u) % 8u, 0.27f);
            if ((lane & 1u) != 0u) route(lane, (lane + 4u) % 8u, -0.13f);
            auto& voice = params.lanes[lane];
            voice.body = 0.36f + 0.048f * static_cast<float>(lane % 5u);
            voice.loss = 0.42f;
            voice.midGainDb = 3.0f;
            voice.midFrequencyHz = 720.0f + 260.0f * lane;
            voice.inserts[0].type = NoInputDistortionType::Rat;
            voice.inserts[0].gain = 0.38f + 0.025f * (lane % 4u);
            voice.inserts[0].tone = 0.42f + 0.06f * (lane % 3u);
            voice.inserts[0].levelDb = -9.0f;
        }
        break;
    }
    case 5u: { // Zone Web: alternating focused and wide high-gain cells.
        params.seed = 0x5a4f4e45u;
        params.feedback = 0.89f;
        params.coupling = 0.72f;
        params.phase = 0.39f;
        params.drift = 0.28f;
        params.formant = 0.46f;
        params.motion = 0.38f;
        params.motionShape = MatrixFlowShape::Flow;
        params.vortex = -0.34f;
        for (uint32_t lane = 0u; lane < kNoInputMixerChannels; ++lane) {
            route(lane, lane, 0.93f);
            route(lane, (lane + 2u) % 8u,
                (lane & 1u) == 0u ? 0.25f : -0.21f);
            route(lane, (lane + 5u) % 8u, 0.10f);
            auto& voice = params.lanes[lane];
            voice.body = 0.20f + 0.075f * static_cast<float>(lane);
            voice.loss = 0.38f + 0.025f * static_cast<float>(lane % 3u);
            voice.midGainDb = (lane & 1u) == 0u ? 5.0f : -3.5f;
            voice.midFrequencyHz = 580.0f + 410.0f * lane;
            voice.inserts[0].type = (lane & 1u) == 0u
                ? NoInputDistortionType::ZoneA
                : NoInputDistortionType::ZoneB;
            voice.inserts[0].gain = 0.36f;
            voice.inserts[0].tone = 0.31f + 0.07f * (lane % 5u);
            voice.inserts[0].bias = (lane & 1u) == 0u ? 0.08f : -0.10f;
            voice.inserts[0].levelDb = -10.0f;
        }
        break;
    }
    case 6u: { // Negative Space: inhibitory routes dominate the ecology.
        params.seed = 0x4e454753u;
        params.feedback = 0.94f;
        params.coupling = 0.76f;
        params.phase = 0.67f;
        params.drift = 0.33f;
        params.formant = 0.18f;
        params.space = 0.68f;
        params.agency = 0.74f;
        params.motion = 0.30f;
        params.motionShape = MatrixFlowShape::Hold;
        for (uint32_t lane = 0u; lane < kNoInputMixerChannels; ++lane) {
            route(lane, lane, 0.95f);
            route(lane, (lane + 1u) % 8u, -0.31f);
            route(lane, (lane + 4u) % 8u,
                (lane & 1u) == 0u ? -0.17f : 0.12f);
            auto& voice = params.lanes[lane];
            voice.body = 0.15f + 0.09f * static_cast<float>(lane);
            voice.loss = 0.26f + 0.03f * static_cast<float>(lane % 4u);
            voice.inserts[0].type = NoInputDistortionType::Diode;
            voice.inserts[0].gain = 0.22f;
            voice.inserts[0].bias = (lane & 1u) == 0u ? -0.14f : 0.14f;
            voice.inserts[0].levelDb = -5.0f;
        }
        break;
    }
    case 7u: { // Relay Bloom: gated fuzz cells open into diode recovery.
        params.seed = 0x52454c59u;
        params.feedback = 0.92f;
        params.coupling = 0.54f;
        params.phase = 0.24f;
        params.drift = 0.44f;
        params.formant = 0.55f;
        params.agency = 0.70f;
        params.space = 0.28f;
        params.motion = 0.46f;
        params.motionShape = MatrixFlowShape::Scatter;
        for (uint32_t lane = 0u; lane < kNoInputMixerChannels; ++lane) {
            route(lane, lane, 0.96f);
            route(lane, (lane + 3u) % 8u,
                (lane % 3u) == 0u ? -0.20f : 0.17f);
            auto& voice = params.lanes[lane];
            voice.body = 0.27f + 0.055f * static_cast<float>(lane);
            voice.loss = 0.30f + 0.04f * static_cast<float>(lane % 3u);
            voice.highDb = 2.0f;
            voice.inserts[0].type = (lane & 1u) == 0u
                ? NoInputDistortionType::FuzzII
                : NoInputDistortionType::FuzzI;
            voice.inserts[0].gain = 0.34f + 0.025f * (lane % 4u);
            voice.inserts[0].tone = 0.38f + 0.05f * (lane % 4u);
            voice.inserts[0].levelDb = -8.5f;
            voice.inserts[1].type = NoInputDistortionType::Diode;
            voice.inserts[1].gain = 0.16f;
            voice.inserts[1].tone = 0.62f;
            voice.inserts[1].levelDb = -3.0f;
            voice.inserts[1].bypass = 0u;
        }
        break;
    }
    case 8u: { // Open House: restrained channels, strong return practice.
        params.seed = 0x4e414b41u;
        params.feedback = 0.90f;
        params.coupling = 0.39f;
        params.phase = 0.27f;
        params.drift = 0.31f;
        params.formant = 0.16f;
        params.agency = 0.82f;
        params.space = 0.76f;
        params.internalTone = 0.18f;
        params.houseTone = -0.34f;
        params.motion = 0.24f;
        params.motionShape = MatrixFlowShape::Hold;
        params.aux[0].effect.type = NoInputDistortionType::Diode;
        params.aux[0].effect.gain = 0.24f;
        params.aux[0].feedback = 0.48f;
        params.aux[0].returnGain = 0.32f;
        params.aux[1].effect.type = NoInputDistortionType::Ring;
        params.aux[1].effect.gain = 0.34f;
        params.aux[1].feedback = 0.37f;
        params.aux[1].returnGain = 0.25f;
        for (uint32_t lane = 0u; lane < kNoInputMixerChannels; ++lane) {
            route(lane, lane, 0.95f);
            if ((lane & 1u) == 0u) {
                route(lane, (lane + 5u) % 8u, -0.13f);
            }
            auto& voice = params.lanes[lane];
            voice.body = 0.16f + 0.087f * static_cast<float>(lane);
            voice.loss = 0.22f + 0.035f * static_cast<float>(lane % 4u);
            voice.levelDb = -6.0f;
            voice.auxSend[0] = (lane % 3u) == 0u ? 0.42f : 0.14f;
            voice.auxSend[1] = (lane % 3u) == 1u ? 0.36f : 0.09f;
            voice.inserts[0].type = NoInputDistortionType::Diode;
            voice.inserts[0].gain = 0.18f;
            voice.inserts[0].levelDb = -4.0f;
            voice.inserts[1].bypass = 1u;
            voice.inserts[2].bypass = 1u;
        }
        break;
    }
    case 9u: { // Mobile Circuit: a moving, signed network of nonlinear cells.
        params.seed = 0x4d4f4249u;
        params.feedback = 0.93f;
        params.coupling = 0.78f;
        params.phase = 0.74f;
        params.drift = 0.48f;
        params.formant = 0.52f;
        params.agency = 0.88f;
        params.space = 0.22f;
        params.internalTone = 0.28f;
        params.houseTone = -0.12f;
        params.flow = 0.72f;
        params.spread = 0.58f;
        params.vortex = 0.76f;
        params.motion = 0.72f;
        params.motionShape = MatrixFlowShape::Swirl;
        params.motionRate = 0.22f;
        params.aux[0].effect.type = NoInputDistortionType::Wool;
        params.aux[0].effect.gain = 0.32f;
        params.aux[0].feedback = 0.34f;
        params.aux[0].returnGain = 0.22f;
        params.aux[1].effect.type = NoInputDistortionType::Rat;
        params.aux[1].effect.gain = 0.28f;
        params.aux[1].feedback = 0.29f;
        params.aux[1].returnGain = 0.20f;
        for (uint32_t lane = 0u; lane < kNoInputMixerChannels; ++lane) {
            route(lane, lane, 0.92f);
            route(lane, (lane + 7u) % 8u,
                (lane & 1u) == 0u ? 0.27f : -0.24f);
            route(lane, (lane + 3u) % 8u,
                (lane & 2u) == 0u ? -0.16f : 0.12f);
            auto& voice = params.lanes[lane];
            voice.body = 0.14f + 0.095f * static_cast<float>(lane);
            voice.loss = 0.25f + 0.028f * static_cast<float>(lane % 4u);
            voice.auxSend[0] = 0.16f + 0.04f * static_cast<float>(lane % 3u);
            voice.auxSend[1] = 0.12f + 0.03f * static_cast<float>((lane + 1u) % 4u);
            voice.inserts[0].type = (lane & 1u) == 0u
                ? NoInputDistortionType::Wool : NoInputDistortionType::Rat;
            voice.inserts[0].gain = 0.29f + 0.025f * (lane % 4u);
            voice.inserts[0].levelDb = -8.0f;
            voice.inserts[1].type = (lane & 1u) == 0u
                ? NoInputDistortionType::Ring : NoInputDistortionType::ZoneA;
            voice.inserts[1].gain = 0.18f;
            voice.inserts[1].levelDb = -5.0f;
            voice.inserts[1].bypass = 0u;
        }
        break;
    }
    default:
        break;
    }
    return sanitizeNoInputMixerParams(params);
}

inline NoInputMixerParams randomizedNoInputMixerParams(uint32_t seed)
{
    uint32_t randomState = seed == 0u ? 1u : seed;
    const auto next = [&randomState]() {
        randomState += 0x9e3779b9u;
        uint32_t value = randomState;
        value ^= value >> 16u;
        value *= 0x7feb352du;
        value ^= value >> 15u;
        value *= 0x846ca68bu;
        value ^= value >> 16u;
        return value;
    };
    const auto unit = [&next]() {
        return static_cast<float>(next() & 0x00ffffffu)
            / static_cast<float>(0x01000000u);
    };
    const auto bipolar = [&unit]() { return unit() * 2.0f - 1.0f; };

    auto params = defaultNoInputMixerParams();
    params.seed = seed == 0u ? 1u : seed;
    params.outputGainDb = -20.0f + unit() * 5.0f;
    params.ceilingDb = -1.0f;
    params.feedback = 0.86f + unit() * 0.11f;
    params.coupling = 0.28f + unit() * 0.48f;
    params.phase = 0.14f + unit() * 0.66f;
    params.drift = 0.08f + unit() * 0.45f;
    params.formant = 0.10f + unit() * 0.52f;
    params.agency = 0.20f + unit() * 0.72f;
    params.space = 0.04f + unit() * 0.68f;
    params.variance = 0.08f + unit() * 0.46f;
    params.internalTone = bipolar() * 0.46f;
    params.houseTone = -0.42f + unit() * 0.58f;
    params.flow = 0.18f + unit() * 0.70f;
    params.spread = 0.12f + unit() * 0.74f;
    params.vortex = bipolar() * 0.88f;
    params.motion = 0.08f + unit() * 0.68f;
    params.motionShape = matrixFlowShapeFromIndex(
        next() % (static_cast<uint32_t>(MatrixFlowShape::Hold) + 1u));
    params.motionRate = 0.06f + unit() * 0.42f;
    params.motionPhase = unit();
    params.quality = unit() > 0.82f ? 2u : 1u;
    params.matrix.fill(0.0f);

    for (uint32_t lane = 0u; lane < kNoInputMixerChannels; ++lane) {
        params.matrix[lane * kNoInputMixerChannels + lane] =
            0.90f + unit() * 0.08f;
        const uint32_t neighbor = (lane + 7u) % kNoInputMixerChannels;
        params.matrix[lane * kNoInputMixerChannels + neighbor] =
            (unit() > 0.42f ? 1.0f : -1.0f) * (0.10f + unit() * 0.25f);
        if (unit() > 0.46f) {
            uint32_t source = next() % kNoInputMixerChannels;
            if (source == lane || source == neighbor) {
                source = (source + 3u) % kNoInputMixerChannels;
            }
            params.matrix[lane * kNoInputMixerChannels + source] =
                (unit() > 0.5f ? 1.0f : -1.0f)
                * (0.06f + unit() * 0.22f);
        }

        auto& voice = params.lanes[lane];
        voice.body = 0.12f + unit() * 0.72f;
        voice.loss = 0.20f + unit() * 0.34f;
        voice.levelDb = -7.0f + unit() * 4.0f;
        voice.lowDb = bipolar() * 5.5f;
        voice.midFrequencyHz = 120.0f * std::pow(52.0f, unit());
        voice.midGainDb = bipolar() * 6.5f;
        voice.highDb = bipolar() * 5.5f;
        voice.auxSend[0] = unit() * 0.46f;
        voice.auxSend[1] = unit() * 0.42f;
        for (uint32_t slot = 0u; slot < kNoInputMixerInsertSlots; ++slot) {
            auto& insert = voice.inserts[slot];
            insert.type = static_cast<NoInputDistortionType>(
                1u + next() % (kNoInputDistortionTypeCount - 1u));
            insert.gain = 0.14f + unit() * (slot == 0u ? 0.43f : 0.32f);
            insert.tone = 0.18f + unit() * 0.68f;
            insert.bias = bipolar() * 0.22f;
            insert.levelDb = -12.0f + unit() * 7.0f;
            insert.bypass = slot == 0u || unit() > 0.66f ? 0u : 1u;
        }
    }
    for (uint32_t bus = 0u; bus < 2u; ++bus) {
        auto& aux = params.aux[bus];
        aux.effect.type = static_cast<NoInputDistortionType>(
            1u + next() % (kNoInputDistortionTypeCount - 1u));
        aux.effect.gain = 0.12f + unit() * 0.44f;
        aux.effect.tone = 0.16f + unit() * 0.68f;
        aux.feedback = 0.12f + unit() * 0.48f;
        aux.returnGain = 0.08f + unit() * 0.30f;
    }
    return sanitizeNoInputMixerParams(params);
}

inline NoInputMixerParams variedNoInputMixerParams(
    NoInputMixerParams params, uint32_t seed, float amount)
{
    uint32_t state = seed == 0u ? 1u : seed;
    const auto randomUnit = [&state]() {
        state += 0x9e3779b9u;
        uint32_t value = state;
        value ^= value >> 16u;
        value *= 0x7feb352du;
        value ^= value >> 15u;
        value *= 0x846ca68bu;
        value ^= value >> 16u;
        return static_cast<float>(value & 0x00ffffffu)
            / static_cast<float>(0x01000000u);
    };
    const auto signedRandom = [&randomUnit]() {
        return randomUnit() * 2.0f - 1.0f;
    };
    amount = clamp(std::isfinite(amount) ? amount : 0.0f, 0.0f, 1.0f);
    params.seed = seed == 0u ? 1u : seed;
    params.variance = amount;
    params.feedback += signedRandom() * 0.035f * amount;
    params.coupling += signedRandom() * 0.10f * amount;
    params.phase += signedRandom() * 0.12f * amount;
    params.motionPhase = noInputWrapPhase(
        params.motionPhase + signedRandom() * amount);
    for (float& route : params.matrix) {
        if (std::abs(route) > 1.0e-6f) {
            route *= 1.0f + signedRandom() * 0.22f * amount;
        }
    }
    for (auto& lane : params.lanes) {
        lane.body += signedRandom() * 0.10f * amount;
        lane.loss += signedRandom() * 0.08f * amount;
        lane.levelDb += signedRandom() * 2.5f * amount;
        lane.lowDb += signedRandom() * 3.0f * amount;
        lane.midFrequencyHz *= std::exp2(signedRandom() * 0.55f * amount);
        lane.midGainDb += signedRandom() * 3.5f * amount;
        lane.highDb += signedRandom() * 3.0f * amount;
        for (float& send : lane.auxSend) {
            send += signedRandom() * 0.16f * amount;
        }
        for (auto& insert : lane.inserts) {
            insert.gain += signedRandom() * 0.12f * amount;
            insert.tone += signedRandom() * 0.16f * amount;
            insert.bias += signedRandom() * 0.12f * amount;
        }
    }
    for (auto& aux : params.aux) {
        aux.effect.gain += signedRandom() * 0.12f * amount;
        aux.effect.tone += signedRandom() * 0.16f * amount;
        aux.feedback += signedRandom() * 0.12f * amount;
        aux.returnGain += signedRandom() * 0.10f * amount;
    }
    return sanitizeNoInputMixerParams(params);
}

// FORGET is deliberately local: it loosens a handful of relationships while
// preserving the instrument, channel settings, pedals, and global safety.
inline NoInputMixerParams forgottenNoInputMixerParams(
    NoInputMixerParams params, uint32_t seed)
{
    uint32_t state = seed == 0u ? 1u : seed;
    const auto next = [&state]() {
        state += 0x9e3779b9u;
        uint32_t value = state;
        value ^= value >> 16u;
        value *= 0x7feb352du;
        value ^= value >> 15u;
        value *= 0x846ca68bu;
        value ^= value >> 16u;
        return value;
    };
    const auto unit = [&next]() {
        return static_cast<float>(next() & 0x00ffffffu)
            / static_cast<float>(0x01000000u);
    };
    const uint32_t changes = 2u + static_cast<uint32_t>(
        std::lround(params.agency * 8.0f));
    for (uint32_t change = 0u; change < changes; ++change) {
        const uint32_t destination = next() % kNoInputMixerChannels;
        uint32_t source = next() % kNoInputMixerChannels;
        if (source == destination) source = (source + 3u) % 8u;
        float& route = params.matrix[destination * 8u + source];
        if (std::abs(route) > 1.0e-5f) {
            if (unit() < 0.58f) route = 0.0f;
            else route = -route * (0.72f + unit() * 0.24f);
        } else {
            route = (unit() < 0.38f ? -1.0f : 1.0f)
                * (0.07f + unit() * (0.10f + params.agency * 0.18f));
        }
    }
    const uint32_t lane = next() % kNoInputMixerChannels;
    params.lanes[lane].auxSend[next() & 1u] = 0.08f + unit() * 0.48f;
    params.motionPhase = unit();
    params.seed = seed == 0u ? 1u : seed;
    return sanitizeNoInputMixerParams(params);
}

enum class NoInputContainmentState : uint32_t {
    Quiet = 0u,
    Stable,
    Edge,
    Runaway,
};

inline const char* noInputContainmentName(NoInputContainmentState state)
{
    switch (state) {
    case NoInputContainmentState::Quiet: return "QUIET";
    case NoInputContainmentState::Stable: return "STABLE";
    case NoInputContainmentState::Edge: return "EDGE";
    case NoInputContainmentState::Runaway: return "RUNAWAY";
    }
    return "QUIET";
}

class NoInputMixer {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        dcPole_ = std::exp(-2.0f * kPi * 12.0f
            / static_cast<float>(sampleRate_));
        energyAttack_ = 1.0f - std::exp(-1.0f
            / static_cast<float>(sampleRate_ * 0.012));
        energyRelease_ = 1.0f - std::exp(-1.0f
            / static_cast<float>(sampleRate_ * 0.180));
        governorAttack_ = 1.0f - std::exp(-1.0f
            / static_cast<float>(sampleRate_ * 0.018));
        governorRelease_ = 1.0f - std::exp(-1.0f
            / static_cast<float>(sampleRate_ * 0.420));
        panicSamplesTotal_ = std::max<uint32_t>(
            1u, static_cast<uint32_t>(sampleRate_ * 0.008));
        setParams(params_);
        reset();
    }

    void setParams(NoInputMixerParams params)
    {
        params_ = sanitizeNoInputMixerParams(params);
        for (uint32_t lane = 0u; lane < kNoInputMixerChannels; ++lane) {
            updateEq(lane);
            updateBodyCoefficients(lane);
            for (uint32_t slot = 0u;
                 slot < kNoInputMixerInsertSlots; ++slot) {
                auto& runtime = laneState_[lane].inserts[slot];
                const auto& insert = params_.lanes[lane].inserts[slot];
                const NoInputDistortionType effective = insert.bypass != 0u
                    ? NoInputDistortionType::Bypass : insert.type;
                if (!runtime.initialized) {
                    runtime.currentType = effective;
                    runtime.previousType = effective;
                    runtime.crossfade = 1.0f;
                    runtime.initialized = true;
                } else if (runtime.currentType != effective) {
                    runtime.previousType = runtime.currentType;
                    runtime.currentType = effective;
                    runtime.crossfade = 0.0f;
                }
            }
        }
        for (uint32_t bus = 0u; bus < 2u; ++bus) {
            auto& runtime = auxState_[bus].insert;
            const auto type = params_.aux[bus].effect.type;
            if (!runtime.initialized) {
                runtime.currentType = type;
                runtime.previousType = type;
                runtime.crossfade = 1.0f;
                runtime.initialized = true;
            } else if (runtime.currentType != type) {
                runtime.previousType = runtime.currentType;
                runtime.currentType = type;
                runtime.crossfade = 0.0f;
            }
        }
        rebuildMotionTargets();
    }

    const NoInputMixerParams& params() const { return params_; }

    void reset()
    {
        clearSignalState();
        silenced_ = true;
        seedRemaining_ = 0u;
        panicRemaining_ = 0u;
        containmentState_ = NoInputContainmentState::Quiet;
    }

    void reseed(uint32_t seed, float amount = 0.45f)
    {
        clearSignalState();
        randomState_ = seed == 0u ? 1u : seed;
        seedAmount_ = clamp(amount, 0.01f, 1.0f);
        seedRemaining_ = std::max<uint32_t>(
            16u, static_cast<uint32_t>(sampleRate_ * 0.018));
        silenced_ = false;
        panicRemaining_ = 0u;
    }

    void triggerSeed(float amount = 0.35f)
    {
        seedAmount_ = clamp(amount, 0.01f, 1.0f);
        seedRemaining_ = std::max<uint32_t>(
            16u, static_cast<uint32_t>(sampleRate_ * 0.012));
        silenced_ = false;
    }

    void panic()
    {
        panicRemaining_ = panicSamplesTotal_;
    }

    void killLane(uint32_t lane)
    {
        if (lane >= kNoInputMixerChannels) return;
        laneState_[lane] = {};
        returns_[lane] = 0.0f;
        previousReturns_[lane] = 0.0f;
        lanePeak_[lane] = 0.0f;
        laneActivity_[lane] = 0.0f;
        for (uint32_t source = 0u; source < kNoInputMixerChannels;
             ++source) {
            phaseMemory_[lane * kNoInputMixerChannels + source] = 0.0f;
            phaseMemory_[source * kNoInputMixerChannels + lane] = 0.0f;
        }
        updateEq(lane);
        updateBodyCoefficients(lane);
        initializeInsertTypes(lane);
    }

    void processFrame(float* output)
    {
        if (!output) return;
        routeSignals_.fill(0.0f);
        if (silenced_ && seedRemaining_ == 0u) {
            std::fill(output, output + kNoInputMixerChannels, 0.0f);
            return;
        }

        if (params_.motion > 0.0001f
            && params_.motionShape != MatrixFlowShape::Hold) {
            const float hz = noInputMixerMotionRateHz(params_.motionRate);
            motionPhase_ = noInputWrapPhase(motionPhase_
                + hz / static_cast<float>(sampleRate_));
        }
        if ((controlCounter_++ & 31u) == 0u) updateSlowControl();
        previousReturns_ = returns_;
        previousAuxReturns_ = auxReturns_;
        processAuxReturns();

        std::array<float, kNoInputMixerChannels> activeMotionPeak {};
        std::array<uint32_t, kNoInputMixerChannels> activeMotionRouteCount {};
        for (uint32_t destination = 0u;
             destination < kNoInputMixerChannels; ++destination) {
            for (uint32_t source = 0u;
                 source < kNoInputMixerChannels; ++source) {
                const uint32_t index =
                    destination * kNoInputMixerChannels + source;
                if (std::abs(params_.matrix[index]) < 1.0e-7f) continue;
                activeMotionPeak[source] = std::max(
                    activeMotionPeak[source], motionCurrent_[index]);
                ++activeMotionRouteCount[source];
            }
        }

        std::array<float, kNoInputMixerChannels> matrixInput {};
        for (uint32_t destination = 0u;
             destination < kNoInputMixerChannels; ++destination) {
            const float busBPolarity = (destination & 1u) == 0u
                ? 1.0f : -1.0f;
            float sum = seedForLane(destination)
                + auxReturns_[0] * params_.aux[0].returnGain * 0.42f
                + auxReturns_[1] * params_.aux[1].returnGain * 0.36f
                    * busBPolarity;
            float weightSum = 0.0f;
            for (uint32_t source = 0u;
                 source < kNoInputMixerChannels; ++source) {
                const uint32_t index =
                    destination * kNoInputMixerChannels + source;
                const float matrixGain = params_.matrix[index];
                if (std::abs(matrixGain) < 1.0e-7f) continue;
                const float networkScale = source == destination
                    ? params_.feedback
                    : params_.feedback * params_.coupling;
                const float driftScale = 1.0f
                    + driftState_[index] * params_.drift * 0.14f;
                const float motionScale = noInputMixerMotionGainScale(
                    motionCurrent_[index], activeMotionPeak[source],
                    activeMotionRouteCount[source], params_.motion);
                const float spaceScale = lerp(1.0f,
                    routeSpaceGate_[index], params_.space);
                const float activityDifference = laneActivity_[source]
                    - laneActivity_[destination];
                const float agencyScale = clamp(1.0f + params_.agency
                    * (activityDifference * 0.58f
                        + driftState_[index] * 0.07f), 0.62f, 1.38f);
                const float baseGain = matrixGain * networkScale * driftScale
                    * spaceScale * agencyScale;
                const float gain = baseGain * motionScale;
                const float coefficient = 0.08f + params_.phase
                    * (0.20f + 0.22f * hashUnit(index + 0x3157u));
                const float input = previousReturns_[source];
                const float allpass = -coefficient * input
                    + phaseMemory_[index];
                phaseMemory_[index] = flushDenormal(
                    input + coefficient * allpass);
                const float phased = lerp(input, allpass, params_.phase);
                routeSignals_[index] = flushDenormal(phased * gain);
                sum += routeSignals_[index];
                // Normalize the graph, not the movement envelope. Including
                // the modulated gain here applies inverse compensation and
                // makes route motion disappear perceptually.
                weightSum += std::abs(baseGain);
            }
            const float normalization = 1.0f
                / std::max(1.0f, 0.42f + weightSum * 0.68f);
            const float normalized = sum * normalization;
            const float clampScale = std::abs(normalized) > 6.0f
                ? 6.0f / std::abs(normalized) : 1.0f;
            for (uint32_t source = 0u;
                 source < kNoInputMixerChannels; ++source) {
                const uint32_t index =
                    destination * kNoInputMixerChannels + source;
                routeSignals_[index] = flushDenormal(
                    routeSignals_[index] * normalization * clampScale);
            }
            matrixInput[destination] = clamp(normalized, -6.0f, 6.0f);
        }

        float networkPeak = 0.0f;
        for (uint32_t lane = 0u; lane < kNoInputMixerChannels; ++lane) {
            float value = processBody(lane, matrixInput[lane]);
            value = processFormant(lane, value);
            value = laneState_[lane].lowShelf.process(value);
            value = laneState_[lane].midPeak.process(value);
            value = laneState_[lane].highShelf.process(value);

            for (uint32_t slot = 0u;
                 slot < kNoInputMixerInsertSlots; ++slot) {
                const uint32_t ringSource =
                    (lane + slot + 1u) % kNoInputMixerChannels;
                value = processInsert(lane, slot, value,
                    previousReturns_[ringSource]);
            }

            if (!std::isfinite(value)) {
                killLane(lane);
                value = 0.0f;
            }
            value = clamp(value, -8.0f, 8.0f);

            auto& state = laneState_[lane];
            const float energyInput = value * value;
            const float energyCoefficient = energyInput > state.energy
                ? energyAttack_ : energyRelease_;
            state.energy += (energyInput - state.energy) * energyCoefficient;
            state.energy = std::max(0.0f, flushDenormal(state.energy));
            const float rms = std::sqrt(state.energy);
            const float excess = std::max(0.0f, rms - 0.42f);
            const float targetGovernor = 1.0f
                / (1.0f + excess * excess * 28.0f + excess * 3.5f);
            const float governorCoefficient = targetGovernor < state.governor
                ? governorAttack_ : governorRelease_;
            state.governor += (targetGovernor - state.governor)
                * governorCoefficient;
            state.governor = clamp(state.governor, 0.055f, 1.0f);

            float returned = value * state.governor;
            returned = processTone(returned, params_.internalTone,
                state.internalToneLow);
            returned = dcBlock(returned, state.returnDcInput,
                state.returnDcOutput);
            returns_[lane] = flushDenormal(returned);
            networkPeak = std::max(networkPeak, std::abs(returned));

            float audition = value * dbToGain(params_.lanes[lane].levelDb)
                * dbToGain(params_.outputGainDb);
            if (params_.lanes[lane].mute != 0u) audition = 0.0f;
            audition = processTone(audition, params_.houseTone,
                state.houseToneLow);
            audition = dcBlock(audition, state.outputDcInput,
                state.outputDcOutput);
            if (params_.limiterEnabled != 0u) {
                audition = softLimit(audition,
                    dbToGain(params_.ceilingDb));
            }
            if (panicRemaining_ > 0u) {
                audition *= static_cast<float>(panicRemaining_)
                    / static_cast<float>(panicSamplesTotal_);
            }
            output[lane] = std::isfinite(audition)
                ? flushDenormal(audition) : 0.0f;
            lanePeak_[lane] = std::max(
                lanePeak_[lane] * 0.9994f, std::abs(output[lane]));
            laneActivity_[lane] += (rms - laneActivity_[lane]) * 0.003f;
        }

        if (seedRemaining_ > 0u) --seedRemaining_;
        if (panicRemaining_ > 0u) {
            --panicRemaining_;
            if (panicRemaining_ == 0u) {
                clearSignalState();
                silenced_ = true;
                containmentState_ = NoInputContainmentState::Quiet;
                std::fill(output, output + kNoInputMixerChannels, 0.0f);
                return;
            }
        }

        networkActivity_ += (networkPeak - networkActivity_) * 0.0025f;
        const float minimumGovernor = minimumLaneGovernor();
        if (networkActivity_ < 1.0e-5f) {
            containmentState_ = NoInputContainmentState::Quiet;
        } else if (minimumGovernor > 0.72f) {
            containmentState_ = NoInputContainmentState::Stable;
        } else if (minimumGovernor > 0.28f) {
            containmentState_ = NoInputContainmentState::Edge;
        } else {
            containmentState_ = NoInputContainmentState::Runaway;
        }
    }

    float lanePeak(uint32_t lane) const
    {
        return lane < kNoInputMixerChannels ? lanePeak_[lane] : 0.0f;
    }

    float laneActivity(uint32_t lane) const
    {
        return lane < kNoInputMixerChannels ? laneActivity_[lane] : 0.0f;
    }

    float routeSignal(uint32_t route) const
    {
        return route < kNoInputMixerMatrixCells
            ? routeSignals_[route] : 0.0f;
    }

    float networkActivity() const { return networkActivity_; }
    float auxActivity(uint32_t bus) const
    {
        return bus < 2u ? auxActivity_[bus] : 0.0f;
    }
    float motionPhase() const { return motionPhase_; }
    float minimumGovernor() const { return minimumLaneGovernor(); }
    NoInputContainmentState containmentState() const
    {
        return containmentState_;
    }

private:
    struct Biquad {
        float b0 = 1.0f;
        float b1 = 0.0f;
        float b2 = 0.0f;
        float a1 = 0.0f;
        float a2 = 0.0f;
        float z1 = 0.0f;
        float z2 = 0.0f;

        float process(float input)
        {
            const float output = input * b0 + z1;
            z1 = flushDenormal(input * b1 - output * a1 + z2);
            z2 = flushDenormal(input * b2 - output * a2);
            return output;
        }

        void clear() { z1 = z2 = 0.0f; }
    };

    struct Resonator {
        float c1 = 0.0f;
        float c2 = 0.0f;
        float inputGain = 0.0f;
        float z1 = 0.0f;
        float z2 = 0.0f;

        float process(float input)
        {
            const float output = input * inputGain + c1 * z1 + c2 * z2;
            z2 = z1;
            z1 = flushDenormal(clamp(output, -12.0f, 12.0f));
            return z1;
        }

        void clear() { z1 = z2 = 0.0f; }
    };

    struct DistortionState {
        float low = 0.0f;
        float high = 0.0f;
        float memory = 0.0f;
        float envelope = 0.0f;
        float previous = 0.0f;
    };

    struct InsertRuntime {
        std::array<DistortionState, kNoInputDistortionTypeCount> states {};
        NoInputDistortionType currentType = NoInputDistortionType::Bypass;
        NoInputDistortionType previousType = NoInputDistortionType::Bypass;
        float crossfade = 1.0f;
        bool initialized = false;
    };

    struct LaneState {
        std::array<Resonator, 4u> resonators {};
        Biquad lowShelf {};
        Biquad midPeak {};
        Biquad highShelf {};
        std::array<InsertRuntime, kNoInputMixerInsertSlots> inserts {};
        float formantLow = 0.0f;
        float formantHigh = 0.0f;
        float returnDcInput = 0.0f;
        float returnDcOutput = 0.0f;
        float outputDcInput = 0.0f;
        float outputDcOutput = 0.0f;
        float internalToneLow = 0.0f;
        float houseToneLow = 0.0f;
        float energy = 0.0f;
        float governor = 1.0f;
    };

    struct AuxState {
        InsertRuntime insert {};
        float dcInput = 0.0f;
        float dcOutput = 0.0f;
        float activity = 0.0f;
    };

    static void setBiquad(Biquad& biquad,
        float b0, float b1, float b2, float a0, float a1, float a2)
    {
        const float inverseA0 = 1.0f / std::max(1.0e-9f, a0);
        biquad.b0 = b0 * inverseA0;
        biquad.b1 = b1 * inverseA0;
        biquad.b2 = b2 * inverseA0;
        biquad.a1 = a1 * inverseA0;
        biquad.a2 = a2 * inverseA0;
    }

    void setLowShelf(Biquad& biquad, float frequency, float gainDb)
    {
        const float a = std::pow(10.0f, gainDb / 40.0f);
        const float omega = 2.0f * kPi * clamp(frequency, 20.0f,
            static_cast<float>(sampleRate_ * 0.45))
            / static_cast<float>(sampleRate_);
        const float cosine = std::cos(omega);
        const float sine = std::sin(omega);
        const float alpha = sine * std::sqrt((a + 1.0f / a) * 2.0f);
        const float beta = 2.0f * std::sqrt(a) * alpha;
        setBiquad(biquad,
            a * ((a + 1.0f) - (a - 1.0f) * cosine + beta),
            2.0f * a * ((a - 1.0f) - (a + 1.0f) * cosine),
            a * ((a + 1.0f) - (a - 1.0f) * cosine - beta),
            (a + 1.0f) + (a - 1.0f) * cosine + beta,
            -2.0f * ((a - 1.0f) + (a + 1.0f) * cosine),
            (a + 1.0f) + (a - 1.0f) * cosine - beta);
    }

    void setHighShelf(Biquad& biquad, float frequency, float gainDb)
    {
        const float a = std::pow(10.0f, gainDb / 40.0f);
        const float omega = 2.0f * kPi * clamp(frequency, 20.0f,
            static_cast<float>(sampleRate_ * 0.45))
            / static_cast<float>(sampleRate_);
        const float cosine = std::cos(omega);
        const float sine = std::sin(omega);
        const float alpha = sine * std::sqrt((a + 1.0f / a) * 2.0f);
        const float beta = 2.0f * std::sqrt(a) * alpha;
        setBiquad(biquad,
            a * ((a + 1.0f) + (a - 1.0f) * cosine + beta),
            -2.0f * a * ((a - 1.0f) + (a + 1.0f) * cosine),
            a * ((a + 1.0f) + (a - 1.0f) * cosine - beta),
            (a + 1.0f) - (a - 1.0f) * cosine + beta,
            2.0f * ((a - 1.0f) - (a + 1.0f) * cosine),
            (a + 1.0f) - (a - 1.0f) * cosine - beta);
    }

    void setPeaking(Biquad& biquad, float frequency, float q, float gainDb)
    {
        const float a = std::pow(10.0f, gainDb / 40.0f);
        const float omega = 2.0f * kPi * clamp(frequency, 20.0f,
            static_cast<float>(sampleRate_ * 0.45))
            / static_cast<float>(sampleRate_);
        const float alpha = std::sin(omega) / (2.0f * std::max(0.1f, q));
        const float cosine = std::cos(omega);
        setBiquad(biquad,
            1.0f + alpha * a,
            -2.0f * cosine,
            1.0f - alpha * a,
            1.0f + alpha / a,
            -2.0f * cosine,
            1.0f - alpha / a);
    }

    void updateEq(uint32_t lane)
    {
        if (lane >= kNoInputMixerChannels) return;
        auto& state = laneState_[lane];
        const auto& params = params_.lanes[lane];
        setLowShelf(state.lowShelf, 180.0f, params.lowDb);
        setPeaking(state.midPeak, params.midFrequencyHz, 0.82f,
            params.midGainDb);
        setHighShelf(state.highShelf, 4200.0f, params.highDb);
    }

    void updateBodyCoefficients(uint32_t lane)
    {
        static constexpr std::array<float, 4u> ratios {{
            1.0f, 1.47f, 2.11f, 3.29f,
        }};
        if (lane >= kNoInputMixerChannels) return;
        const auto& params = params_.lanes[lane];
        const float laneSpread = 1.0f + (static_cast<float>(lane) - 3.5f)
            * 0.013f;
        const float base = 44.0f * std::pow(2.0f, params.body * 5.0f)
            * laneSpread;
        const float decaySeconds = lerp(2.8f, 0.055f, params.loss);
        const float radius = std::exp(-1.0f
            / std::max(1.0f,
                static_cast<float>(sampleRate_) * decaySeconds));
        for (uint32_t mode = 0u; mode < 4u; ++mode) {
            const float detune = 1.0f + driftState_[lane * 8u + mode]
                * params_.drift * 0.018f;
            const float frequency = clamp(base * ratios[mode] * detune,
                24.0f, static_cast<float>(sampleRate_ * 0.42));
            const float omega = 2.0f * kPi * frequency
                / static_cast<float>(sampleRate_);
            auto& resonator = laneState_[lane].resonators[mode];
            resonator.c1 = 2.0f * radius * std::cos(omega);
            resonator.c2 = -radius * radius;
            resonator.inputGain = std::max(0.00015f,
                (1.0f - radius) * (2.4f + 0.35f * mode));
        }
    }

    void initializeInsertTypes(uint32_t lane)
    {
        for (uint32_t slot = 0u;
             slot < kNoInputMixerInsertSlots; ++slot) {
            auto& runtime = laneState_[lane].inserts[slot];
            const auto& insert = params_.lanes[lane].inserts[slot];
            const auto type = insert.bypass != 0u
                ? NoInputDistortionType::Bypass : insert.type;
            runtime.currentType = type;
            runtime.previousType = type;
            runtime.crossfade = 1.0f;
            runtime.initialized = true;
        }
    }

    float processBody(uint32_t lane, float input)
    {
        auto& state = laneState_[lane];
        float body = 0.0f;
        for (uint32_t mode = 0u; mode < 4u; ++mode) {
            const float signedInput = (mode & 1u) == 0u ? input : -input;
            body += state.resonators[mode].process(signedInput);
        }
        body *= 0.31f;
        return std::tanh(input * 0.24f + body * 1.38f);
    }

    float processFormant(uint32_t lane, float input)
    {
        auto& state = laneState_[lane];
        const float body = params_.lanes[lane].body;
        const float lowHz = 120.0f + body * 780.0f;
        const float highHz = 1300.0f + body * 5600.0f;
        const float lowCoefficient = 1.0f - std::exp(-2.0f * kPi * lowHz
            / static_cast<float>(sampleRate_));
        const float highCoefficient = 1.0f - std::exp(-2.0f * kPi * highHz
            / static_cast<float>(sampleRate_));
        state.formantLow += (input - state.formantLow) * lowCoefficient;
        state.formantHigh += (input - state.formantHigh) * highCoefficient;
        state.formantLow = flushDenormal(state.formantLow);
        state.formantHigh = flushDenormal(state.formantHigh);
        const float highpass = input - state.formantLow;
        const float product = std::tanh(
            highpass * state.formantHigh * 5.5f);
        return lerp(input, product, params_.formant * 0.82f);
    }

    float processInsert(uint32_t lane, uint32_t slot,
        float input, float ringSource)
    {
        auto& runtime = laneState_[lane].inserts[slot];
        const auto& params = params_.lanes[lane].inserts[slot];
        const uint32_t substeps = 1u << params_.quality;
        const float step = 1.0f / static_cast<float>(substeps);
        float output = 0.0f;
        const float previousInput = runtime.states[
            static_cast<uint32_t>(runtime.currentType)].previous;
        for (uint32_t substep = 0u; substep < substeps; ++substep) {
            const float fraction = static_cast<float>(substep + 1u) * step;
            const float sample = lerp(previousInput, input, fraction);
            const float current = processDistortion(runtime, runtime.currentType,
                sample, ringSource, params);
            if (runtime.crossfade < 1.0f) {
                const float previous = processDistortion(runtime,
                    runtime.previousType, sample, ringSource, params);
                output += lerp(previous, current, runtime.crossfade);
            } else {
                output += current;
            }
        }
        runtime.states[static_cast<uint32_t>(runtime.currentType)].previous = input;
        output *= step;
        if (runtime.crossfade < 1.0f) {
            runtime.crossfade = std::min(1.0f,
                runtime.crossfade + static_cast<float>(substeps)
                    / static_cast<float>(sampleRate_ * 0.020));
        }
        return flushDenormal(output * dbToGain(params.levelDb));
    }

    float processAuxInsert(uint32_t bus, float input, float ringSource)
    {
        auto& runtime = auxState_[bus].insert;
        const auto& params = params_.aux[bus].effect;
        const uint32_t substeps = 1u << params_.quality;
        const float step = 1.0f / static_cast<float>(substeps);
        float output = 0.0f;
        const float previousInput = runtime.states[
            static_cast<uint32_t>(runtime.currentType)].previous;
        for (uint32_t substep = 0u; substep < substeps; ++substep) {
            const float fraction = static_cast<float>(substep + 1u) * step;
            const float sample = lerp(previousInput, input, fraction);
            const float current = processDistortion(runtime,
                runtime.currentType, sample, ringSource, params);
            if (runtime.crossfade < 1.0f) {
                const float previous = processDistortion(runtime,
                    runtime.previousType, sample, ringSource, params);
                output += lerp(previous, current, runtime.crossfade);
            } else {
                output += current;
            }
        }
        runtime.states[static_cast<uint32_t>(runtime.currentType)].previous
            = input;
        output *= step;
        if (runtime.crossfade < 1.0f) {
            runtime.crossfade = std::min(1.0f,
                runtime.crossfade + static_cast<float>(substeps)
                    / static_cast<float>(sampleRate_ * 0.020));
        }
        return flushDenormal(output);
    }

    void processAuxReturns()
    {
        for (uint32_t bus = 0u; bus < 2u; ++bus) {
            float input = previousAuxReturns_[bus]
                * params_.aux[bus].feedback;
            float sendWeight = 0.0f;
            for (uint32_t lane = 0u; lane < kNoInputMixerChannels;
                 ++lane) {
                const float send = params_.lanes[lane].auxSend[bus];
                input += previousReturns_[lane] * send;
                sendWeight += send;
            }
            input /= std::max(1.0f, 0.7f + sendWeight * 0.52f);
            const float ringSource = previousAuxReturns_[1u - bus];
            float value = processAuxInsert(bus, clamp(input, -5.0f, 5.0f),
                ringSource);
            value = dcBlock(clamp(value, -6.0f, 6.0f),
                auxState_[bus].dcInput, auxState_[bus].dcOutput);
            auxReturns_[bus] = flushDenormal(value);
            auxState_[bus].activity += (std::abs(value)
                - auxState_[bus].activity) * 0.0025f;
            auxActivity_[bus] = auxState_[bus].activity;
        }
    }

    float processDistortion(InsertRuntime& runtime,
        NoInputDistortionType type, float input, float ringSource,
        const NoInputInsertParams& params)
    {
        auto& state = runtime.states[static_cast<uint32_t>(type)];
        const float gain = params.gain;
        const float tone = params.tone;
        const float bias = params.bias * 0.22f;
        const float sr = static_cast<float>(sampleRate_)
            * static_cast<float>(1u << params_.quality);
        const auto onePole = [sr](float frequency) {
            return 1.0f - std::exp(-2.0f * kPi
                * std::min(frequency, sr * 0.45f) / sr);
        };
        switch (type) {
        case NoInputDistortionType::Bypass:
            return input;
        case NoInputDistortionType::Wool: {
            const float drive = 1.0f + gain * gain * 44.0f;
            const float first = std::tanh((input + bias) * drive);
            state.memory += (first - state.memory) * onePole(5200.0f);
            const float second = std::tanh(
                (state.memory - bias * 0.45f) * (2.2f + gain * 6.0f));
            state.low += (second - state.low)
                * onePole(lerp(360.0f, 2200.0f, tone));
            const float high = second - state.low;
            return std::tanh(lerp(state.low * 1.7f, high * 1.45f,
                tone) * 1.05f);
        }
        case NoInputDistortionType::Rat: {
            state.low += (input - state.low) * onePole(720.0f);
            const float pre = input - state.low * 0.78f;
            const float driven = pre * (2.0f + gain * gain * 78.0f) + bias;
            const float clipped = clamp(driven, -0.72f, 0.72f) / 0.72f;
            const float cutoff = 14000.0f
                * std::pow(420.0f / 14000.0f, tone);
            state.memory += (clipped - state.memory) * onePole(cutoff);
            return state.memory;
        }
        case NoInputDistortionType::ZoneA:
        case NoInputDistortionType::ZoneB: {
            const bool lower = type == NoInputDistortionType::ZoneB;
            const float lowCut = lower ? 120.0f : 260.0f;
            state.low += (input - state.low) * onePole(lowCut);
            const float upper = input - state.low;
            const float first = std::tanh((upper * (3.0f + gain * 54.0f)
                + bias) * (lower ? 0.72f : 1.0f));
            const float focusHz = lerp(lower ? 420.0f : 780.0f,
                lower ? 2600.0f : 5200.0f, tone);
            state.high += (first - state.high) * onePole(focusHz);
            const float focused = lower
                ? first + state.high * 0.48f
                : first + (first - state.high) * 0.72f;
            return std::tanh((focused - bias * 0.35f)
                * (2.2f + gain * 5.5f));
        }
        case NoInputDistortionType::FuzzI: {
            const float absolute = std::abs(input);
            state.envelope += (absolute - state.envelope)
                * onePole(lerp(4.0f, 30.0f, tone));
            const float sag = 1.0f
                / (1.0f + state.envelope * (1.0f + gain * 8.0f));
            const float drive = (2.0f + gain * gain * 64.0f) * sag;
            return std::tanh((input + bias) * drive)
                - std::tanh(bias * drive);
        }
        case NoInputDistortionType::FuzzII: {
            const float threshold = lerp(0.22f, 0.015f, gain);
            const float hold = state.memory;
            const float driven = input * (3.0f + gain * 72.0f) + bias;
            if (driven > threshold) state.memory = 1.0f;
            else if (driven < -threshold) state.memory = -1.0f;
            const float speed = onePole(lerp(900.0f, 9000.0f, tone));
            return lerp(hold, state.memory, speed);
        }
        case NoInputDistortionType::Diode: {
            const float drive = 1.0f + gain * gain * 36.0f;
            const float positive = std::tanh((input + bias) * drive);
            const float negative = std::tanh((input - bias)
                * drive * lerp(0.68f, 1.32f, tone));
            return (positive + negative) * 0.5f;
        }
        case NoInputDistortionType::Ring: {
            const float depth = gain;
            const float carrier = std::tanh(ringSource
                * (1.0f + tone * 8.0f));
            return lerp(input, input * carrier * 2.0f + bias, depth);
        }
        case NoInputDistortionType::Count:
            break;
        }
        return input;
    }

    float dcBlock(float input, float& previousInput, float& previousOutput)
    {
        if (params_.dcBlockEnabled == 0u) return input;
        const float output = input - previousInput + dcPole_ * previousOutput;
        previousInput = input;
        previousOutput = flushDenormal(output);
        return previousOutput;
    }

    float processTone(float input, float tone, float& lowState)
    {
        const float coefficient = 1.0f - std::exp(-2.0f * kPi * 1150.0f
            / static_cast<float>(sampleRate_));
        lowState += (input - lowState) * coefficient;
        lowState = flushDenormal(lowState);
        if (tone < 0.0f) {
            return lerp(input, lowState, -tone * 0.88f);
        }
        const float high = input - lowState;
        return input + high * tone * 1.25f;
    }

    static float softLimit(float input, float ceiling)
    {
        ceiling = std::max(0.001f, ceiling);
        const float absolute = std::abs(input);
        const float knee = ceiling * 0.72f;
        if (absolute <= knee) return input;
        const float span = std::max(1.0e-6f, ceiling - knee);
        const float limited = knee + span
            * (1.0f - std::exp(-(absolute - knee) / span));
        return std::copysign(std::min(ceiling, limited), input);
    }

    float seedForLane(uint32_t lane)
    {
        if (seedRemaining_ == 0u) return 0.0f;
        const float phase = static_cast<float>(seedRemaining_)
            / static_cast<float>(std::max<uint32_t>(1u,
                static_cast<uint32_t>(sampleRate_ * 0.018)));
        const float envelope = phase * phase;
        const float random = randomSigned();
        const float polarity = (lane & 1u) == 0u ? 1.0f : -1.0f;
        return random * envelope * seedAmount_ * 0.34f
            * polarity * (0.72f + 0.055f * static_cast<float>(lane));
    }

    void updateSlowControl()
    {
        rebuildMotionTargets();
        for (uint32_t index = 0u;
             index < kNoInputMixerMatrixCells; ++index) {
            driftVelocity_[index] += randomSigned() * 0.014f;
            driftVelocity_[index] *= 0.972f;
            driftState_[index] += driftVelocity_[index] * 0.025f;
            driftState_[index] *= 0.9992f;
            if (driftState_[index] > 1.0f) {
                driftState_[index] = 1.0f;
                driftVelocity_[index] = -std::abs(driftVelocity_[index]);
            } else if (driftState_[index] < -1.0f) {
                driftState_[index] = -1.0f;
                driftVelocity_[index] = std::abs(driftVelocity_[index]);
            }
            motionCurrent_[index] += (motionTarget_[index]
                - motionCurrent_[index]) * 0.16f;
            const uint32_t source = index % kNoInputMixerChannels;
            const float closeThreshold = 0.0015f + params_.space
                * (0.010f + hashUnit(index + 0x53504143u) * 0.022f);
            const float openThreshold = closeThreshold
                * (1.8f + params_.agency * 1.6f);
            const float activity = laneActivity_[source];
            float gateTarget = routeSpaceGate_[index];
            if (seedRemaining_ > 0u || activity > openThreshold) {
                gateTarget = 1.0f;
            } else if (activity < closeThreshold) {
                gateTarget = 0.025f;
            }
            const float gateSpeed = gateTarget > routeSpaceGate_[index]
                ? 0.045f : 0.012f;
            routeSpaceGate_[index] += (gateTarget
                - routeSpaceGate_[index]) * gateSpeed;
        }
        for (uint32_t lane = 0u; lane < kNoInputMixerChannels; ++lane) {
            updateBodyCoefficients(lane);
        }
    }

    void rebuildMotionTargets()
    {
        const auto generated = noInputMixerMotionWeights(params_,
            motionPhase_);
        for (uint32_t index = 0u; index < kNoInputMixerMatrixCells;
             ++index) {
            motionTarget_[index] = generated[index];
        }
    }

    void clearSignalState()
    {
        laneState_ = {};
        returns_.fill(0.0f);
        previousReturns_.fill(0.0f);
        routeSignals_.fill(0.0f);
        phaseMemory_.fill(0.0f);
        driftState_.fill(0.0f);
        driftVelocity_.fill(0.0f);
        motionTarget_.fill(1.0f);
        motionCurrent_.fill(1.0f);
        routeSpaceGate_.fill(1.0f);
        auxState_ = {};
        auxReturns_.fill(0.0f);
        previousAuxReturns_.fill(0.0f);
        auxActivity_.fill(0.0f);
        lanePeak_.fill(0.0f);
        laneActivity_.fill(0.0f);
        networkActivity_ = 0.0f;
        controlCounter_ = 0u;
        motionPhase_ = params_.motionPhase;
        for (uint32_t lane = 0u; lane < kNoInputMixerChannels; ++lane) {
            updateEq(lane);
            updateBodyCoefficients(lane);
            initializeInsertTypes(lane);
        }
        for (uint32_t bus = 0u; bus < 2u; ++bus) {
            auto& runtime = auxState_[bus].insert;
            const auto type = params_.aux[bus].effect.type;
            runtime.currentType = type;
            runtime.previousType = type;
            runtime.crossfade = 1.0f;
            runtime.initialized = true;
        }
        rebuildMotionTargets();
        motionCurrent_ = motionTarget_;
    }

    float minimumLaneGovernor() const
    {
        float result = 1.0f;
        for (const auto& lane : laneState_) {
            result = std::min(result, lane.governor);
        }
        return result;
    }

    uint32_t randomU32()
    {
        randomState_ += 0x9e3779b9u;
        uint32_t value = randomState_;
        value ^= value >> 16u;
        value *= 0x7feb352du;
        value ^= value >> 15u;
        value *= 0x846ca68bu;
        value ^= value >> 16u;
        return value;
    }

    float randomSigned()
    {
        return static_cast<float>(randomU32() & 0x00ffffffu)
            / static_cast<float>(0x00800000u) - 1.0f;
    }

    static float hashUnit(uint32_t value)
    {
        value ^= value >> 16u;
        value *= 0x7feb352du;
        value ^= value >> 15u;
        value *= 0x846ca68bu;
        value ^= value >> 16u;
        return static_cast<float>(value & 0x00ffffffu)
            / static_cast<float>(0x01000000u);
    }

    double sampleRate_ = 48000.0;
    NoInputMixerParams params_ = defaultNoInputMixerParams();
    std::array<LaneState, kNoInputMixerChannels> laneState_ {};
    std::array<AuxState, 2u> auxState_ {};
    std::array<float, kNoInputMixerChannels> returns_ {};
    std::array<float, kNoInputMixerChannels> previousReturns_ {};
    std::array<float, kNoInputMixerMatrixCells> routeSignals_ {};
    std::array<float, 2u> auxReturns_ {};
    std::array<float, 2u> previousAuxReturns_ {};
    std::array<float, 2u> auxActivity_ {};
    std::array<float, kNoInputMixerMatrixCells> phaseMemory_ {};
    std::array<float, kNoInputMixerMatrixCells> driftState_ {};
    std::array<float, kNoInputMixerMatrixCells> driftVelocity_ {};
    std::array<float, kNoInputMixerMatrixCells> motionTarget_ {};
    std::array<float, kNoInputMixerMatrixCells> motionCurrent_ {};
    std::array<float, kNoInputMixerMatrixCells> routeSpaceGate_ {};
    std::array<float, kNoInputMixerChannels> lanePeak_ {};
    std::array<float, kNoInputMixerChannels> laneActivity_ {};
    float networkActivity_ = 0.0f;
    float dcPole_ = 0.998f;
    float energyAttack_ = 0.002f;
    float energyRelease_ = 0.0001f;
    float governorAttack_ = 0.001f;
    float governorRelease_ = 0.00005f;
    float seedAmount_ = 0.45f;
    float motionPhase_ = 0.0f;
    uint32_t seedRemaining_ = 0u;
    uint32_t randomState_ = 0x5455444fu;
    uint32_t controlCounter_ = 0u;
    uint32_t panicRemaining_ = 0u;
    uint32_t panicSamplesTotal_ = 384u;
    bool silenced_ = true;
    NoInputContainmentState containmentState_ =
        NoInputContainmentState::Quiet;
};

} // namespace s3g
