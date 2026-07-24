#pragma once

#include "s3g_ambi_field_listener.h"
#include "s3g_ambisonic_speaker_decoder.h"
#include "s3g_realtime.h"
#include "s3g_topology.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace s3g {

constexpr uint32_t kAmbiWranglerMaxVoices = 64;
constexpr uint32_t kAmbiWranglerMaxOrder = 7;
constexpr uint32_t kAmbiWranglerMaxChannels = 64;
constexpr uint32_t kAmbiWranglerHistoricalCurveDimensionCount = 19u;
constexpr uint32_t kAmbiWranglerCurveDimensionCount = 24u;

// The first nineteen dimensions retain the order of the twenty-voice
// wrnglr.gendsp controller in __sluicer24_3OAencoder-01.maxpat. The five
// modern dimensions follow it so UI, state, and DSP can share one contract.
enum class AmbiWranglerCurveDimension : uint32_t {
    RateA = 0u,
    RungA,
    FmBtoA,
    ColorA,
    RateB,
    RungB,
    FmAtoB,
    ColorB,
    FilterFreqA,
    FilterFreqB,
    FilterRes,
    FilterComp,
    FilterType,
    CrossA,
    CrossB,
    CrossLpf,
    RungMode,
    RungThresh,
    RungSize,
    PwmA,
    PwmB,
    RampA,
    RampB,
    Amp,
};

static_assert(
    static_cast<uint32_t>(AmbiWranglerCurveDimension::RungSize) + 1u
        == kAmbiWranglerHistoricalCurveDimensionCount);
static_assert(
    static_cast<uint32_t>(AmbiWranglerCurveDimension::Amp) + 1u
        == kAmbiWranglerCurveDimensionCount);

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

enum class AmbiWranglerRungMode : uint32_t {
    Common = 0u,
    Split = 1u,
    Swap = 2u,
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
    // Continuous DJ-style response: 0 = low-pass, 0.5 = open,
    // 1 = high-pass. This deliberately replaces the former stepped outlet.
    float filterMorph = 0.0f;
    std::array<float, kAmbiWranglerMaxVoices> bpColorA {};
    std::array<float, kAmbiWranglerMaxVoices> bpColorB {};
    std::array<float, kAmbiWranglerMaxVoices> bpFilterFreqB {};
    std::array<float, kAmbiWranglerMaxVoices> bpFilterRes {};
    std::array<float, kAmbiWranglerMaxVoices> bpFilterComp {};
    std::array<float, kAmbiWranglerMaxVoices> bpFilterType {};
    std::array<float, kAmbiWranglerMaxVoices> bpCrossA {};
    std::array<float, kAmbiWranglerMaxVoices> bpCrossB {};
    std::array<float, kAmbiWranglerMaxVoices> bpCrossLpf {};
    std::array<float, kAmbiWranglerMaxVoices> bpRungMode {};
    std::array<float, kAmbiWranglerMaxVoices> bpRungSize {};
};

static_assert(
    offsetof(AmbiWranglerParams, bpColorA) == 3576u,
    "New Wrangler curves must remain appended after the v12 state");

struct AmbiWranglerPoint {
    float azimuthDeg = 0.0f;
    float elevationDeg = 0.0f;
    float distance = 1.0f;
};

// Seventh-order Padé tanh. Across the active nonlinear range its absolute
// error stays below 0.00011, while avoiding hundreds of scalar libm calls per
// sample in a 64-voice/64-channel render. The final clamp also gives every
// feedback and output path a strict finite bound.
inline float ambiWranglerTanh(float input)
{
    const float x = clamp(input, -5.0f, 5.0f);
    const float x2 = x * x;
    const float numerator =
        x * (135135.0f
            + x2 * (17325.0f
                + x2 * (378.0f + x2)));
    const float denominator =
        135135.0f
        + x2 * (62370.0f
            + x2 * (3150.0f + x2 * 28.0f));
    return clamp(numerator / denominator, -1.0f, 1.0f);
}

struct AmbiWranglerFilter {
    struct Outputs {
        float low = 0.0f;
        float high = 0.0f;
        float open = 0.0f;
        float legacy = 0.0f;
    };

    float low = 0.0f;
    float band = 0.0f;

    Outputs processSvf(
        float input, float cutoffNorm, float resonance, float drive)
    {
        const float f = clamp(cutoffNorm, 0.0002f, 0.46f);
        const float q =
            1.72f - clamp(resonance, 0.0f, 1.0f) * 1.42f;
        const float driven = ambiWranglerTanh(clamp(
            input * (1.0f + drive * 5.0f), -8.0f, 8.0f));
        low = flushDenormal(clamp(low + f * band, -16.0f, 16.0f));
        const float high = driven - low - q * band;
        band = flushDenormal(clamp(band + f * high, -16.0f, 16.0f));
        return {
            low,
            flushDenormal(high),
            driven,
            low + band * (0.25f + resonance * 0.55f),
        };
    }
};

struct AmbiWranglerVoiceTargets {
    float rateA = 0.28f;
    float rateB = 0.34f;
    float fmAtoB = 0.18f;
    float fmBtoA = 0.14f;
    float runglerA = 0.25f;
    float runglerB = 0.32f;
    float filterFreqA = 0.36f;
    float filterFreqB = 0.36f;
    float filterRes = 0.20f;
    float filterComp = 0.0f;
    float filterMorph = 0.0f;
    float colorA = 0.0f;
    float colorB = 0.0f;
    float crossA = 0.0f;
    float crossB = 0.0f;
    float crossLpf = 0.25f;
    float rungMode = 0.0f;
    float rungSize = 0.5f;
    float threshold = 0.50f;
    float pwmA = 0.50f;
    float pwmB = 0.50f;
    float rampA = 0.50f;
    float rampB = 0.50f;
    float amp = 1.0f;
};

struct AmbiWranglerVoice {
    float phaseA = 0.0f;
    float phaseB = 0.0f;
    float rateA = 80.0f;
    float rateB = 91.0f;
    float rungValue = 0.0f;
    float rungSmooth = 0.0f;
    float rungSplitA = 0.0f;
    float rungSplitB = 0.0f;
    float rungSplitSmoothA = 0.0f;
    float rungSplitSmoothB = 0.0f;
    float lastClock = 0.0f;
    float energy = 0.0f;
    float maskGain = 1.0f;
    float targetMaskGain = 1.0f;
    float snapEnv = 0.0f;
    float snapPolarity = 1.0f;
    float fieldWriteAccumulator = 0.0f;
    float fieldWritePulse = 0.0f;
    float lastReturn = 0.0f;
    float changeAccumulator = 0.0f;
    float transitionDensity = 0.0f;
    float listenerCapture = 0.0f;
    float listenerEvolutionAccumulator = 0.0f;
    float smoothedRateA = 0.28f;
    float smoothedRateB = 0.34f;
    float smoothedFmAtoB = 0.18f;
    float smoothedFmBtoA = 0.14f;
    float smoothedRunglerA = 0.25f;
    float smoothedRunglerB = 0.32f;
    float smoothedSpread = 0.26f;
    float smoothedDeviation = 0.12f;
    float smoothedField = 0.62f;
    float smoothedLane = 0.0f;
    float smoothedRateScaleA = 1.0f;
    float smoothedRateScaleB = 1.0f;
    float smoothedPwmA = 0.50f;
    float smoothedPwmB = 0.50f;
    float smoothedRampA = 0.50f;
    float smoothedRampB = 0.50f;
    float smoothedThreshold = 0.50f;
    float smoothedColor = 0.42f;
    float smoothedColorA = 0.0f;
    float smoothedColorB = 0.0f;
    float smoothedFilterFreqA = 0.36f;
    float smoothedFilterFreqB = 0.36f;
    float smoothedFilterRes = 0.20f;
    float smoothedFilterComp = 0.0f;
    float smoothedCrossA = 0.0f;
    float smoothedCrossB = 0.0f;
    float smoothedCrossLpf = 0.25f;
    float smoothedFilterMorph = 0.0f;
    float smoothedFilterRun = 0.28f;
    float smoothedFilterSweep = 0.20f;
    float smoothedSaturation = 0.36f;
    float smoothedSnap = 0.0f;
    float smoothedSnapDecay = 0.34f;
    float smoothedAmp = 1.0f;
    float smoothedRungOutputA = 0.0f;
    float smoothedRungOutputB = 0.0f;
    float oscillatorFeedbackA = 0.0f;
    float oscillatorFeedbackB = 0.0f;
    float crossFeedbackA = 0.0f;
    float crossFeedbackB = 0.0f;
    float previousFilteredA = 0.0f;
    float previousFilteredB = 0.0f;
    float renderGain = 0.0f;
    float lastOutput = 0.0f;
    float outputTransitionCorrection = 0.0f;
    float circuitPreviousOutput = 0.0f;
    float circuitCurrentOutput = 0.0f;
    float cachedBaseA = 1.0f;
    float cachedBaseB = 1.0f;
    float cachedLaneFmA = 0.0f;
    float cachedLaneFmB = 0.0f;
    float cachedLaneRunA = 0.0f;
    float cachedLaneRunB = 0.0f;
    float cachedLegacyFmHzA = 0.0f;
    float cachedLegacyFmHzB = 0.0f;
    float cachedLegacyRungHzA = 0.0f;
    float cachedLegacyRungHzB = 0.0f;
    float cachedFilterCurveA = 0.0f;
    float cachedFilterCurveB = 0.0f;
    float cachedCrossCoefficient = 0.0f;
    float cachedFeedbackCompensation = 1.0f;
    float cachedFilterInputCompensation = 1.0f;
    float cachedOutputCompensation = 1.0f;
    float cachedListenerCoupling = 1.0f;
    float cachedSnapDecay = 1.0f;
    AmbiWranglerVoiceTargets targets {};
    uint32_t reg = 1u;
    uint32_t seed = 1u;
    uint32_t listenHead = 0u;
    uint32_t lastReadEar = 0u;
    uint32_t comparatorBit = 0u;
    uint32_t auditoryBit = 0u;
    uint32_t writtenBit = 0u;
    uint32_t registerHeld = 0u;
    uint32_t latchedInputA = 0u;
    uint32_t latchedInputB = 0u;
    uint32_t latchedRungMode = 0u;
    uint32_t latchedRungSize = 4u;
    uint32_t settleEar = 0u;
    bool outputTransitionPending = false;
    bool circuitOutputPrimed = false;
    uint64_t clockCount = 0u;
    uint64_t registerAdvanceCount = 0u;
    uint64_t heldClockCount = 0u;
    AmbiWranglerFilter filterA {};
    AmbiWranglerFilter filterB {};
};

