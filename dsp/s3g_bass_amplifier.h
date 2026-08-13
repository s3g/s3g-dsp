#pragma once

#include "s3g_math.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

// Functional bass-drive families. The names describe signal topology rather
// than commercial products: parallel clipping, a warmer memory-bearing path,
// two morphable asymmetric transfers, and clean-low/distorted-high splitting.
enum class BassPedalCircuit : uint32_t {
    Bypass = 0u,
    ModernParallel,
    WarmParallel,
    DualAsymmetry,
    SplitBand,
    Count,
};

inline constexpr uint32_t kBassPedalCircuitCount =
    static_cast<uint32_t>(BassPedalCircuit::Count);

inline const char* bassPedalCircuitName(BassPedalCircuit circuit)
{
    switch (circuit) {
    case BassPedalCircuit::Bypass: return "BYPASS";
    case BassPedalCircuit::ModernParallel: return "MODERN PARALLEL";
    case BassPedalCircuit::WarmParallel: return "WARM PARALLEL";
    case BassPedalCircuit::DualAsymmetry: return "DUAL ASYMMETRY";
    case BassPedalCircuit::SplitBand: return "SPLIT BAND";
    case BassPedalCircuit::Count: break;
    }
    return "BYPASS";
}

struct BassAmplifierParams {
    float valvePreamp = 0.34f;
    float powerStage = 0.30f;
    float supplySag = 0.22f;
    float bassEq = 0.0f;
    float midEq = 0.0f;
    uint32_t midFrequency = 2u;
    float trebleEq = 0.0f;
    float cabinet = 0.42f;
    BassPedalCircuit pedalCircuit = BassPedalCircuit::Bypass;
    float pedalDrive = 0.0f;
    float pedalTone = 0.5f;
    float pedalCharacter = 0.5f;
    float pedalCrossoverHz = 240.0f;
    float pedalBlend = 0.0f;
};

// A compact realtime bass amplification chain. It is intentionally a musical
// topology model, not a component-exact amplifier or pedal clone. The amp
// stage uses cascaded asymmetric voltage gain, a push-pull power transfer,
// envelope-dependent rail compression, the documented broad tone centers of
// a large all-valve bass head, and a blendable 8x10-like bandwidth contour.
class BassAmplifierCircuit {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = std::isfinite(sampleRate)
            ? std::clamp(sampleRate, 8000.0, 768000.0) : 48000.0;
        reset();
    }

    void reset()
    {
        channels_.fill({});
        smoothed_ = params_;
        ampPathMix_ = ampPathTarget(params_);
    }

    void setParams(BassAmplifierParams params)
    {
        params_ = sanitize(params);
    }

    BassAmplifierParams params() const { return params_; }

    void processStereo(float& left, float& right)
    {
        smoothParams();
        // The transformer and cabinet states remain warm at the neutral
        // endpoint. Crossfade the complete topology instead of switching to
        // an exact bypass at a small parameter threshold; that former branch
        // could expose the transformer state as a one-sample discontinuity.
        const float pathTarget = ampPathTarget(params_);
        const float pathCoefficient = 1.0f - static_cast<float>(std::exp(
            -1.0 / std::max(1.0, sampleRate_ * 0.020)));
        ampPathMix_ += (pathTarget - ampPathMix_) * pathCoefficient;
        if (std::fabs(pathTarget - ampPathMix_) < 1.0e-7f) {
            ampPathMix_ = pathTarget;
        }
        const float dryLeft = left;
        const float dryRight = right;
        const float ampLeft = processChannel(channels_[0u], left);
        const float ampRight = processChannel(channels_[1u], right);
        left = lerp(dryLeft, ampLeft, ampPathMix_);
        right = lerp(dryRight, ampRight, ampPathMix_);
    }

