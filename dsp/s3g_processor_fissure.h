#pragma once

#include "s3g_macro_shred.h"
#include "s3g_math.h"
#include "s3g_realtime.h"
#include "s3g_structural_failure.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace s3g {

inline constexpr uint32_t kProcessorFissureCells = 8u;
inline constexpr uint32_t kProcessorFissureMaxOutputs = 8u;
using ProcessorFissureMatrix = std::array<float,
    kProcessorFissureCells * kProcessorFissureCells>;

enum class ProcessorFissureAction : uint32_t {
    Cut = 0u,
    Breach,
    Withdraw,
    Reseed,
    Panic,
};

struct ProcessorFissureParams {
    float pressure = 0.62f;
    float mass = 0.78f;
    float edge = 0.46f;
    float voidAmount = 0.18f;
    float memory = 0.48f;
    float body = 0.56f;
    float voice = 0.50f;
    float motion = 0.32f;
    float contact = 0.48f;
    float shaker = 0.26f;
    float rattle = 0.42f;
    float spring = 0.34f;
    float inputGainDb = 0.0f;
    float outputGainDb = -12.0f;
    uint32_t seed = 1979u;
    bool hold = false;
    bool run = true;
};

struct ProcessorFissureObject {
    float size = 0.50f;
    float decay = 0.55f;
    float hardness = 0.50f;
    float sensitivity = 0.62f;
    float drive = 0.46f;
    float level = 1.0f;
};

// Eight logical material cells form the instrument. A contact/shaker exciter
// and lightweight modal bodies feed the same signed matrix as external audio.
// Rendering is always hosted on an eight-channel bus; the final ring renderer
// can fold the field to Stereo or Quad while leaving unused bus channels mute.
class ProcessorFissure {
public:
    void prepare(double sampleRate, uint32_t outputChannels)
    {
        sampleRate_ = std::isfinite(sampleRate)
            ? std::clamp(sampleRate, 8000.0, 768000.0) : 48000.0;
        outputChannels_ = supportedOutputChannels(outputChannels);
        const float sr = static_cast<float>(sampleRate_);
        parameterCoefficient_ = onePoleCoefficient(16.0f, sr);
        runCoefficient_ = onePoleCoefficient(7.0f, sr);
        voiceAttackCoefficient_ = onePoleCoefficient(2.0f, sr);
        voiceReleaseCoefficient_ = onePoleCoefficient(95.0f, sr);
        energyAttackCoefficient_ = onePoleCoefficient(8.0f, sr);
        energyReleaseCoefficient_ = onePoleCoefficient(230.0f, sr);
        inputRmsCoefficient_ = onePoleCoefficient(72.0f, sr);
        gateFastCoefficient_ = onePoleCoefficient(0.7f, sr);
        gateSlowCoefficient_ = onePoleCoefficient(38.0f, sr);
        cutDownCoefficient_ = onePoleCoefficient(0.22f, sr);
        cutUpCoefficient_ = onePoleCoefficient(1.15f, sr);
        fractureAttackCoefficient_ = onePoleCoefficient(3.0f, sr);
        fractureReleaseCoefficient_ = onePoleCoefficient(72.0f, sr);
        repeatCoefficient_ = onePoleCoefficient(2.4f, sr);
        cutVisualDecay_ = std::exp(-1.0f / std::max(1.0f, sr * 0.34f));
        breachDecay_ = std::exp(-1.0f / std::max(1.0f, sr * 0.38f));
        burstDecay_ = std::exp(-1.0f / std::max(1.0f, sr * 0.095f));
        contactLowCoefficient_ = frequencyCoefficient(72.0f, sr);
        pitchDcCoefficient_ = frequencyCoefficient(28.0f, sr);
        pitchLowpassCoefficient_ = frequencyCoefficient(5200.0f, sr);
        modalCoefficientSmoothing_ = onePoleCoefficient(28.0f, sr);
        pitchSustainAttackCoefficient_ = onePoleCoefficient(42.0f, sr);
        pitchSustainReleaseCoefficient_ = onePoleCoefficient(240.0f, sr);
        pitchDecimation_ = std::clamp<uint32_t>(
            static_cast<uint32_t>(std::lround(sr / 16000.0f)), 1u, 48u);
        pitchAnalysisRate_ = sr / static_cast<float>(pitchDecimation_);

        historyFrames_ = static_cast<uint32_t>(
            std::max(64.0, std::ceil(sampleRate_ * 3.0)));
        history_.assign(static_cast<std::size_t>(historyFrames_)
                * kProcessorFissureCells,
            0.0f);
        for (uint32_t cell = 0u; cell < kProcessorFissureCells; ++cell) {
            shred_[cell].prepare(sampleRate_, 0.075);
            failure_[cell].prepare(sampleRate_,
                target_.seed ^ ((cell + 1u) * 0x9e3779b9u));
        }
        reset();
    }

    void reset()
    {
        rng_ = target_.seed ? target_.seed : 1u;
        smoothed_ = target_;
        performed_ = target_;
        previousReturns_.fill(0.0f);
        nextReturns_.fill(0.0f);
        gate_.fill(1.0f);
        gateTarget_.fill(1.0f);
        cutDelayFrames_.fill(0u);
        pendingCutDelayFrames_.fill(0u);
        cutPolarity_.fill(1.0f);
        pendingCutPolarity_.fill(1.0f);
        cutSeam_.fill(1.0f);
        cutSeamTarget_.fill(1.0f);
        cutSwapPending_.fill(false);
        cutVisual_.fill(0.0f);
        grabbedValid_ = false;
        performanceCaptureActive_ = false;
        performanceFrameCount_ = 0u;
        performanceCaptureCountdown_ = 0u;
        performancePlaybackIndex_ = 0u;
        performancePlaybackCountdown_ = 0u;
        capturedCutMask_ = 0u;
        capturedStrikeMask_ = 0u;
        repeatedFrame_ = PerformanceFrame {};
        performanceFrameApplied_ = false;
        performanceControlFrames_ = std::max<uint32_t>(1u,
            static_cast<uint32_t>(sampleRate_ / 200.0));
        repeatTarget_ = false;
        repeatMix_ = 0.0f;
        fractureXSmoothed_ = fractureXTarget_;
        fractureYSmoothed_ = fractureYTarget_;
        cutRevision_ = 0u;
        grabRevision_ = 0u;
        eventCountdown_.fill(1u);
        oscillatorPhase_.fill(0.0f);
        lowNoise_.fill(0.0f);
        inputCouplingLow_.fill(0.0f);
        inputCouplingHigh_.fill(0.0f);
        energy_.fill(0.0f);
        activity_.fill(0.0f);
        governor_.fill(1.0f);
        burstEnvelope_.fill(0.0f);
        for (auto& modes : modalY1_) modes.fill(0.0f);
        for (auto& modes : modalY2_) modes.fill(0.0f);
        for (auto& modes : modalImpulse_) modes.fill(0.0f);
        for (uint32_t cell = 0u; cell < kProcessorFissureCells; ++cell) {
            cellLevelTarget_[cell] = objectTarget_[cell].level;
        }
        cellLevelSmoothed_ = cellLevelTarget_;
        breachArrival_.fill(0u);
        breachPending_.fill(false);
        historyWrite_ = 0u;
        std::fill(history_.begin(), history_.end(), 0.0f);
        voiceEnvelope_ = 0.0f;
        inputPower_ = 0.0f;
        contactLow_ = 0.0f;
        previousContactInput_ = 0.0f;
        contactActivity_ = 0.0f;
        inputTransferActivity_ = 0.0f;
        sustainedPitchDrive_ = 0.0f;
        pitchRing_.fill(0.0f);
        pitchWrite_ = 0u;
        pitchSamples_ = 0u;
        pitchAnalysisActive_ = false;
        pitchAnalysisLag_ = 0u;
        pitchAnalysisMinimumLag_ = 0u;
        pitchAnalysisMaximumLag_ = 0u;
        pitchAnalysisReferenceWrite_ = 0u;
        pitchAnalysisRecentEnergy_ = 0.0;
        pitchQuietCountdown_ = 0u;
        pitchScore_.fill(0.0f);
        pitchDecimationCounter_ = 0u;
        pitchDecimationSum_ = 0.0f;
        pitchDc_ = 0.0f;
        pitchLowpass_ = 0.0f;
        inputPower_ = 0.0f;
        trackedPitchHz_ = 0.0f;
        pitchConfidence_ = 0.0f;
        pitchRng_ = (target_.seed ^ 0xa511e9b3u) | 1u;
        for (auto& row : pitchChaos_) row.fill(0.0f);
        shakerCountdown_ = 1u;
        rotationPhase_ = 0.0f;
        breachEnvelope_ = 0.0f;
        withdrawGain_ = 1.0f;
        withdrawTarget_ = 1.0f;
        withdrawHoldRemaining_ = 0u;
        blackoutFrames_ = 0u;
        runGain_ = target_.run ? 1.0f : 0.0f;
        silenced_ = false;
        matrixRevision_ = 0u;
        regenerateMatrix(0.0f);
        for (uint32_t cell = 0u; cell < kProcessorFissureCells; ++cell) {
            shred_[cell].reset();
            failure_[cell].reset(target_.seed
                ^ ((cell + 1u) * 0x9e3779b9u));
            scheduleNextEvent(cell, true);
        }
        updateCellProcessors();
        updatePhysicalModel();
        modalA_ = modalTargetA_;
        modalB_ = modalTargetB_;
    }

    void setParams(ProcessorFissureParams params)
    {
        params = sanitize(params);
        if (params.seed != target_.seed) {
            rng_ = params.seed ? params.seed : 1u;
            for (uint32_t cell = 0u;
                 cell < kProcessorFissureCells; ++cell) {
                failure_[cell].reset(params.seed
                    ^ ((cell + 1u) * 0x9e3779b9u));
            }
        }
        target_ = params;
        updateCellProcessors();
        updatePhysicalModel();
    }

    ProcessorFissureParams params() const { return target_; }
    uint32_t outputChannels() const { return outputChannels_; }

    void setMatrix(const ProcessorFissureMatrix& matrix)
    {
        for (uint32_t index = 0u; index < matrix_.size(); ++index) {
            matrix_[index] = std::isfinite(matrix[index])
                ? clamp(matrix[index], -1.0f, 1.0f) : 0.0f;
        }
    }

    const ProcessorFissureMatrix& matrix() const { return matrix_; }

    void setMatrixRoute(uint32_t destination, uint32_t source, float gain)
    {
        if (destination >= kProcessorFissureCells
            || source >= kProcessorFissureCells) return;
        matrix_[destination * kProcessorFissureCells + source]
            = std::isfinite(gain) ? clamp(gain, -1.0f, 1.0f) : 0.0f;
    }

    float matrixRoute(uint32_t destination, uint32_t source) const
    {
        return destination < kProcessorFissureCells
                && source < kProcessorFissureCells
            ? matrix_[destination * kProcessorFissureCells + source]
            : 0.0f;
    }

    void setCellLevel(uint32_t cell, float level)
    {
        if (cell >= kProcessorFissureCells) return;
        auto object = objectTarget_[cell];
        object.level = level;
        setObject(cell, object);
    }

    float cellLevel(uint32_t cell) const
    {
        return cell < kProcessorFissureCells
            ? objectTarget_[cell].level : 0.0f;
    }

