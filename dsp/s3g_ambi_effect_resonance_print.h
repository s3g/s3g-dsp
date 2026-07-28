#pragma once

#include "s3g_ambi_effect_dj_filter.h"
#include "s3g_math.h"
#include "s3g_realtime.h"

#if defined(__APPLE__)
#define S3G_HAS_RESONANCE_PRINT_FFT 1
#include <Accelerate/Accelerate.h>
#else
#define S3G_HAS_RESONANCE_PRINT_FFT 0
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace s3g {

constexpr uint32_t kResonancePrintVersion = 1u;
constexpr uint32_t kResonancePrintMaxModes = 12u;
constexpr uint32_t kResonancePrintAnalysisSize = 4096u;
constexpr uint32_t kResonancePrintAnalysisHop = 2048u;
constexpr uint32_t kResonancePrintBins = kResonancePrintAnalysisSize / 2u + 1u;

struct ResonancePrintMode {
    float frequencyHz = 0.0f;
    float amplitude = 0.0f;
    float stability = 0.0f;
};

struct ResonancePrintData {
    uint32_t version = kResonancePrintVersion;
    uint32_t valid = 0u;
    AmbiEffectBody capturedBody = AmbiEffectBody::Icosa12;
    uint32_t pickupCount = 0u;
    float fundamentalHz = 0.0f;
    float pitchConfidence = 0.0f;
    std::array<uint32_t, kAmbiEffectDjFilterMaxPickups> modeCount {};
    std::array<std::array<ResonancePrintMode, kResonancePrintMaxModes>,
        kAmbiEffectDjFilterMaxPickups> modes {};
};

struct AmbiEffectResonancePrintParams {
    uint32_t order = 7u;
    AmbiEffectBody body = AmbiEffectBody::Auto;
    AmbiEffectTopology topology = AmbiEffectTopology::Local;
    float captureSeconds = 0.75f;
    float sensitivity = 0.65f;
    uint32_t modalCount = 10u;
    float transposeSemitones = 0.0f;
    float harmonicPull = 0.0f;
    float harmonicStretch = 0.0f;
    float decaySeconds = 1.8f;
    float decayTilt = 0.15f;
    float drive = 0.35f;
    float spread = 0.0f;
    float deviation = 0.0f;
    float topologyAmount = 0.65f;
    float roamingRateHz = 0.08f;
    float mix = 0.55f;
    float outputGainDb = 0.0f;
    std::array<float, kAmbiEffectDjFilterMaxPickups> pickupTuneTrim {};
    std::array<float, kAmbiEffectDjFilterMaxPickups> pickupDecayTrim {};
    float maskAmount = 0.0f;
    float maskAzimuthDeg = 0.0f;
    float maskElevationDeg = 0.0f;
    float maskWidth = 0.35f;
    float maskCurve = 0.5f;
    float maskDry = 1.0f;
    uint32_t printEnabled = 0u;
};

inline AmbiEffectResonancePrintParams sanitizeAmbiEffectResonancePrintParams(
    AmbiEffectResonancePrintParams params)
{
    params.order = std::clamp<uint32_t>(params.order, 1u,
        kAmbiEffectDjFilterMaxOrder);
    params.body = static_cast<AmbiEffectBody>(std::min<uint32_t>(
        static_cast<uint32_t>(params.body), 5u));
    if (params.body == AmbiEffectBody::Tetra4
        || params.body == AmbiEffectBody::Cube8) {
        params.body = AmbiEffectBody::Icosa12;
    }
    params.topology = static_cast<AmbiEffectTopology>(std::min<uint32_t>(
        static_cast<uint32_t>(params.topology), 3u));
    params.captureSeconds = clamp(params.captureSeconds, 0.25f, 3.0f);
    params.sensitivity = clamp(params.sensitivity, 0.0f, 1.0f);
    params.modalCount = std::clamp<uint32_t>(params.modalCount, 2u,
        kResonancePrintMaxModes);
    params.printEnabled = params.printEnabled ? 1u : 0u;
    params.transposeSemitones = clamp(params.transposeSemitones, -24.0f, 24.0f);
    params.harmonicPull = clamp(params.harmonicPull, 0.0f, 1.0f);
    params.harmonicStretch = clamp(params.harmonicStretch, -1.0f, 1.0f);
    params.decaySeconds = clamp(params.decaySeconds, 0.08f, 8.0f);
    params.decayTilt = clamp(params.decayTilt, -1.0f, 1.0f);
    params.drive = clamp(params.drive, 0.0f, 1.0f);
    params.spread = clamp(params.spread, 0.0f, 1.0f);
    params.deviation = clamp(params.deviation, 0.0f, 1.0f);
    params.topologyAmount = clamp(params.topologyAmount, 0.0f, 1.0f);
    params.roamingRateHz = clamp(params.roamingRateHz, 0.005f, 2.0f);
    params.mix = clamp(params.mix, 0.0f, 1.0f);
    params.outputGainDb = clamp(params.outputGainDb, -60.0f, 12.0f);
    for (float& value : params.pickupTuneTrim) value = clamp(value, -1.0f, 1.0f);
    for (float& value : params.pickupDecayTrim) value = clamp(value, -1.0f, 1.0f);
    params.maskAmount = clamp(params.maskAmount, 0.0f, 1.0f);
    params.maskAzimuthDeg = clamp(params.maskAzimuthDeg, -180.0f, 180.0f);
    params.maskElevationDeg = clamp(params.maskElevationDeg, -90.0f, 90.0f);
    params.maskWidth = clamp(params.maskWidth, 0.0f, 1.0f);
    params.maskCurve = clamp(params.maskCurve, 0.0f, 1.0f);
    params.maskDry = clamp(params.maskDry, 0.0f, 1.0f);
    return params;
}

class AmbiEffectResonancePrint {
public:
    AmbiEffectResonancePrint() = default;
    ~AmbiEffectResonancePrint() { releaseFft(); }
    AmbiEffectResonancePrint(const AmbiEffectResonancePrint&) = delete;
    AmbiEffectResonancePrint& operator=(const AmbiEffectResonancePrint&) = delete;

    bool prepare(double sampleRate)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        safetyRelease_ = 1.0f - std::exp(-1.0f
            / static_cast<float>(sampleRate_ * 0.300));
        excitationGovernorRelease_ = 1.0f - std::exp(-1.0f
            / static_cast<float>(sampleRate_ * 0.250));
        outputGainCoefficient_ = 1.0f - std::exp(-1.0f
            / static_cast<float>(sampleRate_ * 0.025));
        resonatorTargetCoefficient_ = 1.0f - std::exp(
            -32.0f / static_cast<float>(sampleRate_ * 0.045));
        resonatorCoefficientSmoothing_ = 1.0f - std::exp(-1.0f
            / static_cast<float>(sampleRate_ * 0.0025));
        buildMatrixCache();
#if S3G_HAS_RESONANCE_PRINT_FFT
        releaseFft();
        fftSetup_ = vDSP_create_fftsetup(12u, kFFTRadix2);
        if (!fftSetup_) return false;
#endif
        for (uint32_t i = 0u; i < kResonancePrintAnalysisSize; ++i) {
            analysisWindow_[i] = 0.5f - 0.5f * std::cos(
                2.0f * kPi * static_cast<float>(i)
                / static_cast<float>(kResonancePrintAnalysisSize));
        }
        params_ = sanitizeAmbiEffectResonancePrintParams(params_);
        outputGainTargetDb_.store(params_.outputGainDb,
            std::memory_order_relaxed);
        updateMatrix();
        updateMask();
        rebuildModeTargets();
        reset();
        return S3G_HAS_RESONANCE_PRINT_FFT != 0;
    }

    void reset()
    {
        roamingPhase_ = 0.0f;
        safetyGain_ = 1.0f;
        excitationGovernorGain_ = 1.0f;
        resonatorUpdateCountdown_ = 0u;
        currentTopologyAmount_ = params_.topologyAmount;
        currentRoamingRateHz_ = params_.roamingRateHz;
        currentMix_ = params_.mix;
        currentOutputGain_ = dbToGain(outputGainTargetDb_.load(
            std::memory_order_relaxed));
        currentDrive_ = params_.drive;
        topologyFade_ = 1.0f;
        previousTopology_ = targetTopology_ = params_.topology;
        nodeLevel_.fill(0.0f);
        for (auto& row : resonators_) {
            for (auto& mode : row) {
                mode.y1 = 0.0f;
                mode.y2 = 0.0f;
                mode.currentFrequency = mode.targetFrequency;
                mode.currentRadius = mode.targetRadius;
                mode.currentAmplitude = mode.targetAmplitude;
                updateResonatorCoefficientTargets(mode);
                snapResonatorCoefficients(mode);
            }
        }
        if (capturing_) cancelCapture();
    }

    void setParams(AmbiEffectResonancePrintParams params)
    {
        auto next = sanitizeAmbiEffectResonancePrintParams(params);
        if (capturing_) next.printEnabled = 0u;
        const bool matrixChanged = next.order != params_.order
            || resolveAmbiEffectBody(next.body, next.order)
                != resolveAmbiEffectBody(params_.body, params_.order);
        if (next.topology != targetTopology_) {
            previousTopology_ = targetTopology_;
            targetTopology_ = next.topology;
            topologyFade_ = 0.0f;
        }
        params_ = next;
        outputGainTargetDb_.store(params_.outputGainDb,
            std::memory_order_relaxed);
        if (matrixChanged) {
            if (capturing_) cancelCapture();
            updateMatrix();
        }
        updateMask();
        rebuildModeTargets();
    }

    const AmbiEffectResonancePrintParams& params() const { return params_; }
    void setOutputGainTarget(float outputGainDb)
    {
        outputGainTargetDb_.store(clamp(
            std::isfinite(outputGainDb) ? outputGainDb : 0.0f,
            -60.0f, 12.0f), std::memory_order_relaxed);
    }
    AmbiEffectBody resolvedBody() const
    {
        return resolveAmbiEffectBody(params_.body, params_.order);
    }
    uint32_t activePickupCount() const
    {
        return ambiEffectBodyPickupCount(resolvedBody());
    }

    void beginCapture()
    {
        params_.printEnabled = 0u;
        capturing_ = true;
        captureSampleCount_ = 0u;
        captureWritePosition_ = 0u;
        captureAnalysisFrames_ = 0u;
        captureTargetSamples_ = std::max<uint64_t>(
            kResonancePrintAnalysisSize,
            static_cast<uint64_t>(std::llround(params_.captureSeconds * sampleRate_)));
        captureBody_ = resolvedBody();
        capturePickupCount_ = activePickupCount();
        for (auto& row : captureRing_) row.fill(0.0f);
        for (auto& row : captureSpectrum_) row.fill(0.0f);
        ++generation_;
    }

    void cancelCapture()
    {
        capturing_ = false;
        captureSampleCount_ = 0u;
        captureAnalysisFrames_ = 0u;
        ++generation_;
    }

    void clearPrint()
    {
        capturing_ = false;
        captureSampleCount_ = 0u;
        captureAnalysisFrames_ = 0u;
        print_ = {};
        print_.version = kResonancePrintVersion;
        params_.printEnabled = 0u;
        resonatorModeCount_.fill(0u);
        nodePrintStrength_.fill(0.0f);
        maximumDecaySeconds_ = 0.0f;
        safetyGain_ = 1.0f;
        excitationGovernorGain_ = 1.0f;
        for (auto& row : resonators_) {
            for (auto& mode : row) mode = {};
        }
        ++generation_;
    }

    void setPrint(const ResonancePrintData& data)
    {
        safetyGain_ = 1.0f;
        excitationGovernorGain_ = 1.0f;
        print_ = data;
        print_.version = kResonancePrintVersion;
        print_.valid = print_.valid ? 1u : 0u;
        print_.capturedBody = resolveAmbiEffectBody(print_.capturedBody, 7u);
        print_.pickupCount = std::min<uint32_t>(print_.pickupCount,
            kAmbiEffectDjFilterMaxPickups);
        print_.fundamentalHz = std::isfinite(print_.fundamentalHz)
            ? clamp(print_.fundamentalHz, 0.0f, 4000.0f) : 0.0f;
        print_.pitchConfidence = std::isfinite(print_.pitchConfidence)
            ? clamp(print_.pitchConfidence, 0.0f, 1.0f) : 0.0f;
        for (uint32_t node = 0u; node < kAmbiEffectDjFilterMaxPickups; ++node) {
            print_.modeCount[node] = std::min<uint32_t>(print_.modeCount[node],
                kResonancePrintMaxModes);
            for (auto& mode : print_.modes[node]) {
                mode.frequencyHz = std::isfinite(mode.frequencyHz)
                    ? clamp(mode.frequencyHz, 0.0f, 24000.0f) : 0.0f;
                mode.amplitude = std::isfinite(mode.amplitude)
                    ? clamp(mode.amplitude, 0.0f, 1.0f) : 0.0f;
                mode.stability = std::isfinite(mode.stability)
                    ? clamp(mode.stability, 0.0f, 1.0f) : 0.0f;
            }
        }
        rebuildModeTargets();
        ++generation_;
    }

    const ResonancePrintData& printData() const { return print_; }
    bool hasPrint() const { return print_.valid != 0u; }
    bool printApplied() const
    {
        return !capturing_ && hasPrint() && params_.printEnabled != 0u;
    }
    bool isCapturing() const { return capturing_; }
    float captureProgress() const
    {
        return capturing_ && captureTargetSamples_ > 0u
            ? clamp(static_cast<float>(captureSampleCount_)
                / static_cast<float>(captureTargetSamples_), 0.0f, 1.0f)
            : 0.0f;
    }
    uint64_t generation() const { return generation_; }
    float fundamentalHz() const { return print_.fundamentalHz; }
    float pitchConfidence() const { return print_.pitchConfidence; }
    uint32_t printedModeCount(uint32_t pickup) const
    {
        if (pickup >= activePickupCount()) return 0u;
        return resonatorModeCount_[pickup];
    }
    float nodeLevel(uint32_t pickup) const
    {
        return pickup < kAmbiEffectDjFilterMaxPickups ? nodeLevel_[pickup] : 0.0f;
    }
    float nodePrintStrength(uint32_t pickup) const
    {
        return pickup < kAmbiEffectDjFilterMaxPickups
            ? nodePrintStrength_[pickup] : 0.0f;
    }
    float nodeWetMask(uint32_t pickup) const
    {
        return pickup < kAmbiEffectDjFilterMaxPickups ? maskGain_[pickup] : 1.0f;
    }
    float roamingPhase() const { return roamingPhase_; }
    float safetyGain() const { return safetyGain_; }
    float excitationGovernorGain() const { return excitationGovernorGain_; }
    float maximumResonatorState() const
    {
        float maximum = 0.0f;
        for (const auto& row : resonators_) {
            for (const auto& mode : row) {
                maximum = std::max(maximum,
                    std::max(std::abs(mode.y1), std::abs(mode.y2)));
            }
        }
        return maximum;
    }
    uint32_t tailFrames() const
    {
        return printApplied() ? static_cast<uint32_t>(std::ceil(maximumDecaySeconds_
            * sampleRate_)) : 0u;
    }

    template <typename Sample>
    void process(Sample** input, Sample** output,
        uint32_t inputChannels, uint32_t outputChannels, uint32_t frames)
    {
        if (!output || !currentMatrix_) return;
        const uint32_t inCount = std::min<uint32_t>(inputChannels,
            kAmbiEffectDjFilterMaxChannels);
        const uint32_t outCount = std::min<uint32_t>(outputChannels,
            kAmbiEffectDjFilterMaxChannels);
        const uint32_t channels = ambiEffectChannelsForOrder(params_.order);
        const uint32_t pickupCount = currentMatrix_->count;
        std::array<float, kAmbiEffectDjFilterMaxChannels> field {};
        std::array<float, kAmbiEffectDjFilterMaxChannels> outputFrame {};
        std::array<float, kAmbiEffectDjFilterMaxPickups> ears {};
        std::array<float, kAmbiEffectDjFilterMaxPickups> resonant {};
        std::array<float, kAmbiEffectDjFilterMaxPickups> routedPrevious {};
        std::array<float, kAmbiEffectDjFilterMaxPickups> routedTarget {};

        for (uint32_t frameIndex = 0u; frameIndex < frames; ++frameIndex) {
            smoothGlobals();
            roamingPhase_ += currentRoamingRateHz_ / static_cast<float>(sampleRate_);
            roamingPhase_ -= std::floor(roamingPhase_);
            topologyFade_ += (1.0f - topologyFade_) * topologyCoefficient_;
            if (topologyFade_ > 0.99999f) {
                topologyFade_ = 1.0f;
                previousTopology_ = targetTopology_;
            }
            if (resonatorUpdateCountdown_ == 0u) {
                updateResonatorSmoothing();
                resonatorUpdateCountdown_ = 32u;
            }
            --resonatorUpdateCountdown_;

            for (uint32_t ch = 0u; ch < kAmbiEffectDjFilterMaxChannels; ++ch) {
                const float value = ch < inCount && input && input[ch]
                    ? static_cast<float>(input[ch][frameIndex]) : 0.0f;
                field[ch] = std::isfinite(value) ? value : 0.0f;
            }
            for (uint32_t node = 0u; node < pickupCount; ++node) {
                float value = 0.0f;
                for (uint32_t ch = 0u; ch < channels; ++ch) {
                    value += currentMatrix_->decode[node][ch] * field[ch];
                }
                ears[node] = flushDenormal(value);
                nodeLevel_[node] += (std::abs(ears[node]) - nodeLevel_[node])
                    * levelCoefficient_;
            }
            for (uint32_t node = pickupCount;
                node < kAmbiEffectDjFilterMaxPickups; ++node) {
                ears[node] = 0.0f;
                nodeLevel_[node] += (0.0f - nodeLevel_[node]) * levelCoefficient_;
            }

            captureFrame(ears, pickupCount);

            // Govern only the shared resonator excitation. The analyzed field
            // and dry HOA path remain untouched, while every pickup receives
            // the same gain so the excitation direction cannot move.
            float excitationPeak = 0.0f;
            for (uint32_t node = 0u; node < pickupCount; ++node) {
                excitationPeak = std::max(excitationPeak, std::abs(ears[node]));
            }
            constexpr float kExcitationCeiling = 0.25f;
            const float excitationTarget = hasPrint()
                && excitationPeak > kExcitationCeiling
                ? kExcitationCeiling / excitationPeak : 1.0f;
            if (!std::isfinite(excitationGovernorGain_)
                || excitationTarget < excitationGovernorGain_) {
                excitationGovernorGain_ = excitationTarget;
            } else {
                excitationGovernorGain_ += excitationGovernorRelease_
                    * (1.0f - excitationGovernorGain_);
            }
            excitationGovernorGain_ = clamp(
                excitationGovernorGain_, 0.0f, 1.0f);

            for (uint32_t node = 0u; node < pickupCount; ++node) {
                if (!hasPrint() || resonatorModeCount_[node] == 0u) {
                    resonant[node] = ears[node];
                    continue;
                }
                const float driven = softDrive(
                    ears[node] * excitationGovernorGain_, currentDrive_);
                float sum = 0.0f;
                for (uint32_t modeIndex = 0u;
                    modeIndex < resonatorModeCount_[node]; ++modeIndex) {
                    auto& mode = resonators_[node][modeIndex];
                    smoothResonatorCoefficients(mode);
                    const float stateMagnitude = std::max(
                        std::abs(mode.y1), std::abs(mode.y2));
                    // Long, closely driven modes shed pole energy gradually
                    // before reaching the remote C1-continuous state knee.
                    // Non-finite state is the only condition that hard-resets.
                    const float overload = std::max(
                        0.0f, stateMagnitude * 0.25f - 1.0f);
                    const float nonlinearDamping = 1.0f
                        / (1.0f + 0.0025f * overload * overload);
                    float value = mode.inputGain * driven
                        + mode.coefficient * nonlinearDamping * mode.y1
                        - mode.radiusSquared * nonlinearDamping
                            * nonlinearDamping * mode.y2;
                    if (!std::isfinite(value)) {
                        mode.y1 = 0.0f;
                        mode.y2 = 0.0f;
                        value = 0.0f;
                    } else {
                        constexpr float kStateKnee = 24.0f;
                        constexpr float kStateLimit = 64.0f;
                        const float magnitude = std::abs(value);
                        if (magnitude > kStateKnee) {
                            const float excess = magnitude - kStateKnee;
                            const float range = kStateLimit - kStateKnee;
                            value = std::copysign(kStateKnee
                                    + excess / (1.0f + excess / range),
                                value);
                        }
                    }
                    mode.y2 = mode.y1;
                    mode.y1 = flushDenormal(value);
                    sum += value;
                }
                resonant[node] = flushDenormal(softProtect(sum));
            }
            for (uint32_t node = pickupCount;
                node < kAmbiEffectDjFilterMaxPickups; ++node) {
                resonant[node] = 0.0f;
            }

            std::array<float, kAmbiEffectDjFilterMaxPickups> delta {};
            if (printApplied()) {
                routeBody(resonant, routedPrevious, previousTopology_, roamingPhase_);
                routeBody(resonant, routedTarget, targetTopology_, roamingPhase_);
                for (uint32_t node = 0u; node < pickupCount; ++node) {
                    const float routed = lerp(routedPrevious[node], routedTarget[node],
                        topologyFade_);
                    const float processed = lerp(resonant[node], routed,
                        currentTopologyAmount_);
                    const float masked = lerp(ears[node] * params_.maskDry,
                        processed, maskGain_[node]);
                    delta[node] = masked - ears[node];
                }
            }

            const uint32_t activeOut = std::min(outCount, channels);
            float outputPeak = 0.0f;
            for (uint32_t ch = 0u; ch < activeOut; ++ch) {
                float correction = 0.0f;
                for (uint32_t node = 0u; node < pickupCount; ++node) {
                    correction += currentMatrix_->encode[ch][node] * delta[node];
                }
                const float value = (field[ch] + correction * currentMix_)
                    * currentOutputGain_;
                outputFrame[ch] = flushDenormal(
                    std::isfinite(value) ? value : 0.0f);
                outputPeak = std::max(outputPeak, std::abs(outputFrame[ch]));
            }
            constexpr float kOutputCeiling = 0.89125094f; // -1 dBFS
            // OUT is already included in outputFrame. This final linked guard
            // therefore protects applied, bypassed, and no-print paths alike.
            const float target = outputPeak > kOutputCeiling
                ? kOutputCeiling / outputPeak : 1.0f;
            if (!std::isfinite(safetyGain_) || target < safetyGain_) {
                safetyGain_ = target;
            } else {
                safetyGain_ += safetyRelease_ * (1.0f - safetyGain_);
            }
            safetyGain_ = clamp(std::min(safetyGain_, target), 0.0f, 1.0f);
            for (uint32_t ch = 0u; ch < activeOut; ++ch) {
                if (!output[ch]) continue;
                const float value = outputFrame[ch] * safetyGain_;
                output[ch][frameIndex] = static_cast<Sample>(flushDenormal(
                    std::isfinite(value) ? value : 0.0f));
            }
            for (uint32_t ch = activeOut; ch < outputChannels; ++ch) {
                if (output[ch]) output[ch][frameIndex] = Sample(0);
            }
        }
    }

private:
    struct MatrixSet {
        uint32_t count = 0u;
        std::array<Vec3, kAmbiEffectDjFilterMaxPickups> directions {};
        std::array<std::array<float, kAmbiEffectDjFilterMaxChannels>,
            kAmbiEffectDjFilterMaxPickups> decode {};
        std::array<std::array<float, kAmbiEffectDjFilterMaxPickups>,
            kAmbiEffectDjFilterMaxChannels> encode {};
        std::array<uint32_t, kAmbiEffectDjFilterMaxPickups> opposite {};
        std::array<std::array<uint32_t, 5u>,
            kAmbiEffectDjFilterMaxPickups> neighbors {};
        std::array<uint32_t, kAmbiEffectDjFilterMaxPickups> neighborCount {};
        std::array<uint32_t, kAmbiEffectDjFilterMaxPickups> roamCycle {};
    };

    struct Resonator {
        float targetFrequency = 440.0f;
        float currentFrequency = 440.0f;
        float targetRadius = 0.99f;
        float currentRadius = 0.99f;
        float targetAmplitude = 0.0f;
        float currentAmplitude = 0.0f;
        float coefficient = 0.0f;
        float radiusSquared = 0.9801f;
        float inputGain = 0.0f;
        float nextCoefficient = 0.0f;
        float nextRadiusSquared = 0.9801f;
        float nextInputGain = 0.0f;
        float y1 = 0.0f;
        float y2 = 0.0f;
    };

    static float vectorDot(Vec3 a, Vec3 b)
    {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    static uint32_t bodyCacheIndex(AmbiEffectBody body)
    {
        body = resolveAmbiEffectBody(body, 7u);
        if (body == AmbiEffectBody::Dodeca20) return 1u;
        if (body == AmbiEffectBody::Sphere24) return 2u;
        return 0u;
    }

    static bool invertMatrix(
        const std::array<std::array<double, kAmbiEffectDjFilterMaxPickups>,
            kAmbiEffectDjFilterMaxPickups>& input,
        uint32_t count,
        std::array<std::array<double, kAmbiEffectDjFilterMaxPickups>,
            kAmbiEffectDjFilterMaxPickups>& inverse)
    {
        std::array<std::array<double, kAmbiEffectDjFilterMaxPickups * 2u>,
            kAmbiEffectDjFilterMaxPickups> augmented {};
        for (uint32_t row = 0u; row < count; ++row) {
            for (uint32_t col = 0u; col < count; ++col) {
                augmented[row][col] = input[row][col];
            }
            augmented[row][count + row] = 1.0;
        }
        for (uint32_t pivot = 0u; pivot < count; ++pivot) {
            uint32_t best = pivot;
            for (uint32_t row = pivot + 1u; row < count; ++row) {
                if (std::abs(augmented[row][pivot])
                    > std::abs(augmented[best][pivot])) best = row;
            }
            if (std::abs(augmented[best][pivot]) < 1.0e-12) return false;
            if (best != pivot) std::swap(augmented[best], augmented[pivot]);
            const double divisor = augmented[pivot][pivot];
            for (uint32_t col = 0u; col < count * 2u; ++col) {
                augmented[pivot][col] /= divisor;
            }
            for (uint32_t row = 0u; row < count; ++row) {
                if (row == pivot) continue;
                const double factor = augmented[row][pivot];
                for (uint32_t col = 0u; col < count * 2u; ++col) {
                    augmented[row][col] -= factor * augmented[pivot][col];
                }
            }
        }
        for (uint32_t row = 0u; row < count; ++row) {
            for (uint32_t col = 0u; col < count; ++col) {
                inverse[row][col] = augmented[row][count + col];
            }
        }
        return true;
    }

    static void buildRelationships(MatrixSet& matrix)
    {
        for (uint32_t node = 0u; node < matrix.count; ++node) {
            float minimum = 2.0f;
            float nearest = -2.0f;
            for (uint32_t other = 0u; other < matrix.count; ++other) {
                if (node == other) continue;
                const float relation = vectorDot(matrix.directions[node],
                    matrix.directions[other]);
                if (relation < minimum) {
                    minimum = relation;
                    matrix.opposite[node] = other;
                }
                nearest = std::max(nearest, relation);
            }
            for (uint32_t other = 0u; other < matrix.count; ++other) {
                if (node == other) continue;
                if (vectorDot(matrix.directions[node], matrix.directions[other])
                    >= nearest - 0.0001f && matrix.neighborCount[node] < 5u) {
                    matrix.neighbors[node][matrix.neighborCount[node]++] = other;
                }
            }
        }
        // A deterministic ordinal cycle is sufficient for the smooth roaming
        // relationship and remains stable across saved states.
        for (uint32_t node = 0u; node < matrix.count; ++node) {
            matrix.roamCycle[node] = node;
        }
    }

    static void buildMatrix(MatrixSet& matrix, uint32_t order,
        AmbiEffectBody body)
    {
        matrix = {};
        matrix.directions = ambiEffectBodyDirections(body);
        matrix.count = ambiEffectBodyPickupCount(body);
        buildRelationships(matrix);
        const uint32_t channels = ambiEffectChannelsForOrder(order);
        for (uint32_t node = 0u; node < matrix.count; ++node) {
            const auto basis = acnSn3dBasis7(matrix.directions[node]);
            double norm = 0.0;
            for (uint32_t ch = 0u; ch < channels; ++ch) {
                norm += static_cast<double>(basis[ch]) * basis[ch];
            }
            norm = std::max(1.0, norm);
            for (uint32_t ch = 0u; ch < channels; ++ch) {
                matrix.decode[node][ch] = static_cast<float>(
                    static_cast<double>(basis[ch]) / norm);
            }
        }
        std::array<std::array<double, kAmbiEffectDjFilterMaxPickups>,
            kAmbiEffectDjFilterMaxPickups> gram {};
        for (uint32_t row = 0u; row < matrix.count; ++row) {
            for (uint32_t col = 0u; col < matrix.count; ++col) {
                for (uint32_t ch = 0u; ch < channels; ++ch) {
                    gram[row][col] += static_cast<double>(
                        matrix.decode[row][ch]) * matrix.decode[col][ch];
                }
                if (row == col) gram[row][col] += 1.0e-5;
            }
        }
        std::array<std::array<double, kAmbiEffectDjFilterMaxPickups>,
            kAmbiEffectDjFilterMaxPickups> inverse {};
        if (!invertMatrix(gram, matrix.count, inverse)) return;
        for (uint32_t ch = 0u; ch < channels; ++ch) {
            for (uint32_t node = 0u; node < matrix.count; ++node) {
                double value = 0.0;
                for (uint32_t row = 0u; row < matrix.count; ++row) {
                    value += static_cast<double>(matrix.decode[row][ch])
                        * inverse[row][node];
                }
                matrix.encode[ch][node] = static_cast<float>(value);
            }
        }
    }

    void buildMatrixCache()
    {
        for (uint32_t order = 1u; order <= kAmbiEffectDjFilterMaxOrder; ++order) {
            buildMatrix(matrixCache_[order - 1u][0u], order,
                AmbiEffectBody::Icosa12);
            buildMatrix(matrixCache_[order - 1u][1u], order,
                AmbiEffectBody::Dodeca20);
            buildMatrix(matrixCache_[order - 1u][2u], order,
                AmbiEffectBody::Sphere24);
        }
    }

    void updateMatrix()
    {
        currentMatrix_ = &matrixCache_[params_.order - 1u]
            [bodyCacheIndex(resolvedBody())];
    }

    void updateMask()
    {
        maskGain_.fill(1.0f);
        if (!currentMatrix_ || params_.maskAmount <= 0.0f) return;
        const float azimuth = params_.maskAzimuthDeg * kPi / 180.0f;
        const float elevation = params_.maskElevationDeg * kPi / 180.0f;
        const float cosElevation = std::cos(elevation);
        const Vec3 direction { cosElevation * std::cos(azimuth),
            cosElevation * std::sin(azimuth), std::sin(elevation) };
        float maximum = 0.000001f;
        std::array<float, kAmbiEffectDjFilterMaxPickups> alignment {};
        for (uint32_t node = 0u; node < currentMatrix_->count; ++node) {
            alignment[node] = clamp((vectorDot(currentMatrix_->directions[node],
                direction) + 1.0f) * 0.5f, 0.0f, 1.0f);
            maximum = std::max(maximum, alignment[node]);
        }
        const float exponent = ambiEffectMaskExponent(params_.maskWidth,
            params_.maskCurve);
        for (uint32_t node = 0u; node < currentMatrix_->count; ++node) {
            const float directional = std::pow(clamp(alignment[node] / maximum,
                0.0f, 1.0f), exponent);
            maskGain_[node] = lerp(1.0f, directional, params_.maskAmount);
        }
    }

    uint32_t nearestCapturedPickup(uint32_t currentNode) const
    {
        if (!print_.valid || print_.pickupCount == 0u || !currentMatrix_) return 0u;
        const auto captured = ambiEffectBodyDirections(print_.capturedBody);
        uint32_t best = 0u;
        float bestDot = -2.0f;
        for (uint32_t node = 0u; node < print_.pickupCount; ++node) {
            const float relation = vectorDot(currentMatrix_->directions[currentNode],
                captured[node]);
            if (relation > bestDot) {
                bestDot = relation;
                best = node;
            }
        }
        return best;
    }

    void rebuildModeTargets()
    {
        maximumDecaySeconds_ = 0.0f;
        nodePrintStrength_.fill(0.0f);
        if (!currentMatrix_) return;
        const uint32_t count = currentMatrix_->count;
        for (uint32_t node = 0u; node < kAmbiEffectDjFilterMaxPickups; ++node) {
            resonatorModeCount_[node] = 0u;
            if (node >= count || !hasPrint()) {
                for (auto& mode : resonators_[node]) mode.targetAmplitude = 0.0f;
                continue;
            }
            const uint32_t source = nearestCapturedPickup(node);
            const uint32_t modeCount = std::min<uint32_t>(
                { print_.modeCount[source], params_.modalCount,
                    kResonancePrintMaxModes });
            resonatorModeCount_[node] = modeCount;
            const float ordinal = ambiEffectPickupOrdinal(node, count);
            const float semitoneOffset = params_.transposeSemitones
                + params_.pickupTuneTrim[node] * 12.0f
                + ordinal * params_.spread * 6.0f
                + ambiEffectPickupHash(node, 97u) * params_.deviation * 1.5f;
            for (uint32_t index = 0u; index < kResonancePrintMaxModes; ++index) {
                auto& target = resonators_[node][index];
                if (index >= modeCount) {
                    target.targetAmplitude = 0.0f;
                    continue;
                }
                const auto& captured = print_.modes[source][index];
                float frequency = captured.frequencyHz;
                if (print_.fundamentalHz > 20.0f && print_.pitchConfidence > 0.05f) {
                    const float harmonic = std::max(1.0f,
                        std::round(frequency / print_.fundamentalHz));
                    const float harmonicFrequency = print_.fundamentalHz * harmonic;
                    const float pull = params_.harmonicPull
                        * clamp(print_.pitchConfidence * 1.5f, 0.0f, 1.0f);
                    frequency = lerp(frequency, harmonicFrequency, pull);
                    const float ratio = std::max(0.01f,
                        frequency / print_.fundamentalHz);
                    frequency = print_.fundamentalHz * std::pow(ratio,
                        1.0f + params_.harmonicStretch * 0.45f);
                }
                frequency *= std::pow(2.0f, semitoneOffset / 12.0f);
                frequency = clamp(frequency, 20.0f,
                    static_cast<float>(sampleRate_ * 0.46));
                const float frequencyOctaves = std::log2(
                    std::max(20.0f, frequency) / 1000.0f);
                const float decayExponent = params_.pickupDecayTrim[node] * 1.5f
                    + ordinal * params_.spread * 0.45f
                    + ambiEffectPickupHash(node, 101u)
                        * params_.deviation * 0.35f
                    - params_.decayTilt * frequencyOctaves * 0.45f;
                const float decay = clamp(params_.decaySeconds
                    * std::pow(2.0f, decayExponent), 0.05f, 12.0f);
                target.targetFrequency = frequency;
                target.targetRadius = std::exp(std::log(0.001f)
                    / std::max(1.0f, decay * static_cast<float>(sampleRate_)));
                target.targetAmplitude = captured.amplitude
                    * lerp(0.65f, 1.0f, captured.stability);
                nodePrintStrength_[node] += target.targetAmplitude;
                maximumDecaySeconds_ = std::max(maximumDecaySeconds_, decay);
            }
            nodePrintStrength_[node] = clamp(nodePrintStrength_[node], 0.0f, 1.0f);
        }
    }

    void updateResonatorSmoothing()
    {
        for (uint32_t node = 0u; node < activePickupCount(); ++node) {
            for (uint32_t index = 0u; index < kResonancePrintMaxModes; ++index) {
                auto& mode = resonators_[node][index];
                mode.currentFrequency += (mode.targetFrequency
                    - mode.currentFrequency) * resonatorTargetCoefficient_;
                mode.currentRadius += (mode.targetRadius
                    - mode.currentRadius) * resonatorTargetCoefficient_;
                mode.currentAmplitude += (mode.targetAmplitude
                    - mode.currentAmplitude) * resonatorTargetCoefficient_;
                updateResonatorCoefficientTargets(mode);
            }
        }
    }

    void updateResonatorCoefficientTargets(Resonator& mode)
    {
        const float radius = clamp(mode.currentRadius, 0.0f, 0.9999995f);
        const float omega = 2.0f * kPi * clamp(mode.currentFrequency, 20.0f,
            static_cast<float>(sampleRate_ * 0.46))
            / static_cast<float>(sampleRate_);
        mode.nextCoefficient = 2.0f * radius * std::cos(omega);
        mode.nextRadiusSquared = radius * radius;
        const float driveGain = lerp(0.7f, 2.4f, currentDrive_);
        mode.nextInputGain = mode.currentAmplitude * (1.0f - radius)
            * driveGain * 1.8f;
    }

    static void snapResonatorCoefficients(Resonator& mode)
    {
        mode.coefficient = mode.nextCoefficient;
        mode.radiusSquared = mode.nextRadiusSquared;
        mode.inputGain = mode.nextInputGain;
    }

    void smoothResonatorCoefficients(Resonator& mode) const
    {
        mode.coefficient += (mode.nextCoefficient - mode.coefficient)
            * resonatorCoefficientSmoothing_;
        mode.radiusSquared += (mode.nextRadiusSquared - mode.radiusSquared)
            * resonatorCoefficientSmoothing_;
        mode.inputGain += (mode.nextInputGain - mode.inputGain)
            * resonatorCoefficientSmoothing_;
    }

    static float softDrive(float value, float amount)
    {
        if (amount <= 0.00001f) return value;
        const float gain = lerp(1.0f, 5.0f, amount);
        const float normalization = 1.0f / std::tanh(gain);
        return std::tanh(clamp(value * gain, -8.0f, 8.0f)) * normalization;
    }

    static float softProtect(float value)
    {
        if (std::abs(value) <= 1.2f) return value;
        return std::tanh(value / 1.2f) * 1.2f;
    }

    void smoothGlobals()
    {
        currentTopologyAmount_ += (params_.topologyAmount
            - currentTopologyAmount_) * parameterCoefficient_;
        currentRoamingRateHz_ += (params_.roamingRateHz
            - currentRoamingRateHz_) * parameterCoefficient_;
        currentMix_ += (params_.mix - currentMix_) * parameterCoefficient_;
        currentOutputGain_ += (dbToGain(outputGainTargetDb_.load(
                std::memory_order_relaxed))
            - currentOutputGain_) * outputGainCoefficient_;
        currentDrive_ += (params_.drive - currentDrive_) * parameterCoefficient_;
    }

    void routeBody(
        const std::array<float, kAmbiEffectDjFilterMaxPickups>& input,
        std::array<float, kAmbiEffectDjFilterMaxPickups>& output,
        AmbiEffectTopology topology, float phase) const
    {
        output.fill(0.0f);
        if (!currentMatrix_ || currentMatrix_->count == 0u) return;
        const uint32_t count = currentMatrix_->count;
        if (topology == AmbiEffectTopology::Local) {
            for (uint32_t node = 0u; node < count; ++node) output[node] = input[node];
        } else if (topology == AmbiEffectTopology::Cross) {
            for (uint32_t node = 0u; node < count; ++node) {
                output[node] = input[currentMatrix_->opposite[node] % count];
            }
        } else if (topology == AmbiEffectTopology::Diffuse) {
            for (uint32_t node = 0u; node < count; ++node) {
                float value = 0.0f;
                const uint32_t neighbors = currentMatrix_->neighborCount[node];
                for (uint32_t slot = 0u; slot < neighbors; ++slot) {
                    value += input[currentMatrix_->neighbors[node][slot] % count];
                }
                output[node] = neighbors > 0u
                    ? value / static_cast<float>(neighbors) : input[node];
            }
        } else {
            const float position = phase * static_cast<float>(count);
            const uint32_t firstOffset = static_cast<uint32_t>(position) % count;
            const uint32_t secondOffset = (firstOffset + 1u) % count;
            const float fraction = position - std::floor(position);
            for (uint32_t node = 0u; node < count; ++node) {
                output[node] = lerp(input[(node + firstOffset) % count],
                    input[(node + secondOffset) % count], fraction);
            }
        }
    }

    void captureFrame(
        const std::array<float, kAmbiEffectDjFilterMaxPickups>& ears,
        uint32_t pickupCount)
    {
        if (!capturing_) return;
        const uint32_t captureCount = std::min(pickupCount, capturePickupCount_);
        for (uint32_t node = 0u; node < captureCount; ++node) {
            captureRing_[node][captureWritePosition_] = ears[node];
        }
        captureWritePosition_ = (captureWritePosition_ + 1u)
            % kResonancePrintAnalysisSize;
        ++captureSampleCount_;
        if (captureSampleCount_ >= kResonancePrintAnalysisSize
            && ((captureSampleCount_ - kResonancePrintAnalysisSize)
                % kResonancePrintAnalysisHop) == 0u) {
            analyzeCaptureFrame();
        }
        if (captureSampleCount_ >= captureTargetSamples_) finishCapture();
    }

    void analyzeCaptureFrame()
    {
#if S3G_HAS_RESONANCE_PRINT_FFT
        for (uint32_t node = 0u; node < capturePickupCount_; ++node) {
            for (uint32_t i = 0u; i < kResonancePrintAnalysisSize; ++i) {
                const uint32_t source = (captureWritePosition_ + i)
                    % kResonancePrintAnalysisSize;
                fftTime_[i] = captureRing_[node][source] * analysisWindow_[i];
            }
            DSPSplitComplex split { fftReal_.data(), fftImag_.data() };
            vDSP_ctoz(reinterpret_cast<const DSPComplex*>(fftTime_.data()), 2,
                &split, 1, kResonancePrintAnalysisSize / 2u);
            vDSP_fft_zrip(fftSetup_, &split, 1, 12u, FFT_FORWARD);
            captureSpectrum_[node][0] += std::abs(fftReal_[0]);
            for (uint32_t bin = 1u; bin < kResonancePrintBins - 1u; ++bin) {
                captureSpectrum_[node][bin] += std::hypot(fftReal_[bin],
                    fftImag_[bin]);
            }
            captureSpectrum_[node][kResonancePrintBins - 1u]
                += std::abs(fftImag_[0]);
        }
        ++captureAnalysisFrames_;
#endif
    }

    float aggregateMagnitude(float frequency) const
    {
        const float binPosition = frequency
            * static_cast<float>(kResonancePrintAnalysisSize)
            / static_cast<float>(sampleRate_);
        const uint32_t first = std::min<uint32_t>(
            static_cast<uint32_t>(binPosition), kResonancePrintBins - 1u);
        const uint32_t second = std::min<uint32_t>(first + 1u,
            kResonancePrintBins - 1u);
        const float fraction = binPosition - std::floor(binPosition);
        float value = 0.0f;
        for (uint32_t node = 0u; node < capturePickupCount_; ++node) {
            value += lerp(captureSpectrum_[node][first],
                captureSpectrum_[node][second], fraction);
        }
        return value;
    }

    void estimateFundamental(float& frequency, float& confidence) const
    {
        frequency = 0.0f;
        confidence = 0.0f;
        float bestScore = 0.0f;
        float secondScore = 0.0f;
        float bestFrequency = 0.0f;
        constexpr uint32_t candidates = 224u;
        std::array<float, candidates> scores {};
        std::array<float, candidates> frequencies {};
        for (uint32_t candidate = 0u; candidate < candidates; ++candidate) {
            const float f0 = 40.0f * std::pow(2.0f,
                static_cast<float>(candidate) / 48.0f);
            if (f0 > 1000.0f) break;
            float score = 0.0f;
            for (uint32_t harmonic = 1u; harmonic <= 12u; ++harmonic) {
                const float partial = f0 * static_cast<float>(harmonic);
                if (partial >= sampleRate_ * 0.46) break;
                score += aggregateMagnitude(partial)
                    / std::sqrt(static_cast<float>(harmonic));
            }
            scores[candidate] = score;
            frequencies[candidate] = f0;
            if (score > bestScore) {
                bestScore = score;
                bestFrequency = f0;
            }
        }
        if (bestScore <= 1.0e-8f) return;
        for (uint32_t candidate = 0u; candidate < candidates; ++candidate) {
            if (frequencies[candidate] <= 0.0f) continue;
            const float cents = std::abs(1200.0f * std::log2(
                frequencies[candidate] / bestFrequency));
            if (cents > 120.0f) secondScore = std::max(secondScore,
                scores[candidate]);
        }
        frequency = bestFrequency;
        confidence = clamp((bestScore - secondScore)
            / std::max(bestScore, 1.0e-8f), 0.0f, 1.0f);
    }

    void finishCapture()
    {
        capturing_ = false;
        params_.printEnabled = 0u;
        ResonancePrintData next {};
        next.version = kResonancePrintVersion;
        next.capturedBody = captureBody_;
        next.pickupCount = capturePickupCount_;
        if (captureAnalysisFrames_ == 0u) {
            ++generation_;
            return;
        }
        estimateFundamental(next.fundamentalHz, next.pitchConfidence);
        bool anyModes = false;
        const float binHz = static_cast<float>(sampleRate_)
            / static_cast<float>(kResonancePrintAnalysisSize);
        for (uint32_t node = 0u; node < capturePickupCount_; ++node) {
            float maximum = 0.0f;
            for (uint32_t bin = 2u; bin < kResonancePrintBins - 1u; ++bin) {
                maximum = std::max(maximum, captureSpectrum_[node][bin]);
            }
            if (maximum <= 1.0e-9f) continue;
            const float threshold = maximum * lerp(0.22f, 0.008f,
                params_.sensitivity);
            std::array<float, kResonancePrintMaxModes> score {};
            for (uint32_t bin = 2u; bin < kResonancePrintBins - 1u; ++bin) {
                const float center = captureSpectrum_[node][bin];
                const float frequencyAtBin = static_cast<float>(bin) * binHz;
                if (frequencyAtBin < 35.0f || frequencyAtBin > 16000.0f
                    || center < threshold
                    || center <= captureSpectrum_[node][bin - 1u]
                    || center < captureSpectrum_[node][bin + 1u]) continue;
                uint32_t insert = kResonancePrintMaxModes;
                for (uint32_t slot = 0u; slot < params_.modalCount; ++slot) {
                    if (center > score[slot]) { insert = slot; break; }
                }
                if (insert >= params_.modalCount) continue;
                for (uint32_t slot = params_.modalCount - 1u; slot > insert; --slot) {
                    score[slot] = score[slot - 1u];
                    next.modes[node][slot] = next.modes[node][slot - 1u];
                }
                const float left = captureSpectrum_[node][bin - 1u];
                const float right = captureSpectrum_[node][bin + 1u];
                const float denominator = left - 2.0f * center + right;
                const float offset = std::abs(denominator) > 1.0e-12f
                    ? clamp(0.5f * (left - right) / denominator, -0.5f, 0.5f)
                    : 0.0f;
                score[insert] = center;
                next.modes[node][insert] = {
                    (static_cast<float>(bin) + offset) * binHz,
                    center / maximum,
                    clamp((center - threshold) / std::max(center, 1.0e-9f),
                        0.0f, 1.0f)
                };
                next.modeCount[node] = std::min<uint32_t>(
                    next.modeCount[node] + 1u, params_.modalCount);
            }
            float sum = 0.0f;
            for (uint32_t index = 0u; index < next.modeCount[node]; ++index) {
                sum += next.modes[node][index].amplitude;
            }
            if (sum > 1.0e-9f) {
                for (uint32_t index = 0u; index < next.modeCount[node]; ++index) {
                    next.modes[node][index].amplitude /= sum;
                }
                anyModes = true;
            }
        }
        next.valid = anyModes ? 1u : 0u;
        setPrint(next);
    }

    void releaseFft()
    {
#if S3G_HAS_RESONANCE_PRINT_FFT
        if (fftSetup_) vDSP_destroy_fftsetup(fftSetup_);
        fftSetup_ = nullptr;
#endif
    }

    double sampleRate_ = 48000.0;
    AmbiEffectResonancePrintParams params_ {};
    ResonancePrintData print_ {};
    std::array<std::array<MatrixSet, 3u>, kAmbiEffectDjFilterMaxOrder>
        matrixCache_ {};
    const MatrixSet* currentMatrix_ = nullptr;

    std::array<std::array<Resonator, kResonancePrintMaxModes>,
        kAmbiEffectDjFilterMaxPickups> resonators_ {};
    std::array<uint32_t, kAmbiEffectDjFilterMaxPickups> resonatorModeCount_ {};
    std::array<float, kAmbiEffectDjFilterMaxPickups> nodeLevel_ {};
    std::array<float, kAmbiEffectDjFilterMaxPickups> nodePrintStrength_ {};
    std::array<float, kAmbiEffectDjFilterMaxPickups> maskGain_ = [] {
        std::array<float, kAmbiEffectDjFilterMaxPickups> result {};
        result.fill(1.0f);
        return result;
    }();

    bool capturing_ = false;
    AmbiEffectBody captureBody_ = AmbiEffectBody::Icosa12;
    uint32_t capturePickupCount_ = 12u;
    uint64_t captureSampleCount_ = 0u;
    uint64_t captureTargetSamples_ = 0u;
    uint32_t captureWritePosition_ = 0u;
    uint32_t captureAnalysisFrames_ = 0u;
    std::array<std::array<float, kResonancePrintAnalysisSize>,
        kAmbiEffectDjFilterMaxPickups> captureRing_ {};
    std::array<std::array<float, kResonancePrintBins>,
        kAmbiEffectDjFilterMaxPickups> captureSpectrum_ {};
    std::array<float, kResonancePrintAnalysisSize> analysisWindow_ {};
    std::array<float, kResonancePrintAnalysisSize> fftTime_ {};
    std::array<float, kResonancePrintAnalysisSize / 2u> fftReal_ {};
    std::array<float, kResonancePrintAnalysisSize / 2u> fftImag_ {};
#if S3G_HAS_RESONANCE_PRINT_FFT
    FFTSetup fftSetup_ = nullptr;
#endif

    AmbiEffectTopology previousTopology_ = AmbiEffectTopology::Local;
    AmbiEffectTopology targetTopology_ = AmbiEffectTopology::Local;
    float topologyFade_ = 1.0f;
    float roamingPhase_ = 0.0f;
    float maximumDecaySeconds_ = 0.0f;
    uint64_t generation_ = 0u;
    uint32_t resonatorUpdateCountdown_ = 0u;
    float currentTopologyAmount_ = 0.65f;
    float currentRoamingRateHz_ = 0.08f;
    float currentMix_ = 0.55f;
    float currentOutputGain_ = 1.0f;
    float currentDrive_ = 0.35f;
    std::atomic<float> outputGainTargetDb_ { 0.0f };
    float safetyGain_ = 1.0f;
    float safetyRelease_ = 0.0002f;
    float excitationGovernorGain_ = 1.0f;
    float excitationGovernorRelease_ = 0.0001f;
    float outputGainCoefficient_ = 0.001f;
    float resonatorTargetCoefficient_ = 0.01f;
    float resonatorCoefficientSmoothing_ = 0.01f;
    float parameterCoefficient_ = 0.0012f;
    float topologyCoefficient_ = 0.0015f;
    float levelCoefficient_ = 0.003f;
};

} // namespace s3g