class AmbiWranglerEncoder {
public:
    void prepare(double sampleRate)
    {
        if (!std::isfinite(sampleRate)) sampleRate = 48000.0;
        sampleRate_ = std::clamp(sampleRate, 1000.0, 768000.0);
        // The analogue-model circuit itself does not gain useful bandwidth
        // above roughly 48 kHz. At high host rates, advance it on a bounded
        // internal clock and linearly interpolate its already band-limited
        // output; HOA motion, fades, listener delay, and host automation
        // remain at the full host rate.
        circuitStride_ = std::max<uint32_t>(
            1u, static_cast<uint32_t>(
                std::ceil(sampleRate_ / 48000.0)));
        circuitSampleRate_ =
            sampleRate_ / static_cast<double>(circuitStride_);
        outputTransitionDecay_ = std::exp(
            -1.0f / static_cast<float>(
                circuitSampleRate_ * 0.008));
        transitionDecay_ = std::exp(
            -1.0f / static_cast<float>(
                circuitSampleRate_ * 0.45));
        edgeCoefficient_ = circuitOnePoleCoefficient(0.010f);
        shapeCoefficient_ = circuitOnePoleCoefficient(0.018f);
        colorCoefficient_ = circuitOnePoleCoefficient(0.020f);
        feedbackCoefficient_ = circuitOnePoleCoefficient(0.024f);
        ampCoefficient_ = circuitOnePoleCoefficient(0.012f);
        captureAttackCoefficient_ =
            circuitOnePoleCoefficient(0.18f);
        maskAttackCoefficient_ = onePoleCoefficient(0.004f);
        maskReleaseCoefficient_ = onePoleCoefficient(0.018f);
        voiceAttackCoefficient_ = onePoleCoefficient(0.012f);
        voiceReleaseCoefficient_ = onePoleCoefficient(0.024f);
        outputGainCoefficient_ = onePoleCoefficient(0.018f);
        // Spatial coefficients contain high-order trigonometry. A 64-sample
        // target clock remains well above audible motion rates and the
        // coefficients interpolate linearly on every sample between ticks.
        spatialControlStep_ = (
            1.0f - std::exp(
                -64.0f / static_cast<float>(
                    sampleRate_ * 0.014)))
            / 64.0f;
        orderCoefficient_ = onePoleCoefficient(0.020f);
        listenerEnableCoefficient_ = onePoleCoefficient(0.018f);
        returnEnableCoefficient_ = onePoleCoefficient(0.026f);
        startupCoefficient_ = onePoleCoefficient(0.008f);
        for (uint32_t index = 0u;
            index < kCoefficientTableSize; ++index) {
            const float value = static_cast<float>(index)
                / static_cast<float>(kCoefficientTableSize - 1u);
            const float crossCutoffHz =
                2.0f * std::pow(3000.0f, value);
            crossCoefficientTable_[index] =
                1.0f - std::exp(
                    -2.0f * kPi * crossCutoffHz
                    / static_cast<float>(
                        circuitSampleRate_));
            const float rungSeconds =
                lerp(0.300f, 0.035f, std::sqrt(value));
            rungCoefficientTable_[index] =
                circuitOnePoleCoefficient(rungSeconds);
            const float snapSamples =
                (12.0f + value * value * 900.0f)
                * static_cast<float>(
                    circuitSampleRate_ / 48000.0);
            snapDecayTable_[index] =
                std::exp(-1.0f / snapSamples);
            listenerCouplingTable_[index] =
                listenerCouplingRate(value);
        }
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
        audioStarted_ = false;
        controlFramesRemaining_ = 0u;
        spatialFramesRemaining_ = 0u;
        circuitFramesRemaining_ = 0u;
        expensiveControlFramesRemaining_ = 0u;
        startupGain_ = 0.0f;
        topologyPhase_ = 0.0f;
        maskPhase_ = 0.0f;
        listenerEnableGain_ = 0.0f;
        returnEnableGain_ = 0.0f;
        listenerDelayWrite_ = 0u;
        listenerDelayValidSamples_ = 0u;
        listenerDormant_ = true;
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
        smoothedOutputGain_ = dbToGain(params_.outputGainDb)
            / std::sqrt(static_cast<float>(
                std::max<uint32_t>(1u, params_.voices)));
        for (uint32_t i = 0u; i < kAmbiWranglerMaxVoices; ++i) {
            voices_[i] = {};
            voices_[i].seed = 0x9e3779b9u + i * 0x85ebca6bu;
            voices_[i].reg = (hash(voices_[i].seed) & 0xffu) | 1u;
            initializeVoice(i);
            voices_[i].renderGain =
                i < params_.voices ? 1.0f : 0.0f;
            points_[i] = basePoint(i, std::max<uint32_t>(1u, params_.voices));
            targetPoints_[i] = points_[i];
            renderBasis_[i] = acnSn3dBasis7(directionFromAed(
                points_[i].azimuthDeg, points_[i].elevationDeg));
            targetRenderBasis_[i] = renderBasis_[i];
            renderDistanceGain_[i] =
                1.0f / std::max(0.50f, points_[i].distance);
            targetRenderDistanceGain_[i] =
                renderDistanceGain_[i];
        }
        const uint32_t activeChannels =
            ambiChannelsForOrder(params_.order);
        processingVoiceLimit_ = params_.voices;
        processingChannelLimit_ = activeChannels;
        for (uint32_t ch = 0u;
            ch < kAmbiWranglerMaxChannels; ++ch) {
            channelOrderGain_[ch] =
                ch < activeChannels ? 1.0f : 0.0f;
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
            params.bpColorA[i] = clamp(params.bpColorA[i], 0.0f, 1.0f);
            params.bpColorB[i] = clamp(params.bpColorB[i], 0.0f, 1.0f);
            params.bpFilterFreqB[i] =
                clamp(params.bpFilterFreqB[i], 0.0f, 1.0f);
            params.bpFilterRes[i] =
                clamp(params.bpFilterRes[i], 0.0f, 1.0f);
            params.bpFilterComp[i] =
                clamp(params.bpFilterComp[i], 0.0f, 1.0f);
            params.bpFilterType[i] =
                clamp(params.bpFilterType[i], 0.0f, 1.0f);
            params.bpCrossA[i] = clamp(params.bpCrossA[i], 0.0f, 1.0f);
            params.bpCrossB[i] = clamp(params.bpCrossB[i], 0.0f, 1.0f);
            params.bpCrossLpf[i] =
                clamp(params.bpCrossLpf[i], 0.0f, 1.0f);
            params.bpRungMode[i] =
                clamp(params.bpRungMode[i], 0.0f, 1.0f);
            params.bpRungSize[i] =
                clamp(params.bpRungSize[i], 0.0f, 1.0f);
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
        // Retained as an internal layout field, but a Wrangler point is one
        // complete synth voice and no second synthesis count is exposed.
        params.engines = params.voices;
        params.change = clamp(params.change, 0.0f, 1.0f);
        params.listenerResponse = static_cast<AmbiWranglerListenerResponse>(
            std::min<uint32_t>(static_cast<uint32_t>(params.listenerResponse), 1u));
        params.settleAmount = clamp(params.settleAmount, 0.0f, 1.0f);
        params.settleTarget = clamp(params.settleTarget, 0.0f, 0.95f);
        params.settleRecoverySeconds = clamp(params.settleRecoverySeconds, 0.25f, 12.0f);
        params.filterMorph = clamp(params.filterMorph, 0.0f, 1.0f);
        const bool pickupSetChanged = params.pickupSet != params_.pickupSet;
        const bool circuitLawChanged =
            params.circuitLaw != params_.circuitLaw;
        params_ = params;
        if (audioStarted_) {
            processingVoiceLimit_ = std::max(
                processingVoiceLimit_, params_.voices);
            processingChannelLimit_ = std::max(
                processingChannelLimit_,
                ambiChannelsForOrder(params_.order));
            for (uint32_t voice = 0u;
                voice < processingVoiceLimit_; ++voice) {
                updateVoiceTargets(voice);
                voices_[voice].targetMaskGain =
                    voiceMask(voice);
            }
            // A host event may arrive between ticks of the four-sample
            // expensive-control clock. Re-arm it so the event takes effect
            // on its exact sample rather than as much as three samples late.
            expensiveControlFramesRemaining_ = 0u;
        }
        captureReleaseCoefficient_ =
            circuitOnePoleCoefficient(
                params_.settleRecoverySeconds);
        if (!audioStarted_) {
            // A preset or restored state installed before the first rendered
            // sample is the initial condition, not an automation gesture.
            // Prime every smoother and structural latch to it so the first
            // milliseconds cannot leak the constructor defaults into audio.
            primeUnheardState();
        } else if (circuitLawChanged) {
            for (auto& voice : voices_) {
                voice.outputTransitionPending = true;
            }
        }
        if (pickupSetChanged) configureListener();
    }

    AmbiWranglerParams params() const { return params_; }
    uint32_t engineCount() const { return params_.voices; }
    uint32_t nodeEngine(uint32_t node) const
    {
        return std::min<uint32_t>(
            node, kAmbiWranglerMaxVoices - 1u);
    }
    float voiceEnergy(uint32_t voice) const { return voices_[std::min<uint32_t>(voice, kAmbiWranglerMaxVoices - 1u)].energy; }
    AmbiWranglerPoint voicePoint(uint32_t voice) const { return points_[std::min<uint32_t>(voice, kAmbiWranglerMaxVoices - 1u)]; }
    float voiceMaskLevel(uint32_t voice) const { return voices_[std::min<uint32_t>(voice, kAmbiWranglerMaxVoices - 1u)].maskGain; }
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
    float voiceCurveValue(
        uint32_t voice, AmbiWranglerCurveDimension dimension) const
    {
        return laneCurveValue(
            std::min<uint32_t>(
                voice, kAmbiWranglerMaxVoices - 1u),
            dimension);
    }
    uint32_t voiceRungMode(uint32_t voice) const
    {
        return voices_[nodeEngine(voice)].latchedRungMode;
    }
    uint32_t voiceRungSize(uint32_t voice) const
    {
        return voices_[nodeEngine(voice)].latchedRungSize;
    }
    float voiceFilterMorph(uint32_t voice) const
    {
        return voices_[nodeEngine(voice)].smoothedFilterMorph;
    }
    float voiceFilterStatePeak(uint32_t voice) const
    {
        const auto& state = voices_[nodeEngine(voice)];
        return std::max({
            std::fabs(state.filterA.low),
            std::fabs(state.filterA.band),
            std::fabs(state.filterB.low),
            std::fabs(state.filterB.band),
        });
    }
    static float curveFilterMorph(float stored, float fallback)
    {
        return centeredStructuralCurve(stored, fallback);
    }
    static uint32_t curveRungSize(
        float stored, uint32_t fallback)
    {
        fallback = std::clamp<uint32_t>(fallback, 2u, 8u);
        const float effective = centeredStructuralCurve(
            stored, static_cast<float>(fallback - 2u) / 6.0f);
        return std::clamp<uint32_t>(
            2u + static_cast<uint32_t>(
                std::lround(effective * 6.0f)),
            2u, 8u);
    }
    static AmbiWranglerRungMode curveRungMode(float stored)
    {
        return static_cast<AmbiWranglerRungMode>(
            std::clamp<uint32_t>(
                static_cast<uint32_t>(
                    std::lround(clamp(stored, 0.0f, 1.0f)
                        * 2.0f)),
                0u, 2u));
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
        const uint32_t voices = params_.voices;
        if (voices == 0u) return 0.0f;
        float sum = 0.0f;
        for (uint32_t voice = 0u; voice < voices; ++voice) {
            sum += voices_[voice].listenerCapture;
        }
        return sum / static_cast<float>(voices);
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
        audioStarted_ = true;
        outputChannels = std::min<uint32_t>(
            outputChannels, kAmbiWranglerMaxChannels);
        for (uint32_t ch = 0u; ch < outputChannels; ++ch) {
            if (outputs[ch]) {
                std::fill(outputs[ch], outputs[ch] + frames, 0.0f);
            }
        }

        const uint32_t voices = params_.voices;
        const uint32_t requestedAmbiChannels =
            ambiChannelsForOrder(params_.order);
        const uint32_t ambiChannels = std::min<uint32_t>(
            requestedAmbiChannels, outputChannels);
        const float targetGain = dbToGain(params_.outputGainDb)
            / std::sqrt(static_cast<float>(
                std::max<uint32_t>(1u, voices)));
        constexpr uint32_t kControlFrames = 16u;
        constexpr uint32_t kSpatialControlFrames = 64u;
        constexpr float kRenderSilence = 0.000001f;
        uint32_t frameCursor = 0u;
        while (frameCursor < frames) {
            if (controlFramesRemaining_ == 0u) {
                const float controlDt =
                    static_cast<float>(kControlFrames)
                    / static_cast<float>(sampleRate_);
                updateTopology(controlDt);
                updateMask(controlDt);
                for (uint32_t v = 0u;
                    v < processingVoiceLimit_; ++v) {
                    // maskPhase_ advances only on this clock, so the target
                    // is exactly constant throughout the following span.
                    voices_[v].targetMaskGain = voiceMask(v);
                }
                if (spatialFramesRemaining_ == 0u) {
                    const bool cacheSettleEar =
                        params_.listeningEnabled != 0u
                        && params_.listenerResponse
                            == AmbiWranglerListenerResponse::Settle
                        && params_.settleAmount > 0.0f;
                    for (uint32_t v = 0u;
                        v < processingVoiceLimit_; ++v) {
                        const Vec3 direction = directionFromAed(
                            points_[v].azimuthDeg,
                            points_[v].elevationDeg);
                        targetRenderBasis_[v] =
                            acnSn3dBasis7(direction);
                        targetRenderDistanceGain_[v] =
                            1.0f / std::max(
                                0.50f, points_[v].distance);
                        renderDistanceIncrement_[v] = (
                            targetRenderDistanceGain_[v]
                                - renderDistanceGain_[v])
                            * spatialControlStep_;
                        for (uint32_t ch = 0u;
                            ch < kAmbiWranglerMaxChannels; ++ch) {
                            renderBasisIncrement_[v][ch] = (
                                targetRenderBasis_[v][ch]
                                    - renderBasis_[v][ch])
                                * spatialControlStep_;
                        }
                        if (cacheSettleEar && v < voices) {
                            voices_[v].settleEar =
                                nearestListener(direction);
                        }
                    }
                    spatialFramesRemaining_ =
                        kSpatialControlFrames;
                }
                controlFramesRemaining_ = kControlFrames;
            }

            const uint32_t spanFrames = std::min<uint32_t>(
                controlFramesRemaining_, frames - frameCursor);
            const uint32_t spanEnd = frameCursor + spanFrames;
            for (uint32_t frame = frameCursor;
                frame < spanEnd; ++frame) {
                startupGain_ +=
                    (1.0f - startupGain_) * startupCoefficient_;
                listenerEnableGain_ += (
                    (params_.listeningEnabled ? 1.0f : 0.0f)
                        - listenerEnableGain_)
                    * listenerEnableCoefficient_;
                const bool writeResponse =
                    params_.listenerResponse
                    == AmbiWranglerListenerResponse::Write;
                returnEnableGain_ += (
                    (params_.listeningEnabled && writeResponse
                            && !params_.returnBypass ? 1.0f : 0.0f)
                    - returnEnableGain_) * returnEnableCoefficient_;
                smoothedOutputGain_ +=
                    (targetGain - smoothedOutputGain_)
                    * outputGainCoefficient_;
                for (uint32_t ch = 0u;
                    ch < processingChannelLimit_; ++ch) {
                    const float channelTarget =
                        ch < requestedAmbiChannels ? 1.0f : 0.0f;
                    channelOrderGain_[ch] += (
                        channelTarget - channelOrderGain_[ch])
                        * orderCoefficient_;
                    if (channelTarget == 1.0f
                        && channelOrderGain_[ch]
                            > 1.0f - kRenderSilence) {
                        channelOrderGain_[ch] = 1.0f;
                    }
                    if (channelTarget == 0.0f
                        && channelOrderGain_[ch] < kRenderSilence) {
                        channelOrderGain_[ch] = 0.0f;
                    }
                }
                while (processingChannelLimit_
                        > requestedAmbiChannels
                    && channelOrderGain_[
                        processingChannelLimit_ - 1u] == 0.0f) {
                    --processingChannelLimit_;
                }
                const uint32_t renderChannels =
                    std::min<uint32_t>(
                        processingChannelLimit_, outputChannels);
                const bool unityOrderGains =
                    processingChannelLimit_
                        == requestedAmbiChannels
                    && (processingChannelLimit_ == 0u
                        || channelOrderGain_[
                            processingChannelLimit_ - 1u] == 1.0f);
                std::array<float, kAmbiWranglerMaxChannels>
                    encodedFrame {};
                const bool advanceCircuits =
                    circuitFramesRemaining_ == 0u;
                if (advanceCircuits) {
                    circuitFramesRemaining_ =
                        circuitStride_;
                }
                const bool refreshExpensiveControl =
                    advanceCircuits
                    && expensiveControlFramesRemaining_ == 0u;
                if (refreshExpensiveControl) {
                    expensiveControlFramesRemaining_ = 4u;
                }
                const float circuitInterpolation =
                    circuitStride_ <= 1u ? 1.0f
                    : static_cast<float>(
                        circuitStride_
                            - circuitFramesRemaining_)
                        / static_cast<float>(circuitStride_);

                for (uint32_t v = 0u;
                    v < processingVoiceLimit_; ++v) {
                    auto& voice = voices_[v];
                    const float voiceTarget = v < voices ? 1.0f : 0.0f;
                    voice.renderGain += (voiceTarget - voice.renderGain)
                        * (voiceTarget > voice.renderGain
                                ? voiceAttackCoefficient_
                                : voiceReleaseCoefficient_);
                    if (voiceTarget == 0.0f
                        && voice.renderGain < kRenderSilence) {
                        voice.renderGain = 0.0f;
                    }
                    if (voiceTarget == 0.0f
                        && voice.renderGain == 0.0f) {
                        continue;
                    }
                    const float targetMask =
                        voice.targetMaskGain;
                    voice.maskGain += (targetMask - voice.maskGain)
                        * (targetMask > voice.maskGain
                                ? maskAttackCoefficient_
                                : maskReleaseCoefficient_);

                    // A faded-out voice freezes its complete circuit. When it
                    // is requested again it resumes under the same attack
                    // envelope, avoiding both a hard edge and the cost of
                    // running dozens of inaudible synths.
                    const uint32_t laneVoices =
                        std::max<uint32_t>(voices, v + 1u);
                    if (advanceCircuits) {
                        const float nextCircuitSample = processVoice(
                            v, laneVoices,
                            refreshExpensiveControl);
                        if (!voice.circuitOutputPrimed) {
                            voice.circuitPreviousOutput =
                                nextCircuitSample;
                            voice.circuitCurrentOutput =
                                nextCircuitSample;
                            voice.circuitOutputPrimed = true;
                        } else {
                            voice.circuitPreviousOutput =
                                voice.circuitCurrentOutput;
                            voice.circuitCurrentOutput =
                                nextCircuitSample;
                        }
                    }
                    const float circuitSample =
                        !voice.circuitOutputPrimed ? 0.0f
                        : circuitStride_ <= 1u
                        ? voice.circuitCurrentOutput
                        : lerp(
                            voice.circuitPreviousOutput,
                            voice.circuitCurrentOutput,
                            circuitInterpolation);
                    renderDistanceGain_[v] +=
                        renderDistanceIncrement_[v];
                    const float sample = circuitSample
                        * voice.maskGain * voice.renderGain
                        * smoothedOutputGain_
                        * renderDistanceGain_[v]
                        * startupGain_;
                    voice.energy +=
                        (sample * sample - voice.energy) * 0.0008f;
                    if (unityOrderGains) {
                        for (uint32_t ch = 0u;
                            ch < renderChannels; ++ch) {
                            renderBasis_[v][ch] +=
                                renderBasisIncrement_[v][ch];
                            encodedFrame[ch] +=
                                sample * renderBasis_[v][ch];
                        }
                    } else {
                        for (uint32_t ch = 0u;
                            ch < renderChannels; ++ch) {
                            renderBasis_[v][ch] +=
                                renderBasisIncrement_[v][ch];
                            encodedFrame[ch] +=
                                sample * renderBasis_[v][ch]
                                * channelOrderGain_[ch];
                        }
                    }
                }
                while (processingVoiceLimit_ > voices
                    && voices_[
                        processingVoiceLimit_ - 1u].renderGain == 0.0f) {
                    --processingVoiceLimit_;
                }

                const bool listenerNeeded =
                    params_.listeningEnabled != 0u
                    || listenerEnableGain_ > 0.00001f
                    || returnEnableGain_ > 0.00001f
                    || listenerCapture() > 0.00001f;
                if (listenerNeeded) {
                    listenerDormant_ = false;
                    for (uint32_t ch = 0u;
                        ch < ambiChannels; ++ch) {
                        listenerFrame_[ch] = outputs[ch]
                            ? encodedFrame[ch] : 0.0f;
                    }
                    fieldListener_.processFrame(
                        listenerFrame_.data(), ambiChannels);
                    writeListenerDelay();
                } else if (!listenerDormant_) {
                    fieldListener_.reset();
                    listenerDc_.fill(0.0f);
                    listenerConditioned_.fill(0.0f);
                    listenerDecision_.fill(0.0f);
                    listenerPreviousSignal_.fill(0.0f);
                    listenerRoughness_.fill(0.0f);
                    listenerTension_.fill(0.0f);
                    listenerDelayValidSamples_ = 0u;
                    listenerDormant_ = true;
                }

                for (uint32_t ch = 0u;
                    ch < renderChannels; ++ch) {
                    if (!outputs[ch]) continue;
                    outputs[ch][frame] = ambiWranglerTanh(clamp(
                        flushDenormal(encodedFrame[ch]),
                        -6.0f, 6.0f));
                }
                if (advanceCircuits) {
                    --expensiveControlFramesRemaining_;
                }
                --circuitFramesRemaining_;
                --spatialFramesRemaining_;
            }
            frameCursor = spanEnd;
            controlFramesRemaining_ -= spanFrames;
        }
    }

private:
    static constexpr float kMaximumPropagationSeconds = 0.180f;
    static constexpr uint32_t kCoefficientTableSize = 1025u;

    float onePoleCoefficient(float seconds) const
    {
        return 1.0f - std::exp(
            -1.0f / static_cast<float>(
                sampleRate_ * std::max(0.000001f, seconds)));
    }

    float circuitOnePoleCoefficient(float seconds) const
    {
        return 1.0f - std::exp(
            -1.0f / static_cast<float>(
                circuitSampleRate_
                * std::max(0.000001f, seconds)));
    }

    static float coefficientFromTable(
        const std::array<float, kCoefficientTableSize>& table,
        float normalized)
    {
        const float position = clamp(normalized, 0.0f, 1.0f)
            * static_cast<float>(kCoefficientTableSize - 1u);
        const uint32_t first = std::min<uint32_t>(
            static_cast<uint32_t>(position),
            kCoefficientTableSize - 1u);
        const uint32_t second = std::min<uint32_t>(
            first + 1u, kCoefficientTableSize - 1u);
        return lerp(table[first], table[second],
            position - static_cast<float>(first));
    }

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
        return 440.0f * std::exp2((note - 69.0f) / 12.0f);
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
        if (!std::isfinite(value)) return 0.0f;
        value = std::remainder(value, 360.0f);
        if (value <= -180.0f) value += 360.0f;
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
        updateVoiceTargets(index);
        const auto& target = voice.targets;
        voice.smoothedRateA = target.rateA;
        voice.smoothedRateB = target.rateB;
        voice.smoothedFmAtoB = target.fmAtoB;
        voice.smoothedFmBtoA = target.fmBtoA;
        voice.smoothedRunglerA = target.runglerA;
        voice.smoothedRunglerB = target.runglerB;
        voice.smoothedSpread = params_.spread;
        voice.smoothedDeviation = params_.deviation;
        voice.smoothedField = params_.field;
        voice.smoothedLane = static_cast<float>(index)
            / static_cast<float>(std::max<uint32_t>(
                1u, std::max<uint32_t>(params_.voices, index + 1u) - 1u));
        voice.smoothedRateScaleA = rateModeScale(params_.rateModeA);
        voice.smoothedRateScaleB = rateModeScale(params_.rateModeB);
        voice.smoothedPwmA = target.pwmA;
        voice.smoothedPwmB = target.pwmB;
        voice.smoothedRampA = target.rampA;
        voice.smoothedRampB = target.rampB;
        voice.smoothedThreshold = target.threshold;
        voice.smoothedColor = params_.color;
        voice.smoothedColorA = target.colorA;
        voice.smoothedColorB = target.colorB;
        voice.smoothedFilterFreqA = target.filterFreqA;
        voice.smoothedFilterFreqB = target.filterFreqB;
        voice.smoothedFilterRes = target.filterRes;
        voice.smoothedFilterComp = target.filterComp;
        voice.smoothedCrossA = target.crossA;
        voice.smoothedCrossB = target.crossB;
        voice.smoothedCrossLpf = target.crossLpf;
        voice.smoothedFilterMorph = target.filterMorph;
        voice.smoothedFilterRun = params_.filterRun;
        voice.smoothedFilterSweep = params_.filterSweep;
        voice.smoothedSaturation = params_.saturation;
        voice.smoothedSnap = params_.snap;
        voice.smoothedSnapDecay = params_.snapDecay;
        voice.smoothedAmp = target.amp;
        voice.latchedInputA = params_.inputA;
        voice.latchedInputB = params_.inputB;
        voice.latchedRungMode =
            rungModeForCurve(target.rungMode);
        voice.latchedRungSize =
            rungSizeForCurve(target.rungSize);
        // Keep the historical zero-valued DAC startup until the first clock,
        // but constrain the visible register immediately to its active size.
        voice.reg &= (1u << voice.latchedRungSize) - 1u;
    }

