#include "s3g_accelerometer_field_encoder.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

namespace {

constexpr uint32_t kSampleRate = 48000u;
constexpr uint32_t kFrames = kSampleRate * 2u;
constexpr float kLinkedOutputCeiling = 0.89125094f; // -1 dBFS

// Substrate is serialized as an integer in the CLAP state. Public material
// and resonant-form profiles must remain append-only so old sessions keep
// their original selections.
static_assert(static_cast<uint32_t>(
    s3g::AccelerometerSubstrate::TieredBronze) == 2u);
static_assert(static_cast<uint32_t>(
    s3g::AccelerometerSubstrate::DeepBronze) == 10u);
static_assert(static_cast<uint32_t>(
    s3g::AccelerometerSubstrate::BroadBronze) == 11u);
static_assert(static_cast<uint32_t>(
    s3g::AccelerometerSubstrate::BrightBronze) == 12u);
static_assert(static_cast<uint32_t>(
    s3g::AccelerometerSubstrate::CarbonLaminate) == 13u);
static_assert(static_cast<uint32_t>(
    s3g::AccelerometerSubstrate::GlassPlate) == 14u);
static_assert(static_cast<uint32_t>(
    s3g::AccelerometerSubstrate::SteelShell) == 15u);
static_assert(static_cast<uint32_t>(
    s3g::AccelerometerSubstrate::AluminumPlate) == 16u);
static_assert(static_cast<uint32_t>(
    s3g::AccelerometerSubstrate::PorcelainShell) == 17u);
static_assert(static_cast<uint32_t>(
    s3g::AccelerometerSubstrate::PorousEarthenware) == 18u);
static_assert(static_cast<uint32_t>(
    s3g::AccelerometerSubstrate::SprucePlate) == 19u);
static_assert(static_cast<uint32_t>(
    s3g::AccelerometerSubstrate::TensionedSkin) == 20u);
static_assert(static_cast<uint32_t>(
    s3g::AccelerometerSubstrate::LoadedMembrane) == 21u);
static_assert(static_cast<uint32_t>(
    s3g::AccelerometerSubstrate::CoupledMembrane) == 22u);
static_assert(static_cast<uint32_t>(
    s3g::AccelerometerSubstrate::CavityMembrane) == 23u);
static_assert(static_cast<uint32_t>(
    s3g::AccelerometerSubstrate::LooseMembrane) == 24u);
static_assert(static_cast<uint32_t>(
    s3g::AccelerometerSubstrate::Count) == 25u);
static_assert(s3g::kAccelerometerFieldPresetCount == 25u);
using Buffer = std::array<float, kFrames>;
using StemBuffers = std::array<std::vector<float>,
    s3g::kAccelerometerFieldMaxBodyCount>;

struct Metrics {
    double energy = 0.0;
    double differenceEnergy = 0.0;
    float peak = 0.0f;
    bool finite = true;
};

template <typename Samples>
Metrics metrics(const Samples& buffer, size_t firstFrame = 0u,
    size_t lastFrame = std::numeric_limits<size_t>::max())
{
    Metrics result;
    lastFrame = std::min(lastFrame, buffer.size());
    if (firstFrame >= lastFrame) return result;
    float previous = buffer[firstFrame];
    for (size_t frame = firstFrame; frame < lastFrame; ++frame) {
        const float sample = buffer[frame];
        result.finite = result.finite && std::isfinite(sample);
        result.energy += static_cast<double>(sample) * sample;
        const double difference = static_cast<double>(sample) - previous;
        result.differenceEnergy += difference * difference;
        result.peak = std::max(result.peak, std::fabs(sample));
        previous = sample;
    }
    const double count = static_cast<double>(lastFrame - firstFrame);
    result.energy /= count;
    result.differenceEnergy /= count;
    return result;
}

template <typename First, typename Second>
double difference(const First& first, const Second& second)
{
    const size_t frames = std::min(first.size(), second.size());
    if (frames == 0u) return 0.0;
    double total = 0.0;
    for (size_t frame = 0u; frame < frames; ++frame) {
        total += std::fabs(first[frame] - second[frame]);
    }
    return total / static_cast<double>(frames);
}

struct ScaleFit {
    double scale = 0.0;
    double normalizedError = std::numeric_limits<double>::infinity();
};

template <typename Reference, typename Candidate>
ScaleFit fitScale(const Reference& reference, const Candidate& candidate,
    size_t firstFrame = 0u,
    size_t lastFrame = std::numeric_limits<size_t>::max())
{
    lastFrame = std::min({ lastFrame, reference.size(), candidate.size() });
    if (firstFrame >= lastFrame) return {};
    double referenceEnergy = 0.0;
    double candidateEnergy = 0.0;
    double cross = 0.0;
    for (size_t frame = firstFrame; frame < lastFrame; ++frame) {
        const double ref = reference[frame];
        const double value = candidate[frame];
        referenceEnergy += ref * ref;
        candidateEnergy += value * value;
        cross += ref * value;
    }
    if (!(referenceEnergy > 1.0e-24) || !(candidateEnergy > 1.0e-24)) {
        return {};
    }
    ScaleFit result;
    result.scale = cross / referenceEnergy;
    double errorEnergy = 0.0;
    for (size_t frame = firstFrame; frame < lastFrame; ++frame) {
        const double error = candidate[frame]
            - reference[frame] * result.scale;
        errorEnergy += error * error;
    }
    result.normalizedError = std::sqrt(errorEnergy / candidateEnergy);
    return result;
}

void render(const s3g::AccelerometerFieldParams& params,
    Buffer& first, Buffer& second)
{
    s3g::AccelerometerFieldEncoder engine;
    engine.prepare(kSampleRate);
    engine.setParams(params);
    engine.reset();
    std::array<float*, s3g::kAccelerometerFieldMaxChannels> outputs {};
    outputs[0] = first.data();
    outputs[1] = second.data();
    engine.process(nullptr, outputs.data(), outputs.size(), kFrames);
}

StemBuffers processStems(s3g::AccelerometerFieldEncoder& engine,
    uint32_t frames, float initialValue = 0.0f,
    const float* externalExcitation = nullptr)
{
    StemBuffers audio;
    std::array<float*, s3g::kAccelerometerFieldMaxChannels> outputs {};
    for (uint32_t body = 0u; body < audio.size(); ++body) {
        audio[body].assign(frames, initialValue);
        outputs[body] = audio[body].data();
    }
    engine.process(externalExcitation, outputs.data(), outputs.size(), frames);
    return audio;
}

s3g::AccelerometerFieldParams balancedDroneParams(uint32_t bodyCount)
{
    auto params = s3g::accelerometerFieldFactoryPreset(10u);
    params.bodyCount = bodyCount;
    params.outputMode = s3g::AccelerometerFieldOutputMode::BodyStems;
    params.activity = 0.0f;
    params.ambientDrive = 0.0f;
    params.externalDrive = 0.0f;
    params.sensorNoise = 0.0f;
    params.fieldListenMode = s3g::AmbiFieldListenMode::Balance;
    params.fieldListenAmount = 0.86f;
    params.fieldListenResponse = s3g::AmbiFieldListenerResponse::Imprint;
    params.force = 0.56f;
    params.outputGainDb = -12.0f;
    return params;
}

bool testFactoryPresets()
{
    Buffer first {};
    Buffer second {};
    for (uint32_t preset = 0u;
        preset < s3g::kAccelerometerFieldPresetCount; ++preset) {
        render(s3g::accelerometerFieldFactoryPreset(preset), first, second);
        const Metrics firstMetrics = metrics(first);
        const Metrics secondMetrics = metrics(second);
        const Metrics firstTail = metrics(first, kSampleRate);
        const Metrics secondTail = metrics(second, kSampleRate);
        if (!firstMetrics.finite || !secondMetrics.finite
            || !firstTail.finite || !secondTail.finite
            || !(firstTail.energy > 1.0e-10)
            || !(firstTail.peak > 1.0e-5f)
            || !(firstMetrics.peak <= 1.0f)
            || !(secondMetrics.peak <= 1.0f)) {
            std::cerr << "Ambi Encoder Modal preset " << preset
                      << " failed sustained-tail bounds: tail energy "
                      << firstTail.energy << ", tail peaks "
                      << firstTail.peak << ", " << secondTail.peak
                      << ", overall peaks " << firstMetrics.peak << ", "
                      << secondMetrics.peak << "\n";
            return false;
        }
    }
    return true;
}

bool testLiftedFactoryPresetsRemainClickFree()
{
    constexpr uint32_t blockFrames = 256u;
    constexpr uint32_t totalFrames = kSampleRate * 12u;
    constexpr uint32_t warmupFrames = kSampleRate;
    constexpr std::array<uint32_t, 4u> presets {{ 3u, 5u, 9u, 10u }};
    for (const uint32_t preset : presets) {
        const auto params = s3g::accelerometerFieldFactoryPreset(preset);
        s3g::AccelerometerFieldEncoder engine;
        engine.prepare(kSampleRate);
        engine.setParams(params);
        engine.reset();

        std::array<std::array<float, blockFrames>,
            s3g::kAccelerometerFieldMaxChannels> audio {};
        std::array<float*, s3g::kAccelerometerFieldMaxChannels> outputs {};
        for (uint32_t channel = 0u; channel < outputs.size(); ++channel) {
            outputs[channel] = audio[channel].data();
        }
        std::array<float, s3g::kAccelerometerFieldMaxChannels> previous {};
        double differenceEnergy = 0.0;
        float maximumDifference = 0.0f;
        uint64_t measuredSamples = 0u;
        for (uint32_t start = 0u; start < totalFrames; start += blockFrames) {
            engine.process(nullptr, outputs.data(), outputs.size(), blockFrames);
            for (uint32_t frame = 0u; frame < blockFrames; ++frame) {
                for (uint32_t channel = 0u;
                    channel < engine.outputChannelCount(); ++channel) {
                    const float sample = audio[channel][frame];
                    if (!std::isfinite(sample)) {
                        std::cerr << "Lifted modal preset produced a non-finite sample: "
                                  << preset << "\n";
                        return false;
                    }
                    const float delta = std::fabs(sample - previous[channel]);
                    if (start + frame >= warmupFrames) {
                        differenceEnergy += static_cast<double>(delta) * delta;
                        maximumDifference = std::max(maximumDifference, delta);
                        ++measuredSamples;
                    }
                    previous[channel] = sample;
                }
            }
        }
        const double differenceRms = std::sqrt(differenceEnergy
            / static_cast<double>(std::max<uint64_t>(1u, measuredSamples)));
        const double differenceCrest = maximumDifference
            / std::max(1.0e-12, differenceRms);
        if (!(differenceRms > 1.0e-8)
            || !(maximumDifference < 0.0025f)
            || !(differenceCrest < 24.0)) {
            std::cerr << "Lifted modal preset retained a click-like discontinuity: "
                      << preset << ", max delta " << maximumDifference
                      << ", derivative RMS " << differenceRms
                      << ", crest " << differenceCrest << "\n";
            return false;
        }
    }
    return true;
}

bool testDeterminism()
{
    Buffer first {};
    Buffer second {};
    Buffer companion {};
    const auto params = s3g::accelerometerFieldFactoryPreset(3u);
    render(params, first, companion);
    render(params, second, companion);
    if (first != second) {
        std::cerr << "Ambi Encoder Modal is not deterministic for one seed\n";
        return false;
    }
    return true;
}

bool testHoaDefaultAndRotation()
{
    Buffer frontW {};
    Buffer frontY {};
    Buffer turnedW {};
    Buffer turnedY {};
    auto params = s3g::accelerometerFieldFactoryPreset(3u);
    s3g::AccelerometerFieldEncoder contract;
    contract.prepare(kSampleRate);
    contract.setParams(params);
    if (params.outputMode != s3g::AccelerometerFieldOutputMode::Ambisonic
        || params.ambisonicOrder != 3u
        || contract.outputChannelCount() != 16u) {
        std::cerr << "Ambi Encoder Modal did not default to 3OA ACN/SN3D\n";
        return false;
    }

    params.ambisonicOrder = 1u;
    // Spatial rotation is an encoder contract. Disable the world-fixed
    // listener/actuator so its intentionally anchored feedback does not alter
    // the source while measuring that projection invariant.
    params.fieldListenMode = s3g::AmbiFieldListenMode::Off;
    const auto renderStrike = [](const auto& strikeParams,
                                 Buffer& w, Buffer& y) {
        s3g::AccelerometerFieldEncoder engine;
        engine.prepare(kSampleRate);
        engine.setParams(strikeParams);
        engine.reset();
        engine.strikeMidi(60, 0.82f);
        std::array<float*, s3g::kAccelerometerFieldMaxChannels> outputs {};
        outputs[0] = w.data();
        outputs[1] = y.data();
        engine.process(nullptr, outputs.data(), outputs.size(), kFrames);
    };
    params.fieldAzimuthDeg = 0.0f;
    renderStrike(params, frontW, frontY);
    params.fieldAzimuthDeg = 90.0f;
    renderStrike(params, turnedW, turnedY);
    const double wDifference = difference(frontW, turnedW);
    const double yDifference = difference(frontY, turnedY);
    if (!(wDifference < 1.0e-7) || !(yDifference > 1.0e-5)) {
        std::cerr << "HOA rotation did not preserve W while rotating direction: "
                  << wDifference << ", " << yDifference << "\n";
        return false;
    }
    return true;
}

bool testLowerOrderClearsUnusedChannels()
{
    constexpr uint32_t frames = 4096u;
    std::array<std::array<float, frames>,
        s3g::kAccelerometerFieldMaxChannels> audio {};
    std::array<float*, s3g::kAccelerometerFieldMaxChannels> outputs {};
    for (uint32_t channel = 0u; channel < outputs.size(); ++channel) {
        audio[channel].fill(0.25f);
        outputs[channel] = audio[channel].data();
    }
    auto params = s3g::accelerometerFieldFactoryPreset(0u);
    params.ambisonicOrder = 1u;
    s3g::AccelerometerFieldEncoder engine;
    engine.prepare(kSampleRate);
    engine.setParams(params);
    engine.reset();
    engine.process(nullptr, outputs.data(), outputs.size(), frames);
    for (uint32_t channel = 4u; channel < outputs.size(); ++channel) {
        for (float sample : audio[channel]) {
            if (sample != 0.0f) {
                std::cerr << "Lower HOA order left an unused channel uncleared\n";
                return false;
            }
        }
    }
    return true;
}

bool testBodyCountAndModeBudget()
{
    auto params = s3g::accelerometerFieldFactoryPreset(0u);
    params.bodyCount = 0u;
    if (s3g::sanitizeAccelerometerFieldParams(params).bodyCount
        != s3g::kAccelerometerFieldMinBodyCount) {
        std::cerr << "Body count did not clamp to four\n";
        return false;
    }
    params.bodyCount = std::numeric_limits<uint32_t>::max();
    if (s3g::sanitizeAccelerometerFieldParams(params).bodyCount
        != s3g::kAccelerometerFieldMaxBodyCount) {
        std::cerr << "Body count did not clamp to eight\n";
        return false;
    }

    s3g::AccelerometerFieldEncoder engine;
    engine.prepare(kSampleRate);
    for (uint32_t count = s3g::kAccelerometerFieldMinBodyCount;
        count <= s3g::kAccelerometerFieldMaxBodyCount; ++count) {
        params.bodyCount = count;
        engine.setParams(params);
        uint32_t totalModes = 0u;
        if (engine.bodyCount() != count
            || engine.activeModeCount() != s3g::kAccelerometerFieldModeBudget) {
            std::cerr << "Body allocation reported the wrong active count at "
                      << count << " bodies\n";
            return false;
        }
        for (uint32_t body = 0u;
            body < s3g::kAccelerometerFieldMaxBodyCount; ++body) {
            const uint32_t modes = engine.modesForBody(body);
            if (body < count) {
                if (modes == 0u || (modes & 1u) != 0u) {
                    std::cerr << "Modal allocation split a resonant pair at "
                              << count << " bodies, body " << body << "\n";
                    return false;
                }
                totalModes += modes;
            } else if (modes != 0u) {
                std::cerr << "Inactive body retained modal allocation\n";
                return false;
            }
        }
        if (totalModes != s3g::kAccelerometerFieldModeBudget) {
            std::cerr << "Modal allocation did not sum to 96 at " << count
                      << " bodies: " << totalModes << "\n";
            return false;
        }
    }
    return true;
}

float directionDistance(s3g::Vec3 first, s3g::Vec3 second)
{
    const float x = first.x - second.x;
    const float y = first.y - second.y;
    const float z = first.z - second.z;
    return std::sqrt(x * x + y * y + z * z);
}

bool testFixedListenerPickupSets()
{
    auto params = s3g::accelerometerFieldFactoryPreset(0u);
    if (params.listenerPickupSet
        != s3g::AccelerometerFieldListenerPickupSet::Cube8) {
        std::cerr << "Modal listener did not default to Cube 8\n";
        return false;
    }
    auto unsafe = params;
    unsafe.listenerPickupSet =
        static_cast<s3g::AccelerometerFieldListenerPickupSet>(99u);
    if (s3g::sanitizeAccelerometerFieldParams(unsafe).listenerPickupSet
        != s3g::AccelerometerFieldListenerPickupSet::Cube8) {
        std::cerr << "Invalid listener pickup set did not sanitize to Cube 8\n";
        return false;
    }

    s3g::AccelerometerFieldEncoder engine;
    engine.prepare(kSampleRate);
    engine.setParams(params);
    const auto& cube = s3g::ambiFieldListenerCubeDirections();
    if (engine.listenerPickupCount() != cube.size()) {
        std::cerr << "Cube listener did not configure eight fixed ears\n";
        return false;
    }
    for (uint32_t ear = 0u; ear < cube.size(); ++ear) {
        if (directionDistance(engine.listenerPickupDirection(ear), cube[ear])
            > 1.0e-6f) {
            std::cerr << "Cube listener direction mismatch at ear "
                      << ear << "\n";
            return false;
        }
    }

    std::array<uint32_t, s3g::kAccelerometerFieldMaxBodyCount> modeCounts {};
    std::array<s3g::Vec3, s3g::kAccelerometerFieldMaxBodyCount>
        bodyDirections {};
    for (uint32_t body = 0u; body < params.bodyCount; ++body) {
        modeCounts[body] = engine.modesForBody(body);
        bodyDirections[body] = engine.bodyDirection(body);
    }
    params.listenerPickupSet =
        s3g::AccelerometerFieldListenerPickupSet::Tetra4;
    engine.setParams(params);
    const auto& tetra = s3g::ambiFieldListenerTetraDirections();
    if (engine.listenerPickupCount() != tetra.size()) {
        std::cerr << "Tetra listener did not configure four fixed ears\n";
        return false;
    }
    for (uint32_t ear = 0u; ear < tetra.size(); ++ear) {
        const s3g::Vec3 actual = engine.listenerPickupDirection(ear);
        if (directionDistance(actual, tetra[ear]) > 1.0e-6f) {
            std::cerr << "Tetra listener direction mismatch at ear "
                      << ear << "\n";
            return false;
        }
        for (uint32_t other = ear + 1u; other < tetra.size(); ++other) {
            const float dot = actual.x * tetra[other].x
                + actual.y * tetra[other].y
                + actual.z * tetra[other].z;
            if (std::fabs(dot + 1.0f / 3.0f) > 2.0e-5f) {
                std::cerr << "Tetra listener lost its symmetric geometry\n";
                return false;
            }
        }
    }
    for (uint32_t body = 0u; body < params.bodyCount; ++body) {
        if (engine.modesForBody(body) != modeCounts[body]
            || directionDistance(engine.bodyDirection(body),
                bodyDirections[body]) > 1.0e-7f) {
            std::cerr << "Switching listener sets rebuilt a modal body\n";
            return false;
        }
    }

    params.fieldAzimuthDeg = 127.0f;
    params.fieldElevationDeg = -31.0f;
    params.spatialExtent = 0.22f;
    params.bodyAzimuthOffsetDeg[0] = 76.0f;
    params.bodyElevationOffsetDeg[1] = -42.0f;
    engine.setParams(params);
    if (engine.listenerPickupCount() != tetra.size()) {
        std::cerr << "Body geometry changed the listener ear count\n";
        return false;
    }
    for (uint32_t ear = 0u; ear < tetra.size(); ++ear) {
        if (directionDistance(engine.listenerPickupDirection(ear), tetra[ear])
            > 1.0e-6f) {
            std::cerr << "Body AED editing moved a fixed listener ear\n";
            return false;
        }
    }
    return true;
}

bool testListenerAndActuatorTelemetry()
{
    constexpr uint32_t frames = kSampleRate;
    auto params = balancedDroneParams(8u);
    params.listenerPickupSet =
        s3g::AccelerometerFieldListenerPickupSet::Tetra4;
    s3g::AccelerometerFieldEncoder engine;
    engine.prepare(kSampleRate);
    engine.setParams(params);
    engine.reset();
    const StemBuffers drone = processStems(engine, frames);

    float pickupPeak = 0.0f;
    for (uint32_t ear = 0u; ear < 4u; ++ear) {
        const float energy = engine.listenerPickupEnergy(ear);
        if (!std::isfinite(energy) || energy < 0.0f) {
            std::cerr << "Tetra listener produced invalid pickup telemetry\n";
            return false;
        }
        pickupPeak = std::max(pickupPeak, energy);
    }
    for (uint32_t ear = 4u;
        ear < s3g::kAmbiFieldListenerMaxLobes; ++ear) {
        if (engine.listenerPickupEnergy(ear) != 0.0f) {
            std::cerr << "Inactive Tetra listener ear retained telemetry\n";
            return false;
        }
    }
    if (!(pickupPeak > 1.0e-6f)) {
        std::cerr << "Tetra listener did not hear the internal HOA field\n";
        return false;
    }
    for (uint32_t body = 0u; body < params.bodyCount; ++body) {
        if (!(engine.actuatorBodyDrive(body) > 1.0e-7f)
            || !(engine.bodyEnergy(body) > 1.0e-8f)
            || !metrics(drone[body]).finite) {
            std::cerr << "Diffuse actuator telemetry omitted body "
                      << body << "\n";
            return false;
        }
    }

    params.listenerPickupSet =
        s3g::AccelerometerFieldListenerPickupSet::Cube8;
    engine.setParams(params);
    if (engine.listenerPickupCount() != 8u) {
        std::cerr << "Live listener switch did not configure Cube 8\n";
        return false;
    }
    for (uint32_t ear = 0u; ear < 8u; ++ear) {
        if (engine.listenerPickupEnergy(ear) != 0.0f) {
            std::cerr << "Listener switch did not clear directional history\n";
            return false;
        }
    }

    const auto renderOffStrike = [](auto pickupSet) {
        auto off = s3g::accelerometerFieldFactoryPreset(0u);
        off.bodyCount = 4u;
        off.outputMode = s3g::AccelerometerFieldOutputMode::BodyStems;
        off.fieldListenMode = s3g::AmbiFieldListenMode::Off;
        off.externalDrive = 0.0f;
        off.listenerPickupSet = pickupSet;
        s3g::AccelerometerFieldEncoder probe;
        probe.prepare(kSampleRate);
        probe.setParams(off);
        probe.reset();
        probe.strikeMidi(60, 0.8f);
        return processStems(probe, 12000u);
    };
    const StemBuffers tetraOff = renderOffStrike(
        s3g::AccelerometerFieldListenerPickupSet::Tetra4);
    const StemBuffers cubeOff = renderOffStrike(
        s3g::AccelerometerFieldListenerPickupSet::Cube8);
    if (tetraOff != cubeOff) {
        std::cerr << "Analyzer-only listener geometry changed the Off path\n";
        return false;
    }

    auto off = params;
    off.bodyCount = 4u;
    off.fieldListenMode = s3g::AmbiFieldListenMode::Off;
    off.listenerPickupSet =
        s3g::AccelerometerFieldListenerPickupSet::Tetra4;
    engine.setParams(off);
    engine.reset();
    engine.strikeMidi(60, 0.8f);
    (void)processStems(engine, 4096u);
    if (!(engine.actuatorBodyDrive(0u) > 1.0e-7f)) {
        std::cerr << "MIDI actuator drive was not published\n";
        return false;
    }
    for (uint32_t body = 1u;
        body < s3g::kAccelerometerFieldMaxBodyCount; ++body) {
        if (engine.actuatorBodyDrive(body) != 0.0f) {
            std::cerr << "MIDI actuator telemetry leaked to another body\n";
            return false;
        }
    }
    engine.reset();
    for (uint32_t body = 0u;
        body < s3g::kAccelerometerFieldMaxBodyCount; ++body) {
        if (engine.actuatorBodyDrive(body) != 0.0f) {
            std::cerr << "Reset retained actuator telemetry\n";
            return false;
        }
    }
    return true;
}

bool testCounterActuatorUsesSpatialAntipode()
{
    auto params = s3g::accelerometerFieldFactoryPreset(0u);
    params.bodyCount = 6u;
    params.outputMode = s3g::AccelerometerFieldOutputMode::BodyStems;
    params.activity = 0.0f;
    params.ambientDrive = 0.0f;
    params.externalDrive = 0.0f;
    params.sensorNoise = 0.0f;
    params.spatialExtent = 1.0f;
    params.fieldListenMode = s3g::AmbiFieldListenMode::Off;
    params.fieldListenAmount = 1.0f;
    params.listenerPickupSet =
        s3g::AccelerometerFieldListenerPickupSet::Cube8;

    s3g::AccelerometerFieldEncoder engine;
    engine.prepare(kSampleRate);
    engine.setParams(params);
    engine.reset();
    engine.strikeMidi(60, 1.0f);
    if (engine.lastActuatedBody() != 0u) {
        std::cerr << "Counter probe did not begin on body zero\n";
        return false;
    }
    (void)processStems(engine, kSampleRate / 2u);

    const s3g::Vec3 source = engine.bodyDirection(0u);
    uint32_t antipodalBody = 0u;
    float minimumDot = 1.0f;
    for (uint32_t body = 0u; body < params.bodyCount; ++body) {
        const s3g::Vec3 candidate = engine.bodyDirection(body);
        const float dot = source.x * candidate.x
            + source.y * candidate.y + source.z * candidate.z;
        if (dot < minimumDot) {
            minimumDot = dot;
            antipodalBody = body;
        }
    }

    params.fieldListenMode = s3g::AmbiFieldListenMode::Counter;
    engine.setParams(params);
    engine.strikeMidi(60, 1.0f);
    if (engine.lastActuatedBody() != antipodalBody) {
        std::cerr << "Counter actuator did not select the spatial antipode: "
                  << engine.lastActuatedBody() << " instead of "
                  << antipodalBody << "\n";
        return false;
    }
    return true;
}

bool testWorstCaseOutputHeadroom()
{
    constexpr uint32_t blockFrames = 256u;
    auto params = s3g::accelerometerFieldFactoryPreset(10u);
    params.substrate = s3g::AccelerometerSubstrate::BroadBronze;
    params.bodyCount = s3g::kAccelerometerFieldMaxBodyCount;
    params.ambisonicOrder = s3g::kAccelerometerFieldMaxOrder;
    params.outputGainDb = 12.0f;
    params.modalLift = 1.0f;
    params.bodyDistance.fill(0.15f);
    params.spatialExtent = 0.0f;
    params.fieldAzimuthDeg = 0.0f;
    params.fieldElevationDeg = 0.0f;
    params.bodyAzimuthOffsetDeg.fill(0.0f);
    params.bodyElevationOffsetDeg.fill(0.0f);
    params.fieldListenMode = s3g::AmbiFieldListenMode::Balance;
    params.fieldListenAmount = 1.0f;
    params.fieldListenResponse =
        s3g::AmbiFieldListenerResponse::Imprint;
    params.externalDrive = 1.0f;
    params.coupling = 1.0f;
    params.energy = 1.0f;
    params.force = 1.0f;
    params.size = 0.0f;
    params.damping = 0.0f;
    params.sourcePosition = 0.0f;
    params.pickupPosition = 0.0f;
    params.pickupAxis = 0.0f;
    params.contactRadiation = 0.0f;
    params.airRadiation = 1.0f;
    params.mountStiffness = 1.0f;
    params.sensorMass = 0.0f;
    params.conditionerHighpassHz = 0.25f;
    params.arraySpread = 1.0f;
    params.seed = 0x6d2b79f5u;

    const auto renderWorstCase = [&](s3g::AccelerometerFieldOutputMode mode,
                                      uint32_t seconds) {
        params.outputMode = mode;
        s3g::AccelerometerFieldEncoder engine;
        engine.prepare(kSampleRate);
        engine.setParams(params);
        engine.reset();
        const auto strikeAllBodies = [&]() {
            auto off = params;
            off.fieldListenMode = s3g::AmbiFieldListenMode::Off;
            off.fieldListenAmount = 0.0f;
            engine.setParams(off);
            for (uint32_t body = 0u; body < params.bodyCount; ++body) {
                engine.strikeMidi(60, 1.0f);
            }
            engine.setParams(params);
        };
        strikeAllBodies();

        std::array<std::array<float, blockFrames>,
            s3g::kAccelerometerFieldMaxChannels> audio {};
        std::array<float*, s3g::kAccelerometerFieldMaxChannels> outputs {};
        for (uint32_t channel = 0u; channel < audio.size(); ++channel) {
            outputs[channel] = audio[channel].data();
        }
        std::array<float, blockFrames> excitation {};
        uint32_t noiseState = 0x01234567u ^ params.seed;
        float peak = 0.0f;
        const uint32_t blockCount = kSampleRate * seconds / blockFrames;
        for (uint32_t block = 0u; block < blockCount; ++block) {
            if (block > 0u && block % (kSampleRate / blockFrames) == 0u) {
                strikeAllBodies();
            }
            for (float& sample : excitation) {
                noiseState ^= noiseState << 13u;
                noiseState ^= noiseState >> 17u;
                noiseState ^= noiseState << 5u;
                sample = static_cast<float>(static_cast<int32_t>(noiseState))
                    / 2147483648.0f;
            }
            engine.process(excitation.data(), outputs.data(), outputs.size(),
                blockFrames);
            for (const auto& channel : audio) {
                const Metrics channelMetrics = metrics(channel);
                if (!channelMetrics.finite) {
                    return std::numeric_limits<float>::quiet_NaN();
                }
                peak = std::max(peak, channelMetrics.peak);
            }
        }
        return peak;
    };

    for (const auto mode : {
             s3g::AccelerometerFieldOutputMode::Ambisonic,
             s3g::AccelerometerFieldOutputMode::BodyStems }) {
        const float peak = renderWorstCase(mode,
            mode == s3g::AccelerometerFieldOutputMode::Ambisonic ? 8u : 4u);
        if (!std::isfinite(peak) || !(peak > 0.80f)
            || peak > kLinkedOutputCeiling + 2.0e-4f) {
            std::cerr << "Worst-case "
                      << (mode == s3g::AccelerometerFieldOutputMode::Ambisonic
                              ? "HOA" : "body-stem")
                      << " linked safety was not exercised or exceeded -1 dBFS: "
                      << peak << "\n";
            return false;
        }
    }
    return true;
}

bool testBodyPlacementAndSpread()
{
    s3g::AccelerometerFieldEncoder engine;
    engine.prepare(kSampleRate);
    auto params = s3g::accelerometerFieldFactoryPreset(2u);
    params.bodyCount = 8u;
    params.fieldAzimuthDeg = 37.0f;
    params.fieldElevationDeg = -18.0f;
    params.spatialExtent = 0.0f;
    params.arraySpread = 0.0f;
    params.pickupPosition = 0.18f;
    engine.setParams(params);

    const s3g::Vec3 localizedDirection = engine.bodyDirection(0u);
    const float localizedPosition = engine.bodyPosition(0u);
    for (uint32_t body = 1u; body < params.bodyCount; ++body) {
        if (directionDistance(engine.bodyDirection(body), localizedDirection)
                > 1.0e-6f
            || std::fabs(engine.bodyPosition(body) - localizedPosition)
                > 1.0e-6f) {
            std::cerr << "Zero spread did not co-locate active bodies\n";
            return false;
        }
    }

    params.spatialExtent = 1.0f;
    params.arraySpread = 1.0f;
    engine.setParams(params);
    float minimumDirectionSeparation = 2.0f;
    for (uint32_t first = 0u; first < params.bodyCount; ++first) {
        for (uint32_t second = first + 1u;
            second < params.bodyCount; ++second) {
            minimumDirectionSeparation = std::min(
                minimumDirectionSeparation,
                directionDistance(engine.bodyDirection(first),
                    engine.bodyDirection(second)));
        }
    }
    if (!(minimumDirectionSeparation > 0.35f)
        || !(engine.bodyPosition(7u) - engine.bodyPosition(0u) > 0.75f)) {
        std::cerr << "Full spread did not separate the body constellation: "
                  << minimumDirectionSeparation << "\n";
        return false;
    }
    return true;
}

bool testBodyGeometrySmoothing()
{
    s3g::AccelerometerFieldEncoder engine;
    engine.prepare(kSampleRate);
    auto params = s3g::accelerometerFieldFactoryPreset(2u);
    params.bodyCount = 6u;
    params.pickupPosition = 0.15f;
    params.arraySpread = 0.0f;
    engine.setParams(params);
    engine.reset();
    const uint32_t body = params.bodyCount - 1u;
    const float initial = engine.currentBodyPosition(body);

    params.pickupPosition = 0.85f;
    params.arraySpread = 1.0f;
    engine.setParams(params);
    const float target = engine.bodyPosition(body);
    if (!(target > initial + 0.50f)
        || std::fabs(engine.currentBodyPosition(body) - initial) > 1.0e-7f) {
        std::cerr << "Body geometry snapped before audio-rate interpolation\n";
        return false;
    }

    constexpr uint32_t frames = 4800u;
    std::array<float, frames> audio {};
    std::array<float*, s3g::kAccelerometerFieldMaxChannels> outputs {};
    outputs[0] = audio.data();
    engine.process(nullptr, outputs.data(), outputs.size(), 1u);
    const float firstStep = engine.currentBodyPosition(body);
    if (!(firstStep > initial)
        || !(firstStep < initial + (target - initial) * 0.01f)) {
        std::cerr << "Body geometry did not begin with a click-free small step\n";
        return false;
    }
    engine.process(nullptr, outputs.data(), outputs.size(), frames);
    if (!(std::fabs(engine.currentBodyPosition(body) - target)
            < std::fabs(target - initial) * 0.08f)) {
        std::cerr << "Body geometry smoothing did not converge promptly\n";
        return false;
    }
    return true;
}

bool testPerBodyAedEditing()
{
    auto unsafe = s3g::accelerometerFieldFactoryPreset(0u);
    unsafe.bodyAzimuthOffsetDeg[0] = 999.0f;
    unsafe.bodyElevationOffsetDeg[0] = -999.0f;
    unsafe.bodyDistance[0] = 999.0f;
    unsafe.bodyAzimuthOffsetDeg[1] =
        std::numeric_limits<float>::quiet_NaN();
    unsafe.bodyElevationOffsetDeg[1] =
        std::numeric_limits<float>::infinity();
    unsafe.bodyDistance[1] = -std::numeric_limits<float>::infinity();
    const auto safe = s3g::sanitizeAccelerometerFieldParams(unsafe);
    if (safe.bodyAzimuthOffsetDeg[0] != 180.0f
        || safe.bodyElevationOffsetDeg[0] != -180.0f
        || safe.bodyDistance[0] != 2.0f
        || safe.bodyAzimuthOffsetDeg[1] != 0.0f
        || safe.bodyElevationOffsetDeg[1] != 0.0f
        || safe.bodyDistance[1] != 1.0f) {
        std::cerr << "Per-body AED fields did not sanitize safely\n";
        return false;
    }

    auto params = s3g::accelerometerFieldFactoryPreset(0u);
    params.bodyCount = 8u;
    params.outputMode = s3g::AccelerometerFieldOutputMode::BodyStems;
    params.fieldListenMode = s3g::AmbiFieldListenMode::Off;
    params.externalDrive = 0.0f;
    params.spatialExtent = 0.86f;
    params.fieldAzimuthDeg = 23.0f;
    params.fieldElevationDeg = -11.0f;
    params.bodyAzimuthOffsetDeg.fill(0.0f);
    params.bodyElevationOffsetDeg.fill(0.0f);
    params.bodyDistance.fill(1.0f);

    s3g::AccelerometerFieldEncoder engine;
    engine.prepare(kSampleRate);
    engine.setParams(params);
    engine.reset();
    std::array<s3g::Vec3, s3g::kAccelerometerFieldMaxBodyCount>
        autoDirections {};
    std::array<uint32_t, s3g::kAccelerometerFieldMaxBodyCount> modeCounts {};
    for (uint32_t body = 0u; body < params.bodyCount; ++body) {
        autoDirections[body] = engine.bodyDirection(body);
        modeCounts[body] = engine.modesForBody(body);
    }

    engine.strikeMidi(60, 0.84f);
    const StemBuffers onset = processStems(engine, 4096u);
    if (!(metrics(onset[0]).energy > 1.0e-12)) {
        std::cerr << "AED test could not establish a ringing body\n";
        return false;
    }
    const float energyBeforeEdit = engine.bodyEnergy(0u);
    const float pitchBeforeEdit = engine.bodyPitchRatio(0u);
    const float frequencyBeforeEdit = engine.bodyModeFrequencyHz(0u, 0u);

    params.bodyAzimuthOffsetDeg[0] = 54.0f;
    params.bodyElevationOffsetDeg[0] = -17.0f;
    params.bodyDistance[0] = 1.70f;
    engine.setParams(params);
    if (!(directionDistance(engine.bodyDirection(0u), autoDirections[0])
            > 0.20f)
        || std::fabs(engine.bodyDistance(0u) - 1.70f) > 1.0e-7f
        || engine.bodyEnergy(0u) != energyBeforeEdit
        || engine.bodyPitchRatio(0u) != pitchBeforeEdit
        || engine.bodyModeFrequencyHz(0u, 0u) != frequencyBeforeEdit) {
        std::cerr << "Direct AED editing moved or reset modal state incorrectly\n";
        return false;
    }
    for (uint32_t body = 0u; body < params.bodyCount; ++body) {
        if (engine.modesForBody(body) != modeCounts[body]) {
            std::cerr << "Direct AED editing rebuilt the modal allocation\n";
            return false;
        }
    }
    const StemBuffers movedTail = processStems(engine, 4096u);
    if (!(metrics(movedTail[0]).energy > 1.0e-13)) {
        std::cerr << "Direct AED editing silenced an already-ringing body\n";
        return false;
    }

    const float energyBeforeRestore = engine.bodyEnergy(0u);
    params.bodyAzimuthOffsetDeg[0] = 0.0f;
    params.bodyElevationOffsetDeg[0] = 0.0f;
    params.bodyDistance[0] = 1.0f;
    engine.setParams(params);
    if (directionDistance(engine.bodyDirection(0u), autoDirections[0])
            > 1.0e-6f
        || engine.bodyEnergy(0u) != energyBeforeRestore) {
        std::cerr << "Zero AED offsets did not restore automatic geometry "
                  << "without resetting the body\n";
        return false;
    }

    auto inactiveParams = params;
    inactiveParams.bodyCount = 4u;
    inactiveParams.bodyAzimuthOffsetDeg[7] = 39.0f;
    inactiveParams.bodyElevationOffsetDeg[7] = 12.0f;
    inactiveParams.bodyDistance[7] = 1.46f;
    s3g::AccelerometerFieldEncoder inactiveEngine;
    inactiveEngine.prepare(kSampleRate);
    inactiveEngine.setParams(inactiveParams);
    inactiveEngine.reset();
    const auto storedWhileInactive = inactiveEngine.params();
    if (storedWhileInactive.bodyAzimuthOffsetDeg[7] != 39.0f
        || storedWhileInactive.bodyElevationOffsetDeg[7] != 12.0f
        || storedWhileInactive.bodyDistance[7] != 1.46f) {
        std::cerr << "An inactive body's AED edit was discarded\n";
        return false;
    }
    inactiveParams.bodyCount = 8u;
    inactiveEngine.setParams(inactiveParams);

    auto automaticParams = inactiveParams;
    automaticParams.bodyAzimuthOffsetDeg[7] = 0.0f;
    automaticParams.bodyElevationOffsetDeg[7] = 0.0f;
    automaticParams.bodyDistance[7] = 1.0f;
    s3g::AccelerometerFieldEncoder automaticEngine;
    automaticEngine.prepare(kSampleRate);
    automaticEngine.setParams(automaticParams);
    automaticEngine.reset();
    const auto restoredEdit = inactiveEngine.params();
    if (restoredEdit.bodyAzimuthOffsetDeg[7] != 39.0f
        || restoredEdit.bodyElevationOffsetDeg[7] != 12.0f
        || std::fabs(inactiveEngine.bodyDistance(7u) - 1.46f) > 1.0e-7f
        || !(directionDistance(inactiveEngine.bodyDirection(7u),
                 automaticEngine.bodyDirection(7u)) > 0.20f)) {
        std::cerr << "An inactive AED edit did not survive body activation\n";
        return false;
    }

    auto distanceParams = params;
    distanceParams.bodyCount = 4u;
    distanceParams.bodyDistance[0] = 0.50f;
    s3g::AccelerometerFieldEncoder distanceEngine;
    distanceEngine.prepare(kSampleRate);
    distanceEngine.setParams(distanceParams);
    distanceEngine.reset();
    distanceEngine.strikeMidi(60, 0.80f);
    const StemBuffers nearAudio = processStems(distanceEngine, 8192u);
    distanceParams.bodyDistance[0] = 2.0f;
    distanceEngine.setParams(distanceParams);
    distanceEngine.reset();
    if (distanceEngine.currentBodyDistance(0u) != 2.0f) {
        std::cerr << "Engine reset lost the edited body distance\n";
        return false;
    }
    distanceEngine.strikeMidi(60, 0.80f);
    const StemBuffers farAudio = processStems(distanceEngine, 8192u);
    const double nearEnergy = metrics(nearAudio[0]).energy;
    const double farEnergy = metrics(farAudio[0]).energy;
    if (!(nearEnergy > farEnergy * 8.0)) {
        std::cerr << "Body distance stopped affecting level across reset: "
                  << nearEnergy << ", " << farEnergy << "\n";
        return false;
    }
    return true;
}

bool testRawBodyStems()
{
    constexpr uint32_t frames = kSampleRate;
    constexpr uint32_t count = 6u;
    const auto params = balancedDroneParams(count);
    s3g::AccelerometerFieldEncoder engine;
    engine.prepare(kSampleRate);
    engine.setParams(params);
    engine.reset();
    if (engine.outputChannelCount()
        != s3g::kAccelerometerFieldMaxBodyCount) {
        std::cerr << "Raw-body mode exposed the wrong channel contract\n";
        return false;
    }

    const StemBuffers audio = processStems(engine, frames, 0.25f);
    for (uint32_t body = 0u; body < count; ++body) {
        const Metrics bodyMetrics = metrics(audio[body]);
        if (!bodyMetrics.finite || !(bodyMetrics.energy > 1.0e-10)
            || !(bodyMetrics.peak < 1.0f)) {
            std::cerr << "Active raw body stem was silent or unbounded: "
                      << body << ", " << bodyMetrics.energy << ", "
                      << bodyMetrics.peak << "\n";
            return false;
        }
        if (body > 0u && !(difference(audio[0], audio[body]) > 1.0e-7)) {
            std::cerr << "Raw body stems collapsed to a shared signal\n";
            return false;
        }
    }
    for (uint32_t body = count;
        body < s3g::kAccelerometerFieldMaxBodyCount; ++body) {
        if (!std::all_of(audio[body].begin(), audio[body].end(),
                [](float sample) { return sample == 0.0f; })) {
            std::cerr << "Inactive raw body stem was not cleared exactly\n";
            return false;
        }
    }
    return true;
}

bool testBalancedDroneAcrossBodyCounts()
{
    constexpr uint32_t frames = kSampleRate * 3u;
    constexpr uint32_t tailStart = kSampleRate * 2u;
    for (const uint32_t count : { 4u, 8u }) {
        const auto params = balancedDroneParams(count);
        s3g::AccelerometerFieldEncoder engine;
        engine.prepare(kSampleRate);
        engine.setParams(params);
        engine.reset();
        const StemBuffers audio = processStems(engine, frames);

        double minimumEnergy = std::numeric_limits<double>::max();
        double maximumEnergy = 0.0;
        for (uint32_t body = 0u; body < count; ++body) {
            const Metrics tail = metrics(audio[body], tailStart);
            if (!tail.finite || !(tail.energy > 1.0e-8)
                || !(tail.peak > 1.0e-6f) || !(tail.peak < 0.98f)) {
                std::cerr << "Balanced drone did not sustain a finite body at "
                          << count << " bodies, stem " << body << ": "
                          << tail.energy << ", " << tail.peak << "\n";
                return false;
            }
            minimumEnergy = std::min(minimumEnergy, tail.energy);
            maximumEnergy = std::max(maximumEnergy, tail.energy);
        }
        const double energyRatio = maximumEnergy
            / std::max(minimumEnergy, 1.0e-30);
        if (!(energyRatio < 4.0)
            || !(engine.listenerActivity() > 0.20f)) {
            std::cerr << "Balanced drone developed unreasonable body imbalance at "
                      << count << " bodies: " << energyRatio
                      << ", activity " << engine.listenerActivity() << "\n";
            return false;
        }
    }
    return true;
}

bool testPublicProfilesRemainDistinct()
{
    constexpr std::array<s3g::AccelerometerSubstrate, 16u> profiles {{
        s3g::AccelerometerSubstrate::DeepBronze,
        s3g::AccelerometerSubstrate::TieredBronze,
        s3g::AccelerometerSubstrate::BroadBronze,
        s3g::AccelerometerSubstrate::BrightBronze,
        s3g::AccelerometerSubstrate::CarbonLaminate,
        s3g::AccelerometerSubstrate::GlassPlate,
        s3g::AccelerometerSubstrate::SteelShell,
        s3g::AccelerometerSubstrate::AluminumPlate,
        s3g::AccelerometerSubstrate::PorcelainShell,
        s3g::AccelerometerSubstrate::PorousEarthenware,
        s3g::AccelerometerSubstrate::SprucePlate,
        s3g::AccelerometerSubstrate::TensionedSkin,
        s3g::AccelerometerSubstrate::LoadedMembrane,
        s3g::AccelerometerSubstrate::CoupledMembrane,
        s3g::AccelerometerSubstrate::CavityMembrane,
        s3g::AccelerometerSubstrate::LooseMembrane,
    }};
    std::array<bool, profiles.size()> seen {};
    for (uint32_t preset = 0u;
        preset < s3g::kAccelerometerFieldPresetCount; ++preset) {
        const auto params = s3g::accelerometerFieldFactoryPreset(preset);
        const auto found = std::find(
            profiles.begin(), profiles.end(), params.substrate);
        if (found == profiles.end()) {
            std::cerr << "Factory bank exposed a non-public modal profile\n";
            return false;
        }
        seen[static_cast<size_t>(found - profiles.begin())] = true;
    }
    if (!std::all_of(seen.begin(), seen.end(), [](bool value) { return value; })) {
        std::cerr << "Factory bank no longer covers all public modal profiles\n";
        return false;
    }

    struct ProfileSignature {
        float fundamentalHz = 0.0f;
        float fundamentalDecaySeconds = 0.0f;
        std::array<float, 12u> normalizedModes {};
        std::array<float, 12u> normalizedDecay {};
    };
    std::array<ProfileSignature, profiles.size()> signatures {};
    auto params = s3g::accelerometerFieldFactoryPreset(0u);
    params.bodyCount = 4u;
    params.size = 0.5f;
    params.irregularity = 0.0f;
    params.sensorMass = 0.0f;
    params.arraySpread = 0.0f;
    for (uint32_t profile = 0u; profile < profiles.size(); ++profile) {
        params.substrate = profiles[profile];
        s3g::AccelerometerFieldEncoder engine;
        engine.prepare(kSampleRate);
        engine.setParams(params);
        signatures[profile].fundamentalHz = engine.modeFrequencyHz(0u);
        signatures[profile].fundamentalDecaySeconds =
            engine.modeDecaySeconds(0u);
        if (!std::isfinite(signatures[profile].fundamentalHz)
            || !(signatures[profile].fundamentalHz > 5.0f)
            || !std::isfinite(signatures[profile].fundamentalDecaySeconds)
            || !(signatures[profile].fundamentalDecaySeconds > 0.0f)) {
            std::cerr << "Public modal profile has an invalid fundamental or "
                      << "decay: " << profile << "\n";
            return false;
        }
        for (uint32_t mode = 0u;
            mode < signatures[profile].normalizedModes.size(); ++mode) {
            const float frequency = engine.modeFrequencyHz(mode);
            const float decaySeconds = engine.modeDecaySeconds(mode);
            if (!std::isfinite(frequency) || !(frequency > 0.0f)
                || !(frequency < static_cast<float>(kSampleRate) * 0.44f)
                || !std::isfinite(decaySeconds) || !(decaySeconds > 0.0f)) {
                std::cerr << "Public modal profile has an invalid mode or decay: "
                          << profile << ", " << mode << ", "
                          << frequency << ", " << decaySeconds << "\n";
                return false;
            }
            signatures[profile].normalizedModes[mode] = frequency
                / signatures[profile].fundamentalHz;
            signatures[profile].normalizedDecay[mode] = decaySeconds
                / signatures[profile].fundamentalDecaySeconds;
        }
    }
    for (uint32_t first = 0u; first < signatures.size(); ++first) {
        for (uint32_t second = first + 1u;
            second < signatures.size(); ++second) {
            double ratioDistance = 0.0;
            double decayShapeDistance = 0.0;
            for (uint32_t mode = 0u;
                mode < signatures[first].normalizedModes.size(); ++mode) {
                ratioDistance += std::fabs(
                    signatures[first].normalizedModes[mode]
                        - signatures[second].normalizedModes[mode]);
                decayShapeDistance += std::fabs(std::log(
                    signatures[first].normalizedDecay[mode]
                        / signatures[second].normalizedDecay[mode]));
            }
            const double registerDistance = std::fabs(std::log2(
                signatures[first].fundamentalHz
                    / signatures[second].fundamentalHz));
            const double absoluteDecayDistance = std::fabs(std::log(
                signatures[first].fundamentalDecaySeconds
                    / signatures[second].fundamentalDecaySeconds));
            if (!(ratioDistance > 0.01 || registerDistance > 0.02
                    || decayShapeDistance > 0.02
                    || absoluteDecayDistance > 0.02)) {
                std::cerr << "Two public modal profiles collapsed together: "
                          << first << ", " << second << "\n";
                return false;
            }
        }
    }

    // The appended presets are the renderer-facing audition path for the new
    // material and resonant-form profiles. Exercise a discrete MIDI actuation
    // as a complement to testFactoryPresets(), which covers their continuous
    // drive and sustained tail.
    constexpr std::array<s3g::AccelerometerSubstrate, 12u> newProfiles {{
        s3g::AccelerometerSubstrate::CarbonLaminate,
        s3g::AccelerometerSubstrate::GlassPlate,
        s3g::AccelerometerSubstrate::SteelShell,
        s3g::AccelerometerSubstrate::AluminumPlate,
        s3g::AccelerometerSubstrate::PorcelainShell,
        s3g::AccelerometerSubstrate::PorousEarthenware,
        s3g::AccelerometerSubstrate::SprucePlate,
        s3g::AccelerometerSubstrate::TensionedSkin,
        s3g::AccelerometerSubstrate::LoadedMembrane,
        s3g::AccelerometerSubstrate::CoupledMembrane,
        s3g::AccelerometerSubstrate::CavityMembrane,
        s3g::AccelerometerSubstrate::LooseMembrane,
    }};
    constexpr uint32_t strikeFrames = kSampleRate;
    for (uint32_t profile = 0u; profile < newProfiles.size(); ++profile) {
        auto strikeParams = s3g::accelerometerFieldFactoryPreset(13u + profile);
        if (strikeParams.substrate != newProfiles[profile]) {
            std::cerr << "Appended material preset order changed at index "
                      << 13u + profile << "\n";
            return false;
        }
        strikeParams.bodyCount = 4u;
        strikeParams.outputMode =
            s3g::AccelerometerFieldOutputMode::BodyStems;
        strikeParams.fieldListenMode = s3g::AmbiFieldListenMode::Off;
        strikeParams.fieldListenAmount = 0.0f;
        strikeParams.externalDrive = 0.0f;
        strikeParams.outputGainDb = -18.0f;
        strikeParams.modalLift = 0.0f;
        s3g::AccelerometerFieldEncoder engine;
        engine.prepare(kSampleRate);
        engine.setParams(strikeParams);
        engine.reset();
        engine.strikeMidi(60, 0.70f);
        const StemBuffers audio = processStems(engine, strikeFrames);
        const Metrics response = metrics(audio[0]);
        const Metrics tail = metrics(audio[0], strikeFrames * 3u / 4u);
        if (!response.finite || !tail.finite || audio[0].front() != 0.0f
            || !(response.energy > 1.0e-14) || !(response.peak > 1.0e-6f)
            || !(response.peak < 0.98f) || !(tail.energy > 1.0e-18)) {
            std::cerr << "New material/form preset did not produce a finite, "
                      << "bounded modal strike and tail: " << 13u + profile
                      << ", energy " << response.energy << ", tail "
                      << tail.energy << ", peak " << response.peak << "\n";
            return false;
        }
    }
    return true;
}

bool testSkinControlsDoNotSelfExcite()
{
    constexpr uint32_t frames = 24000u;
    auto params = s3g::accelerometerFieldFactoryPreset(0u);
    params.outputMode = s3g::AccelerometerFieldOutputMode::BodyStems;
    params.fieldListenMode = s3g::AmbiFieldListenMode::Off;
    params.fieldListenAmount = 1.0f;
    params.excitation = s3g::AccelerometerExcitation::Tremulation;
    params.eventRateHz = 80.0f;
    params.activity = 1.0f;
    params.force = 1.0f;
    params.texture = 1.0f;
    params.ambientDrive = 1.0f;
    params.sensorNoise = 1.0f;
    params.contactDetail = 1.0f;
    params.propagationLoss = 1.0f;
    params.externalDrive = 1.0f;

    s3g::AccelerometerFieldEncoder engine;
    engine.prepare(kSampleRate);
    engine.setParams(params);
    engine.reset();
    const StemBuffers audio = processStems(engine, frames, 0.25f);
    for (uint32_t body = 0u; body < params.bodyCount; ++body) {
        if (!std::all_of(audio[body].begin(), audio[body].end(),
                [](float sample) { return sample == 0.0f; })) {
            std::cerr << "A skin control or dormant transient field self-excited "
                      << "audio on "
                      << "body " << body << "\n";
            return false;
        }
    }
    return true;
}

bool testExternalActuatorIsModalOnly()
{
    constexpr uint32_t frames = kSampleRate * 4u;
    const auto factorySkin = s3g::accelerometerFieldFactoryPreset(0u);
    bool factoryHasPerBodyContact = false;
    for (uint32_t body = 1u; body < factorySkin.bodyCount; ++body) {
        factoryHasPerBodyContact = factoryHasPerBodyContact
            || std::fabs(factorySkin.bodySkinX[body]
                - factorySkin.bodySkinX[0u]) > 1.0e-6f
            || std::fabs(factorySkin.bodySkinY[body]
                - factorySkin.bodySkinY[0u]) > 1.0e-6f;
    }
    if (!factoryHasPerBodyContact) {
        std::cerr << "Factory Modal voice did not expose per-body skins\n";
        return false;
    }
    std::array<float, frames> input {};
    input[0] = 1.0f;
    const auto renderImpulse = [&](float contactDetail,
                                   float propagationLoss) {
        auto params = s3g::accelerometerFieldFactoryPreset(0u);
        params.outputMode = s3g::AccelerometerFieldOutputMode::BodyStems;
        params.fieldListenMode = s3g::AmbiFieldListenMode::Off;
        params.activity = 0.0f;
        params.ambientDrive = 0.0f;
        params.sensorNoise = 0.0f;
        params.externalDrive = 1.0f;
        params.bodySkinY.fill(contactDetail);
        params.propagationLoss = propagationLoss;
        s3g::AccelerometerFieldEncoder engine;
        engine.prepare(kSampleRate);
        engine.setParams(params);
        engine.reset();
        return processStems(engine, frames, 0.0f, input.data());
    };

    const StemBuffers neutral = renderImpulse(0.0f, 0.0f);
    const StemBuffers yOnly = renderImpulse(1.0f, 0.0f);
    const StemBuffers distributed = renderImpulse(1.0f, 1.0f);
    if (neutral != yOnly) {
        std::cerr << "Skin Y changed the exact point-skin compatibility path\n";
        return false;
    }
    if (!(difference(neutral[0], distributed[0]) > 1.0e-7)
        || !metrics(distributed[0]).finite) {
        std::cerr << "Distributed Skin X/Y/Extent did not reshape the modal "
                  << "actuator response\n";
        return false;
    }

    s3g::AccelerometerFieldEncoder geometry;
    geometry.prepare(kSampleRate);
    auto geometryParams = s3g::accelerometerFieldFactoryPreset(20u);
    geometryParams.propagationLoss = 0.0f;
    geometry.setParams(geometryParams);
    const s3g::Vec3 center = geometry.bodyDirection(0u);
    for (uint32_t patch = 0u;
        patch < s3g::kAccelerometerFieldSkinPatchCount; ++patch) {
        if (directionDistance(center,
                geometry.skinPatchDirection(0u, patch)) > 1.0e-6f) {
            std::cerr << "Zero-extent modal skin did not collapse to its body\n";
            return false;
        }
    }
    geometryParams.propagationLoss = 1.0f;
    geometry.setParams(geometryParams);
    float maximumPatchDistance = 0.0f;
    for (uint32_t patch = 0u;
        patch < s3g::kAccelerometerFieldSkinPatchCount; ++patch) {
        maximumPatchDistance = std::max(maximumPatchDistance,
            directionDistance(geometry.bodyDirection(0u),
                geometry.skinPatchDirection(0u, patch)));
    }
    if (!(maximumPatchDistance > 0.10f)) {
        std::cerr << "Modal skin extent did not distribute its HOA patches\n";
        return false;
    }

    // One body's skin edit must retarget only that body's modal drive. This
    // catches accidental regressions back to a shared contact surface.
    geometryParams.bodySkinX[0u] = 0.14f;
    geometryParams.bodySkinY[0u] = 0.24f;
    geometryParams.bodySkinX[1u] = 0.82f;
    geometryParams.bodySkinY[1u] = 0.76f;
    geometry.setParams(geometryParams);
    std::array<float, 8u> body0Before {};
    std::array<float, 8u> body1Before {};
    for (uint32_t mode = 0u; mode < body0Before.size(); ++mode) {
        body0Before[mode] = geometry.modeDriveWeight(mode, 0u);
        body1Before[mode] = geometry.modeDriveWeight(mode, 1u);
    }
    geometryParams.bodySkinX[1u] = 0.31f;
    geometryParams.bodySkinY[1u] = 0.18f;
    geometry.setParams(geometryParams);
    double unchangedBodyDifference = 0.0;
    double editedBodyDifference = 0.0;
    for (uint32_t mode = 0u; mode < body0Before.size(); ++mode) {
        unchangedBodyDifference += std::fabs(
            geometry.modeDriveWeight(mode, 0u) - body0Before[mode]);
        editedBodyDifference += std::fabs(
            geometry.modeDriveWeight(mode, 1u) - body1Before[mode]);
    }
    if (unchangedBodyDifference > 1.0e-8
        || !(editedBodyDifference > 1.0e-5)) {
        std::cerr << "Per-body modal skin edit leaked or failed to retarget "
                  << "its body's modes\n";
        return false;
    }

    const uint32_t body = 0u;
    const Metrics response = metrics(neutral[body]);
    const Metrics earlyTail = metrics(
        neutral[body], 256u, kSampleRate);
    const Metrics lateTail = metrics(
        neutral[body], kSampleRate * 3u, frames);
    if (!response.finite || !(response.energy > 1.0e-12)
        || !(std::fabs(neutral[body][0]) < 1.0e-3f)
        || !(earlyTail.energy > 1.0e-13)
        || !(lateTail.energy > 1.0e-14)
        || !(lateTail.energy < earlyTail.energy)) {
        std::cerr << "External actuator was not a smooth, decaying modal tail: "
                  << neutral[body][0] << ", " << response.energy << ", "
                  << earlyTail.energy << ", " << lateTail.energy << "\n";
        return false;
    }
    return true;
}

bool testPerBodyMidiStrike()
{
    constexpr uint32_t frames = kSampleRate;
    auto params = s3g::accelerometerFieldFactoryPreset(0u);
    params.bodyCount = 4u;
    params.activity = 0.0f;
    params.ambientDrive = 0.0f;
    params.sensorNoise = 0.0f;
    params.externalDrive = 0.0f;
    params.fieldListenMode = s3g::AmbiFieldListenMode::Off;
    params.outputMode = s3g::AccelerometerFieldOutputMode::BodyStems;

    s3g::AccelerometerFieldEncoder engine;
    engine.prepare(kSampleRate);
    engine.setParams(params);
    engine.reset();
    const float reference = engine.modeFrequencyHz(0u);
    std::array<uint32_t, s3g::kAccelerometerFieldMaxBodyCount> modeCounts {};
    std::array<float, s3g::kAccelerometerFieldMaxBodyCount> positions {};
    std::array<s3g::Vec3, s3g::kAccelerometerFieldMaxBodyCount> directions {};
    for (uint32_t body = 0u; body < params.bodyCount; ++body) {
        modeCounts[body] = engine.modesForBody(body);
        positions[body] = engine.bodyPosition(body);
        directions[body] = engine.bodyDirection(body);
    }

    engine.strikeMidi(72, 0.82f);
    const float transposed = engine.modeFrequencyHz(0u);
    if (engine.lastActuatedBody() != 0u
        || !(transposed / reference > 1.999f
            && transposed / reference < 2.001f)) {
        std::cerr << "First MIDI strike did not select and transpose one body\n";
        return false;
    }
    const StemBuffers firstStrike = processStems(engine, frames);
    const Metrics selected = metrics(firstStrike[0]);
    const Metrics ringingTail = metrics(firstStrike[0], 30000u);
    if (!selected.finite || !ringingTail.finite
        || firstStrike[0][0] != 0.0f
        || !(selected.energy > 1.0e-12)
        || !(ringingTail.energy > 1.0e-13)) {
        std::cerr << "MIDI actuation was not a zero-start modal pulse with "
                  << "a ringing tail: " << firstStrike[0][0] << ", "
                  << selected.energy << ", " << ringingTail.energy << "\n";
        return false;
    }
    for (uint32_t body = 1u; body < params.bodyCount; ++body) {
        if (metrics(firstStrike[body]).energy != 0.0) {
            std::cerr << "One MIDI strike leaked into an unselected body\n";
            return false;
        }
    }

    const float retainedEnergy = engine.bodyEnergy(0u);
    engine.strikeMidi(48, 0.72f);
    if (engine.lastActuatedBody() != 1u
        || std::fabs(engine.modeFrequencyHz(0u) - transposed) > 1.0e-5f
        || std::fabs(engine.bodyEnergy(0u) - retainedEnergy) > 1.0e-9f) {
        std::cerr << "Second MIDI strike rebuilt or retuned the first body\n";
        return false;
    }
    for (uint32_t body = 0u; body < params.bodyCount; ++body) {
        if (engine.modesForBody(body) != modeCounts[body]
            || std::fabs(engine.bodyPosition(body) - positions[body]) > 1.0e-7f
            || directionDistance(engine.bodyDirection(body), directions[body])
                > 1.0e-7f) {
            std::cerr << "MIDI strike rebuilt the body allocation or geometry\n";
            return false;
        }
    }
    engine.reset();
    if (std::fabs(engine.performancePitchRatio() - 1.0f) > 1.0e-6f
        || std::fabs(engine.modeFrequencyHz(0u) - reference) > 1.0e-3f) {
        std::cerr << "Per-body MIDI pitch survived an engine reset\n";
        return false;
    }
    return true;
}

bool testRepeatedHighVelocityMidiRemainsBounded()
{
    constexpr uint32_t blockFrames = 256u;
    constexpr uint32_t totalFrames = kSampleRate * 10u;
    constexpr std::array<int32_t, s3g::kAccelerometerFieldMaxBodyCount>
        notes {{ 36, 48, 60, 72, 84, 96, 108, 127 }};
    auto params = s3g::accelerometerFieldFactoryPreset(10u);
    params.substrate = s3g::AccelerometerSubstrate::BroadBronze;
    params.bodyCount = s3g::kAccelerometerFieldMaxBodyCount;
    params.ambisonicOrder = s3g::kAccelerometerFieldMaxOrder;
    params.outputMode = s3g::AccelerometerFieldOutputMode::Ambisonic;
    params.activity = 0.0f;
    params.ambientDrive = 0.0f;
    params.externalDrive = 0.0f;
    params.sensorNoise = 0.0f;
    params.fieldListenMode = s3g::AmbiFieldListenMode::Off;
    params.fieldListenAmount = 0.0f;
    params.outputGainDb = 12.0f;
    params.modalLift = 1.0f;
    params.damping = 0.0f;

    s3g::AccelerometerFieldEncoder engine;
    engine.prepare(kSampleRate);
    engine.setParams(params);
    engine.reset();

    std::array<std::array<float, blockFrames>,
        s3g::kAccelerometerFieldMaxChannels> audio {};
    std::array<float*, s3g::kAccelerometerFieldMaxChannels> outputs {};
    for (uint32_t channel = 0u; channel < audio.size(); ++channel) {
        outputs[channel] = audio[channel].data();
    }

    uint32_t strikeCount = 0u;
    double energy = 0.0;
    float peak = 0.0f;
    bool finite = true;
    float minimumGuardGain = 1.0f;
    for (uint32_t firstFrame = 0u; firstFrame < totalFrames;
        firstFrame += blockFrames) {
        // One strike every 512 samples is dense enough to accumulate modal
        // energy while still allowing each body's smooth pulse to advance.
        if ((firstFrame / blockFrames) % 2u == 0u) {
            engine.strikeMidi(notes[strikeCount % notes.size()], 1.0f);
            ++strikeCount;
        }
        engine.process(nullptr, outputs.data(), outputs.size(), blockFrames);
        minimumGuardGain = std::min(
            minimumGuardGain, engine.currentOutputGuardGain());
        for (const auto& channel : audio) {
            const Metrics channelMetrics = metrics(channel);
            finite = finite && channelMetrics.finite;
            peak = std::max(peak, channelMetrics.peak);
            energy += channelMetrics.energy;
        }
    }

    if (!finite || strikeCount < 900u || !(energy > 1.0e-5)
        || !(peak > 0.01f) || !(minimumGuardGain > 0.05f)
        || peak > kLinkedOutputCeiling + 2.0e-4f) {
        std::cerr << "Dense high-velocity MIDI was silent, non-finite, or "
                  << "escaped the linked output bound: strikes "
                  << strikeCount << ", energy " << energy << ", peak "
                  << peak << ", minimum guard " << minimumGuardGain
                  << "\n";
        return false;
    }
    return true;
}

bool testOrdinaryMidiResponseSurvivesStressAndReset()
{
    constexpr uint32_t frames = kSampleRate;
    constexpr uint32_t stressBlockFrames = 256u;
    auto params = s3g::accelerometerFieldFactoryPreset(0u);
    params.bodyCount = 4u;
    params.outputMode = s3g::AccelerometerFieldOutputMode::BodyStems;
    params.activity = 0.0f;
    params.ambientDrive = 0.0f;
    params.externalDrive = 0.0f;
    params.sensorNoise = 0.0f;
    params.fieldListenMode = s3g::AmbiFieldListenMode::Off;
    params.fieldListenAmount = 0.0f;
    params.outputGainDb = -18.0f;
    params.modalLift = 0.0f;

    const auto renderNote = [&](float velocity, bool stressBeforeReset) {
        s3g::AccelerometerFieldEncoder engine;
        engine.prepare(kSampleRate);
        engine.setParams(params);
        engine.reset();
        if (stressBeforeReset) {
            std::array<std::array<float, stressBlockFrames>,
                s3g::kAccelerometerFieldMaxChannels> stressAudio {};
            std::array<float*, s3g::kAccelerometerFieldMaxChannels>
                stressOutputs {};
            for (uint32_t channel = 0u;
                channel < stressAudio.size(); ++channel) {
                stressOutputs[channel] = stressAudio[channel].data();
            }
            for (uint32_t strike = 0u; strike < 128u; ++strike) {
                engine.strikeMidi(36 + static_cast<int32_t>(strike % 8u) * 12,
                    1.0f);
                engine.process(nullptr, stressOutputs.data(),
                    stressOutputs.size(), stressBlockFrames);
            }
            engine.reset();
        }
        engine.strikeMidi(60, velocity);
        return processStems(engine, frames)[0];
    };

    const std::vector<float> quiet = renderNote(0.35f, false);
    const std::vector<float> ordinary = renderNote(0.70f, false);
    const std::vector<float> recovered = renderNote(0.70f, true);
    const Metrics quietMetrics = metrics(quiet);
    const Metrics ordinaryMetrics = metrics(ordinary);
    const Metrics recoveredMetrics = metrics(recovered);
    const double resetDifference = difference(ordinary, recovered);

    if (!quietMetrics.finite || !ordinaryMetrics.finite
        || !recoveredMetrics.finite
        || quiet.front() != 0.0f || ordinary.front() != 0.0f
        || !(quietMetrics.energy > 1.0e-13)
        || !(ordinaryMetrics.energy > quietMetrics.energy * 1.15)
        || !(ordinaryMetrics.peak > quietMetrics.peak * 1.08f)
        || resetDifference > 1.0e-9) {
        std::cerr << "Ordinary MIDI response was muted, flattened, altered, "
                  << "or not restored by reset: energies "
                  << quietMetrics.energy << ", " << ordinaryMetrics.energy
                  << ", peaks " << quietMetrics.peak << ", "
                  << ordinaryMetrics.peak << ", reset difference "
                  << resetDifference << "\n";
        return false;
    }
    return true;
}

bool testListenerActiveCoincidentMidiUsesIdleBodies()
{
    auto params = s3g::accelerometerFieldFactoryPreset(10u);
    params.bodyCount = s3g::kAccelerometerFieldMaxBodyCount;
    params.activity = 0.0f;
    params.ambientDrive = 0.0f;
    params.externalDrive = 0.0f;
    params.sensorNoise = 0.0f;
    params.fieldListenMode = s3g::AmbiFieldListenMode::Balance;
    params.fieldListenAmount = 1.0f;

    s3g::AccelerometerFieldEncoder engine;
    engine.prepare(kSampleRate);
    engine.setParams(params);
    engine.reset();

    std::array<bool, s3g::kAccelerometerFieldMaxBodyCount> selected {};
    for (uint32_t strike = 0u; strike < params.bodyCount; ++strike) {
        engine.strikeMidi(48 + static_cast<int32_t>(strike) * 5, 1.0f);
        const uint32_t body = engine.lastActuatedBody();
        if (body >= params.bodyCount || selected[body]) {
            std::cerr << "Listener-active coincident MIDI reused body "
                      << body << " before all idle bodies were assigned\n";
            return false;
        }
        selected[body] = true;
    }

    const float filledPitchRatio = engine.performancePitchRatio();
    engine.strikeMidi(24, 1.0f);
    if (engine.performancePitchRatio() != filledPitchRatio) {
        std::cerr << "A ninth coincident full-velocity note escaped the "
                  << "per-body MIDI energy reservoirs\n";
        return false;
    }

    // A quarter-second returns enough of the two-second leaky reservoir to
    // admit a useful retrigger, without requiring the modal tail to end.
    constexpr uint32_t recoveryFrames = kSampleRate / 4u;
    processStems(engine, recoveryFrames);
    engine.strikeMidi(96, 1.0f);
    if (std::fabs(engine.performancePitchRatio() - filledPitchRatio) < 0.1f
        || std::fabs(engine.performancePitchRatio() - 8.0f) > 1.0e-5f) {
        std::cerr << "MIDI energy reservoirs did not recover enough to admit "
                  << "a note after 250 ms: "
                  << engine.performancePitchRatio() << "\n";
        return false;
    }
    return true;
}

bool testContinuousBronzeActuatorIsSmoothAndStable()
{
    constexpr uint32_t blockFrames = 256u;
    struct Result {
        double rms = 0.0;
        double normalizedDifference = 0.0;
        float peak = 0.0f;
        float minimumGuardGain = 1.0f;
        bool finite = true;
    };
    const auto renderAt = [](uint32_t sampleRate) {
        auto params = s3g::accelerometerFieldFactoryPreset(0u);
        params.outputMode =
            s3g::AccelerometerFieldOutputMode::Ambisonic;
        params.fieldListenMode = s3g::AmbiFieldListenMode::Balance;
        params.fieldListenAmount = 0.64f;
        params.fieldListenResponse =
            s3g::AmbiFieldListenerResponse::Imprint;
        params.coupling = 0.0f;
        params.energy = 0.0f;
        params.externalDrive = 0.0f;
        params.modalLift = 0.0f;
        params.outputGainDb = 0.0f;

        s3g::AccelerometerFieldEncoder engine;
        engine.prepare(sampleRate);
        engine.setParams(params);
        engine.reset();

        std::array<float, blockFrames> w {};
        std::array<float*, s3g::kAccelerometerFieldMaxChannels> outputs {};
        outputs[0] = w.data();
        constexpr uint32_t warmSeconds = 4u;
        constexpr uint32_t measuredSeconds = 8u;
        const uint32_t warmFrames = sampleRate * warmSeconds;
        const uint32_t totalFrames = sampleRate
            * (warmSeconds + measuredSeconds);
        double energy = 0.0;
        double differenceEnergy = 0.0;
        uint64_t measuredFrames = 0u;
        float previous = 0.0f;
        bool hasPrevious = false;
        Result result;
        for (uint32_t firstFrame = 0u; firstFrame < totalFrames;
            firstFrame += blockFrames) {
            engine.process(nullptr, outputs.data(), outputs.size(),
                blockFrames);
            if (firstFrame < warmFrames) continue;
            result.minimumGuardGain = std::min(
                result.minimumGuardGain, engine.currentOutputGuardGain());
            for (const float sample : w) {
                result.finite = result.finite && std::isfinite(sample);
                result.peak = std::max(result.peak, std::fabs(sample));
                energy += static_cast<double>(sample) * sample;
                if (hasPrevious) {
                    const double delta = static_cast<double>(sample)
                        - previous;
                    differenceEnergy += delta * delta;
                }
                previous = sample;
                hasPrevious = true;
                ++measuredFrames;
            }
        }
        result.rms = std::sqrt(energy
            / static_cast<double>(std::max<uint64_t>(1u, measuredFrames)));
        result.normalizedDifference = std::sqrt(differenceEnergy
            / std::max(1.0e-30, energy));
        return result;
    };

    const Result at48k = renderAt(48000u);
    const Result at96k = renderAt(96000u);
    if (!at48k.finite || !(at48k.rms > 0.008)
        || !(at48k.rms < 0.08) || !(at48k.peak < 0.25f)
        || !(at48k.normalizedDifference < 0.017)
        || !(at48k.minimumGuardGain > 0.999f)) {
        std::cerr << "Continuous bronze actuator lost its drone or retained "
                  << "the fast snare-like stochastic edge at 48 kHz: RMS "
                  << at48k.rms << ", normalized difference "
                  << at48k.normalizedDifference << ", peak " << at48k.peak
                  << ", guard " << at48k.minimumGuardGain << "\n";
        return false;
    }
    if (!at96k.finite || !(at96k.rms > 0.008)
        || !(at96k.rms < 0.10) || !(at96k.peak < 0.50f)
        || !(at96k.normalizedDifference < 0.014)
        || !(at96k.minimumGuardGain > 0.999f)) {
        std::cerr << "Physical-time continuous actuator was silent, rough, "
                  << "or unstable at 96 kHz: RMS " << at96k.rms
                  << ", normalized difference "
                  << at96k.normalizedDifference << ", peak " << at96k.peak
                  << ", guard " << at96k.minimumGuardGain << "\n";
        return false;
    }
    return true;
}

bool testModalCouplingEvolution()
{
    Buffer uncoupled {};
    Buffer coupled {};
    Buffer companion {};
    auto params = s3g::accelerometerFieldFactoryPreset(0u);
    params.energy = 0.0f;
    params.coupling = 0.0f;
    render(params, uncoupled, companion);
    params.coupling = 1.0f;
    render(params, coupled, companion);
    if (!(difference(uncoupled, coupled) > 1.0e-5)) {
        std::cerr << "Modal coupling did not animate paired modes\n";
        return false;
    }
    return true;
}

bool testZeroPlayerDepthPreservesOpenLoop()
{
    Buffer open {};
    Buffer zeroDepth {};
    Buffer companion {};
    auto params = s3g::accelerometerFieldFactoryPreset(4u);
    params.fieldListenMode = s3g::AmbiFieldListenMode::Off;
    render(params, open, companion);
    params.fieldListenMode = s3g::AmbiFieldListenMode::Balance;
    params.fieldListenAmount = 0.0f;
    params.fieldListenResponse = s3g::AmbiFieldListenerResponse::Imprint;
    render(params, zeroDepth, companion);
    if (open != zeroDepth) {
        std::cerr << "Zero player depth changed the authored open-loop field\n";
        return false;
    }
    return true;
}

bool testOutputAndLiftDoNotDriveTelemetry()
{
    constexpr uint32_t frames = kSampleRate * 2u;
    struct Result {
        std::vector<float> w;
        std::array<float, s3g::kAccelerometerFieldMaxBodyCount> bodyEnergy {};
        std::array<float, s3g::kAccelerometerFieldMaxBodyCount> bodyDrive {};
        std::array<float, s3g::kAmbiFieldListenerMaxLobes> pickupEnergy {};
        float listenerActivity = 0.0f;
        float fieldListenerActivity = 0.0f;
        float actuatorActivity = 0.0f;
        uint32_t pickupCount = 0u;
    };
    const auto renderTelemetry = [](const auto& params) {
        Result result;
        result.w.assign(frames, 0.0f);
        s3g::AccelerometerFieldEncoder engine;
        engine.prepare(kSampleRate);
        engine.setParams(params);
        engine.reset();
        std::array<float*, s3g::kAccelerometerFieldMaxChannels> outputs {};
        outputs[0] = result.w.data();
        engine.process(nullptr, outputs.data(), outputs.size(), frames);
        result.listenerActivity = engine.listenerActivity();
        result.fieldListenerActivity = engine.fieldListenerActivity();
        result.actuatorActivity = engine.actuatorActivity();
        result.pickupCount = engine.listenerPickupCount();
        for (uint32_t body = 0u; body < result.bodyEnergy.size(); ++body) {
            result.bodyEnergy[body] = engine.bodyEnergy(body);
            result.bodyDrive[body] = engine.actuatorBodyDrive(body);
        }
        for (uint32_t pickup = 0u;
            pickup < result.pickupEnergy.size(); ++pickup) {
            result.pickupEnergy[pickup] = engine.listenerPickupEnergy(pickup);
        }
        return result;
    };
    const auto telemetryDelta = [](const Result& first,
                                    const Result& second) {
        double delta = std::max({
            std::fabs(static_cast<double>(first.listenerActivity)
                - second.listenerActivity),
            std::fabs(static_cast<double>(first.fieldListenerActivity)
                - second.fieldListenerActivity),
            std::fabs(static_cast<double>(first.actuatorActivity)
                - second.actuatorActivity),
        });
        for (uint32_t body = 0u; body < first.bodyEnergy.size(); ++body) {
            delta = std::max(delta, std::fabs(
                static_cast<double>(first.bodyEnergy[body])
                    - second.bodyEnergy[body]));
            delta = std::max(delta, std::fabs(
                static_cast<double>(first.bodyDrive[body])
                    - second.bodyDrive[body]));
        }
        for (uint32_t pickup = 0u;
            pickup < first.pickupEnergy.size(); ++pickup) {
            delta = std::max(delta, std::fabs(
                static_cast<double>(first.pickupEnergy[pickup])
                    - second.pickupEnergy[pickup]));
        }
        return delta;
    };

    auto params = s3g::accelerometerFieldFactoryPreset(10u);
    params.bodyCount = 8u;
    params.outputMode = s3g::AccelerometerFieldOutputMode::Ambisonic;
    params.listenerPickupSet =
        s3g::AccelerometerFieldListenerPickupSet::Cube8;
    params.fieldListenMode = s3g::AmbiFieldListenMode::Balance;
    params.fieldListenAmount = 0.82f;
    params.outputGainDb = -30.0f;
    params.modalLift = 0.0f;
    const Result quiet = renderTelemetry(params);
    params.outputGainDb = 12.0f;
    const Result loud = renderTelemetry(params);
    params.outputGainDb = -30.0f;
    params.modalLift = 1.0f;
    const Result lifted = renderTelemetry(params);

    const double outputDelta = telemetryDelta(quiet, loud);
    const double liftDelta = telemetryDelta(quiet, lifted);
    const double liftEnergyRatio = metrics(lifted.w).energy
        / std::max(1.0e-30, metrics(quiet.w).energy);
    if (quiet.pickupCount != loud.pickupCount
        || quiet.pickupCount != lifted.pickupCount
        || outputDelta > 5.0e-7 || liftDelta > 5.0e-7
        || !(liftEnergyRatio > 1.05)) {
        std::cerr << "OUT or modal lift leaked into Listener/body telemetry: "
                  << outputDelta << ", " << liftDelta
                  << ", lift energy ratio " << liftEnergyRatio << "\n";
        return false;
    }

    auto stemParams = balancedDroneParams(6u);
    stemParams.outputGainDb = -24.0f;
    stemParams.modalLift = 0.0f;
    s3g::AccelerometerFieldEncoder unliftedEngine;
    unliftedEngine.prepare(kSampleRate);
    unliftedEngine.setParams(stemParams);
    unliftedEngine.reset();
    const StemBuffers unlifted = processStems(
        unliftedEngine, kSampleRate / 2u);
    stemParams.modalLift = 1.0f;
    s3g::AccelerometerFieldEncoder liftedEngine;
    liftedEngine.prepare(kSampleRate);
    liftedEngine.setParams(stemParams);
    liftedEngine.reset();
    const StemBuffers liftedStems = processStems(
        liftedEngine, kSampleRate / 2u);
    if (unlifted != liftedStems) {
        std::cerr << "Modal lift changed the raw body-stem contract\n";
        return false;
    }
    return true;
}

bool testOutputGainIsTransparentBelowGuard()
{
    const auto renderDrivenField = [](auto params) {
        Buffer w {};
        s3g::AccelerometerFieldEncoder engine;
        engine.prepare(kSampleRate);
        engine.setParams(params);
        engine.reset();
        for (uint32_t body = 0u; body < params.bodyCount; ++body) {
            engine.strikeMidi(60, 0.60f);
        }
        std::array<float*, s3g::kAccelerometerFieldMaxChannels> outputs {};
        outputs[0] = w.data();
        engine.process(nullptr, outputs.data(), outputs.size(), kFrames);
        return w;
    };

    auto params = s3g::accelerometerFieldFactoryPreset(10u);
    params.bodyCount = 8u;
    params.outputMode = s3g::AccelerometerFieldOutputMode::Ambisonic;
    params.fieldListenMode = s3g::AmbiFieldListenMode::Off;
    params.fieldListenAmount = 0.0f;
    params.bodyDistance.fill(1.0f);
    params.modalLift = 0.0f;
    params.outputGainDb = -30.0f;
    const Buffer quiet = renderDrivenField(params);
    params.outputGainDb = -18.0f;
    const Buffer raised = renderDrivenField(params);

    const ScaleFit fit = fitScale(quiet, raised, kSampleRate, kFrames);
    const double expected = std::pow(10.0, 12.0 / 20.0);
    const double gainErrorDb = 20.0 * std::log10(
        std::max(1.0e-30, fit.scale / expected));
    const Metrics raisedMetrics = metrics(raised, kSampleRate, kFrames);
    if (!raisedMetrics.finite || !(raisedMetrics.energy > 1.0e-12)
        || !(raisedMetrics.peak < kLinkedOutputCeiling * 0.75f)
        || std::fabs(gainErrorDb) > 0.05
        || !(fit.normalizedError < 0.001)) {
        std::cerr << "Post-encoder OUT was nonlinear below the guard: gain error "
                  << gainErrorDb << " dB, NRMSE " << fit.normalizedError
                  << ", peak " << raisedMetrics.peak << "\n";
        return false;
    }
    return true;
}

bool testLiveOutputGainIsSmoothed()
{
    constexpr uint32_t warmFrames = kSampleRate * 3u / 4u;
    constexpr uint32_t changedFrames = kSampleRate / 4u;
    struct GainChange {
        std::vector<float> samples;
        float previousSample = 0.0f;
    };
    const auto renderChange = [](float nextGainDb) {
        auto params = s3g::accelerometerFieldFactoryPreset(10u);
        params.bodyCount = 8u;
        params.outputMode = s3g::AccelerometerFieldOutputMode::Ambisonic;
        params.fieldListenMode = s3g::AmbiFieldListenMode::Off;
        params.fieldListenAmount = 0.0f;
        params.bodyDistance.fill(1.0f);
        params.modalLift = 0.0f;
        params.outputGainDb = -24.0f;

        s3g::AccelerometerFieldEncoder engine;
        engine.prepare(kSampleRate);
        engine.setParams(params);
        engine.reset();
        for (uint32_t body = 0u; body < params.bodyCount; ++body) {
            engine.strikeMidi(60, 0.60f);
        }
        std::vector<float> warm(warmFrames, 0.0f);
        std::array<float*, s3g::kAccelerometerFieldMaxChannels> outputs {};
        outputs[0] = warm.data();
        engine.process(nullptr, outputs.data(), outputs.size(), warmFrames);

        params.outputGainDb = nextGainDb;
        engine.setParams(params);
        GainChange result;
        result.previousSample = warm.back();
        result.samples.assign(changedFrames, 0.0f);
        outputs[0] = result.samples.data();
        engine.process(nullptr, outputs.data(), outputs.size(), changedFrames);
        return result;
    };

    const GainChange reference = renderChange(-24.0f);
    const GainChange raised = renderChange(-12.0f);
    const Metrics referenceStart = metrics(reference.samples, 0u, 256u);
    const double boundaryExcess = std::fabs(
        static_cast<double>(raised.samples.front())
            - reference.samples.front());
    const ScaleFit early = fitScale(
        reference.samples, raised.samples, 0u, 16u);
    const ScaleFit settled = fitScale(reference.samples, raised.samples,
        changedFrames - 2048u, changedFrames);
    const double expected = std::pow(10.0, 12.0 / 20.0);
    const double settledErrorDb = 20.0 * std::log10(
        std::max(1.0e-30, settled.scale / expected));
    if (reference.previousSample != raised.previousSample
        || !metrics(reference.samples).finite || !metrics(raised.samples).finite
        || !(referenceStart.peak > 1.0e-7f)
        || boundaryExcess > std::max(2.0e-7,
            static_cast<double>(referenceStart.peak) * 0.04)
        || !(early.scale > 0.98 && early.scale < 1.10)
        || std::fabs(settledErrorDb) > 0.12
        || !(settled.normalizedError < 0.002)) {
        std::cerr << "Live OUT change clicked or failed to settle smoothly: "
                  << boundaryExcess << ", early scale " << early.scale
                  << ", settled error " << settledErrorDb
                  << " dB, NRMSE " << settled.normalizedError << "\n";
        return false;
    }
    return true;
}

bool testVisibleContinuousControlsAreClickFree()
{
    // Exercise every continuous scalar exposed by the compact editor. The
    // per-body AED controls use the same geometry smoother already covered by
    // testBodyGeometrySmoothing() and testPerBodyAedEditing(); menu parameters
    // are deliberately excluded because they represent discrete topology or
    // routing changes rather than slider automation.
    using Params = s3g::AccelerometerFieldParams;
    struct Control {
        const char* name;
        float Params::*member;
        float initial;
        float target;
        uint32_t bodySkinCoordinate = 0u; // 1 = X, 2 = Y
    };
    constexpr std::array<Control, 19u> controls {{
        { "Actuator input", &Params::externalDrive, 0.12f, 0.78f },
        { "Size", &Params::size, 0.68f, 0.30f },
        { "Damping", &Params::damping, 0.32f, 0.78f },
        { "Irregularity", &Params::irregularity, 0.08f, 0.70f },
        { "Skin X", nullptr, 0.15f, 0.85f, 1u },
        { "Skin Y", nullptr, 0.20f, 0.80f, 2u },
        { "Skin extent", &Params::propagationLoss, 0.10f, 0.90f },
        { "Tone center", &Params::pickupPosition, 0.20f, 0.80f },
        { "Modal angle", &Params::pickupAxis, 0.15f, 0.85f },
        { "Air radiation", &Params::airRadiation, 0.10f, 0.90f },
        { "Contact / radiation", &Params::contactRadiation, 0.15f, 0.85f },
        { "Body spread", &Params::spatialExtent, 0.25f, 0.90f },
        { "Field azimuth", &Params::fieldAzimuthDeg, -40.0f, 120.0f },
        { "Field elevation", &Params::fieldElevationDeg, -25.0f, 45.0f },
        { "Output gain", &Params::outputGainDb, -18.0f, -8.0f },
        { "Body variation", &Params::arraySpread, 0.10f, 0.90f },
        { "Actuator drive", &Params::fieldListenAmount, 0.35f, 0.85f },
        { "Modal coupling", &Params::coupling, 0.15f, 0.85f },
        { "Modal lift", &Params::modalLift, 0.10f, 0.90f },
    }};

    constexpr uint32_t blockFrames = 64u;
    constexpr uint32_t warmFrames = kSampleRate / 4u;
    constexpr uint32_t changedFrames = kSampleRate / 2u;
    constexpr uint32_t onsetFrames = kSampleRate / 500u; // first 2 ms
    constexpr uint32_t lateStartFrame = kSampleRate / 8u;
    constexpr uint32_t channels = 4u; // first-order ACN/SN3D
    constexpr double twoPi = 6.283185307179586476925286766559;
    bool passed = true;

    for (const Control& control : controls) {
        const auto setControl = [&control](Params& target, float value) {
            if (control.bodySkinCoordinate == 1u) {
                target.bodySkinX[0u] = value;
                target.sourcePosition = value;
            } else if (control.bodySkinCoordinate == 2u) {
                target.bodySkinY[0u] = value;
                target.contactDetail = value;
            } else {
                target.*(control.member) = value;
            }
        };
        auto params = s3g::accelerometerFieldFactoryPreset(10u);
        params.bodyCount = 6u;
        params.ambisonicOrder = 1u;
        params.outputMode =
            s3g::AccelerometerFieldOutputMode::Ambisonic;
        params.fieldListenMode = s3g::AmbiFieldListenMode::Balance;
        params.fieldListenResponse =
            s3g::AmbiFieldListenerResponse::Imprint;
        params.sensorNoise = 0.0f;
        params.outputGainDb = -18.0f;
        params.modalLift = 0.10f;
        params.externalDrive = 0.12f;
        if (control.bodySkinCoordinate == 2u) {
            params.propagationLoss = 0.65f;
        }
        setControl(params, control.initial);

        s3g::AccelerometerFieldEncoder reference;
        s3g::AccelerometerFieldEncoder changed;
        reference.prepare(kSampleRate);
        changed.prepare(kSampleRate);
        reference.setParams(params);
        changed.setParams(params);
        reference.reset();
        changed.reset();
        const float initialModeFrequency = changed.modeFrequencyHz(0u);
        const float initialModeDecay = changed.modeDecaySeconds(0u);

        std::array<std::array<float, blockFrames>, channels>
            referenceAudio {};
        std::array<std::array<float, blockFrames>, channels>
            changedAudio {};
        std::array<float*, s3g::kAccelerometerFieldMaxChannels>
            referenceOutputs {};
        std::array<float*, s3g::kAccelerometerFieldMaxChannels>
            changedOutputs {};
        for (uint32_t channel = 0u; channel < channels; ++channel) {
            referenceOutputs[channel] = referenceAudio[channel].data();
            changedOutputs[channel] = changedAudio[channel].data();
        }
        std::array<float, blockFrames> external {};
        std::array<float, channels> previousReference {};
        std::array<float, channels> previousChanged {};
        double preDerivativeEnergy = 0.0;
        uint64_t preDerivativeCount = 0u;
        double warmDifferencePeak = 0.0;

        for (uint32_t firstFrame = 0u; firstFrame < warmFrames;
            firstFrame += blockFrames) {
            const uint32_t framesThisBlock = std::min(
                blockFrames, warmFrames - firstFrame);
            for (uint32_t frame = 0u; frame < framesThisBlock; ++frame) {
                const double phase = twoPi * 73.0
                    * static_cast<double>(firstFrame + frame)
                    / static_cast<double>(kSampleRate);
                external[frame] = 0.16f * static_cast<float>(std::sin(phase));
            }
            reference.process(external.data(), referenceOutputs.data(),
                channels, framesThisBlock);
            changed.process(external.data(), changedOutputs.data(),
                channels, framesThisBlock);
            for (uint32_t frame = 0u; frame < framesThisBlock; ++frame) {
                for (uint32_t channel = 0u; channel < channels; ++channel) {
                    const float ref = referenceAudio[channel][frame];
                    const float value = changedAudio[channel][frame];
                    warmDifferencePeak = std::max(warmDifferencePeak,
                        std::fabs(static_cast<double>(value) - ref));
                    if (firstFrame + frame >= warmFrames / 2u) {
                        const double derivative = static_cast<double>(ref)
                            - previousReference[channel];
                        preDerivativeEnergy += derivative * derivative;
                        ++preDerivativeCount;
                    }
                    previousReference[channel] = ref;
                    previousChanged[channel] = value;
                }
            }
        }

        setControl(params, control.target);
        changed.setParams(params);
        const float targetModeFrequency = changed.modeFrequencyHz(0u);
        const float targetModeDecay = changed.modeDecaySeconds(0u);
        double onsetDifferencePeak = 0.0;
        double onsetDerivativeExcessPeak = 0.0;
        double lateDifferenceEnergy = 0.0;
        double lateDifferenceDerivativeEnergy = 0.0;
        double lateReferenceEnergy = 0.0;
        uint64_t lateCount = 0u;
        float outputPeak = 0.0f;
        bool finite = true;
        for (uint32_t firstFrame = 0u; firstFrame < changedFrames;
            firstFrame += blockFrames) {
            const uint32_t framesThisBlock = std::min(
                blockFrames, changedFrames - firstFrame);
            for (uint32_t frame = 0u; frame < framesThisBlock; ++frame) {
                const double phase = twoPi * 73.0
                    * static_cast<double>(warmFrames + firstFrame + frame)
                    / static_cast<double>(kSampleRate);
                external[frame] = 0.16f * static_cast<float>(std::sin(phase));
            }
            reference.process(external.data(), referenceOutputs.data(),
                channels, framesThisBlock);
            changed.process(external.data(), changedOutputs.data(),
                channels, framesThisBlock);
            for (uint32_t frame = 0u; frame < framesThisBlock; ++frame) {
                const uint32_t absoluteFrame = firstFrame + frame;
                for (uint32_t channel = 0u; channel < channels; ++channel) {
                    const float ref = referenceAudio[channel][frame];
                    const float value = changedAudio[channel][frame];
                    finite = finite && std::isfinite(ref)
                        && std::isfinite(value);
                    outputPeak = std::max(outputPeak, std::fabs(value));
                    const double difference = static_cast<double>(value)
                        - ref;
                    const double previousDifference =
                        static_cast<double>(previousChanged[channel])
                        - previousReference[channel];
                    const double derivativeDifference = difference
                        - previousDifference;
                    if (absoluteFrame < onsetFrames) {
                        onsetDifferencePeak = std::max(onsetDifferencePeak,
                            std::fabs(difference));
                        onsetDerivativeExcessPeak = std::max(
                            onsetDerivativeExcessPeak,
                            std::fabs(derivativeDifference));
                    }
                    if (absoluteFrame >= lateStartFrame) {
                        lateDifferenceEnergy += difference * difference;
                        lateDifferenceDerivativeEnergy +=
                            derivativeDifference * derivativeDifference;
                        lateReferenceEnergy +=
                            static_cast<double>(ref) * ref;
                        ++lateCount;
                    }
                    previousReference[channel] = ref;
                    previousChanged[channel] = value;
                }
            }
        }

        const double preDerivativeRms = std::sqrt(preDerivativeEnergy
            / static_cast<double>(std::max<uint64_t>(1u,
                preDerivativeCount)));
        const double lateDifferenceRms = std::sqrt(lateDifferenceEnergy
            / static_cast<double>(std::max<uint64_t>(1u, lateCount)));
        const double lateDifferenceDerivativeRms = std::sqrt(
            lateDifferenceDerivativeEnergy
            / static_cast<double>(std::max<uint64_t>(1u, lateCount)));
        const double lateReferenceRms = std::sqrt(lateReferenceEnergy
            / static_cast<double>(std::max<uint64_t>(1u, lateCount)));
        const double effectRatio = lateDifferenceRms
            / std::max(1.0e-12, lateReferenceRms);
        // A legal modal move can quickly change phase, so judge its onset
        // against both the pre-existing waveform derivative and the eventual
        // size/derivative of the audible parameter difference. A one-sample
        // discontinuity cannot hide behind a quiet absolute-value threshold.
        const double allowedOnset = std::max(
            preDerivativeRms * 6.0, lateDifferenceRms * 0.30);
        const double allowedDerivativeExcess = std::max(
            preDerivativeRms * 6.0,
            lateDifferenceDerivativeRms * 6.0);
        if (!finite || warmDifferencePeak > 1.0e-7
            || !(lateReferenceRms > 1.0e-7)
            || !(effectRatio > 0.002)
            || !(outputPeak <= 1.0f)
            || onsetDifferencePeak > allowedOnset
            || onsetDerivativeExcessPeak > allowedDerivativeExcess) {
            std::cerr << "Live " << control.name
                      << " automation was silent, unbounded, or click-like: "
                      << "effect " << effectRatio
                      << ", onset " << onsetDifferencePeak << " / "
                      << allowedOnset << ", derivative excess "
                      << onsetDerivativeExcessPeak << " / "
                      << allowedDerivativeExcess << ", peak " << outputPeak
                      << ", warm delta " << warmDifferencePeak
                      << ", mode Hz " << initialModeFrequency << " -> "
                      << targetModeFrequency << ", decay "
                      << initialModeDecay << " -> " << targetModeDecay
                      << " s\n";
            passed = false;
        }
    }
    return passed;
}

} // namespace

