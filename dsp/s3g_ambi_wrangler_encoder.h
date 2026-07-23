#pragma once

#include "s3g_ambi_field_listener.h"
#include "s3g_ambisonic_speaker_decoder.h"
#include "s3g_realtime.h"
#include "s3g_topology.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace s3g {

constexpr uint32_t kAmbiWranglerMaxVoices = 64;
constexpr uint32_t kAmbiWranglerMaxOrder = 7;
constexpr uint32_t kAmbiWranglerMaxChannels = 64;

enum class AmbiWranglerPickupSet : uint32_t {
    Tetra4 = 0u,
    Cube8 = 1u,
};

enum class AmbiWranglerListenMode : uint32_t {
    Trace = 0u,
    Ring = 1u,
    Cross = 2u,
    Balance = 3u,
};

enum class AmbiWranglerCircuitLaw : uint32_t {
    Legacy = 0u,
    Bounded = 1u,
};

enum class AmbiWranglerListenerResponse : uint32_t {
    Write = 0u,
    Settle = 1u,
};

struct AmbiWranglerParams {
    uint32_t order = 3;
    uint32_t voices = 16;
    float rateA = 0.28f;
    float rateB = 0.34f;
    float fmAtoB = 0.18f;
    float fmBtoA = 0.14f;
    float runglerA = 0.25f;
    float runglerB = 0.32f;
    float pwmA = 0.50f;
    float pwmB = 0.50f;
    float rampA = 0.50f;
    float rampB = 0.50f;
    uint32_t inputA = 0;
    uint32_t inputB = 0;
    float spread = 0.26f;
    float deviation = 0.12f;
    uint32_t rungSize = 4;
    uint32_t rateModeA = 1;
    uint32_t rateModeB = 1;
    uint32_t rungLoop = 0;
    float threshold = 0.50f;
    float color = 0.42f;
    float filter = 0.36f;
    float resonance = 0.20f;
    float filterRun = 0.28f;
    float filterSweep = 0.20f;
    float saturation = 0.36f;
    float field = 0.62f;
    uint32_t maskMode = 2;
    float maskDepth = 0.55f;
    float maskRateHz = 0.070f;
    bool voiceBreakpointsEnabled = false;
    std::array<float, kAmbiWranglerMaxVoices> bpRateA {};
    std::array<float, kAmbiWranglerMaxVoices> bpRateB {};
    std::array<float, kAmbiWranglerMaxVoices> bpFmAtoB {};
    std::array<float, kAmbiWranglerMaxVoices> bpFmBtoA {};
    std::array<float, kAmbiWranglerMaxVoices> bpRunglerA {};
    std::array<float, kAmbiWranglerMaxVoices> bpRunglerB {};
    std::array<float, kAmbiWranglerMaxVoices> bpFilter {};
    std::array<float, kAmbiWranglerMaxVoices> bpThreshold {};
    std::array<float, kAmbiWranglerMaxVoices> bpPwmA {};
    std::array<float, kAmbiWranglerMaxVoices> bpPwmB {};
    std::array<float, kAmbiWranglerMaxVoices> bpRampA {};
    std::array<float, kAmbiWranglerMaxVoices> bpRampB {};
    std::array<float, kAmbiWranglerMaxVoices> bpAmp {};
    uint32_t topologyShape = 11;
    uint32_t topologyMotion = 1;
    float topologyRateHz = 0.032f;
    float topologyAmount = 0.80f;
    float topologyDepth = 0.72f;
    float topologyScale = 1.14f;
    float topologyCollapse = 0.0f;
    float centerAzimuthDeg = 0.0f;
    float centerElevationDeg = 0.0f;
    float centerDistance = 1.0f;
    float spatialFollow = 0.90f;
    float outputGainDb = -6.0f;
    float snap = 0.0f;
    float snapDecay = 0.34f;
    uint32_t listeningEnabled = 0u;
    AmbiWranglerPickupSet pickupSet = AmbiWranglerPickupSet::Cube8;
    AmbiWranglerListenMode listenMode = AmbiWranglerListenMode::Trace;
    float fieldWrite = 0.0f;
    float registerMotion = 0.0f;
    float fieldReturn = 0.0f;
    float propagation = 0.18f;
    uint32_t returnBypass = 0u;
    AmbiWranglerCircuitLaw circuitLaw = AmbiWranglerCircuitLaw::Legacy;
    uint32_t engines = 4u;
    float change = 1.0f;
    AmbiWranglerListenerResponse listenerResponse = AmbiWranglerListenerResponse::Write;
    float settleAmount = 0.65f;
    float settleTarget = 0.30f;
    float settleRecoverySeconds = 3.0f;
};

struct AmbiWranglerPoint {
    float azimuthDeg = 0.0f;
    float elevationDeg = 0.0f;
    float distance = 1.0f;
};

struct AmbiWranglerFilter {
    float low = 0.0f;
    float band = 0.0f;

    float process(float input, float cutoffNorm, float resonance, float drive)
    {
        const float f = clamp(cutoffNorm, 0.0002f, 0.46f);
        const float q = 1.72f - clamp(resonance, 0.0f, 1.0f) * 1.42f;
        const float driven = std::tanh(clamp(input * (1.0f + drive * 5.0f), -8.0f, 8.0f));
        low = flushDenormal(low + f * band);
        const float high = driven - low - q * band;
        band = flushDenormal(band + f * high);
        return std::tanh(clamp(low + band * (0.25f + resonance * 0.55f), -6.0f, 6.0f));
    }
};

struct AmbiWranglerVoice {
    float phaseA = 0.0f;
    float phaseB = 0.0f;
    float rateA = 80.0f;
    float rateB = 91.0f;
    float rungValue = 0.0f;
    float rungSmooth = 0.0f;
    float lastClock = 0.0f;
    float energy = 0.0f;
    float maskGain = 1.0f;
    float snapEnv = 0.0f;
    float snapPolarity = 1.0f;
    float fieldWriteAccumulator = 0.0f;
    float fieldWritePulse = 0.0f;
    float lastReturn = 0.0f;
    float changeAccumulator = 0.0f;
    float transitionDensity = 0.0f;
    float listenerCapture = 0.0f;
    float listenerEvolutionAccumulator = 0.0f;
    uint32_t reg = 1u;
    uint32_t seed = 1u;
    uint32_t listenHead = 0u;
    uint32_t lastReadEar = 0u;
    uint32_t comparatorBit = 0u;
    uint32_t auditoryBit = 0u;
    uint32_t writtenBit = 0u;
    uint32_t registerHeld = 0u;
    uint64_t clockCount = 0u;
    uint64_t registerAdvanceCount = 0u;
    uint64_t heldClockCount = 0u;
    AmbiWranglerFilter filterA {};
    AmbiWranglerFilter filterB {};
};

struct AmbiWranglerNode {
    float energy = 0.0f;
    float maskGain = 1.0f;
    uint32_t seed = 1u;
};