    void primeUnheardState()
    {
        controlFramesRemaining_ = 0u;
        spatialFramesRemaining_ = 0u;
        circuitFramesRemaining_ = 0u;
        expensiveControlFramesRemaining_ = 0u;
        startupGain_ = 0.0f;
        smoothedOutputGain_ = dbToGain(params_.outputGainDb)
            / std::sqrt(static_cast<float>(
                std::max<uint32_t>(1u, params_.voices)));
        for (uint32_t i = 0u; i < kAmbiWranglerMaxVoices; ++i) {
            voices_[i] = {};
            voices_[i].seed = 0x9e3779b9u + i * 0x85ebca6bu;
            voices_[i].reg =
                (hash(voices_[i].seed) & 0xffu) | 1u;
            initializeVoice(i);
            voices_[i].renderGain =
                i < params_.voices ? 1.0f : 0.0f;
            points_[i] = basePoint(
                i, std::max<uint32_t>(1u, params_.voices));
            targetPoints_[i] = points_[i];
            renderBasis_[i] = acnSn3dBasis7(directionFromAed(
                points_[i].azimuthDeg, points_[i].elevationDeg));
            targetRenderBasis_[i] = renderBasis_[i];
            renderDistanceGain_[i] =
                1.0f / std::max(0.50f, points_[i].distance);
            targetRenderDistanceGain_[i] =
                renderDistanceGain_[i];
        }
        const uint32_t activeChannels =
            ambiChannelsForOrder(params_.order);
        processingVoiceLimit_ = params_.voices;
        processingChannelLimit_ = activeChannels;
        for (uint32_t ch = 0u;
            ch < kAmbiWranglerMaxChannels; ++ch) {
            channelOrderGain_[ch] =
                ch < activeChannels ? 1.0f : 0.0f;
        }
    }

