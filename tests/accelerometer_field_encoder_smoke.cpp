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
            std::cerr << "Ambi Encoder Modal preset " << preset
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
    contract.prepare(48000.0);
    contract.setParams(params);
    if (params.outputMode != s3g::AccelerometerFieldOutputMode::Ambisonic
        || params.ambisonicOrder != 3u
        || contract.outputChannelCount() != 16u) {
        std::cerr << "Ambi Encoder Modal did not default to 3OA ACN/SN3D\n";
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
    if (!(difference(first, second) > 1.0e-6)) {
        std::cerr << "Pickup position and axis did not expose different mode shapes\n";
        return false;
    }
    return true;
}

bool testArrayCenterAndSpread()
{
    s3g::AccelerometerFieldEncoder engine;
    engine.prepare(48000.0);
    auto params = s3g::accelerometerFieldFactoryPreset(2u);
    params.pickupPosition = 0.18f;
    params.arraySpread = 0.0f;
    engine.setParams(params);
    const float localized = engine.sensorPosition(0u);
    for (uint32_t sensor = 1u;
        sensor < s3g::kAccelerometerFieldSensorCount; ++sensor) {
        if (std::fabs(engine.sensorPosition(sensor) - localized) > 1.0e-6f) {
            std::cerr << "Zero array spread did not co-locate the sensors\n";
            return false;
        }
    }
    params.pickupPosition = 0.82f;
    engine.setParams(params);
    if (!(engine.sensorPosition(0u) > localized + 0.25f)) {
        std::cerr << "Array center did not move the localized pickup\n";
        return false;
    }
    params.arraySpread = 1.0f;
    engine.setParams(params);
    if (!(engine.sensorPosition(7u) - engine.sensorPosition(0u) > 0.75f)) {
        std::cerr << "Array spread did not separate the attachment points\n";
        return false;
    }
    return true;
}

bool testAedSpreadLocalization()
{
    s3g::AccelerometerFieldEncoder engine;
    engine.prepare(48000.0);
    auto params = s3g::accelerometerFieldFactoryPreset(2u);
    params.fieldAzimuthDeg = 37.0f;
    params.fieldElevationDeg = -18.0f;
    params.spatialExtent = 0.0f;
    engine.setParams(params);
    const auto localized = engine.sensorDirection(0u);
    for (uint32_t sensor = 1u;
        sensor < s3g::kAccelerometerFieldSensorCount; ++sensor) {
        const auto direction = engine.sensorDirection(sensor);
        if (std::fabs(direction.x - localized.x) > 1.0e-6f
            || std::fabs(direction.y - localized.y) > 1.0e-6f
            || std::fabs(direction.z - localized.z) > 1.0e-6f) {
            std::cerr << "Zero AED spread did not localize the field\n";
            return false;
        }
    }
    params.spatialExtent = 1.0f;
    engine.setParams(params);
    const auto first = engine.sensorDirection(0u);
    const auto last = engine.sensorDirection(7u);
    const float separation = std::fabs(first.x - last.x)
        + std::fabs(first.y - last.y) + std::fabs(first.z - last.z);
    if (!(separation > 0.25f)) {
        std::cerr << "AED spread did not distribute the encoded field\n";
        return false;
    }
    return true;
}

