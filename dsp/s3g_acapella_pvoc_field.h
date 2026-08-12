#pragma once

#include "s3g_acapella_source_synth.h"
#include "s3g_math.h"
#include "s3g_realtime.h"
#include "s3g_spectral_fft.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace s3g {

constexpr uint32_t kAcapellaPvocFftSize = 1024u;
constexpr uint32_t kAcapellaPvocOverlap = 4u;
constexpr uint32_t kAcapellaPvocChannels = 2u;
constexpr uint32_t kAcapellaPvocMaxHeads = 8u;
constexpr float kAcapellaPvocMaxMemoryMs = 10000.0f;

enum class AcapellaPvocMode : uint32_t {
    Live = 0u,
    Freeze,
    Stretch,
    Scrub,
    Reverse,
    Loop,
    Cloud,
};

constexpr uint32_t kAcapellaPvocModeCount = 7u;

enum class AcapellaPvocPhaseMode : uint32_t {
    Identity = 0u,
    PeakLocked,
    Loose,
    Diffuse,
};

constexpr uint32_t kAcapellaPvocPhaseModeCount = 4u;

enum class AcapellaPvocCaptureTrigger : uint32_t {
    Continuous = 0u,
    Note,
    Phoneme,
    Syllable,
    Word,
    Rest,
};

constexpr uint32_t kAcapellaPvocCaptureTriggerCount = 6u;

struct AcapellaPvocGesture {
    AcapellaPhoneme phoneme = AcapellaPhoneme::AX;
    float frequencyHz = 146.83f;
    uint32_t stepIndex = 0u;
    uint8_t stress = 0u;
    uint8_t flags = 0u;
    bool active = false;
    float stepProgress = 0.0f;
    uint64_t voiceInstance = 0u;
};

struct AcapellaPvocParams {
    float amount = 0.0f;
    AcapellaPvocMode mode = AcapellaPvocMode::Freeze;
    float memoryMs = 1200.0f;
    float position = 0.18f;
    float speed = 1.0f;
    float loopLengthMs = 360.0f;
    float timeSpread = 0.0f;
    uint32_t heads = 1u;
    float feedback = 0.0f;
    float pitchSemitones = 0.0f;
    float formantSemitones = 0.0f;
    float warp = 0.0f;
    float harmonicLock = 0.0f;
    float peakResidue = 0.0f;
    float partialCloud = 0.0f;
    AcapellaPvocPhaseMode phaseMode = AcapellaPvocPhaseMode::Identity;
    float coherence = 0.86f;
    float phaseDrift = 0.0f;
    float transientPreserve = 0.36f;
    AcapellaPvocCaptureTrigger captureTrigger
        = AcapellaPvocCaptureTrigger::Syllable;
    float captureReleaseMs = 320.0f;
    float gestureFollow = 0.55f;
};

inline AcapellaPvocParams sanitizeAcapellaPvocParams(
    AcapellaPvocParams params)
{
    params.amount = clamp(acapellaFiniteOr(params.amount, 0.0f), 0.0f, 1.0f);
    params.mode = static_cast<AcapellaPvocMode>(std::min<uint32_t>(
        static_cast<uint32_t>(params.mode), kAcapellaPvocModeCount - 1u));
    params.memoryMs = clamp(acapellaFiniteOr(params.memoryMs, 1200.0f),
        20.0f, kAcapellaPvocMaxMemoryMs);
    params.position = clamp(acapellaFiniteOr(params.position, 0.18f),
        0.0f, 1.0f);
    params.speed = clamp(acapellaFiniteOr(params.speed, 1.0f), -2.0f, 2.0f);
    params.loopLengthMs = clamp(acapellaFiniteOr(
        params.loopLengthMs, 360.0f), 20.0f, 5000.0f);
    params.timeSpread = clamp(acapellaFiniteOr(params.timeSpread, 0.0f),
        0.0f, 1.0f);
    params.heads = std::clamp<uint32_t>(
        params.heads, 1u, kAcapellaPvocMaxHeads);
    params.feedback = clamp(acapellaFiniteOr(params.feedback, 0.0f),
        0.0f, 0.94f);
    params.pitchSemitones = clamp(acapellaFiniteOr(
        params.pitchSemitones, 0.0f), -24.0f, 24.0f);
    params.formantSemitones = clamp(acapellaFiniteOr(
        params.formantSemitones, 0.0f), -24.0f, 24.0f);
    params.warp = clamp(acapellaFiniteOr(params.warp, 0.0f), -1.0f, 1.0f);
    params.harmonicLock = clamp(acapellaFiniteOr(
        params.harmonicLock, 0.0f), 0.0f, 1.0f);
    params.peakResidue = clamp(acapellaFiniteOr(
        params.peakResidue, 0.0f), -1.0f, 1.0f);
    params.partialCloud = clamp(acapellaFiniteOr(
        params.partialCloud, 0.0f), 0.0f, 1.0f);
    params.phaseMode = static_cast<AcapellaPvocPhaseMode>(
        std::min<uint32_t>(static_cast<uint32_t>(params.phaseMode),
            kAcapellaPvocPhaseModeCount - 1u));
    params.coherence = clamp(acapellaFiniteOr(params.coherence, 0.86f),
        0.0f, 1.0f);
    params.phaseDrift = clamp(acapellaFiniteOr(params.phaseDrift, 0.0f),
        0.0f, 1.0f);
    params.transientPreserve = clamp(acapellaFiniteOr(
        params.transientPreserve, 0.36f), 0.0f, 1.0f);
    params.captureTrigger = static_cast<AcapellaPvocCaptureTrigger>(
        std::min<uint32_t>(static_cast<uint32_t>(params.captureTrigger),
            kAcapellaPvocCaptureTriggerCount - 1u));
    params.captureReleaseMs = clamp(acapellaFiniteOr(
        params.captureReleaseMs, 320.0f), 20.0f, 5000.0f);
    params.gestureFollow = clamp(acapellaFiniteOr(
        params.gestureFollow, 0.55f), 0.0f, 1.0f);
    return params;
}

struct AcapellaPvocFrame {
    float output = 0.0f;
    float dry = 0.0f;
};

struct AcapellaPvocStereoFrame {
    float left = 0.0f;
    float right = 0.0f;
    float dryLeft = 0.0f;
    float dryRight = 0.0f;
};

// These counters deliberately describe the signal before the time-domain
// output limiter. Tests can therefore detect an unstable spectral state even
// if a later plug-in stage keeps the audible samples finite.
struct AcapellaPvocDiagnostics {
    float maximumSpectralMagnitude = 0.0f;
    float maximumFrameGain = 1.0f;
    float maximumWetSample = 0.0f;
    uint64_t spectralGuardHits = 0u;
    uint64_t outputGuardHits = 0u;
    uint64_t nonFiniteRecoveries = 0u;
    uint64_t captureCount = 0u;
};

