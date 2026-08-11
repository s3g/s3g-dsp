#pragma once

#include "s3g_math.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace s3g {

inline float modulationCircuitWrapPhase(float phase)
{
    phase -= std::floor(phase);
    return phase < 0.0f ? phase + 1.0f : phase;
}

// State is supplied by the caller so the same circuit can live in NIM's
// fixed insert bank or another processor's independently allocated lanes.
// Required state members: phase and gate.
template <typename State>
float processRotorCircuit(State& state, float input, float amount,
    float rateControl, float shape, float sampleRate)
{
    input = std::isfinite(input) ? input : 0.0f;
    amount = clamp(std::isfinite(amount) ? amount : 0.0f, 0.0f, 1.0f);
    rateControl = clamp(std::isfinite(rateControl)
        ? rateControl : 0.5f, 0.0f, 1.0f);
    shape = clamp(std::isfinite(shape) ? shape : 0.0f, -1.0f, 1.0f);
    const float sr = std::max(1.0f, sampleRate);
    const float rate = 0.08f * std::pow(562.5f, rateControl);
    state.phase = modulationCircuitWrapPhase(state.phase + rate / sr);
    const float sine = std::sin(state.phase * 2.0f * kPi);
    const float shaped = std::tanh((sine + shape * 0.62f)
        * (1.25f + std::abs(shape) * 5.5f));
    const float amplitude = 0.5f + shaped * 0.5f;
    return input * lerp(1.0f, amplitude * 1.32f, amount);
}

// Runtime must supply timeBuffer and timeWrite; State supplies phase and gate.
// The buffer may be a fixed std::array (NIM) or a vector allocated in prepare.
template <typename Runtime, typename State>
float processChorusCircuit(Runtime& runtime, State& state, float input,
    float depth, float rateControl, float feedbackControl, float sampleRate)
{
    input = std::isfinite(input) ? input : 0.0f;
    depth = clamp(std::isfinite(depth) ? depth : 0.0f, 0.0f, 1.0f);
    rateControl = clamp(std::isfinite(rateControl)
        ? rateControl : 0.5f, 0.0f, 1.0f);
    feedbackControl = clamp(std::isfinite(feedbackControl)
        ? feedbackControl : 0.0f, -1.0f, 1.0f);
    const float sr = std::max(1.0f, sampleRate);
    const uint32_t size = static_cast<uint32_t>(runtime.timeBuffer.size());
    if (size < 8u) return input;
    runtime.timeWrite %= size;
    const float feedback = feedbackControl * 0.38f;
    runtime.timeBuffer[runtime.timeWrite] = clamp(
        input + state.gate * feedback, -5.0f, 5.0f);
    runtime.timeWrite = (runtime.timeWrite + 1u) % size;
    const float rate = 0.05f * std::pow(120.0f, rateControl);
    state.phase = modulationCircuitWrapPhase(state.phase + rate / sr);
    const float modulation = std::sin(state.phase * 2.0f * kPi);
    const float baseMs = 8.0f + std::abs(feedbackControl) * 11.0f;
    const float depthMs = 0.6f + depth * 8.5f;
    const float delaySamples = clamp(
        (baseMs + modulation * depthMs) * sr * 0.001f,
        1.0f, static_cast<float>(size - 3u));
    const uint32_t delayWhole = static_cast<uint32_t>(delaySamples);
    const float fraction = delaySamples - static_cast<float>(delayWhole);
    const uint32_t first = (runtime.timeWrite + size
        - delayWhole - 1u) % size;
    const uint32_t second = (first + size - 1u) % size;
    const float delayed = lerp(runtime.timeBuffer[first],
        runtime.timeBuffer[second], fraction);
    state.gate = flushDenormal(delayed);
    return lerp(input, delayed, depth * 0.78f);
}

} // namespace s3g
