#pragma once

#include "s3g_math.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

enum class DrumOverloadCircuit : uint32_t {
    Console = 0u,
    Valve,
    Clip,
    Rupture,
    Tape,
    Transformer,
    Diode,
    Speaker,
};

constexpr uint32_t kDrumOverloadCircuitCount = 8u;

inline const char* drumOverloadCircuitName(DrumOverloadCircuit circuit)
{
    switch (circuit) {
    case DrumOverloadCircuit::Console: return "CONSOLE";
    case DrumOverloadCircuit::Valve: return "VALVE";
    case DrumOverloadCircuit::Clip: return "CLIP";
    case DrumOverloadCircuit::Rupture: return "RUPTURE";
    case DrumOverloadCircuit::Tape: return "TAPE";
    case DrumOverloadCircuit::Transformer: return "TRANSFORMER";
    case DrumOverloadCircuit::Diode: return "DIODE";
    case DrumOverloadCircuit::Speaker: return "SPEAKER";
    }
    return "CONSOLE";
}

// Stereo-only overload processor for drums. The POD parameter block and
// frame-oriented API deliberately have no CLAP dependency so this processor
// can be embedded directly by s3g-tracker later.
struct DrumOverloadParams {
    DrumOverloadCircuit circuit = DrumOverloadCircuit::Console;
    float inputGainDb = 0.0f;
    float overload = 0.62f;
    float density = 0.50f;
    float punch = 0.24f;
    float bias = 0.12f;
    float breakup = 0.16f;
    float weight = 0.72f;
    float tone = 0.0f;
    float stereoLink = 0.85f;
    float mix = 0.82f;
    float outputGainDb = -6.0f;
    bool bypass = false;
};

class DrumOverload {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = std::isfinite(sampleRate)
            ? std::clamp(sampleRate, 8000.0, 768000.0)
            : 48000.0;
        const float sr = static_cast<float>(sampleRate_);
        parameterCoefficient_ = onePoleCoefficient(5.0f, sr);
        fastAttackCoefficient_ = onePoleCoefficient(0.10f, sr);
        fastReleaseCoefficient_ = onePoleCoefficient(18.0f, sr);
        slowAttackCoefficient_ = onePoleCoefficient(8.0f, sr);
        slowReleaseCoefficient_ = onePoleCoefficient(215.0f, sr);
        // The 210 Hz body handoff covers the completed drum family's kick,
        // snare body, floor tom, and low/mid/high tom fundamentals while the
        // modal/rim bands still enter the overload core.
        weightCoefficient_ = frequencyCoefficient(210.0f, sr);
        toneCoefficient_ = frequencyCoefficient(1450.0f, sr);
        dcCoefficient_ = std::exp(-2.0f * kPi * 9.0f / sr);

