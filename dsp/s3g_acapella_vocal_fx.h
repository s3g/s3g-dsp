#pragma once

#include "s3g_acapella_source_synth.h"
#include "s3g_drum_echo.h"
#include "s3g_math.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

struct AcapellaVocalFxParams {
    float octaveDown = 0.0f;
    float octaveUp = 0.0f;
    float fuzzDriveDb = 0.0f;
    float fuzzMix = 0.0f;
    float fuzzToneHz = 6500.0f;
    float compression = 0.22f;
    float parallelCrush = 0.0f;
    float deEss = 0.10f;
    DrumEchoHeadMode echoHeads = DrumEchoHeadMode::Heads123;
    DrumEchoClock echoClock = DrumEchoClock::Eighth;
    float echoTimeMs = 180.0f;
    float echoFeedback = 0.34f;
    float echoWear = 0.18f;
    float echoFlutter = 0.10f;
    float echoTone = -0.12f;
    float echoSpread = 0.58f;
    float echoMix = 0.0f;
    float width = 0.0f;
    float intelligibility = 0.78f;
};

struct AcapellaStereoFrame {
    float left = 0.0f;
    float right = 0.0f;
};

inline AcapellaVocalFxParams sanitizeAcapellaVocalFxParams(
    AcapellaVocalFxParams params)
{
    params.octaveDown = clamp(acapellaFiniteOr(params.octaveDown, 0.0f),
        0.0f, 1.0f);
    params.octaveUp = clamp(acapellaFiniteOr(params.octaveUp, 0.0f),
        0.0f, 1.0f);
    params.fuzzDriveDb = clamp(acapellaFiniteOr(params.fuzzDriveDb, 0.0f),
        0.0f, 30.0f);
    params.fuzzMix = clamp(acapellaFiniteOr(params.fuzzMix, 0.0f),
        0.0f, 1.0f);
    params.fuzzToneHz = clamp(acapellaFiniteOr(params.fuzzToneHz, 6500.0f),
        700.0f, 16000.0f);
    params.compression = clamp(acapellaFiniteOr(params.compression, 0.22f),
        0.0f, 1.0f);
    params.parallelCrush = clamp(acapellaFiniteOr(params.parallelCrush, 0.0f),
        0.0f, 1.0f);
    params.deEss = clamp(acapellaFiniteOr(params.deEss, 0.10f), 0.0f, 1.0f);
    params.echoHeads = static_cast<DrumEchoHeadMode>(std::min<uint32_t>(
        static_cast<uint32_t>(params.echoHeads), kDrumEchoHeadModeCount - 1u));
    params.echoClock = static_cast<DrumEchoClock>(std::min<uint32_t>(
        static_cast<uint32_t>(params.echoClock), kDrumEchoClockCount - 1u));
    params.echoTimeMs = clamp(acapellaFiniteOr(params.echoTimeMs, 180.0f),
        20.0f, 1800.0f);
    params.echoFeedback = clamp(acapellaFiniteOr(params.echoFeedback, 0.34f),
        0.0f, 0.92f);
    params.echoWear = clamp(acapellaFiniteOr(params.echoWear, 0.18f),
        0.0f, 1.0f);
    params.echoFlutter = clamp(acapellaFiniteOr(params.echoFlutter, 0.10f),
        0.0f, 1.0f);
    params.echoTone = clamp(acapellaFiniteOr(params.echoTone, -0.12f),
        -1.0f, 1.0f);
    params.echoSpread = clamp(acapellaFiniteOr(params.echoSpread, 0.58f),
        0.0f, 1.0f);
    params.echoMix = clamp(acapellaFiniteOr(params.echoMix, 0.0f),
        0.0f, 1.0f);
    params.width = clamp(acapellaFiniteOr(params.width, 0.0f), 0.0f, 1.0f);
    params.intelligibility = clamp(acapellaFiniteOr(
        params.intelligibility, 0.78f), 0.0f, 1.0f);
    return params;
}