// A stereo, causal phase vocoder over the live procedural instrument. This is
// intentionally independent of the discarded first-generation PVOC field:
// capture, transport, feedback, phase propagation, and output gain each have a
// separate bounded state machine. The history stores spectra, never samples.
class AcapellaPvocField {
public:
    bool prepare(double sampleRate)
    {
        sampleRate_ = std::isfinite(sampleRate)
            ? clamp(static_cast<float>(sampleRate), 8000.0f, 192000.0f)
            : 48000.0f;
        params_ = sanitizeAcapellaPvocParams(params_);
        ready_ = fft_.prepare(kAcapellaPvocChannels,
            kAcapellaPvocFftSize, kAcapellaPvocOverlap);
        if (!ready_) return false;

        bins_ = fft_.bins();
        historyFrameCapacity_ = std::max<uint32_t>(2u,
            static_cast<uint32_t>(std::ceil(sampleRate_
                * kAcapellaPvocMaxMemoryMs * 0.001f
                / static_cast<float>(fft_.hopSize()))) + 2u);
        const size_t historyValues = static_cast<size_t>(
            historyFrameCapacity_) * kAcapellaPvocChannels * bins_;
        magnitudeHistory_.assign(historyValues, 0.0f);
        advanceHistory_.assign(historyValues, 0.0f);
        historyEnergy_.assign(historyFrameCapacity_, 0.0f);

        const size_t channelValues = kAcapellaPvocChannels * bins_;
        liveReal_.assign(channelValues, 0.0f);
        liveImag_.assign(channelValues, 0.0f);
        liveMagnitude_.assign(channelValues, 0.0f);
        livePhase_.assign(channelValues, 0.0f);
        liveAdvance_.assign(channelValues, 0.0f);
        previousMagnitude_.assign(channelValues, 0.0f);
        previousAnalysisPhase_.assign(channelValues, 0.0f);
        freezeMagnitude_.assign(channelValues, 0.0f);
        freezeAdvance_.assign(channelValues, 0.0f);
        feedbackMagnitude_.assign(channelValues, 0.0f);
        feedbackAdvance_.assign(channelValues, 0.0f);
        referencePower_.assign(channelValues, 0.0f);
        warpedBin_.assign(bins_, 0.0f);
        transitionReal_.assign(channelValues, 0.0f);
        transitionImag_.assign(channelValues, 0.0f);
        lastWetReal_.assign(channelValues, 0.0f);
        lastWetImag_.assign(channelValues, 0.0f);

        const size_t headValues = static_cast<size_t>(
            kAcapellaPvocMaxHeads) * channelValues;
        headSourceMagnitude_.assign(headValues, 0.0f);
        headSourceAdvance_.assign(headValues, 0.0f);
        headEnvelope_.assign(headValues, 0.0f);
        headMagnitude_.assign(headValues, 0.0f);
        headAdvance_.assign(headValues, 0.0f);
        headSynthesisPhase_.assign(headValues, 0.0f);
        nearestPeak_.assign(headValues, 0u);
        headSourceBin_.assign(
            static_cast<size_t>(kAcapellaPvocMaxHeads) * bins_, 0.0f);
        headPhaseMotion_.assign(
            static_cast<size_t>(kAcapellaPvocMaxHeads) * bins_, 0.0f);

        dryDelayLeft_.assign(kAcapellaPvocFftSize, 0.0f);
        dryDelayRight_.assign(kAcapellaPvocFftSize, 0.0f);
        reset();
        return true;
    }

    // Reset is O(FFT state), not O(ten-second spectral memory). valid frame
    // counts make old history unreachable, which is both deterministic and
    // safe to invoke from a host's audio lifecycle.
    void reset()
    {
        fft_.reset();
        std::fill(liveReal_.begin(), liveReal_.end(), 0.0f);
        std::fill(liveImag_.begin(), liveImag_.end(), 0.0f);
        std::fill(liveMagnitude_.begin(), liveMagnitude_.end(), 0.0f);
        std::fill(livePhase_.begin(), livePhase_.end(), 0.0f);
        std::fill(liveAdvance_.begin(), liveAdvance_.end(), 0.0f);
        std::fill(previousMagnitude_.begin(), previousMagnitude_.end(), 0.0f);
        std::fill(previousAnalysisPhase_.begin(),
            previousAnalysisPhase_.end(), 0.0f);
        std::fill(freezeMagnitude_.begin(), freezeMagnitude_.end(), 0.0f);
        std::fill(freezeAdvance_.begin(), freezeAdvance_.end(), 0.0f);
        std::fill(feedbackMagnitude_.begin(), feedbackMagnitude_.end(), 0.0f);
        std::fill(feedbackAdvance_.begin(), feedbackAdvance_.end(), 0.0f);
        std::fill(referencePower_.begin(), referencePower_.end(), 0.0f);
        std::fill(transitionReal_.begin(), transitionReal_.end(), 0.0f);
        std::fill(transitionImag_.begin(), transitionImag_.end(), 0.0f);
        std::fill(lastWetReal_.begin(), lastWetReal_.end(), 0.0f);
        std::fill(lastWetImag_.begin(), lastWetImag_.end(), 0.0f);
        std::fill(headSynthesisPhase_.begin(),
            headSynthesisPhase_.end(), 0.0f);
        std::fill(dryDelayLeft_.begin(), dryDelayLeft_.end(), 0.0f);
        std::fill(dryDelayRight_.begin(), dryDelayRight_.end(), 0.0f);
        headPhaseReady_.fill(false);
        smoothed_ = params_;
        smoothedAmount_ = params_.amount;
        gestureEnvelope_ = 0.0f;
        activityEnvelope_ = 0.0f;
        limiterGain_ = 1.0f;
        fluxEnvelope_ = 0.0f;
        historyWrite_ = 0u;
        historyFilled_ = 0u;
        spectralFrame_ = 0u;
        dryWrite_ = 0u;
        continuousCaptureCountdown_ = 0u;
        lastGestureStep_ = 0xffffffffu;
        lastVoiceInstance_ = std::numeric_limits<uint64_t>::max();
        lastMode_ = params_.mode;
        capturePending_ = false;
        pendingCaptureHops_ = 0u;
        freezeValid_ = false;
        analysisReady_ = false;
        transportInitialized_ = false;
        lastOutputValid_ = false;
        transitionFramesRemaining_ = 0u;
        anchorSerial_ = 0.0;
        transportCursor_ = 0.0;
        scrubPhase_ = 0.0f;
        amountWasActive_ = params_.amount > 1.0e-4f;
        diagnostics_ = {};
    }

    void setParams(AcapellaPvocParams params)
    {
        params_ = sanitizeAcapellaPvocParams(params);
    }

    void setGesture(AcapellaPvocGesture gesture)
    {
        gesture.frequencyHz = clamp(acapellaFiniteOr(
            gesture.frequencyHz, 146.83f), 35.0f, sampleRate_ * 0.20f);
        gesture.stepProgress = clamp(acapellaFiniteOr(
            gesture.stepProgress, 0.0f), 0.0f, 1.0f);
        gesture.stress = std::min<uint8_t>(gesture.stress, 2u);
        if (static_cast<uint32_t>(gesture.phoneme)
            >= kAcapellaPhonemeCount) gesture.phoneme = AcapellaPhoneme::AX;
        gesture_ = gesture;
    }

    const AcapellaPvocParams& params() const { return params_; }
    const AcapellaPvocDiagnostics& diagnostics() const { return diagnostics_; }
    bool ready() const { return ready_; }

    uint32_t latencySamples() const
    {
#if S3G_HAS_ACCELERATE_FFT
        return kAcapellaPvocFftSize;
#else
        return 0u;
#endif
    }

    uint32_t tailSamples() const
    {
        if (params_.amount <= 1.0e-4f) return latencySamples();
        const float feedbackGain = 0.78f * params_.feedback * params_.feedback;
        const float feedbackSeconds = feedbackGain > 1.0e-4f
            ? clamp(std::log(1.0e-5f) / std::log(feedbackGain),
                  0.0f, 128.0f)
                * static_cast<float>(fft_.hopSize()) / sampleRate_
            : 0.0f;
        const float seconds = params_.memoryMs * 0.001f
            + params_.captureReleaseMs * 0.001f + feedbackSeconds;
        const uint64_t samples = static_cast<uint64_t>(latencySamples())
            + static_cast<uint64_t>(seconds * sampleRate_);
        return static_cast<uint32_t>(std::min<uint64_t>(
            samples, 0xfffffffeu));
    }

    bool active() const
    {
        return smoothedAmount_ > 1.0e-4f
            && activityEnvelope_ > 1.0e-5f;
    }

