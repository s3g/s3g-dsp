#pragma once

#include "s3g_math.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace s3g {

// Shared analogue-inspired transfer families used by NIM inserts and the
// Macro Shred circuit selector. State is supplied by the caller so each lane
// can keep its own realtime-safe circuit memory.
enum class AnalogDriveCircuit : uint32_t {
    Wool = 0,
    Rat,
    ZoneA,
    ZoneB,
    FuzzI,
    FuzzII,
    Diode,
    Count,
};

constexpr uint32_t kAnalogDriveCircuitCount =
    static_cast<uint32_t>(AnalogDriveCircuit::Count);

inline const char* analogDriveCircuitName(AnalogDriveCircuit circuit)
{
    switch (circuit) {
    case AnalogDriveCircuit::Wool: return "WOOL";
    case AnalogDriveCircuit::Rat: return "RAT";
    case AnalogDriveCircuit::ZoneA: return "ZONE A";
    case AnalogDriveCircuit::ZoneB: return "ZONE B";
    case AnalogDriveCircuit::FuzzI: return "FUZZ I";
    case AnalogDriveCircuit::FuzzII: return "FUZZ II";
    case AnalogDriveCircuit::Diode: return "DIODE";
    case AnalogDriveCircuit::Count: break;
    }
    return "WOOL";
}

template <typename State>
float processAnalogDriveCircuit(AnalogDriveCircuit circuit, State& state,
    float input, float gain, float tone, float bias, float sampleRate)
{
    input = std::isfinite(input) ? input : 0.0f;
    gain = clamp(std::isfinite(gain) ? gain : 0.0f, 0.0f, 1.0f);
    tone = clamp(std::isfinite(tone) ? tone : 0.5f, 0.0f, 1.0f);
    bias = clamp(std::isfinite(bias) ? bias : 0.0f, -0.22f, 0.22f);
    const float sr = std::max(1.0f, sampleRate);
    const auto onePole = [sr](float frequency) {
        return 1.0f - std::exp(-2.0f * kPi
            * std::min(frequency, sr * 0.45f) / sr);
    };

    switch (circuit) {
    case AnalogDriveCircuit::Wool: {
        const float drive = 1.0f + gain * gain * 44.0f;
        const float first = std::tanh((input + bias) * drive);
        state.memory += (first - state.memory) * onePole(5200.0f);
        const float second = std::tanh(
            (state.memory - bias * 0.45f) * (2.2f + gain * 6.0f));
        state.low += (second - state.low)
            * onePole(lerp(360.0f, 2200.0f, tone));
        const float high = second - state.low;
        return std::tanh(lerp(state.low * 1.7f, high * 1.45f,
            tone) * 1.05f);
    }
    case AnalogDriveCircuit::Rat: {
        state.low += (input - state.low) * onePole(720.0f);
        const float pre = input - state.low * 0.78f;
        const float driven = pre * (2.0f + gain * gain * 78.0f) + bias;
        const float clipped = clamp(driven, -0.72f, 0.72f) / 0.72f;
        const float cutoff = 14000.0f
            * std::pow(420.0f / 14000.0f, tone);
        state.memory += (clipped - state.memory) * onePole(cutoff);
        return state.memory;
    }
    case AnalogDriveCircuit::ZoneA:
    case AnalogDriveCircuit::ZoneB: {
        const bool lower = circuit == AnalogDriveCircuit::ZoneB;
        const float lowCut = lower ? 120.0f : 260.0f;
        state.low += (input - state.low) * onePole(lowCut);
        const float upper = input - state.low;
        const float first = std::tanh((upper * (3.0f + gain * 54.0f)
            + bias) * (lower ? 0.72f : 1.0f));
        const float focusHz = lerp(lower ? 420.0f : 780.0f,
            lower ? 2600.0f : 5200.0f, tone);
        state.high += (first - state.high) * onePole(focusHz);
        const float focused = lower
            ? first + state.high * 0.48f
            : first + (first - state.high) * 0.72f;
        return std::tanh((focused - bias * 0.35f)
            * (2.2f + gain * 5.5f));
    }
    case AnalogDriveCircuit::FuzzI: {
        const float absolute = std::abs(input);
        state.envelope += (absolute - state.envelope)
            * onePole(lerp(4.0f, 30.0f, tone));
        const float sag = 1.0f
            / (1.0f + state.envelope * (1.0f + gain * 8.0f));
        const float drive = (2.0f + gain * gain * 64.0f) * sag;
        return std::tanh((input + bias) * drive)
            - std::tanh(bias * drive);
    }
    case AnalogDriveCircuit::FuzzII: {
        const float threshold = lerp(0.22f, 0.015f, gain);
        const float hold = state.memory;
        const float driven = input * (3.0f + gain * 72.0f) + bias;
        if (driven > threshold) state.memory = 1.0f;
        else if (driven < -threshold) state.memory = -1.0f;
        const float speed = onePole(lerp(900.0f, 9000.0f, tone));
        return lerp(hold, state.memory, speed);
    }
    case AnalogDriveCircuit::Diode: {
        const float drive = 1.0f + gain * gain * 36.0f;
        const float positive = std::tanh((input + bias) * drive);
        const float negative = std::tanh((input - bias)
            * drive * lerp(0.68f, 1.32f, tone));
        return (positive + negative) * 0.5f;
    }
    case AnalogDriveCircuit::Count:
        break;
    }
    return input;
}

} // namespace s3g