bool testArrayMotionSmoothing()
{
    s3g::AccelerometerFieldEncoder engine;
    engine.prepare(48000.0);
    auto params = s3g::accelerometerFieldFactoryPreset(2u);
    params.pickupPosition = 0.15f;
    params.arraySpread = 0.0f;
    engine.setParams(params);
    engine.reset();
    const float initial = engine.currentSensorPosition(7u);

    params.pickupPosition = 0.85f;
    params.arraySpread = 1.0f;
    engine.setParams(params);
    const float target = engine.sensorPosition(7u);
    if (!(target > initial + 0.50f)
        || std::fabs(engine.currentSensorPosition(7u) - initial) > 1.0e-7f) {
        std::cerr << "Array geometry snapped before audio-rate interpolation\n";
        return false;
    }

    constexpr uint32_t frames = 4800u;
    std::array<float, frames> audio {};
    std::array<float*, s3g::kAccelerometerFieldMaxChannels> outputs {};
    outputs[0] = audio.data();
    engine.process(nullptr, outputs.data(), outputs.size(), 1u);
    const float firstStep = engine.currentSensorPosition(7u);
    if (!(firstStep > initial)
        || !(firstStep < initial + (target - initial) * 0.01f)) {
        std::cerr << "Array geometry did not begin with a click-free small step\n";
        return false;
    }
    engine.process(nullptr, outputs.data(), outputs.size(), frames);
    if (!(std::fabs(engine.currentSensorPosition(7u) - target)
            < std::fabs(target - initial) * 0.08f)) {
        std::cerr << "Array geometry smoothing did not converge promptly\n";
        return false;
    }
    return true;
}

bool testGongFamilyHeadroom()
{
    constexpr std::array<uint32_t, 4u> presets {{ 0u, 2u, 6u, 10u }};
    Buffer first {};
    Buffer second {};
    for (const uint32_t preset : presets) {
        auto params = s3g::accelerometerFieldFactoryPreset(preset);
        params.outputMode = s3g::AccelerometerFieldOutputMode::SensorStems;
        render(params, first, second);
        const auto firstMetrics = metrics(first);
        const auto secondMetrics = metrics(second);
        if (!(firstMetrics.energy > 1.0e-10)
            || !(secondMetrics.energy > 1.0e-10)
            || !(firstMetrics.peak < 0.98f)
            || !(secondMetrics.peak < 0.98f)) {
            std::cerr << "Gong family preset lost usable radiation headroom: "
                      << preset << ", " << firstMetrics.peak << ", "
                      << secondMetrics.peak << "\n";
            return false;
        }
    }
    return true;
}