    void configureListener()
    {
        fieldListener_.reset();
        listenerDc_.fill(0.0f);
        listenerConditioned_.fill(0.0f);
        listenerDecision_.fill(0.0f);
        listenerPreviousSignal_.fill(0.0f);
        listenerRoughness_.fill(0.0f);
        listenerTension_.fill(0.0f);
        listenerDelayValidSamples_ = 0u;
        listenerDormant_ = true;
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
            return voice.settleEar % count;
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
        if (delaySamples > listenerDelayValidSamples_) return 0.0f;
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
            listenerConditioned_[ear] = ambiWranglerTanh(
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
        const uint32_t voices = params_.voices;
        for (uint32_t voice = 0u; voice < voices; ++voice) {
            const uint32_t ear = nearestListener(directionFromAed(
                points_[voice].azimuthDeg, points_[voice].elevationDeg));
            transitionSum[ear] += voices_[voice].transitionDensity;
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
        listenerDelayValidSamples_ = std::min<size_t>(
            listenerDelayValidSamples_ + 1u,
            listenerDelay_[0].size() - 1u);
        listenerDelayWrite_ =
            (listenerDelayWrite_ + 1u) % listenerDelay_[0].size();
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
                    voices_[i].reg & (fieldListener_.count() - 1u));
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
        const float phase = maskPhase_ + hash01(voices_[index].seed + 1009u);
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
            const float laneBias = 0.62f + 0.38f * hash01(voices_[index].seed + 1223u);
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
                + voices_[index].seed);
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

    static float breakpointRange(AmbiWranglerCurveDimension dimension)
    {
        switch (dimension) {
        case AmbiWranglerCurveDimension::RateA:
        case AmbiWranglerCurveDimension::RateB:
            return 0.85f;
        case AmbiWranglerCurveDimension::FilterFreqA:
        case AmbiWranglerCurveDimension::FilterFreqB:
        case AmbiWranglerCurveDimension::FilterRes:
        case AmbiWranglerCurveDimension::RungThresh:
        case AmbiWranglerCurveDimension::PwmA:
        case AmbiWranglerCurveDimension::PwmB:
        case AmbiWranglerCurveDimension::RampA:
        case AmbiWranglerCurveDimension::RampB:
            return 0.70f;
        case AmbiWranglerCurveDimension::FilterType:
        case AmbiWranglerCurveDimension::RungSize:
            return 1.0f;
        default:
            return 0.95f;
        }
    }

    static bool breakpointIsAbsolute(
        AmbiWranglerCurveDimension dimension)
    {
        switch (dimension) {
        case AmbiWranglerCurveDimension::ColorA:
        case AmbiWranglerCurveDimension::ColorB:
        case AmbiWranglerCurveDimension::FilterComp:
        case AmbiWranglerCurveDimension::CrossA:
        case AmbiWranglerCurveDimension::CrossB:
        case AmbiWranglerCurveDimension::CrossLpf:
        case AmbiWranglerCurveDimension::RungMode:
        case AmbiWranglerCurveDimension::Amp:
            return true;
        default:
            return false;
        }
    }

    float curveFallback(AmbiWranglerCurveDimension dimension) const
    {
        switch (dimension) {
        case AmbiWranglerCurveDimension::RateA:
            return params_.rateA;
        case AmbiWranglerCurveDimension::RungA:
            return params_.runglerA;
        case AmbiWranglerCurveDimension::FmBtoA:
            return params_.fmBtoA;
        case AmbiWranglerCurveDimension::ColorA:
            return 0.0f;
        case AmbiWranglerCurveDimension::RateB:
            return params_.rateB;
        case AmbiWranglerCurveDimension::RungB:
            return params_.runglerB;
        case AmbiWranglerCurveDimension::FmAtoB:
            return params_.fmAtoB;
        case AmbiWranglerCurveDimension::ColorB:
            return 0.0f;
        case AmbiWranglerCurveDimension::FilterFreqA:
        case AmbiWranglerCurveDimension::FilterFreqB:
            return params_.filter;
        case AmbiWranglerCurveDimension::FilterRes:
            return params_.resonance;
        case AmbiWranglerCurveDimension::FilterComp:
            return 0.0f;
        case AmbiWranglerCurveDimension::FilterType:
            return params_.filterMorph;
        case AmbiWranglerCurveDimension::CrossA:
        case AmbiWranglerCurveDimension::CrossB:
            return 0.0f;
        case AmbiWranglerCurveDimension::CrossLpf:
            return 0.25f;
        case AmbiWranglerCurveDimension::RungMode:
            return 0.0f;
        case AmbiWranglerCurveDimension::RungThresh:
            return params_.threshold;
        case AmbiWranglerCurveDimension::RungSize:
            return static_cast<float>(params_.rungSize - 2u) / 6.0f;
        case AmbiWranglerCurveDimension::PwmA:
            return params_.pwmA;
        case AmbiWranglerCurveDimension::PwmB:
            return params_.pwmB;
        case AmbiWranglerCurveDimension::RampA:
            return params_.rampA;
        case AmbiWranglerCurveDimension::RampB:
            return params_.rampB;
        case AmbiWranglerCurveDimension::Amp:
            return 1.0f;
        }
        return 0.0f;
    }

    const std::array<float, kAmbiWranglerMaxVoices>& curveValues(
        AmbiWranglerCurveDimension dimension) const
    {
        switch (dimension) {
        case AmbiWranglerCurveDimension::RateA:
            return params_.bpRateA;
        case AmbiWranglerCurveDimension::RungA:
            return params_.bpRunglerA;
        case AmbiWranglerCurveDimension::FmBtoA:
            return params_.bpFmBtoA;
        case AmbiWranglerCurveDimension::ColorA:
            return params_.bpColorA;
        case AmbiWranglerCurveDimension::RateB:
            return params_.bpRateB;
        case AmbiWranglerCurveDimension::RungB:
            return params_.bpRunglerB;
        case AmbiWranglerCurveDimension::FmAtoB:
            return params_.bpFmAtoB;
        case AmbiWranglerCurveDimension::ColorB:
            return params_.bpColorB;
        case AmbiWranglerCurveDimension::FilterFreqA:
            return params_.bpFilter;
        case AmbiWranglerCurveDimension::FilterFreqB:
            return params_.bpFilterFreqB;
        case AmbiWranglerCurveDimension::FilterRes:
            return params_.bpFilterRes;
        case AmbiWranglerCurveDimension::FilterComp:
            return params_.bpFilterComp;
        case AmbiWranglerCurveDimension::FilterType:
            return params_.bpFilterType;
        case AmbiWranglerCurveDimension::CrossA:
            return params_.bpCrossA;
        case AmbiWranglerCurveDimension::CrossB:
            return params_.bpCrossB;
        case AmbiWranglerCurveDimension::CrossLpf:
            return params_.bpCrossLpf;
        case AmbiWranglerCurveDimension::RungMode:
            return params_.bpRungMode;
        case AmbiWranglerCurveDimension::RungThresh:
            return params_.bpThreshold;
        case AmbiWranglerCurveDimension::RungSize:
            return params_.bpRungSize;
        case AmbiWranglerCurveDimension::PwmA:
            return params_.bpPwmA;
        case AmbiWranglerCurveDimension::PwmB:
            return params_.bpPwmB;
        case AmbiWranglerCurveDimension::RampA:
            return params_.bpRampA;
        case AmbiWranglerCurveDimension::RampB:
            return params_.bpRampB;
        case AmbiWranglerCurveDimension::Amp:
            return params_.bpAmp;
        }
        return params_.bpAmp;
    }

    static float centeredStructuralCurve(
        float stored, float fallback)
    {
        stored = clamp(stored, 0.0f, 1.0f);
        fallback = clamp(fallback, 0.0f, 1.0f);
        return stored < 0.5f
            ? lerp(0.0f, fallback, stored * 2.0f)
            : lerp(fallback, 1.0f, (stored - 0.5f) * 2.0f);
    }

    float laneCurveValue(
        uint32_t index, AmbiWranglerCurveDimension dimension) const
    {
        const float fallback = curveFallback(dimension);
        if (!params_.voiceBreakpointsEnabled) return fallback;
        const float value = curveValues(dimension)[std::min<uint32_t>(
            index, kAmbiWranglerMaxVoices - 1u)];
        if (breakpointIsAbsolute(dimension)) return value;
        if (dimension == AmbiWranglerCurveDimension::FilterType) {
            return curveFilterMorph(value, params_.filterMorph);
        }
        if (dimension == AmbiWranglerCurveDimension::RungSize) {
            return static_cast<float>(
                curveRungSize(value, params_.rungSize) - 2u) / 6.0f;
        }
        return clamp(
            fallback + (value - 0.5f) * breakpointRange(dimension),
            0.0f, 1.0f);
    }

    void updateVoiceTargets(uint32_t index)
    {
        auto& target = voices_[index].targets;
        target.rateA =
            laneCurveValue(index, AmbiWranglerCurveDimension::RateA);
        target.rateB =
            laneCurveValue(index, AmbiWranglerCurveDimension::RateB);
        target.fmAtoB =
            laneCurveValue(index, AmbiWranglerCurveDimension::FmAtoB);
        target.fmBtoA =
            laneCurveValue(index, AmbiWranglerCurveDimension::FmBtoA);
        target.runglerA =
            laneCurveValue(index, AmbiWranglerCurveDimension::RungA);
        target.runglerB =
            laneCurveValue(index, AmbiWranglerCurveDimension::RungB);
        target.filterFreqA = laneCurveValue(
            index, AmbiWranglerCurveDimension::FilterFreqA);
        target.filterFreqB = laneCurveValue(
            index, AmbiWranglerCurveDimension::FilterFreqB);
        target.filterRes =
            laneCurveValue(index, AmbiWranglerCurveDimension::FilterRes);
        target.filterComp = laneCurveValue(
            index, AmbiWranglerCurveDimension::FilterComp);
        target.filterMorph = laneCurveValue(
            index, AmbiWranglerCurveDimension::FilterType);
        target.colorA =
            laneCurveValue(index, AmbiWranglerCurveDimension::ColorA);
        target.colorB =
            laneCurveValue(index, AmbiWranglerCurveDimension::ColorB);
        target.crossA =
            laneCurveValue(index, AmbiWranglerCurveDimension::CrossA);
        target.crossB =
            laneCurveValue(index, AmbiWranglerCurveDimension::CrossB);
        target.crossLpf =
            laneCurveValue(index, AmbiWranglerCurveDimension::CrossLpf);
        target.rungMode =
            laneCurveValue(index, AmbiWranglerCurveDimension::RungMode);
        target.rungSize =
            laneCurveValue(index, AmbiWranglerCurveDimension::RungSize);
        target.threshold = laneCurveValue(
            index, AmbiWranglerCurveDimension::RungThresh);
        target.pwmA =
            laneCurveValue(index, AmbiWranglerCurveDimension::PwmA);
        target.pwmB =
            laneCurveValue(index, AmbiWranglerCurveDimension::PwmB);
        target.rampA =
            laneCurveValue(index, AmbiWranglerCurveDimension::RampA);
        target.rampB =
            laneCurveValue(index, AmbiWranglerCurveDimension::RampB);
        target.amp =
            laneCurveValue(index, AmbiWranglerCurveDimension::Amp);
    }

    static uint32_t rungModeForCurve(float value)
    {
        return static_cast<uint32_t>(curveRungMode(value));
    }

    static uint32_t rungSizeForCurve(float value)
    {
        return std::clamp<uint32_t>(
            2u + static_cast<uint32_t>(
                std::lround(clamp(value, 0.0f, 1.0f) * 6.0f)),
            2u, 8u);
    }

    static float splitRegisterValue(
        uint32_t reg, uint32_t size, uint32_t parity)
    {
        parity &= 1u;
        uint32_t packed = 0u;
        uint32_t count = 0u;
        for (uint32_t bit = parity; bit < size; bit += 2u) {
            packed |= ((reg >> bit) & 1u) << count;
            ++count;
        }
        const uint32_t mask = count > 0u ? (1u << count) - 1u : 1u;
        return static_cast<float>(packed)
            / static_cast<float>(mask) * 2.0f - 1.0f;
    }

    static void refreshRegisterViews(AmbiWranglerVoice& voice)
    {
        const uint32_t mask =
            (1u << voice.latchedRungSize) - 1u;
        voice.reg &= mask;
        const float denom = static_cast<float>(
            std::max<uint32_t>(1u, mask));
        voice.rungValue =
            static_cast<float>(voice.reg) / denom * 2.0f - 1.0f;
        voice.rungSplitA = splitRegisterValue(
            voice.reg, voice.latchedRungSize, 0u);
        voice.rungSplitB = splitRegisterValue(
            voice.reg, voice.latchedRungSize, 1u);
    }

    static float nonlinearOscillatorFeedback(
        float input, float previous, float color, float compensation)
    {
        color = clamp(color, 0.0f, 1.0f);
        if (color <= 0.000001f) return input;
        const float feedback = ambiWranglerTanh(previous * 1.8f)
            * color * 0.82f * compensation;
        const float drive = 1.0f + color * 2.6f;
        const float shaped = ambiWranglerTanh(clamp(
            (input + feedback) * drive, -6.0f, 6.0f))
            / ambiWranglerTanh(drive);
        return lerp(input, shaped, color);
    }

    static float selectedFilterOutput(
        const AmbiWranglerFilter::Outputs& outputs,
        const AmbiWranglerVoice& voice,
        bool legacyCompatibleLow)
    {
        const float low =
            legacyCompatibleLow ? outputs.legacy : outputs.low;
        const float morph =
            clamp(voice.smoothedFilterMorph, 0.0f, 1.0f);
        const float side = std::fabs(morph * 2.0f - 1.0f);
        const float depth =
            side * side * (3.0f - side * 2.0f);
        if (depth <= 0.000001f) return outputs.open;
        const float wing = ambiWranglerTanh(clamp(
            morph < 0.5f ? low : outputs.high,
            -6.0f, 6.0f));
        return lerp(outputs.open, wing, depth);
    }

    float finishVoiceOutput(
        AmbiWranglerVoice& voice, float current)
    {
        current = flushDenormal(current);
        if (voice.outputTransitionPending) {
            voice.outputTransitionCorrection =
                voice.lastOutput - current;
            voice.outputTransitionPending = false;
        }
        const float result = flushDenormal(
            current + voice.outputTransitionCorrection);
        voice.outputTransitionCorrection = flushDenormal(
            voice.outputTransitionCorrection * outputTransitionDecay_);
        voice.lastOutput = result;
        return result;
    }

    static void rungStreams(
        const AmbiWranglerVoice& voice, float& forA, float& forB)
    {
        if (voice.latchedRungMode
            == static_cast<uint32_t>(AmbiWranglerRungMode::Split)) {
            forA = voice.rungSplitSmoothA;
            forB = voice.rungSplitSmoothB;
        } else if (voice.latchedRungMode
            == static_cast<uint32_t>(AmbiWranglerRungMode::Swap)) {
            forA = voice.rungSplitSmoothB;
            forB = voice.rungSplitSmoothA;
        } else {
            forA = voice.rungSmooth;
            forB = voice.rungSmooth;
        }
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
        return ambiWranglerTanh((ramp - threshold) / edge);
    }

    float processVoice(
        uint32_t index, uint32_t activeVoices,
        bool refreshExpensiveControl)
    {
        auto& voice = voices_[index];
        const bool bounded =
            params_.circuitLaw == AmbiWranglerCircuitLaw::Bounded;
        const bool settleResponse =
            params_.listenerResponse == AmbiWranglerListenerResponse::Settle;
        const float laneTarget = static_cast<float>(index)
            / static_cast<float>(std::max<uint32_t>(1u, activeVoices - 1u));
        const float fieldTarget = params_.field;
        voice.transitionDensity *= transitionDecay_;
        float desiredCapture = 0.0f;
        if (settleResponse && params_.listeningEnabled != 0u
            && params_.settleAmount > 0.0f) {
            const uint32_t settleEar =
                voice.settleEar % std::max<uint32_t>(
                    1u, fieldListener_.count());
            const float excess = clamp(
                (listenerTension_[settleEar] - params_.settleTarget)
                    / std::max(0.05f, 1.0f - params_.settleTarget),
                0.0f, 1.0f);
            const float shapedExcess =
                excess * excess * (3.0f - 2.0f * excess);
            const float activityGate = clamp(
                (fieldListener_.activity() - 0.01f) / 0.09f,
                0.0f, 1.0f);
            desiredCapture =
                shapedExcess * params_.settleAmount
                * listenerEnableGain_ * activityGate;
        }
        const float captureCoefficient =
            desiredCapture > voice.listenerCapture
            ? captureAttackCoefficient_
            : captureReleaseCoefficient_;
        voice.listenerCapture += (
            desiredCapture - voice.listenerCapture)
            * captureCoefficient;
        voice.listenerCapture =
            flushDenormal(voice.listenerCapture);
        if (voice.listenerCapture <= 0.000001f
            && desiredCapture <= 0.000001f) {
            voice.listenerCapture = 0.0f;
            voice.listenerEvolutionAccumulator = 0.0f;
            voice.registerHeld = 0u;
        }
        const auto& target = voice.targets;
        const float rateATarget = target.rateA;
        const float rateBTarget = target.rateB;
        const float fmAtoBTarget = target.fmAtoB;
        const float fmBtoATarget = target.fmBtoA;
        const float runglerATarget = target.runglerA;
        const float runglerBTarget = target.runglerB;
        const float filterFreqATarget = target.filterFreqA;
        const float filterFreqBTarget = target.filterFreqB;
        const float filterResTarget = target.filterRes;
        const float filterCompTarget = target.filterComp;
        const float filterTypeTarget = target.filterMorph;
        const float colorATarget = target.colorA;
        const float colorBTarget = target.colorB;
        const float crossATarget = target.crossA;
        const float crossBTarget = target.crossB;
        const float crossLpfTarget = target.crossLpf;
        const float rungModeTarget = target.rungMode;
        const float rungSizeTarget = target.rungSize;
        const float thresholdTarget = target.threshold;
        const float pwmATarget = target.pwmA;
        const float pwmBTarget = target.pwmB;
        const float rampATarget = target.rampA;
        const float rampBTarget = target.rampB;
        const float ampTarget = target.amp;
        const float edgeCoefficient = edgeCoefficient_;
        const float shapeCoefficient = shapeCoefficient_;
        const float colorCoefficient = colorCoefficient_;
        const float feedbackCoefficient = feedbackCoefficient_;
        const float ampCoefficient = ampCoefficient_;
        voice.smoothedRateA +=
            (rateATarget - voice.smoothedRateA) * shapeCoefficient;
        voice.smoothedRateB +=
            (rateBTarget - voice.smoothedRateB) * shapeCoefficient;
        voice.smoothedFmAtoB +=
            (fmAtoBTarget - voice.smoothedFmAtoB) * shapeCoefficient;
        voice.smoothedFmBtoA +=
            (fmBtoATarget - voice.smoothedFmBtoA) * shapeCoefficient;
        voice.smoothedRunglerA +=
            (runglerATarget - voice.smoothedRunglerA)
                * shapeCoefficient;
        voice.smoothedRunglerB +=
            (runglerBTarget - voice.smoothedRunglerB)
                * shapeCoefficient;
        voice.smoothedSpread +=
            (params_.spread - voice.smoothedSpread) * shapeCoefficient;
        voice.smoothedDeviation +=
            (params_.deviation - voice.smoothedDeviation)
                * shapeCoefficient;
        voice.smoothedField +=
            (fieldTarget - voice.smoothedField) * shapeCoefficient;
        voice.smoothedLane +=
            (laneTarget - voice.smoothedLane) * shapeCoefficient;
        voice.smoothedRateScaleA += (
            rateModeScale(params_.rateModeA)
                - voice.smoothedRateScaleA) * shapeCoefficient;
        voice.smoothedRateScaleB += (
            rateModeScale(params_.rateModeB)
                - voice.smoothedRateScaleB) * shapeCoefficient;
        voice.smoothedPwmA +=
            (pwmATarget - voice.smoothedPwmA) * edgeCoefficient;
        voice.smoothedPwmB +=
            (pwmBTarget - voice.smoothedPwmB) * edgeCoefficient;
        voice.smoothedThreshold +=
            (thresholdTarget - voice.smoothedThreshold) * edgeCoefficient;
        voice.smoothedRampA +=
            (rampATarget - voice.smoothedRampA) * shapeCoefficient;
        voice.smoothedRampB +=
            (rampBTarget - voice.smoothedRampB) * shapeCoefficient;
        voice.smoothedColor +=
            (params_.color - voice.smoothedColor) * colorCoefficient;
        voice.smoothedColorA +=
            (colorATarget - voice.smoothedColorA) * feedbackCoefficient;
        voice.smoothedColorB +=
            (colorBTarget - voice.smoothedColorB) * feedbackCoefficient;
        voice.smoothedFilterFreqA +=
            (filterFreqATarget - voice.smoothedFilterFreqA)
                * shapeCoefficient;
        voice.smoothedFilterFreqB +=
            (filterFreqBTarget - voice.smoothedFilterFreqB)
                * shapeCoefficient;
        voice.smoothedFilterRes +=
            (filterResTarget - voice.smoothedFilterRes)
                * colorCoefficient;
        voice.smoothedFilterComp +=
            (filterCompTarget - voice.smoothedFilterComp)
                * feedbackCoefficient;
        voice.smoothedCrossA +=
            (crossATarget - voice.smoothedCrossA) * feedbackCoefficient;
        voice.smoothedCrossB +=
            (crossBTarget - voice.smoothedCrossB) * feedbackCoefficient;
        voice.smoothedCrossLpf +=
            (crossLpfTarget - voice.smoothedCrossLpf)
                * feedbackCoefficient;
        voice.smoothedFilterMorph +=
            (filterTypeTarget - voice.smoothedFilterMorph)
                * colorCoefficient;
        voice.smoothedFilterRun +=
            (params_.filterRun - voice.smoothedFilterRun)
                * shapeCoefficient;
        voice.smoothedFilterSweep +=
            (params_.filterSweep - voice.smoothedFilterSweep)
                * shapeCoefficient;
        voice.smoothedSaturation +=
            (params_.saturation - voice.smoothedSaturation)
                * colorCoefficient;
        voice.smoothedSnap +=
            (params_.snap - voice.smoothedSnap) * edgeCoefficient;
        voice.smoothedSnapDecay +=
            (params_.snapDecay - voice.smoothedSnapDecay)
                * shapeCoefficient;
        voice.smoothedAmp +=
            (ampTarget - voice.smoothedAmp) * ampCoefficient;
        const float rateAParam = voice.smoothedRateA;
        const float rateBParam = voice.smoothedRateB;
        const float fmAtoBParam = voice.smoothedFmAtoB;
        const float fmBtoAParam = voice.smoothedFmBtoA;
        const float runglerAParam = voice.smoothedRunglerA;
        const float runglerBParam = voice.smoothedRunglerB;
        const float field = voice.smoothedField;
        const float lane = voice.smoothedLane;
        const float thresholdParam = voice.smoothedThreshold;
        const float pwmAParam = voice.smoothedPwmA;
        const float pwmBParam = voice.smoothedPwmB;
        const float rampAParam = voice.smoothedRampA;
        const float rampBParam = voice.smoothedRampB;
        if (refreshExpensiveControl) {
            voice.cachedCrossCoefficient = coefficientFromTable(
                crossCoefficientTable_, voice.smoothedCrossLpf);
            const float feedbackLoad =
                (voice.smoothedColorA + voice.smoothedColorB
                    + voice.smoothedCrossA + voice.smoothedCrossB)
                * 0.5f;
            const float compensationDenominator =
                1.0f
                + voice.smoothedFilterComp
                    * feedbackLoad * 1.4f;
            voice.cachedFeedbackCompensation =
                1.0f / compensationDenominator;
            voice.cachedFilterInputCompensation =
                (1.0f + voice.smoothedFilterComp)
                * voice.cachedFeedbackCompensation;
            voice.cachedOutputCompensation =
                std::min(
                    2.0f, std::sqrt(compensationDenominator))
                / (1.0f + voice.smoothedFilterComp);
            voice.cachedListenerCoupling = coefficientFromTable(
                listenerCouplingTable_, voice.listenerCapture);
            voice.cachedSnapDecay = coefficientFromTable(
                snapDecayTable_, voice.smoothedSnapDecay);
        }
        const float crossCoefficient =
            voice.cachedCrossCoefficient;
        // The one-sample history makes the feedback loop causal: Cross A is
        // filter A returning to oscillator B, while Cross B is filter B
        // returning to oscillator A. Cross LPF controls the shared memory
        // without altering either filter's audio tap.
        voice.crossFeedbackA +=
            (voice.previousFilteredB - voice.crossFeedbackA)
                * crossCoefficient;
        voice.crossFeedbackB +=
            (voice.previousFilteredA - voice.crossFeedbackB)
                * crossCoefficient;
        const float feedbackCompensation =
            voice.cachedFeedbackCompensation;
        // The historical compensation control also acts with no Color/Cross
        // feedback: it drives the filter network and normalizes that drive at
        // the output. The feedback-dependent denominator adds restraint as
        // recursive paths are introduced, while a value of zero is exact
        // identity at zero.
        const float filterInputCompensation =
            voice.cachedFilterInputCompensation;
        const float outputCompensation =
            voice.cachedOutputCompensation;
        const float spreadA = (lane - 0.5f) * voice.smoothedSpread
            * (1.4f + field * 1.6f);
        const float spreadB = (0.5f - lane) * voice.smoothedSpread
            * (1.2f + field * 1.4f);
        const float devA = hashSigned(voice.seed + 101u)
            * voice.smoothedDeviation * (0.35f + field * 0.85f);
        const float devB = hashSigned(voice.seed + 211u)
            * voice.smoothedDeviation * (0.35f + field * 0.85f);
        const float rateScaleA = voice.smoothedRateScaleA;
        const float rateScaleB = voice.smoothedRateScaleB;
        float baseA = 0.0f;
        float baseB = 0.0f;
        float laneFmA = 0.0f;
        float laneFmB = 0.0f;
        float laneRunA = 0.0f;
        float laneRunB = 0.0f;
        float hzA = 0.0f;
        float hzB = 0.0f;
        float rungForA = 0.0f;
        float rungForB = 0.0f;
        float rungTargetA = 0.0f;
        float rungTargetB = 0.0f;
        rungStreams(voice, rungTargetA, rungTargetB);
        voice.smoothedRungOutputA += (
            rungTargetA - voice.smoothedRungOutputA)
            * shapeCoefficient;
        voice.smoothedRungOutputB += (
            rungTargetB - voice.smoothedRungOutputB)
            * shapeCoefficient;
        rungForA = voice.smoothedRungOutputA;
        rungForB = voice.smoothedRungOutputB;
        // Curve interpolation and audible smoothing still run per sample.
        // Only the static pitch/filter transforms are cached on a persistent
        // four-sample clock; re-arming that clock in setParams preserves
        // sample-accurate event boundaries.
        if (refreshExpensiveControl) {
            if (!bounded) {
                voice.cachedBaseA = targetNormToHz(clamp(
                    rateAParam
                        + hashSigned(voice.seed + 307u) * field
                            * voice.smoothedDeviation * 0.42f,
                    0.0f, 1.0f))
                    * rateScaleA
                    * std::exp2(spreadA + devA)
                    * voice.rateA;
                voice.cachedBaseB = targetNormToHz(clamp(
                    rateBParam
                        + hashSigned(voice.seed + 401u) * field
                            * voice.smoothedDeviation * 0.42f,
                    0.0f, 1.0f))
                    * rateScaleB
                    * std::exp2(spreadB + devB)
                    * voice.rateB;
                voice.cachedLaneFmA = clamp(
                    fmBtoAParam
                        + hashSigned(voice.seed + 503u) * field
                            * voice.smoothedDeviation * 0.55f,
                    0.0f, 1.0f);
                voice.cachedLaneFmB = clamp(
                    fmAtoBParam
                        + hashSigned(voice.seed + 601u) * field
                            * voice.smoothedDeviation * 0.55f,
                    0.0f, 1.0f);
                voice.cachedLaneRunA = clamp(
                    runglerAParam
                        + hashSigned(voice.seed + 701u) * field
                            * voice.smoothedDeviation * 0.65f,
                    0.0f, 1.0f);
                voice.cachedLaneRunB = clamp(
                    runglerBParam
                        + hashSigned(voice.seed + 809u) * field
                            * voice.smoothedDeviation * 0.65f,
                    0.0f, 1.0f);
                voice.cachedLegacyFmHzA =
                    targetNormToHz(voice.cachedLaneFmA);
                voice.cachedLegacyFmHzB =
                    targetNormToHz(voice.cachedLaneFmB);
                voice.cachedLegacyRungHzA =
                    targetNormToHz(voice.cachedLaneRunA);
                voice.cachedLegacyRungHzB =
                    targetNormToHz(voice.cachedLaneRunB);
            } else {
                voice.cachedBaseA = targetNormToHz(rateAParam)
                    * rateScaleA
                    * std::exp2(
                        spreadA * 0.65f + devA * 0.45f)
                    * voice.rateA;
                voice.cachedBaseB = targetNormToHz(rateBParam)
                    * rateScaleB
                    * std::exp2(
                        spreadB * 0.65f + devB * 0.45f)
                    * voice.rateB;
                voice.cachedLaneFmA =
                    fmBtoAParam <= 0.000001f
                    ? 0.0f
                    : clamp(
                        fmBtoAParam
                            * (1.0f
                                + hashSigned(voice.seed + 503u)
                                    * field
                                    * voice.smoothedDeviation
                                    * 0.55f),
                        0.0f, 1.0f);
                voice.cachedLaneFmB =
                    fmAtoBParam <= 0.000001f
                    ? 0.0f
                    : clamp(
                        fmAtoBParam
                            * (1.0f
                                + hashSigned(voice.seed + 601u)
                                    * field
                                    * voice.smoothedDeviation
                                    * 0.55f),
                        0.0f, 1.0f);
                voice.cachedLaneRunA =
                    runglerAParam <= 0.000001f
                    ? 0.0f
                    : clamp(
                        runglerAParam
                            * (1.0f
                                + hashSigned(voice.seed + 701u)
                                    * field
                                    * voice.smoothedDeviation
                                    * 0.65f),
                        0.0f, 1.0f);
                voice.cachedLaneRunB =
                    runglerBParam <= 0.000001f
                    ? 0.0f
                    : clamp(
                        runglerBParam
                            * (1.0f
                                + hashSigned(voice.seed + 809u)
                                    * field
                                    * voice.smoothedDeviation
                                    * 0.65f),
                        0.0f, 1.0f);
            }
            voice.cachedFilterCurveA =
                std::pow(voice.smoothedFilterFreqA, 2.2f);
            voice.cachedFilterCurveB =
                std::pow(voice.smoothedFilterFreqB, 2.2f);
        }
        baseA = voice.cachedBaseA;
        baseB = voice.cachedBaseB;
        laneFmA = voice.cachedLaneFmA;
        laneFmB = voice.cachedLaneFmB;
        laneRunA = voice.cachedLaneRunA;
        laneRunB = voice.cachedLaneRunB;
        if (!bounded) {
            const float couplingRate =
                voice.cachedListenerCoupling;
            const float fmToA =
                phaseTri(voice.phaseB) * voice.cachedLegacyFmHzA
                    * couplingRate;
            const float fmToB =
                phaseTri(voice.phaseA) * voice.cachedLegacyFmHzB
                    * couplingRate;
            const float rungToA =
                rungForA * voice.cachedLegacyRungHzA
                    * couplingRate;
            const float rungToB =
                rungForB * voice.cachedLegacyRungHzB
                    * couplingRate;
            hzA = clampOscHz(
                baseA + fmToA + rungToA,
                static_cast<float>(circuitSampleRate_));
            hzB = clampOscHz(
                baseB + fmToB + rungToB,
                static_cast<float>(circuitSampleRate_));
        } else {
            // In Bounded law modulation is a depth around a positive base,
            // never a second absolute-frequency oscillator.
            const float couplingRate =
                voice.cachedListenerCoupling;
            const float pitchA = clamp(
                phaseTri(voice.phaseB) * 18.0f * laneFmA * laneFmA
                    + rungForA * 24.0f * laneRunA * laneRunA,
                -36.0f, 36.0f) * couplingRate;
            const float pitchB = clamp(
                phaseTri(voice.phaseA) * 18.0f * laneFmB * laneFmB
                    + rungForB * 24.0f * laneRunB * laneRunB,
                -36.0f, 36.0f) * couplingRate;
            hzA = clampPositiveOscHz(
                baseA * std::exp2(pitchA / 12.0f),
                static_cast<float>(circuitSampleRate_));
            hzB = clampPositiveOscHz(
                baseB * std::exp2(pitchB / 12.0f),
                static_cast<float>(circuitSampleRate_));
        }
        if (voice.smoothedCrossB > 0.000001f) {
            const float crossPitchA = ambiWranglerTanh(
                voice.crossFeedbackA * 1.6f)
                * voice.smoothedCrossB * 18.0f
                * feedbackCompensation;
            hzA *= std::exp2(crossPitchA / 12.0f);
        }
        if (voice.smoothedCrossA > 0.000001f) {
            const float crossPitchB = ambiWranglerTanh(
                voice.crossFeedbackB * 1.6f)
                * voice.smoothedCrossA * 18.0f
                * feedbackCompensation;
            hzB *= std::exp2(crossPitchB / 12.0f);
        }
        hzA = bounded
            ? clampPositiveOscHz(
                hzA, static_cast<float>(circuitSampleRate_))
            : clampOscHz(
                hzA, static_cast<float>(circuitSampleRate_));
        hzB = bounded
            ? clampPositiveOscHz(
                hzB, static_cast<float>(circuitSampleRate_))
            : clampOscHz(
                hzB, static_cast<float>(circuitSampleRate_));
        voice.phaseA +=
            hzA / static_cast<float>(circuitSampleRate_);
        voice.phaseB +=
            hzB / static_cast<float>(circuitSampleRate_);
        voice.phaseA -= std::floor(voice.phaseA);
        voice.phaseB -= std::floor(voice.phaseB);

        const float rawTriA =
            shapedRamp(voice.phaseA, rampAParam);
        const float rawTriB =
            shapedRamp(voice.phaseB, rampBParam);
        const float triA = nonlinearOscillatorFeedback(
            rawTriA, voice.oscillatorFeedbackA,
            voice.smoothedColorA, feedbackCompensation);
        const float triB = nonlinearOscillatorFeedback(
            rawTriB, voice.oscillatorFeedbackB,
            voice.smoothedColorB, feedbackCompensation);
        voice.oscillatorFeedbackA = flushDenormal(triA);
        voice.oscillatorFeedbackB = flushDenormal(triB);
        const float widthA = clamp(pwmAParam + (thresholdParam - 0.5f) * 0.42f, 0.03f, 0.97f);
        const float widthB = clamp(pwmBParam - (thresholdParam - 0.5f) * 0.42f, 0.03f, 0.97f);
        const float squareA = smoothPulse(
            triA, widthA, std::fabs(hzA),
            static_cast<float>(circuitSampleRate_));
        const float squareB = smoothPulse(
            triB, widthB, std::fabs(hzB),
            static_cast<float>(circuitSampleRate_));
        const float oldClockInput =
            voice.latchedInputB == 0u ? squareB : triB;
        const float requestedClockInput =
            params_.inputB == 0u ? squareB : triB;
        if (voice.latchedInputB != params_.inputB
            && (oldClockInput > 0.0f)
                == (requestedClockInput > 0.0f)) {
            // Changing the clock outlet while its logic level differs would
            // manufacture a register edge. Wait for a shared logic region.
            voice.latchedInputB = params_.inputB;
        }
        const float clockInput =
            voice.latchedInputB == 0u ? squareB : triB;
        const bool clock = clockInput > 0.0f && voice.lastClock <= 0.0f;
        voice.lastClock = clockInput;
        voice.fieldWritePulse *= 0.992f;
        if (clock) {
            ++voice.clockCount;
            // Register topology is structural: per-voice mode and size only
            // commit on the circuit's own clock edge, never midway through a
            // bit decision.
            const uint32_t previousRungMode =
                voice.latchedRungMode;
            const uint32_t previousRungSize =
                voice.latchedRungSize;
            voice.latchedRungMode =
                rungModeForCurve(rungModeTarget);
            voice.latchedRungSize =
                rungSizeForCurve(rungSizeTarget);
            const bool structureChanged =
                voice.latchedRungMode != previousRungMode
                || voice.latchedRungSize != previousRungSize;
            // A held Settle clock can still commit a structural edit. Refresh
            // all register views after masking so a new size or mode never
            // selects stale values from the former structure. The audible
            // register streams are already slewed below, so adding an output
            // correction here would cancel an ordinary waveform delta and
            // create a false transient of its own.
            refreshRegisterViews(voice);
            const uint32_t ear = listenerEarForClock(index, index);
            voice.lastReadEar = ear;
            const auto decision = auditoryDecision(ear, voice.auditoryBit);
            voice.auditoryBit = decision.bit;
            const bool writeResponse =
                params_.listenerResponse == AmbiWranglerListenerResponse::Write;
            voice.lastReturn = writeResponse
                ? delayedListenerSignal(ear) * params_.fieldReturn
                    * params_.fieldReturn * returnEnableGain_
                : 0.0f;
            // Comparator outlet changes are only consumed by the register,
            // so commit them at the genuine clock boundary.
            voice.latchedInputA = params_.inputA;
            const float nativeComparator = voice.latchedInputA == 0u
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
                const uint32_t mask =
                    (1u << voice.latchedRungSize) - 1u;
                const uint32_t loopBit =
                    (voice.reg
                        >> (voice.latchedRungSize - 1u)) & 1u;
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
                refreshRegisterViews(voice);
                const float edgeDelta =
                    voice.rungValue - voice.rungSmooth;
                voice.snapPolarity = edgeDelta == 0.0f
                    ? (bit ? 1.0f : -1.0f)
                    : (edgeDelta > 0.0f ? 1.0f : -1.0f);
                if (!structureChanged) {
                    voice.snapEnv = std::min(
                        1.0f,
                        voice.snapEnv
                            + (0.38f
                                + std::fabs(edgeDelta) * 0.62f));
                }
            }
        }
        const float rungAmount =
            std::max(laneRunA, laneRunB);
        if (!bounded) {
            const float coefficient =
                0.002f + rungAmount * 0.030f;
            voice.rungSmooth +=
                (voice.rungValue - voice.rungSmooth) * coefficient;
            voice.rungSplitSmoothA +=
                (voice.rungSplitA - voice.rungSplitSmoothA)
                    * coefficient;
            voice.rungSplitSmoothB +=
                (voice.rungSplitB - voice.rungSplitSmoothB)
                    * coefficient;
        } else {
            const float slewCoefficient = coefficientFromTable(
                rungCoefficientTable_, rungAmount);
            voice.rungSmooth += (
                voice.rungValue - voice.rungSmooth) * slewCoefficient;
            voice.rungSplitSmoothA += (
                voice.rungSplitA - voice.rungSplitSmoothA)
                * slewCoefficient;
            voice.rungSplitSmoothB += (
                voice.rungSplitB - voice.rungSplitSmoothB)
                * slewCoefficient;
        }
        const float rungForFilter =
            (rungForA + rungForB) * 0.5f;
        const float rungDiscrete = rungForFilter;
        const float snapDecayCoeff =
            voice.cachedSnapDecay;
        const float snapTransient =
            voice.snapEnv * voice.snapPolarity * voice.smoothedSnap;
        voice.snapEnv *= snapDecayCoeff;

        if (!bounded) {
            const float sweep = (triB * 0.5f + 0.5f)
                * voice.smoothedFilterSweep * 0.11f;
            const float run = (rungForFilter * 0.5f + 0.5f)
                * voice.smoothedFilterRun * 0.14f;
            const float filterNormA = 0.0015f
                + voice.cachedFilterCurveA * 0.20f + sweep + run;
            const float filterNormB = (0.0015f
                + voice.cachedFilterCurveB * 0.20f
                    + sweep + run) * 1.17f;
            const float rungAudio =
                rungDiscrete * (0.10f + rungAmount * 0.18f);
            const float pwm = squareA * 0.55f + squareB * 0.45f
                + (triA - triB) * 0.25f + rungAudio;
            const float sourceA = (
                pwm + squareB * laneRunA * 0.30f
                    + snapTransient * 0.46f)
                * filterInputCompensation;
            const float sourceB = (
                pwm + squareA * laneRunB * 0.30f
                    - snapTransient * 0.32f)
                * filterInputCompensation;
            const auto filteredA = voice.filterA.processSvf(
                sourceA, filterNormA,
                voice.smoothedFilterRes, voice.smoothedSaturation);
            const auto filteredB = voice.filterB.processSvf(
                sourceB, filterNormB,
                voice.smoothedFilterRes, voice.smoothedSaturation);
            // The Legacy LP position is its historical low-plus-band tap;
            // BP and HP are the true SVF outlets. The selected tap itself has
            // no dry bypass, while the final Color blend below deliberately
            // retains Legacy's original direct oscillator/register mixture.
            const float filtA = selectedFilterOutput(
                filteredA, voice, true) * outputCompensation;
            const float filtB = selectedFilterOutput(
                filteredB, voice, true) * outputCompensation;
            voice.previousFilteredA = flushDenormal(filtA);
            voice.previousFilteredB = flushDenormal(filtB);
            const float edge = (squareA + squareB) * 0.24f;
            const float body = (triA + triB) * 0.32f;
            const float bloom = (filtA + filtB) * 0.48f;
            const float mixed =
                lerp(edge + body, bloom + body * 0.24f,
                    voice.smoothedColor);
            const float click = snapTransient
                * (0.10f + (1.0f - voice.smoothedColor) * 0.22f);
            const float output = ambiWranglerTanh(clamp(
                (mixed + click)
                    * (1.0f + voice.smoothedSaturation * 3.2f),
                -7.0f, 7.0f)) * voice.smoothedAmp;
            return finishVoiceOutput(voice, output);
        }

        // The bounded source follows the characteristic Benjolin relation:
        // audio PWM is the comparison of the two triangle oscillators. The
        // individual pulse outputs remain available to clock and perturb the
        // register, but no sub-audio square is mixed directly into the body.
        const float comparatorEdge = clamp(
            (std::fabs(hzA) + std::fabs(hzB))
                / static_cast<float>(
                    circuitSampleRate_) * 12.0f,
            0.002f, 0.18f);
        const float comparatorBias = (thresholdParam - 0.5f) * 0.90f
            + (pwmAParam - pwmBParam) * 0.45f;
        const float classicPwm = ambiWranglerTanh(
            (triA - triB - comparatorBias) / comparatorEdge);
        const float sweep = (triB * 0.5f + 0.5f)
            * voice.smoothedFilterSweep * 0.085f;
        const float run = (rungForFilter * 0.5f + 0.5f)
            * voice.smoothedFilterRun * 0.105f;
        const float filterNormA = 0.0015f
            + voice.cachedFilterCurveA * 0.20f + sweep + run;
        const float filterNormB = 0.0015f
            + voice.cachedFilterCurveB * 0.20f + sweep + run;
        const float rungSource =
            rungForFilter * rungAmount * 0.48f;
        const float source = (
            classicPwm * 0.82f + rungSource
                + snapTransient * 0.24f)
            * filterInputCompensation;
        // Color now pushes the filter's nonlinear input/Z trajectory; it no
        // longer opens an unfiltered path around the selected outlet.
        const float filterDrive =
            voice.smoothedColor * voice.smoothedColor * 0.90f
            + voice.smoothedSaturation * 0.35f;
        const auto filteredA = voice.filterA.processSvf(
            source, filterNormA,
            voice.smoothedFilterRes, filterDrive);
        const auto filteredB = voice.filterB.processSvf(
            source, filterNormB,
            voice.smoothedFilterRes, filterDrive);
        const float selectedA = selectedFilterOutput(
            filteredA, voice, false);
        const float selectedB = selectedFilterOutput(
            filteredB, voice, false);
        voice.previousFilteredA =
            flushDenormal(selectedA * outputCompensation);
        voice.previousFilteredB =
            flushDenormal(selectedB * outputCompensation);
        const float selected =
            (voice.previousFilteredA + voice.previousFilteredB) * 0.5f;
        const float output = ambiWranglerTanh(clamp(
            selected
                * (1.0f + voice.smoothedSaturation * 1.7f),
            -7.0f, 7.0f)) * voice.smoothedAmp;
        return finishVoiceOutput(voice, output);
    }

