#include "s3g_ambi_insect_encoder.h"
#include "s3g_ambi_insect_presets.h"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <utility>

namespace {

constexpr uint32_t kFrames = 128u;
using Buffer = std::array<std::array<float, kFrames>, s3g::kAmbiInsectMaxChannels>;

struct Metrics {
    double energy = 0.0;
    double differenceEnergy = 0.0;
    double absolute = 0.0;
    float peak = 0.0f;
    uint64_t nearLimitSamples = 0u;
    uint64_t discontinuitySamples = 0u;
    uint64_t samples = 0u;
    bool finite = true;
};

Metrics processBlock(s3g::AmbiInsectEncoder& engine, Buffer& buffer, uint32_t channels)
{
    std::array<float*, s3g::kAmbiInsectMaxChannels> outputs {};
    for (uint32_t channel = 0u; channel < channels; ++channel) outputs[channel] = buffer[channel].data();
    engine.process(outputs.data(), channels, kFrames);
    Metrics result;
    for (uint32_t channel = 0u; channel < channels; ++channel) {
        for (uint32_t frame = 0u; frame < kFrames; ++frame) {
            const float value = buffer[channel][frame];
            result.finite = result.finite && std::isfinite(value);
            result.energy += static_cast<double>(value) * value;
            if (frame > 0u) {
                const double difference = static_cast<double>(value)
                    - buffer[channel][frame - 1u];
                result.differenceEnergy += difference * difference;
            }
            result.absolute += std::fabs(value);
            result.peak = std::max(result.peak, std::fabs(value));
            result.nearLimitSamples += std::fabs(value) > 0.95f ? 1u : 0u;
            result.samples += 1u;
            if (frame > 0u && std::fabs(value - buffer[channel][frame - 1u]) > 0.50f) {
                result.discontinuitySamples += 1u;
            }
        }
    }
    return result;
}

Metrics render(s3g::AmbiInsectParams params, uint32_t blocks = 120u, uint32_t channels = 16u)
{
    s3g::AmbiInsectEncoder engine;
    Buffer buffer {};
    engine.prepare(48000.0);
    engine.setParams(params);
    engine.reset();
    Metrics total;
    for (uint32_t block = 0u; block < blocks; ++block) {
        const auto current = processBlock(engine, buffer, channels);
        total.energy += current.energy;
        total.differenceEnergy += current.differenceEnergy;
        total.absolute += current.absolute;
        total.peak = std::max(total.peak, current.peak);
        total.nearLimitSamples += current.nearLimitSamples;
        total.discontinuitySamples += current.discontinuitySamples;
        total.samples += current.samples;
        total.finite = total.finite && current.finite;
    }
    return total;
}

bool testFactoryPresets()
{
    constexpr uint32_t kFactoryBlocks = 480u;
    float quietestDb = 0.0f;
    float loudestDb = -120.0f;
    for (uint32_t preset = 0u; preset < s3g::kAmbiInsectFactoryPresetCount; ++preset) {
        const auto params = s3g::ambiInsectFactoryPreset(preset);
        if (params.order != 3u || params.outputGainDb != -6.0f) {
            std::cerr << "Insect preset " << preset << " did not default to 3OA / -6 dB\n";
            return false;
        }
        const auto metrics = render(params, kFactoryBlocks);
        if (!metrics.finite || !(metrics.peak > 1.0e-8f) || metrics.peak > 1.001f) {
            std::cerr << "Insect preset " << preset << " produced invalid peak " << metrics.peak << "\n";
            return false;
        }
        const float rms = std::sqrt(static_cast<float>(
            metrics.energy / (static_cast<double>(kFactoryBlocks) * kFrames * 16.0)));
        const float rmsDb = 20.0f * std::log10(std::max(1.0e-12f, rms));
        const double roughness = metrics.differenceEnergy
            / std::max(1.0e-12, metrics.energy);
        quietestDb = std::min(quietestDb, rmsDb);
        loudestDb = std::max(loudestDb, rmsDb);
        std::cout << "  " << s3g::ambiInsectFactoryPresetInfo(preset).name
                  << ": " << rmsDb << " dBFS RMS, peak " << metrics.peak
                  << ", roughness " << roughness << "\n";
        if ((preset == 2u || preset == 3u) && !(roughness > 0.28)) {
            std::cerr << "Cicada preset " << preset
                      << " collapsed into an overly periodic resonator\n";
            return false;
        }
        if (preset == 4u || preset == 5u || preset == 10u || preset == 13u) {
            const float effectiveWingbeat = params.bodyPitchHz
                * std::exp2((0.5f - params.bodySize) * 1.4f);
            const double pureToneDifference = 4.0 * std::pow(
                std::sin(3.14159265358979323846 * effectiveWingbeat / 48000.0f), 2.0);
            if (!(roughness > pureToneDifference * 1.80)
                || !(roughness < pureToneDifference * 8.0)) {
                std::cerr << "Flyer preset " << preset
                          << " lost its bounded harmonic/wake texture: "
                          << roughness << " versus pure-tone baseline "
                          << pureToneDifference << "\n";
                return false;
            }
        }
    }
    std::cout << "  factory RMS span: " << quietestDb << " to " << loudestDb << " dBFS\n";
    return true;
}

bool testOrderRouting()
{
    s3g::AmbiInsectEncoder engine;
    Buffer buffer {};
    auto params = s3g::ambiInsectFactoryPreset(0u);
    params.order = 2u;
    engine.prepare(48000.0);
    engine.setParams(params);
    engine.reset();
    processBlock(engine, buffer, 64u);
    for (uint32_t channel = 9u; channel < 64u; ++channel) {
        for (float value : buffer[channel]) {
            if (value != 0.0f) {
                std::cerr << "Insect encoder failed to clear channel " << channel << " above 2OA\n";
                return false;
            }
        }
    }
    return true;
}

bool testAudibleControls()
{
    auto base = s3g::ambiInsectFactoryPreset(0u);
    auto changed = base;
    changed.bodyPitchHz = 1800.0f;
    changed.chirpRateHz = 18.0f;
    changed.rasp = 0.86f;
    const auto a = render(base, 80u);
    const auto b = render(changed, 80u);
    const double sourceDifference = std::fabs(a.energy - b.energy) + std::fabs(a.absolute - b.absolute);
    if (!(sourceDifference > 0.01)) {
        std::cerr << "Insect call/body controls did not materially change the render\n";
        return false;
    }

    auto dry = s3g::ambiInsectFactoryPreset(7u);
    dry.space = 0.0f;
    auto wet = dry;
    wet.space = 0.90f;
    wet.environmentSize = 0.88f;
    wet.environmentDecay = 0.88f;
    const auto dryMetrics = render(dry, 160u);
    const auto wetMetrics = render(wet, 160u);
    if (!(std::fabs(dryMetrics.energy - wetMetrics.energy) > 0.001)) {
        std::cerr << "Insect environment field did not materially change the render\n";
        return false;
    }
    return true;
}

bool testWingLayer()
{
    auto bodyOnly = s3g::ambiInsectFactoryPreset(4u);
    bodyOnly.order = 1u;
    bodyOnly.voices = 1u;
    bodyOnly.activity = 1.0f;
    bodyOnly.variation = 0.0f;
    bodyOnly.coupling = 0.0f;
    bodyOnly.rest = 0.0f;
    bodyOnly.callLength = 1.0f;
    bodyOnly.rasp = 0.0f;
    bodyOnly.wing = 0.0f;
    bodyOnly.air = 0.0f;
    bodyOnly.space = 0.0f;
    auto winged = bodyOnly;
    winged.wing = 1.0f;

    // Roughly 170 ms: long enough for many wing cycles, but short enough to
    // assess the local flight-tone spectrum independently of phrase behavior.
    const auto bodyMetrics = render(bodyOnly, 64u, 4u);
    const auto wingMetrics = render(winged, 64u, 4u);
    const double energyRatio = wingMetrics.energy
        / std::max(1.0e-12, bodyMetrics.energy);
    const double differenceRatio = wingMetrics.differenceEnergy
        / std::max(1.0e-12, bodyMetrics.differenceEnergy);
    std::cout << "  wing-layer energy ratio: " << energyRatio
              << ", first-difference ratio: " << differenceRatio << "\n";
    if (!wingMetrics.finite || !(energyRatio > 1.20)
        || !(differenceRatio > 1.20)) {
        std::cerr << "WING did not add a distinct short-window flight-tone layer\n";
        return false;
    }
    return true;
}

bool testDensePopulation()
{
    auto params = s3g::ambiInsectFactoryPreset(13u);
    params.voices = 64u;
    params.activity = 1.0f;
    params.outputGainDb = -6.0f;
    const auto timedRender = [](s3g::AmbiInsectParams renderParams, uint32_t channels) {
        constexpr uint32_t kDenseBlocks = 480u;
        const auto started = std::chrono::steady_clock::now();
        const auto metrics = render(renderParams, kDenseBlocks, channels);
        const double elapsed = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - started).count();
        const double audioSeconds = static_cast<double>(
            kDenseBlocks * kFrames) / 48000.0;
        return std::pair<Metrics, double> { metrics, elapsed / audioSeconds };
    };
    const auto [metrics, realtimeLoad] = timedRender(params, 16u);
    auto seventhOrder = params;
    seventhOrder.order = 7u;
    const auto [wideMetrics, wideRealtimeLoad] = timedRender(seventhOrder, 64u);
    const double limitRatio = static_cast<double>(metrics.nearLimitSamples)
        / std::max<uint64_t>(1u, metrics.samples);
    const double discontinuityRatio = static_cast<double>(metrics.discontinuitySamples)
        / std::max<uint64_t>(1u, metrics.samples);
    std::cout << "  dense Flyer limiting: " << limitRatio * 100.0
              << "%, discontinuities: " << discontinuityRatio * 100.0
              << "%, peak " << metrics.peak
              << ", realtime load " << realtimeLoad * 100.0
              << "% (3OA), " << wideRealtimeLoad * 100.0 << "% (7OA)\n";
    if (!metrics.finite || !wideMetrics.finite || limitRatio > 0.0001
        || discontinuityRatio > 0.0001) {
        std::cerr << "Dense Flyer render clipped, jumped, or became non-finite\n";
        return false;
    }
#if defined(NDEBUG)
    if (realtimeLoad >= 0.85 || wideRealtimeLoad >= 0.95) {
        std::cerr << "Dense Flyer render exceeded its release realtime budget: "
                  << realtimeLoad << " / " << wideRealtimeLoad << "\n";
        return false;
    }
#endif
    return true;
}