    AcapellaPvocStereoFrame processFrameStereo(float left, float right)
    {
        left = std::isfinite(left) ? clamp(left, -2.0f, 2.0f) : 0.0f;
        right = std::isfinite(right) ? clamp(right, -2.0f, 2.0f) : 0.0f;
        if (!ready_) return { left, right, left, right };

        float wetLeft = 0.0f;
        float wetRight = 0.0f;
        const float* inputs[kAcapellaPvocChannels] { &left, &right };
        float* outputs[kAcapellaPvocChannels] { &wetLeft, &wetRight };
        fft_.processBlock(inputs, outputs, 1u,
            [&](SpectralFrameBlockView frame) { processSpectralFrame(frame); });

        const float dryLeft = dryDelayLeft_[dryWrite_];
        const float dryRight = dryDelayRight_[dryWrite_];
        dryDelayLeft_[dryWrite_] = left;
        dryDelayRight_[dryWrite_] = right;
        if (++dryWrite_ >= dryDelayLeft_.size()) dryWrite_ = 0u;

        // The common WOLA path is extremely close to unity but need not be
        // bit-identical. Make the explicitly neutral transport a true null so
        // hosts and tests never mistake analysis-window residue for an effect.
        if (neutralLiveRequested()) {
            wetLeft = dryLeft;
            wetRight = dryRight;
        }

        smoothedAmount_ += (params_.amount - smoothedAmount_) * 0.0015f;
        const float amount = clamp(smoothedAmount_ * gestureEnvelope_,
            0.0f, 1.0f);

        if (!std::isfinite(wetLeft) || !std::isfinite(wetRight)) {
            ++diagnostics_.nonFiniteRecoveries;
            wetLeft = dryLeft;
            wetRight = dryRight;
            lastOutputValid_ = false;
            headPhaseReady_.fill(false);
        }
        diagnostics_.maximumWetSample = std::max(
            diagnostics_.maximumWetSample,
            std::max(std::abs(wetLeft), std::abs(wetRight)));

        const float wetPeak = std::max(std::abs(wetLeft), std::abs(wetRight));
        const float desiredLimiter = wetPeak > 1.25f ? 1.25f / wetPeak : 1.0f;
        if (desiredLimiter < limiterGain_) {
            limiterGain_ = desiredLimiter;
            ++diagnostics_.outputGuardHits;
        } else {
            limiterGain_ += (1.0f - limiterGain_) * 0.00012f;
        }
        wetLeft *= limiterGain_;
        wetRight *= limiterGain_;

        float outputLeft = dryLeft + (wetLeft - dryLeft) * amount;
        float outputRight = dryRight + (wetRight - dryRight) * amount;
        if (!std::isfinite(outputLeft) || !std::isfinite(outputRight)) {
            ++diagnostics_.nonFiniteRecoveries;
            outputLeft = dryLeft;
            outputRight = dryRight;
        }
        outputLeft = clamp(outputLeft, -1.5f, 1.5f);
        outputRight = clamp(outputRight, -1.5f, 1.5f);

        const float level = std::max(std::abs(outputLeft), std::abs(outputRight));
        if (level > activityEnvelope_) activityEnvelope_ = level;
        else activityEnvelope_ += (level - activityEnvelope_) * 0.00008f;
        return { flushDenormal(outputLeft), flushDenormal(outputRight),
            flushDenormal(dryLeft), flushDenormal(dryRight) };
    }

    AcapellaPvocFrame processFrame(float input)
    {
        const auto stereo = processFrameStereo(input, input);
        return { stereo.left, stereo.dryLeft };
    }

private:
    bool neutralLiveRequested() const
    {
        return params_.mode == AcapellaPvocMode::Live
            && std::abs(params_.pitchSemitones) < 1.0e-4f
            && std::abs(params_.formantSemitones) < 1.0e-4f
            && std::abs(params_.warp) < 1.0e-4f
            && params_.harmonicLock < 1.0e-4f
            && std::abs(params_.peakResidue) < 1.0e-4f
            && params_.partialCloud < 1.0e-4f
            && params_.phaseDrift < 1.0e-4f
            && params_.feedback < 1.0e-4f
            && params_.phaseMode == AcapellaPvocPhaseMode::Identity
            && params_.heads == 1u;
    }

    static uint32_t hash(uint32_t value)
    {
        value ^= value >> 16u;
        value *= 0x7feb352du;
        value ^= value >> 15u;
        value *= 0x846ca68bu;
        value ^= value >> 16u;
        return value;
    }

    static float hashUnit(uint32_t value)
    {
        return static_cast<float>(hash(value) & 0xffffu) / 65535.0f;
    }

    static float hashSigned(uint32_t value)
    {
        return hashUnit(value) * 2.0f - 1.0f;
    }

    static float wrapPhase(float phase)
    {
        return std::remainder(phase, 2.0f * kPi);
    }

    static float lerpPhase(float first, float second, float amount)
    {
        return first + wrapPhase(second - first) * amount;
    }

    static void sinCos(float phase, float& sine, float& cosine)
    {
#if defined(__APPLE__)
        __sincosf(phase, &sine, &cosine);
#else
        sine = std::sin(phase);
        cosine = std::cos(phase);
#endif
    }

    size_t channelIndex(uint32_t channel, uint32_t bin) const
    {
        return static_cast<size_t>(channel) * bins_ + bin;
    }

    size_t headIndex(uint32_t head, uint32_t channel, uint32_t bin) const
    {
        return (static_cast<size_t>(head) * kAcapellaPvocChannels + channel)
            * bins_ + bin;
    }

    size_t historyIndex(uint32_t frame, uint32_t channel, uint32_t bin) const
    {
        return (static_cast<size_t>(frame) * kAcapellaPvocChannels + channel)
            * bins_ + bin;
    }

    uint32_t historyFrameAtAge(uint32_t age) const
    {
        return (historyWrite_ + historyFrameCapacity_
            - std::min<uint32_t>(age, historyFilled_))
            % historyFrameCapacity_;
    }

    float historyValue(const std::vector<float>& history, uint32_t channel,
        uint32_t bin, float ageFrames) const
    {
        if (historyFilled_ == 0u) return 0.0f;
        ageFrames = clamp(ageFrames, 0.0f,
            static_cast<float>(historyFilled_ - 1u));
        const uint32_t recentAge = static_cast<uint32_t>(ageFrames);
        const uint32_t olderAge = std::min<uint32_t>(
            historyFilled_ - 1u, recentAge + 1u);
        const float fraction = ageFrames - static_cast<float>(recentAge);
        const float recent = history[historyIndex(
            historyFrameAtAge(recentAge), channel, bin)];
        const float older = history[historyIndex(
            historyFrameAtAge(olderAge), channel, bin)];
        return lerp(recent, older, fraction);
    }

    float selectedMagnitude(uint32_t channel, uint32_t bin,
        double sourceSerial) const
    {
        if (smoothed_.mode == AcapellaPvocMode::Freeze && freezeValid_) {
            return freezeMagnitude_[channelIndex(channel, bin)];
        }
        const float age = static_cast<float>(
            static_cast<double>(spectralFrame_) - sourceSerial);
        return historyValue(magnitudeHistory_, channel, bin, age);
    }

    float selectedAdvance(uint32_t channel, uint32_t bin,
        double sourceSerial) const
    {
        if (smoothed_.mode == AcapellaPvocMode::Freeze && freezeValid_) {
            return freezeAdvance_[channelIndex(channel, bin)];
        }
        const float age = static_cast<float>(
            static_cast<double>(spectralFrame_) - sourceSerial);
        return historyValue(advanceHistory_, channel, bin, age);
    }