    double sampleRate_ = 48000.0;
    double circuitSampleRate_ = 48000.0;
    AmbiWranglerParams params_ {};
    std::array<AmbiWranglerVoice, kAmbiWranglerMaxVoices> voices_ {};
    std::array<AmbiWranglerPoint, kAmbiWranglerMaxVoices> points_ {};
    std::array<AmbiWranglerPoint, kAmbiWranglerMaxVoices> targetPoints_ {};
    std::array<std::array<float, kAmbiWranglerMaxChannels>,
        kAmbiWranglerMaxVoices> renderBasis_ {};
    std::array<std::array<float, kAmbiWranglerMaxChannels>,
        kAmbiWranglerMaxVoices> targetRenderBasis_ {};
    std::array<std::array<float, kAmbiWranglerMaxChannels>,
        kAmbiWranglerMaxVoices> renderBasisIncrement_ {};
    std::array<float, kAmbiWranglerMaxVoices>
        renderDistanceGain_ {};
    std::array<float, kAmbiWranglerMaxVoices>
        targetRenderDistanceGain_ {};
    std::array<float, kAmbiWranglerMaxVoices>
        renderDistanceIncrement_ {};
    std::array<float, kAmbiWranglerMaxChannels>
        channelOrderGain_ {};
    uint32_t controlFramesRemaining_ = 0u;
    uint32_t spatialFramesRemaining_ = 0u;
    uint32_t circuitFramesRemaining_ = 0u;
    uint32_t expensiveControlFramesRemaining_ = 0u;
    uint32_t circuitStride_ = 1u;
    uint32_t processingVoiceLimit_ = 1u;
    uint32_t processingChannelLimit_ = 4u;
    float topologyPhase_ = 0.0f;
    float maskPhase_ = 0.0f;
    float smoothedOutputGain_ = 0.0f;
    float startupGain_ = 0.0f;
    float outputTransitionDecay_ = 0.0f;
    float transitionDecay_ = 0.0f;
    float edgeCoefficient_ = 0.0f;
    float shapeCoefficient_ = 0.0f;
    float colorCoefficient_ = 0.0f;
    float feedbackCoefficient_ = 0.0f;
    float ampCoefficient_ = 0.0f;
    float captureAttackCoefficient_ = 0.0f;
    float captureReleaseCoefficient_ = 0.0f;
    float maskAttackCoefficient_ = 0.0f;
    float maskReleaseCoefficient_ = 0.0f;
    float voiceAttackCoefficient_ = 0.0f;
    float voiceReleaseCoefficient_ = 0.0f;
    float outputGainCoefficient_ = 0.0f;
    float spatialControlStep_ = 0.0f;
    float orderCoefficient_ = 0.0f;
    float listenerEnableCoefficient_ = 0.0f;
    float returnEnableCoefficient_ = 0.0f;
    float startupCoefficient_ = 0.0f;
    std::array<float, kCoefficientTableSize>
        crossCoefficientTable_ {};
    std::array<float, kCoefficientTableSize>
        rungCoefficientTable_ {};
    std::array<float, kCoefficientTableSize>
        snapDecayTable_ {};
    std::array<float, kCoefficientTableSize>
        listenerCouplingTable_ {};
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
    size_t listenerDelayValidSamples_ = 0u;
    bool listenerDormant_ = true;
    bool audioStarted_ = false;
};

} // namespace s3g