bool testMotionAndTransition()
{
    s3g::AmbiInsectEncoder engine;
    Buffer buffer {};
    auto params = s3g::ambiInsectFactoryPreset(4u);
    params.fieldRateHz = 1.1f;
    params.roam = 1.0f;
    params.orbit = 1.0f;
    params.nearPass = 1.0f;
    params.spatialFollow = 0.0f;
    engine.prepare(48000.0);
    engine.setParams(params);
    engine.reset();
    const auto firstPoint = engine.voicePoint(0u);
    float last = 0.0f;
    for (uint32_t block = 0u; block < 100u; ++block) {
        processBlock(engine, buffer, 16u);
        last = buffer[0][kFrames - 1u];
    }
    const auto movedPoint = engine.voicePoint(0u);
    const float travel = std::fabs(movedPoint.azimuthDeg - firstPoint.azimuthDeg)
        + std::fabs(movedPoint.elevationDeg - firstPoint.elevationDeg)
        + std::fabs(movedPoint.distance - firstPoint.distance);
    if (!(travel > 0.1f) || !(engine.voiceCallLevel(0u) >= 0.0f)) {
        std::cerr << "Insect swarm points did not respond to motion controls\n";
        return false;
    }

    engine.setParams(s3g::ambiInsectFactoryPreset(2u));
    engine.beginTransition();
    processBlock(engine, buffer, 16u);
    if (!std::isfinite(buffer[0][0]) || std::fabs(buffer[0][0] - last) > 1.0e-6f) {
        std::cerr << "Insect preset transition was discontinuous: " << last << " -> " << buffer[0][0] << "\n";
        return false;
    }
    return true;
}