inline AcapellaVocalFxParams acapellaVocalFxPreset(
    AcapellaSourcePreset preset)
{
    AcapellaVocalFxParams params;
    switch (preset) {
    case AcapellaSourcePreset::RhythmicRap:
        params.compression = 0.40f;
        params.parallelCrush = 0.10f;
        params.deEss = 0.24f;
        params.fuzzDriveDb = 6.0f;
        params.fuzzMix = 0.06f;
        break;
    case AcapellaSourcePreset::AirySung:
        params.compression = 0.18f;
        params.deEss = 0.28f;
        params.width = 0.10f;
        break;
    case AcapellaSourcePreset::PressedLead:
        params.compression = 0.50f;
        params.parallelCrush = 0.16f;
        params.deEss = 0.20f;
        params.fuzzDriveDb = 9.0f;
        params.fuzzMix = 0.13f;
        params.fuzzToneHz = 7200.0f;
        break;
    case AcapellaSourcePreset::HarshScream:
        params.octaveDown = 0.07f;
        params.octaveUp = 0.14f;
        params.fuzzDriveDb = 15.0f;
        params.fuzzMix = 0.34f;
        params.fuzzToneHz = 6800.0f;
        params.compression = 0.70f;
        params.parallelCrush = 0.34f;
        params.deEss = 0.42f;
        params.echoHeads = DrumEchoHeadMode::Head12;
        params.echoClock = DrumEchoClock::Sixteenth;
        params.echoFeedback = 0.25f;
        params.echoMix = 0.08f;
        params.width = 0.20f;
        break;
    case AcapellaSourcePreset::DeathGrowl:
        params.octaveDown = 0.34f;
        params.octaveUp = 0.04f;
        params.fuzzDriveDb = 18.0f;
        params.fuzzMix = 0.43f;
        params.fuzzToneHz = 3900.0f;
        params.compression = 0.78f;
        params.parallelCrush = 0.46f;
        params.deEss = 0.22f;
        params.echoHeads = DrumEchoHeadMode::Head23;
        params.echoClock = DrumEchoClock::EighthTriplet;
        params.echoFeedback = 0.39f;
        params.echoTone = -0.42f;
        params.echoMix = 0.17f;
        params.width = 0.16f;
        break;
    case AcapellaSourcePreset::NeutralSung:
    default:
        break;
    }
    return sanitizeAcapellaVocalFxParams(params);
}