        const float oversampledRate = sr * 2.0f;
        const float antiAliasCutoff = std::min(18000.0f, sr * 0.42f);
        antiAliasCoefficient_ = frequencyCoefficient(
            antiAliasCutoff, oversampledRate);
        tapeMemoryCoefficient_ = frequencyCoefficient(
            900.0f, oversampledRate);
        tapeHeadCoefficient_ = frequencyCoefficient(
            std::min(13500.0f, sr * 0.38f), oversampledRate);
        transformerCoefficient_ = frequencyCoefficient(
            280.0f, oversampledRate);
        speakerCoefficient_ = frequencyCoefficient(
            std::min(5200.0f, sr * 0.28f), oversampledRate);
        sagAttackCoefficient_ = onePoleCoefficient(0.30f, oversampledRate);
        sagReleaseCoefficient_ = onePoleCoefficient(58.0f, oversampledRate);
        reset();
    }

    void reset()
    {
        channels_.fill({});
        smoothed_ = target_;
        bypassWasActive_ = target_.bypass;
        gainReductionDb_ = 0.0f;
        overloadActivity_ = 0.0f;
    }

    void setParams(DrumOverloadParams params)
    {
        target_ = sanitize(params);
    }

    DrumOverloadParams params() const { return target_; }

    // The IIR anti-alias stages are zero-latency from the host's perspective.
    static constexpr uint32_t latencySamples() { return 0u; }

    float gainReductionDb() const { return gainReductionDb_; }
    float overloadActivity() const { return overloadActivity_; }

    void processFrame(float& left, float& right)
    {
        std::array<float, 2u> input {
            sanitizeSample(left), sanitizeSample(right)
        };

        if (target_.bypass) {
            if (!bypassWasActive_) {
                channels_.fill({});
                bypassWasActive_ = true;
            }
            left = input[0u];
            right = input[1u];
            gainReductionDb_ = 0.0f;
            overloadActivity_ = 0.0f;
            return;
        }
        bypassWasActive_ = false;
        smoothParameters();

        const float maximumMagnitude = std::max(
            std::abs(input[0u]), std::abs(input[1u]));
        std::array<float, 2u> detector {};
        std::array<float, 2u> transient {};
        for (uint32_t channel = 0u; channel < channels_.size(); ++channel) {
            auto& state = channels_[channel];
            const float magnitude = lerp(
                std::abs(input[channel]), maximumMagnitude,
                smoothed_.stereoLink);
            state.fastEnvelope = followEnvelope(
                state.fastEnvelope, magnitude,
                fastAttackCoefficient_, fastReleaseCoefficient_);
            state.slowEnvelope = followEnvelope(
                state.slowEnvelope, magnitude,
                slowAttackCoefficient_, slowReleaseCoefficient_);
            detector[channel] = state.fastEnvelope;
            transient[channel] = clamp(
                (state.fastEnvelope - state.slowEnvelope)
                    / (state.slowEnvelope + 0.035f),
                0.0f, 1.0f);
        }

        const float overloadDriveDb = 3.0f
            + 27.0f * smoothed_.overload * smoothed_.overload;
        const float driveGain = dbToGain(
            smoothed_.inputGainDb + overloadDriveDb);
        const float threshold = lerp(
            0.92f, 0.065f,
            smoothed_.density * smoothed_.density);
        float minimumGain = 1.0f;
        float maximumActivity = 0.0f;

        std::array<float, 2u> output {};
        for (uint32_t channel = 0u; channel < channels_.size(); ++channel) {
            auto& state = channels_[channel];
            float densityGain = 1.0f;
            const float drivenEnvelope = detector[channel] * driveGain;
            if (drivenEnvelope > threshold) {
                densityGain = std::pow(
                    threshold / drivenEnvelope,
                    0.72f * smoothed_.density);
            }
            minimumGain = std::min(minimumGain, densityGain);

            const float transientAmount = transient[channel];
            float punchGain = 1.0f;
            if (smoothed_.punch >= 0.0f) {
                punchGain += smoothed_.punch * 1.35f * transientAmount;
            } else {
                punchGain /= 1.0f + (-smoothed_.punch)
                    * 2.8f * transientAmount;
            }

            const float clean = input[channel];
            state.weightLowpass = flushDenormal(state.weightLowpass
                + (clean - state.weightLowpass) * weightCoefficient_);
            const float preservedLow = state.weightLowpass
                * smoothed_.weight * 0.78f;
            const float toOverload = (clean - preservedLow)
                * driveGain * densityGain * punchGain;

            float processed = processOversampled(state, toOverload)
                + preservedLow;
            state.toneLowpass = flushDenormal(state.toneLowpass
                + (processed - state.toneLowpass) * toneCoefficient_);
            const float high = processed - state.toneLowpass;
            if (smoothed_.tone >= 0.0f) {
                processed = state.toneLowpass
                    * (1.0f - 0.10f * smoothed_.tone)
                    + high * (1.0f + 1.20f * smoothed_.tone);
            } else {
                const float dark = -smoothed_.tone;
                processed = state.toneLowpass * (1.0f + 0.12f * dark)
                    + high * (1.0f - 0.88f * dark);
            }
            processed = dcBlock(state, processed);

            const float wet = lerp(clean, processed, smoothed_.mix);
            output[channel] = sanitizeSample(
                wet * dbToGain(smoothed_.outputGainDb));
            maximumActivity = std::max(maximumActivity,
                std::min(1.0f, std::abs(toOverload) * 0.18f
                    + smoothed_.overload * 0.55f));
        }

        gainReductionDb_ = gainToDb(std::max(minimumGain, 1.0e-6f));
        overloadActivity_ = maximumActivity;
        left = output[0u];
        right = output[1u];
    }

