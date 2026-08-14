#pragma once

#include "s3g_analog_drive_circuits.h"
#include "s3g_math.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace s3g {

enum class BassShredCircuit : uint32_t {
    Shred = 0u,
    Wool,
    Rat,
    ZoneA,
    ZoneB,
    FuzzI,
    FuzzII,
    Diode,
    Count,
};

inline constexpr uint32_t kBassShredCircuitCount =
    static_cast<uint32_t>(BassShredCircuit::Count);

inline const char* bassShredCircuitName(BassShredCircuit circuit)
{
    if (circuit == BassShredCircuit::Shred) return "SHRED";
    const uint32_t index = static_cast<uint32_t>(circuit);
    if (index > 0u && index < kBassShredCircuitCount) {
        return analogDriveCircuitName(
            static_cast<AnalogDriveCircuit>(index - 1u));
    }
    return "SHRED";
}

struct BassShredParams {
    float shred = 0.0f;
    float feedback = 0.0f;
    float feedbackToneLevel = 1.0f;
    float color = 0.55f;
    float mix = 0.0f;
    BassShredCircuit circuit = BassShredCircuit::Shred;
};

struct BassShredCoefficients {
    float split = 0.02f;
    float loopLowpass = 0.20f;
    float postLowpass = 0.12f;
    float delaySamples = 96.0f;
};

