#include "s3g_ambi_stochastic_encoder.h"
#include "s3g_ambi_wave_terrain_encoder.h"
#include "s3g_ambi_water_encoder.h"
#include "s3g_ambi_wind_encoder.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>

namespace {

constexpr uint32_t kFrames = 256u;
constexpr uint32_t kChannels = 16u;
using Buffer = std::array<std::array<float, kFrames>, kChannels>;

std::array<float*, kChannels> pointers(Buffer& buffer)
{
    std::array<float*, kChannels> result {};
    for (uint32_t channel = 0u; channel < kChannels; ++channel) {
        result[channel] = buffer[channel].data();
    }
    return result;
}

bool finite(const Buffer& buffer)
{
    for (const auto& channel : buffer) {
        for (float sample : channel) {
            if (!std::isfinite(sample)) return false;
        }
    }
    return true;
}

bool testWaveTerrainListener()
{
    auto off = std::make_unique<s3g::AmbiWaveTerrainEncoder>();
    auto follow = std::make_unique<s3g::AmbiWaveTerrainEncoder>();
    auto counter = std::make_unique<s3g::AmbiWaveTerrainEncoder>();
    off->prepare(48000.0);
    follow->prepare(48000.0);
    counter->prepare(48000.0);

    s3g::AmbiWaveTerrainParams params {};
    params.order = 3u;
    params.voices = 8u;
    params.mode = s3g::AmbiWaveTerrainMode::Free;
    params.motionMode = s3g::AmbiWaveTerrainMotionMode::Field;
    params.selection = s3g::AmbiWaveTerrainSelection::Markov;
    params.fieldDensity = 0.68f;
    params.fieldDurationSeconds = 0.05f;
    params.fieldRestSeconds = 0.02f;
    params.fieldContrast = 0.78f;
    params.selectionMemory = 0.18f;
    params.regionDeviation = 0.82f;
    params.tableXfadeMs = 8.0f;
    params.spatialSpread = 1.0f;
    params.outputGainDb = -18.0f;

    auto offParams = params;
    offParams.fieldListenMode = s3g::AmbiFieldListenMode::Off;
    auto followParams = params;
    followParams.fieldListenMode = s3g::AmbiFieldListenMode::Follow;
    auto counterParams = params;
    counterParams.fieldListenMode = s3g::AmbiFieldListenMode::Counter;
    off->setParams(offParams);
    follow->setParams(followParams);
    counter->setParams(counterParams);
    off->reset();
    follow->reset();
    counter->reset();

    Buffer offBuffer {};
    Buffer followBuffer {};
    Buffer counterBuffer {};
    auto offOutputs = pointers(offBuffer);
    auto followOutputs = pointers(followBuffer);
    auto counterOutputs = pointers(counterBuffer);
    double followDifference = 0.0;
    double counterDifference = 0.0;
    float peak = 0.0f;
    for (uint32_t block = 0u; block < 260u; ++block) {
        off->process(offOutputs.data(), kChannels, kFrames);
        follow->process(followOutputs.data(), kChannels, kFrames);
        counter->process(counterOutputs.data(), kChannels, kFrames);
        if (!finite(offBuffer) || !finite(followBuffer) || !finite(counterBuffer)) {
            std::cerr << "Wave Terrain listener generated a non-finite sample\n";
            return false;
        }
        for (uint32_t channel = 0u; channel < kChannels; ++channel) {
            for (uint32_t frame = 0u; frame < kFrames; ++frame) {
                peak = std::max(peak, std::fabs(followBuffer[channel][frame]));
                if (block >= 70u) {
                    followDifference += std::fabs(
                        followBuffer[channel][frame] - offBuffer[channel][frame]);
                    counterDifference += std::fabs(
                        counterBuffer[channel][frame] - followBuffer[channel][frame]);
                }
            }
        }
    }

    double regionDifference = 0.0;
    for (uint32_t voice = 0u; voice < params.voices; ++voice) {
        const auto followRegion = follow->voiceRegion(voice, true);
        const auto counterRegion = counter->voiceRegion(voice, true);
        regionDifference += std::fabs(followRegion.u - counterRegion.u)
            + std::fabs(followRegion.v - counterRegion.v)
            + std::fabs(followRegion.rotation - counterRegion.rotation);
    }
    if (!(peak > 1.0e-5f)
        || !(follow->fieldListenActivity() > 0.02f)
        || !(followDifference > 1.0)
        || !(counterDifference > 1.0)
        || !(regionDifference > 0.02)) {
        std::cerr << "Wave Terrain listener did not alter audible traversal: "
                  << peak << ", " << follow->fieldListenActivity() << ", "
                  << followDifference << ", " << counterDifference << ", "
                  << regionDifference << "\n";
        return false;
    }
    return true;
}

bool testStochasticListener()
{
    auto off = std::make_unique<s3g::AmbiStochasticEncoder>();
    auto local = std::make_unique<s3g::AmbiStochasticEncoder>();
    auto cross = std::make_unique<s3g::AmbiStochasticEncoder>();
    auto diffuse = std::make_unique<s3g::AmbiStochasticEncoder>();
    auto roaming = std::make_unique<s3g::AmbiStochasticEncoder>();
    off->prepare(48000.0);
    local->prepare(48000.0);
    cross->prepare(48000.0);
    diffuse->prepare(48000.0);
    roaming->prepare(48000.0);

    s3g::AmbiStochasticParams params {};
    params.order = 3u;
    params.voices = 10u;
    params.mode = s3g::AmbiStochasticMode::Free;
    params.selection = s3g::AmbiStochasticSelection::Markov;
    params.fieldDensity = 0.64f;
    params.fieldDurationSeconds = 0.05f;
    params.fieldRestSeconds = 0.02f;
    params.fieldContrast = 0.82f;
    params.selectionMemory = 0.16f;
    params.topologyRateHz = 0.18f;
    params.topologyDepth = 0.92f;
    params.outputGainDb = -12.0f;

    auto offParams = params;
    offParams.fieldListenMode = s3g::AmbiStochasticListenMode::Off;
    auto localParams = params;
    localParams.fieldListenMode = s3g::AmbiStochasticListenMode::Local;
    localParams.fieldListenAmount = 0.86f;
    auto crossParams = localParams;
    crossParams.fieldListenMode = s3g::AmbiStochasticListenMode::Cross;
    auto diffuseParams = localParams;
    diffuseParams.fieldListenMode = s3g::AmbiStochasticListenMode::Diffuse;
    auto roamingParams = localParams;
    roamingParams.fieldListenMode = s3g::AmbiStochasticListenMode::Roaming;
    off->setParams(offParams);
    local->setParams(localParams);
    cross->setParams(crossParams);
    diffuse->setParams(diffuseParams);
    roaming->setParams(roamingParams);
    off->reset();
    local->reset();
    cross->reset();
    diffuse->reset();
    roaming->reset();

    Buffer offBuffer {};
    Buffer localBuffer {};
    Buffer crossBuffer {};
    Buffer diffuseBuffer {};
    Buffer roamingBuffer {};
    auto offOutputs = pointers(offBuffer);
    auto localOutputs = pointers(localBuffer);
    auto crossOutputs = pointers(crossBuffer);
    auto diffuseOutputs = pointers(diffuseBuffer);
    auto roamingOutputs = pointers(roamingBuffer);
    double localDifference = 0.0;
    double crossDifference = 0.0;
    double diffuseDifference = 0.0;
    double roamingDifference = 0.0;
    uint32_t offFieldTransitions = 0u;
    uint32_t localFieldTransitions = 0u;
    uint32_t offGeneratorChanges = 0u;
    uint32_t localGeneratorChanges = 0u;
    double offFrequencyTravel = 0.0;
    double localFrequencyTravel = 0.0;
    float maximumCapture = 0.0f;
    std::array<bool, s3g::kAmbiStochasticMaxVoices> priorOffActive {};
    std::array<bool, s3g::kAmbiStochasticMaxVoices> priorLocalActive {};
    std::array<uint32_t, s3g::kAmbiStochasticMaxVoices> priorOffGenerator {};
    std::array<uint32_t, s3g::kAmbiStochasticMaxVoices> priorLocalGenerator {};
    std::array<float, s3g::kAmbiStochasticMaxVoices> priorOffFrequency {};
    std::array<float, s3g::kAmbiStochasticMaxVoices> priorLocalFrequency {};
    for (uint32_t voice = 0u; voice < params.voices; ++voice) {
        priorOffActive[voice] = off->voiceFieldActive(voice);
        priorLocalActive[voice] = local->voiceFieldActive(voice);
        priorOffGenerator[voice] = off->nextGenerator(voice);
        priorLocalGenerator[voice] = local->nextGenerator(voice);
        priorOffFrequency[voice] = off->voiceFrequency(voice);
        priorLocalFrequency[voice] = local->voiceFrequency(voice);
    }
    float peak = 0.0f;
    for (uint32_t block = 0u; block < 320u; ++block) {
        off->process(offOutputs.data(), kChannels, kFrames);
        local->process(localOutputs.data(), kChannels, kFrames);
        cross->process(crossOutputs.data(), kChannels, kFrames);
        diffuse->process(diffuseOutputs.data(), kChannels, kFrames);
        roaming->process(roamingOutputs.data(), kChannels, kFrames);
        if (!finite(offBuffer) || !finite(localBuffer) || !finite(crossBuffer)
            || !finite(diffuseBuffer) || !finite(roamingBuffer)) {
            std::cerr << "Stochastic listener generated a non-finite sample\n";
            return false;
        }
        for (uint32_t voice = 0u; voice < params.voices; ++voice) {
            const bool offActive = off->voiceFieldActive(voice);
            const bool localActive = local->voiceFieldActive(voice);
            const uint32_t offGenerator = off->nextGenerator(voice);
            const uint32_t localGenerator = local->nextGenerator(voice);
            const float offFrequency = off->voiceFrequency(voice);
            const float localFrequency = local->voiceFrequency(voice);
            offFieldTransitions += offActive != priorOffActive[voice];
            localFieldTransitions += localActive != priorLocalActive[voice];
            offGeneratorChanges += offGenerator != priorOffGenerator[voice];
            localGeneratorChanges += localGenerator != priorLocalGenerator[voice];
            offFrequencyTravel += std::fabs(offFrequency - priorOffFrequency[voice]);
            localFrequencyTravel += std::fabs(localFrequency - priorLocalFrequency[voice]);
            maximumCapture = std::max(maximumCapture,
                local->fieldListenVoiceTelemetry(voice).capture);
            priorOffActive[voice] = offActive;
            priorLocalActive[voice] = localActive;
            priorOffGenerator[voice] = offGenerator;
            priorLocalGenerator[voice] = localGenerator;
            priorOffFrequency[voice] = offFrequency;
            priorLocalFrequency[voice] = localFrequency;
        }
        for (uint32_t channel = 0u; channel < kChannels; ++channel) {
            for (uint32_t frame = 0u; frame < kFrames; ++frame) {
                peak = std::max(peak, std::fabs(localBuffer[channel][frame]));
                if (block >= 80u) {
                    localDifference += std::fabs(
                        localBuffer[channel][frame] - offBuffer[channel][frame]);
                    crossDifference += std::fabs(
                        crossBuffer[channel][frame] - localBuffer[channel][frame]);
                    diffuseDifference += std::fabs(
                        diffuseBuffer[channel][frame] - localBuffer[channel][frame]);
                    roamingDifference += std::fabs(
                        roamingBuffer[channel][frame] - localBuffer[channel][frame]);
                }
            }
        }
    }

    double spatialDifference = 0.0;
    uint32_t generatorDifference = 0u;
    uint32_t pickupDifference = 0u;
    for (uint32_t voice = 0u; voice < params.voices; ++voice) {
        const s3g::Vec3 a = off->topologyPosition(voice);
        const s3g::Vec3 b = local->topologyPosition(voice);
        const s3g::Vec3 c = cross->topologyPosition(voice);
        const auto& offPoint = off->points()[voice];
        const auto& localPoint = local->points()[voice];
        const auto& crossPoint = cross->points()[voice];
        spatialDifference += std::fabs(a.x - b.x) + std::fabs(a.y - b.y)
            + std::fabs(a.z - b.z) + std::fabs(b.x - c.x)
            + std::fabs(b.y - c.y) + std::fabs(b.z - c.z)
            + std::fabs(offPoint.azimuthDeg - localPoint.azimuthDeg) / 180.0
            + std::fabs(offPoint.elevationDeg - localPoint.elevationDeg) / 90.0
            + std::fabs(offPoint.distance - localPoint.distance)
            + std::fabs(localPoint.azimuthDeg - crossPoint.azimuthDeg) / 180.0
            + std::fabs(localPoint.elevationDeg - crossPoint.elevationDeg) / 90.0
            + std::fabs(localPoint.distance - crossPoint.distance);
        if (local->nextGenerator(voice) != cross->nextGenerator(voice)
            || local->nextGenerator(voice) != diffuse->nextGenerator(voice)
            || local->nextGenerator(voice) != roaming->nextGenerator(voice)) {
            ++generatorDifference;
        }
        if (local->fieldListenPickup(voice) != cross->fieldListenPickup(voice)) {
            ++pickupDifference;
        }
    }
    const auto localTelemetry = local->fieldListenVoiceTelemetry(0u);
    const auto roamingTelemetry = roaming->fieldListenVoiceTelemetry(0u);
    const auto offTelemetry = off->fieldListenVoiceTelemetry(0u);
    const bool telemetryValid =
        localTelemetry.pickup < s3g::kAmbiFieldListenerMaxLobes
        && localTelemetry.secondaryPickup < s3g::kAmbiFieldListenerMaxLobes
        && localTelemetry.response >= 0.0f && localTelemetry.response <= 1.0f
        && localTelemetry.energy >= 0.0f && localTelemetry.energy <= 1.0f
        && localTelemetry.signal >= -1.0f && localTelemetry.signal <= 1.0f
        && localTelemetry.capture >= 0.0f && localTelemetry.capture <= 1.0f
        && localTelemetry.mutationRate >= 0.04f && localTelemetry.mutationRate <= 1.0f
        && localTelemetry.evolutionRate >= 0.03f && localTelemetry.evolutionRate <= 1.0f
        && localTelemetry.fieldClockRate >= 0.15f && localTelemetry.fieldClockRate <= 1.0f
        && localTelemetry.cascadeRate >= 0.02f && localTelemetry.cascadeRate <= 1.0f
        && roamingTelemetry.pickup < s3g::kAmbiFieldListenerMaxLobes
        && roamingTelemetry.secondaryPickup < s3g::kAmbiFieldListenerMaxLobes
        && roamingTelemetry.pickup != roamingTelemetry.secondaryPickup
        && roamingTelemetry.pickupMix >= 0.0f && roamingTelemetry.pickupMix <= 1.0f
        && offTelemetry.capture == 0.0f
        && offTelemetry.mutationRate == 1.0f
        && offTelemetry.evolutionRate == 1.0f
        && offTelemetry.fieldClockRate == 1.0f
        && offTelemetry.cascadeRate == 1.0f;
    if (!(peak > 1.0e-5f)
        || !(local->fieldListenActivity() > 0.02f)
        || !(localDifference > 1.0)
        || !(crossDifference > 1.0)
        || !(diffuseDifference > 1.0)
        || !(roamingDifference > 1.0)
        || !(spatialDifference < 1.0e-5)
        || generatorDifference == 0u
        || pickupDifference == 0u
        || !(maximumCapture > 0.20f)
        || !(localFieldTransitions * 2u < offFieldTransitions)
        || !(localGeneratorChanges * 2u < offGeneratorChanges)
        || !(localFrequencyTravel * 2.0 < offFrequencyTravel)
        || !telemetryValid) {
        std::cerr << "Stochastic local listener scoring failed: "
                  << peak << ", " << local->fieldListenActivity() << ", "
                  << localDifference << ", " << crossDifference << ", "
                  << diffuseDifference << ", " << roamingDifference << ", "
                  << spatialDifference << ", " << generatorDifference << ", "
                  << pickupDifference << ", capture " << maximumCapture
                  << ", fields " << offFieldTransitions << ">" << localFieldTransitions
                  << ", generators " << offGeneratorChanges << ">" << localGeneratorChanges
                  << ", frequency " << offFrequencyTravel << ">" << localFrequencyTravel
                  << ", telemetry " << telemetryValid << "\n";
        return false;
    }

    auto zeroResponse = std::make_unique<s3g::AmbiStochasticEncoder>();
    zeroResponse->prepare(48000.0);
    auto zeroParams = localParams;
    zeroParams.fieldListenAmount = 0.0f;
    off->setParams(offParams);
    zeroResponse->setParams(zeroParams);
    off->reset();
    zeroResponse->reset();
    auto zeroOutputs = pointers(localBuffer);
    double zeroResponseDifference = 0.0;
    for (uint32_t block = 0u; block < 96u; ++block) {
        off->process(offOutputs.data(), kChannels, kFrames);
        zeroResponse->process(zeroOutputs.data(), kChannels, kFrames);
        for (uint32_t channel = 0u; channel < kChannels; ++channel) {
            for (uint32_t frame = 0u; frame < kFrames; ++frame) {
                zeroResponseDifference += std::fabs(
                    offBuffer[channel][frame] - localBuffer[channel][frame]);
            }
        }
    }
    if (zeroResponseDifference > 1.0e-7) {
        std::cerr << "Stochastic zero listener response changed the open-loop render: "
                  << zeroResponseDifference << "\n";
        return false;
    }

    auto unsafe = params;
    unsafe.fieldListenMode = static_cast<s3g::AmbiStochasticListenMode>(99u);
    unsafe.fieldListenAmount = -3.0f;
    off->setParams(unsafe);
    const auto safe = off->params();
    if (safe.fieldListenMode != s3g::AmbiStochasticListenMode::Roaming
        || safe.fieldListenAmount != 0.0f) {
        std::cerr << "Stochastic listener parameters were not sanitized\n";
        return false;
    }
    return true;
}

bool testWaterListener()
{
    auto off = std::make_unique<s3g::AmbiWaterEncoder>();
    auto follow = std::make_unique<s3g::AmbiWaterEncoder>();
    auto counter = std::make_unique<s3g::AmbiWaterEncoder>();
    off->prepare(48000.0);
    follow->prepare(48000.0);
    counter->prepare(48000.0);

    s3g::AmbiWaterParams params {};
    params.order = 3u;
    params.voices = 10u;
    params.regime = 4u;
    params.environment = 8u;
    params.water = 0.62f;
    params.flow = 0.64f;
    params.turbulence = 0.30f;
    params.aeration = 0.42f;
    params.drops = 0.40f;
    params.splash = 0.46f;
    params.bubbles = 0.54f;
    params.density = 0.48f;
    params.motionRateHz = 0.34f;
    params.current = 0.78f;
    params.eddy = 0.68f;
    params.convergence = 0.72f;
    params.spatialFollow = 0.24f;
    params.place = 2u;
    params.space = 0.76f;
    params.environmentSize = 0.64f;
    params.environmentDecay = 0.72f;
    params.outputGainDb = -14.0f;

    auto offParams = params;
    offParams.fieldListenMode = s3g::AmbiFieldListenMode::Off;
    auto followParams = params;
    followParams.fieldListenMode = s3g::AmbiFieldListenMode::Follow;
    auto counterParams = params;
    counterParams.fieldListenMode = s3g::AmbiFieldListenMode::Counter;
    off->setParams(offParams);
    follow->setParams(followParams);
    counter->setParams(counterParams);
    off->reset();
    follow->reset();
    counter->reset();

    Buffer offBuffer {};
    Buffer followBuffer {};
    Buffer counterBuffer {};
    auto offOutputs = pointers(offBuffer);
    auto followOutputs = pointers(followBuffer);
    auto counterOutputs = pointers(counterBuffer);
    double offDifference = 0.0;
    double modeDifference = 0.0;
    float peak = 0.0f;
    for (uint32_t block = 0u; block < 180u; ++block) {
        off->process(offOutputs.data(), kChannels, kFrames);
        follow->process(followOutputs.data(), kChannels, kFrames);
        counter->process(counterOutputs.data(), kChannels, kFrames);
        if (!finite(offBuffer) || !finite(followBuffer) || !finite(counterBuffer)) {
            std::cerr << "Water listener generated a non-finite sample\n";
            return false;
        }
        for (uint32_t channel = 0u; channel < kChannels; ++channel) {
            for (uint32_t frame = 0u; frame < kFrames; ++frame) {
                peak = std::max(peak, std::fabs(followBuffer[channel][frame]));
                if (block >= 35u) {
                    offDifference += std::fabs(
                        followBuffer[channel][frame] - offBuffer[channel][frame]);
                    modeDifference += std::fabs(
                        counterBuffer[channel][frame] - followBuffer[channel][frame]);
                }
            }
        }
    }

    double pointDifference = 0.0;
    double eventDifference = 0.0;
    for (uint32_t voice = 0u; voice < params.voices; ++voice) {
        const auto a = follow->voicePoint(voice);
        const auto b = counter->voicePoint(voice);
        pointDifference += std::fabs(a.azimuthDeg - b.azimuthDeg) / 180.0
            + std::fabs(a.elevationDeg - b.elevationDeg) / 90.0
            + std::fabs(a.distance - b.distance);
        eventDifference += std::fabs(
            follow->voiceEventLevel(voice) - counter->voiceEventLevel(voice));
    }
    if (!(peak > 1.0e-5f)
        || !(follow->fieldListenActivity() > 0.02f)
        || !(offDifference > 1.0)
        || !(modeDifference > 1.0)
        || !(pointDifference > 0.02)
        || !(eventDifference > 0.001)) {
        std::cerr << "Water listener did not alter current, eddies, and events: "
                  << peak << ", " << follow->fieldListenActivity() << ", "
                  << offDifference << ", " << modeDifference << ", "
                  << pointDifference << ", " << eventDifference << "\n";
        return false;
    }
    return true;
}

bool testWindListener()
{
    auto off = std::make_unique<s3g::AmbiWindEncoder>();
    auto follow = std::make_unique<s3g::AmbiWindEncoder>();
    auto balance = std::make_unique<s3g::AmbiWindEncoder>();
    off->prepare(48000.0);
    follow->prepare(48000.0);
    balance->prepare(48000.0);

    s3g::AmbiWindParams params {};
    params.order = 3u;
    params.voices = 10u;
    params.wind = 0.52f;
    params.gustRate = 0.54f;
    params.gustDepth = 0.62f;
    params.turbulence = 0.28f;
    params.flutter = 0.42f;
    params.field = 0.74f;
    params.motionRateHz = 0.20f;
    params.motionFlow = 0.78f;
    params.motionShear = 0.62f;
    params.motionCurl = 0.82f;
    params.motionUpdraft = 0.32f;
    params.spatialFollow = 0.24f;
    params.place = 5u;
    params.space = 0.76f;
    params.environmentSize = 0.66f;
    params.environmentDecay = 0.72f;
    params.outputGainDb = -14.0f;

    auto offParams = params;
    offParams.fieldListenMode = s3g::AmbiFieldListenMode::Off;
    auto followParams = params;
    followParams.fieldListenMode = s3g::AmbiFieldListenMode::Follow;
    auto balanceParams = params;
    balanceParams.fieldListenMode = s3g::AmbiFieldListenMode::Balance;
    off->setParams(offParams);
    follow->setParams(followParams);
    balance->setParams(balanceParams);
    off->reset();
    follow->reset();
    balance->reset();

    Buffer offBuffer {};
    Buffer followBuffer {};
    Buffer balanceBuffer {};
    auto offOutputs = pointers(offBuffer);
    auto followOutputs = pointers(followBuffer);
    auto balanceOutputs = pointers(balanceBuffer);
    double offDifference = 0.0;
    double modeDifference = 0.0;
    float peak = 0.0f;
    for (uint32_t block = 0u; block < 180u; ++block) {
        off->process(offOutputs.data(), kChannels, kFrames);
        follow->process(followOutputs.data(), kChannels, kFrames);
        balance->process(balanceOutputs.data(), kChannels, kFrames);
        if (!finite(offBuffer) || !finite(followBuffer) || !finite(balanceBuffer)) {
            std::cerr << "Wind listener generated a non-finite sample\n";
            return false;
        }
        for (uint32_t channel = 0u; channel < kChannels; ++channel) {
            for (uint32_t frame = 0u; frame < kFrames; ++frame) {
                peak = std::max(peak, std::fabs(followBuffer[channel][frame]));
                if (block >= 35u) {
                    offDifference += std::fabs(
                        followBuffer[channel][frame] - offBuffer[channel][frame]);
                    modeDifference += std::fabs(
                        balanceBuffer[channel][frame] - followBuffer[channel][frame]);
                }
            }
        }
    }

    double pointDifference = 0.0;
    double gustDifference = 0.0;
    for (uint32_t voice = 0u; voice < params.voices; ++voice) {
        const auto a = follow->voicePoint(voice);
        const auto b = balance->voicePoint(voice);
        pointDifference += std::fabs(a.azimuthDeg - b.azimuthDeg) / 180.0
            + std::fabs(a.elevationDeg - b.elevationDeg) / 90.0
            + std::fabs(a.distance - b.distance);
        gustDifference += std::fabs(
            follow->voiceGustLevel(voice) - balance->voiceGustLevel(voice));
    }
    if (!(peak > 1.0e-5f)
        || !(follow->fieldListenActivity() > 0.02f)
        || !(offDifference > 1.0)
        || !(modeDifference > 1.0)
        || !(pointDifference > 0.02)
        || !(gustDifference > 0.001)) {
        std::cerr << "Wind listener did not alter flow, gusts, and turbulence: "
                  << peak << ", " << follow->fieldListenActivity() << ", "
                  << offDifference << ", " << modeDifference << ", "
                  << pointDifference << ", " << gustDifference << "\n";
        return false;
    }
    return true;
}

} // namespace

int main()
{
    if (!testWaveTerrainListener()) return 1;
    if (!testStochasticListener()) return 1;
    if (!testWaterListener()) return 1;
    if (!testWindListener()) return 1;
    std::cout << "s3g ambisonic field-listener generator smoke test passed\n";
    return 0;
}