    void setObject(uint32_t cell, ProcessorFissureObject object)
    {
        if (cell >= kProcessorFissureCells) return;
        object.size = finiteUnit(object.size, 0.50f);
        object.decay = finiteUnit(object.decay, 0.55f);
        object.hardness = finiteUnit(object.hardness, 0.50f);
        object.sensitivity = finiteUnit(object.sensitivity, 0.62f);
        object.drive = finiteUnit(object.drive, 0.46f);
        object.level = finiteUnit(object.level, 1.0f);
        objectTarget_[cell] = object;
        cellLevelTarget_[cell] = object.level;
        updateCellProcessor(cell);
        updatePhysicalCell(cell);
    }

    ProcessorFissureObject object(uint32_t cell) const
    {
        return cell < kProcessorFissureCells
            ? objectTarget_[cell] : ProcessorFissureObject {};
    }

    void setOutputChannels(uint32_t outputChannels)
    {
        outputChannels_ = supportedOutputChannels(outputChannels);
    }

    void setCutVariation(float amount)
    {
        cutVariation_ = finiteUnit(amount, 0.46f);
    }

    float cutVariation() const { return cutVariation_; }

    void setCutMask(uint32_t cell, bool enabled)
    {
        if (cell >= kProcessorFissureCells) return;
        cutMask_[cell] = enabled;
        if (!enabled) {
            gateTarget_[cell] = 1.0f;
            pendingCutDelayFrames_[cell] = 0u;
            pendingCutPolarity_[cell] = 1.0f;
            cutSwapPending_[cell] = true;
            cutSeamTarget_[cell] = 0.0f;
            cutVisual_[cell] = 0.0f;
        }
    }

    bool cutMask(uint32_t cell) const
    {
        return cell < kProcessorFissureCells && cutMask_[cell];
    }

    void setFracturePerformance(float distance, float force)
    {
        distance = finiteUnit(distance, 0.0f);
        force = finiteUnit(force, 0.0f);
        if (force > fractureYTarget_ + 0.025f) {
            const uint32_t immediate = std::max<uint32_t>(1u,
                static_cast<uint32_t>(sampleRate_ * 0.006));
            for (uint32_t cell = 0u; cell < kProcessorFissureCells; ++cell) {
                if (cutMask_[cell]) {
                    eventCountdown_[cell] = std::min(
                        eventCountdown_[cell], immediate + cell * 17u);
                }
            }
        }
        fractureXTarget_ = distance;
        fractureYTarget_ = force;
    }

    float fractureDistance() const { return fractureXTarget_; }
    float fractureForce() const { return fractureYTarget_; }

    void setGrab(bool enabled)
    {
        if (enabled == performanceCaptureActive_) return;
        if (enabled) {
            repeatTarget_ = false;
            performanceCaptureActive_ = true;
            performanceFrameCount_ = 0u;
            performanceCaptureCountdown_ = 0u;
            capturedCutMask_ = 0u;
            capturedStrikeMask_ = 0u;
            grabbedValid_ = false;
            return;
        }
        capturePerformanceFrame();
        performanceCaptureActive_ = false;
        grabbedValid_ = performanceFrameCount_ > 0u;
        performancePlaybackIndex_ = 0u;
        performancePlaybackCountdown_ = 0u;
        if (grabbedValid_) ++grabRevision_;
    }

    void setRepeat(bool enabled)
    {
        if (enabled && !repeatTarget_) {
            if (!grabbedValid_ || performanceCaptureActive_) return;
            performancePlaybackIndex_ = 0u;
            performancePlaybackCountdown_ = 0u;
        }
        if (!enabled && repeatTarget_) {
            updateCellProcessors();
            updatePhysicalModel();
            performanceFrameApplied_ = false;
        }
        repeatTarget_ = enabled;
    }

    void clearPerformanceLoop()
    {
        performanceCaptureActive_ = false;
        performanceFrameCount_ = 0u;
        performanceCaptureCountdown_ = 0u;
        performancePlaybackIndex_ = 0u;
        performancePlaybackCountdown_ = 0u;
        capturedCutMask_ = 0u;
        capturedStrikeMask_ = 0u;
        grabbedValid_ = false;
        repeatTarget_ = false;
        repeatMix_ = 0.0f;
        performanceFrameApplied_ = false;
        repeatedFrame_ = PerformanceFrame {};
        updateCellProcessors();
        updatePhysicalModel();
    }

    bool repeat() const { return repeatTarget_; }
    bool grabbing() const { return performanceCaptureActive_; }
    bool grabbed() const { return grabbedValid_; }
    float repeatMix() const { return repeatMix_; }
    float grabDurationSeconds() const
    {
        return static_cast<float>(performanceFrameCount_
            * performanceControlFrames_) / static_cast<float>(sampleRate_);
    }
    float repeatPhase() const
    {
        return performanceFrameCount_ > 0u
            ? static_cast<float>(performancePlaybackIndex_)
                / static_cast<float>(performanceFrameCount_)
            : 0.0f;
    }

    uint32_t performanceFrameCount() const { return performanceFrameCount_; }
    ProcessorFissureParams performanceParams() const { return performed_; }

    void rearm(float strength = 0.62f)
    {
        strength = clamp(std::isfinite(strength) ? strength : 0.62f,
            0.0f, 1.0f);
        const bool wasSilenced = silenced_;
        silenced_ = false;
        withdrawGain_ = wasSilenced ? 1.0f : std::max(withdrawGain_, 0.18f);
        withdrawTarget_ = 1.0f;
        withdrawHoldRemaining_ = 0u;
        blackoutFrames_ = 0u;
        fractureXTarget_ = 0.0f;
        fractureYTarget_ = 0.0f;
        repeatTarget_ = false;
        repeatMix_ = 0.0f;
        for (uint32_t cell = 0u; cell < kProcessorFissureCells; ++cell) {
            gate_[cell] = std::max(gate_[cell], 0.18f);
            gateTarget_[cell] = 1.0f;
            pendingCutDelayFrames_[cell] = 0u;
            pendingCutPolarity_[cell] = 1.0f;
            cutSwapPending_[cell] = true;
            cutSeamTarget_[cell] = 0.0f;
            cutVisual_[cell] = 0.0f;
            scheduleNextEvent(cell, true);
        }
        strikeCell(0u, strength);
        strikeCell(4u, strength * 0.72f);
    }

    void strikeCell(uint32_t cell, float velocity = 1.0f)
    {
        if (cell >= kProcessorFissureCells || !std::isfinite(velocity)) {
            return;
        }
        velocity = clamp(velocity, 0.0f, 1.0f);
        if (performanceCaptureActive_) {
            capturedStrikeMask_ |= static_cast<uint8_t>(1u << cell);
        }
        if (silenced_) {
            withdrawGain_ = 1.0f;
            withdrawTarget_ = 1.0f;
            withdrawHoldRemaining_ = 0u;
            gate_[cell] = 1.0f;
        }
        silenced_ = false;
        gateTarget_[cell] = 1.0f;
        burstEnvelope_[cell] = std::max(
            burstEnvelope_[cell], 0.18f + velocity * 0.82f);
        excitePhysical(cell, 0.24f + velocity * 0.76f);
        failure_[cell].excite(velocity, false);
    }

    void trigger(ProcessorFissureAction action, uint32_t seed = 0u)
    {
        switch (action) {
        case ProcessorFissureAction::Cut:
            silenced_ = false;
            for (uint32_t cell = 0u;
                 cell < kProcessorFissureCells; ++cell) {
                if (!cutMask_[cell]) continue;
                requestCutSplice(cell, 0.55f + cutVariation_ * 0.45f);
                const float emptyProbability = 0.08f
                    + target_.voidAmount * 0.72f;
                gateTarget_[cell] = randomUnit() < emptyProbability
                    ? 0.0f : 1.0f;
                burstEnvelope_[cell] = std::max(
                    burstEnvelope_[cell], 0.16f + target_.edge * 0.52f);
                excitePhysical(cell, 0.16f + target_.edge * 0.48f);
                scheduleNextEvent(cell, false);
            }
            break;
        case ProcessorFissureAction::Breach: {
            silenced_ = false;
            breachEnvelope_ = 1.0f;
            const uint32_t step = static_cast<uint32_t>(sampleRate_
                * (0.004 + target_.motion * 0.055));
            for (uint32_t cell = 0u;
                 cell < kProcessorFissureCells; ++cell) {
                breachArrival_[cell] = cell * step;
                breachPending_[cell] = true;
                gateTarget_[cell] = 1.0f;
            }
            break;
        }
        case ProcessorFissureAction::Withdraw:
            silenced_ = false;
            withdrawTarget_ = 0.0f;
            withdrawHoldRemaining_ = static_cast<uint32_t>(sampleRate_
                * (0.16 + target_.voidAmount * target_.voidAmount * 3.8));
            break;
        case ProcessorFissureAction::Reseed:
            reseed(seed ? seed : nextRandom());
            break;
        case ProcessorFissureAction::Panic:
            panic();
            break;
        }
    }

    void reseed(uint32_t seed)
    {
        seed = seed ? seed : 1u;
        target_.seed = seed;
        smoothed_.seed = seed;
        rng_ = seed;
        pitchRng_ = (seed ^ 0xa511e9b3u) | 1u;
        silenced_ = false;
        withdrawGain_ = 1.0f;
        withdrawTarget_ = 1.0f;
        withdrawHoldRemaining_ = 0u;
        blackoutFrames_ = 0u;
        cutDelayFrames_.fill(0u);
        pendingCutDelayFrames_.fill(0u);
        cutPolarity_.fill(1.0f);
        pendingCutPolarity_.fill(1.0f);
        cutSeam_.fill(1.0f);
        cutSeamTarget_.fill(1.0f);
        cutSwapPending_.fill(false);
        cutVisual_.fill(0.0f);
        grabbedValid_ = false;
        performanceCaptureActive_ = false;
        performanceFrameCount_ = 0u;
        performanceCaptureCountdown_ = 0u;
        performancePlaybackIndex_ = 0u;
        performancePlaybackCountdown_ = 0u;
        repeatTarget_ = false;
        repeatMix_ = 0.0f;
        regenerateMatrix(0.24f);
        for (uint32_t cell = 0u; cell < kProcessorFissureCells; ++cell) {
            gate_[cell] = 1.0f;
            gateTarget_[cell] = 1.0f;
            failure_[cell].reset(seed ^ ((cell + 1u) * 0x9e3779b9u));
            failure_[cell].excite(0.28f + 0.06f * static_cast<float>(cell),
                false);
            burstEnvelope_[cell] = 0.18f + randomUnit() * 0.22f;
            scheduleNextEvent(cell, true);
        }
    }

