#pragma once

#include "s3g_math.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace s3g {

constexpr uint32_t kFractureTimeBufferSize = 8192u;

enum class FractureProcessor : uint32_t {
    Relay = 0,
    Crush,
    Splice,
    Logic,
    Void,
    Throat,
    Robot,
    OctDown,
    OctUp,
    OctStack,
    Count,
};

constexpr uint32_t kFractureProcessorCount =
    static_cast<uint32_t>(FractureProcessor::Count);

inline const char* fractureProcessorName(FractureProcessor processor)
{
    switch (processor) {
    case FractureProcessor::Relay: return "RELAY";
    case FractureProcessor::Crush: return "CRUSH";
    case FractureProcessor::Splice: return "SPLICE";
    case FractureProcessor::Logic: return "LOGIC";
    case FractureProcessor::Void: return "VOID";
    case FractureProcessor::Throat: return "THROAT";
    case FractureProcessor::Robot: return "ROBOT";
    case FractureProcessor::OctDown: return "OCT DOWN";
    case FractureProcessor::OctUp: return "OCT UP";
    case FractureProcessor::OctStack: return "OCT STACK";
    case FractureProcessor::Count: break;
    }
    return "RELAY";
}

inline const char* fractureAmountLabel(FractureProcessor processor)
{
    switch (processor) {
    case FractureProcessor::Relay: return "THRESH";
    case FractureProcessor::Crush: return "BITS";
    case FractureProcessor::Splice: return "DEPTH";
    case FractureProcessor::Logic:
    case FractureProcessor::Void:
    case FractureProcessor::Robot: return "DEPTH";
    case FractureProcessor::Throat:
    case FractureProcessor::OctDown:
    case FractureProcessor::OctUp:
    case FractureProcessor::OctStack: return "MIX";
    case FractureProcessor::Count: break;
    }
    return "AMOUNT";
}

inline const char* fractureColorLabel(FractureProcessor processor)
{
    switch (processor) {
    case FractureProcessor::Relay: return "CHATTER";
    case FractureProcessor::Crush: return "RATE";
    case FractureProcessor::Splice: return "LENGTH";
    case FractureProcessor::Logic: return "THRESH";
    case FractureProcessor::Void: return "RECOVER";
    case FractureProcessor::Throat: return "VOWEL";
    case FractureProcessor::Robot: return "CARRIER";
    case FractureProcessor::OctDown: return "FILTER";
    case FractureProcessor::OctUp:
    case FractureProcessor::OctStack: return "TONE";
    case FractureProcessor::Count: break;
    }
    return "COLOR";
}

inline const char* fractureBiasLabel(FractureProcessor processor)
{
    switch (processor) {
    case FractureProcessor::Relay: return "ASYM";
    case FractureProcessor::Crush: return "DITHER";
    case FractureProcessor::Splice: return "DIRECTION";
    case FractureProcessor::Logic:
    case FractureProcessor::OctStack: return "BALANCE";
    case FractureProcessor::Void: return "SKEW";
    case FractureProcessor::Throat: return "SHIFT";
    case FractureProcessor::Robot: return "CHARACTER";
    case FractureProcessor::OctDown: return "TRACK";
    case FractureProcessor::OctUp: return "SHAPE";
    case FractureProcessor::Count: break;
    }
    return "BIAS";
}

inline float fractureWrapPhase(float phase)
{
    phase -= std::floor(phase);
    return phase < 0.0f ? phase + 1.0f : phase;
}