private:
    struct ChannelState {
        float previousInput = 0.0f;
        float inputDc = 0.0f;
        float bassLow = 0.0f;
        float midLow = 0.0f;
        float midHigh = 0.0f;
        float trebleLow = 0.0f;
        float clarityInputLow = 0.0f;
        float clarityOutputLow = 0.0f;
        float preampMemory = 0.0f;
        float preampDc = 0.0f;
        float sagEnvelope = 0.0f;
        float powerDc = 0.0f;
        float transformerLow = 0.0f;
        float cabinetLow = 0.0f;
        float cabinetHigh = 0.0f;
        float pedalLow = 0.0f;
        float pedalHigh = 0.0f;
        float pedalMemory = 0.0f;
        float pedalEnvelope = 0.0f;
        float pedalDc = 0.0f;
    };

    static float finiteClamp(float value, float fallback,
        float minimum, float maximum)
    {
        return std::clamp(std::isfinite(value) ? value : fallback,
            minimum, maximum);
    }

    static BassAmplifierParams sanitize(BassAmplifierParams p)
    {
        p.valvePreamp = finiteClamp(p.valvePreamp, 0.34f, 0.0f, 1.0f);
        p.powerStage = finiteClamp(p.powerStage, 0.30f, 0.0f, 1.0f);
        p.supplySag = finiteClamp(p.supplySag, 0.22f, 0.0f, 1.0f);
        p.bassEq = finiteClamp(p.bassEq, 0.0f, -1.0f, 1.0f);
        p.midEq = finiteClamp(p.midEq, 0.0f, -1.0f, 1.0f);
        p.midFrequency = std::min<uint32_t>(p.midFrequency, 4u);
        p.trebleEq = finiteClamp(p.trebleEq, 0.0f, -1.0f, 1.0f);
        p.cabinet = finiteClamp(p.cabinet, 0.42f, 0.0f, 1.0f);
        p.pedalCircuit = static_cast<BassPedalCircuit>(
            std::min<uint32_t>(static_cast<uint32_t>(p.pedalCircuit),
                kBassPedalCircuitCount - 1u));
        p.pedalDrive = finiteClamp(p.pedalDrive, 0.0f, 0.0f, 1.0f);
        p.pedalTone = finiteClamp(p.pedalTone, 0.5f, 0.0f, 1.0f);
        p.pedalCharacter = finiteClamp(
            p.pedalCharacter, 0.5f, 0.0f, 1.0f);
        p.pedalCrossoverHz = finiteClamp(
            p.pedalCrossoverHz, 240.0f, 50.0f, 1000.0f);
        p.pedalBlend = finiteClamp(p.pedalBlend, 0.0f, 0.0f, 1.0f);
        return p;
    }

    static float ampPathTarget(const BassAmplifierParams& p)
    {
        const bool ampActive = p.valvePreamp > 0.0f
            || p.powerStage > 0.0f || p.supplySag > 0.0f
            || p.bassEq != 0.0f || p.midEq != 0.0f
            || p.trebleEq != 0.0f || p.cabinet > 0.0f;
        const bool pedalActive = p.pedalCircuit != BassPedalCircuit::Bypass
            && p.pedalBlend > 0.0f;
        return ampActive || pedalActive ? 1.0f : 0.0f;
    }

    float coefficient(float frequency, float rateScale = 1.0f) const
    {
        const float rate = static_cast<float>(sampleRate_) * rateScale;
        return 1.0f - std::exp(-2.0f * kPi
            * std::min(frequency, rate * 0.45f) / rate);
    }

    static float gainFromNormalizedDb(float value, float rangeDb)
    {
        return std::pow(10.0f, value * rangeDb * 0.05f);
    }

    static float asymmetricStage(float input, float gain, float bias)
    {
        const float positive = std::tanh((input + bias) * gain);
        const float negative = std::tanh((input - bias)
            * gain * 0.82f);
        const float zero = (std::tanh(bias * gain)
            + std::tanh(-bias * gain * 0.82f)) * 0.5f;
        return (positive + negative) * 0.5f - zero;
    }

    static float fold(float value)
    {
        const float shifted = value + 1.0f;
        const float wrapped = shifted
            - 4.0f * std::floor(shifted * 0.25f);
        return wrapped <= 2.0f ? wrapped - 1.0f : 3.0f - wrapped;
    }

    void smoothParams()
    {
        const float c = 1.0f - static_cast<float>(std::exp(
            -1.0 / std::max(1.0, sampleRate_ * 0.010)));
        const auto smooth = [c](float& value, float target) {
            value += (target - value) * c;
        };
        smooth(smoothed_.valvePreamp, params_.valvePreamp);
        smooth(smoothed_.powerStage, params_.powerStage);
        smooth(smoothed_.supplySag, params_.supplySag);
        smooth(smoothed_.bassEq, params_.bassEq);
        smooth(smoothed_.midEq, params_.midEq);
        smooth(smoothed_.trebleEq, params_.trebleEq);
        smooth(smoothed_.cabinet, params_.cabinet);
        smooth(smoothed_.pedalDrive, params_.pedalDrive);
        smooth(smoothed_.pedalTone, params_.pedalTone);
        smooth(smoothed_.pedalCharacter, params_.pedalCharacter);
        smooth(smoothed_.pedalCrossoverHz, params_.pedalCrossoverHz);
        smooth(smoothed_.pedalBlend, params_.pedalBlend);
        smoothed_.midFrequency = params_.midFrequency;
        smoothed_.pedalCircuit = params_.pedalCircuit;
    }

    float processPedal(ChannelState& state, float input)
    {
        if (smoothed_.pedalCircuit == BassPedalCircuit::Bypass
            || smoothed_.pedalBlend < 1.0e-5f) return input;

        const float crossover = smoothed_.pedalCrossoverHz;
        state.pedalLow += (input - state.pedalLow)
            * coefficient(crossover, 2.0f);
        const float low = state.pedalLow;
        const float high = input - low;
        const float drive = smoothed_.pedalDrive;
        const float tone = smoothed_.pedalTone;
        const float character = smoothed_.pedalCharacter;
        float wet = input;

        switch (smoothed_.pedalCircuit) {
        case BassPedalCircuit::ModernParallel: {
            const float bassEntry = lerp(0.18f, 1.35f, character);
            const float trebleEntry = lerp(0.52f, 1.65f, tone);
            const float clipped = std::tanh((high * trebleEntry
                + low * bassEntry) * (1.0f + drive * drive * 38.0f));
            state.pedalHigh += (clipped - state.pedalHigh)
                * coefficient(lerp(2200.0f, 11000.0f, tone), 2.0f);
            wet = state.pedalHigh;
            break;
        }
        case BassPedalCircuit::WarmParallel: {
            const float warmed = low * lerp(0.72f, 1.22f, character)
                + high * lerp(0.42f, 1.0f, tone);
            const float first = asymmetricStage(warmed,
                1.4f + drive * 12.0f, 0.045f + drive * 0.035f);
            state.pedalMemory += (first - state.pedalMemory)
                * coefficient(lerp(1800.0f, 6500.0f, tone), 2.0f);
            wet = asymmetricStage(state.pedalMemory,
                1.2f + drive * 4.0f, -0.025f);
            break;
        }
        case BassPedalCircuit::DualAsymmetry: {
            const float tight = std::tanh(high
                * (2.0f + drive * drive * 54.0f));
            const float rawInput = input * (1.5f + drive * 18.0f);
            const float raw = lerp(asymmetricStage(rawInput, 1.0f, 0.08f),
                fold(rawInput) * 0.82f, 0.35f + drive * 0.45f);
            wet = lerp(tight, raw, character);
            state.pedalHigh += (wet - state.pedalHigh)
                * coefficient(lerp(2600.0f, 12000.0f, tone), 2.0f);
            wet = state.pedalHigh;
            break;
        }
        case BassPedalCircuit::SplitBand: {
            const float envelopeCoefficient = coefficient(
                lerp(4.0f, 36.0f, character), 2.0f);
            state.pedalEnvelope += (std::fabs(low)
                - state.pedalEnvelope) * envelopeCoefficient;
            const float lowCompression = 1.0f
                / (1.0f + state.pedalEnvelope
                    * (1.0f + character * 8.0f));
            const float cleanLow = low * lowCompression
                * (1.0f + character * 0.55f);
            const float distortedHigh = std::tanh(high
                * (2.0f + drive * drive * 72.0f));
            state.pedalHigh += (distortedHigh - state.pedalHigh)
                * coefficient(lerp(1800.0f, 12000.0f, tone), 2.0f);
            wet = cleanLow + state.pedalHigh;
            break;
        }
        case BassPedalCircuit::Bypass:
        case BassPedalCircuit::Count:
            break;
        }

        const float dc = coefficient(8.0f, 2.0f);
        state.pedalDc += (wet - state.pedalDc) * dc;
        wet -= state.pedalDc;
        return lerp(input, wet, smoothed_.pedalBlend);
    }

    float processAmpSubstep(ChannelState& state, float input)
    {
        const float bassGain = gainFromNormalizedDb(smoothed_.bassEq, 12.0f);
        state.bassLow += (input - state.bassLow)
            * coefficient(40.0f, 2.0f);
        float shaped = input + state.bassLow * (bassGain - 1.0f);

        constexpr std::array<float, 5u> midCenters {{
            220.0f, 450.0f, 800.0f, 1600.0f, 3000.0f,
        }};
        const float center = midCenters[smoothed_.midFrequency];
        state.midLow += (shaped - state.midLow)
            * coefficient(center * 0.58f, 2.0f);
        state.midHigh += (shaped - state.midHigh)
            * coefficient(center * 1.72f, 2.0f);
        const float midBand = state.midHigh - state.midLow;
        const float midGain = gainFromNormalizedDb(smoothed_.midEq, 16.0f);
        shaped += midBand * (midGain - 1.0f);

        state.trebleLow += (shaped - state.trebleLow)
            * coefficient(4000.0f, 2.0f);
        const float high = shaped - state.trebleLow;
        const float trebleGain = gainFromNormalizedDb(
            smoothed_.trebleEq, 15.0f);
        shaped += high * (trebleGain - 1.0f);

        // Preserve a controlled part of the pitched low foundation around
        // the saturating stages. This is not a dry bypass: the restored band
        // still passes through the transformer and cabinet response below.
        state.clarityInputLow += (shaped - state.clarityInputLow)
            * coefficient(105.0f, 2.0f);

        const float preampAmount = smoothed_.valvePreamp;
        float preamped = shaped;
        if (preampAmount > 1.0e-5f) {
            const float first = asymmetricStage(shaped,
                1.0f + preampAmount * 7.0f,
                0.035f + preampAmount * 0.045f);
            state.preampMemory += (first - state.preampMemory)
                * coefficient(15000.0f, 2.0f);
            const float second = asymmetricStage(state.preampMemory,
                1.0f + preampAmount * 4.4f,
                -0.025f - preampAmount * 0.020f);
            preamped = lerp(shaped, second * 0.82f, preampAmount);
        }

        const float envelopeCoefficient = coefficient(
            preamped * preamped > state.sagEnvelope ? 28.0f : 3.2f, 2.0f);
        state.sagEnvelope += (preamped * preamped - state.sagEnvelope)
            * envelopeCoefficient;
        const float rail = 1.0f / (1.0f
            + state.sagEnvelope * smoothed_.supplySag * 2.8f);
        const float powerAmount = smoothed_.powerStage;
        const float powerInput = preamped * rail
            * (1.0f + powerAmount * 6.5f);
        const float positive = std::tanh(std::max(0.0f, powerInput)
            * (1.0f + powerAmount * 0.8f));
        const float negative = std::tanh(std::min(0.0f, powerInput)
            * (1.0f - powerAmount * 0.12f));
        const float powered = (positive + negative)
            / std::max(1.0f, 1.0f + powerAmount * 1.6f);
        float output = lerp(preamped, powered, powerAmount);
        state.clarityOutputLow += (output - state.clarityOutputLow)
            * coefficient(105.0f, 2.0f);
        const float clarityAmount = std::clamp(
            preampAmount * 0.18f + powerAmount * 0.22f, 0.0f, 0.34f);
        output += (state.clarityInputLow - state.clarityOutputLow)
            * clarityAmount;

        state.transformerLow += (output - state.transformerLow)
            * coefficient(20.0f, 2.0f);
        output -= state.transformerLow;
        const float cabinetCutoff = lerp(15000.0f, 5200.0f,
            smoothed_.cabinet);
        state.cabinetLow += (output - state.cabinetLow)
            * coefficient(cabinetCutoff, 2.0f);
        state.cabinetHigh += (output - state.cabinetHigh)
            * coefficient(82.0f, 2.0f);
        const float cabinetPunch = state.cabinetHigh - state.cabinetLow;
        const float cabinetOutput = state.cabinetLow
            + cabinetPunch * smoothed_.cabinet * 0.16f;
        return lerp(output, cabinetOutput, smoothed_.cabinet);
    }

    float processChannel(ChannelState& state, float input)
    {
        input = std::isfinite(input) ? input : 0.0f;
        const float midpoint = (state.previousInput + input) * 0.5f;
        state.previousInput = input;
        const float pedalMid = processPedal(state, midpoint);
        const float first = processAmpSubstep(state, pedalMid);
        const float pedalEnd = processPedal(state, input);
        const float second = processAmpSubstep(state, pedalEnd);
        const float output = (first + second) * 0.5f;
        return std::isfinite(output)
            ? std::clamp(output, -2.0f, 2.0f) : 0.0f;
    }

    double sampleRate_ = 48000.0;
    BassAmplifierParams params_ {};
    BassAmplifierParams smoothed_ {};
    float ampPathMix_ = 1.0f;
    std::array<ChannelState, 2u> channels_ {};
};

} // namespace s3g
