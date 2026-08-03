#include "s3g_ambi_water_encoder.h"
#include "s3g_ambi_wind_encoder.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>

namespace {

struct ReferenceSvf {
    float ic1eq = 0.0f;
    float ic2eq = 0.0f;

    float process(float input, float cutoffHz, float resonance,
        float sampleRate, float minimumHz)
    {
        const float hz = s3g::clamp(cutoffHz, minimumHz, sampleRate * 0.45f);
        const float g = std::tan(s3g::kPi * hz / sampleRate);
        const float k = 2.0f - s3g::clamp(resonance, 0.0f, 1.0f) * 1.88f;
        const float a1 = 1.0f / (1.0f + g * (g + k));
        const float a2 = g * a1;
        const float a3 = g * a2;
        const float v3 = input - ic2eq;
        const float band = a1 * ic1eq + a2 * v3;
        const float low = ic2eq + a2 * ic1eq + a3 * v3;
        ic1eq = s3g::flushDenormal(2.0f * band - ic1eq);
        ic2eq = s3g::flushDenormal(2.0f * low - ic2eq);
        return band;
    }
};

template <typename CachedSvf>
bool runProbe(const char* label, float minimumHz)
{
    constexpr float sampleRate = 48000.0f;
    constexpr uint32_t frames = 48000u * 2u;
    CachedSvf cached;
    ReferenceSvf reference;
    double errorEnergy = 0.0;
    double referenceEnergy = 0.0;
    float previousCached = 0.0f;
    float previousReference = 0.0f;
    float maximumStepDelta = 0.0f;
    float maximumOutput = 0.0f;

    for (uint32_t frame = 0u; frame < frames; ++frame) {
        const float time = static_cast<float>(frame) / sampleRate;
        const float slow = 0.5f + 0.5f * std::sin(time * 0.73f);
        const float cutoff = 35.0f * std::pow(360.0f, slow)
            + std::sin(time * 7.13f) * 9.0f;
        const float resonance = 0.08f + 0.82f
            * (0.5f + 0.5f * std::sin(time * 0.41f + 0.8f));
        const float input = std::sin(time * 2.0f * s3g::kPi * 173.0f) * 0.24f
            + std::sin(time * 2.0f * s3g::kPi * 619.0f) * 0.07f;
        const float actual = cached.process(input, cutoff, resonance, sampleRate);
        const float expected = reference.process(
            input, cutoff, resonance, sampleRate, minimumHz);
        if (!std::isfinite(actual) || !cached.healthy()) {
            std::cerr << label << " SVF became non-finite during a smooth sweep\n";
            return false;
        }
        const float error = actual - expected;
        errorEnergy += static_cast<double>(error) * error;
        referenceEnergy += static_cast<double>(expected) * expected;
        maximumStepDelta = std::max(maximumStepDelta,
            std::fabs((actual - previousCached)
                - (expected - previousReference)));
        maximumOutput = std::max(maximumOutput, std::fabs(actual));
        previousCached = actual;
        previousReference = expected;
    }
    const float normalizedError = static_cast<float>(
        std::sqrt(errorEnergy / std::max(1.0e-20, referenceEnergy)));
    if (!(normalizedError < 0.025f) || !(maximumStepDelta < 0.015f)
        || !(maximumOutput < 2.0f)) {
        std::cerr << label << " cached/full-rate SVF drift: normalized error="
                  << normalizedError << ", step delta=" << maximumStepDelta
                  << ", peak=" << maximumOutput << "\n";
        return false;
    }

    cached.reset();
    previousCached = 0.0f;
    float maximumAutomationStep = 0.0f;
    for (uint32_t frame = 0u; frame < 8192u; ++frame) {
        const float cutoff = ((frame / 257u) & 1u) ? 15000.0f : 90.0f;
        const float resonance = ((frame / 383u) & 1u) ? 0.92f : 0.05f;
        const float input = std::sin(static_cast<float>(frame)
            * 2.0f * s3g::kPi * 211.0f / sampleRate) * 0.20f;
        const float output = cached.process(input, cutoff, resonance, sampleRate);
        if (!std::isfinite(output) || !cached.healthy()) {
            std::cerr << label << " SVF became non-finite under abrupt automation\n";
            return false;
        }
        maximumAutomationStep = std::max(
            maximumAutomationStep, std::fabs(output - previousCached));
        previousCached = output;
    }
    if (!(maximumAutomationStep < 1.0f)) {
        std::cerr << label << " SVF automation produced an unsafe step: "
                  << maximumAutomationStep << "\n";
        return false;
    }

    cached.ic1eq = std::numeric_limits<float>::infinity();
    if (cached.process(0.2f, 1000.0f, 0.5f, sampleRate) != 0.0f
        || !cached.healthy()) {
        std::cerr << label << " SVF did not recover from corrupt state\n";
        return false;
    }
    std::cout << label << " SVF normalized error/step: "
              << normalizedError << " / " << maximumStepDelta << "\n";
    return true;
}

} // namespace

int main()
{
    return runProbe<s3g::AmbiWaterSvf>("Water", 8.0f)
            && runProbe<s3g::AmbiWindSvf>("Wind", 6.0f)
        ? 0
        : 1;
}