// A bass-specific stereo adaptation of Macro Shred's folded transfer and
// governed resonant delay. The fundamental is reconstructed from a clean
// low-pass branch outside both the clipper and the feedback loop. Independent
// left/right loops receive a small tuning offset, while all regenerated energy
// is high-passed again before it rejoins the protected low branch.
class BassShredCore {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        const auto size = static_cast<size_t>(
            std::ceil(sampleRate_ * 0.015)) + 4u;
        delay_.assign(std::max<size_t>(size, 8u), 0.0f);
        const float sr = static_cast<float>(sampleRate_);
        delaySmoothingCoefficient_ = 1.0f
            - std::exp(-1.0f / (sr * 0.040f));
        safetyCoefficient_ = 1.0f
            - std::exp(-1.0f / (sr * 0.012f));
        activityCoefficient_ = 1.0f
            - std::exp(-1.0f / (sr * 0.060f));
        sourceAttackCoefficient_ = 1.0f
            - std::exp(-1.0f / (sr * 0.003f));
        sourceReleaseCoefficient_ = 1.0f
            - std::exp(-1.0f / (sr * 0.180f));
        feedbackGovernorAttackCoefficient_ = 1.0f
            - std::exp(-1.0f / (sr * 0.002f));
        feedbackGovernorReleaseCoefficient_ = 1.0f
            - std::exp(-1.0f / (sr * 0.045f));
        circuitFadeCoefficient_ = 1.0f
            / std::max(1.0f, sr * 0.020f);
        dcPole_ = std::exp(-2.0f * kPi * 18.0f / sr);
        reset();
    }

    void reset(BassShredCircuit circuit = BassShredCircuit::Shred)
    {
        std::fill(delay_.begin(), delay_.end(), 0.0f);
        writeIndex_ = 0u;
        delaySamples_ = 96.0f;
        cleanLowOne_ = 0.0f;
        cleanLowTwo_ = 0.0f;
        dirtyLowOne_ = 0.0f;
        dirtyLowTwo_ = 0.0f;
        dryAnchorOne_ = 0.0f;
        dryAnchorTwo_ = 0.0f;
        wetAnchorOne_ = 0.0f;
        wetAnchorTwo_ = 0.0f;
        loopDcInput_ = 0.0f;
        loopDcOutput_ = 0.0f;
        loopLowpass_ = 0.0f;
        loopEnvelope_ = 0.0f;
        sourceEnvelope_ = 0.0f;
        feedbackToneEnvelope_ = 0.0f;
        feedbackToneGovernor_ = 1.0f;
        postLowpass_ = 0.0f;
        previousExcitation_ = 0.0f;
        previousSourceExcitation_ = 0.0f;
        activity_ = 0.0f;
        circuitStates_.fill({});
        sourceCircuitStates_.fill({});
        activeCircuit_ = circuit;
        previousCircuit_ = circuit;
        circuitFade_ = 1.0f;
    }

    float processSample(float input, float shred, float feedback,
        float feedbackToneLevel, float color, BassShredCircuit circuit,
        const BassShredCoefficients& coefficients)
    {
        if (delay_.empty()) return input;
        input = std::isfinite(input) ? input : 0.0f;
        shred = clamp(std::isfinite(shred) ? shred : 0.0f,
            0.0f, 1.0f);
        feedback = clamp(std::isfinite(feedback) ? feedback : 0.0f,
            0.0f, 1.0f);
        feedbackToneLevel = clamp(std::isfinite(feedbackToneLevel)
                ? feedbackToneLevel : 1.0f,
            0.0f, 1.0f);
        color = clamp(std::isfinite(color) ? color : 0.55f,
            0.0f, 1.0f);
        const uint32_t circuitIndex = std::min<uint32_t>(
            static_cast<uint32_t>(circuit), kBassShredCircuitCount - 1u);
        circuit = static_cast<BassShredCircuit>(circuitIndex);

        const float split = clamp(coefficients.split, 0.00001f, 0.95f);
        cleanLowOne_ += (input - cleanLowOne_) * split;
        cleanLowTwo_ += (cleanLowOne_ - cleanLowTwo_) * split;
        cleanLowOne_ = flushDenormal(cleanLowOne_);
        cleanLowTwo_ = flushDenormal(cleanLowTwo_);
        const float upper = input - cleanLowTwo_;
        const float sourceMagnitude = std::fabs(upper);
        const float sourceCoefficient = sourceMagnitude > sourceEnvelope_
            ? sourceAttackCoefficient_ : sourceReleaseCoefficient_;
        sourceEnvelope_ += (sourceMagnitude - sourceEnvelope_)
            * sourceCoefficient;
        sourceEnvelope_ = flushDenormal(sourceEnvelope_);

        const float maximumDelay = static_cast<float>(delay_.size() - 2u);
        const float delayTarget = clamp(coefficients.delaySamples,
            2.0f, maximumDelay);
        delaySamples_ += (delayTarget - delaySamples_)
            * delaySmoothingCoefficient_;
        const float delayed = readDelay(delaySamples_);
        const float loopDc = delayed - loopDcInput_
            + dcPole_ * loopDcOutput_;
        loopDcInput_ = delayed;
        loopDcOutput_ = flushDenormal(loopDc);
        loopLowpass_ += (loopDcOutput_ - loopLowpass_)
            * clamp(coefficients.loopLowpass, 0.00001f, 0.95f);
        loopLowpass_ = flushDenormal(loopLowpass_);

        loopEnvelope_ += (std::fabs(loopLowpass_) - loopEnvelope_)
            * safetyCoefficient_;
        const float excess = std::max(0.0f, loopEnvelope_ - 0.48f);
        const float governor = 1.0f / (1.0f + excess * 12.0f);
        const float sourceGate = clamp(sourceEnvelope_ * 18.0f,
            0.0f, 1.0f);
        const float feedbackGain = std::min(0.945f,
            feedback * (0.46f + feedback * 0.49f))
            * governor * sourceGate;

        const float drive = 1.0f + shred * shred * 11.0f;
        const float sourceExcitation = clamp(
            upper * (1.0f + shred * 1.8f), -6.0f, 6.0f);
        const float excitation = clamp(sourceExcitation
                + loopLowpass_ * feedbackGain,
            -6.0f, 6.0f);
        const float midpoint = 0.5f * (previousExcitation_ + excitation);
        const float sourceMidpoint = 0.5f
            * (previousSourceExcitation_ + sourceExcitation);
        if (circuit != activeCircuit_) {
            previousCircuit_ = activeCircuit_;
            activeCircuit_ = circuit;
            circuitFade_ = 0.0f;
        }
        const float circuitBias = (0.5f - color) * 0.10f;
        const auto shapePair = [&](BassShredCircuit selected,
                                   float pairMidpoint,
                                   float pairExcitation,
                                   auto& states) {
            return 0.5f * (
                processCircuit(selected, pairMidpoint, shred, color,
                    circuitBias, drive, states)
                + processCircuit(selected, pairExcitation, shred, color,
                    circuitBias, drive, states));
        };
        const float activeShape = shapePair(activeCircuit_, midpoint,
            excitation, circuitStates_);
        const float activeSourceShape = shapePair(activeCircuit_,
            sourceMidpoint, sourceExcitation, sourceCircuitStates_);
        float loopShaped = activeShape;
        float sourceShaped = activeSourceShape;
        if (circuitFade_ < 1.0f) {
            const float previousShape = shapePair(previousCircuit_, midpoint,
                excitation, circuitStates_);
            const float previousSourceShape = shapePair(previousCircuit_,
                sourceMidpoint, sourceExcitation, sourceCircuitStates_);
            loopShaped = lerp(previousShape, activeShape, circuitFade_);
            sourceShaped = lerp(previousSourceShape, activeSourceShape,
                circuitFade_);
            circuitFade_ = std::min(1.0f,
                circuitFade_ + circuitFadeCoefficient_);
        }
        previousExcitation_ = excitation;
        previousSourceExcitation_ = sourceExcitation;
        delay_[writeIndex_] = flushDenormal(std::tanh(loopShaped * 0.90f));
        writeIndex_ = (writeIndex_ + 1u) % delay_.size();

        // Keep regeneration independent from its audible return. The loop is
        // always written from the fully regenerated circuit path, while this
        // parallel source-only path lets Feedback Tone Level remove just the
        // delayed nonlinear contribution. A slow-recovery energy governor
        // contains interruption transients without shortening the loop.
        const float feedbackTone = loopShaped - sourceShaped;
        const float feedbackToneMagnitude = std::fabs(feedbackTone);
        const float feedbackEnvelopeCoefficient =
            feedbackToneMagnitude > feedbackToneEnvelope_
                ? feedbackGovernorAttackCoefficient_
                : feedbackGovernorReleaseCoefficient_;
        feedbackToneEnvelope_ += (feedbackToneMagnitude
                - feedbackToneEnvelope_) * feedbackEnvelopeCoefficient;
        feedbackToneEnvelope_ = flushDenormal(feedbackToneEnvelope_);
        const float feedbackExcess = std::max(
            0.0f, feedbackToneEnvelope_ - 0.82f);
        const float feedbackGovernorTarget =
            1.0f / (1.0f + feedbackExcess * 7.0f);
        const float feedbackGovernorCoefficient =
            feedbackGovernorTarget < feedbackToneGovernor_
                ? feedbackGovernorAttackCoefficient_
                : feedbackGovernorReleaseCoefficient_;
        feedbackToneGovernor_ += (feedbackGovernorTarget
                - feedbackToneGovernor_) * feedbackGovernorCoefficient;
        feedbackToneGovernor_ = clamp(
            flushDenormal(feedbackToneGovernor_), 0.0f, 1.0f);
        const float shaped = sourceShaped + feedbackTone
            * feedbackToneLevel * feedbackToneGovernor_;

        postLowpass_ += (shaped - postLowpass_)
            * clamp(coefficients.postLowpass, 0.00001f, 0.95f);
        postLowpass_ = flushDenormal(postLowpass_);
        const float contoured = lerp(postLowpass_, shaped,
            0.34f + shred * 0.34f);

        // Nonlinear stages can synthesize new low-frequency products. Remove
        // those from the dirty branch and restore only the original low band.
        dirtyLowOne_ += (contoured - dirtyLowOne_) * split;
        dirtyLowTwo_ += (dirtyLowOne_ - dirtyLowTwo_) * split;
        dirtyLowOne_ = flushDenormal(dirtyLowOne_);
        dirtyLowTwo_ = flushDenormal(dirtyLowTwo_);
        const float dirtyUpper = contoured - dirtyLowTwo_;
        const float upperMakeup = 1.0f + shred * 0.18f;
        const float processed = cleanLowTwo_ + dirtyUpper * upperMakeup;

        // A final complementary low replacement prevents phase residue from
        // the nonlinear branch from subtracting the bass fundamental. This is
        // the internal equivalent of a bass pedal's clean low sidechain.
        dryAnchorOne_ += (input - dryAnchorOne_) * split;
        dryAnchorTwo_ += (dryAnchorOne_ - dryAnchorTwo_) * split;
        wetAnchorOne_ += (processed - wetAnchorOne_) * split;
        wetAnchorTwo_ += (wetAnchorOne_ - wetAnchorTwo_) * split;
        dryAnchorOne_ = flushDenormal(dryAnchorOne_);
        dryAnchorTwo_ = flushDenormal(dryAnchorTwo_);
        wetAnchorOne_ = flushDenormal(wetAnchorOne_);
        wetAnchorTwo_ = flushDenormal(wetAnchorTwo_);
        const float anchored = processed
            + (dryAnchorTwo_ - wetAnchorTwo_);

        activity_ += (std::fabs(loopLowpass_) - activity_)
            * activityCoefficient_;
        return flushDenormal(std::clamp(anchored, -4.0f, 4.0f));
    }

    float feedbackActivity() const
    {
        return clamp(activity_, 0.0f, 1.0f);
    }