    void smoothParams(float hopSeconds)
    {
        const float coefficient = 1.0f - std::exp(-hopSeconds / 0.055f);
        const auto toward = [coefficient](float current, float target) {
            return current + (target - current) * coefficient;
        };
        smoothed_.amount = toward(smoothed_.amount, params_.amount);
        smoothed_.memoryMs = toward(smoothed_.memoryMs, params_.memoryMs);
        smoothed_.position = toward(smoothed_.position, params_.position);
        smoothed_.speed = toward(smoothed_.speed, params_.speed);
        smoothed_.loopLengthMs = toward(
            smoothed_.loopLengthMs, params_.loopLengthMs);
        smoothed_.timeSpread = toward(smoothed_.timeSpread, params_.timeSpread);
        smoothed_.feedback = toward(smoothed_.feedback, params_.feedback);
        smoothed_.pitchSemitones = toward(
            smoothed_.pitchSemitones, params_.pitchSemitones);
        smoothed_.formantSemitones = toward(
            smoothed_.formantSemitones, params_.formantSemitones);
        smoothed_.warp = toward(smoothed_.warp, params_.warp);
        smoothed_.harmonicLock = toward(
            smoothed_.harmonicLock, params_.harmonicLock);
        smoothed_.peakResidue = toward(
            smoothed_.peakResidue, params_.peakResidue);
        smoothed_.partialCloud = toward(
            smoothed_.partialCloud, params_.partialCloud);
        smoothed_.coherence = toward(smoothed_.coherence, params_.coherence);
        smoothed_.phaseDrift = toward(
            smoothed_.phaseDrift, params_.phaseDrift);
        smoothed_.transientPreserve = toward(
            smoothed_.transientPreserve, params_.transientPreserve);
        smoothed_.captureReleaseMs = toward(
            smoothed_.captureReleaseMs, params_.captureReleaseMs);
        smoothed_.gestureFollow = toward(
            smoothed_.gestureFollow, params_.gestureFollow);
        smoothed_.mode = params_.mode;
        smoothed_.heads = params_.heads;
        smoothed_.phaseMode = params_.phaseMode;
        smoothed_.captureTrigger = params_.captureTrigger;
    }

    void snapshotTransition()
    {
        if (!lastOutputValid_) return;
        transitionReal_ = lastWetReal_;
        transitionImag_ = lastWetImag_;
        transitionFramesRemaining_ = kAcapellaPvocOverlap;
    }

    void armCapture()
    {
        if (capturePending_) return;
        capturePending_ = true;
        pendingCaptureHops_ = 0u;
    }

    void initializeTransport(uint32_t memoryFrames)
    {
        const double current = static_cast<double>(spectralFrame_);
        const double offset = static_cast<double>(smoothed_.position)
            * static_cast<double>(std::max(1u, memoryFrames) - 1u);
        anchorSerial_ = current - offset;
        transportCursor_ = anchorSerial_;
        if (smoothed_.mode == AcapellaPvocMode::Loop) {
            const double loopFrames = clamp(
                smoothed_.loopLengthMs * 0.001f * sampleRate_
                    / static_cast<float>(fft_.hopSize()),
                1.0f, static_cast<float>(std::max(1u, memoryFrames)));
            transportCursor_ = anchorSerial_ - loopFrames;
        }
        scrubPhase_ = 0.0f;
        transportInitialized_ = true;
    }

    void copyFreezeAtAge(float age)
    {
        for (uint32_t channel = 0u;
             channel < kAcapellaPvocChannels; ++channel) {
            for (uint32_t bin = 0u; bin < bins_; ++bin) {
                const size_t index = channelIndex(channel, bin);
                freezeMagnitude_[index] = historyValue(
                    magnitudeHistory_, channel, bin, age);
                freezeAdvance_[index] = historyValue(
                    advanceHistory_, channel, bin, age);
            }
        }
        freezeValid_ = true;
    }

    void commitCapture(uint32_t memoryFrames)
    {
        const uint32_t searchFrames = std::min<uint32_t>(
            historyFilled_ > 0u ? historyFilled_ - 1u : 0u,
            std::min<uint32_t>(pendingCaptureHops_ > 0u
                    ? pendingCaptureHops_ - 1u : 0u,
                7u));
        uint32_t bestAge = 0u;
        float bestEnergy = -1.0f;
        for (uint32_t age = 0u; age <= searchFrames; ++age) {
            const float candidate = historyEnergy_[historyFrameAtAge(age)];
            if (candidate > bestEnergy) {
                bestEnergy = candidate;
                bestAge = age;
            }
        }
        // Wait through the onset guard for a usable voiced frame. Eight hops is
        // the bounded fallback for intentionally quiet or noise-only gestures.
        if (bestEnergy < 1.0e-7f && pendingCaptureHops_ < 8u) return;

        snapshotTransition();
        if (smoothed_.mode == AcapellaPvocMode::Freeze) {
            copyFreezeAtAge(static_cast<float>(bestAge));
        }
        const double selected = static_cast<double>(spectralFrame_ - bestAge);
        anchorSerial_ = selected - static_cast<double>(smoothed_.position)
            * static_cast<double>(std::max(1u, memoryFrames) - 1u);
        transportCursor_ = anchorSerial_;
        if (smoothed_.mode == AcapellaPvocMode::Loop) {
            const double loopFrames = clamp(
                smoothed_.loopLengthMs * 0.001f * sampleRate_
                    / static_cast<float>(fft_.hopSize()),
                1.0f, static_cast<float>(std::max(1u, memoryFrames)));
            transportCursor_ = anchorSerial_ - loopFrames;
        }
        scrubPhase_ = 0.0f;
        headPhaseReady_.fill(false);
        transportInitialized_ = true;
        capturePending_ = false;
        pendingCaptureHops_ = 0u;
        ++diagnostics_.captureCount;
    }

    void updateGestureAndCapture(bool modeChanged, uint32_t memoryFrames,
        float hopSeconds)
    {
        const bool instanceChanged = gesture_.active
            && gesture_.voiceInstance != lastVoiceInstance_;
        const bool stepChanged = gesture_.active
            && (instanceChanged || gesture_.stepIndex != lastGestureStep_);
        bool trigger = modeChanged;
        switch (smoothed_.captureTrigger) {
        case AcapellaPvocCaptureTrigger::Continuous:
            if (continuousCaptureCountdown_ == 0u) {
                trigger = true;
                continuousCaptureCountdown_ = std::max<uint32_t>(1u,
                    static_cast<uint32_t>(smoothed_.captureReleaseMs * 0.001f
                        * sampleRate_ / static_cast<float>(fft_.hopSize())));
            } else {
                --continuousCaptureCountdown_;
            }
            break;
        case AcapellaPvocCaptureTrigger::Note:
            trigger |= instanceChanged;
            break;
        case AcapellaPvocCaptureTrigger::Phoneme:
            trigger |= stepChanged;
            break;
        case AcapellaPvocCaptureTrigger::Syllable:
            trigger |= stepChanged
                && (gesture_.flags & kAcapellaSyllableStart) != 0u;
            break;
        case AcapellaPvocCaptureTrigger::Word:
            trigger |= stepChanged
                && (gesture_.flags & kAcapellaWordStart) != 0u;
            break;
        case AcapellaPvocCaptureTrigger::Rest:
            trigger |= stepChanged
                && (gesture_.flags & kAcapellaForcedRest) != 0u;
            break;
        }
        const bool amountActive = params_.amount > 1.0e-4f;
        trigger |= amountActive && !amountWasActive_;
        amountWasActive_ = amountActive;
        if (trigger) armCapture();

        if (capturePending_) {
            ++pendingCaptureHops_;
            if (pendingCaptureHops_ >= 3u) commitCapture(memoryFrames);
        }

        const bool sounding = gesture_.active
            && gesture_.phoneme != AcapellaPhoneme::Silence
            && (gesture_.flags & kAcapellaForcedRest) == 0u;
        const float target = sounding ? 1.0f : 0.0f;
        const float seconds = target > gestureEnvelope_
            ? lerp(0.035f, 0.012f, smoothed_.gestureFollow)
            : smoothed_.captureReleaseMs * 0.001f;
        const float coefficient = 1.0f
            - std::exp(-hopSeconds / std::max(0.005f, seconds));
        gestureEnvelope_ += (target - gestureEnvelope_) * coefficient;

        if (gesture_.active) {
            lastGestureStep_ = gesture_.stepIndex;
            lastVoiceInstance_ = gesture_.voiceInstance;
        }
    }