    void panic()
    {
        previousReturns_.fill(0.0f);
        nextReturns_.fill(0.0f);
        gate_.fill(0.0f);
        gateTarget_.fill(0.0f);
        cutDelayFrames_.fill(0u);
        pendingCutDelayFrames_.fill(0u);
        cutPolarity_.fill(1.0f);
        pendingCutPolarity_.fill(1.0f);
        cutSeam_.fill(1.0f);
        cutSeamTarget_.fill(1.0f);
        cutSwapPending_.fill(false);
        cutVisual_.fill(0.0f);
        grabbedValid_ = false;
        performanceCaptureActive_ = false;
        performanceFrameCount_ = 0u;
        performanceCaptureCountdown_ = 0u;
        performancePlaybackIndex_ = 0u;
        performancePlaybackCountdown_ = 0u;
        repeatTarget_ = false;
        repeatMix_ = 0.0f;
        energy_.fill(0.0f);
        activity_.fill(0.0f);
        burstEnvelope_.fill(0.0f);
        for (auto& modes : modalY1_) modes.fill(0.0f);
        for (auto& modes : modalY2_) modes.fill(0.0f);
        for (auto& modes : modalImpulse_) modes.fill(0.0f);
        breachPending_.fill(false);
        std::fill(history_.begin(), history_.end(), 0.0f);
        historyWrite_ = 0u;
        breachEnvelope_ = 0.0f;
        contactActivity_ = 0.0f;
        inputTransferActivity_ = 0.0f;
        sustainedPitchDrive_ = 0.0f;
        pitchConfidence_ = 0.0f;
        trackedPitchHz_ = 0.0f;
        pitchRing_.fill(0.0f);
        pitchWrite_ = 0u;
        pitchSamples_ = 0u;
        pitchAnalysisActive_ = false;
        pitchAnalysisLag_ = 0u;
        pitchAnalysisMinimumLag_ = 0u;
        pitchAnalysisMaximumLag_ = 0u;
        pitchAnalysisReferenceWrite_ = 0u;
        pitchAnalysisRecentEnergy_ = 0.0;
        pitchQuietCountdown_ = 0u;
        pitchScore_.fill(0.0f);
        pitchDecimationCounter_ = 0u;
        pitchDecimationSum_ = 0.0f;
        pitchDc_ = 0.0f;
        pitchLowpass_ = 0.0f;
        inputCouplingLow_.fill(0.0f);
        inputCouplingHigh_.fill(0.0f);
        withdrawGain_ = 0.0f;
        withdrawTarget_ = 0.0f;
        withdrawHoldRemaining_ = 0u;
        blackoutFrames_ = 0u;
        for (auto& core : shred_) core.reset();
        silenced_ = true;
    }

    void processFrame(const float* stereoInput, float* output)
    {
        processFrame(stereoInput, output, outputChannels_);
    }

    void processFrame(const float* stereoInput, float* output,
        uint32_t outputChannels)
    {
        if (!output) return;
        outputChannels = supportedOutputChannels(outputChannels);
        std::fill(output, output + outputChannels, 0.0f);
        smoothParameters();
        const float fractureXCoefficient = fractureXTarget_
                > fractureXSmoothed_
            ? fractureAttackCoefficient_ : fractureReleaseCoefficient_;
        const float fractureYCoefficient = fractureYTarget_
                > fractureYSmoothed_
            ? fractureAttackCoefficient_ : fractureReleaseCoefficient_;
        fractureXSmoothed_ += (fractureXTarget_ - fractureXSmoothed_)
            * fractureXCoefficient;
        fractureYSmoothed_ += (fractureYTarget_ - fractureYSmoothed_)
            * fractureYCoefficient;
        advancePerformanceTape();
        repeatMix_ += ((repeatTarget_ ? 1.0f : 0.0f) - repeatMix_)
            * repeatCoefficient_;
        updatePerformedParams();
        if (performanceFrameApplied_) {
            updateCellProcessors(performed_);
            updatePhysicalModel(performed_);
            performanceFrameApplied_ = false;
        }
        runGain_ += ((performed_.run ? 1.0f : 0.0f) - runGain_)
            * runCoefficient_;
        if (silenced_) return;

        if (withdrawHoldRemaining_ > 0u) {
            --withdrawHoldRemaining_;
            if (withdrawHoldRemaining_ == 0u) withdrawTarget_ = 1.0f;
        }
        const float withdrawCoefficient = withdrawTarget_ < withdrawGain_
            ? gateFastCoefficient_ : gateSlowCoefficient_;
        withdrawGain_ += (withdrawTarget_ - withdrawGain_)
            * withdrawCoefficient;
        breachEnvelope_ = flushDenormal(breachEnvelope_ * breachDecay_);

        const float inputLeft = stereoInput && std::isfinite(stereoInput[0])
            ? stereoInput[0] : 0.0f;
        const float inputRight = stereoInput && std::isfinite(stereoInput[1])
            ? stereoInput[1] : 0.0f;
        const float inputGain = dbToGain(performed_.inputGainDb);
        const float contactInput = 0.5f * (inputLeft + inputRight)
            * inputGain;
        advancePitchTracker(contactInput);
        contactLow_ += (contactInput - contactLow_) * contactLowCoefficient_;
        const float contactHigh = contactInput - contactLow_;
        const float contactDerivative = contactInput - previousContactInput_;
        previousContactInput_ = contactInput;
        const float contactSignal = std::tanh((contactHigh * 2.4f
                + contactDerivative * (5.0f + performed_.edge * 13.0f))
            * performed_.contact);
        const float contactMagnitude = std::abs(contactSignal);
        contactActivity_ += (contactMagnitude - contactActivity_)
            * (contactMagnitude > contactActivity_
                ? energyAttackCoefficient_ : voiceReleaseCoefficient_);
        contactActivity_ = flushDenormal(std::max(0.0f, contactActivity_));
        const float inputMagnitude = std::max(
            std::abs(inputLeft), std::abs(inputRight)) * inputGain;
        const float inputMeanSquare = 0.5f
            * (inputLeft * inputLeft + inputRight * inputRight)
            * inputGain * inputGain;
        inputPower_ += (inputMeanSquare - inputPower_)
            * inputRmsCoefficient_;
        inputPower_ = flushDenormal(std::max(0.0f, inputPower_));
        const float inputRms = std::sqrt(inputPower_);
        const float inputOnset = std::max(0.0f,
            inputMagnitude - voiceEnvelope_);
        voiceEnvelope_ += (inputMagnitude - voiceEnvelope_)
            * (inputMagnitude > voiceEnvelope_
                ? voiceAttackCoefficient_ : voiceReleaseCoefficient_);
        voiceEnvelope_ = flushDenormal(std::max(0.0f, voiceEnvelope_));
        const float pitchLock = clamp((pitchConfidence_ - 0.08f) / 0.72f,
            0.0f, 1.0f);
        const float sustainedPitchTarget = performed_.voice * pitchLock
            * clamp(inputRms * 6.2f, 0.0f, 1.0f);
        sustainedPitchDrive_ += (sustainedPitchTarget
                - sustainedPitchDrive_)
            * (sustainedPitchTarget > sustainedPitchDrive_
                ? pitchSustainAttackCoefficient_
                : pitchSustainReleaseCoefficient_);
        sustainedPitchDrive_ = flushDenormal(
            std::max(0.0f, sustainedPitchDrive_));
        // Input Coupling is intentionally not a dry or parallel audio path.
        // A de-meaned transient signal and its envelope seed resonant bodies,
        // matrix returns, and feedback pressure. Every audible contribution
        // therefore crosses the physical/network processors first.
        const float couplingSignal = std::tanh(contactHigh * 1.8f
            + contactDerivative * (2.0f + performed_.edge * 5.0f));
        const float couplingEnergy = performed_.voice * clamp(
            voiceEnvelope_ * 3.2f + inputOnset * 1.4f, 0.0f, 1.0f);
        const float transferMagnitude = std::max(contactMagnitude,
            std::max(std::abs(couplingSignal) * performed_.voice * 0.72f,
                std::max(couplingEnergy * 0.54f,
                    sustainedPitchDrive_ * 0.78f)));
        inputTransferActivity_ += (transferMagnitude
                - inputTransferActivity_)
            * (transferMagnitude > inputTransferActivity_
                ? energyAttackCoefficient_ : voiceReleaseCoefficient_);
        inputTransferActivity_ = flushDenormal(
            std::max(0.0f, inputTransferActivity_));

        rotationPhase_ = wrapPhase(rotationPhase_
            + (0.003f + performed_.motion * performed_.motion * 0.31f)
                / static_cast<float>(sampleRate_));

        advanceShaker();

        for (uint32_t cell = 0u; cell < kProcessorFissureCells; ++cell) {
            advanceEvents(cell);
            advanceBreach(cell);
            cellLevelSmoothed_[cell] += (cellLevelTarget_[cell]
                    - cellLevelSmoothed_[cell])
                * parameterCoefficient_;
        }

        if (!performed_.hold) {
            float activeGate = 0.0f;
            float activeMass = 0.0f;
            for (uint32_t cell = 0u; cell < kProcessorFissureCells; ++cell) {
                const float active = cellActiveAmount(cell);
                activeMass += active;
                activeGate += active * gate_[cell];
            }
            if (activeMass > 0.0f && activeGate < activeMass * 0.025f) {
                ++blackoutFrames_;
                if (blackoutFrames_ >= static_cast<uint32_t>(
                        sampleRate_ * 0.85)) {
                    for (uint32_t cell = 0u;
                         cell < kProcessorFissureCells; ++cell) {
                        if (cellActiveAmount(cell) <= 0.0f) continue;
                        gate_[cell] = std::max(gate_[cell], 0.16f);
                        gateTarget_[cell] = 1.0f;
                        burstEnvelope_[cell] = std::max(
                            burstEnvelope_[cell], 0.28f);
                        excitePhysical(cell, 0.34f);
                        scheduleNextEvent(cell, false);
                        break;
                    }
                    blackoutFrames_ = 0u;
                }
            } else {
                blackoutFrames_ = 0u;
            }
        } else {
            blackoutFrames_ = 0u;
        }

        const float internalBlend = 1.0f - couplingEnergy * 0.16f;
        const float feedbackGain = 0.34f
            + performed_.pressure * 0.78f
            + couplingEnergy * (0.04f + performed_.voice * 0.12f);
        const float historyGain = performed_.memory * 0.58f
            + couplingEnergy * performed_.memory * 0.08f;
        const float sourceGain = 0.055f
            + performed_.mass * 0.22f;
        for (uint32_t cell = 0u; cell < kProcessorFissureCells; ++cell) {
            const float active = cellActiveAmount(cell);
            const float white = randomSigned();
            const float lowFrequency = 35.0f
                * std::pow(34.0f, (static_cast<float>(cell) + 0.5f)
                        / static_cast<float>(kProcessorFissureCells)
                    * (0.42f + performed_.body * 0.58f));
            lowNoise_[cell] += (white - lowNoise_[cell])
                * frequencyCoefficient(lowFrequency,
                    static_cast<float>(sampleRate_));
            const float brightNoise = white - lowNoise_[cell];

            static constexpr std::array<float,
                kProcessorFissureCells> baseFrequencies {{
                    31.0f, 47.0f, 79.0f, 131.0f,
                    223.0f, 389.0f, 947.0f, 2269.0f,
                }};
            const float oscillatorFrequency = baseFrequencies[cell]
                * std::exp2((performed_.body - 0.5f) * 1.25f
                    + (0.5f - objectTarget_[cell].size) * 2.25f);
            oscillatorPhase_[cell] = wrapPhase(oscillatorPhase_[cell]
                + oscillatorFrequency / static_cast<float>(sampleRate_));
            const float sine = std::sin(
                oscillatorPhase_[cell] * 2.0f * kPi);
            const float pulse = std::tanh(sine
                * (1.2f + performed_.edge * 7.0f));
            const float tonal = lerp(sine, pulse, performed_.edge * 0.74f);
            const float noise = lerp(lowNoise_[cell], brightNoise,
                clamp(0.18f + performed_.edge * 0.72f
                        + static_cast<float>(cell) * 0.025f,
                    0.0f, 1.0f));

            StructuralFailureParams failureParams;
            failureParams.drive = clamp(0.025f
                + performed_.mass * 0.10f
                + breachEnvelope_ * 0.72f, 0.0f, 1.0f);
            failureParams.motion = performed_.edge;
            failureParams.hierarchy = static_cast<float>(cell)
                / static_cast<float>(kProcessorFissureCells - 1u);
            failureParams.stiffness = clamp(0.18f
                    + objectTarget_[cell].hardness * 0.76f
                    - performed_.body * 0.24f,
                0.0f, 1.0f);
            failureParams.toughness = clamp(0.18f
                    + performed_.memory * 0.48f
                    + objectTarget_[cell].decay * 0.30f,
                0.0f, 1.0f);
            failureParams.damage = clamp(performed_.pressure * 0.72f
                + breachEnvelope_ * 0.45f, 0.0f, 1.0f);
            failureParams.highDetail = performed_.edge;
            failureParams.consequence = breachEnvelope_;
            failureParams.mass = performed_.body;
            failureParams.mode = (cell & 1u) == 0u
                ? StructuralConsequence::PlateRupture
                : StructuralConsequence::HingeFall;
            const auto fracture = failure_[cell].process(failureParams);
            const float material = fracture.flex * 0.38f
                + fracture.crack * 0.52f + fracture.snap * 0.58f
                + fracture.rupture * 0.46f + fracture.fall * 0.32f
                + fracture.impact * 0.58f;

            const float angle = cellAngle(cell);
            inputCouplingLow_[cell] += (couplingSignal
                    - inputCouplingLow_[cell])
                * inputCouplingLowCoefficient_[cell];
            inputCouplingHigh_[cell] += (couplingSignal
                    - inputCouplingHigh_[cell])
                * inputCouplingHighCoefficient_[cell];
            const float embeddedInputEnergy = std::abs(std::tanh(
                (inputCouplingHigh_[cell] - inputCouplingLow_[cell])
                    * (1.4f + objectTarget_[cell].hardness * 1.8f)));
            // The microphone contributes timing and energy, while the cell's
            // own material noise supplies polarity and fine structure. This
            // prevents Input Coupling from becoming a filtered foreground
            // copy of the source.
            const float sustainedCarrier = sustainedPitchDrive_
                * (0.08f + objectTarget_[cell].sensitivity * 0.18f);
            const float embeddedExcitation = clamp(
                    embeddedInputEnergy + sustainedCarrier, 0.0f, 1.0f)
                * white;

            float modal = 0.0f;
            for (uint32_t mode = 0u; mode < kModalCount; ++mode) {
                modalA_[cell][mode] += (modalTargetA_[cell][mode]
                        - modalA_[cell][mode])
                    * modalCoefficientSmoothing_;
                modalB_[cell][mode] += (modalTargetB_[cell][mode]
                        - modalB_[cell][mode])
                    * modalCoefficientSmoothing_;
                const float contactDrive = contactSignal
                    * (0.004f + performed_.contact * 0.018f)
                    * (0.18f + objectTarget_[cell].sensitivity * 1.36f)
                    * (1.0f + static_cast<float>(
                        (cell * 5u + mode * 3u) % 7u) * 0.045f);
                const float couplingDrive = embeddedExcitation
                    * performed_.voice
                    * (0.0012f + static_cast<float>(mode) * 0.0007f)
                    * (0.20f + objectTarget_[cell].sensitivity * 1.10f)
                    * (mode == 2u ? 1.0f + performed_.spring * 0.7f : 1.0f);
                float state = modalA_[cell][mode] * modalY1_[cell][mode]
                    + modalB_[cell][mode] * modalY2_[cell][mode]
                    + modalImpulse_[cell][mode] + contactDrive
                    + couplingDrive;
                modalImpulse_[cell][mode] = 0.0f;
                if (!std::isfinite(state)) state = 0.0f;
                state = clamp(state, -3.0f, 3.0f);
                modalY2_[cell][mode] = modalY1_[cell][mode];
                modalY1_[cell][mode] = state;
                const float modeMix = mode == 2u
                    ? (0.10f + performed_.spring * 0.40f)
                        * (0.28f + objectTarget_[cell].hardness * 1.10f)
                    : (0.26f - static_cast<float>(mode) * 0.045f)
                        * (1.18f - objectTarget_[cell].hardness * 0.36f);
                modal += state * modeMix;
            }

            float feedback = 0.0f;
            float routeWeight = 0.0f;
            for (uint32_t source = 0u;
                 source < kProcessorFissureCells; ++source) {
                float route = matrix_[cell * kProcessorFissureCells + source];
                if (performed_.motion > 1.0e-5f) {
                    const float routePhase = rotationPhase_
                        + static_cast<float>(cell * 7u + source * 3u)
                            / 29.0f;
                    route *= 1.0f + std::sin(routePhase * 2.0f * kPi)
                        * performed_.motion * 0.32f;
                }
                feedback += previousReturns_[source] * route;
                routeWeight += std::abs(route);
            }
            feedback /= std::sqrt(std::max(1.0f, routeWeight * 0.74f));

            const uint32_t delay = cutDelayFrames_[cell] > 0u
                ? cutDelayFrames_[cell] : historyDelayFrames(cell);
            const uint32_t read = historyWrite_ >= delay
                ? historyWrite_ - delay
                : historyFrames_ - (delay - historyWrite_);
            const float ordinaryRemembered = history_[
                    static_cast<std::size_t>(cell)
                    * historyFrames_ + read]
                * cutPolarity_[cell] * cutSeam_[cell];
            const float fresh = active * gate_[cell] * (
                ((noise * (0.72f - performed_.body * 0.24f)
                    + tonal * (0.08f + performed_.body * 0.34f))
                        * sourceGain * internalBlend)
                + material * (0.20f + performed_.edge * 0.30f
                    + objectTarget_[cell].hardness * 0.34f)
                + white * burstEnvelope_[cell]
                    * (0.18f + performed_.pressure * 0.44f)
                + modal * (0.20f + performed_.body * 0.42f));
            float networkInput = fresh
                + feedback * feedbackGain
                + ordinaryRemembered * historyGain;
            networkInput *= 0.48f + objectTarget_[cell].drive * 1.34f;
            networkInput *= 1.0f + breachEnvelope_ * 1.8f;
            networkInput = clamp(networkInput, -6.0f, 6.0f);

            float value = shred_[cell].processSample(networkInput);
            if (!std::isfinite(value)) {
                shred_[cell].reset();
                value = 0.0f;
            }
            value = clamp(value, -4.0f, 4.0f);
            const float energyInput = value * value;
            energy_[cell] += (energyInput - energy_[cell])
                * (energyInput > energy_[cell]
                    ? energyAttackCoefficient_ : energyReleaseCoefficient_);
            const float rms = std::sqrt(std::max(0.0f, energy_[cell]));
            const float threshold = 0.32f
                - performed_.pressure * 0.10f;
            const float excess = std::max(0.0f, rms - threshold);
            const float targetGovernor = 1.0f
                / (1.0f + excess * 1.8f + excess * excess * 8.0f);
            governor_[cell] += (targetGovernor - governor_[cell])
                * (targetGovernor < governor_[cell]
                    ? energyAttackCoefficient_ : energyReleaseCoefficient_);
            governor_[cell] = clamp(governor_[cell], 0.10f, 1.0f);

            const float returned = flushDenormal(
                std::tanh(value) * governor_[cell] * gate_[cell]
                    * cellLevelSmoothed_[cell]);
            const float couplingPolarity = (cell & 1u) == 0u ? 1.0f : -1.0f;
            const float couplingSeed = embeddedExcitation * couplingPolarity
                * performed_.voice
                * (0.0015f + objectTarget_[cell].sensitivity * 0.0055f)
                * (0.28f + couplingEnergy * 0.72f);
            nextReturns_[cell] = flushDenormal(clamp(
                returned + couplingSeed, -1.0f, 1.0f));
            history_[static_cast<std::size_t>(cell) * historyFrames_
                + historyWrite_] = nextReturns_[cell];
            activity_[cell] += (rms - activity_[cell]) * 0.0025f;
            renderCell(returned, angle, output, outputChannels);
            burstEnvelope_[cell] = flushDenormal(
                burstEnvelope_[cell] * burstDecay_);
        }

        previousReturns_ = nextReturns_;
        historyWrite_ = (historyWrite_ + 1u) % historyFrames_;

        // The end-stop is a true mute. This is also an important host-routing
        // diagnostic: no live input may emerge from this processor when OUT
        // is parked at its minimum.
        const float outputGain = (target_.outputGainDb <= -59.5f
                ? 0.0f : dbToGain(performed_.outputGainDb))
            * runGain_ * withdrawGain_
            / std::sqrt(static_cast<float>(kProcessorFissureCells) * 0.62f);
        for (uint32_t channel = 0u; channel < outputChannels; ++channel) {
            const float sample = output[channel] * outputGain;
            output[channel] = std::isfinite(sample)
                ? transparentLimit(sample) : 0.0f;
        }
    }