private:
    struct ChannelState {
        float fastEnvelope = 0.0f;
        float slowEnvelope = 0.0f;
        float weightLowpass = 0.0f;
        float toneLowpass = 0.0f;
        float previousDriven = 0.0f;
        float antiAliasOne = 0.0f;
        float antiAliasTwo = 0.0f;
        float tapeMemory = 0.0f;
        float tapeHead = 0.0f;
        float transformerLow = 0.0f;
        float speakerCone = 0.0f;
        float speakerEnvelope = 0.0f;
        float dcInput = 0.0f;
        float dcOutput = 0.0f;
    };

    static float finiteOr(float value, float fallback = 0.0f)
    {
        return std::isfinite(value) ? value : fallback;
    }

    static DrumOverloadParams sanitize(DrumOverloadParams params)
    {
        const uint32_t circuit = std::min<uint32_t>(
            static_cast<uint32_t>(params.circuit),
            kDrumOverloadCircuitCount - 1u);
        params.circuit = static_cast<DrumOverloadCircuit>(circuit);
        params.inputGainDb = clamp(finiteOr(params.inputGainDb), -18.0f, 24.0f);
        params.overload = clamp(finiteOr(params.overload), 0.0f, 1.0f);
        params.density = clamp(finiteOr(params.density), 0.0f, 1.0f);
        params.punch = clamp(finiteOr(params.punch), -1.0f, 1.0f);
        params.bias = clamp(finiteOr(params.bias), -1.0f, 1.0f);
        params.breakup = clamp(finiteOr(params.breakup), 0.0f, 1.0f);
        params.weight = clamp(finiteOr(params.weight), 0.0f, 1.0f);
        params.tone = clamp(finiteOr(params.tone), -1.0f, 1.0f);
        params.stereoLink = clamp(finiteOr(params.stereoLink), 0.0f, 1.0f);
        params.mix = clamp(finiteOr(params.mix), 0.0f, 1.0f);
        params.outputGainDb = clamp(
            finiteOr(params.outputGainDb), -36.0f, 12.0f);
        return params;
    }

    static float sanitizeSample(float input)
    {
        return clamp(finiteOr(input), -8.0f, 8.0f);
    }

    static float onePoleCoefficient(float timeMs, float sampleRate)
    {
        const float samples = std::max(1.0f,
            timeMs * 0.001f * sampleRate);
        return 1.0f - std::exp(-1.0f / samples);
    }

    static float frequencyCoefficient(float frequencyHz, float sampleRate)
    {
        const float frequency = clamp(
            frequencyHz, 1.0f, sampleRate * 0.45f);
        return 1.0f - std::exp(-2.0f * kPi * frequency / sampleRate);
    }

    static float followEnvelope(float current, float input,
        float attackCoefficient, float releaseCoefficient)
    {
        const float coefficient = input > current
            ? attackCoefficient : releaseCoefficient;
        return flushDenormal(current + (input - current) * coefficient);
    }

    void smoothParameters()
    {
        const auto smooth = [this](float& value, float target) {
            value = flushDenormal(value
                + (target - value) * parameterCoefficient_);
        };
        smoothed_.circuit = target_.circuit;
        smoothed_.bypass = target_.bypass;
        smooth(smoothed_.inputGainDb, target_.inputGainDb);
        smooth(smoothed_.overload, target_.overload);
        smooth(smoothed_.density, target_.density);
        smooth(smoothed_.punch, target_.punch);
        smooth(smoothed_.bias, target_.bias);
        smooth(smoothed_.breakup, target_.breakup);
        smooth(smoothed_.weight, target_.weight);
        smooth(smoothed_.tone, target_.tone);
        smooth(smoothed_.stereoLink, target_.stereoLink);
        smooth(smoothed_.mix, target_.mix);
        smooth(smoothed_.outputGainDb, target_.outputGainDb);
    }

    float transfer(float input) const
    {
        const float x = clamp(input, -32.0f, 32.0f);
        const float amount = smoothed_.overload;
        const float offset = smoothed_.bias * (0.10f + 0.42f * amount);
        const float u = x + offset;
        float shaped = x;
        switch (smoothed_.circuit) {
        case DrumOverloadCircuit::Console: {
            const float zero = std::atan(offset * 1.35f);
            shaped = (std::atan(u * 1.35f) - zero) / std::atan(1.35f);
            break;
        }
        case DrumOverloadCircuit::Valve: {
            const float zero = std::tanh(offset * 1.55f);
            shaped = (std::tanh(u * 1.55f) - zero) / std::tanh(1.55f);
            const float even = x * x * (x >= 0.0f ? 1.0f : -0.35f);
            shaped += even * smoothed_.bias * 0.11f;
            break;
        }
        case DrumOverloadCircuit::Clip: {
            const float soft = std::tanh(u * 2.5f) / std::tanh(2.5f);
            const float hard = clamp(u * 1.18f, -1.0f, 1.0f);
            shaped = lerp(soft, hard, 0.62f + 0.28f * smoothed_.breakup)
                - lerp(std::tanh(offset * 2.5f) / std::tanh(2.5f),
                    clamp(offset * 1.18f, -1.0f, 1.0f),
                    0.62f + 0.28f * smoothed_.breakup);
            break;
        }
        case DrumOverloadCircuit::Rupture: {
            const float zero = std::tanh(offset * 1.8f);
            const float base = std::tanh(u * 1.8f) - zero;
            const float phase = 2.2f + 5.8f * smoothed_.breakup;
            const float fractured = (std::sin(u * phase)
                - std::sin(offset * phase)) * 0.74f;
            shaped = lerp(base, fractured,
                0.42f + 0.46f * smoothed_.breakup);
            break;
        }
        case DrumOverloadCircuit::Tape: {
            const auto tapeCurve = [](float value) {
                return std::tanh(value * 1.18f)
                    + 0.035f * std::sin(value * 2.55f);
            };
            const float normalization = std::max(0.01f, tapeCurve(1.0f));
            shaped = (tapeCurve(u) - tapeCurve(offset)) / normalization;
            break;
        }
        case DrumOverloadCircuit::Transformer: {
            const auto transformerCurve = [](float value) {
                return std::atan(value * 1.88f)
                    + 0.025f * std::sin(value * 3.0f);
            };
            const float normalization = std::max(
                0.01f, transformerCurve(1.0f));
            shaped = (transformerCurve(u) - transformerCurve(offset))
                / normalization;
            break;
        }
        case DrumOverloadCircuit::Diode: {
            const auto diodeCurve = [](float value) {
                if (value >= 0.0f) {
                    return (1.0f - std::exp(-2.80f * value))
                        / (1.0f - std::exp(-2.80f));
                }
                return -(1.0f - std::exp(1.28f * value))
                    / (1.0f - std::exp(-1.28f));
            };
            shaped = diodeCurve(u) - diodeCurve(offset);
            break;
        }
        case DrumOverloadCircuit::Speaker: {
            const auto speakerCurve = [this](float value) {
                const float base = std::tanh(value * 2.18f);
                return base - base * base * base
                    * (0.11f + 0.16f * smoothed_.breakup);
            };
            const float normalization = std::max(
                0.01f, speakerCurve(1.0f));
            shaped = (speakerCurve(u) - speakerCurve(offset))
                / normalization;
            break;
        }
        }

        const float hardness = smoothed_.breakup * (0.22f + 0.48f * amount);
        const float hardShape = std::tanh(shaped * (1.0f + 5.0f * hardness))
            / std::tanh(1.0f + 5.0f * hardness);
        shaped = lerp(shaped, hardShape, hardness);
        return lerp(x, shaped, amount);
    }

    float processOversampled(ChannelState& state, float input) const
    {
        float output = 0.0f;
        for (uint32_t phase = 0u; phase < 2u; ++phase) {
            const float fraction = phase == 0u ? 0.5f : 1.0f;
            float interpolated = lerp(
                state.previousDriven, input, fraction);

            if (smoothed_.circuit == DrumOverloadCircuit::Tape) {
                state.tapeMemory = flushDenormal(state.tapeMemory
                    + (interpolated - state.tapeMemory)
                        * tapeMemoryCoefficient_);
                interpolated += state.tapeMemory
                    * (0.10f + 0.15f * smoothed_.breakup);
            } else if (smoothed_.circuit
                    == DrumOverloadCircuit::Transformer) {
                state.transformerLow = flushDenormal(state.transformerLow
                    + (interpolated - state.transformerLow)
                        * transformerCoefficient_);
                interpolated += state.transformerLow
                    * (0.16f + 0.24f * smoothed_.breakup);
            } else if (smoothed_.circuit
                    == DrumOverloadCircuit::Speaker) {
                const float magnitude = std::abs(interpolated);
                const float sagCoefficient = magnitude > state.speakerEnvelope
                    ? sagAttackCoefficient_ : sagReleaseCoefficient_;
                state.speakerEnvelope = followEnvelope(
                    state.speakerEnvelope, magnitude,
                    sagCoefficient, sagCoefficient);
                interpolated /= 1.0f + state.speakerEnvelope
                    * (0.22f + 0.72f * smoothed_.breakup);
            }

            const float nonlinear = transfer(interpolated);
            float circuitOutput = nonlinear;
            if (smoothed_.circuit == DrumOverloadCircuit::Tape) {
                state.tapeHead = flushDenormal(state.tapeHead
                    + (nonlinear - state.tapeHead) * tapeHeadCoefficient_);
                circuitOutput = lerp(nonlinear, state.tapeHead,
                    0.18f + 0.36f * smoothed_.breakup);
            } else if (smoothed_.circuit
                    == DrumOverloadCircuit::Speaker) {
                state.speakerCone = flushDenormal(state.speakerCone
                    + (nonlinear - state.speakerCone) * speakerCoefficient_);
                circuitOutput = state.speakerCone;
            }
            state.antiAliasOne = flushDenormal(state.antiAliasOne
                + (circuitOutput - state.antiAliasOne)
                    * antiAliasCoefficient_);
            state.antiAliasTwo = flushDenormal(state.antiAliasTwo
                + (state.antiAliasOne - state.antiAliasTwo)
                    * antiAliasCoefficient_);
            output = state.antiAliasTwo;
        }
        state.previousDriven = input;
        return output;
    }

    float dcBlock(ChannelState& state, float input) const
    {
        const float output = flushDenormal(input - state.dcInput
            + dcCoefficient_ * state.dcOutput);
        state.dcInput = input;
        state.dcOutput = output;
        return output;
    }

    double sampleRate_ = 48000.0;
    DrumOverloadParams target_ {};
    DrumOverloadParams smoothed_ {};
    std::array<ChannelState, 2u> channels_ {};
    float parameterCoefficient_ = 1.0f;
    float fastAttackCoefficient_ = 1.0f;
    float fastReleaseCoefficient_ = 1.0f;
    float slowAttackCoefficient_ = 1.0f;
    float slowReleaseCoefficient_ = 1.0f;
    float weightCoefficient_ = 1.0f;
    float toneCoefficient_ = 1.0f;
    float antiAliasCoefficient_ = 1.0f;
    float tapeMemoryCoefficient_ = 1.0f;
    float tapeHeadCoefficient_ = 1.0f;
    float transformerCoefficient_ = 1.0f;
    float speakerCoefficient_ = 1.0f;
    float sagAttackCoefficient_ = 1.0f;
    float sagReleaseCoefficient_ = 1.0f;
    float dcCoefficient_ = 0.0f;
    float gainReductionDb_ = 0.0f;
    float overloadActivity_ = 0.0f;
    bool bypassWasActive_ = false;
};

} // namespace s3g