bool testFocusedGongBodyBank()
{
    constexpr std::array<s3g::AccelerometerSubstrate, 4u> expected {{
        s3g::AccelerometerSubstrate::GongAgeng,
        s3g::AccelerometerSubstrate::BellBronze,
        s3g::AccelerometerSubstrate::Jing,
        s3g::AccelerometerSubstrate::Kkwaenggwari,
    }};
    std::array<uint32_t, expected.size()> bodyCounts {};
    for (uint32_t preset = 0u;
        preset < s3g::kAccelerometerFieldPresetCount; ++preset) {
        const auto params = s3g::accelerometerFieldFactoryPreset(preset);
        const auto found = std::find(
            expected.begin(), expected.end(), params.substrate);
        if (found == expected.end()) {
            std::cerr << "Factory bank exposed a non-gong body at preset "
                      << preset << "\n";
            return false;
        }
        ++bodyCounts[static_cast<size_t>(found - expected.begin())];
    }
    for (uint32_t body = 0u; body < bodyCounts.size(); ++body) {
        if (bodyCounts[body] < 2u) {
            std::cerr << "Gong body lacks a curated preset pair: "
                      << body << "\n";
            return false;
        }
    }

    std::array<float, expected.size()> fundamentals {};
    for (uint32_t body = 0u; body < expected.size(); ++body) {
        auto params = s3g::accelerometerFieldFactoryPreset(0u);
        params.substrate = expected[body];
        params.size = 0.5f;
        s3g::AccelerometerFieldEncoder engine;
        engine.prepare(48000.0);
        engine.setParams(params);
        fundamentals[body] = engine.modeFrequencyHz(0u);
    }
    if (!(fundamentals[0] > 40.0f && fundamentals[0] < 50.0f)
        || !(fundamentals[1] > 145.0f && fundamentals[1] < 165.0f)
        || !(fundamentals[2] > 108.0f && fundamentals[2] < 116.0f)
        || !(fundamentals[3] > 240.0f && fundamentals[3] < 255.0f)) {
        std::cerr << "Gong-family modal landmarks drifted: "
                  << fundamentals[0] << ", " << fundamentals[1] << ", "
                  << fundamentals[2] << ", " << fundamentals[3] << "\n";
        return false;
    }
    for (uint32_t firstIndex = 0u;
        firstIndex < fundamentals.size(); ++firstIndex) {
        for (uint32_t secondIndex = firstIndex + 1u;
            secondIndex < fundamentals.size(); ++secondIndex) {
            if (std::fabs(fundamentals[firstIndex]
                    - fundamentals[secondIndex]) < 2.0f) {
                std::cerr << "Curated gong profiles collapsed to one modal family\n";
                return false;
            }
        }
    }

    auto measured = s3g::accelerometerFieldFactoryPreset(0u);
    measured.size = 0.5f;
    measured.irregularity = 0.0f;
    measured.sensorMass = 0.0f;
    measured.substrate = s3g::AccelerometerSubstrate::Jing;
    s3g::AccelerometerFieldEncoder analysisProfile;
    analysisProfile.prepare(48000.0);
    analysisProfile.setParams(measured);
    if (std::fabs(analysisProfile.modeFrequencyHz(0u) - 114.0f) > 0.01f
        || std::fabs(analysisProfile.modeFrequencyHz(2u) - 228.0f) > 0.02f
        || std::fabs(analysisProfile.modeFrequencyHz(3u) - 240.0f) > 0.03f
        || std::fabs(analysisProfile.modeFrequencyHz(5u) - 342.0f) > 0.03f) {
        std::cerr << "Jing analysis landmarks left the measured mode bank\n";
        return false;
    }
    measured.substrate = s3g::AccelerometerSubstrate::Kkwaenggwari;
    analysisProfile.setParams(measured);
    if (std::fabs(analysisProfile.modeFrequencyHz(2u) - 500.0f) > 0.05f
        || std::fabs(analysisProfile.modeFrequencyHz(4u) - 1000.0f) > 0.05f
        || std::fabs(analysisProfile.modeFrequencyHz(10u) - 2050.0f) > 0.10f
        || std::fabs(analysisProfile.modeFrequencyHz(14u) - 3900.0f) > 0.10f) {
        std::cerr << "Kkwaenggwari radiation landmarks left the modal bank\n";
        return false;
    }
    return true;
}