    float cellActivity(uint32_t cell) const
    {
        return cell < kProcessorFissureCells
            ? clamp(activity_[cell] * 1.7f, 0.0f, 1.0f) : 0.0f;
    }

    float minimumGovernor() const
    {
        float value = 1.0f;
        for (float governor : governor_) value = std::min(value, governor);
        return value;
    }

    float contactActivity() const
    {
        return clamp(contactActivity_ * 1.8f, 0.0f, 1.0f);
    }

    float inputTransferActivity() const
    {
        return clamp(inputTransferActivity_ * 1.55f, 0.0f, 1.0f);
    }

    float inputLevelActivity() const
    {
        return clamp(std::sqrt(std::max(0.0f, inputPower_)), 0.0f, 4.0f);
    }

    float inputPeakActivity() const
    {
        return clamp(voiceEnvelope_, 0.0f, 4.0f);
    }

    float sustainedPitchDrive() const
    {
        return clamp(sustainedPitchDrive_, 0.0f, 1.0f);
    }

    float detectedPitchHz() const { return trackedPitchHz_; }
    float pitchConfidence() const { return pitchConfidence_; }

    float modalTargetFrequencyHz(uint32_t cell, uint32_t mode) const
    {
        return cell < kProcessorFissureCells && mode < kModalCount
            ? modalTargetFrequency_[cell][mode] : 0.0f;
    }

    bool silenced() const { return silenced_; }
    uint32_t matrixRevision() const { return matrixRevision_; }
    uint32_t cutRevision() const { return cutRevision_; }
    uint32_t grabRevision() const { return grabRevision_; }
    uint32_t cutDelayFrames(uint32_t cell) const
    {
        return cell < kProcessorFissureCells ? cutDelayFrames_[cell] : 0u;
    }

    float cutActivity(uint32_t cell) const
    {
        return cell < kProcessorFissureCells
            ? clamp(cutVisual_[cell], 0.0f, 1.0f) : 0.0f;
    }

    float cutPolarity(uint32_t cell) const
    {
        return cell < kProcessorFissureCells ? cutPolarity_[cell] : 1.0f;
    }