// A bounded, allocation-free post chain tailored to the synthesized vocal.
// The octave generators are waveform operations, not pitch-shifted samples:
// full-wave rectification supplies octave-up energy, while a hysteretic
// period divider supplies octave-down energy.
class AcapellaVocalEffects {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = std::isfinite(sampleRate)
            ? clamp(static_cast<float>(sampleRate), 8000.0f, 192000.0f)
            : 48000.0f;
        params_ = sanitizeAcapellaVocalFxParams(params_);
        smoothingCoefficient_ = coefficient(18.0f);
        pitchLowCoefficient_ = lowpassCoefficient(720.0f);
        subLowCoefficient_ = lowpassCoefficient(520.0f);
        rectifiedDcCoefficient_ = lowpassCoefficient(24.0f);
        deEssCoefficient_ = lowpassCoefficient(4800.0f);
        crushLowCoefficient_ = lowpassCoefficient(260.0f);
        crushHighCoefficient_ = lowpassCoefficient(3200.0f);
        crushAttackCoefficient_ = coefficient(0.8f);
        crushReleaseCoefficient_ = coefficient(70.0f);
        crushGainAttackCoefficient_ = coefficient(1.0f);
        crushGainReleaseCoefficient_ = coefficient(85.0f);
        subAttackCoefficient_ = coefficient(2.0f);
        subReleaseCoefficient_ = coefficient(45.0f);
        fastAttackCoefficient_ = coefficient(0.7f);
        fastReleaseCoefficient_ = coefficient(48.0f);
        fastGainAttackCoefficient_ = coefficient(1.0f);
        slowAttackCoefficient_ = coefficient(11.0f);
        slowReleaseCoefficient_ = coefficient(180.0f);
        slowGainAttackCoefficient_ = coefficient(1.0f);
        deEssAttackCoefficient_ = coefficient(0.6f);
        deEssReleaseCoefficient_ = coefficient(55.0f);
        deEssGainAttackCoefficient_ = coefficient(0.8f);
        deEssGainReleaseCoefficient_ = coefficient(65.0f);
        limiterReleaseCoefficient_ = coefficient(70.0f);
        activityReleaseCoefficient_ = coefficient(180.0f);
        tapeEcho_.prepare(sampleRate_, 12.0);
        reset();
    }

    void reset()
    {
        smoothed_ = params_;
        previousInput_ = 0.0f;
        pitchLow1_ = 0.0f;
        pitchLow2_ = 0.0f;
        pitchDividerArmed_ = false;
        subPolarity_ = 1.0f;
        subEnvelope_ = 0.0f;
        subLow_ = 0.0f;
        rectifiedDc_ = 0.0f;
        fuzzTone_ = 0.0f;
        fuzzDcInput_ = 0.0f;
        fuzzDcOutput_ = 0.0f;
        fastEnvelope_ = 0.0f;
        fastGain_ = 1.0f;
        slowEnvelope_ = 0.0f;
        slowGain_ = 1.0f;
        crushLow_ = 0.0f;
        crushHighLow_ = 0.0f;
        crushEnvelopes_.fill(0.0f);
        crushGains_.fill(1.0f);
        deEssLow_ = 0.0f;
        deEssEnvelope_ = 0.0f;
        deEssGain_ = 1.0f;
        updateTapeEchoParams();
        tapeEcho_.reset();
        allpassInput_ = 0.0f;
        allpassOutput_ = 0.0f;
        limiterGain_ = 1.0f;
        activityEnvelope_ = 0.0f;
        coefficientCounter_ = 0u;
        updateToneCoefficient();
    }

    void setParams(AcapellaVocalFxParams params)
    {
        params_ = sanitizeAcapellaVocalFxParams(params);
    }

    void setTempo(double beatsPerMinute, bool valid = true)
    {
        tapeEcho_.setTempo(beatsPerMinute, valid);
    }

    uint32_t tailSamples() const { return tapeEcho_.tailSamples(); }

    const AcapellaVocalFxParams& params() const { return params_; }

    bool active() const
    {
        return smoothed_.echoMix > 1.0e-4f
            && activityEnvelope_ > 1.0e-4f;
    }

    AcapellaStereoFrame processFrame(float input)
    {
        input = std::isfinite(input) ? clamp(input, -2.0f, 2.0f) : 0.0f;
        const float cleanInput = input;
        smoothParams();
        if ((coefficientCounter_++ & 15u) == 0u) {
            updateToneCoefficient();
            updateTapeEchoParams();
        }

        pitchLow1_ += pitchLowCoefficient_ * (input - pitchLow1_);
        pitchLow2_ += pitchLowCoefficient_ * (pitchLow1_ - pitchLow2_);
        const float hysteresis = 0.0025f;
        if (pitchLow2_ <= -hysteresis) pitchDividerArmed_ = true;
        if (pitchDividerArmed_ && pitchLow2_ >= hysteresis) {
            subPolarity_ = -subPolarity_;
            pitchDividerArmed_ = false;
        }
        const float absoluteInput = std::abs(input);
        const float subEnvelopeCoefficient = absoluteInput > subEnvelope_
            ? subAttackCoefficient_ : subReleaseCoefficient_;
        subEnvelope_ += (absoluteInput - subEnvelope_)
            * subEnvelopeCoefficient;
        subLow_ += subLowCoefficient_
            * (subPolarity_ * subEnvelope_ - subLow_);

        const float rectified = std::abs(input);
        rectifiedDc_ += rectifiedDcCoefficient_
            * (rectified - rectifiedDc_);
        const float octaveUp = (rectified - rectifiedDc_) * 1.55f;
        const float octaveNormalization = 1.0f
            + 0.28f * (smoothed_.octaveDown + smoothed_.octaveUp);
        const float octaveSignal = (input
            + subLow_ * smoothed_.octaveDown * 0.72f
            + octaveUp * smoothed_.octaveUp * 0.58f)
            / octaveNormalization;

        const float drive = std::exp2(smoothed_.fuzzDriveDb / 6.020599913f);
        const float midpoint = 0.5f * (previousInput_ + octaveSignal);
        const float fuzz = 0.5f * (fuzzShape(midpoint, drive)
            + fuzzShape(octaveSignal, drive));
        previousInput_ = octaveSignal;
        fuzzTone_ += fuzzToneCoefficient_ * (fuzz - fuzzTone_);
        const float fuzzDc = fuzzTone_ - fuzzDcInput_
            + 0.997f * fuzzDcOutput_;
        fuzzDcInput_ = fuzzTone_;
        fuzzDcOutput_ = flushDenormal(fuzzDc);
        float signal = lerp(octaveSignal, fuzzDcOutput_, smoothed_.fuzzMix);

        signal = compressorStage(signal, fastEnvelope_, fastGain_,
            -18.0f, 8.0f, fastAttackCoefficient_, fastReleaseCoefficient_,
            fastGainAttackCoefficient_, fastReleaseCoefficient_,
            smoothed_.compression);
        signal = compressorStage(signal, slowEnvelope_, slowGain_,
            -14.0f, 3.2f, slowAttackCoefficient_, slowReleaseCoefficient_,
            slowGainAttackCoefficient_, slowReleaseCoefficient_,
            smoothed_.compression * 0.78f);
        signal *= std::exp2(smoothed_.compression * 4.5f / 6.020599913f);

        crushLow_ += crushLowCoefficient_ * (signal - crushLow_);
        crushHighLow_ += crushHighCoefficient_ * (signal - crushHighLow_);
        const float lowBand = crushLow_;
        const float midBand = crushHighLow_ - crushLow_;
        const float highBand = signal - crushHighLow_;
        const float crushed = crushBand(lowBand, 0u, 0.085f, 4.0f, 0.48f)
            + crushBand(midBand, 1u, 0.055f, 5.5f, 0.38f)
            + crushBand(highBand, 2u, 0.030f, 7.0f, 0.24f);
        const float parallel = signal * 0.60f + crushed;
        signal = lerp(signal, parallel, smoothed_.parallelCrush);

        deEssLow_ += deEssCoefficient_ * (signal - deEssLow_);
        const float deEssHigh = signal - deEssLow_;
        const float highLevel = std::abs(deEssHigh);
        deEssEnvelope_ += (highLevel - deEssEnvelope_)
            * (highLevel > deEssEnvelope_ ? deEssAttackCoefficient_
                                          : deEssReleaseCoefficient_);
        const float deEssThreshold = 0.035f;
        const float desiredDeEss = deEssEnvelope_ > deEssThreshold
            ? clamp(deEssThreshold / deEssEnvelope_, 0.16f, 1.0f) : 1.0f;
        const float targetDeEss = lerp(1.0f, desiredDeEss, smoothed_.deEss);
        deEssGain_ += (targetDeEss - deEssGain_)
            * (targetDeEss < deEssGain_ ? deEssGainAttackCoefficient_
                                        : deEssGainReleaseCoefficient_);
        signal = deEssLow_ + deEssHigh * deEssGain_;

        // Keep a phase-aligned clean articulation rail under nonlinear
        // processing. The amount follows actual effect intensity, so the
        // control is essentially transparent on an already-clean voice.
        const float destructiveAmount = std::max({
            smoothed_.fuzzMix,
            smoothed_.parallelCrush * 0.82f,
            (smoothed_.octaveDown + smoothed_.octaveUp) * 0.42f,
        });
        const float cleanPreserve = smoothed_.intelligibility
            * destructiveAmount * 0.36f;
        signal = lerp(signal, cleanInput, cleanPreserve);

        constexpr float allpassCoefficient = 0.71f;
        const float decorrelated = -allpassCoefficient * signal
            + allpassInput_ + allpassCoefficient * allpassOutput_;
        allpassInput_ = signal;
        allpassOutput_ = flushDenormal(decorrelated);
        const float side = (decorrelated - signal)
            * smoothed_.width * 0.34f;
        float left = signal + side + stereoInputSide_;
        float right = signal - side - stereoInputSide_;
        tapeEcho_.processFrame(left, right);

        const float peak = std::max(std::abs(left), std::abs(right));
        const float desiredLimiter = peak > 0.94f ? 0.94f / peak : 1.0f;
        if (desiredLimiter < limiterGain_) {
            limiterGain_ = desiredLimiter;
        } else {
            limiterGain_ += (1.0f - limiterGain_)
                * limiterReleaseCoefficient_;
        }
        left = clamp(left * limiterGain_, -0.98f, 0.98f);
        right = clamp(right * limiterGain_, -0.98f, 0.98f);
        if (!std::isfinite(left) || !std::isfinite(right)) {
            reset();
            return {};
        }
        const float outputActivity = std::max(std::abs(left), std::abs(right));
        if (outputActivity > activityEnvelope_) {
            activityEnvelope_ = outputActivity;
        } else {
            activityEnvelope_ += (outputActivity - activityEnvelope_)
                * activityReleaseCoefficient_;
        }
        return { left, right };
    }

    AcapellaStereoFrame processFrameStereo(float leftInput, float rightInput)
    {
        leftInput = std::isfinite(leftInput) ? leftInput : 0.0f;
        rightInput = std::isfinite(rightInput) ? rightInput : 0.0f;
        const float mid = 0.5f * (leftInput + rightInput);
        const float side = 0.5f * (leftInput - rightInput) * 0.82f;
        stereoInputSide_ = side;
        auto output = processFrame(mid);
        stereoInputSide_ = 0.0f;
        const float peak = std::max(std::abs(output.left),
            std::abs(output.right));
        if (peak > 0.98f) {
            const float gain = 0.98f / peak;
            output.left *= gain;
            output.right *= gain;
        }
        return output;
    }

