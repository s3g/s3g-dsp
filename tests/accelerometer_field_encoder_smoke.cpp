#include "s3g_accelerometer_field_encoder.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>

namespace {

constexpr uint32_t kFrames = 48000u * 3u;
using Buffer = std::array<float, kFrames>;

struct Metrics {
    double energy = 0.0;
    double differenceEnergy = 0.0;
    float peak = 0.0f;
    bool finite = true;
};

Metrics metrics(const Buffer& buffer)
{
    Metrics result;
    float previous = 0.0f;
    for (float sample : buffer) {
        result.finite = result.finite && std::isfinite(sample);
        result.energy += static_cast<double>(sample) * sample;
        const double difference = static_cast<double>(sample) - previous;
        result.differenceEnergy += difference * difference;
        result.peak = std::max(result.peak, std::fabs(sample));
        previous = sample;
    }
    result.energy /= buffer.size();
    result.differenceEnergy /= buffer.size();
    return result;
}

void render(const s3g::AccelerometerFieldParams& params,
    Buffer& first, Buffer& second)
{
    s3g::AccelerometerFieldEncoder engine;
    engine.prepare(48000.0);
    engine.setParams(params);
    engine.reset();
    std::array<float*, s3g::kAccelerometerFieldMaxChannels> outputs {};
    outputs[0] = first.data();
    outputs[1] = second.data();
    engine.process(nullptr, outputs.data(), outputs.size(), kFrames);
}

double difference(const Buffer& first, const Buffer& second)
{
    double total = 0.0;
    for (uint32_t frame = 0u; frame < kFrames; ++frame) {
        total += std::fabs(first[frame] - second[frame]);
    }
    return total / kFrames;
}

bool testFactoryPresets()
{
    Buffer contact {};
    Buffer air {};
    for (uint32_t preset = 0u;
        preset < s3g::kAccelerometerFieldPresetCount; ++preset) {
        render(s3g::accelerometerFieldFactoryPreset(preset), contact, air);
        const Metrics contactMetrics = metrics(contact);
        const Metrics airMetrics = metrics(air);
        if (!contactMetrics.finite || !airMetrics.finite
            || !(contactMetrics.energy > 1.0e-10)
            || !(contactMetrics.peak > 1.0e-5f)
            || !(contactMetrics.peak <= 1.0f)
            || !(airMetrics.peak <= 1.0f)) {
            std::cerr << "Accelerometer preset " << preset
                      << " failed finite/audibility bounds: contact energy "
                      << contactMetrics.energy << ", peak " << contactMetrics.peak
                      << ", air peak " << airMetrics.peak << "\n";
            return false;
        }
    }
    return true;
}

bool testDeterminism()
{
    Buffer first {};
    Buffer second {};
    Buffer air {};
    const auto params = s3g::accelerometerFieldFactoryPreset(3u);
    render(params, first, air);
    render(params, second, air);
    if (first != second) {
        std::cerr << "Accelerometer field encoder is not deterministic for one seed\n";
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
    contract.prepare(48000.0);
    contract.setParams(params);
    if (params.outputMode != s3g::AccelerometerFieldOutputMode::Ambisonic
        || params.ambisonicOrder != 3u
        || contract.outputChannelCount() != 16u) {
        std::cerr << "Accelerometer field did not default to 3OA ACN/SN3D\n";
        return false;
    }

    params.ambisonicOrder = 1u;
    params.fieldAzimuthDeg = 0.0f;
    render(params, frontW, frontY);
    params.fieldAzimuthDeg = 90.0f;
    render(params, turnedW, turnedY);
    if (!(difference(frontW, turnedW) < 1.0e-7)
        || !(difference(frontY, turnedY) > 1.0e-5)) {
        std::cerr << "HOA rotation did not preserve W while rotating direction: "
                  << difference(frontW, turnedW) << ", "
                  << difference(frontY, turnedY) << "\n";
        return false;
    }
    return true;
}

bool testOptionalSensorStems()
{
    Buffer first {};
    Buffer second {};
    auto params = s3g::accelerometerFieldFactoryPreset(3u);
    params.outputMode = s3g::AccelerometerFieldOutputMode::SensorStems;
    s3g::AccelerometerFieldEncoder contract;
    contract.prepare(48000.0);
    contract.setParams(params);
    if (contract.outputChannelCount() != s3g::kAccelerometerFieldSensorCount) {
        std::cerr << "Sensor-stem mode exposed the wrong channel count\n";
        return false;
    }
    render(params, first, second);
    if (!(metrics(first).energy > 1.0e-10)
        || !(metrics(second).energy > 1.0e-10)
        || !(difference(first, second) > 1.0e-5)) {
        std::cerr << "Optional sensor stems lost independent attachment views\n";
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
    engine.prepare(48000.0);
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

bool testPickupGeometry()
{
    Buffer first {};
    Buffer second {};
    Buffer air {};
    auto params = s3g::accelerometerFieldFactoryPreset(4u);
    params.sensorNoise = 0.0f;
    params.pickupPosition = 0.14f;
    params.pickupAxis = 0.0f;
    render(params, first, air);
    params.pickupPosition = 0.72f;
    params.pickupAxis = 0.94f;
    render(params, second, air);
    if (!(difference(first, second) > 1.0e-4)) {
        std::cerr << "Pickup position and axis did not expose different mode shapes\n";
        return false;
    }
    return true;
}

bool testReadoutDomain()
{
    Buffer acceleration {};
    Buffer displacement {};
    Buffer air {};
    auto params = s3g::accelerometerFieldFactoryPreset(7u);
    params.sensorNoise = 0.0f;
    params.readout = s3g::AccelerometerReadout::Acceleration;
    render(params, acceleration, air);
    params.readout = s3g::AccelerometerReadout::Displacement;
    render(params, displacement, air);
    const Metrics accelerationMetrics = metrics(acceleration);
    const Metrics displacementMetrics = metrics(displacement);
    if (!(difference(acceleration, displacement) > 1.0e-5)
        || !(accelerationMetrics.differenceEnergy
            > displacementMetrics.differenceEnergy * 1.4)) {
        std::cerr << "Acceleration readout lost its high-frequency weighting: "
                  << accelerationMetrics.differenceEnergy << " versus "
                  << displacementMetrics.differenceEnergy << "\n";
        return false;
    }
    return true;
}

bool testChewingIsBroadbandAndPulsed()
{
    Buffer chewing {};
    Buffer bronze {};
    Buffer air {};
    const auto chewingParams = s3g::accelerometerFieldFactoryPreset(3u);
    if (!(chewingParams.eventRateHz >= 3.0f
        && chewingParams.eventRateHz <= 5.0f)
        || !(chewingParams.contactDetail > 0.80f)
        || !(chewingParams.damping > 0.70f)) {
        std::cerr << "Caterpillar preset lost documented closure rate/contact behavior\n";
        return false;
    }
    render(chewingParams, chewing, air);
    render(s3g::accelerometerFieldFactoryPreset(0u), bronze, air);
    const auto chewingMetrics = metrics(chewing);
    const auto bronzeMetrics = metrics(bronze);
    const double chewingPulseRatio = chewingMetrics.differenceEnergy
        / std::max(chewingMetrics.energy, 1.0e-20);
    const double bronzePulseRatio = bronzeMetrics.differenceEnergy
        / std::max(bronzeMetrics.energy, 1.0e-20);
    if (!(chewingPulseRatio > bronzePulseRatio * 4.0)) {
        std::cerr << "Caterpillar chewing collapsed back into a bell-like ring: "
                  << chewingPulseRatio << " versus bronze "
                  << bronzePulseRatio << "\n";
        return false;
    }
    return true;
}

bool testMountAndMassLoading()
{
    s3g::AccelerometerFieldEncoder light;
    s3g::AccelerometerFieldEncoder loaded;
    light.prepare(48000.0);
    loaded.prepare(48000.0);
    auto params = s3g::accelerometerFieldFactoryPreset(2u);
    params.sensorMass = 0.0f;
    light.setParams(params);
    params.sensorMass = 1.0f;
    loaded.setParams(params);
    if (!(loaded.modeFrequencyHz(0u) < light.modeFrequencyHz(0u) * 0.85f)) {
        std::cerr << "Sensor mass did not lower the leaf modal frequency\n";
        return false;
    }

    Buffer stiff {};
    Buffer soft {};
    Buffer air {};
    params = s3g::accelerometerFieldFactoryPreset(3u);
    params.sensorNoise = 0.0f;
    params.mountStiffness = 1.0f;
    render(params, stiff, air);
    params.mountStiffness = 0.02f;
    render(params, soft, air);
    const auto stiffMetrics = metrics(stiff);
    const auto softMetrics = metrics(soft);
    if (!(stiffMetrics.differenceEnergy > softMetrics.differenceEnergy * 1.8)) {
        std::cerr << "Soft mounting did not reduce high-frequency transmissibility: "
                  << stiffMetrics.differenceEnergy << " versus "
                  << softMetrics.differenceEnergy << "\n";
        return false;
    }
    return true;
}

bool testExternalDrive()
{
    constexpr uint32_t frames = 8192u;
    std::array<float, frames> input {};
    std::array<float, frames> contact {};
    std::array<float, frames> air {};
    input[0] = 1.0f;
    input[2048] = -0.6f;
    auto params = s3g::accelerometerFieldFactoryPreset(0u);
    params.activity = 0.0f;
    params.ambientDrive = 0.0f;
    params.sensorNoise = 0.0f;
    params.externalDrive = 1.0f;
    s3g::AccelerometerFieldEncoder engine;
    engine.prepare(48000.0);
    engine.setParams(params);
    engine.reset();
    std::array<float*, s3g::kAccelerometerFieldMaxChannels> outputs {};
    outputs[0] = contact.data();
    outputs[1] = air.data();
    engine.process(input.data(), outputs.data(), outputs.size(), frames);
    double energy = 0.0;
    for (float sample : contact) energy += static_cast<double>(sample) * sample;
    if (!(energy > 1.0e-8)) {
        std::cerr << "External excitation did not drive the structural model\n";
        return false;
    }
    return true;
}

} // namespace

int main()
{
    if (!testFactoryPresets()
        || !testDeterminism()
        || !testHoaDefaultAndRotation()
        || !testOptionalSensorStems()
        || !testLowerOrderClearsUnusedChannels()
        || !testPickupGeometry()
        || !testReadoutDomain()
        || !testChewingIsBroadbandAndPulsed()
        || !testMountAndMassLoading()
        || !testExternalDrive()) {
        return 1;
    }
    std::cout << "Accelerometer field encoder smoke passed\n";
    return 0;
}
