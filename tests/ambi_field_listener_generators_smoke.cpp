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
    auto follow = std::make_unique<s3g::AmbiStochasticEncoder>();
    auto balance = std::make_unique<s3g::AmbiStochasticEncoder>();
    off->prepare(48000.0);
    follow->prepare(48000.0);
    balance->prepare(48000.0);

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
    double followDifference = 0.0;
    double balanceDifference = 0.0;
    float peak = 0.0f;
    for (uint32_t block = 0u; block < 280u; ++block) {
        off->process(offOutputs.data(), kChannels, kFrames);
        follow->process(followOutputs.data(), kChannels, kFrames);
        balance->process(balanceOutputs.data(), kChannels, kFrames);
        if (!finite(offBuffer) || !finite(followBuffer) || !finite(balanceBuffer)) {
            std::cerr << "Stochastic listener generated a non-finite sample\n";
            return false;
        }
        for (uint32_t channel = 0u; channel < kChannels; ++channel) {
            for (uint32_t frame = 0u; frame < kFrames; ++frame) {
                peak = std::max(peak, std::fabs(followBuffer[channel][frame]));
                if (block >= 80u) {
                    followDifference += std::fabs(
                        followBuffer[channel][frame] - offBuffer[channel][frame]);
                    balanceDifference += std::fabs(
                        balanceBuffer[channel][frame] - followBuffer[channel][frame]);
                }
            }
        }
    }

    double topologyDifference = 0.0;
    uint32_t generatorDifference = 0u;
    for (uint32_t voice = 0u; voice < params.voices; ++voice) {
        const s3g::Vec3 a = follow->topologyPosition(voice);
        const s3g::Vec3 b = balance->topologyPosition(voice);
        topologyDifference += std::fabs(a.x - b.x)
            + std::fabs(a.y - b.y) + std::fabs(a.z - b.z);
        if (follow->nextGenerator(voice) != balance->nextGenerator(voice)) {
            ++generatorDifference;
        }
    }
    if (!(peak > 1.0e-5f)
        || !(follow->fieldListenActivity() > 0.02f)
        || !(followDifference > 1.0)
        || !(balanceDifference > 1.0)
        || !(topologyDifference > 0.02)
        || generatorDifference == 0u) {
        std::cerr << "Stochastic listener did not alter audible field scoring: "
                  << peak << ", " << follow->fieldListenActivity() << ", "
                  << followDifference << ", " << balanceDifference << ", "
                  << topologyDifference << ", " << generatorDifference << "\n";
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
