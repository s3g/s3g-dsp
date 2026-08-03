#pragma once

#include "s3g_ambisonic_speaker_decoder.h"
#include "s3g_environmental_score.h"
#include "s3g_math.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

inline constexpr uint32_t kAmbiHorizonMaxOrder = 7u;
inline constexpr uint32_t kAmbiHorizonMaxChannels = 64u;
inline constexpr uint32_t kAmbiHorizonMaxEntities = 32u;

enum class AmbiHorizonEcology : uint32_t {
    Mixed = 0u,
    Rural,
    Traffic,
    City,
    Industrial,
    Water,
    Weather,
};

enum class AmbiHorizonGround : uint32_t {
    Water = 0u,
    Hard,
    Mixed,
    Grass,
    Forest,
};

enum class AmbiHorizonLayer : uint8_t {
    LocalFloor = 0u,
    HorizonBed,
    HorizonSignal,
};

struct AmbiHorizonEncoderParams {
    uint32_t order = 3u;
    uint32_t entities = 24u;
    AmbiHorizonEcology ecology = AmbiHorizonEcology::Mixed;
    float activity = 0.48f;
    float occupancy = 0.36f;
    float pace = 0.42f;
    float memory = 0.68f;
    float cascade = 0.48f;
    float signals = 0.48f;
    float horizonBed = 0.62f;
    float localFloor = 0.22f;
    float rangeKm = 2.8f;
    float azimuthDeg = 0.0f;
    float elevationDeg = 0.0f;
    float arcDeg = 240.0f;
    float detail = 0.54f;
    float air = 0.58f;
    AmbiHorizonGround ground = AmbiHorizonGround::Mixed;
    float terrain = 0.42f;
    float carry = 0.0f;
    float turbulence = 0.24f;
    float edgeDb = 0.0f;
    float outputGainDb = -6.0f;
    uint32_t seed = 1979u;
};

struct AmbiHorizonEntityTelemetry {
    float azimuthDeg = 0.0f;
    float elevationDeg = 0.0f;
    float rangeNorm = 0.0f;
    float energy = 0.0f;
    AmbiHorizonLayer layer = AmbiHorizonLayer::HorizonBed;
};