    float cutFragmentAge(uint32_t cell) const
    {
        if (cell >= kProcessorFissureCells || cutDelayFrames_[cell] == 0u) {
            return 0.0f;
        }
        const float seconds = static_cast<float>(cutDelayFrames_[cell])
            / static_cast<float>(sampleRate_);
        return clamp(std::log(std::max(0.002f, seconds) / 0.002f)
            / std::log(3.0f / 0.002f), 0.0f, 1.0f);
    }

private:
    struct PerformanceFrame {
        float pressure = 0.62f;
        float mass = 0.78f;
        float edge = 0.46f;
        float voidAmount = 0.18f;
        float memory = 0.48f;
        float body = 0.56f;
        float voice = 0.50f;
        float motion = 0.32f;
        float contact = 0.48f;
        float shaker = 0.26f;
        float rattle = 0.42f;
        float spring = 0.34f;
        float variation = 0.46f;
        float fractureX = 0.0f;
        float fractureY = 0.0f;
        uint8_t enabledMask = 0xffu;
        uint8_t gateMask = 0xffu;
        uint8_t cutEventMask = 0u;
        uint8_t strikeEventMask = 0u;
    };

    static constexpr uint32_t kPerformanceFrameCapacity = 1600u;

    uint8_t currentEnabledMask() const
    {
        uint8_t result = 0u;
        for (uint32_t cell = 0u; cell < kProcessorFissureCells; ++cell) {
            if (cutMask_[cell]) result |= static_cast<uint8_t>(1u << cell);
        }
        return result;
    }

    uint8_t currentGateMask() const
    {
        uint8_t result = 0u;
        for (uint32_t cell = 0u; cell < kProcessorFissureCells; ++cell) {
            if (gateTarget_[cell] >= 0.5f) {
                result |= static_cast<uint8_t>(1u << cell);
            }
        }
        return result;
    }

    void capturePerformanceFrame()
    {
        if (!performanceCaptureActive_
            || performanceFrameCount_ >= kPerformanceFrameCapacity) return;
        auto& frame = performanceFrames_[performanceFrameCount_++];
        frame.pressure = target_.pressure;
        frame.mass = target_.mass;
        frame.edge = target_.edge;
        frame.voidAmount = target_.voidAmount;
        frame.memory = target_.memory;
        frame.body = target_.body;
        frame.voice = target_.voice;
        frame.motion = target_.motion;
        frame.contact = target_.contact;
        frame.shaker = target_.shaker;
        frame.rattle = target_.rattle;
        frame.spring = target_.spring;
        frame.variation = cutVariation_;
        frame.fractureX = fractureXTarget_;
        frame.fractureY = fractureYTarget_;
        frame.enabledMask = currentEnabledMask();
        frame.gateMask = currentGateMask();
        frame.cutEventMask = capturedCutMask_;
        frame.strikeEventMask = capturedStrikeMask_;
        capturedCutMask_ = 0u;
        capturedStrikeMask_ = 0u;
    }

    void applyPerformanceFrame(const PerformanceFrame& frame)
    {
        repeatedFrame_ = frame;
        performanceFrameApplied_ = true;
        for (uint32_t cell = 0u; cell < kProcessorFissureCells; ++cell) {
            const uint8_t bit = static_cast<uint8_t>(1u << cell);
            if (!cutMask_[cell] || (frame.enabledMask & bit) == 0u) continue;
            gateTarget_[cell] = (frame.gateMask & bit) != 0u ? 1.0f : 0.0f;
            if ((frame.cutEventMask & bit) != 0u) {
                requestCutSplice(cell, 0.76f);
            }
            if ((frame.strikeEventMask & bit) != 0u) strikeCell(cell, 0.72f);
        }
    }

    void advancePerformanceTape()
    {
        if (performanceCaptureActive_) {
            if (performanceCaptureCountdown_ == 0u) {
                capturePerformanceFrame();
                performanceCaptureCountdown_ = performanceControlFrames_;
            }
            --performanceCaptureCountdown_;
        }
        if (!repeatTarget_ || !grabbedValid_
            || performanceFrameCount_ == 0u) return;
        if (performancePlaybackCountdown_ == 0u) {
            applyPerformanceFrame(
                performanceFrames_[performancePlaybackIndex_]);
            performancePlaybackIndex_ = (performancePlaybackIndex_ + 1u)
                % performanceFrameCount_;
            performancePlaybackCountdown_ = performanceControlFrames_;
        }
        --performancePlaybackCountdown_;
    }

    void updatePerformedParams()
    {
        performed_ = smoothed_;
        if (!grabbedValid_ || repeatMix_ <= 1.0e-5f) return;
        const auto blend = [this](float live, float looped) {
            return lerp(live, looped, repeatMix_);
        };
        performed_.pressure = blend(smoothed_.pressure,
            repeatedFrame_.pressure);
        performed_.mass = blend(smoothed_.mass, repeatedFrame_.mass);
        performed_.edge = blend(smoothed_.edge, repeatedFrame_.edge);
        performed_.voidAmount = blend(smoothed_.voidAmount,
            repeatedFrame_.voidAmount);
        performed_.memory = blend(smoothed_.memory, repeatedFrame_.memory);
        performed_.body = blend(smoothed_.body, repeatedFrame_.body);
        performed_.voice = blend(smoothed_.voice, repeatedFrame_.voice);
        performed_.motion = blend(smoothed_.motion, repeatedFrame_.motion);
        performed_.contact = blend(smoothed_.contact, repeatedFrame_.contact);
        performed_.shaker = blend(smoothed_.shaker, repeatedFrame_.shaker);
        performed_.rattle = blend(smoothed_.rattle, repeatedFrame_.rattle);
        performed_.spring = blend(smoothed_.spring, repeatedFrame_.spring);
    }

    static ProcessorFissureParams sanitize(ProcessorFissureParams params)
    {
        params.pressure = finiteUnit(params.pressure, 0.62f);
        params.mass = finiteUnit(params.mass, 0.78f);
        params.edge = finiteUnit(params.edge, 0.46f);
        params.voidAmount = finiteUnit(params.voidAmount, 0.18f);
        params.memory = finiteUnit(params.memory, 0.48f);
        params.body = finiteUnit(params.body, 0.56f);
        params.voice = finiteUnit(params.voice, 0.50f);
        params.motion = finiteUnit(params.motion, 0.32f);
        params.contact = finiteUnit(params.contact, 0.48f);
        params.shaker = finiteUnit(params.shaker, 0.26f);
        params.rattle = finiteUnit(params.rattle, 0.42f);
        params.spring = finiteUnit(params.spring, 0.34f);
        params.inputGainDb = finiteClamp(params.inputGainDb,
            -60.0f, 24.0f, 0.0f);
        params.outputGainDb = finiteClamp(params.outputGainDb,
            -60.0f, 6.0f, -12.0f);
        params.seed = std::clamp<uint32_t>(params.seed, 1u, 0x00ffffffu);
        return params;
    }

    static float finiteUnit(float value, float fallback)
    {
        return clamp(std::isfinite(value) ? value : fallback, 0.0f, 1.0f);
    }

    float cellActiveAmount(uint32_t cell) const
    {
        static constexpr std::array<uint32_t,
            kProcessorFissureCells> massOrder {{
                0u, 4u, 2u, 6u, 1u, 5u, 3u, 7u,
            }};
        return cell < kProcessorFissureCells
            ? clamp(performed_.mass
                    * static_cast<float>(kProcessorFissureCells) + 0.45f
                    - static_cast<float>(massOrder[cell]),
                0.0f, 1.0f)
            : 0.0f;
    }

    static float finiteClamp(float value, float minimum, float maximum,
        float fallback)
    {
        return clamp(std::isfinite(value) ? value : fallback,
            minimum, maximum);
    }

    static uint32_t supportedOutputChannels(uint32_t channels)
    {
        if (channels <= 2u) return 2u;
        if (channels <= 4u) return 4u;
        return 8u;
    }

    static float onePoleCoefficient(float timeMs, float sampleRate)
    {
        return 1.0f - std::exp(-1.0f
            / std::max(1.0f, sampleRate * timeMs * 0.001f));
    }

    static float frequencyCoefficient(float frequency, float sampleRate)
    {
        return 1.0f - std::exp(-2.0f * kPi
            * std::min(frequency, sampleRate * 0.45f) / sampleRate);
    }

    static float dbToGain(float db)
    {
        return std::pow(10.0f, db / 20.0f);
    }

    static float wrapPhase(float phase)
    {
        phase -= std::floor(phase);
        return phase < 0.0f ? phase + 1.0f : phase;
    }

    static float transparentLimit(float value)
    {
        const float magnitude = std::abs(value);
        if (magnitude <= 0.82f) return value;
        const float limited = 0.82f + 0.18f
            * std::tanh((magnitude - 0.82f) / 0.18f);
        return std::copysign(std::min(1.0f, limited), value);
    }

    uint32_t nextRandom()
    {
        uint32_t x = rng_ ? rng_ : 1u;
        x ^= x << 13u;
        x ^= x >> 17u;
        x ^= x << 5u;
        rng_ = x ? x : 1u;
        return rng_;
    }

    float randomUnit()
    {
        return static_cast<float>(nextRandom() & 0x00ffffffu)
            / static_cast<float>(0x01000000u);
    }

    float randomSigned() { return randomUnit() * 2.0f - 1.0f; }

    void smoothParameters()
    {
        const auto smooth = [this](float& current, float target) {
            current += (target - current) * parameterCoefficient_;
        };
        smooth(smoothed_.pressure, target_.pressure);
        smooth(smoothed_.mass, target_.mass);
        smooth(smoothed_.edge, target_.edge);
        smooth(smoothed_.voidAmount, target_.voidAmount);
        smooth(smoothed_.memory, target_.memory);
        smooth(smoothed_.body, target_.body);
        smooth(smoothed_.voice, target_.voice);
        smooth(smoothed_.motion, target_.motion);
        smooth(smoothed_.contact, target_.contact);
        smooth(smoothed_.shaker, target_.shaker);
        smooth(smoothed_.rattle, target_.rattle);
        smooth(smoothed_.spring, target_.spring);
        smooth(smoothed_.inputGainDb, target_.inputGainDb);
        smooth(smoothed_.outputGainDb, target_.outputGainDb);
        smoothed_.hold = target_.hold;
        smoothed_.run = target_.run;
        smoothed_.seed = target_.seed;
    }

    void updateCellProcessors()
    {
        updateCellProcessors(target_);
    }

    void updateCellProcessors(const ProcessorFissureParams& controls)
    {
        for (uint32_t cell = 0u; cell < kProcessorFissureCells; ++cell) {
            updateCellProcessor(cell, controls);
        }
    }

    void updateCellProcessor(uint32_t cell)
    {
        updateCellProcessor(cell, target_);
    }