    double wrapSerial(double value, double oldest, double newest) const
    {
        const double span = std::max(1.0, newest - oldest + 1.0);
        value = std::fmod(value - oldest, span);
        if (value < 0.0) value += span;
        return oldest + value;
    }

    double advanceTransport(uint32_t memoryFrames, float hopSeconds)
    {
        const double current = static_cast<double>(spectralFrame_);
        const double oldest = current
            - static_cast<double>(std::max(1u, memoryFrames) - 1u);
        if (!transportInitialized_) initializeTransport(memoryFrames);

        double result = transportCursor_;
        switch (smoothed_.mode) {
        case AcapellaPvocMode::Live:
            result = current;
            transportCursor_ = current;
            break;
        case AcapellaPvocMode::Freeze:
            result = current;
            break;
        case AcapellaPvocMode::Stretch:
            result = wrapSerial(transportCursor_, oldest, current);
            transportCursor_ = wrapSerial(
                transportCursor_ + smoothed_.speed, oldest, current);
            break;
        case AcapellaPvocMode::Reverse:
            result = wrapSerial(transportCursor_, oldest, current);
            transportCursor_ = wrapSerial(transportCursor_
                - std::max(0.05, static_cast<double>(
                    std::abs(smoothed_.speed))), oldest, current);
            break;
        case AcapellaPvocMode::Scrub: {
            scrubPhase_ = wrapPhase(scrubPhase_
                + 2.0f * kPi * std::max(0.05f, std::abs(smoothed_.speed))
                    * hopSeconds * (0.22f + 1.78f * smoothed_.timeSpread));
            const double excursion = smoothed_.timeSpread
                * static_cast<double>(memoryFrames) * 0.46;
            result = clamp(anchorSerial_
                + static_cast<double>(std::sin(scrubPhase_)) * excursion,
                oldest, current);
            break;
        }
        case AcapellaPvocMode::Loop: {
            const double loopFrames = clamp(
                smoothed_.loopLengthMs * 0.001f * sampleRate_
                    / static_cast<float>(fft_.hopSize()),
                1.0f, static_cast<float>(memoryFrames));
            const double loopEnd = clamp(anchorSerial_, oldest, current);
            const double loopStart = std::max(oldest, loopEnd - loopFrames);
            result = wrapSerial(transportCursor_, loopStart, loopEnd);
            const double direction = smoothed_.speed < 0.0f ? -1.0 : 1.0;
            transportCursor_ = wrapSerial(transportCursor_ + direction
                * std::max(0.05, static_cast<double>(
                    std::abs(smoothed_.speed))), loopStart, loopEnd);
            break;
        }
        case AcapellaPvocMode::Cloud:
            result = wrapSerial(transportCursor_, oldest, current);
            transportCursor_ = wrapSerial(transportCursor_
                + static_cast<double>(smoothed_.speed), oldest, current);
            break;
        }
        return clamp(result, oldest, current);
    }

    void prepareHeadSource(uint32_t head, uint32_t channel,
        double sourceSerial)
    {
        for (uint32_t bin = 0u; bin < bins_; ++bin) {
            const size_t index = headIndex(head, channel, bin);
            headSourceMagnitude_[index] = std::max(0.0f,
                selectedMagnitude(channel, bin, sourceSerial));
            headSourceAdvance_[index] =
                selectedAdvance(channel, bin, sourceSerial);
        }
        constexpr uint32_t radius = 7u;
        float sum = 0.0f;
        for (uint32_t bin = 0u;
             bin <= std::min<uint32_t>(bins_ - 1u, radius); ++bin) {
            sum += headSourceMagnitude_[headIndex(head, channel, bin)];
        }
        for (uint32_t bin = 0u; bin < bins_; ++bin) {
            const uint32_t low = bin > radius ? bin - radius : 0u;
            const uint32_t high = std::min<uint32_t>(bins_ - 1u, bin + radius);
            headEnvelope_[headIndex(head, channel, bin)] = std::max(
                1.0e-7f, sum / static_cast<float>(high - low + 1u));
            if (bin >= radius) {
                sum -= headSourceMagnitude_[headIndex(
                    head, channel, bin - radius)];
            }
            const uint32_t add = bin + radius + 1u;
            if (add < bins_) {
                sum += headSourceMagnitude_[headIndex(head, channel, add)];
            }
        }
    }

    float headValue(const std::vector<float>& values, uint32_t head,
        uint32_t channel, float binPosition) const
    {
        if (binPosition < 0.0f
            || binPosition > static_cast<float>(bins_ - 1u)) return 0.0f;
        const uint32_t low = static_cast<uint32_t>(binPosition);
        const uint32_t high = std::min<uint32_t>(bins_ - 1u, low + 1u);
        return lerp(values[headIndex(head, channel, low)],
            values[headIndex(head, channel, high)],
            binPosition - static_cast<float>(low));
    }

    void recoverSpectralFrame(SpectralFrameBlockView frame)
    {
        ++diagnostics_.nonFiniteRecoveries;
        for (uint32_t channel = 0u;
             channel < kAcapellaPvocChannels; ++channel) {
            float* real = frame.real(channel);
            float* imag = frame.imag(channel);
            for (uint32_t bin = 0u; bin < bins_; ++bin) {
                const size_t index = channelIndex(channel, bin);
                real[bin] = std::isfinite(liveReal_[index])
                    ? liveReal_[index] : 0.0f;
                imag[bin] = std::isfinite(liveImag_[index])
                    ? liveImag_[index] : 0.0f;
                previousMagnitude_[index] = 0.0f;
                previousAnalysisPhase_[index] = 0.0f;
                feedbackMagnitude_[index] = 0.0f;
                feedbackAdvance_[index] = 0.0f;
            }
        }
        analysisReady_ = false;
        headPhaseReady_.fill(false);
        lastOutputValid_ = false;
    }