// Autonomous acoustic-horizon scene generator. Source synthesis happens in
// mono per entity; outdoor propagation is applied before the final ACN/SN3D
// encoding stage. The kilometer control is perceptual rather than a calibrated
// sound-pressure model: it preserves the defining condition that very distant
// events remain faintly audible in a low-noise environment.
class AmbiHorizonEncoder {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = std::max(1000.0, sampleRate);
        score_.prepare(sampleRate_, params_.seed);
        score_.setRangeExpansion(1.0f);
        rebuildVoices();
        reset();
    }

    void reset()
    {
        score_.prepare(sampleRate_, params_.seed);
        score_.setRangeExpansion(1.0f);
        score_.reset(params_.entities);
        rebuildVoices();
        frameCounter_ = 0u;
        for (uint32_t index = 0u; index < kAmbiHorizonMaxEntities; ++index) {
            auto& voice = voices_[index];
            voice.phase = randomUnit(voice.rng);
            voice.phase2 = randomUnit(voice.rng);
            voice.slowPhase = randomUnit(voice.rng);
            voice.fastPhase = randomUnit(voice.rng);
            voice.envelope = 0.0f;
            voice.eventEnvelope = 0.0f;
            voice.low = 0.0f;
            voice.low2 = 0.0f;
            voice.band = 0.0f;
            voice.modal1 = 0.0f;
            voice.modal2 = 0.0f;
            voice.energy = 0.0f;
        }
        updateControl(64u);
    }

    void setParams(AmbiHorizonEncoderParams params)
    {
        params.order = std::clamp<uint32_t>(params.order, 1u, kAmbiHorizonMaxOrder);
        params.entities = std::clamp<uint32_t>(params.entities, 4u, kAmbiHorizonMaxEntities);
        params.ecology = static_cast<AmbiHorizonEcology>(std::min<uint32_t>(
            static_cast<uint32_t>(params.ecology), 6u));
        params.ground = static_cast<AmbiHorizonGround>(std::min<uint32_t>(
            static_cast<uint32_t>(params.ground), 4u));
        params.activity = finiteUnit(params.activity, params_.activity);
        params.occupancy = finiteUnit(params.occupancy, params_.occupancy);
        params.pace = finiteUnit(params.pace, params_.pace);
        params.memory = finiteUnit(params.memory, params_.memory);
        params.cascade = finiteUnit(params.cascade, params_.cascade);
        params.signals = finiteUnit(params.signals, params_.signals);
        params.horizonBed = finiteUnit(params.horizonBed, params_.horizonBed);
        params.localFloor = finiteUnit(params.localFloor, params_.localFloor);
        params.rangeKm = std::clamp(finite(params.rangeKm, params_.rangeKm), 0.03f, 20.0f);
        params.azimuthDeg = std::clamp(finite(params.azimuthDeg, params_.azimuthDeg), -180.0f, 180.0f);
        params.elevationDeg = std::clamp(finite(params.elevationDeg, params_.elevationDeg), -20.0f, 20.0f);
        params.arcDeg = std::clamp(finite(params.arcDeg, params_.arcDeg), 0.0f, 360.0f);
        params.detail = finiteUnit(params.detail, params_.detail);
        params.air = finiteUnit(params.air, params_.air);
        params.terrain = finiteUnit(params.terrain, params_.terrain);
        params.carry = std::clamp(finite(params.carry, params_.carry), -1.0f, 1.0f);
        params.turbulence = finiteUnit(params.turbulence, params_.turbulence);
        params.edgeDb = std::clamp(finite(params.edgeDb, params_.edgeDb), -18.0f, 9.0f);
        params.outputGainDb = std::clamp(finite(params.outputGainDb, params_.outputGainDb), -60.0f, 12.0f);
        params.seed = std::clamp<uint32_t>(params.seed, 1u, 65535u);

        const bool identityChanged = params.seed != params_.seed
            || params.ecology != params_.ecology
            || params.entities != params_.entities;
        params_ = params;
        if (identityChanged) {
            score_.prepare(sampleRate_, params_.seed);
            score_.setRangeExpansion(1.0f);
            rebuildVoices();
        }
        updateScoreParams();
    }

    const AmbiHorizonEncoderParams& params() const { return params_; }

    uint32_t activeChannels() const
    {
        return (params_.order + 1u) * (params_.order + 1u);
    }

    uint32_t activeEntities() const { return params_.entities; }

    AmbiHorizonEntityTelemetry entityTelemetry(uint32_t index) const
    {
        if (index >= params_.entities) return {};
        const auto& voice = voices_[index];
        return { voice.azimuthDeg, voice.elevationDeg, voice.rangeNorm,
            voice.energy, voice.layer };
    }

    void processBlock(float* const* outputs, uint32_t outputChannels,
                      uint32_t frames)
    {
        if (!outputs || frames == 0u) return;
        const uint32_t channels = std::min<uint32_t>(
            outputChannels, activeChannels());
        for (uint32_t channel = 0u; channel < outputChannels; ++channel) {
            clearChannel(outputs[channel], frames);
        }
        if (channels == 0u) return;

        updateControl(frames);
        const float master = dbToGain(params_.outputGainDb);
        // The horizon is deliberately low-level, but the original preview
        // reference placed its quietest startup near -73 dBFS RMS and was
        // effectively silent at normal monitor gain. Preserve density
        // normalization while lifting the scene into an audible distant-field
        // range; OUT still supplies 18 dB of downward trim and 12 dB upward.
        const float densityGain = 1.0f
            / std::sqrt(std::max(1.0f, static_cast<float>(params_.entities) / 8.0f));
        const float edgeGain = dbToGain(params_.edgeDb);
        for (uint32_t index = 0u; index < params_.entities; ++index) {
            auto& voice = voices_[index];
            const float layerGain = voice.layer == AmbiHorizonLayer::LocalFloor
                ? params_.localFloor
                : edgeGain * (voice.layer == AmbiHorizonLayer::HorizonBed
                    ? params_.horizonBed : params_.signals);
            voice.blockGain = layerGain * master * densityGain;
        }

        for (uint32_t frame = 0u; frame < frames; ++frame) {
            for (uint32_t index = 0u; index < params_.entities; ++index) {
                auto& voice = voices_[index];
                const float raw = renderVoice(voice);
                const float propagated = propagate(voice, raw);
                const float sample = propagated * voice.blockGain;
                voice.energy += (std::abs(sample) - voice.energy) * 0.0025f;
                for (uint32_t channel = 0u; channel < channels; ++channel) {
                    if (!outputs[channel]) continue;
                    outputs[channel][frame] += sample
                        * voice.encodingWeights[channel];
                }
            }
        }
        frameCounter_ += frames;
    }