    void updateCellProcessor(uint32_t cell,
        const ProcessorFissureParams& controls)
    {
        if (cell >= kProcessorFissureCells) return;
        const auto& object = objectTarget_[cell];
        MacroShredCoreParams params;
        params.inputGainDb = -7.0f + controls.pressure * 7.0f
            + object.drive * 12.0f;
        params.pressure = clamp(0.10f + controls.pressure * 0.62f
                + object.drive * 0.22f,
            0.0f, 1.0f);
        params.shred = clamp(0.06f + controls.edge * 0.52f
                + object.hardness * 0.34f
                + static_cast<float>(cell & 1u) * 0.04f,
            0.0f, 1.0f);
        params.feedback = clamp(controls.memory * 0.24f
                + controls.pressure * 0.20f + object.decay * 0.12f,
            0.0f, 0.72f);
        params.color = clamp(0.08f
                + static_cast<float>(cell) * 0.095f
                + (controls.edge - 0.5f) * 0.18f
                + (object.hardness - 0.5f) * 0.30f,
            0.0f, 1.0f);
        params.react = clamp(0.12f + controls.edge * 0.42f
                + object.sensitivity * 0.34f,
            0.0f, 1.0f);
        params.tune = clamp(0.10f + controls.body * 0.54f
                + (1.0f - object.size) * 0.27f
                + static_cast<float>(cell) * 0.012f,
            0.0f, 1.0f);
        params.body = clamp(0.18f + controls.body * 0.48f
                + object.size * 0.28f,
            0.0f, 1.0f);
        params.mix = 0.72f + object.drive * 0.22f;
        params.outputGainDb = -6.0f + object.level * 2.0f;
        params.colorShiftOctaves = (static_cast<float>(cell) - 3.5f)
            * controls.motion * 0.10f
            + (0.5f - object.size) * 0.42f;
        params.intensityTrim = (object.hardness - 0.5f) * 0.12f;
        static constexpr std::array<MacroShredCircuit,
            kProcessorFissureCells> circuits {{
                MacroShredCircuit::Shred,
                MacroShredCircuit::Wool,
                MacroShredCircuit::Rat,
                MacroShredCircuit::ZoneA,
                MacroShredCircuit::ZoneB,
                MacroShredCircuit::FuzzI,
                MacroShredCircuit::FuzzII,
                MacroShredCircuit::Diode,
            }};
        const uint32_t characterOffset = static_cast<uint32_t>(
            object.hardness * 7.999f);
        params.circuit = circuits[(cell + matrixRevision_ + characterOffset)
            % kProcessorFissureCells];
        shred_[cell].setParams(params);
    }

    static constexpr uint32_t kModalCount = 3u;
    static constexpr uint32_t kPitchRingSize = 2048u;
    static constexpr uint32_t kPitchAnalysisSamples = 512u;
    static constexpr uint32_t kPitchMaximumLag = 384u;

    float pitchRandomSigned()
    {
        uint32_t x = pitchRng_ ? pitchRng_ : 1u;
        x ^= x << 13u;
        x ^= x >> 17u;
        x ^= x << 5u;
        pitchRng_ = x ? x : 1u;
        return static_cast<float>(pitchRng_ & 0x00ffffffu)
                / static_cast<float>(0x00800000u)
            - 1.0f;
    }

    float pitchSampleAtAge(uint32_t referenceWrite, uint32_t age) const
    {
        const uint32_t index = (referenceWrite + kPitchRingSize - 1u
                - age % kPitchRingSize)
            % kPitchRingSize;
        return pitchRing_[index];
    }

    void beginPitchAnalysis()
    {
        pitchAnalysisMinimumLag_ = std::max<uint32_t>(2u,
            static_cast<uint32_t>(pitchAnalysisRate_ / 4000.0f));
        pitchAnalysisMaximumLag_ = std::min<uint32_t>(kPitchMaximumLag,
            static_cast<uint32_t>(pitchAnalysisRate_ / 45.0f));
        if (pitchAnalysisMaximumLag_ <= pitchAnalysisMinimumLag_ + 2u
            || pitchSamples_ < kPitchAnalysisSamples
                + pitchAnalysisMaximumLag_) return;
        pitchAnalysisReferenceWrite_ = pitchWrite_;
        pitchAnalysisRecentEnergy_ = 0.0;
        for (uint32_t index = 0u; index < kPitchAnalysisSamples;
             ++index) {
            const double sample = pitchSampleAtAge(
                pitchAnalysisReferenceWrite_, index);
            pitchAnalysisRecentEnergy_ += sample * sample;
        }
        if (pitchAnalysisRecentEnergy_ < 1.0e-7) {
            pitchConfidence_ *= 0.72f;
            if (pitchConfidence_ < 0.01f) trackedPitchHz_ = 0.0f;
            pitchQuietCountdown_ = 128u;
            updatePhysicalModel();
            return;
        }
        pitchAnalysisLag_ = pitchAnalysisMinimumLag_;
        pitchAnalysisActive_ = true;
    }

    void finishPitchAnalysis()
    {
        float bestScore = 0.0f;
        for (uint32_t lag = pitchAnalysisMinimumLag_;
             lag <= pitchAnalysisMaximumLag_; ++lag) {
            bestScore = std::max(bestScore, pitchScore_[lag]);
        }

        uint32_t selectedLag = 0u;
        const float peakThreshold = std::max(0.54f, bestScore * 0.90f);
        for (uint32_t lag = pitchAnalysisMinimumLag_ + 1u;
             lag < pitchAnalysisMaximumLag_; ++lag) {
            if (pitchScore_[lag] >= peakThreshold
                && pitchScore_[lag] >= pitchScore_[lag - 1u]
                && pitchScore_[lag] > pitchScore_[lag + 1u]) {
                selectedLag = lag;
                break;
            }
        }

        if (selectedLag == 0u) {
            pitchConfidence_ *= 0.72f;
            if (pitchConfidence_ < 0.01f) trackedPitchHz_ = 0.0f;
            updatePhysicalModel();
            return;
        }

        const float left = pitchScore_[selectedLag - 1u];
        const float center = pitchScore_[selectedLag];
        const float right = pitchScore_[selectedLag + 1u];
        const float denominator = left - 2.0f * center + right;
        const float offset = std::abs(denominator) > 1.0e-6f
            ? clamp(0.5f * (left - right) / denominator, -0.5f, 0.5f)
            : 0.0f;
        const float candidate = pitchAnalysisRate_
            / (static_cast<float>(selectedLag) + offset);
        const float confidence = clamp((center - 0.48f) / 0.44f,
            0.0f, 1.0f);
        if (!std::isfinite(candidate) || candidate < 40.0f
            || candidate > 4200.0f || confidence < 0.08f) {
            pitchConfidence_ *= 0.72f;
            if (pitchConfidence_ < 0.01f) trackedPitchHz_ = 0.0f;
            updatePhysicalModel();
            return;
        }

        float continuityCandidate = candidate;
        if (trackedPitchHz_ > 0.0f && pitchConfidence_ > 0.22f) {
            while (continuityCandidate > trackedPitchHz_ * 1.82f) {
                continuityCandidate *= 0.5f;
            }
            while (continuityCandidate < trackedPitchHz_ * 0.55f) {
                continuityCandidate *= 2.0f;
            }
        }
        const float pitchBlend = trackedPitchHz_ > 0.0f
            ? 0.16f + confidence * 0.30f : 1.0f;
        trackedPitchHz_ = trackedPitchHz_ > 0.0f
            ? std::exp(lerp(std::log(trackedPitchHz_),
                std::log(continuityCandidate), pitchBlend))
            : continuityCandidate;
        pitchConfidence_ += (confidence - pitchConfidence_)
            * (confidence > pitchConfidence_ ? 0.48f : 0.20f);
        for (uint32_t cell = 0u; cell < kProcessorFissureCells; ++cell) {
            for (uint32_t mode = 0u; mode < kModalCount; ++mode) {
                const float chaosBlend = 0.06f
                    + performed_.motion * 0.28f + performed_.edge * 0.06f;
                pitchChaos_[cell][mode] = lerp(
                    pitchChaos_[cell][mode], pitchRandomSigned(),
                    chaosBlend);
            }
        }
        updatePhysicalModel();
    }

    void advancePitchAnalysis()
    {
        if (!pitchAnalysisActive_) {
            if (pitchQuietCountdown_ > 0u) {
                --pitchQuietCountdown_;
                return;
            }
            beginPitchAnalysis();
            if (!pitchAnalysisActive_) return;
        }
        const uint32_t lag = pitchAnalysisLag_;
        double product = 0.0;
        for (uint32_t index = 0u; index < kPitchAnalysisSamples;
             ++index) {
            const double recent = pitchSampleAtAge(
                pitchAnalysisReferenceWrite_, index);
            const double delayed = pitchSampleAtAge(
                pitchAnalysisReferenceWrite_, index + lag);
            product += recent * delayed;
        }
        pitchScore_[lag] = pitchAnalysisRecentEnergy_ > 1.0e-12
            ? clamp(static_cast<float>(product
                    / pitchAnalysisRecentEnergy_),
                0.0f, 1.0f)
            : 0.0f;
        if (++pitchAnalysisLag_ > pitchAnalysisMaximumLag_) {
            pitchAnalysisActive_ = false;
            finishPitchAnalysis();
        }
    }

    void advancePitchTracker(float input)
    {
        pitchDc_ += (input - pitchDc_) * pitchDcCoefficient_;
        const float highpassed = input - pitchDc_;
        pitchLowpass_ += (highpassed - pitchLowpass_)
            * pitchLowpassCoefficient_;
        pitchDecimationSum_ += pitchLowpass_;
        if (++pitchDecimationCounter_ < pitchDecimation_) return;
        const float decimated = pitchDecimationSum_
            / static_cast<float>(pitchDecimation_);
        pitchDecimationCounter_ = 0u;
        pitchDecimationSum_ = 0.0f;
        pitchRing_[pitchWrite_] = decimated;
        pitchWrite_ = (pitchWrite_ + 1u) % kPitchRingSize;
        pitchSamples_ = std::min<uint32_t>(pitchSamples_ + 1u,
            kPitchRingSize);
        advancePitchAnalysis();
    }

    void updatePhysicalModel()
    {
        updatePhysicalModel(target_);
    }

    void updatePhysicalModel(const ProcessorFissureParams& controls)
    {
        for (uint32_t cell = 0u; cell < kProcessorFissureCells; ++cell) {
            updatePhysicalCell(cell, controls);
        }
    }

    void updatePhysicalCell(uint32_t cell)
    {
        updatePhysicalCell(cell, target_);
    }