class AmbiWranglerEncoder {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        fieldListener_.prepare(sampleRate_);
        fieldListener_.setMemorySeconds(0.34f);
        const size_t delaySize = static_cast<size_t>(
            std::ceil(sampleRate_ * kMaximumPropagationSeconds)) + 4u;
        for (auto& delay : listenerDelay_) delay.assign(delaySize, 0.0f);
        reset();
        setParams(params_);
    }

    void reset()
    {
        topologyPhase_ = 0.0f;
        maskPhase_ = 0.0f;
        listenerEnableGain_ = 0.0f;
        returnEnableGain_ = 0.0f;
        listenerDelayWrite_ = 0u;
        listenerDc_.fill(0.0f);
        listenerConditioned_.fill(0.0f);
        listenerDecision_.fill(0.0f);
        listenerPreviousSignal_.fill(0.0f);
        listenerRoughness_.fill(0.0f);
        listenerTension_.fill(0.0f);
        listenerFrame_.fill(0.0f);
        fieldListener_.reset();
        configureListener();
        for (auto& delay : listenerDelay_) std::fill(delay.begin(), delay.end(), 0.0f);
        smoothedOutputGain_ = dbToGain(params_.outputGainDb);
        for (uint32_t i = 0u; i < kAmbiWranglerMaxVoices; ++i) {
            voices_[i] = {};
            voices_[i].seed = 0x9e3779b9u + i * 0x85ebca6bu;
            voices_[i].reg = (hash(voices_[i].seed) & 0xffu) | 1u;
            nodes_[i] = {};
            nodes_[i].seed = voices_[i].seed;
            initializeVoice(i);
            points_[i] = basePoint(i, std::max<uint32_t>(1u, params_.voices));
            targetPoints_[i] = points_[i];
        }
    }

    void setParams(AmbiWranglerParams params)
    {
        params.order = std::clamp<uint32_t>(params.order, 1u, kAmbiWranglerMaxOrder);
        params.voices = std::clamp<uint32_t>(params.voices, 1u, kAmbiWranglerMaxVoices);
        params.rateA = clamp(params.rateA, 0.0f, 1.0f);
        params.rateB = clamp(params.rateB, 0.0f, 1.0f);
        params.fmAtoB = clamp(params.fmAtoB, 0.0f, 1.0f);
        params.fmBtoA = clamp(params.fmBtoA, 0.0f, 1.0f);
        params.runglerA = clamp(params.runglerA, 0.0f, 1.0f);
        params.runglerB = clamp(params.runglerB, 0.0f, 1.0f);
        params.pwmA = clamp(params.pwmA, 0.0f, 1.0f);
        params.pwmB = clamp(params.pwmB, 0.0f, 1.0f);
        params.rampA = clamp(params.rampA, 0.0f, 1.0f);
        params.rampB = clamp(params.rampB, 0.0f, 1.0f);
        params.inputA = std::clamp<uint32_t>(params.inputA, 0u, 1u);
        params.inputB = std::clamp<uint32_t>(params.inputB, 0u, 1u);
        params.spread = clamp(params.spread, 0.0f, 1.0f);
        params.deviation = clamp(params.deviation, 0.0f, 1.0f);
        params.rungSize = std::clamp<uint32_t>(params.rungSize, 2u, 8u);
        params.rateModeA = std::clamp<uint32_t>(params.rateModeA, 0u, 2u);
        params.rateModeB = std::clamp<uint32_t>(params.rateModeB, 0u, 2u);
        params.rungLoop = std::clamp<uint32_t>(params.rungLoop, 0u, 2u);
        params.threshold = clamp(params.threshold, 0.0f, 1.0f);
        params.color = clamp(params.color, 0.0f, 1.0f);
        params.filter = clamp(params.filter, 0.0f, 1.0f);
        params.resonance = clamp(params.resonance, 0.0f, 1.0f);
        params.filterRun = clamp(params.filterRun, 0.0f, 1.0f);
        params.filterSweep = clamp(params.filterSweep, 0.0f, 1.0f);
        params.saturation = clamp(params.saturation, 0.0f, 1.0f);
        params.snap = clamp(params.snap, 0.0f, 1.0f);
        params.snapDecay = clamp(params.snapDecay, 0.0f, 1.0f);
        params.field = clamp(params.field, 0.0f, 1.0f);
        params.maskMode = std::clamp<uint32_t>(params.maskMode, 0u, 5u);
        params.maskDepth = clamp(params.maskDepth, 0.0f, 1.0f);
        params.maskRateHz = clamp(params.maskRateHz, 0.0f, 4.0f);
        for (uint32_t i = 0u; i < kAmbiWranglerMaxVoices; ++i) {
            params.bpRateA[i] = clamp(params.bpRateA[i], 0.0f, 1.0f);
            params.bpRateB[i] = clamp(params.bpRateB[i], 0.0f, 1.0f);
            params.bpFmAtoB[i] = clamp(params.bpFmAtoB[i], 0.0f, 1.0f);
            params.bpFmBtoA[i] = clamp(params.bpFmBtoA[i], 0.0f, 1.0f);
            params.bpRunglerA[i] = clamp(params.bpRunglerA[i], 0.0f, 1.0f);
            params.bpRunglerB[i] = clamp(params.bpRunglerB[i], 0.0f, 1.0f);
            params.bpFilter[i] = clamp(params.bpFilter[i], 0.0f, 1.0f);
            params.bpThreshold[i] = clamp(params.bpThreshold[i], 0.0f, 1.0f);
            params.bpPwmA[i] = clamp(params.bpPwmA[i], 0.0f, 1.0f);
            params.bpPwmB[i] = clamp(params.bpPwmB[i], 0.0f, 1.0f);
            params.bpRampA[i] = clamp(params.bpRampA[i], 0.0f, 1.0f);
            params.bpRampB[i] = clamp(params.bpRampB[i], 0.0f, 1.0f);
            params.bpAmp[i] = clamp(params.bpAmp[i], 0.0f, 1.0f);
        }
        params.topologyShape = std::clamp<uint32_t>(params.topologyShape, 0u, kTopologyShapeCount - 1u);
        params.topologyMotion = std::clamp<uint32_t>(params.topologyMotion, 0u, kTopologyMotionModeCount - 1u);
        params.topologyRateHz = clamp(params.topologyRateHz, 0.001f, 2.0f);
        params.topologyAmount = clamp(params.topologyAmount, 0.0f, 1.0f);
        params.topologyDepth = clamp(params.topologyDepth, 0.0f, 1.0f);
        params.topologyScale = clamp(params.topologyScale, 0.20f, 2.50f);
        params.topologyCollapse = clamp(params.topologyCollapse, 0.0f, 1.0f);
        params.centerAzimuthDeg = wrapSignedDeg(params.centerAzimuthDeg);
        params.centerElevationDeg = clamp(params.centerElevationDeg, -90.0f, 90.0f);
        params.centerDistance = clamp(params.centerDistance, 0.15f, 2.0f);
        params.spatialFollow = clamp(params.spatialFollow, 0.0f, 1.0f);
        params.outputGainDb = clamp(params.outputGainDb, -60.0f, 12.0f);
        params.listeningEnabled = std::min<uint32_t>(params.listeningEnabled, 1u);
        params.pickupSet = static_cast<AmbiWranglerPickupSet>(
            std::min<uint32_t>(static_cast<uint32_t>(params.pickupSet), 1u));
        params.listenMode = static_cast<AmbiWranglerListenMode>(
            std::min<uint32_t>(static_cast<uint32_t>(params.listenMode), 3u));
        params.fieldWrite = clamp(params.fieldWrite, 0.0f, 1.0f);
        params.registerMotion = clamp(params.registerMotion, 0.0f, 1.0f);
        params.fieldReturn = clamp(params.fieldReturn, 0.0f, 1.0f);
        params.propagation = clamp(params.propagation, 0.0f, 1.0f);
        params.returnBypass = std::min<uint32_t>(params.returnBypass, 1u);
        params.circuitLaw = static_cast<AmbiWranglerCircuitLaw>(
            std::min<uint32_t>(static_cast<uint32_t>(params.circuitLaw), 1u));
        params.engines = std::clamp<uint32_t>(params.engines, 1u, kAmbiWranglerMaxVoices);
        params.change = clamp(params.change, 0.0f, 1.0f);
        params.listenerResponse = static_cast<AmbiWranglerListenerResponse>(
            std::min<uint32_t>(static_cast<uint32_t>(params.listenerResponse), 1u));
        params.settleAmount = clamp(params.settleAmount, 0.0f, 1.0f);
        params.settleTarget = clamp(params.settleTarget, 0.0f, 0.95f);
        params.settleRecoverySeconds = clamp(params.settleRecoverySeconds, 0.25f, 12.0f);
        const bool voiceCountChanged = params.voices != params_.voices;
        const bool engineCountChanged =
            effectiveEngineCount(params) != effectiveEngineCount(params_);
        const bool pickupSetChanged = params.pickupSet != params_.pickupSet;
        params_ = params;
        if (pickupSetChanged) configureListener();
        if (voiceCountChanged || engineCountChanged) {
            for (uint32_t i = 0u; i < kAmbiWranglerMaxVoices; ++i) initializeVoice(i);
        }
    }

    AmbiWranglerParams params() const { return params_; }
    uint32_t engineCount() const { return effectiveEngineCount(params_); }
    uint32_t nodeEngine(uint32_t node) const
    {
        return engineForNode(node, params_.voices, engineCount());
    }
    float voiceEnergy(uint32_t voice) const { return nodes_[std::min<uint32_t>(voice, kAmbiWranglerMaxVoices - 1u)].energy; }
    AmbiWranglerPoint voicePoint(uint32_t voice) const { return points_[std::min<uint32_t>(voice, kAmbiWranglerMaxVoices - 1u)]; }
    float voiceMaskLevel(uint32_t voice) const { return nodes_[std::min<uint32_t>(voice, kAmbiWranglerMaxVoices - 1u)].maskGain; }
    uint32_t voiceRegister(uint32_t voice) const { return voices_[nodeEngine(voice)].reg; }
    uint32_t voiceReadEar(uint32_t voice) const { return voices_[nodeEngine(voice)].lastReadEar; }
    uint32_t voiceComparatorBit(uint32_t voice) const { return voices_[nodeEngine(voice)].comparatorBit; }
    uint32_t voiceAuditoryBit(uint32_t voice) const { return voices_[nodeEngine(voice)].auditoryBit; }
    uint32_t voiceWrittenBit(uint32_t voice) const { return voices_[nodeEngine(voice)].writtenBit; }
    float voiceFieldWritePulse(uint32_t voice) const { return voices_[nodeEngine(voice)].fieldWritePulse; }
    float voiceFieldReturn(uint32_t voice) const { return voices_[nodeEngine(voice)].lastReturn; }
    float voiceSettle(uint32_t voice) const { return voiceListenerCapture(voice); }
    float voiceListenerCapture(uint32_t voice) const
    {
        return voices_[nodeEngine(voice)].listenerCapture;
    }
    float voiceListenerEvolutionRate(uint32_t voice) const
    {
        return listenerEvolutionRate(voiceListenerCapture(voice));
    }
    float voiceListenerCouplingRate(uint32_t voice) const
    {
        return listenerCouplingRate(voiceListenerCapture(voice));
    }
    uint32_t voiceRegisterHeld(uint32_t voice) const
    {
        return voices_[nodeEngine(voice)].registerHeld;
    }
    uint64_t voiceClockCount(uint32_t voice) const
    {
        return voices_[nodeEngine(voice)].clockCount;
    }
    uint64_t voiceRegisterAdvanceCount(uint32_t voice) const
    {
        return voices_[nodeEngine(voice)].registerAdvanceCount;
    }
    uint64_t voiceHeldClockCount(uint32_t voice) const
    {
        return voices_[nodeEngine(voice)].heldClockCount;
    }
    float voicePhaseA(uint32_t voice) const
    {
        return voices_[nodeEngine(voice)].phaseA;
    }
    float voicePhaseB(uint32_t voice) const
    {
        return voices_[nodeEngine(voice)].phaseB;
    }
    uint32_t listenerPickupCount() const { return fieldListener_.count(); }
    float listenerEnvelope(uint32_t pickup) const { return fieldListener_.envelope(pickup); }
    float listenerSignal(uint32_t pickup) const { return fieldListener_.signal(pickup); }
    float listenerActivity() const { return fieldListener_.activity(); }
    Vec3 listenerDirection(uint32_t pickup) const { return fieldListener_.direction(pickup); }
    float listenerEnableGain() const { return listenerEnableGain_; }
    float returnEnableGain() const { return returnEnableGain_; }
    float listenerTension(uint32_t pickup) const
    {
        return listenerTension_[std::min<uint32_t>(
            pickup, kAmbiFieldListenerMaxLobes - 1u)];
    }
    float listenerTension() const
    {
        const uint32_t count = fieldListener_.count();
        if (count == 0u) return 0.0f;
        float sum = 0.0f;
        for (uint32_t ear = 0u; ear < count; ++ear) sum += listenerTension_[ear];
        return sum / static_cast<float>(count);
    }
    float listenerCapture() const
    {
        const uint32_t engines = engineCount();
        if (engines == 0u) return 0.0f;
        float sum = 0.0f;
        for (uint32_t engine = 0u; engine < engines; ++engine) {
            sum += voices_[engine].listenerCapture;
        }
        return sum / static_cast<float>(engines);
    }
    float listenerTopologyRate() const
    {
        if (params_.listenerResponse
                != AmbiWranglerListenerResponse::Settle
            || params_.listeningEnabled == 0u
            || params_.settleAmount <= 0.0f) {
            return 1.0f;
        }
        return listenerTopologyClockRate(listenerCapture());
    }

    void process(float* const* outputs, uint32_t outputChannels, uint32_t frames)
    {
        if (!outputs || frames == 0u) return;
        outputChannels = std::min<uint32_t>(outputChannels, kAmbiWranglerMaxChannels);
        for (uint32_t ch = 0u; ch < outputChannels; ++ch) {
            if (outputs[ch]) std::fill(outputs[ch], outputs[ch] + frames, 0.0f);
        }

        const uint32_t voices = params_.voices;
        const uint32_t engines = engineCount();
        const bool bounded = params_.circuitLaw == AmbiWranglerCircuitLaw::Bounded;
        const uint32_t ambiChannels = std::min<uint32_t>(ambiChannelsForOrder(params_.order), outputChannels);
        const float targetGain = dbToGain(params_.outputGainDb)
            / std::sqrt(static_cast<float>(std::max<uint32_t>(
                1u, bounded ? engines : voices)));
        const float maskAttack = 1.0f - std::exp(-1.0f / (static_cast<float>(sampleRate_) * 0.004f));
        const float maskRelease = 1.0f - std::exp(-1.0f / (static_cast<float>(sampleRate_) * 0.018f));
        std::array<uint32_t, kAmbiWranglerMaxVoices> engineNodeCounts {};
        for (uint32_t node = 0u; node < voices; ++node) {
            ++engineNodeCounts[engineForNode(node, voices, engines)];
        }
        constexpr uint32_t kControlFrames = 16u;

        for (uint32_t chunkStart = 0u; chunkStart < frames; chunkStart += kControlFrames) {
            const uint32_t chunkFrames = std::min<uint32_t>(kControlFrames, frames - chunkStart);
            const float chunkDt = static_cast<float>(chunkFrames) / static_cast<float>(sampleRate_);
            updateTopology(chunkDt);
            updateMask(chunkDt);

            std::array<std::array<float, kAmbiWranglerMaxChannels>, kAmbiWranglerMaxVoices> basis {};
            std::array<float, kAmbiWranglerMaxVoices> distGain {};
            for (uint32_t v = 0u; v < voices; ++v) {
                basis[v] = acnSn3dBasis7(directionFromAed(points_[v].azimuthDeg, points_[v].elevationDeg));
                distGain[v] = 1.0f / std::max(0.50f, points_[v].distance);
            }

            for (uint32_t frame = chunkStart; frame < chunkStart + chunkFrames; ++frame) {
                const float listenerSmoothing = 1.0f - std::exp(
                    -1.0f / static_cast<float>(sampleRate_ * 0.018));
                const float returnSmoothing = 1.0f - std::exp(
                    -1.0f / static_cast<float>(sampleRate_ * 0.026));
                listenerEnableGain_ += (
                    (params_.listeningEnabled ? 1.0f : 0.0f) - listenerEnableGain_)
                    * listenerSmoothing;
                const bool writeResponse =
                    params_.listenerResponse == AmbiWranglerListenerResponse::Write;
                returnEnableGain_ += (
                    (params_.listeningEnabled && writeResponse
                            && !params_.returnBypass ? 1.0f : 0.0f)
                    - returnEnableGain_) * returnSmoothing;
                smoothedOutputGain_ += (targetGain - smoothedOutputGain_) * 0.0015f;

                if (!bounded) {
                    // Keep the original per-voice ordering in Legacy law. In this
                    // case engineForNode() is the identity mapping.
                    for (uint32_t v = 0u; v < voices; ++v) {
                        const float targetMask = voiceMask(v);
                        auto& node = nodes_[v];
                        node.maskGain += (targetMask - node.maskGain)
                            * (targetMask > node.maskGain ? maskAttack : maskRelease);
                        const float sample = processVoice(v, v, voices)
                            * node.maskGain * smoothedOutputGain_ * distGain[v];
                        node.energy += (sample * sample - node.energy) * 0.0008f;
                        if (std::fabs(sample) < 0.0000001f) continue;
                        for (uint32_t ch = 0u; ch < ambiChannels; ++ch) {
                            if (outputs[ch]) outputs[ch][frame] = flushDenormal(
                                outputs[ch][frame] + sample * basis[v][ch]);
                        }
                    }
                } else {
                    std::array<float, kAmbiWranglerMaxVoices> engineSamples {};
                    for (uint32_t engine = 0u; engine < engines; ++engine) {
                        const uint32_t anchor =
                            engineAnchorNode(engine, voices, engines);
                        engineSamples[engine] =
                            processVoice(engine, anchor, engines);
                    }
                    for (uint32_t nodeIndex = 0u; nodeIndex < voices; ++nodeIndex) {
                        const float targetMask = voiceMask(nodeIndex);
                        auto& node = nodes_[nodeIndex];
                        node.maskGain += (targetMask - node.maskGain)
                            * (targetMask > node.maskGain ? maskAttack : maskRelease);
                        const uint32_t engine =
                            engineForNode(nodeIndex, voices, engines);
                        const float copies = static_cast<float>(
                            std::max<uint32_t>(1u, engineNodeCounts[engine]));
                        const float sample = engineSamples[engine]
                            * node.maskGain * smoothedOutputGain_
                            * distGain[nodeIndex] / copies;
                        node.energy += (sample * sample - node.energy) * 0.0008f;
                        if (std::fabs(sample) < 0.0000001f) continue;
                        for (uint32_t ch = 0u; ch < ambiChannels; ++ch) {
                            if (outputs[ch]) outputs[ch][frame] = flushDenormal(
                                outputs[ch][frame] + sample * basis[nodeIndex][ch]);
                        }
                    }
                }
                for (uint32_t ch = 0u; ch < ambiChannels; ++ch) {
                    listenerFrame_[ch] = outputs[ch] ? outputs[ch][frame] : 0.0f;
                }
                for (uint32_t ch = ambiChannels; ch < kAmbiWranglerMaxChannels; ++ch) {
                    listenerFrame_[ch] = 0.0f;
                }
                fieldListener_.processFrame(listenerFrame_.data(), ambiChannels);
                writeListenerDelay();
            }
        }

        for (uint32_t ch = 0u; ch < ambiChannels; ++ch) {
            if (!outputs[ch]) continue;
            for (uint32_t frame = 0u; frame < frames; ++frame) {
                outputs[ch][frame] = std::tanh(clamp(outputs[ch][frame], -6.0f, 6.0f));
            }
        }
    }