private:
    enum class SourceKind : uint8_t { Flow, Roll, Motor, Modal, Air, Pulse };

    struct Voice {
        uint32_t rng = 1u;
        SourceKind kind = SourceKind::Flow;
        AmbiHorizonLayer layer = AmbiHorizonLayer::HorizonBed;
        float phase = 0.0f;
        float phase2 = 0.0f;
        float slowPhase = 0.0f;
        float fastPhase = 0.0f;
        float baseFrequency = 80.0f;
        float brightness = 0.5f;
        float identity = 0.5f;
        float positionJitter = 0.0f;
        float rangeJitter = 0.0f;
        float envelope = 0.0f;
        float targetEnvelope = 0.0f;
        float eventEnvelope = 0.0f;
        float low = 0.0f;
        float low2 = 0.0f;
        float band = 0.0f;
        float modal1 = 0.0f;
        float modal2 = 0.0f;
        float filterAlpha = 0.1f;
        float propagationGain = 1.0f;
        float modalCoefficient = 0.0f;
        float modalRadiusSquared = 0.0f;
        float blockGain = 0.0f;
        float azimuthDeg = 0.0f;
        float elevationDeg = 0.0f;
        float rangeNorm = 0.5f;
        float energy = 0.0f;
        std::array<float, kAmbiHorizonMaxChannels> basis {};
        std::array<float, kAmbiHorizonMaxChannels> encodingWeights {};
    };

    static float finite(float value, float fallback)
    {
        return std::isfinite(value) ? value : fallback;
    }

    static float finiteUnit(float value, float fallback)
    {
        return std::clamp(finite(value, fallback), 0.0f, 1.0f);
    }

    static uint32_t nextRandom(uint32_t& state)
    {
        state ^= state << 13u;
        state ^= state >> 17u;
        state ^= state << 5u;
        return state;
    }

    static float randomUnit(uint32_t& state)
    {
        return static_cast<float>(nextRandom(state) & 0x00ffffffu)
            / static_cast<float>(0x01000000u);
    }

    static float wrapPhase(float phase)
    {
        phase -= std::floor(phase);
        return phase;
    }

    static float wrapDegrees(float degrees)
    {
        while (degrees > 180.0f) degrees -= 360.0f;
        while (degrees < -180.0f) degrees += 360.0f;
        return degrees;
    }

    void updateScoreParams()
    {
        score_.setParams({
            params_.pace,
            params_.occupancy * params_.activity,
            params_.cascade,
            params_.memory,
            0.35f + 0.55f * params_.memory,
        });
    }

    SourceKind selectSource(uint32_t index) const
    {
        static constexpr SourceKind mixed[] {
            SourceKind::Flow, SourceKind::Motor, SourceKind::Modal,
            SourceKind::Air, SourceKind::Roll, SourceKind::Pulse };
        static constexpr SourceKind rural[] {
            SourceKind::Air, SourceKind::Modal, SourceKind::Flow,
            SourceKind::Pulse, SourceKind::Roll, SourceKind::Modal };
        static constexpr SourceKind traffic[] {
            SourceKind::Roll, SourceKind::Motor, SourceKind::Flow,
            SourceKind::Motor, SourceKind::Roll, SourceKind::Air };
        static constexpr SourceKind city[] {
            SourceKind::Motor, SourceKind::Roll, SourceKind::Air,
            SourceKind::Pulse, SourceKind::Flow, SourceKind::Motor };
        static constexpr SourceKind industrial[] {
            SourceKind::Motor, SourceKind::Pulse, SourceKind::Modal,
            SourceKind::Roll, SourceKind::Motor, SourceKind::Air };
        static constexpr SourceKind water[] {
            SourceKind::Flow, SourceKind::Roll, SourceKind::Air,
            SourceKind::Flow, SourceKind::Modal, SourceKind::Roll };
        static constexpr SourceKind weather[] {
            SourceKind::Flow, SourceKind::Air, SourceKind::Roll,
            SourceKind::Flow, SourceKind::Pulse, SourceKind::Air };
        const SourceKind* set = mixed;
        switch (params_.ecology) {
        case AmbiHorizonEcology::Rural: set = rural; break;
        case AmbiHorizonEcology::Traffic: set = traffic; break;
        case AmbiHorizonEcology::City: set = city; break;
        case AmbiHorizonEcology::Industrial: set = industrial; break;
        case AmbiHorizonEcology::Water: set = water; break;
        case AmbiHorizonEcology::Weather: set = weather; break;
        default: break;
        }
        return set[index % 6u];
    }

    void rebuildVoices()
    {
        uint32_t sceneRng = params_.seed ? params_.seed : 1u;
        for (uint32_t index = 0u; index < kAmbiHorizonMaxEntities; ++index) {
            auto& voice = voices_[index];
            voice = {};
            voice.rng = nextRandom(sceneRng) ^ ((index + 1u) * 0x9e3779b9u);
            if (voice.rng == 0u) voice.rng = index + 1u;
            voice.kind = selectSource(index);
            voice.identity = randomUnit(voice.rng);
            voice.brightness = randomUnit(voice.rng);
            voice.positionJitter = randomUnit(voice.rng) * 2.0f - 1.0f;
            voice.rangeJitter = randomUnit(voice.rng) * 2.0f - 1.0f;
            const float pitchBias = static_cast<float>(params_.ecology == AmbiHorizonEcology::Rural
                || params_.ecology == AmbiHorizonEcology::Water);
            voice.baseFrequency = 32.0f * std::pow(2.0f,
                voice.identity * (2.4f + pitchBias * 1.8f));
            voice.phase = randomUnit(voice.rng);
            voice.phase2 = randomUnit(voice.rng);
            voice.slowPhase = randomUnit(voice.rng);
            voice.fastPhase = randomUnit(voice.rng);
        }
        updateScoreParams();
        updateControl(64u);
    }

    void updateControl(uint32_t frames)
    {
        const float dt = static_cast<float>(frames / sampleRate_);
        updateScoreParams();
        score_.update(dt, params_.entities);

        const uint32_t localCount = std::min<uint32_t>(4u, params_.entities);
        const uint32_t remaining = params_.entities - localCount;
        const uint32_t signalCount = std::min<uint32_t>(remaining,
            std::max<uint32_t>(1u, static_cast<uint32_t>(
                std::lround((0.08f + 0.34f * params_.signals) * remaining))));
        const float logRange = std::log(params_.rangeKm / 0.03f)
            / std::log(20.0f / 0.03f);
        const float elapsed = static_cast<float>(frameCounter_ / sampleRate_);
        const uint32_t channels = activeChannels();
        const float detailBase = 0.28f + 0.72f * params_.detail;

        for (uint32_t index = 0u; index < params_.entities; ++index) {
            auto& voice = voices_[index];
            const bool local = index < localCount;
            const uint32_t horizonIndex = index - std::min(index, localCount);
            const bool signal = !local && horizonIndex < signalCount;
            voice.layer = local ? AmbiHorizonLayer::LocalFloor
                : (signal ? AmbiHorizonLayer::HorizonSignal
                          : AmbiHorizonLayer::HorizonBed);

            const float spreadIndex = remaining > 1u
                ? static_cast<float>(horizonIndex) / static_cast<float>(remaining - 1u) - 0.5f
                : 0.0f;
            const float localAngle = 360.0f * static_cast<float>(index)
                / static_cast<float>(std::max<uint32_t>(1u, localCount));
            const float slowDrift = std::sin(
                2.0f * kPi * (0.0015f + voice.identity * 0.0025f) * elapsed
                + voice.identity * 6.0f) * params_.turbulence;
            voice.azimuthDeg = wrapDegrees(local
                ? localAngle + params_.azimuthDeg * 0.2f
                : params_.azimuthDeg + spreadIndex * params_.arcDeg
                    + voice.positionJitter * (4.0f + 16.0f * params_.terrain)
                    + slowDrift * 7.0f);
            voice.elevationDeg = std::clamp(local
                ? -12.0f + 18.0f * voice.identity
                : params_.elevationDeg
                    + voice.positionJitter * (2.0f + 5.0f * params_.terrain),
                -35.0f, 35.0f);
            voice.rangeNorm = local
                ? 0.02f + 0.10f * voice.identity
                : std::clamp(logRange + voice.rangeJitter * 0.08f, 0.0f, 1.0f);
            voice.basis = acnSn3dBasis7(directionFromAed(
                voice.azimuthDeg, voice.elevationDeg));
            const float distanceBase = std::max(
                0.12f, 1.0f - 0.67f * voice.rangeNorm);
            float distanceWeight = 1.0f;
            float detailWeight = 1.0f;
            for (uint32_t degree = 0u; degree <= params_.order; ++degree) {
                if (degree > 0u) {
                    distanceWeight *= distanceBase;
                    detailWeight *= detailBase;
                }
                const float degreeWeight = distanceWeight * detailWeight;
                const uint32_t first = degree * degree;
                const uint32_t end = std::min<uint32_t>(
                    (degree + 1u) * (degree + 1u), channels);
                for (uint32_t channel = first; channel < end; ++channel) {
                    voice.encodingWeights[channel] = voice.basis[channel]
                        * degreeWeight;
                }
            }
            score_.setEntityPosition(index,
                std::cos(voice.azimuthDeg * kPi / 180.0f) * voice.rangeNorm,
                std::sin(voice.azimuthDeg * kPi / 180.0f) * voice.rangeNorm,
                voice.elevationDeg / 35.0f);

            const auto directive = score_.directive(index);
            if (directive.onset || directive.cascadeArrival) {
                voice.eventEnvelope = std::max(voice.eventEnvelope,
                    0.35f + 0.65f * std::max(directive.drive, directive.propagation));
            }
            if (local) {
                voice.targetEnvelope = 0.24f + 0.26f * params_.activity;
            } else if (signal) {
                voice.targetEnvelope = 0.03f + directive.activity
                    * (0.25f + 0.75f * params_.activity);
            } else {
                voice.targetEnvelope = 0.16f + 0.34f * params_.occupancy
                    + directive.aftermath * 0.16f;
            }

            const float terrainLoss = params_.terrain * (local ? 0.1f : voice.rangeNorm);
            const float airLoss = params_.air * (local ? 0.04f : voice.rangeNorm);
            float groundLoss = 0.0f;
            switch (params_.ground) {
            case AmbiHorizonGround::Water: groundLoss = -0.10f; break;
            case AmbiHorizonGround::Hard: groundLoss = -0.03f; break;
            case AmbiHorizonGround::Grass: groundLoss = 0.16f; break;
            case AmbiHorizonGround::Forest: groundLoss = 0.28f; break;
            default: groundLoss = 0.08f; break;
            }
            const float cutoff = std::clamp(15500.0f * std::pow(0.045f,
                std::max(0.0f, airLoss + terrainLoss * 0.72f + groundLoss * voice.rangeNorm)),
                180.0f, 18000.0f);
            voice.filterAlpha = 1.0f - std::exp(
                -2.0f * kPi * cutoff / static_cast<float>(sampleRate_));
            const float d = voice.rangeNorm;
            const float geometric = 1.0f / (1.0f + 4.5f * d * d);
            const float carryGain = std::pow(
                2.0f, params_.carry * (0.35f + 1.4f * d));
            float groundCarry = 1.0f;
            if (params_.ground == AmbiHorizonGround::Water) {
                groundCarry += 0.28f * d;
            } else if (params_.ground == AmbiHorizonGround::Hard) {
                groundCarry += 0.12f * d;
            } else if (params_.ground == AmbiHorizonGround::Forest) {
                groundCarry -= 0.24f * d;
            }
            voice.propagationGain = geometric * carryGain * groundCarry;
            const float modalFrequency = std::min(4800.0f,
                voice.baseFrequency * (3.0f + voice.brightness * 13.0f));
            const float modalRadius = 0.996f + params_.memory * 0.0032f;
            voice.modalCoefficient = 2.0f * modalRadius * std::cos(
                2.0f * kPi * modalFrequency / static_cast<float>(sampleRate_));
            voice.modalRadiusSquared = modalRadius * modalRadius;
        }
    }

    float renderVoice(Voice& voice)
    {
        const float noise = randomUnit(voice.rng) * 2.0f - 1.0f;
        const float attack = voice.layer == AmbiHorizonLayer::HorizonSignal
            ? 0.0022f : 0.00035f;
        const float release = voice.layer == AmbiHorizonLayer::HorizonSignal
            ? 0.00018f : 0.00008f;
        voice.envelope += (voice.targetEnvelope - voice.envelope)
            * (voice.targetEnvelope > voice.envelope ? attack : release);
        voice.eventEnvelope *= 0.99965f - 0.00022f * params_.memory;

        const float slowRate = 0.012f + voice.identity * 0.045f;
        const float fastRate = 0.23f + voice.brightness * 1.8f;
        voice.slowPhase = wrapPhase(voice.slowPhase
            + slowRate / static_cast<float>(sampleRate_));
        voice.fastPhase = wrapPhase(voice.fastPhase
            + fastRate / static_cast<float>(sampleRate_));
        const float slow = 0.5f + 0.5f * std::sin(2.0f * kPi * voice.slowPhase);
        const float fast = 0.5f + 0.5f * std::sin(2.0f * kPi * voice.fastPhase);
        const float turbulenceGain = 1.0f - params_.turbulence
            * (0.22f * slow + 0.09f * fast);

        voice.low += (noise - voice.low) * (0.002f + voice.brightness * 0.008f);
        voice.low2 += (voice.low - voice.low2) * 0.0015f;
        const float high = noise - voice.low;
        float sample = 0.0f;
        switch (voice.kind) {
        case SourceKind::Flow:
            sample = voice.low * 0.72f + high * (0.08f + voice.brightness * 0.15f);
            break;
        case SourceKind::Roll:
            sample = voice.low2 * 1.4f + (voice.low - voice.low2) * 0.38f;
            break;
        case SourceKind::Motor: {
            const float rate = voice.baseFrequency
                * (1.0f + 0.018f * std::sin(2.0f * kPi * voice.slowPhase));
            voice.phase = wrapPhase(voice.phase + rate / static_cast<float>(sampleRate_));
            voice.phase2 = wrapPhase(voice.phase2 + rate * (1.98f + 0.04f * voice.identity)
                / static_cast<float>(sampleRate_));
            sample = std::sin(2.0f * kPi * voice.phase) * 0.52f
                + std::sin(2.0f * kPi * voice.phase2) * 0.20f
                + voice.low * 0.18f;
            break;
        }
        case SourceKind::Modal: {
            const float excitation = noise * voice.eventEnvelope * 0.08f;
            const float modal = excitation
                + voice.modalCoefficient * voice.modal1
                - voice.modalRadiusSquared * voice.modal2;
            voice.modal2 = voice.modal1;
            voice.modal1 = flushDenormal(modal);
            sample = modal * 0.38f + voice.low * 0.12f;
            break;
        }
        case SourceKind::Air:
            voice.band += ((high * 0.6f) - voice.band)
                * (0.01f + voice.brightness * 0.025f);
            sample = voice.band * 0.72f + voice.low * 0.20f;
            break;
        case SourceKind::Pulse: {
            const float pulseRate = 0.18f + voice.identity * 1.4f
                + params_.pace * 1.1f;
            const float previous = voice.phase;
            voice.phase = wrapPhase(voice.phase + pulseRate
                / static_cast<float>(sampleRate_));
            if (voice.phase < previous) voice.eventEnvelope = std::max(
                voice.eventEnvelope, 0.35f + 0.55f * voice.identity);
            sample = (noise * 0.28f
                + std::sin(2.0f * kPi * voice.phase * 7.0f) * 0.32f)
                * voice.eventEnvelope;
            break;
        }
        }
        const float eventLift = voice.layer == AmbiHorizonLayer::HorizonSignal
            ? 0.38f + voice.eventEnvelope : 0.75f;
        return softSat(sample * voice.envelope * eventLift * turbulenceGain * 1.8f);
    }

    float propagate(Voice& voice, float sample)
    {
        voice.band += (sample - voice.band) * voice.filterAlpha;
        return voice.band * voice.propagationGain;
    }

    double sampleRate_ = 48000.0;
    uint64_t frameCounter_ = 0u;
    AmbiHorizonEncoderParams params_ {};
    EnvironmentalScore score_ {};
    std::array<Voice, kAmbiHorizonMaxEntities> voices_ {};
};

} // namespace s3g