    void updatePhysicalCell(uint32_t cell,
        const ProcessorFissureParams& controls)
    {
        if (cell >= kProcessorFissureCells) return;
        static constexpr std::array<float, kProcessorFissureCells> roots {{
            43.0f, 59.0f, 83.0f, 127.0f,
            181.0f, 263.0f, 397.0f, 619.0f,
        }};
        static constexpr std::array<float, kModalCount> ratios {{
            1.0f, 2.71f, 6.37f,
        }};
        static constexpr std::array<float,
            kProcessorFissureCells> pitchRelations {{
                0.5f, 2.0f / 3.0f, 0.75f, 1.0f,
                1.25f, 1.5f, 2.0f, 2.5f,
            }};
        static constexpr std::array<float, kModalCount>
            pitchModeRelations {{ 1.0f, 2.0f, 4.0f }};
        const auto& object = objectTarget_[cell];
        const float bodyScale = std::exp2((controls.body - 0.5f) * 1.20f
            + (0.5f - object.size) * 3.15f);
        for (uint32_t mode = 0u; mode < kModalCount; ++mode) {
            const float baseFrequency = roots[cell] * ratios[mode]
                * bodyScale;
            float frequency = baseFrequency;
            if (mode == 2u) {
                frequency *= 1.0f + controls.spring
                    * (0.8f + object.hardness * 2.6f);
            }
            const float pitchGate = controls.voice
                * clamp((pitchConfidence_ - 0.10f) / 0.70f,
                    0.0f, 1.0f);
            if (trackedPitchHz_ > 0.0f && pitchGate > 0.0f) {
                float attracted = trackedPitchHz_ * pitchRelations[cell]
                    * pitchModeRelations[mode];
                while (attracted < frequency * 0.58f) attracted *= 2.0f;
                while (attracted > frequency * 1.72f) attracted *= 0.5f;
                float routeTension = 0.0f;
                for (uint32_t source = 0u;
                     source < kProcessorFissureCells; ++source) {
                    routeTension += matrix_[cell * kProcessorFissureCells
                            + source]
                        * (static_cast<float>(source) - 3.5f) / 3.5f;
                }
                const float chaosSemitones = pitchChaos_[cell][mode]
                        * (0.45f + controls.motion * 6.0f
                            + controls.edge * 1.7f)
                    + routeTension * controls.motion * 2.4f;
                attracted *= std::exp2(chaosSemitones / 12.0f);
                const float attraction = pitchGate
                    * (0.24f + object.sensitivity * 0.58f);
                frequency = std::exp(lerp(std::log(
                        std::max(20.0f, frequency)),
                    std::log(std::max(20.0f, attracted)), attraction));
            }
            frequency = std::min(frequency,
                static_cast<float>(sampleRate_) * 0.43f);
            const float decaySeconds = mode == 2u
                ? 0.025f + controls.spring * 0.52f
                    + object.decay * 1.35f
                : 0.035f + controls.body * 0.42f
                    + controls.memory * 0.34f
                    + object.decay * 2.1f;
            const float radius = std::exp(-1.0f
                / std::max(1.0f, static_cast<float>(sampleRate_)
                    * decaySeconds));
            const float omega = 2.0f * kPi * frequency
                / static_cast<float>(sampleRate_);
            modalTargetFrequency_[cell][mode] = frequency;
            modalTargetA_[cell][mode] = 2.0f * radius * std::cos(omega);
            modalTargetB_[cell][mode] = -radius * radius;
        }
        const float couplingCenter = clamp(roots[cell] * bodyScale * 1.25f,
            55.0f, static_cast<float>(sampleRate_) * 0.19f);
        inputCouplingLowCoefficient_[cell] = frequencyCoefficient(
            couplingCenter * 0.42f, static_cast<float>(sampleRate_));
        inputCouplingHighCoefficient_[cell] = frequencyCoefficient(
            couplingCenter * 2.7f, static_cast<float>(sampleRate_));
    }

    void excitePhysical(uint32_t cell, float strength)
    {
        if (cell >= kProcessorFissureCells) return;
        strength = clamp(strength, 0.0f, 1.5f);
        for (uint32_t mode = 0u; mode < kModalCount; ++mode) {
            const float hardness = mode == 2u
                ? (0.18f + performed_.spring * 0.54f)
                    * (0.25f + objectTarget_[cell].hardness * 1.15f)
                : (0.58f - static_cast<float>(mode) * 0.11f)
                    * (1.20f - objectTarget_[cell].hardness * 0.32f);
            modalImpulse_[cell][mode] += strength * hardness
                * (0.12f + objectTarget_[cell].sensitivity * 1.24f)
                * (randomUnit() < 0.5f ? -1.0f : 1.0f);
        }
    }

    void advanceShaker()
    {
        if (performed_.hold || performed_.shaker <= 1.0e-4f) return;
        if (shakerCountdown_ > 0u) --shakerCountdown_;
        if (shakerCountdown_ > 0u) return;
        const uint32_t cell = nextRandom() % kProcessorFissureCells;
        const float impact = performed_.shaker
            * (0.12f + randomUnit() * 0.88f)
            * (0.42f + performed_.rattle * 0.78f);
        excitePhysical(cell, impact);
        if (randomUnit() < 0.28f + performed_.rattle * 0.62f) {
            const uint32_t neighbour = (cell + 1u
                + nextRandom() % (kProcessorFissureCells - 1u))
                % kProcessorFissureCells;
            excitePhysical(neighbour, impact
                * (0.18f + randomUnit() * 0.42f));
        }
        burstEnvelope_[cell] = std::max(burstEnvelope_[cell],
            impact * (0.12f + performed_.rattle * 0.28f));
        const float impactsPerSecond = 0.45f
            + performed_.shaker * performed_.shaker * 19.0f
            + performed_.motion * 7.0f
            + performed_.rattle * 14.0f;
        const float scatter = 0.18f + randomUnit() * 1.64f;
        shakerCountdown_ = std::max<uint32_t>(1u,
            static_cast<uint32_t>(sampleRate_ * scatter
                / impactsPerSecond));
    }

    void regenerateMatrix(float mutation)
    {
        mutation = clamp(mutation, 0.0f, 1.0f);
        ++matrixRevision_;
        for (uint32_t destination = 0u;
             destination < kProcessorFissureCells; ++destination) {
            float magnitude = 0.0f;
            for (uint32_t source = 0u;
                 source < kProcessorFissureCells; ++source) {
                float route = 0.0f;
                if (source == destination) {
                    route = 0.28f + randomUnit() * 0.18f;
                } else if (source == (destination + 1u)
                        % kProcessorFissureCells
                    || source == (destination
                            + kProcessorFissureCells - 1u)
                        % kProcessorFissureCells) {
                    route = (randomUnit() < 0.35f ? -1.0f : 1.0f)
                        * (0.10f + randomUnit() * 0.18f);
                } else if (randomUnit() < 0.10f + mutation * 0.24f) {
                    route = (randomUnit() < 0.48f ? -1.0f : 1.0f)
                        * (0.035f + randomUnit()
                            * (0.09f + mutation * 0.14f));
                }
                matrix_[destination * kProcessorFissureCells + source]
                    = route;
                magnitude += std::abs(route);
            }
            if (magnitude > 1.22f) {
                const float scale = 1.22f / magnitude;
                for (uint32_t source = 0u;
                     source < kProcessorFissureCells; ++source) {
                    matrix_[destination * kProcessorFissureCells + source]
                        *= scale;
                }
            }
        }
        updateCellProcessors();
    }

    void requestCutSplice(uint32_t cell, float strength)
    {
        if (cell >= kProcessorFissureCells || !cutMask_[cell]
            || historyFrames_ <= 2u) return;
        strength = clamp(strength, 0.0f, 1.0f);
        const float fractureX = performedFractureDistance();
        const float variation = performedCutVariation();
        const float effectiveContrast = 1.0f
            - (1.0f - variation) * (1.0f - fractureX);
        const float fragmentReach = std::max(performed_.memory,
            fractureX * (0.52f + performed_.memory * 0.48f));
        const float maximumSeconds = 0.028f
            + fragmentReach * fragmentReach
                * (0.16f + effectiveContrast * 1.84f);
        const float minimumSeconds = 0.0025f
            + (1.0f - effectiveCutEdge()) * 0.018f;
        const float shapedRandom = std::pow(randomUnit(),
            lerp(2.4f, 0.55f, strength));
        const float seconds = lerp(minimumSeconds,
            std::max(minimumSeconds, maximumSeconds), shapedRandom);
        pendingCutDelayFrames_[cell] = std::clamp<uint32_t>(
            static_cast<uint32_t>(seconds * sampleRate_),
            1u, historyFrames_ - 1u);
        const float inversionProbability = 0.06f
            + performed_.motion * 0.34f
            + effectiveContrast * strength * 0.28f;
        pendingCutPolarity_[cell] = randomUnit() < inversionProbability
            ? -1.0f : 1.0f;
        cutSwapPending_[cell] = true;
        cutSeamTarget_[cell] = 0.0f;
        cutVisual_[cell] = 1.0f;
        if (performanceCaptureActive_) {
            capturedCutMask_ |= static_cast<uint8_t>(1u << cell);
        }
        ++cutRevision_;
    }

    void advanceCutSplice(uint32_t cell)
    {
        const float coefficient = cutSeamTarget_[cell] < cutSeam_[cell]
            ? cutDownCoefficient_ : cutUpCoefficient_;
        cutSeam_[cell] += (cutSeamTarget_[cell] - cutSeam_[cell])
            * coefficient;
        if (cutSwapPending_[cell] && cutSeam_[cell] <= 0.025f) {
            cutDelayFrames_[cell] = pendingCutDelayFrames_[cell];
            cutPolarity_[cell] = pendingCutPolarity_[cell];
            cutSwapPending_[cell] = false;
            cutSeamTarget_[cell] = 1.0f;
        }
        cutSeam_[cell] = flushDenormal(clamp(
            cutSeam_[cell], 0.0f, 1.0f));
        cutVisual_[cell] = flushDenormal(cutVisual_[cell] * cutVisualDecay_);
    }

    float performedCutVariation() const
    {
        return grabbedValid_ && repeatMix_ > 1.0e-5f
            ? lerp(cutVariation_, repeatedFrame_.variation, repeatMix_)
            : cutVariation_;
    }

    float performedFractureDistance() const
    {
        return grabbedValid_ && repeatMix_ > 1.0e-5f
            ? lerp(fractureXSmoothed_, repeatedFrame_.fractureX, repeatMix_)
            : fractureXSmoothed_;
    }

    float performedFractureForce() const
    {
        return grabbedValid_ && repeatMix_ > 1.0e-5f
            ? lerp(fractureYSmoothed_, repeatedFrame_.fractureY, repeatMix_)
            : fractureYSmoothed_;
    }

    float effectiveCutEdge() const
    {
        return 1.0f - (1.0f - performed_.edge)
            * (1.0f - performedFractureForce() * 0.98f);
    }

    float effectiveCutVoid() const
    {
        return 1.0f - (1.0f - performed_.voidAmount)
            * (1.0f - performedFractureForce() * 0.88f);
    }

    void scheduleNextEvent(uint32_t cell, bool startup)
    {
        const float rate = 0.018f
            * std::pow(6666.6665f, effectiveCutEdge());
        const float seconds = (0.58f + randomUnit() * 0.94f)
            / std::max(0.005f, rate);
        eventCountdown_[cell] = startup
            ? static_cast<uint32_t>(sampleRate_
                * std::min(seconds, 0.04f + randomUnit() * 0.28f))
            : static_cast<uint32_t>(sampleRate_ * seconds);
        eventCountdown_[cell] = std::max<uint32_t>(1u,
            eventCountdown_[cell]);
    }

    void advanceEvents(uint32_t cell)
    {
        if (!performed_.hold && !repeatTarget_) {
            if (eventCountdown_[cell] > 0u) --eventCountdown_[cell];
            if (eventCountdown_[cell] == 0u) {
                const float effectiveEdge = effectiveCutEdge();
                const float effectiveVoid = effectiveCutVoid();
                const float spliceProbability = clamp(
                    effectiveEdge * effectiveEdge
                        * (0.14f + effectiveVoid * 0.86f)
                        * (0.22f + (1.0f
                            - (1.0f - performedCutVariation())
                                * (1.0f - performedFractureDistance()))
                            * 0.78f),
                    0.0f, 1.0f);
                if (cutMask_[cell]) {
                    if (randomUnit() < spliceProbability) {
                        requestCutSplice(cell, spliceProbability);
                    }
                    const float closeProbability = effectiveVoid
                        * (0.32f + effectiveEdge * 0.62f);
                    if (gateTarget_[cell] < 0.5f) {
                        gateTarget_[cell] = 1.0f;
                    } else {
                        gateTarget_[cell] = randomUnit() < closeProbability
                            ? 0.0f : 1.0f;
                    }
                    if (gateTarget_[cell] > 0.5f
                        && randomUnit() < 0.18f + effectiveEdge * 0.42f) {
                        burstEnvelope_[cell] = std::max(burstEnvelope_[cell],
                            0.16f + randomUnit() * 0.54f);
                    }
                }
                scheduleNextEvent(cell, false);
            }
        }
        const float coefficient = performed_.edge > 0.56f
            ? gateFastCoefficient_ : gateSlowCoefficient_;
        gate_[cell] += (gateTarget_[cell] - gate_[cell]) * coefficient;
        gate_[cell] = flushDenormal(clamp(gate_[cell], 0.0f, 1.0f));
        advanceCutSplice(cell);
    }