private:

    float coefficient(float milliseconds) const
    {
        const float samples = std::max(1.0f,
            milliseconds * 0.001f * sampleRate_);
        return 1.0f - std::exp(-1.0f / samples);
    }

    float lowpassCoefficient(float frequencyHz) const
    {
        return 1.0f - std::exp(-2.0f * kPi
            * clamp(frequencyHz, 10.0f, sampleRate_ * 0.45f) / sampleRate_);
    }

    static float fuzzShape(float input, float drive)
    {
        const float bias = 0.055f;
        return (std::tanh((input + bias) * drive)
            - std::tanh(bias * drive)) * 0.72f;
    }

    float compressorStage(float input, float& envelope, float& gain,
        float thresholdDb, float ratio, float envelopeAttackCoefficient,
        float envelopeReleaseCoefficient, float gainAttackCoefficient,
        float gainReleaseCoefficient, float amount)
    {
        const float level = std::abs(input);
        envelope += (level - envelope)
            * (level > envelope ? envelopeAttackCoefficient
                                : envelopeReleaseCoefficient);
        const float levelDb = 20.0f * std::log10(std::max(envelope, 1.0e-8f));
        const float compressedDb = levelDb > thresholdDb
            ? thresholdDb + (levelDb - thresholdDb) / ratio : levelDb;
        const float desiredGain = std::exp2(
            (compressedDb - levelDb) * amount / 6.020599913f);
        gain += (desiredGain - gain)
            * (desiredGain < gain ? gainAttackCoefficient
                                  : gainReleaseCoefficient);
        return input * gain;
    }

    float crushBand(float input, uint32_t index, float threshold,
        float drive, float level)
    {
        const float magnitude = std::abs(input);
        crushEnvelopes_[index] += (magnitude - crushEnvelopes_[index])
            * (magnitude > crushEnvelopes_[index] ? crushAttackCoefficient_
                                                  : crushReleaseCoefficient_);
        const float desired = crushEnvelopes_[index] > threshold
            ? (threshold + (crushEnvelopes_[index] - threshold) / 9.0f)
                / crushEnvelopes_[index]
            : 1.0f;
        crushGains_[index] += (desired - crushGains_[index])
            * (desired < crushGains_[index] ? crushGainAttackCoefficient_
                                            : crushGainReleaseCoefficient_);
        return std::tanh(input * crushGains_[index] * drive * 1.6f) * level;
    }

    void smoothParams()
    {
        smoothed_.octaveDown += (params_.octaveDown - smoothed_.octaveDown)
            * smoothingCoefficient_;
        smoothed_.octaveUp += (params_.octaveUp - smoothed_.octaveUp)
            * smoothingCoefficient_;
        smoothed_.fuzzDriveDb += (params_.fuzzDriveDb - smoothed_.fuzzDriveDb)
            * smoothingCoefficient_;
        smoothed_.fuzzMix += (params_.fuzzMix - smoothed_.fuzzMix)
            * smoothingCoefficient_;
        smoothed_.fuzzToneHz += (params_.fuzzToneHz - smoothed_.fuzzToneHz)
            * smoothingCoefficient_;
        smoothed_.compression += (params_.compression - smoothed_.compression)
            * smoothingCoefficient_;
        smoothed_.parallelCrush += (params_.parallelCrush
            - smoothed_.parallelCrush) * smoothingCoefficient_;
        smoothed_.deEss += (params_.deEss - smoothed_.deEss)
            * smoothingCoefficient_;
        smoothed_.echoHeads = params_.echoHeads;
        smoothed_.echoClock = params_.echoClock;
        smoothed_.echoTimeMs += (params_.echoTimeMs - smoothed_.echoTimeMs)
            * smoothingCoefficient_;
        smoothed_.echoFeedback += (params_.echoFeedback
            - smoothed_.echoFeedback) * smoothingCoefficient_;
        smoothed_.echoWear += (params_.echoWear - smoothed_.echoWear)
            * smoothingCoefficient_;
        smoothed_.echoFlutter += (params_.echoFlutter
            - smoothed_.echoFlutter) * smoothingCoefficient_;
        smoothed_.echoTone += (params_.echoTone - smoothed_.echoTone)
            * smoothingCoefficient_;
        smoothed_.echoSpread += (params_.echoSpread - smoothed_.echoSpread)
            * smoothingCoefficient_;
        smoothed_.echoMix += (params_.echoMix - smoothed_.echoMix)
            * smoothingCoefficient_;
        smoothed_.width += (params_.width - smoothed_.width)
            * smoothingCoefficient_;
        smoothed_.intelligibility += (params_.intelligibility
            - smoothed_.intelligibility) * smoothingCoefficient_;
    }

    void updateToneCoefficient()
    {
        fuzzToneCoefficient_ = lowpassCoefficient(smoothed_.fuzzToneHz);
    }

    void updateTapeEchoParams()
    {
        DrumEchoParams echo;
        echo.headMode = smoothed_.echoHeads;
        echo.clock = smoothed_.echoClock;
        echo.timeMs = smoothed_.echoTimeMs;
        echo.feedback = smoothed_.echoFeedback;
        echo.wear = smoothed_.echoWear;
        echo.flutter = smoothed_.echoFlutter;
        echo.transient = 0.0f;
        echo.sensitivity = 0.0f;
        echo.duck = 0.0f;
        echo.tone = smoothed_.echoTone;
        echo.spread = smoothed_.echoSpread;
        echo.mix = smoothed_.echoMix;
        echo.outputGainDb = 0.0f;
        echo.bypass = false;
        tapeEcho_.setParams(echo);
    }

    float sampleRate_ = 48000.0f;
    AcapellaVocalFxParams params_ {};
    AcapellaVocalFxParams smoothed_ {};
    float smoothingCoefficient_ = 0.001f;
    float pitchLowCoefficient_ = 0.08f;
    float subLowCoefficient_ = 0.06f;
    float rectifiedDcCoefficient_ = 0.003f;
    float deEssCoefficient_ = 0.4f;
    float crushLowCoefficient_ = 0.03f;
    float crushHighCoefficient_ = 0.3f;
    float fuzzToneCoefficient_ = 0.5f;
    float subAttackCoefficient_ = 0.01f;
    float subReleaseCoefficient_ = 0.001f;
    float fastAttackCoefficient_ = 0.02f;
    float fastReleaseCoefficient_ = 0.001f;
    float fastGainAttackCoefficient_ = 0.02f;
    float slowAttackCoefficient_ = 0.002f;
    float slowReleaseCoefficient_ = 0.0001f;
    float slowGainAttackCoefficient_ = 0.02f;
    float crushAttackCoefficient_ = 0.02f;
    float crushReleaseCoefficient_ = 0.0003f;
    float crushGainAttackCoefficient_ = 0.02f;
    float crushGainReleaseCoefficient_ = 0.0002f;
    float deEssAttackCoefficient_ = 0.03f;
    float deEssReleaseCoefficient_ = 0.0004f;
    float deEssGainAttackCoefficient_ = 0.02f;
    float deEssGainReleaseCoefficient_ = 0.0003f;
    float limiterReleaseCoefficient_ = 0.0003f;
    float activityReleaseCoefficient_ = 0.00004f;
    float previousInput_ = 0.0f;
    float pitchLow1_ = 0.0f;
    float pitchLow2_ = 0.0f;
    bool pitchDividerArmed_ = false;
    float subPolarity_ = 1.0f;
    float subEnvelope_ = 0.0f;
    float subLow_ = 0.0f;
    float rectifiedDc_ = 0.0f;
    float fuzzTone_ = 0.0f;
    float fuzzDcInput_ = 0.0f;
    float fuzzDcOutput_ = 0.0f;
    float fastEnvelope_ = 0.0f;
    float fastGain_ = 1.0f;
    float slowEnvelope_ = 0.0f;
    float slowGain_ = 1.0f;
    float crushLow_ = 0.0f;
    float crushHighLow_ = 0.0f;
    std::array<float, 3u> crushEnvelopes_ {};
    std::array<float, 3u> crushGains_ { 1.0f, 1.0f, 1.0f };
    float deEssLow_ = 0.0f;
    float deEssEnvelope_ = 0.0f;
    float deEssGain_ = 1.0f;
    DrumEcho tapeEcho_ {};
    float stereoInputSide_ = 0.0f;
    float allpassInput_ = 0.0f;
    float allpassOutput_ = 0.0f;
    float limiterGain_ = 1.0f;
    float activityEnvelope_ = 0.0f;
    uint32_t coefficientCounter_ = 0u;
};

} // namespace s3g