    void processSpectralFrame(SpectralFrameBlockView frame)
    {
        if (frame.channels != kAcapellaPvocChannels
            || frame.bins != bins_ || bins_ == 0u) return;
        constexpr float epsilon = 1.0e-12f;
        const float hopSeconds = static_cast<float>(fft_.hopSize()) / sampleRate_;
        const float binHz = sampleRate_ / static_cast<float>(frame.fftSize);
        const float expectedScale = 2.0f * kPi
            * static_cast<float>(fft_.hopSize())
            / static_cast<float>(frame.fftSize);
        smoothParams(hopSeconds);

        float liveEnergy = 0.0f;
        float magnitudeSum = 0.0f;
        float positiveFlux = 0.0f;
        bool finite = true;
        for (uint32_t channel = 0u;
             channel < kAcapellaPvocChannels; ++channel) {
            float* real = frame.real(channel);
            float* imag = frame.imag(channel);
            for (uint32_t bin = 0u; bin < bins_; ++bin) {
                const size_t index = channelIndex(channel, bin);
                const float inputReal = real[bin];
                const float inputImag = imag[bin];
                finite = finite && std::isfinite(inputReal)
                    && std::isfinite(inputImag);
                const float magnitude = std::sqrt(
                    inputReal * inputReal + inputImag * inputImag);
                const float phase = std::atan2(inputImag, inputReal);
                const float expected = expectedScale * static_cast<float>(bin);
                const float residual = analysisReady_
                    ? wrapPhase(phase - previousAnalysisPhase_[index]
                        - expected)
                    : 0.0f;
                const float advance = expected + residual;
                liveReal_[index] = inputReal;
                liveImag_[index] = inputImag;
                liveMagnitude_[index] = magnitude;
                livePhase_[index] = phase;
                liveAdvance_[index] = advance;
                positiveFlux += std::max(
                    0.0f, magnitude - previousMagnitude_[index]);
                magnitudeSum += magnitude;
                liveEnergy += magnitude * magnitude;
                previousMagnitude_[index] = magnitude;
                previousAnalysisPhase_[index] = phase;
                magnitudeHistory_[historyIndex(
                    historyWrite_, channel, bin)] = magnitude;
                advanceHistory_[historyIndex(
                    historyWrite_, channel, bin)] = advance;
            }
        }
        if (!finite) {
            recoverSpectralFrame(frame);
            return;
        }
        analysisReady_ = true;
        historyEnergy_[historyWrite_] = liveEnergy
            / static_cast<float>(kAcapellaPvocChannels * bins_);
        historyFilled_ = std::min<uint32_t>(
            historyFrameCapacity_, historyFilled_ + 1u);
        fluxEnvelope_ += (positiveFlux
            / std::max(magnitudeSum, 1.0e-8f) - fluxEnvelope_) * 0.28f;

        const uint32_t requestedMemory = static_cast<uint32_t>(std::ceil(
            smoothed_.memoryMs * 0.001f * sampleRate_
                / static_cast<float>(fft_.hopSize())));
        const uint32_t memoryFrames = std::clamp<uint32_t>(requestedMemory,
            1u, std::max(1u, historyFilled_));
        const bool modeChanged = smoothed_.mode != lastMode_;
        if (modeChanged) {
            snapshotTransition();
            headPhaseReady_.fill(false);
            freezeValid_ = false;
            transportInitialized_ = false;
        }
        updateGestureAndCapture(modeChanged, memoryFrames, hopSeconds);
        const double baseSourceSerial = advanceTransport(memoryFrames, hopSeconds);

        const bool neutralLive = smoothed_.mode == AcapellaPvocMode::Live
            && std::abs(smoothed_.pitchSemitones) < 1.0e-4f
            && std::abs(smoothed_.formantSemitones) < 1.0e-4f
            && std::abs(smoothed_.warp) < 1.0e-4f
            && smoothed_.harmonicLock < 1.0e-4f
            && std::abs(smoothed_.peakResidue) < 1.0e-4f
            && smoothed_.partialCloud < 1.0e-4f
            && smoothed_.phaseDrift < 1.0e-4f
            && smoothed_.feedback < 1.0e-4f
            && smoothed_.phaseMode == AcapellaPvocPhaseMode::Identity
            && smoothed_.heads == 1u;

        if (neutralLive && !modeChanged
            && transitionFramesRemaining_ == 0u) {
            for (uint32_t channel = 0u;
                 channel < kAcapellaPvocChannels; ++channel) {
                for (uint32_t bin = 0u; bin < bins_; ++bin) {
                    const size_t index = channelIndex(channel, bin);
                    feedbackMagnitude_[index] = 0.0f;
                    feedbackAdvance_[index] = 0.0f;
                    lastWetReal_[index] = liveReal_[index];
                    lastWetImag_[index] = liveImag_[index];
                }
            }
            lastOutputValid_ = true;
            historyWrite_ = (historyWrite_ + 1u) % historyFrameCapacity_;
            ++spectralFrame_;
            lastMode_ = smoothed_.mode;
            return;
        }

        const uint32_t effectiveHeads = neutralLive ? 1u
            : std::clamp<uint32_t>(std::max(smoothed_.heads,
                  1u + static_cast<uint32_t>(std::round(
                      smoothed_.partialCloud
                      * static_cast<float>(kAcapellaPvocMaxHeads - 1u)))),
                  1u, kAcapellaPvocMaxHeads);
        const float feedbackMix = 0.78f
            * smoothed_.feedback * smoothed_.feedback;
        const float pitchRatio = std::exp2(smoothed_.pitchSemitones / 12.0f);
        const float formantRatio = std::exp2(
            smoothed_.formantSemitones / 12.0f);
        const float warpExponent = std::exp2(smoothed_.warp * 0.68f);
        const float fundamental = clamp(gesture_.frequencyHz,
            35.0f, sampleRate_ * 0.20f);
        const float spectralMagnitudeCeiling
            = static_cast<float>(frame.fftSize) * 2.25f;
        for (uint32_t bin = 0u; bin < bins_; ++bin) {
            const float normalized = static_cast<float>(bin)
                / static_cast<float>(std::max(1u, bins_ - 1u));
            warpedBin_[bin] = std::pow(normalized, 1.0f / warpExponent)
                * static_cast<float>(bins_ - 1u);
        }

        std::fill(referencePower_.begin(), referencePower_.end(), 0.0f);
        for (uint32_t head = 0u; head < effectiveHeads; ++head) {
            const float random = hashSigned(head * 2654435761u
                + gesture_.stepIndex * 2246822519u + 0x32564f50u);
            double spread = static_cast<double>(random * smoothed_.timeSpread)
                * static_cast<double>(memoryFrames) * 0.38;
            if (smoothed_.mode == AcapellaPvocMode::Cloud) {
                spread += static_cast<double>(std::sin(
                    static_cast<float>(spectralFrame_) * (0.019f
                        + static_cast<float>(head) * 0.004f)
                    + random * kPi)) * smoothed_.timeSpread
                    * static_cast<double>(memoryFrames) * 0.24;
            }
            const double current = static_cast<double>(spectralFrame_);
            const double oldest = current
                - static_cast<double>(std::max(1u, memoryFrames) - 1u);
            const double sourceSerial = clamp(
                baseSourceSerial + spread, oldest, current);
            for (uint32_t channel = 0u;
                 channel < kAcapellaPvocChannels; ++channel) {
                prepareHeadSource(head, channel, sourceSerial);
            }

            const float detune = random * smoothed_.partialCloud * 4.5f;
            const float headPitchRatio = pitchRatio * std::exp2(detune / 12.0f);
            for (uint32_t bin = 0u; bin < bins_; ++bin) {
                headSourceBin_[static_cast<size_t>(head) * bins_ + bin]
                    = warpedBin_[bin] / headPitchRatio;
            }
            for (uint32_t channel = 0u;
                 channel < kAcapellaPvocChannels; ++channel) {
                for (uint32_t bin = 0u; bin < bins_; ++bin) {
                    const float sourceBin = headSourceBin_[
                        static_cast<size_t>(head) * bins_ + bin];
                    const size_t index = headIndex(head, channel, bin);
                    if (sourceBin < 0.0f
                        || sourceBin > static_cast<float>(bins_ - 1u)) {
                        headMagnitude_[index] = 0.0f;
                        headAdvance_[index] = 0.0f;
                        continue;
                    }
                    const float sourceMagnitude = headValue(
                        headSourceMagnitude_, head, channel, sourceBin);
                    const float sourceEnvelope = std::max(1.0e-7f,
                        headValue(headEnvelope_, head, channel, sourceBin));
                    const float formantBin = clamp(sourceBin / formantRatio,
                        0.0f, static_cast<float>(bins_ - 1u));
                    const float shiftedEnvelope = std::max(1.0e-7f,
                        headValue(headEnvelope_, head, channel, formantBin));
                    const float envelopeRatio = clamp(
                        shiftedEnvelope / sourceEnvelope, 0.42f, 2.35f);
                    float magnitude = sourceMagnitude * envelopeRatio;

                    const float prominence = clamp(
                        (sourceMagnitude / sourceEnvelope - 1.0f) / 2.5f,
                        0.0f, 1.0f);
                    const float isolate = smoothed_.peakResidue >= 0.0f
                        ? 0.22f + 0.78f * prominence
                        : 1.0f - 0.82f * prominence;
                    magnitude *= lerp(1.0f, isolate,
                        std::abs(smoothed_.peakResidue));

                    const size_t channelBin = channelIndex(channel, bin);
                    const float sourceBeforeFeedback = magnitude;
                    const float feedbackMagnitude =
                        feedbackMagnitude_[channelBin];
                    magnitude = lerp(sourceBeforeFeedback,
                        feedbackMagnitude, feedbackMix);
                    float sourceAdvance = headValue(
                        headSourceAdvance_, head, channel, sourceBin);
                    const float feedbackWeight = feedbackMix
                        * feedbackMagnitude / std::max(magnitude, 1.0e-8f);
                    sourceAdvance = lerp(sourceAdvance,
                        feedbackAdvance_[channelBin],
                        clamp(feedbackWeight, 0.0f, 1.0f));
                    const float sourceFrequency = sourceAdvance * sampleRate_
                        / (2.0f * kPi * static_cast<float>(fft_.hopSize()));
                    const float sourceCenter = sourceBin * binHz;
                    const float destinationCenter = static_cast<float>(bin) * binHz;
                    float targetFrequency = destinationCenter
                        + (sourceFrequency - sourceCenter) * headPitchRatio;
                    if (targetFrequency > 0.0f) {
                        const float harmonic = std::max(1.0f,
                            std::round(targetFrequency / fundamental));
                        targetFrequency = lerp(targetFrequency,
                            harmonic * fundamental, smoothed_.harmonicLock);
                    }
                    targetFrequency = clamp(targetFrequency,
                        0.0f, sampleRate_ * 0.5f);
                    const float targetAdvance = 2.0f * kPi * targetFrequency
                        * static_cast<float>(fft_.hopSize()) / sampleRate_;

                    if (!std::isfinite(magnitude)
                        || !std::isfinite(targetAdvance)) {
                        recoverSpectralFrame(frame);
                        return;
                    }
                    if (magnitude > spectralMagnitudeCeiling) {
                        magnitude = spectralMagnitudeCeiling;
                        ++diagnostics_.spectralGuardHits;
                    }
                    diagnostics_.maximumSpectralMagnitude = std::max(
                        diagnostics_.maximumSpectralMagnitude, magnitude);
                    headMagnitude_[index] = magnitude;
                    headAdvance_[index] = targetAdvance;
                    referencePower_[channelBin] += magnitude * magnitude
                        / static_cast<float>(effectiveHeads);
                }
            }
        }

        const bool consonant = gesture_.active
            && !acapellaPhonemeIsVowel(gesture_.phoneme)
            && gesture_.phoneme != AcapellaPhoneme::Silence;
        const float transient = clamp(fluxEnvelope_ * 2.1f
            + (consonant ? 0.38f : 0.0f)
            + (((gesture_.flags
                    & (kAcapellaSyllableStart | kAcapellaWordStart)) != 0u)
                    ? 0.16f : 0.0f),
            0.0f, 1.0f);
        const float reacquire = smoothed_.transientPreserve * transient;

        // Advance every phase before looking up a locking peak. This makes a
        // peak and all bins in its region refer to the same synthesis instant.
        for (uint32_t head = 0u; head < effectiveHeads; ++head) {
            const float random = hashSigned(head * 747796405u + 2891336453u);
            for (uint32_t channel = 0u;
                 channel < kAcapellaPvocChannels; ++channel) {
                for (uint32_t bin = 0u; bin < bins_; ++bin) {
                    const size_t index = headIndex(head, channel, bin);
                    const size_t liveIndex = channelIndex(channel, bin);
                    if (!headPhaseReady_[head]) {
                        headSynthesisPhase_[index] = livePhase_[liveIndex]
                            + random * smoothed_.partialCloud * 0.35f * kPi;
                    } else {
                        headSynthesisPhase_[index] = wrapPhase(
                            headSynthesisPhase_[index] + headAdvance_[index]);
                    }
                    headSynthesisPhase_[index] = lerpPhase(
                        headSynthesisPhase_[index], livePhase_[liveIndex],
                        reacquire * 0.72f);
                }
            }
            headPhaseReady_[head] = true;
        }

        constexpr uint32_t peakRadius = 3u;
        for (uint32_t head = 0u; head < effectiveHeads; ++head) {
            for (uint32_t channel = 0u;
                 channel < kAcapellaPvocChannels; ++channel) {
                for (uint32_t bin = 0u; bin < bins_; ++bin) {
                    const uint32_t low = bin > peakRadius ? bin - peakRadius : 0u;
                    const uint32_t high = std::min<uint32_t>(
                        bins_ - 1u, bin + peakRadius);
                    uint32_t peak = bin;
                    float strongest = headMagnitude_[headIndex(
                        head, channel, bin)];
                    for (uint32_t candidate = low;
                         candidate <= high; ++candidate) {
                        const float value = headMagnitude_[headIndex(
                            head, channel, candidate)];
                        if (value > strongest) {
                            strongest = value;
                            peak = candidate;
                        }
                    }
                    nearestPeak_[headIndex(head, channel, bin)] = peak;
                }
            }
        }

        for (uint32_t channel = 0u;
             channel < kAcapellaPvocChannels; ++channel) {
            std::fill(frame.real(channel), frame.real(channel) + bins_, 0.0f);
            std::fill(frame.imag(channel), frame.imag(channel) + bins_, 0.0f);
        }
        const float headWeight = 1.0f
            / std::sqrt(static_cast<float>(effectiveHeads));
        for (uint32_t head = 0u; head < effectiveHeads; ++head) {
            const float random = hashSigned(head * 277803737u + 0x50484153u);
            float motionSine = 0.0f;
            float motionCosine = 1.0f;
            float stepSine = 0.0f;
            float stepCosine = 1.0f;
            sinCos(static_cast<float>(spectralFrame_) * 0.041f
                + random * kPi, motionSine, motionCosine);
            sinCos(0.067f, stepSine, stepCosine);
            for (uint32_t bin = 0u; bin < bins_; ++bin) {
                headPhaseMotion_[static_cast<size_t>(head) * bins_ + bin]
                    = motionSine;
                const float nextSine = motionSine * stepCosine
                    + motionCosine * stepSine;
                motionCosine = motionCosine * stepCosine
                    - motionSine * stepSine;
                motionSine = nextSine;
            }
            for (uint32_t channel = 0u;
                 channel < kAcapellaPvocChannels; ++channel) {
                float* real = frame.real(channel);
                float* imag = frame.imag(channel);
                for (uint32_t bin = 0u; bin < bins_; ++bin) {
                    const size_t index = headIndex(head, channel, bin);
                    const uint32_t peak = nearestPeak_[index];
                    const size_t peakIndex = headIndex(head, channel, peak);
                    float lockAmount = 0.0f;
                    switch (smoothed_.phaseMode) {
                    case AcapellaPvocPhaseMode::Identity:
                        break;
                    case AcapellaPvocPhaseMode::PeakLocked:
                        lockAmount = smoothed_.coherence;
                        break;
                    case AcapellaPvocPhaseMode::Loose:
                        lockAmount = smoothed_.coherence * 0.28f;
                        break;
                    case AcapellaPvocPhaseMode::Diffuse:
                        break;
                    }
                    const float liveOffset = wrapPhase(
                        livePhase_[channelIndex(channel, bin)]
                        - livePhase_[channelIndex(channel, peak)]);
                    const float locked = headSynthesisPhase_[peakIndex]
                        + liveOffset;
                    float phase = lerpPhase(
                        headSynthesisPhase_[index], locked, lockAmount);
                    const float diffuse = smoothed_.phaseMode
                            == AcapellaPvocPhaseMode::Diffuse
                        ? 1.0f - 0.68f * smoothed_.coherence
                        : 0.18f * (1.0f - smoothed_.coherence);
                    phase += (diffuse + smoothed_.phaseDrift * 0.72f)
                        * 0.72f * kPi * headPhaseMotion_[
                            static_cast<size_t>(head) * bins_ + bin];
                    float sine = 0.0f;
                    float cosine = 1.0f;
                    sinCos(phase, sine, cosine);
                    const float magnitude = headMagnitude_[index] * headWeight;
                    real[bin] += magnitude * cosine;
                    imag[bin] += magnitude * sine;
                }
            }
        }

        float rawEnergy = 0.0f;
        float referenceEnergy = 0.0f;
        for (uint32_t channel = 0u;
             channel < kAcapellaPvocChannels; ++channel) {
            float* real = frame.real(channel);
            float* imag = frame.imag(channel);
            for (uint32_t bin = 0u; bin < bins_; ++bin) {
                const size_t index = channelIndex(channel, bin);
                rawEnergy += real[bin] * real[bin] + imag[bin] * imag[bin];
                referenceEnergy += referencePower_[index];
            }
        }
        float frameGain = rawEnergy > epsilon
            ? clamp(std::sqrt(referenceEnergy / rawEnergy), 0.50f, 1.50f)
            : 1.0f;
        const float maximumEnergy = referenceEnergy * 1.20f + epsilon;
        if (rawEnergy * frameGain * frameGain > maximumEnergy) {
            frameGain = std::sqrt(maximumEnergy / std::max(rawEnergy, epsilon));
        }
        diagnostics_.maximumFrameGain = std::max(
            diagnostics_.maximumFrameGain, frameGain);

        float finalEnergy = 0.0f;
        const float liveBlend = reacquire * 0.28f;
        const float transitionAmount = transitionFramesRemaining_ > 0u
            ? static_cast<float>(transitionFramesRemaining_)
                / static_cast<float>(kAcapellaPvocOverlap + 1u)
            : 0.0f;
        for (uint32_t channel = 0u;
             channel < kAcapellaPvocChannels; ++channel) {
            float* real = frame.real(channel);
            float* imag = frame.imag(channel);
            for (uint32_t bin = 0u; bin < bins_; ++bin) {
                const size_t index = channelIndex(channel, bin);
                float nextReal = real[bin] * frameGain;
                float nextImag = imag[bin] * frameGain;
                nextReal = lerp(nextReal, liveReal_[index], liveBlend);
                nextImag = lerp(nextImag, liveImag_[index], liveBlend);
                if (transitionFramesRemaining_ > 0u) {
                    nextReal = lerp(nextReal,
                        transitionReal_[index], transitionAmount);
                    nextImag = lerp(nextImag,
                        transitionImag_[index], transitionAmount);
                }
                if (!std::isfinite(nextReal) || !std::isfinite(nextImag)) {
                    recoverSpectralFrame(frame);
                    return;
                }
                real[bin] = flushDenormal(nextReal);
                imag[bin] = bin == 0u || bin + 1u == bins_
                    ? 0.0f : flushDenormal(nextImag);
                finalEnergy += real[bin] * real[bin] + imag[bin] * imag[bin];
            }
        }

        // A second linked governor covers transition/live-phase reacquisition.
        // It is never part of feedback, so it cannot create a positive loop.
        const float finalReference = std::max(referenceEnergy, liveEnergy);
        const float finalCeiling = finalReference * 1.35f + epsilon;
        const float finalGain = finalEnergy > finalCeiling
            ? std::sqrt(finalCeiling / finalEnergy) : 1.0f;
        diagnostics_.maximumFrameGain = std::max(
            diagnostics_.maximumFrameGain, frameGain * finalGain);
        for (uint32_t channel = 0u;
             channel < kAcapellaPvocChannels; ++channel) {
            float* real = frame.real(channel);
            float* imag = frame.imag(channel);
            for (uint32_t bin = 0u; bin < bins_; ++bin) {
                const size_t index = channelIndex(channel, bin);
                real[bin] = flushDenormal(real[bin] * finalGain);
                imag[bin] = flushDenormal(imag[bin] * finalGain);
                lastWetReal_[index] = real[bin];
                lastWetImag_[index] = imag[bin];
                // Convex feedback reads the per-head reference magnitude, not
                // output makeup gain or coherent multi-head summation.
                feedbackMagnitude_[index] = std::min(
                    spectralMagnitudeCeiling,
                    std::sqrt(std::max(0.0f, referencePower_[index])));
                feedbackAdvance_[index] = headAdvance_[headIndex(
                    0u, channel, bin)];
            }
        }
        lastOutputValid_ = true;
        if (transitionFramesRemaining_ > 0u) --transitionFramesRemaining_;
        historyWrite_ = (historyWrite_ + 1u) % historyFrameCapacity_;
        ++spectralFrame_;
        lastMode_ = smoothed_.mode;
    }