private:
    static constexpr float kMaximumPropagationSeconds = 0.180f;

    static float listenerEvolutionRate(float capture)
    {
        return clamp(std::exp(-4.2f * clamp(capture, 0.0f, 1.0f)),
            0.03f, 1.0f);
    }

    static float listenerCouplingRate(float capture)
    {
        return clamp(std::exp(-1.5f * clamp(capture, 0.0f, 1.0f)),
            0.22f, 1.0f);
    }

    static float listenerTopologyClockRate(float capture)
    {
        return clamp(std::exp(-2.25f * clamp(capture, 0.0f, 1.0f)),
            0.12f, 1.0f);
    }

    static uint32_t effectiveEngineCount(const AmbiWranglerParams& params)
    {
        const uint32_t nodes =
            std::clamp<uint32_t>(params.voices, 1u, kAmbiWranglerMaxVoices);
        if (params.circuitLaw == AmbiWranglerCircuitLaw::Legacy) return nodes;
        return std::clamp<uint32_t>(params.engines, 1u, nodes);
    }

    static uint32_t engineForNode(
        uint32_t node, uint32_t nodes, uint32_t engines)
    {
        nodes = std::clamp<uint32_t>(nodes, 1u, kAmbiWranglerMaxVoices);
        engines = std::clamp<uint32_t>(engines, 1u, nodes);
        node = std::min<uint32_t>(node, nodes - 1u);
        return std::min<uint32_t>(
            engines - 1u,
            static_cast<uint32_t>(
                static_cast<uint64_t>(node) * engines / nodes));
    }

    static uint32_t engineAnchorNode(
        uint32_t engine, uint32_t nodes, uint32_t engines)
    {
        nodes = std::clamp<uint32_t>(nodes, 1u, kAmbiWranglerMaxVoices);
        engines = std::clamp<uint32_t>(engines, 1u, nodes);
        engine = std::min<uint32_t>(engine, engines - 1u);
        return std::min<uint32_t>(
            nodes - 1u,
            static_cast<uint32_t>(
                (static_cast<uint64_t>(engine) * 2u + 1u) * nodes
                / (static_cast<uint64_t>(engines) * 2u)));
    }

    static uint32_t hash(uint32_t x)
    {
        x ^= x >> 16u;
        x *= 0x7feb352du;
        x ^= x >> 15u;
        x *= 0x846ca68bu;
        x ^= x >> 16u;
        return x;
    }

    static float hash01(uint32_t x)
    {
        return static_cast<float>(hash(x) & 0xffffu) / 65535.0f;
    }

    static float hashSigned(uint32_t x)
    {
        return hash01(x) * 2.0f - 1.0f;
    }

    static float midiToHz(float note)
    {
        return 440.0f * std::pow(2.0f, (note - 69.0f) / 12.0f);
    }

    static float targetNormToHz(float value)
    {
        value = clamp(value, 0.0f, 1.0f);
        return midiToHz(value * 172.0f - 40.0f);
    }

    static float rateModeScale(uint32_t mode)
    {
        switch (mode) {
        case 0u: return 0.01f;
        case 2u: return 2.0f;
        default: return 1.0f;
        }
    }

    static float clampOscHz(float hz, float sampleRate)
    {
        const float limit = sampleRate * 0.42f;
        hz = clamp(hz, -limit, limit);
        if (std::fabs(hz) < 0.015f) return hz < 0.0f ? -0.015f : 0.015f;
        return hz;
    }

    static float clampPositiveOscHz(float hz, float sampleRate)
    {
        return clamp(hz, 0.015f, sampleRate * 0.42f);
    }

    static float wrapSignedDeg(float value)
    {
        while (value > 180.0f) value -= 360.0f;
        while (value <= -180.0f) value += 360.0f;
        return value;
    }

    static float phaseTri(float phase)
    {
        return 1.0f - 4.0f * std::fabs(phase - 0.5f);
    }

    static AmbiWranglerPoint basePoint(uint32_t voice, uint32_t voices)
    {
        const float u = (static_cast<float>(voice) + 0.5f) / static_cast<float>(std::max<uint32_t>(1u, voices));
        const float z = 1.0f - 2.0f * u;
        const float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
        const float a = static_cast<float>(voice) * 2.39996323f;
        const Vec3 p { std::cos(a) * r, std::sin(a) * r, z };
        return {
            std::atan2(p.y, p.x) * 180.0f / kPi,
            std::asin(clamp(p.z, -1.0f, 1.0f)) * 180.0f / kPi,
            1.0f
        };
    }

    void initializeVoice(uint32_t index)
    {
        auto& voice = voices_[index];
        voice.rateA = 1.0f + hashSigned(voice.seed + 19u) * 0.01f;
        voice.rateB = 1.0f + hashSigned(voice.seed + 31u) * 0.01f;
        voice.phaseA = hash01(voice.seed + 41u);
        voice.phaseB = hash01(voice.seed + 53u);
    }

    void configureListener()
    {
        listenerDecision_.fill(0.0f);
        listenerPreviousSignal_.fill(0.0f);
        listenerRoughness_.fill(0.0f);
        listenerTension_.fill(0.0f);
        constexpr float k = 0.57735026919f;
        static constexpr std::array<Vec3, 4> tetraDirections {{
            { k, k, k }, { -k, -k, k }, { -k, k, -k }, { k, -k, -k },
        }};
        if (params_.pickupSet == AmbiWranglerPickupSet::Tetra4) {
            fieldListener_.setDirections(tetraDirections.data(), 4u);
        } else {
            const auto& cubeDirections = ambiFieldListenerCubeDirections();
            fieldListener_.setDirections(cubeDirections.data(), 8u);
        }
    }

    uint32_t nearestListener(Vec3 direction) const
    {
        direction = normalize(direction);
        uint32_t selected = 0u;
        float best = -2.0f;
        for (uint32_t ear = 0u; ear < fieldListener_.count(); ++ear) {
            const Vec3 candidate = fieldListener_.direction(ear);
            const float dot = direction.x * candidate.x
                + direction.y * candidate.y + direction.z * candidate.z;
            if (dot > best) {
                best = dot;
                selected = ear;
            }
        }
        return selected;
    }

    uint32_t oppositeListener(uint32_t ear) const
    {
        if (fieldListener_.count() == 0u) return 0u;
        ear = std::min<uint32_t>(ear, fieldListener_.count() - 1u);
        const Vec3 direction = fieldListener_.direction(ear);
        uint32_t selected = 0u;
        float best = 2.0f;
        for (uint32_t candidate = 0u; candidate < fieldListener_.count(); ++candidate) {
            const Vec3 other = fieldListener_.direction(candidate);
            const float dot = direction.x * other.x
                + direction.y * other.y + direction.z * other.z;
            if (dot < best) {
                best = dot;
                selected = candidate;
            }
        }
        return selected;
    }

    uint32_t listenerEarForClock(uint32_t index, uint32_t spatialNode)
    {
        auto& voice = voices_[index];
        const uint32_t count = std::max<uint32_t>(1u, fieldListener_.count());
        spatialNode = std::min<uint32_t>(
            spatialNode, kAmbiWranglerMaxVoices - 1u);
        if (params_.listenerResponse == AmbiWranglerListenerResponse::Settle) {
            // Homeostatic listening remains attached to the circuit's local
            // field region. Ring mode must not teleport its sensor each clock.
            return nearestListener(directionFromAed(
                points_[spatialNode].azimuthDeg,
                points_[spatialNode].elevationDeg));
        }
        switch (params_.listenMode) {
        case AmbiWranglerListenMode::Trace:
        case AmbiWranglerListenMode::Cross:
            return nearestListener(directionFromAed(
                points_[spatialNode].azimuthDeg,
                points_[spatialNode].elevationDeg));
        case AmbiWranglerListenMode::Ring:
        case AmbiWranglerListenMode::Balance: {
            const uint32_t ear = (voice.listenHead + index) % count;
            voice.listenHead = (voice.listenHead + 1u) % count;
            return ear;
        }
        }
        return 0u;
    }

    struct AuditoryDecision {
        uint32_t bit = 0u;
        float confidence = 0.0f;
    };

    AuditoryDecision auditoryDecision(uint32_t ear, uint32_t previousBit) const
    {
        const uint32_t count = std::max<uint32_t>(1u, fieldListener_.count());
        ear %= count;
        float score = listenerDecision_[ear];
        float scale = fieldListener_.envelope(ear);
        if (params_.listenMode == AmbiWranglerListenMode::Cross) {
            const uint32_t opposite = oppositeListener(ear);
            score -= listenerDecision_[opposite];
            scale += fieldListener_.envelope(opposite);
        } else if (params_.listenMode == AmbiWranglerListenMode::Balance) {
            scale = fieldListener_.meanEnvelope();
            score = scale - fieldListener_.envelope(ear);
        }
        const float hysteresis = scale * 0.08f + 0.00001f;
        AuditoryDecision decision {};
        decision.bit = previousBit;
        if (score > hysteresis) decision.bit = 1u;
        else if (score < -hysteresis) decision.bit = 0u;
        decision.confidence = clamp(
            (std::fabs(score) - hysteresis)
                / (scale * 0.60f + hysteresis),
            0.0f, 1.0f);
        return decision;
    }

    float delayedListenerSignal(uint32_t ear) const
    {
        if (listenerDelay_[0].empty() || fieldListener_.count() == 0u) return 0.0f;
        ear %= fieldListener_.count();
        const float seconds = 1.0f / static_cast<float>(sampleRate_)
            + params_.propagation * params_.propagation * kMaximumPropagationSeconds;
        const size_t delaySamples = std::clamp<size_t>(
            static_cast<size_t>(std::lround(seconds * sampleRate_)),
            1u, listenerDelay_[ear].size() - 1u);
        const size_t read = (
            listenerDelayWrite_ + listenerDelay_[ear].size() - delaySamples)
            % listenerDelay_[ear].size();
        float result = listenerDelay_[ear][read];
        if (params_.listenMode == AmbiWranglerListenMode::Cross) {
            const uint32_t opposite = oppositeListener(ear);
            result -= listenerDelay_[opposite][read];
        }
        return result;
    }

    void writeListenerDelay()
    {
        if (listenerDelay_[0].empty()) return;
        const float dcCoefficient = 1.0f - std::exp(
            -1.0f / static_cast<float>(sampleRate_ * 0.045));
        const float decisionCoefficient = 1.0f - std::exp(
            -1.0f / static_cast<float>(sampleRate_ * 0.004));
        const float roughnessCoefficient = 1.0f - std::exp(
            -1.0f / static_cast<float>(sampleRate_ * 0.055));
        for (uint32_t ear = 0u; ear < fieldListener_.count(); ++ear) {
            const float signal = fieldListener_.signal(ear);
            const float delta = std::fabs(
                signal - listenerPreviousSignal_[ear]);
            listenerPreviousSignal_[ear] = signal;
            const float roughnessInstant = clamp(
                delta / (fieldListener_.envelope(ear) * 0.025f + 0.00001f),
                0.0f, 1.0f);
            listenerRoughness_[ear] += (
                roughnessInstant - listenerRoughness_[ear])
                * roughnessCoefficient;
            listenerDc_[ear] += (signal - listenerDc_[ear]) * dcCoefficient;
            listenerDecision_[ear] += (
                signal - listenerDecision_[ear]) * decisionCoefficient;
            listenerDecision_[ear] = flushDenormal(listenerDecision_[ear]);
            listenerConditioned_[ear] = std::tanh(
                clamp((signal - listenerDc_[ear]) * 7.0f, -5.0f, 5.0f));
            listenerDelay_[ear][listenerDelayWrite_] = listenerConditioned_[ear];
        }
        for (uint32_t ear = fieldListener_.count(); ear < kAmbiFieldListenerMaxLobes; ++ear) {
            listenerDelay_[ear][listenerDelayWrite_] = 0.0f;
            listenerRoughness_[ear] = 0.0f;
            listenerTension_[ear] = 0.0f;
        }

        std::array<float, kAmbiFieldListenerMaxLobes> transitionSum {};
        std::array<uint32_t, kAmbiFieldListenerMaxLobes> transitionCount {};
        const uint32_t engines = engineCount();
        for (uint32_t engine = 0u; engine < engines; ++engine) {
            const uint32_t anchor =
                engineAnchorNode(engine, params_.voices, engines);
            const uint32_t ear = nearestListener(directionFromAed(
                points_[anchor].azimuthDeg, points_[anchor].elevationDeg));
            transitionSum[ear] += voices_[engine].transitionDensity;
            ++transitionCount[ear];
        }
        for (uint32_t ear = 0u; ear < fieldListener_.count(); ++ear) {
            const float transitions = transitionCount[ear] > 0u
                ? transitionSum[ear]
                    / static_cast<float>(transitionCount[ear])
                : 0.0f;
            listenerTension_[ear] = clamp(
                listenerRoughness_[ear] * 0.72f + transitions * 0.28f,
                0.0f, 1.0f);
        }
        listenerDelayWrite_ = (listenerDelayWrite_ + 1u) % listenerDelay_[0].size();
    }

    void updateTopology(float dt)
    {
        const bool settleResponse =
            params_.listenerResponse == AmbiWranglerListenerResponse::Settle;
        const bool writeResponse =
            params_.listenerResponse == AmbiWranglerListenerResponse::Write;
        const float fieldCapture =
            settleResponse && params_.listeningEnabled != 0u
                && params_.settleAmount > 0.0f
            ? listenerCapture() : 0.0f;
        topologyPhase_ += dt * params_.topologyRateHz
            * listenerTopologyClockRate(fieldCapture);
        if (topologyPhase_ > 100000.0f) topologyPhase_ -= 100000.0f;
        const uint32_t voices = params_.voices;
        TopologyState state {};
        state.shape = params_.topologyShape;
        state.motionMode = params_.topologyMotion;
        state.motionRateHz = params_.topologyRateHz;
        state.motionPhase = topologyPhase_;
        state.amount = params_.topologyAmount;
        state.motionDepth = params_.topologyDepth;
        state.collapse = params_.topologyCollapse;
        state.twist = 0.0f;
        state.jitter = std::max(params_.runglerA, params_.runglerB)
            * 0.24f;
        const float follow = 1.0f - std::pow(params_.spatialFollow, 4.0f);
        for (uint32_t i = 0u; i < voices; ++i) {
            const uint32_t engine = engineForNode(
                i, voices, effectiveEngineCount(params_));
            const auto tp = topologyPointForLane(i, voices, state);
            Vec3 spatialDirection = normalize({
                static_cast<float>(tp.x),
                static_cast<float>(tp.y),
                static_cast<float>(tp.z),
            });
            const float registerInfluence = params_.registerMotion
                * params_.registerMotion
                * (writeResponse ? listenerEnableGain_ : 0.0f)
                * fieldListener_.activity() * 0.35f;
            if (registerInfluence > 0.000001f && fieldListener_.count() > 0u) {
                const Vec3 registerDirection = fieldListener_.direction(
                    voices_[engine].reg & (fieldListener_.count() - 1u));
                spatialDirection = normalize({
                    lerp(spatialDirection.x, registerDirection.x, registerInfluence),
                    lerp(spatialDirection.y, registerDirection.y, registerInfluence),
                    lerp(spatialDirection.z, registerDirection.z, registerInfluence),
                });
            }
            const float az = std::atan2(spatialDirection.y, spatialDirection.x) * 180.0f / kPi;
            const float el = std::asin(clamp(spatialDirection.z, -1.0f, 1.0f)) * 180.0f / kPi;
            const float radius = static_cast<float>(std::sqrt(tp.x * tp.x + tp.y * tp.y + tp.z * tp.z));
            const float dist = clamp(params_.centerDistance * (0.85f + 0.30f * radius) * params_.topologyScale, 0.15f, 2.0f);
            targetPoints_[i] = {
                wrapSignedDeg(params_.centerAzimuthDeg + az),
                clamp(params_.centerElevationDeg + el * 0.72f, -90.0f, 90.0f),
                dist
            };
            points_[i].azimuthDeg = wrapSignedDeg(points_[i].azimuthDeg
                + wrapSignedDeg(targetPoints_[i].azimuthDeg
                    - points_[i].azimuthDeg) * follow);
            points_[i].elevationDeg += (
                targetPoints_[i].elevationDeg - points_[i].elevationDeg)
                * follow;
            points_[i].distance += (
                targetPoints_[i].distance - points_[i].distance)
                * follow;
        }
    }

    void updateMask(float dt)
    {
        maskPhase_ += dt * params_.maskRateHz;
        if (maskPhase_ > 100000.0f) maskPhase_ -= 100000.0f;
    }

    float voiceMask(uint32_t index) const
    {
        if (params_.maskMode == 0u || params_.maskDepth <= 0.0001f) return 1.0f;
        const uint32_t voices = std::max<uint32_t>(1u, params_.voices);
        const float lane = static_cast<float>(index) / static_cast<float>(std::max<uint32_t>(1u, voices - 1u));
        const float phase = maskPhase_ + hash01(nodes_[index].seed + 1009u);
        const float wheel = phase - std::floor(phase);
        float mask = 1.0f;
        switch (params_.maskMode) {
        case 1: {
            const float pulse = 0.5f + 0.5f * std::sin((wheel + lane * 0.17f) * kPi * 2.0f);
            mask = 0.35f + 0.65f * pulse;
            break;
        }
        case 2: {
            const float choir = 0.5f + 0.5f * std::sin((maskPhase_ * 0.43f + lane * 1.7f) * kPi * 2.0f);
            const float laneBias = 0.62f + 0.38f * hash01(nodes_[index].seed + 1223u);
            mask = std::pow(choir, 1.7f) * laneBias;
            break;
        }
        case 3: {
            const float cell = std::fabs((lane - wheel) - std::floor((lane - wheel) + 0.5f));
            mask = cell < 0.18f ? 1.0f : (cell < 0.32f ? 0.35f : 0.03f);
            break;
        }
        case 4: {
            const float n = hash01(static_cast<uint32_t>(
                std::floor(maskPhase_ * 16.0f)) * 7919u
                + nodes_[index].seed);
            mask = n > 0.72f ? 1.0f : (n > 0.54f ? 0.28f : 0.0f);
            break;
        }
        case 5: {
            const float a = 0.5f + 0.5f * std::sin((maskPhase_ * 0.31f + lane * 2.0f) * kPi * 2.0f);
            const float b = 0.5f + 0.5f * std::sin((maskPhase_ * 0.73f - lane * 3.0f) * kPi * 2.0f);
            mask = std::pow(a * b, 1.25f);
            break;
        }
        default:
            break;
        }
        return lerp(1.0f, clamp(mask, 0.0f, 1.0f), params_.maskDepth);
    }

    static float breakpointRange(uint32_t row)
    {
        switch (row) {
        case 0:
        case 1:
            return 0.85f;
        case 6:
        case 7:
        case 8:
        case 9:
        case 10:
        case 11:
            return 0.70f;
        case 12:
            return 1.0f;
        default:
            return 0.95f;
        }
    }

    float laneValue(uint32_t index, uint32_t row, const std::array<float, kAmbiWranglerMaxVoices>& values, float fallback) const
    {
        if (!params_.voiceBreakpointsEnabled) return fallback;
        const float value = values[std::min<uint32_t>(index, kAmbiWranglerMaxVoices - 1u)];
        if (row == 12u) return value;
        return clamp(fallback + (value - 0.5f) * breakpointRange(row), 0.0f, 1.0f);
    }

    static float shapedRamp(float phase, float shape)
    {
        phase -= std::floor(phase);
        const float pivot = clamp(shape, 0.02f, 0.98f);
        const float up = phase < pivot
            ? phase / pivot
            : 1.0f - (phase - pivot) / std::max(0.0001f, 1.0f - pivot);
        return up * 2.0f - 1.0f;
    }

    static float smoothPulse(float ramp, float width, float hz, float sampleRate)
    {
        const float threshold = width * 2.0f - 1.0f;
        const float edge = clamp(hz / std::max(1.0f, sampleRate) * 18.0f, 0.002f, 0.18f);
        return std::tanh((ramp - threshold) / edge);
    }

    float processVoice(
        uint32_t index, uint32_t spatialNode, uint32_t activeEngines)
    {
        auto& voice = voices_[index];
        const bool bounded =
            params_.circuitLaw == AmbiWranglerCircuitLaw::Bounded;
        const bool settleResponse =
            params_.listenerResponse == AmbiWranglerListenerResponse::Settle;
        const float lane = static_cast<float>(index)
            / static_cast<float>(std::max<uint32_t>(1u, activeEngines - 1u));
        const float field = params_.field;
        const float transitionDecay = std::exp(
            -1.0f / static_cast<float>(sampleRate_ * 0.45));
        voice.transitionDensity *= transitionDecay;
        spatialNode = std::min<uint32_t>(
            spatialNode, kAmbiWranglerMaxVoices - 1u);
        const uint32_t settleEar = nearestListener(directionFromAed(
            points_[spatialNode].azimuthDeg,
            points_[spatialNode].elevationDeg));
        const float excess = clamp(
            (listenerTension_[settleEar] - params_.settleTarget)
                / std::max(0.05f, 1.0f - params_.settleTarget),
            0.0f, 1.0f);
        const float shapedExcess = excess * excess * (3.0f - 2.0f * excess);
        const float activityGate = clamp(
            (fieldListener_.activity() - 0.01f) / 0.09f,
            0.0f, 1.0f);
        const float desiredCapture = settleResponse
            && params_.listeningEnabled != 0u
            ? shapedExcess * params_.settleAmount * listenerEnableGain_
                * activityGate
            : 0.0f;
        if (!settleResponse || params_.listeningEnabled == 0u
            || params_.settleAmount <= 0.0f) {
            voice.listenerCapture = 0.0f;
            voice.listenerEvolutionAccumulator = 0.0f;
            voice.registerHeld = 0u;
        } else {
            const float captureSeconds =
                desiredCapture > voice.listenerCapture
                ? 0.18f : params_.settleRecoverySeconds;
            const float captureCoefficient = 1.0f - std::exp(
                -1.0f / static_cast<float>(
                    sampleRate_ * captureSeconds));
            voice.listenerCapture += (
                desiredCapture - voice.listenerCapture)
                * captureCoefficient;
            voice.listenerCapture =
                flushDenormal(voice.listenerCapture);
        }
        // Curves belong to circuits, not their distributed render nodes. In
        // Legacy law index == spatialNode, preserving the original lookup.
        const float rateAParam = laneValue(index, 0u, params_.bpRateA, params_.rateA);
        const float rateBParam = laneValue(index, 1u, params_.bpRateB, params_.rateB);
        const float fmAtoBParam = laneValue(index, 2u, params_.bpFmAtoB, params_.fmAtoB);
        const float fmBtoAParam = laneValue(index, 3u, params_.bpFmBtoA, params_.fmBtoA);
        const float runglerAParam = laneValue(index, 4u, params_.bpRunglerA, params_.runglerA);
        const float runglerBParam = laneValue(index, 5u, params_.bpRunglerB, params_.runglerB);
        const float filterParam = laneValue(index, 6u, params_.bpFilter, params_.filter);
        const float thresholdParam = laneValue(index, 7u, params_.bpThreshold, params_.threshold);
        const float pwmAParam = laneValue(index, 8u, params_.bpPwmA, params_.pwmA);
        const float pwmBParam = laneValue(index, 9u, params_.bpPwmB, params_.pwmB);
        const float rampAParam = laneValue(index, 10u, params_.bpRampA, params_.rampA);
        const float rampBParam = laneValue(index, 11u, params_.bpRampB, params_.rampB);
        const float ampParam = laneValue(index, 12u, params_.bpAmp, 1.0f);
        const float spreadA = (lane - 0.5f) * params_.spread * (1.4f + field * 1.6f);
        const float spreadB = (0.5f - lane) * params_.spread * (1.2f + field * 1.4f);
        const float devA = hashSigned(voice.seed + 101u) * params_.deviation * (0.35f + field * 0.85f);
        const float devB = hashSigned(voice.seed + 211u) * params_.deviation * (0.35f + field * 0.85f);
        const float rateScaleA = rateModeScale(params_.rateModeA);
        const float rateScaleB = rateModeScale(params_.rateModeB);
        float baseA = 0.0f;
        float baseB = 0.0f;
        float laneFmA = 0.0f;
        float laneFmB = 0.0f;
        float laneRunA = 0.0f;
        float laneRunB = 0.0f;
        float hzA = 0.0f;
        float hzB = 0.0f;
        if (!bounded) {
            baseA = targetNormToHz(clamp(rateAParam
                + hashSigned(voice.seed + 307u) * field
                    * params_.deviation * 0.42f, 0.0f, 1.0f))
                * rateScaleA * std::pow(2.0f, spreadA + devA) * voice.rateA;
            baseB = targetNormToHz(clamp(rateBParam
                + hashSigned(voice.seed + 401u) * field
                    * params_.deviation * 0.42f, 0.0f, 1.0f))
                * rateScaleB * std::pow(2.0f, spreadB + devB) * voice.rateB;
            laneFmA = clamp(fmBtoAParam
                + hashSigned(voice.seed + 503u) * field
                    * params_.deviation * 0.55f, 0.0f, 1.0f);
            laneFmB = clamp(fmAtoBParam
                + hashSigned(voice.seed + 601u) * field
                    * params_.deviation * 0.55f, 0.0f, 1.0f);
            laneRunA = clamp(runglerAParam
                + hashSigned(voice.seed + 701u) * field
                    * params_.deviation * 0.65f, 0.0f, 1.0f);
            laneRunB = clamp(runglerBParam
                + hashSigned(voice.seed + 809u) * field
                    * params_.deviation * 0.65f, 0.0f, 1.0f);
            const float couplingRate =
                listenerCouplingRate(voice.listenerCapture);
            const float fmToA =
                phaseTri(voice.phaseB) * targetNormToHz(laneFmA)
                    * couplingRate;
            const float fmToB =
                phaseTri(voice.phaseA) * targetNormToHz(laneFmB)
                    * couplingRate;
            const float rungToA =
                voice.rungSmooth * targetNormToHz(laneRunA)
                    * couplingRate;
            const float rungToB =
                voice.rungSmooth * targetNormToHz(laneRunB)
                    * couplingRate;
            hzA = clampOscHz(
                baseA + fmToA + rungToA, static_cast<float>(sampleRate_));
            hzB = clampOscHz(
                baseB + fmToB + rungToB, static_cast<float>(sampleRate_));
        } else {
            // In Bounded law modulation is a depth around a positive base,
            // never a second absolute-frequency oscillator.
            baseA = targetNormToHz(rateAParam) * rateScaleA
                * std::pow(2.0f, spreadA * 0.65f + devA * 0.45f)
                * voice.rateA;
            baseB = targetNormToHz(rateBParam) * rateScaleB
                * std::pow(2.0f, spreadB * 0.65f + devB * 0.45f)
                * voice.rateB;
            laneFmA = params_.fmBtoA <= 0.000001f ? 0.0f
                : clamp(fmBtoAParam * (1.0f
                    + hashSigned(voice.seed + 503u) * field
                        * params_.deviation * 0.55f), 0.0f, 1.0f);
            laneFmB = params_.fmAtoB <= 0.000001f ? 0.0f
                : clamp(fmAtoBParam * (1.0f
                    + hashSigned(voice.seed + 601u) * field
                        * params_.deviation * 0.55f), 0.0f, 1.0f);
            laneRunA = params_.runglerA <= 0.000001f ? 0.0f
                : clamp(runglerAParam * (1.0f
                    + hashSigned(voice.seed + 701u) * field
                        * params_.deviation * 0.65f), 0.0f, 1.0f);
            laneRunB = params_.runglerB <= 0.000001f ? 0.0f
                : clamp(runglerBParam * (1.0f
                    + hashSigned(voice.seed + 809u) * field
                        * params_.deviation * 0.65f), 0.0f, 1.0f);
            const float couplingRate =
                listenerCouplingRate(voice.listenerCapture);
            const float pitchA = clamp(
                phaseTri(voice.phaseB) * 18.0f * laneFmA * laneFmA
                    + voice.rungSmooth * 24.0f * laneRunA * laneRunA,
                -36.0f, 36.0f) * couplingRate;
            const float pitchB = clamp(
                phaseTri(voice.phaseA) * 18.0f * laneFmB * laneFmB
                    + voice.rungSmooth * 24.0f * laneRunB * laneRunB,
                -36.0f, 36.0f) * couplingRate;
            hzA = clampPositiveOscHz(
                baseA * std::pow(2.0f, pitchA / 12.0f),
                static_cast<float>(sampleRate_));
            hzB = clampPositiveOscHz(
                baseB * std::pow(2.0f, pitchB / 12.0f),
                static_cast<float>(sampleRate_));
        }
        voice.phaseA += hzA / static_cast<float>(sampleRate_);
        voice.phaseB += hzB / static_cast<float>(sampleRate_);
        voice.phaseA -= std::floor(voice.phaseA);
        voice.phaseB -= std::floor(voice.phaseB);

        const float triA = shapedRamp(voice.phaseA, rampAParam);
        const float triB = shapedRamp(voice.phaseB, rampBParam);
        const float widthA = clamp(pwmAParam + (thresholdParam - 0.5f) * 0.42f, 0.03f, 0.97f);
        const float widthB = clamp(pwmBParam - (thresholdParam - 0.5f) * 0.42f, 0.03f, 0.97f);
        const float squareA = smoothPulse(triA, widthA, std::fabs(hzA), static_cast<float>(sampleRate_));
        const float squareB = smoothPulse(triB, widthB, std::fabs(hzB), static_cast<float>(sampleRate_));
        const float clockInput = params_.inputB == 0u ? squareB : triB;
        const bool clock = clockInput > 0.0f && voice.lastClock <= 0.0f;
        voice.lastClock = clockInput;
        voice.fieldWritePulse *= 0.992f;
        if (clock) {
            ++voice.clockCount;
            const uint32_t ear = listenerEarForClock(index, spatialNode);
            voice.lastReadEar = ear;
            const auto decision = auditoryDecision(ear, voice.auditoryBit);
            voice.auditoryBit = decision.bit;
            const bool writeResponse =
                params_.listenerResponse == AmbiWranglerListenerResponse::Write;
            voice.lastReturn = writeResponse
                ? delayedListenerSignal(ear) * params_.fieldReturn
                    * params_.fieldReturn * returnEnableGain_
                : 0.0f;
            const float nativeComparator = params_.inputA == 0u
                ? triA - (widthA * 2.0f - 1.0f)
                : triA;
            const float input = nativeComparator + voice.lastReturn * 1.8f;
            const uint32_t inputBit = input > 0.0f ? 1u : 0u;
            voice.comparatorBit = inputBit;
            bool advanceRegister = true;
            if (settleResponse && voice.listenerCapture > 0.000001f) {
                voice.listenerEvolutionAccumulator +=
                    listenerEvolutionRate(voice.listenerCapture);
                if (voice.listenerEvolutionAccumulator >= 1.0f) {
                    voice.listenerEvolutionAccumulator -= 1.0f;
                } else {
                    advanceRegister = false;
                }
            } else {
                voice.listenerEvolutionAccumulator = 0.0f;
            }

            voice.registerHeld = advanceRegister ? 0u : 1u;
            if (!advanceRegister) {
                ++voice.heldClockCount;
            } else {
                ++voice.registerAdvanceCount;
                const uint32_t mask = (1u << params_.rungSize) - 1u;
                const uint32_t loopBit =
                    (voice.reg >> (params_.rungSize - 1u)) & 1u;
                uint32_t bit = inputBit;
                if (params_.rungLoop == 1u) bit = loopBit;
                else if (params_.rungLoop == 2u) bit = inputBit ^ loopBit;
                if (bounded) {
                    voice.changeAccumulator += params_.change;
                    if (voice.changeAccumulator >= 1.0f) {
                        voice.changeAccumulator -= 1.0f;
                    } else {
                        bit = loopBit;
                    }
                }
                const float writeProbability = writeResponse
                    ? params_.fieldWrite * params_.fieldWrite
                        * 0.45f * listenerEnableGain_
                        * (0.30f + fieldListener_.activity() * 0.70f)
                        * (0.55f + decision.confidence * 0.45f)
                    : 0.0f;
                voice.fieldWriteAccumulator += writeProbability;
                if (voice.fieldWriteAccumulator >= 1.0f) {
                    voice.fieldWriteAccumulator -= 1.0f;
                    bit = voice.auditoryBit;
                    voice.fieldWritePulse = 1.0f;
                }
                if (bit != loopBit) {
                    voice.transitionDensity =
                        std::min(1.0f, voice.transitionDensity + 0.26f);
                }
                voice.writtenBit = bit;
                voice.reg = ((voice.reg << 1u) | bit) & mask;
                const float denom = static_cast<float>(
                    std::max<uint32_t>(1u, mask));
                voice.rungValue =
                    static_cast<float>(voice.reg) / denom * 2.0f - 1.0f;
                const float edgeDelta =
                    voice.rungValue - voice.rungSmooth;
                voice.snapPolarity = edgeDelta == 0.0f
                    ? (bit ? 1.0f : -1.0f)
                    : (edgeDelta > 0.0f ? 1.0f : -1.0f);
                voice.snapEnv = std::min(
                    1.0f,
                    voice.snapEnv
                        + (0.38f + std::fabs(edgeDelta) * 0.62f));
            }
        }
        const float rungAmount = bounded
            ? std::max(laneRunA, laneRunB)
            : std::max(params_.runglerA, params_.runglerB);
        if (!bounded) {
            voice.rungSmooth += (voice.rungValue - voice.rungSmooth)
                * (0.002f + rungAmount * 0.030f);
        } else {
            const float baseSlewSeconds =
                lerp(0.300f, 0.035f, std::sqrt(rungAmount));
            const float slewCoefficient = 1.0f - std::exp(
                -1.0f / static_cast<float>(
                    sampleRate_ * baseSlewSeconds));
            voice.rungSmooth += (
                voice.rungValue - voice.rungSmooth) * slewCoefficient;
        }
        const float snapDecaySamples = 12.0f + std::pow(params_.snapDecay, 2.0f) * 900.0f;
        const float snapDecayCoeff = std::exp(-1.0f / snapDecaySamples);
        const float snapTransient = voice.snapEnv * voice.snapPolarity * params_.snap;
        voice.snapEnv *= snapDecayCoeff;

        if (!bounded) {
            const float sweep = (triB * 0.5f + 0.5f)
                * params_.filterSweep * 0.11f;
            const float run = (voice.rungSmooth * 0.5f + 0.5f)
                * params_.filterRun * 0.14f;
            const float filterNorm = 0.0015f
                + std::pow(filterParam, 2.2f) * 0.20f + sweep + run;
            const float rungAudio =
                voice.rungValue * (0.10f + rungAmount * 0.18f);
            const float pwm = squareA * 0.55f + squareB * 0.45f
                + (triA - triB) * 0.25f + rungAudio;
            const float filtA = voice.filterA.process(
                pwm + squareB * laneRunA * 0.30f + snapTransient * 0.46f,
                filterNorm, params_.resonance, params_.saturation);
            const float filtB = voice.filterB.process(
                pwm + squareA * laneRunB * 0.30f - snapTransient * 0.32f,
                filterNorm * 1.17f, params_.resonance, params_.saturation);
            const float edge = (squareA + squareB) * 0.24f;
            const float body = (triA + triB) * 0.32f;
            const float bloom = (filtA + filtB) * 0.48f;
            const float mixed =
                lerp(edge + body, bloom + body * 0.24f, params_.color);
            const float click = snapTransient
                * (0.10f + (1.0f - params_.color) * 0.22f);
            return std::tanh(clamp(
                (mixed + click) * (1.0f + params_.saturation * 3.2f),
                -7.0f, 7.0f)) * ampParam;
        }

        // The bounded source follows the characteristic Benjolin relation:
        // audio PWM is the comparison of the two triangle oscillators. The
        // individual pulse outputs remain available to clock and perturb the
        // register, but no sub-audio square is mixed directly into the body.
        const float comparatorEdge = clamp(
            (std::fabs(hzA) + std::fabs(hzB))
                / static_cast<float>(sampleRate_) * 12.0f,
            0.002f, 0.18f);
        const float comparatorBias = (thresholdParam - 0.5f) * 0.90f
            + (pwmAParam - pwmBParam) * 0.45f;
        const float classicPwm = std::tanh(
            (triA - triB - comparatorBias) / comparatorEdge);
        const float sweep = (triB * 0.5f + 0.5f)
            * params_.filterSweep * 0.085f;
        const float run = (voice.rungSmooth * 0.5f + 0.5f)
            * params_.filterRun * 0.105f;
        const float filterNorm = 0.0015f
            + std::pow(filterParam, 2.2f) * 0.20f + sweep + run;
        const float source = classicPwm * 0.82f;
        const float filterDrive = params_.saturation * 0.55f;
        const float filtA = voice.filterA.process(
            source + snapTransient * 0.30f,
            filterNorm, params_.resonance, filterDrive);
        const float filtB = voice.filterB.process(
            source - snapTransient * 0.22f,
            filterNorm * 1.12f, params_.resonance, filterDrive);
        const float dry = classicPwm * 0.48f;
        const float bloom = (filtA + filtB) * 0.52f;
        // Color=1 is intentionally all-wet; no dry ramp bypass remains.
        const float mixed = lerp(dry, bloom, params_.color);
        const float click = snapTransient
            * (0.07f + (1.0f - params_.color) * 0.15f);
        return std::tanh(clamp(
            (mixed + click) * (1.0f + params_.saturation * 1.7f),
            -7.0f, 7.0f)) * ampParam;
    }

    double sampleRate_ = 48000.0;
    AmbiWranglerParams params_ {};
    std::array<AmbiWranglerVoice, kAmbiWranglerMaxVoices> voices_ {};
    std::array<AmbiWranglerNode, kAmbiWranglerMaxVoices> nodes_ {};
    std::array<AmbiWranglerPoint, kAmbiWranglerMaxVoices> points_ {};
    std::array<AmbiWranglerPoint, kAmbiWranglerMaxVoices> targetPoints_ {};
    float topologyPhase_ = 0.0f;
    float maskPhase_ = 0.0f;
    float smoothedOutputGain_ = 0.0f;
    float listenerEnableGain_ = 0.0f;
    float returnEnableGain_ = 0.0f;
    AmbiFieldListener fieldListener_ {};
    std::array<float, kAmbiWranglerMaxChannels> listenerFrame_ {};
    std::array<std::vector<float>, kAmbiFieldListenerMaxLobes> listenerDelay_ {};
    std::array<float, kAmbiFieldListenerMaxLobes> listenerDc_ {};
    std::array<float, kAmbiFieldListenerMaxLobes> listenerConditioned_ {};
    std::array<float, kAmbiFieldListenerMaxLobes> listenerDecision_ {};
    std::array<float, kAmbiFieldListenerMaxLobes> listenerPreviousSignal_ {};
    std::array<float, kAmbiFieldListenerMaxLobes> listenerRoughness_ {};
    std::array<float, kAmbiFieldListenerMaxLobes> listenerTension_ {};
    size_t listenerDelayWrite_ = 0u;
};

} // namespace s3g
