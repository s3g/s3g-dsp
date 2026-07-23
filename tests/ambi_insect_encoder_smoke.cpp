#include "s3g_ambi_insect_encoder.h"
#include "s3g_ambi_insect_presets.h"

#include <algorithm>
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
        if (params.order != 3u || params.outputGainDb != -6.0f
            || params.fieldListenMode != s3g::AmbiFieldListenMode::Off) {
            std::cerr << "Insect preset " << preset
                      << " did not default to 3OA / -6 dB / listener Off\n";
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

bool testFieldListenerModes()
{
    struct Capture {
        Metrics metrics {};
        std::array<float, s3g::kAmbiFieldListenerMaxLobes> envelope {};
        std::array<float, s3g::kAmbiFieldListenerMaxLobes> weight {};
    };

    auto params = s3g::ambiInsectFactoryPreset(4u);
    params.order = 3u;
    params.voices = 10u;
    params.activity = 1.0f;
    params.callLength = 0.94f;
    params.rest = 0.05f;
    params.roam = 0.08f;
    params.cohesion = 0.92f;
    params.scatter = 0.10f;
    params.orbit = 0.04f;
    params.lift = 0.08f;
    params.nearPass = 0.0f;
    params.spatialFollow = 0.96f;
    params.centerAzimuthDeg = 34.0f;
    params.centerElevationDeg = 18.0f;
    params.space = 0.18f;

    auto capture = [&](s3g::AmbiFieldListenMode mode) {
        s3g::AmbiInsectEncoder engine;
        Buffer buffer {};
        params.fieldListenMode = mode;
        engine.prepare(48000.0);
        engine.setParams(params);
        engine.reset();
        Capture result;
        for (uint32_t block = 0u; block < 1200u; ++block) {
            const auto current = processBlock(engine, buffer, 16u);
            result.metrics.energy += current.energy;
            result.metrics.absolute += current.absolute;
            result.metrics.peak =
                std::max(result.metrics.peak, current.peak);
            result.metrics.finite =
                result.metrics.finite && current.finite;
        }
        for (uint32_t lobe = 0u;
            lobe < s3g::kAmbiFieldListenerMaxLobes; ++lobe) {
            result.envelope[lobe] = engine.fieldListenEnvelope(lobe);
            result.weight[lobe] = engine.fieldListenWeight(lobe);
        }
        return result;
    };

    const auto off = capture(s3g::AmbiFieldListenMode::Off);
    const auto follow = capture(s3g::AmbiFieldListenMode::Follow);
    const auto counter = capture(s3g::AmbiFieldListenMode::Counter);
    const auto balance = capture(s3g::AmbiFieldListenMode::Balance);
    for (float weight : off.weight) {
        if (std::fabs(weight - 1.0f) > 0.00001f) {
            std::cerr << "Insect field listener Off changed a lobe weight\n";
            return false;
        }
    }
    auto spread = [](const auto& values) {
        const auto limits = std::minmax_element(values.begin(), values.end());
        return *limits.second - *limits.first;
    };
    if (!(spread(follow.envelope) > 0.001f
          && spread(follow.weight) > 0.01f
          && spread(counter.weight) > 0.01f
          && spread(balance.weight) > 0.01f)) {
        std::cerr << "Insect field listener did not develop directional responses\n";
        return false;
    }
    const double scale = std::max({
        1.0, off.metrics.energy, follow.metrics.energy,
        counter.metrics.energy, balance.metrics.energy
    });
    if (!(std::fabs(follow.metrics.energy - off.metrics.energy) / scale > 0.0005
          && std::fabs(counter.metrics.energy - follow.metrics.energy) / scale > 0.0005
          && std::fabs(balance.metrics.energy - follow.metrics.energy) / scale > 0.0005)) {
        std::cerr << "Insect listener modes did not produce distinct audible renders\n";
        return false;
    }
    if (!off.metrics.finite || !follow.metrics.finite
        || !counter.metrics.finite || !balance.metrics.finite
        || std::max({ off.metrics.peak, follow.metrics.peak,
               counter.metrics.peak, balance.metrics.peak }) > 1.001f) {
        std::cerr << "Insect listener modes escaped bounded output\n";
        return false;
    }

    s3g::AmbiInsectEncoder sanitizer;
    sanitizer.prepare(48000.0);
    params.fieldListenMode = static_cast<s3g::AmbiFieldListenMode>(99u);
    sanitizer.setParams(params);
    if (sanitizer.params().fieldListenMode
        != s3g::AmbiFieldListenMode::Balance) {
        std::cerr << "Insect field listener mode was not sanitized\n";
        return false;
    }
    return true;
}

bool testCallTypesAndTremulation()
{
    auto calling = s3g::ambiInsectFactoryPreset(0u);
    calling.order = 1u;
    calling.voices = 10u;
    calling.activity = 1.0f;
    calling.phraseRateHz = 1.2f;
    calling.chirpRateHz = 7.0f;
    calling.callLength = 0.58f;
    calling.rest = 0.18f;
    calling.space = 0.0f;
    calling.callType = 0u;
    auto defensive = calling;
    defensive.callType = 9u;
    const auto callingMetrics = render(calling, 100u, 4u);
    const auto defensiveMetrics = render(defensive, 100u, 4u);
    const double roleDifference = std::fabs(
        callingMetrics.energy - defensiveMetrics.energy)
        + std::fabs(callingMetrics.differenceEnergy
            - defensiveMetrics.differenceEnergy);
    if (!callingMetrics.finite || !defensiveMetrics.finite
        || !(roleDifference > 0.01)) {
        std::cerr << "Insect call types did not produce distinct phrasing\n";
        return false;
    }

    for (uint32_t callType = 0u;
         callType < s3g::kAmbiInsectCallTypeCount; ++callType) {
        auto params = calling;
        params.callType = callType;
        const auto metrics = render(params, 36u, 4u);
        if (!metrics.finite || !(metrics.absolute > 1.0e-7)
            || metrics.peak > 1.001f) {
            std::cerr << "Insect call type " << callType
                      << " was silent or invalid\n";
            return false;
        }
    }

    auto tremulation = s3g::ambiInsectFactoryPreset(16u);
    const auto tremulationMetrics = render(tremulation, 180u, 16u);
    if (!tremulationMetrics.finite
        || !(tremulationMetrics.absolute > 0.01)
        || tremulationMetrics.peak > 1.001f) {
        std::cerr << "Tremulation model was silent or invalid\n";
        return false;
    }
    return true;
}

bool testCinematicRandomizer()
{
    for (uint32_t regime = 0u;
         regime < s3g::kAmbiInsectRegimeCount; ++regime) {
        constexpr uint32_t kSamples = 96u;
        double pitchSum = 0.0;
        double sizeSum = 0.0;
        double pitchSizeSum = 0.0;
        double temperatureSum = 0.0;
        double pulseSum = 0.0;
        double temperaturePulseSum = 0.0;
        s3g::AmbiInsectParams audition {};
        for (uint32_t sample = 0u; sample < kSamples; ++sample) {
            uint32_t seed = 0x51f15e5du
                ^ (regime * 0x9e3779b9u)
                ^ (sample * 0x85ebca6bu);
            const uint32_t initialSeed = seed;
            const auto params =
                s3g::ambiInsectCinematicRandomParamsForRegime(
                    seed, regime);
            uint32_t repeatSeed = initialSeed;
            const auto repeated =
                s3g::ambiInsectCinematicRandomParamsForRegime(
                    repeatSeed, regime);
            if (params.regime != regime
                || params.order != 3u
                || params.outputGainDb != -6.0f
                || params.sceneSeed == 0u
                || params.callType >= s3g::kAmbiInsectCallTypeCount
                || params.voices < 1u
                || params.voices > s3g::kAmbiInsectMaxVoices
                || !std::isfinite(params.bodyPitchHz)
                || !std::isfinite(params.phraseRateHz)
                || !std::isfinite(params.chirpRateHz)
                || !std::isfinite(params.pulseRateHz)
                || params.sceneSeed != repeated.sceneSeed
                || params.voices != repeated.voices
                || params.callType != repeated.callType
                || params.bodyPitchHz != repeated.bodyPitchHz
                || params.temperature != repeated.temperature
                || seed != repeatSeed) {
                std::cerr << "Cinematic randomizer produced an invalid or "
                             "non-deterministic profile for regime "
                          << regime << "\n";
                return false;
            }
            if (regime == 3u
                && std::fabs(params.pulseRateHz - params.bodyPitchHz)
                    > 0.001f) {
                std::cerr << "Flyer randomizer separated wing rate from "
                             "body pitch\n";
                return false;
            }
            pitchSum += params.bodyPitchHz;
            sizeSum += params.bodySize;
            pitchSizeSum += params.bodyPitchHz * params.bodySize;
            temperatureSum += params.temperature;
            pulseSum += params.pulseRateHz;
            temperaturePulseSum +=
                params.temperature * params.pulseRateHz;
            if (sample == 0u) audition = params;
        }

        const double morphologyCovariance =
            static_cast<double>(kSamples) * pitchSizeSum
            - pitchSum * sizeSum;
        if (!(morphologyCovariance < 0.0)) {
            std::cerr << "Cinematic morphology lost its inverse "
                         "pitch/body-size relationship for regime "
                      << regime << "\n";
            return false;
        }
        if (regime != 3u) {
            const double temperatureCovariance =
                static_cast<double>(kSamples) * temperaturePulseSum
                - temperatureSum * pulseSum;
            if (!(temperatureCovariance > 0.0)) {
                std::cerr << "Cinematic temperature stopped driving "
                             "articulation for regime "
                          << regime << "\n";
                return false;
            }
        }

        const auto metrics = render(audition, 240u, 16u);
        if (!metrics.finite || !(metrics.peak > 1.0e-8f)
            || metrics.peak > 1.001f) {
            std::cerr << "Cinematic random profile was silent or invalid "
                         "for regime "
                      << regime << "\n";
            return false;
        }
    }
    return true;
}

bool testSeededColonies()
{
    auto params = s3g::ambiInsectFactoryPreset(15u);
    params.voices = 64u;
    params.sceneSeed = 0x416d6269u;

    s3g::AmbiInsectEncoder first;
    first.prepare(48000.0);
    first.setParams(params);
    first.reset();
    if (first.colonyCount() != 4u) {
        std::cerr << "Dense mixed population did not form four colonies\n";
        return false;
    }

    std::array<uint32_t, s3g::kAmbiInsectMaxColonies> counts {};
    std::array<bool, s3g::kAmbiInsectProductionMethodCount> methods {};
    for (uint32_t voice = 0u; voice < params.voices; ++voice) {
        const uint32_t colony = first.voiceColony(voice);
        const uint32_t method = first.voiceProductionMethod(voice);
        if (colony >= first.colonyCount()
            || method >= s3g::kAmbiInsectProductionMethodCount) {
            std::cerr << "Mixed population produced an invalid colony "
                         "assignment\n";
            return false;
        }
        ++counts[colony];
        methods[method] = true;
    }
    const uint32_t distinctMethods = static_cast<uint32_t>(
        std::count(methods.begin(), methods.end(), true));
    if (distinctMethods < 3u
        || counts[0] <= counts[1]
        || counts[0] <= counts[2]
        || counts[0] <= counts[3]) {
        std::cerr << "Mixed population lost its dominant colony and "
                     "satellite structure\n";
        return false;
    }

    s3g::AmbiInsectEncoder repeated;
    repeated.prepare(48000.0);
    repeated.setParams(params);
    repeated.reset();
    for (uint32_t voice = 0u; voice < params.voices; ++voice) {
        const auto firstPoint = first.voicePoint(voice);
        const auto repeatedPoint = repeated.voicePoint(voice);
        if (first.voiceColony(voice) != repeated.voiceColony(voice)
            || first.voiceProductionMethod(voice)
                != repeated.voiceProductionMethod(voice)
            || firstPoint.azimuthDeg != repeatedPoint.azimuthDeg
            || firstPoint.elevationDeg != repeatedPoint.elevationDeg
            || firstPoint.distance != repeatedPoint.distance) {
            std::cerr << "Scene seed did not reproduce the same colonies\n";
            return false;
        }
    }

    auto changedParams = params;
    changedParams.sceneSeed ^= 0x9e3779b9u;
    s3g::AmbiInsectEncoder changed;
    changed.prepare(48000.0);
    changed.setParams(changedParams);
    changed.reset();
    bool sceneChanged = false;
    for (uint32_t voice = 0u; voice < params.voices; ++voice) {
        const auto a = first.voicePoint(voice);
        const auto b = changed.voicePoint(voice);
        sceneChanged = sceneChanged
            || first.voiceProductionMethod(voice)
                != changed.voiceProductionMethod(voice)
            || std::fabs(a.azimuthDeg - b.azimuthDeg) > 0.001f
            || std::fabs(a.elevationDeg - b.elevationDeg) > 0.001f
            || std::fabs(a.distance - b.distance) > 0.0001f;
    }
    if (!sceneChanged) {
        std::cerr << "Changing the scene seed did not change the population\n";
        return false;
    }

    auto dedicatedParams = s3g::ambiInsectFactoryPreset(0u);
    dedicatedParams.voices = 64u;
    s3g::AmbiInsectEncoder dedicated;
    dedicated.prepare(48000.0);
    dedicated.setParams(dedicatedParams);
    dedicated.reset();
    if (dedicated.colonyCount() < 2u) {
        std::cerr << "Dense dedicated population did not split into colonies\n";
        return false;
    }
    for (uint32_t voice = 0u; voice < dedicatedParams.voices; ++voice) {
        if (dedicated.voiceProductionMethod(voice) != 0u) {
            std::cerr << "Dedicated colonies changed production method\n";
            return false;
        }
    }
    return true;
}

bool testTemperatureHierarchy()
{
    for (uint32_t method = 0u;
         method < s3g::kAmbiInsectProductionMethodCount; ++method) {
        const auto response =
            s3g::ambiInsectTemperatureResponse(method);
        if (!(response.pulse >= response.chirp)
            || !(response.chirp >= response.phrase)
            || !(s3g::ambiInsectTemperatureScale(1.0f, response.pulse)
                > s3g::ambiInsectTemperatureScale(
                    0.0f, response.pulse))) {
            std::cerr << "Temperature hierarchy is invalid for production "
                         "method "
                      << method << "\n";
            return false;
        }
    }

    auto lowParams = s3g::ambiInsectFactoryPreset(0u);
    lowParams.order = 1u;
    lowParams.voices = 1u;
    lowParams.variation = 0.0f;
    lowParams.scatter = 0.0f;
    lowParams.bodySize = 0.5f;
    lowParams.temperature = 0.0f;
    auto highParams = lowParams;
    highParams.temperature = 1.0f;

    const auto capturePitch = [](s3g::AmbiInsectParams params) {
        s3g::AmbiInsectEncoder engine;
        Buffer buffer {};
        engine.prepare(48000.0);
        engine.setParams(params);
        engine.reset();
        processBlock(engine, buffer, 4u);
        return engine.voicePitchHz(0u);
    };
    const float lowPitch = capturePitch(lowParams);
    const float highPitch = capturePitch(highParams);
    if (!(highPitch > lowPitch * 1.20f)) {
        std::cerr << "Temperature did not produce the expected pitch "
                     "response: "
                  << lowPitch << " -> " << highPitch << "\n";
        return false;
    }
    return true;
}

bool testCicadaEntrainment()
{
    auto params = s3g::ambiInsectFactoryPreset(3u);
    params.order = 1u;
    params.voices = 48u;
    params.activity = 0.92f;
    params.temperature = 0.82f;
    params.variation = 0.48f;
    params.phraseRateHz = 8.0f;
    params.space = 0.0f;
    params.sceneSeed = 0x43494341u;

    s3g::AmbiInsectEncoder engine;
    Buffer buffer {};
    engine.prepare(48000.0);
    engine.setParams(params);
    engine.reset();
    std::array<uint32_t, s3g::kAmbiInsectMaxVoices> initialModes {};
    std::array<bool, 4> modesSeen {};
    for (uint32_t voice = 0u; voice < params.voices; ++voice) {
        initialModes[voice] =
            engine.voiceCicadaEntrainmentMode(voice);
        const float scale =
            engine.voiceCicadaEntrainmentScale(voice);
        if (initialModes[voice] >= modesSeen.size()
            || !std::isfinite(scale)
            || scale < 0.49f
            || scale > 2.01f) {
            std::cerr << "Cicada initialized with an invalid entrainment "
                         "state\n";
            return false;
        }
        modesSeen[initialModes[voice]] = true;
    }
    if (std::count(modesSeen.begin(), modesSeen.end(), true) < 2) {
        std::cerr << "Cicada population did not diversify its pulse states\n";
        return false;
    }

    bool modeChanged = false;
    Metrics total;
    for (uint32_t block = 0u; block < 180u; ++block) {
        const auto current = processBlock(engine, buffer, 4u);
        total.finite = total.finite && current.finite;
        total.peak = std::max(total.peak, current.peak);
        total.absolute += current.absolute;
        for (uint32_t voice = 0u; voice < params.voices; ++voice) {
            const uint32_t mode =
                engine.voiceCicadaEntrainmentMode(voice);
            const float scale =
                engine.voiceCicadaEntrainmentScale(voice);
            modeChanged = modeChanged || mode != initialModes[voice];
            if (mode >= modesSeen.size()
                || !std::isfinite(scale)
                || scale < 0.49f
                || scale > 2.01f) {
                std::cerr << "Cicada entrainment became invalid while "
                             "running\n";
                return false;
            }
            modesSeen[mode] = true;
        }
    }
    if (!total.finite || !(total.absolute > 0.01)
        || total.peak > 1.001f || !modeChanged) {
        std::cerr << "Cicada actuation did not evolve safely over time\n";
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

bool testFlyerPitchDistribution()
{
    auto params = s3g::ambiInsectFactoryPreset(13u);
    params.order = 1u;
    params.voices = 64u;
    params.variation = 0.0f;
    params.scatter = 0.0f;
    params.phraseRateHz = 0.01f;
    params.rest = 0.0f;

    const auto capturePitches = [&params](float coupling) {
        s3g::AmbiInsectEncoder engine;
        Buffer buffer {};
        auto current = params;
        current.coupling = coupling;
        engine.prepare(48000.0);
        engine.setParams(current);
        engine.reset();
        processBlock(engine, buffer, 4u);
        std::array<float, s3g::kAmbiInsectMaxVoices> pitches {};
        for (uint32_t voice = 0u; voice < current.voices; ++voice) {
            pitches[voice] = engine.voicePitchHz(voice);
        }
        return pitches;
    };

    const auto uncoupled = capturePitches(0.0f);
    const auto coupled = capturePitches(1.0f);
    for (uint32_t voice = 0u; voice < params.voices; ++voice) {
        if (std::fabs(uncoupled[voice] - coupled[voice]) > 0.001f) {
            std::cerr << "Flyer coupling altered voice pitch " << voice << "\n";
            return false;
        }
    }

    auto sorted = coupled;
    std::sort(sorted.begin(), sorted.begin() + params.voices);
    const float spanCents = 1200.0f * std::log2(
        sorted[params.voices - 1u] / sorted[0u]);
    float minimumSpacingCents = std::numeric_limits<float>::max();
    for (uint32_t voice = 1u; voice < params.voices; ++voice) {
        minimumSpacingCents = std::min(minimumSpacingCents,
            1200.0f * std::log2(sorted[voice] / sorted[voice - 1u]));
    }
    std::cout << "  flyer pitch span: " << spanCents
              << " cents, minimum spacing: " << minimumSpacingCents
              << " cents\n";
    if (!(spanCents > 450.0f) || !(minimumSpacingCents > 3.0f)) {
        std::cerr << "Dense flyer population collapsed toward shared pitches\n";
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
    if (!testFieldListenerModes()) return 1;
    if (!testCallTypesAndTremulation()) return 1;
    if (!testCinematicRandomizer()) return 1;
    if (!testSeededColonies()) return 1;
    if (!testTemperatureHierarchy()) return 1;
    if (!testCicadaEntrainment()) return 1;
    if (!testWingLayer()) return 1;
    if (!testFlyerPitchDistribution()) return 1;
    if (!testDensePopulation()) return 1;
    if (!testMotionAndTransition()) return 1;
    if (!testStressAndRecovery()) return 1;
    std::cout << "Ambi Insect Encoder smoke tests passed\n";
    return 0;
}