bool testStressAndRecovery()
{
    auto params = s3g::ambiInsectFactoryPreset(15u);
    params.order = 7u;
    params.voices = 64u;
    params.activity = 1.0f;
    params.rasp = 1.0f;
    params.wing = 1.0f;
    params.resonance = 1.0f;
    params.space = 1.0f;
    params.outputGainDb = 12.0f;
    auto stressed = render(params, 100u, 64u);
    if (!stressed.finite || stressed.peak > 1.001f) {
        std::cerr << "Insect stress render escaped its finite limiter: " << stressed.peak << "\n";
        return false;
    }

    params.phraseRateHz = std::numeric_limits<float>::quiet_NaN();
    params.bodyPitchHz = std::numeric_limits<float>::infinity();
    params.centerDistance = -std::numeric_limits<float>::infinity();
    params.outputGainDb = std::numeric_limits<float>::quiet_NaN();
    const auto recovered = render(params, 40u, 16u);
    if (!recovered.finite || recovered.peak > 1.001f) {
        std::cerr << "Insect encoder did not recover from invalid host values\n";
        return false;
    }
    return true;
}

} // namespace

int main()
{
    if (!testFactoryPresets()) return 1;
    if (!testOrderRouting()) return 1;
    if (!testAudibleControls()) return 1;
    if (!testWingLayer()) return 1;
    if (!testDensePopulation()) return 1;
    if (!testMotionAndTransition()) return 1;
    if (!testStressAndRecovery()) return 1;
    std::cout << "Ambi Insect Encoder smoke tests passed\n";
    return 0;
}