bool testMalletRollIsBroadbandAndPulsed()
{
    Buffer roll {};
    Buffer bronze {};
    Buffer air {};
    const auto rollParams = s3g::accelerometerFieldFactoryPreset(4u);
    if (rollParams.excitation != s3g::AccelerometerExcitation::Chewing
        || !(rollParams.eventRateHz >= 3.0f
            && rollParams.eventRateHz <= 6.0f)
        || !(rollParams.contactDetail > 0.40f)
        || !(rollParams.texture > 0.65f)) {
        std::cerr << "Mallet Roll lost its articulated high-rate gesture\n";
        return false;
    }
    render(rollParams, roll, air);
    render(s3g::accelerometerFieldFactoryPreset(1u), bronze, air);
    const auto rollMetrics = metrics(roll);
    const auto bronzeMetrics = metrics(bronze);
    const double rollPulseRatio = rollMetrics.differenceEnergy
        / std::max(rollMetrics.energy, 1.0e-20);
    const double bronzePulseRatio = bronzeMetrics.differenceEnergy
        / std::max(bronzeMetrics.energy, 1.0e-20);
    if (!(rollPulseRatio > bronzePulseRatio * 1.5)) {
        std::cerr << "Mallet Roll collapsed into an ambient bronze ring: "
                  << rollPulseRatio << " versus bronze "
                  << bronzePulseRatio << "\n";
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

bool testMidiForceStrike()
{
    constexpr uint32_t frames = 12000u;
    std::array<float, frames> quiet {};
    std::array<float, frames> struck {};
    auto params = s3g::accelerometerFieldFactoryPreset(12u);
    params.activity = 0.0f;
    params.ambientDrive = 0.0f;
    params.sensorNoise = 0.0f;
    params.externalDrive = 0.0f;
    params.outputMode = s3g::AccelerometerFieldOutputMode::SensorStems;

    s3g::AccelerometerFieldEncoder engine;
    engine.prepare(48000.0);
    engine.setParams(params);
    engine.reset();
    const float reference = engine.modeFrequencyHz(0u);
    std::array<float*, s3g::kAccelerometerFieldMaxChannels> outputs {};
    outputs[0] = quiet.data();
    engine.process(nullptr, outputs.data(), outputs.size(), frames);

    engine.reset();
    engine.strikeMidi(72, 0.82f);
    const float transposed = engine.modeFrequencyHz(0u);
    outputs[0] = struck.data();
    engine.process(nullptr, outputs.data(), outputs.size(), frames);
    double quietEnergy = 0.0;
    double strikeEnergy = 0.0;
    for (float sample : quiet) {
        quietEnergy += static_cast<double>(sample) * sample;
    }
    for (float sample : struck) {
        strikeEnergy += static_cast<double>(sample) * sample;
    }
    if (!(engine.performancePitchRatio() > 1.999f
            && engine.performancePitchRatio() < 2.001f)
        || !(transposed / reference > 1.999f
            && transposed / reference < 2.001f)
        || !(strikeEnergy > 1.0e-8)
        || !(quietEnergy < strikeEnergy * 0.01)) {
        std::cerr << "MIDI note-on did not act as a pitched force strike: "
                  << reference << ", " << transposed << ", "
                  << strikeEnergy << "\n";
        return false;
    }
    engine.reset();
    if (std::fabs(engine.performancePitchRatio() - 1.0f) > 1.0e-6f
        || std::fabs(engine.modeFrequencyHz(0u) - reference) > 1.0e-3f) {
        std::cerr << "MIDI performance pitch survived an engine reset\n";
        return false;
    }
    return true;
}

bool testGongAgengEvolution()
{
    auto params = s3g::accelerometerFieldFactoryPreset(0u);
    if (params.substrate != s3g::AccelerometerSubstrate::GongAgeng
        || !(params.coupling > 0.60f)
        || !(params.energy > 0.55f)) {
        std::cerr << "Gong Ageng preset lost its coupled nonlinear profile\n";
        return false;
    }

    s3g::AccelerometerFieldEncoder profile;
    profile.prepare(48000.0);
    profile.setParams(params);
    const float fundamental = profile.modeFrequencyHz(0u);
    const float secondHarmonic = profile.modeFrequencyHz(3u);
    if (!(fundamental > 43.0f && fundamental < 46.0f)
        || !(secondHarmonic / fundamental > 1.96f)
        || !(secondHarmonic / fundamental < 2.04f)
        || !(std::fabs(profile.modePickupWeight(0u, 0u)
                - profile.modePickupWeight(1u, 0u)) > 0.05f)) {
        std::cerr << "Gong Ageng modal landmarks drifted: "
                  << fundamental << ", " << secondHarmonic << "\n";
        return false;
    }

    Buffer uncoupled {};
    Buffer coupled {};
    Buffer companion {};
    params.energy = 0.0f;
    params.coupling = 0.0f;
    render(params, uncoupled, companion);
    params.coupling = 1.0f;
    render(params, coupled, companion);
    if (!(difference(uncoupled, coupled) > 1.0e-5)) {
        std::cerr << "Modal coupling did not animate the gong clusters\n";
        return false;
    }

    constexpr uint32_t frames = 24000u;
    std::array<float, frames> input {};
    std::array<float, frames> linear {};
    std::array<float, frames> nonlinear {};
    input[0] = 1.0f;
    const auto renderImpulse = [&](float energy, auto& output) {
        auto impulseParams = s3g::accelerometerFieldFactoryPreset(0u);
        impulseParams.activity = 0.0f;
        impulseParams.ambientDrive = 0.0f;
        impulseParams.sensorNoise = 0.0f;
        impulseParams.externalDrive = 1.0f;
        impulseParams.coupling = 0.0f;
        impulseParams.energy = energy;
        impulseParams.outputMode =
            s3g::AccelerometerFieldOutputMode::SensorStems;
        s3g::AccelerometerFieldEncoder engine;
        engine.prepare(48000.0);
        engine.setParams(impulseParams);
        engine.reset();
        std::array<float*, s3g::kAccelerometerFieldMaxChannels> outputs {};
        outputs[0] = output.data();
        engine.process(input.data(), outputs.data(), outputs.size(), frames);
    };
    renderImpulse(0.0f, linear);
    renderImpulse(1.0f, nonlinear);
    double delayedDifference = 0.0;
    for (uint32_t frame = 480u; frame < 12000u; ++frame) {
        const double delta = nonlinear[frame] - linear[frame];
        delayedDifference += delta * delta;
    }
    if (!(delayedDifference > 1.0e-7)) {
        std::cerr << "Nonlinear energy did not create a finite delayed response: "
                  << delayedDifference << "\n";
        return false;
    }
    for (float sample : nonlinear) {
        if (!std::isfinite(sample) || std::fabs(sample) > 1.0f) {
            std::cerr << "Nonlinear gong response exceeded finite headroom\n";
            return false;
        }
    }
    return true;
}

bool testFieldListenerFeedback()
{
    Buffer off {};
    Buffer zeroInfluence {};
    Buffer active {};
    Buffer companion {};
    auto params = s3g::accelerometerFieldFactoryPreset(4u);
    if (params.fieldListenMode != s3g::AmbiFieldListenMode::Off) {
        std::cerr << "Field listening did not default to Off\n";
        return false;
    }
    render(params, off, companion);
    params.fieldListenMode = s3g::AmbiFieldListenMode::Follow;
    params.fieldListenAmount = 0.0f;
    params.fieldListenResponse = s3g::AmbiFieldListenerResponse::Imprint;
    render(params, zeroInfluence, companion);
    if (off != zeroInfluence) {
        std::cerr << "Zero listener influence changed the authored synthesis\n";
        return false;
    }

    params.fieldListenAmount = 1.0f;
    render(params, active, companion);
    if (!(difference(off, active) > 1.0e-6)) {
        std::cerr << "Active field listening did not affect later events\n";
        return false;
    }

    params.outputMode = s3g::AccelerometerFieldOutputMode::SensorStems;
    s3g::AccelerometerFieldEncoder rawEngine;
    rawEngine.prepare(48000.0);
    rawEngine.setParams(params);
    rawEngine.reset();
    std::array<float*, s3g::kAccelerometerFieldMaxChannels> outputs {};
    outputs[0] = active.data();
    outputs[1] = companion.data();
    rawEngine.process(nullptr, outputs.data(), outputs.size(), kFrames);
    const float target = rawEngine.listenerTargetPosition();
    if (!(rawEngine.listenerActivity() > 0.001f)
        || !std::isfinite(target)
        || !(target >= 0.02f && target <= 0.98f)
        || !(std::fabs(target - params.sourcePosition) > 0.01f)) {
        std::cerr << "Raw stems stopped feeding the internal HOA listener: "
                  << rawEngine.listenerActivity() << ", target "
                  << target << "\n";
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
        || !testArrayCenterAndSpread()
        || !testAedSpreadLocalization()
        || !testArrayMotionSmoothing()
        || !testGongFamilyHeadroom()
        || !testFocusedGongBodyBank()
        || !testMalletRollIsBroadbandAndPulsed()
        || !testExternalDrive()
        || !testMidiForceStrike()
        || !testGongAgengEvolution()
        || !testFieldListenerFeedback()) {
        return 1;
    }
    std::cout << "Ambi Encoder Modal smoke passed\n";
    return 0;
}