    float sampleRate_ = 48000.0f;
    uint32_t bins_ = 0u;
    SpectralFftProcessor fft_ {};
    AcapellaPvocParams params_ {};
    AcapellaPvocParams smoothed_ {};
    AcapellaPvocGesture gesture_ {};
    AcapellaPvocDiagnostics diagnostics_ {};

    std::vector<float> magnitudeHistory_;
    std::vector<float> advanceHistory_;
    std::vector<float> historyEnergy_;
    std::vector<float> liveReal_;
    std::vector<float> liveImag_;
    std::vector<float> liveMagnitude_;
    std::vector<float> livePhase_;
    std::vector<float> liveAdvance_;
    std::vector<float> previousMagnitude_;
    std::vector<float> previousAnalysisPhase_;
    std::vector<float> freezeMagnitude_;
    std::vector<float> freezeAdvance_;
    std::vector<float> feedbackMagnitude_;
    std::vector<float> feedbackAdvance_;
    std::vector<float> referencePower_;
    std::vector<float> warpedBin_;
    std::vector<float> transitionReal_;
    std::vector<float> transitionImag_;
    std::vector<float> lastWetReal_;
    std::vector<float> lastWetImag_;
    std::vector<float> headSourceMagnitude_;
    std::vector<float> headSourceAdvance_;
    std::vector<float> headEnvelope_;
    std::vector<float> headMagnitude_;
    std::vector<float> headAdvance_;
    std::vector<float> headSynthesisPhase_;
    std::vector<uint32_t> nearestPeak_;
    std::vector<float> headSourceBin_;
    std::vector<float> headPhaseMotion_;
    std::vector<float> dryDelayLeft_;
    std::vector<float> dryDelayRight_;