int main()
{
    if (!testFactoryPresets()
        || !testLiftedFactoryPresetsRemainClickFree()
        || !testDeterminism()
        || !testHoaDefaultAndRotation()
        || !testLowerOrderClearsUnusedChannels()
        || !testBodyCountAndModeBudget()
        || !testFixedListenerPickupSets()
        || !testListenerAndActuatorTelemetry()
        || !testCounterActuatorUsesSpatialAntipode()
        || !testWorstCaseOutputHeadroom()
        || !testBodyPlacementAndSpread()
        || !testBodyGeometrySmoothing()
        || !testPerBodyAedEditing()
        || !testRawBodyStems()
        || !testBalancedDroneAcrossBodyCounts()
        || !testPublicProfilesRemainDistinct()
        || !testSkinControlsDoNotSelfExcite()
        || !testPerBodyMidiStrike()
        || !testRepeatedHighVelocityMidiRemainsBounded()
        || !testOrdinaryMidiResponseSurvivesStressAndReset()
        || !testListenerActiveCoincidentMidiUsesIdleBodies()
        || !testContinuousBronzeActuatorIsSmoothAndStable()
        || !testModalCouplingEvolution()
        || !testZeroPlayerDepthPreservesOpenLoop()
        || !testOutputAndLiftDoNotDriveTelemetry()
        || !testOutputGainIsTransparentBelowGuard()
        || !testLiveOutputGainIsSmoothed()
        || !testVisibleContinuousControlsAreClickFree()
        || !testExternalActuatorIsModalOnly()) {
        return 1;
    }
    std::cout << "Ambi Encoder Modal smoke passed\n";
    return 0;
}