template <typename Runtime, typename State>
float processFractureProcessor(FractureProcessor processor,
    Runtime& runtime, State& state, float input, float modulationSource,
    float amount, float color, float rawBias, float sampleRate)
{
    input = std::isfinite(input) ? input : 0.0f;
    modulationSource =
        std::isfinite(modulationSource) ? modulationSource : 0.0f;
    amount = clamp(std::isfinite(amount) ? amount : 0.0f, 0.0f, 1.0f);
    color = clamp(std::isfinite(color) ? color : 0.5f, 0.0f, 1.0f);
    rawBias = clamp(
        std::isfinite(rawBias) ? rawBias : 0.0f, -1.0f, 1.0f);
    const float bias = rawBias * 0.22f;
    const float sr = std::max(1.0f, sampleRate);
    const auto onePole = [sr](float frequency) {
        return 1.0f - std::exp(-2.0f * kPi
            * std::min(frequency, sr * 0.45f) / sr);
    };

    switch (processor) {
    case FractureProcessor::Relay: {
        const float absolute = std::abs(input + bias);
        const float attack = onePole(lerp(180.0f, 4200.0f, color));
        const float release = onePole(lerp(3.0f, 120.0f, color));
        state.envelope += (absolute - state.envelope)
            * (absolute > state.envelope ? attack : release);
        const float threshold = lerp(0.34f, 0.008f, amount);
        const float hysteresis = 0.18f + std::abs(rawBias) * 0.62f;
        if (state.gate < 0.5f && state.envelope > threshold) {
            state.gate = 1.0f;
        } else if (state.gate > 0.5f
            && state.envelope < threshold * hysteresis) {
            state.gate = 0.0f;
        }
        const float chatter = std::sin(state.phase * 2.0f * kPi)
            * color * (0.08f + amount * 0.24f);
        state.phase = fractureWrapPhase(state.phase
            + (80.0f + color * 1700.0f) / sr);
        const float gateTarget = state.gate > 0.5f
            ? 1.0f : clamp(chatter, 0.0f, 1.0f);
        state.memory += (gateTarget - state.memory)
            * onePole(lerp(90.0f, 4800.0f, color));
        return input * state.memory;
    }
    case FractureProcessor::Crush: {
        const float holdSamples = lerp(sr / 420.0f,
            std::max(1.0f, sr / 18000.0f), color * color);
        state.phase += 1.0f;
        if (state.phase >= holdSamples) {
            state.phase -= holdSamples;
            state.memory = input + rawBias * amount * 0.018f;
        }
        const float bits = lerp(16.0f, 2.0f, amount * amount);
        const float steps = std::pow(2.0f, bits - 1.0f);
        state.envelope += 0.754877666f;
        if (state.envelope >= 1.0f) state.envelope -= 1.0f;
        const float dither = (state.envelope - 0.5f)
            * std::abs(rawBias) / steps;
        return std::round((state.memory + dither) * steps) / steps;
    }
    case FractureProcessor::Splice: {
        runtime.timeBuffer[runtime.timeWrite] = input;
        runtime.timeWrite =
            (runtime.timeWrite + 1u) % kFractureTimeBufferSize;
        runtime.timeValid = std::min<uint32_t>(
            kFractureTimeBufferSize, runtime.timeValid + 1u);
        const float maximumMs = std::min(80.0f,
            static_cast<float>(kFractureTimeBufferSize - 2u)
                * 1000.0f / sr);
        const float lengthMs = std::pow(maximumMs, color * color);
        const uint32_t length = std::clamp<uint32_t>(
            static_cast<uint32_t>(sr * lengthMs * 0.001f), 2u,
            kFractureTimeBufferSize - 2u);
        state.phase += 1.0f;
        if (state.phase >= static_cast<float>(length)) {
            state.phase -= static_cast<float>(length);
            state.gate = state.gate > 0.5f ? 0.0f : 1.0f;
        }
        const uint32_t position =
            static_cast<uint32_t>(state.phase) % length;
        const bool reverse = rawBias < -0.12f
            || (std::abs(rawBias) < 0.12f && state.gate > 0.5f);
        const uint32_t offset =
            reverse ? position : length - position;
        const uint32_t read = (runtime.timeWrite
            + kFractureTimeBufferSize
            - std::min<uint32_t>(offset + 1u,
                kFractureTimeBufferSize - 1u))
            % kFractureTimeBufferSize;
        const uint32_t age = std::min<uint32_t>(offset + 1u,
            kFractureTimeBufferSize - 1u);
        const float repeat = age <= runtime.timeValid
            ? runtime.timeBuffer[read] : 0.0f;
        const uint32_t edgeSamples = std::min<uint32_t>(
            std::max<uint32_t>(1u,
                static_cast<uint32_t>(sr * 0.006f)),
            std::max<uint32_t>(1u, length / 4u));
        const float edgeIn = static_cast<float>(position)
            / static_cast<float>(edgeSamples);
        const float edgeOut = static_cast<float>(length - 1u - position)
            / static_cast<float>(edgeSamples);
        const float edge = clamp(std::min(edgeIn, edgeOut), 0.0f, 1.0f);
        const float window = edge * edge * (3.0f - 2.0f * edge);
        return lerp(input,
            std::tanh(repeat * (1.0f + amount * 4.0f)),
            (0.25f + amount * 0.75f) * window);
    }
    case FractureProcessor::Logic: {
        const float threshold = lerp(0.42f, 0.015f, color);
        const bool a = input + bias >= threshold;
        const bool b = modulationSource - bias >= threshold;
        const float logic = (a != b ? 1.0f : -1.0f)
            * (0.34f + amount * 0.66f);
        state.memory += (logic - state.memory)
            * onePole(lerp(350.0f, 12000.0f,
                0.5f + rawBias * 0.5f));
        return lerp(input, state.memory, amount);
    }
    case FractureProcessor::Void: {
        const float absolute = std::abs(input);
        const float follow = onePole(lerp(2.0f, 80.0f, color));
        state.envelope += (absolute - state.envelope) * follow;
        const float threshold = lerp(0.012f, 0.46f, amount);
        const float skew = rawBias * 0.35f;
        const bool shouldOpen = input + skew > 0.0f
            ? state.envelope > threshold
            : state.envelope > threshold
                * (1.0f + std::abs(skew));
        const float target = shouldOpen ? 1.0f : 0.0f;
        const float speed = onePole(lerp(4.0f, 850.0f, color));
        state.gate += (target - state.gate) * speed;
        return input * lerp(1.0f - amount, state.gate, amount);
    }
    case FractureProcessor::Throat: {
        const float shift = std::pow(2.0f, rawBias * 0.72f);
        const float firstHz = clamp(
            lerp(230.0f, 920.0f, color) * shift,
            80.0f, sr * 0.18f);
        const float secondHz = clamp(
            lerp(2450.0f, 1050.0f, color) * shift,
            180.0f, sr * 0.24f);
        const float firstCoefficient =
            2.0f * std::sin(kPi * firstHz / sr);
        const float secondCoefficient =
            2.0f * std::sin(kPi * secondHz / sr);
        const float damping = 0.10f + (1.0f - amount) * 0.18f;
        state.low += firstCoefficient * state.high;
        const float firstHigh =
            input - state.low - damping * state.high;
        state.high += firstCoefficient * firstHigh;
        state.memory += secondCoefficient * state.envelope;
        const float secondHigh =
            input - state.memory - damping * state.envelope;
        state.envelope += secondCoefficient * secondHigh;
        state.low = flushDenormal(clamp(state.low, -8.0f, 8.0f));
        state.high = flushDenormal(clamp(state.high, -8.0f, 8.0f));
        state.memory =
            flushDenormal(clamp(state.memory, -8.0f, 8.0f));
        state.envelope =
            flushDenormal(clamp(state.envelope, -8.0f, 8.0f));
        const float voiced = std::tanh((state.high * 1.35f
            + state.envelope) * (1.2f + amount * 3.8f));
        return lerp(input, voiced, amount);
    }
    case FractureProcessor::Robot: {
        const float carrierHz = 28.0f * std::pow(40.0f, color);
        state.phase = fractureWrapPhase(
            state.phase + carrierHz / sr);
        const float sine = std::sin(state.phase * 2.0f * kPi);
        const float square = sine >= 0.0f ? 1.0f : -1.0f;
        const float carrier = lerp(
            sine, square, (rawBias + 1.0f) * 0.5f);
        const float robotic = std::tanh(input * carrier
            * (1.5f + amount * 6.5f));
        return lerp(input, robotic, amount);
    }
    case FractureProcessor::OctDown: {
        const float threshold =
            lerp(0.001f, 0.10f, (rawBias + 1.0f) * 0.5f);
        if (input > threshold && state.memory <= threshold)
            state.gate = state.gate > 0.5f ? 0.0f : 1.0f;
        state.memory = input;
        state.envelope += (std::abs(input) - state.envelope)
            * onePole(lerp(18.0f, 110.0f, color));
        const float divided =
            (state.gate > 0.5f ? 1.0f : -1.0f)
            * state.envelope * 1.65f;
        state.low += (divided - state.low)
            * onePole(lerp(140.0f, 5200.0f, color));
        return lerp(input, state.low, amount);
    }
    case FractureProcessor::OctUp: {
        const float shifted = input + rawBias * 0.035f;
        const float rectified = std::abs(shifted);
        state.envelope += (rectified - state.envelope)
            * onePole(lerp(8.0f, 42.0f, color));
        const float doubled =
            (rectified - state.envelope) * 2.1f;
        state.high += (doubled - state.high)
            * onePole(lerp(900.0f, 15000.0f, color));
        return lerp(input, state.high, amount);
    }
    case FractureProcessor::OctStack: {
        constexpr float threshold = 0.008f;
        if (input > threshold && state.memory <= threshold)
            state.gate = state.gate > 0.5f ? 0.0f : 1.0f;
        state.memory = input;
        const float absolute = std::abs(input);
        state.envelope +=
            (absolute - state.envelope) * onePole(38.0f);
        state.low += (absolute - state.low) * onePole(16.0f);
        const float upper = (absolute - state.low) * 2.0f;
        const float lower =
            (state.gate > 0.5f ? 1.0f : -1.0f)
            * state.envelope * 1.55f;
        const float stacked =
            lerp(lower, upper, (rawBias + 1.0f) * 0.5f);
        state.high += (stacked - state.high)
            * onePole(lerp(240.0f, 13000.0f, color));
        return lerp(input, state.high, amount);
    }
    case FractureProcessor::Count:
        break;
    }
    return input;
}

} // namespace s3g