    void advanceBreach(uint32_t cell)
    {
        if (!breachPending_[cell]) return;
        if (breachArrival_[cell] > 0u) {
            --breachArrival_[cell];
            return;
        }
        const float strength = clamp(1.0f
            - static_cast<float>(cell) * 0.055f, 0.45f, 1.0f);
        failure_[cell].excite(strength, true);
        excitePhysical(cell, strength);
        burstEnvelope_[cell] = std::max(
            burstEnvelope_[cell], strength);
        breachPending_[cell] = false;
    }

    uint32_t historyDelayFrames(uint32_t cell) const
    {
        const float cellOffset = static_cast<float>(
            (cell * 37u + target_.seed * 13u) % 101u) / 100.0f;
        const float seconds = 0.024f
            + performed_.memory * performed_.memory
                * (0.18f + cellOffset * 2.62f);
        return std::clamp<uint32_t>(
            static_cast<uint32_t>(seconds * sampleRate_),
            1u, historyFrames_ - 1u);
    }

    float cellAngle(uint32_t cell) const
    {
        const float base = static_cast<float>(cell)
            / static_cast<float>(kProcessorFissureCells);
        const uint32_t hash = (cell + 1u) * 0x9e3779b9u
            ^ target_.seed * 0x85ebca6bu;
        const float jitter = (static_cast<float>(hash & 0xffffu)
                / 32767.5f - 1.0f)
            * performed_.motion * 0.055f;
        return wrapPhase(base + rotationPhase_ * performed_.motion + jitter)
            * 2.0f * kPi;
    }

    static void renderCell(float value, float angle, float* output,
        uint32_t outputChannels)
    {
        if (outputChannels == 2u) {
            const float pan = 0.5f + 0.5f * std::sin(angle);
            output[0] += value * std::cos(pan * kPi * 0.5f);
            output[1] += value * std::sin(pan * kPi * 0.5f);
            return;
        }
        float unit = angle / (2.0f * kPi);
        unit -= std::floor(unit);
        const float position = unit * static_cast<float>(outputChannels);
        const uint32_t first = static_cast<uint32_t>(position)
            % outputChannels;
        const uint32_t second = (first + 1u) % outputChannels;
        const float fraction = position - std::floor(position);
        output[first] += value * std::cos(fraction * kPi * 0.5f);
        output[second] += value * std::sin(fraction * kPi * 0.5f);
    }

    double sampleRate_ = 48000.0;
    uint32_t outputChannels_ = 2u;
    uint32_t rng_ = 1979u;
    ProcessorFissureParams target_ {};
    ProcessorFissureParams smoothed_ {};
    ProcessorFissureParams performed_ {};
    std::array<PerformanceFrame, kPerformanceFrameCapacity>
        performanceFrames_ {};
    PerformanceFrame repeatedFrame_ {};
    std::array<MacroShredCore, kProcessorFissureCells> shred_ {};
    std::array<StructuralFailureModel, kProcessorFissureCells> failure_ {};
    ProcessorFissureMatrix matrix_ {};
    std::array<float, kProcessorFissureCells> previousReturns_ {};
    std::array<float, kProcessorFissureCells> nextReturns_ {};
    std::array<float, kProcessorFissureCells> gate_ {};
    std::array<float, kProcessorFissureCells> gateTarget_ {};
    std::array<uint32_t, kProcessorFissureCells> cutDelayFrames_ {};
    std::array<uint32_t, kProcessorFissureCells>
        pendingCutDelayFrames_ {};
    std::array<float, kProcessorFissureCells> cutPolarity_ {};
    std::array<float, kProcessorFissureCells> pendingCutPolarity_ {};
    std::array<float, kProcessorFissureCells> cutSeam_ {};
    std::array<float, kProcessorFissureCells> cutSeamTarget_ {};
    std::array<bool, kProcessorFissureCells> cutSwapPending_ {};
    std::array<bool, kProcessorFissureCells> cutMask_ {{
        true, true, true, true, true, true, true, true,
    }};
    std::array<float, kProcessorFissureCells> cutVisual_ {};
    std::array<uint32_t, kProcessorFissureCells> eventCountdown_ {};
    std::array<float, kProcessorFissureCells> oscillatorPhase_ {};
    std::array<float, kProcessorFissureCells> lowNoise_ {};
    std::array<float, kProcessorFissureCells> inputCouplingLow_ {};
    std::array<float, kProcessorFissureCells> inputCouplingHigh_ {};
    std::array<float, kProcessorFissureCells>
        inputCouplingLowCoefficient_ {};
    std::array<float, kProcessorFissureCells>
        inputCouplingHighCoefficient_ {};
    std::array<float, kProcessorFissureCells> energy_ {};
    std::array<float, kProcessorFissureCells> activity_ {};
    std::array<float, kProcessorFissureCells> governor_ {};
    std::array<float, kProcessorFissureCells> burstEnvelope_ {};
    std::array<ProcessorFissureObject,
        kProcessorFissureCells> objectTarget_ {};
    std::array<std::array<float, kModalCount>,
        kProcessorFissureCells> modalY1_ {};
    std::array<std::array<float, kModalCount>,
        kProcessorFissureCells> modalY2_ {};
    std::array<std::array<float, kModalCount>,
        kProcessorFissureCells> modalImpulse_ {};
    std::array<std::array<float, kModalCount>,
        kProcessorFissureCells> modalA_ {};
    std::array<std::array<float, kModalCount>,
        kProcessorFissureCells> modalB_ {};
    std::array<std::array<float, kModalCount>,
        kProcessorFissureCells> modalTargetA_ {};
    std::array<std::array<float, kModalCount>,
        kProcessorFissureCells> modalTargetB_ {};
    std::array<std::array<float, kModalCount>,
        kProcessorFissureCells> modalTargetFrequency_ {};
    std::array<std::array<float, kModalCount>,
        kProcessorFissureCells> pitchChaos_ {};
    std::array<float, kProcessorFissureCells> cellLevelTarget_ {{
        1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
    }};
    std::array<float, kProcessorFissureCells> cellLevelSmoothed_ {{
        1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
    }};
    std::array<uint32_t, kProcessorFissureCells> breachArrival_ {};
    std::array<bool, kProcessorFissureCells> breachPending_ {};
    std::vector<float> history_ {};
    uint32_t historyFrames_ = 64u;
    uint32_t historyWrite_ = 0u;
    float parameterCoefficient_ = 0.001f;
    float runCoefficient_ = 0.001f;
    float voiceAttackCoefficient_ = 0.01f;
    float voiceReleaseCoefficient_ = 0.001f;
    float energyAttackCoefficient_ = 0.001f;
    float energyReleaseCoefficient_ = 0.0001f;
    float inputRmsCoefficient_ = 0.001f;
    float gateFastCoefficient_ = 0.01f;
    float gateSlowCoefficient_ = 0.001f;
    float cutDownCoefficient_ = 0.1f;
    float cutUpCoefficient_ = 0.01f;
    float fractureAttackCoefficient_ = 0.01f;
    float fractureReleaseCoefficient_ = 0.001f;
    float repeatCoefficient_ = 0.01f;
    float cutVisualDecay_ = 0.9999f;
    float breachDecay_ = 0.9999f;
    float burstDecay_ = 0.999f;
    float contactLowCoefficient_ = 0.01f;
    float pitchDcCoefficient_ = 0.001f;
    float pitchLowpassCoefficient_ = 0.1f;
    float modalCoefficientSmoothing_ = 0.001f;
    float pitchSustainAttackCoefficient_ = 0.001f;
    float pitchSustainReleaseCoefficient_ = 0.001f;
    float voiceEnvelope_ = 0.0f;
    float inputPower_ = 0.0f;
    float contactLow_ = 0.0f;
    float previousContactInput_ = 0.0f;
    float contactActivity_ = 0.0f;
    float inputTransferActivity_ = 0.0f;
    float sustainedPitchDrive_ = 0.0f;
    std::array<float, kPitchRingSize> pitchRing_ {};
    std::array<float, kPitchMaximumLag + 2u> pitchScore_ {};
    uint32_t pitchWrite_ = 0u;
    uint32_t pitchSamples_ = 0u;
    uint32_t pitchAnalysisLag_ = 0u;
    uint32_t pitchAnalysisMinimumLag_ = 0u;
    uint32_t pitchAnalysisMaximumLag_ = 0u;
    uint32_t pitchAnalysisReferenceWrite_ = 0u;
    uint32_t pitchQuietCountdown_ = 0u;
    uint32_t pitchDecimation_ = 4u;
    uint32_t pitchDecimationCounter_ = 0u;
    float pitchDecimationSum_ = 0.0f;
    float pitchAnalysisRate_ = 16000.0f;
    float pitchDc_ = 0.0f;
    float pitchLowpass_ = 0.0f;
    float trackedPitchHz_ = 0.0f;
    float pitchConfidence_ = 0.0f;
    double pitchAnalysisRecentEnergy_ = 0.0;
    uint32_t pitchRng_ = 1u;
    bool pitchAnalysisActive_ = false;
    uint32_t shakerCountdown_ = 1u;
    float rotationPhase_ = 0.0f;
    float breachEnvelope_ = 0.0f;
    float withdrawGain_ = 1.0f;
    float withdrawTarget_ = 1.0f;
    uint32_t withdrawHoldRemaining_ = 0u;
    uint32_t blackoutFrames_ = 0u;
    float runGain_ = 1.0f;
    float cutVariation_ = 0.46f;
    float fractureXTarget_ = 0.0f;
    float fractureYTarget_ = 0.0f;
    float fractureXSmoothed_ = 0.0f;
    float fractureYSmoothed_ = 0.0f;
    float repeatMix_ = 0.0f;
    uint32_t performanceFrameCount_ = 0u;
    uint32_t performanceCaptureCountdown_ = 0u;
    uint32_t performancePlaybackIndex_ = 0u;
    uint32_t performancePlaybackCountdown_ = 0u;
    uint32_t performanceControlFrames_ = 240u;
    uint8_t capturedCutMask_ = 0u;
    uint8_t capturedStrikeMask_ = 0u;
    bool performanceCaptureActive_ = false;
    bool performanceFrameApplied_ = false;
    bool repeatTarget_ = false;
    bool grabbedValid_ = false;
    bool silenced_ = false;
    uint32_t matrixRevision_ = 0u;
    uint32_t cutRevision_ = 0u;
    uint32_t grabRevision_ = 0u;
};

} // namespace s3g