    std::array<bool, kAcapellaPvocMaxHeads> headPhaseReady_ {};
    float smoothedAmount_ = 0.0f;
    float gestureEnvelope_ = 0.0f;
    float activityEnvelope_ = 0.0f;
    float limiterGain_ = 1.0f;
    float fluxEnvelope_ = 0.0f;
    double anchorSerial_ = 0.0;
    double transportCursor_ = 0.0;
    float scrubPhase_ = 0.0f;
    uint32_t historyFrameCapacity_ = 2u;
    uint32_t historyWrite_ = 0u;
    uint32_t historyFilled_ = 0u;
    uint64_t spectralFrame_ = 0u;
    uint32_t dryWrite_ = 0u;
    uint32_t continuousCaptureCountdown_ = 0u;
    uint32_t pendingCaptureHops_ = 0u;
    uint32_t transitionFramesRemaining_ = 0u;
    uint32_t lastGestureStep_ = 0xffffffffu;
    uint64_t lastVoiceInstance_ = std::numeric_limits<uint64_t>::max();
    AcapellaPvocMode lastMode_ = AcapellaPvocMode::Freeze;
    bool capturePending_ = false;
    bool freezeValid_ = false;
    bool analysisReady_ = false;
    bool transportInitialized_ = false;
    bool lastOutputValid_ = false;
    bool amountWasActive_ = false;
    bool ready_ = false;
};

} // namespace s3g