private:
    static float folded(float value)
    {
        const float shifted = value + 1.0f;
        const float wrapped = shifted
            - 4.0f * std::floor(shifted * 0.25f);
        return wrapped <= 2.0f
            ? wrapped - 1.0f : 3.0f - wrapped;
    }

    struct CircuitState {
        float memory = 0.0f;
        float low = 0.0f;
        float high = 0.0f;
        float envelope = 0.0f;
    };

    float processCircuit(BassShredCircuit circuit, float input,
        float amount, float color, float bias, float shredDrive,
        std::array<CircuitState, kBassShredCircuitCount>& states)
    {
        if (circuit == BassShredCircuit::Shred) {
            const float pressured = std::tanh(input * shredDrive);
            const float foldedValue = folded(
                pressured * (1.0f + amount * 5.0f));
            const float foldMix = amount * amount * 0.68f;
            return lerp(pressured, foldedValue, foldMix);
        }
        const uint32_t index = std::min<uint32_t>(
            static_cast<uint32_t>(circuit), kBassShredCircuitCount - 1u);
        return processAnalogDriveCircuit(
            static_cast<AnalogDriveCircuit>(index - 1u),
            states[index], input, amount, color, bias,
            static_cast<float>(sampleRate_ * 2.0));
    }

    float readDelay(float delaySamples) const
    {
        const float size = static_cast<float>(delay_.size());
        float read = static_cast<float>(writeIndex_) - delaySamples;
        while (read < 0.0f) read += size;
        while (read >= size) read -= size;
        const auto first = static_cast<size_t>(read);
        const auto second = (first + 1u) % delay_.size();
        const float fraction = read - static_cast<float>(first);
        return delay_[first]
            + (delay_[second] - delay_[first]) * fraction;
    }

    double sampleRate_ = 48000.0;
    std::vector<float> delay_;
    size_t writeIndex_ = 0u;
    float delaySamples_ = 96.0f;
    float cleanLowOne_ = 0.0f;
    float cleanLowTwo_ = 0.0f;
    float dirtyLowOne_ = 0.0f;
    float dirtyLowTwo_ = 0.0f;
    float dryAnchorOne_ = 0.0f;
    float dryAnchorTwo_ = 0.0f;
    float wetAnchorOne_ = 0.0f;
    float wetAnchorTwo_ = 0.0f;
    float loopDcInput_ = 0.0f;
    float loopDcOutput_ = 0.0f;
    float loopLowpass_ = 0.0f;
    float loopEnvelope_ = 0.0f;
    float sourceEnvelope_ = 0.0f;
    float feedbackToneEnvelope_ = 0.0f;
    float feedbackToneGovernor_ = 1.0f;
    float postLowpass_ = 0.0f;
    float previousExcitation_ = 0.0f;
    float previousSourceExcitation_ = 0.0f;
    float activity_ = 0.0f;
    float delaySmoothingCoefficient_ = 0.001f;
    float safetyCoefficient_ = 0.001f;
    float activityCoefficient_ = 0.001f;
    float sourceAttackCoefficient_ = 0.001f;
    float sourceReleaseCoefficient_ = 0.001f;
    float feedbackGovernorAttackCoefficient_ = 0.001f;
    float feedbackGovernorReleaseCoefficient_ = 0.001f;
    std::array<CircuitState, kBassShredCircuitCount> circuitStates_ {};
    std::array<CircuitState, kBassShredCircuitCount> sourceCircuitStates_ {};
    BassShredCircuit activeCircuit_ = BassShredCircuit::Shred;
    BassShredCircuit previousCircuit_ = BassShredCircuit::Shred;
    float circuitFade_ = 1.0f;
    float circuitFadeCoefficient_ = 0.001f;
    float dcPole_ = 0.997f;
};

class BassShredStereo {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        for (auto& core : cores_) core.prepare(sampleRate_);
        parameterSmoothingCoefficient_ = 1.0f - std::exp(
            -1.0f / static_cast<float>(sampleRate_ * 0.015));
        feedbackInterruptionAttackCoefficient_ = 1.0f - std::exp(
            -1.0f / static_cast<float>(sampleRate_ * 0.004));
        feedbackInterruptionReleaseCoefficient_ = 1.0f - std::exp(
            -1.0f / static_cast<float>(sampleRate_ * 0.040));
        pitchSmoothingCoefficient_ = 1.0f - std::exp(
            -1.0f / static_cast<float>(sampleRate_ * 0.035));
        reset();
    }

    void reset()
    {
        for (auto& core : cores_) core.reset(params_.circuit);
        shredSmoothed_ = params_.shred;
        feedbackSmoothed_ = params_.feedback;
        feedbackToneLevelSmoothed_ = params_.feedbackToneLevel;
        feedbackContinuityGain_ = 1.0f;
        feedbackInterruptSamplesRemaining_ = 0u;
        colorSmoothed_ = params_.color;
        mixSmoothed_ = params_.mix;
        pitchSmoothed_ = 55.0f;
    }

    void setParams(BassShredParams params)
    {
        params.shred = clamp(std::isfinite(params.shred)
            ? params.shred : 0.0f, 0.0f, 1.0f);
        params.feedback = clamp(std::isfinite(params.feedback)
            ? params.feedback : 0.0f, 0.0f, 1.0f);
        params.feedbackToneLevel = clamp(
            std::isfinite(params.feedbackToneLevel)
                ? params.feedbackToneLevel : 1.0f,
            0.0f, 1.0f);
        params.color = clamp(std::isfinite(params.color)
            ? params.color : 0.55f, 0.0f, 1.0f);
        params.mix = clamp(std::isfinite(params.mix)
            ? params.mix : 0.0f, 0.0f, 1.0f);
        params.circuit = static_cast<BassShredCircuit>(
            std::min<uint32_t>(static_cast<uint32_t>(params.circuit),
                kBassShredCircuitCount - 1u));
        const bool feedbackInterrupted =
            params.feedback + 1.0e-5f < params_.feedback
            || params.feedbackToneLevel + 1.0e-5f
                < params_.feedbackToneLevel;
        params_ = params;
        if (feedbackInterrupted) interruptFeedback();
    }

    BassShredParams params() const { return params_; }

    void interruptFeedback()
    {
        feedbackInterruptSamplesRemaining_ = std::max<uint32_t>(
            feedbackInterruptSamplesRemaining_,
            static_cast<uint32_t>(std::ceil(sampleRate_ * 0.010)));
    }

    void processStereo(float& left, float& right, float pitchHz)
    {
        pitchHz = clamp(std::isfinite(pitchHz) ? pitchHz : 55.0f,
            8.0f, static_cast<float>(sampleRate_ * 0.20));
        shredSmoothed_ += (params_.shred - shredSmoothed_)
            * parameterSmoothingCoefficient_;
        feedbackSmoothed_ += (params_.feedback - feedbackSmoothed_)
            * parameterSmoothingCoefficient_;
        feedbackToneLevelSmoothed_ += (params_.feedbackToneLevel
                - feedbackToneLevelSmoothed_)
            * parameterSmoothingCoefficient_;
        const float feedbackContinuityTarget =
            feedbackInterruptSamplesRemaining_ > 0u ? 0.0f : 1.0f;
        if (feedbackInterruptSamplesRemaining_ > 0u) {
            --feedbackInterruptSamplesRemaining_;
        }
        const float feedbackContinuityCoefficient =
            feedbackContinuityTarget < feedbackContinuityGain_
                ? feedbackInterruptionAttackCoefficient_
                : feedbackInterruptionReleaseCoefficient_;
        feedbackContinuityGain_ += (feedbackContinuityTarget
                - feedbackContinuityGain_)
            * feedbackContinuityCoefficient;
        colorSmoothed_ += (params_.color - colorSmoothed_)
            * parameterSmoothingCoefficient_;
        mixSmoothed_ += (params_.mix - mixSmoothed_)
            * parameterSmoothingCoefficient_;
        pitchSmoothed_ += (pitchHz - pitchSmoothed_)
            * pitchSmoothingCoefficient_;

        const float dryLeft = std::isfinite(left) ? left : 0.0f;
        const float dryRight = std::isfinite(right) ? right : 0.0f;
        const float splitHz = clamp(pitchSmoothed_
                * lerp(5.2f, 6.8f, colorSmoothed_),
            180.0f, 500.0f);
        const float feedbackHz = clamp(pitchSmoothed_
                * lerp(5.0f, 18.0f, colorSmoothed_),
            180.0f, 2400.0f);
        const float postHz = lerp(1400.0f, 7600.0f,
            colorSmoothed_);
        const float loopHz = lerp(1800.0f, 9400.0f,
            colorSmoothed_);
        BassShredCoefficients coefficients;
        coefficients.split = onePole(splitHz);
        coefficients.loopLowpass = onePole(loopHz);
        coefficients.postLowpass = onePole(postHz);

        constexpr std::array<float, 2u> kLaneTune {{ 0.968f, 1.033f }};
        std::array<float, 2u> wet {};
        const std::array<float, 2u> dry {{ dryLeft, dryRight }};
        for (uint32_t lane = 0u; lane < cores_.size(); ++lane) {
            coefficients.delaySamples = static_cast<float>(sampleRate_)
                / (feedbackHz * kLaneTune[lane]);
            wet[lane] = cores_[lane].processSample(dry[lane],
                shredSmoothed_, feedbackSmoothed_,
                feedbackToneLevelSmoothed_ * feedbackContinuityGain_,
                colorSmoothed_,
                params_.circuit, coefficients);
        }
        left = lerp(dryLeft, wet[0u], mixSmoothed_);
        right = lerp(dryRight, wet[1u], mixSmoothed_);
    }

    float feedbackActivity() const
    {
        return std::max(cores_[0u].feedbackActivity(),
            cores_[1u].feedbackActivity());
    }

private:
    float onePole(float frequency) const
    {
        const float sr = static_cast<float>(sampleRate_);
        return 1.0f - std::exp(-2.0f * kPi
            * std::min(frequency, sr * 0.45f) / sr);
    }

    double sampleRate_ = 48000.0;
    BassShredParams params_ {};
    std::array<BassShredCore, 2u> cores_ {};
    float shredSmoothed_ = 0.0f;
    float feedbackSmoothed_ = 0.0f;
    float feedbackToneLevelSmoothed_ = 1.0f;
    float feedbackContinuityGain_ = 1.0f;
    float colorSmoothed_ = 0.55f;
    float mixSmoothed_ = 0.0f;
    float pitchSmoothed_ = 55.0f;
    float parameterSmoothingCoefficient_ = 0.001f;
    float feedbackInterruptionAttackCoefficient_ = 0.001f;
    float feedbackInterruptionReleaseCoefficient_ = 0.001f;
    float pitchSmoothingCoefficient_ = 0.001f;
    uint32_t feedbackInterruptSamplesRemaining_ = 0u;
};

} // namespace s3g
