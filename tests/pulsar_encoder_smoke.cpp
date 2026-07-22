#include "s3g_ambi_pulsar_encoder.h"
#include "s3g_ambi_pulsar_presets.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>

namespace {

constexpr uint32_t kFrames = 512u;
using Buffer = std::array<std::array<float, kFrames>, s3g::kAmbiPulsarMaxChannels>;

float renderPeak(s3g::AmbiPulsarEncoder& encoder, Buffer& buffer, uint32_t blocks = 96u)
{
    std::array<float*, s3g::kAmbiPulsarMaxChannels> outputs {};
    for (uint32_t channel = 0u; channel < outputs.size(); ++channel) outputs[channel] = buffer[channel].data();
    float peak = 0.0f;
    for (uint32_t block = 0u; block < blocks; ++block) {
        encoder.process(outputs.data(), static_cast<uint32_t>(outputs.size()), kFrames);
        for (const auto& channel : buffer) {
            for (float sample : channel) {
                if (!std::isfinite(sample)) return -1.0f;
                peak = std::max(peak, std::fabs(sample));
            }
        }
    }
    return peak;
}

bool testFactoryPresets()
{
    for (uint32_t preset = 0u; preset < s3g::kAmbiPulsarFactoryPresetCount; ++preset) {
        s3g::AmbiPulsarEncoder encoder;
        encoder.prepare(48000.0);
        encoder.setParams(s3g::ambiPulsarFactoryPreset(preset));
        encoder.reset();
        Buffer buffer {};
        const float peak = renderPeak(encoder, buffer, 80u);
        if (!(peak > 1.0e-5f) || peak > 4.0f) {
            std::cerr << "Pulsar preset " << preset << " peak outside expected range: " << peak << "\n";
            return false;
        }
    }
    return true;
}

bool testOrderAndMasking()
{
    s3g::AmbiPulsarEncoder encoder;
    encoder.prepare(48000.0);
    auto params = s3g::ambiPulsarFactoryPreset(0u);
    params.order = 3u;
    encoder.setParams(params);
    encoder.reset();

    Buffer buffer {};
    for (auto& channel : buffer) channel.fill(0.25f);
    const float peak = renderPeak(encoder, buffer, 20u);
    if (!(peak > 1.0e-5f)) {
        std::cerr << "Pulsar encoder produced no output\n";
        return false;
    }
    for (uint32_t channel = 16u; channel < s3g::kAmbiPulsarMaxChannels; ++channel) {
        for (float sample : buffer[channel]) {
            if (sample != 0.0f) {
                std::cerr << "Pulsar encoder did not clear channels above selected order\n";
                return false;
            }
        }
    }

    params.probability = 0.0f;
    encoder.setParams(params);
    encoder.reset();
    Buffer muted {};
    if (renderPeak(encoder, muted, 12u) != 0.0f) {
        std::cerr << "Pulsar probability mask did not mute events\n";
        return false;
    }
    return true;
}

bool testEventRate()
{
    auto countForRate = [](float rate) {
        s3g::AmbiPulsarEncoder encoder;
        encoder.prepare(48000.0);
        auto params = s3g::ambiPulsarFactoryPreset(0u);
        params.emissionHz = rate;
        params.emissionModDepth = 0.0f;
        params.probability = 1.0f;
        params.burstOff = 0u;
        params.sieveModulo = 1u;
        encoder.setParams(params);
        encoder.reset();
        Buffer buffer {};
        renderPeak(encoder, buffer, 94u); // ~1.003 seconds
        return encoder.emittedEventCount();
    };

    const uint64_t slow = countForRate(7.0f);
    const uint64_t fast = countForRate(70.0f);
    if (slow < 6u || slow > 8u || fast < 68u || fast > 72u || fast <= slow * 8u) {
        std::cerr << "Pulsar emission clock inaccurate: slow=" << slow << " fast=" << fast << "\n";
        return false;
    }
    return true;
}

bool testValidAmbisonicEncoding()
{
    s3g::AmbiPulsarEncoder encoder;
    encoder.prepare(48000.0);
    auto params = s3g::ambiPulsarFactoryPreset(0u);
    params.order = 7u;
    params.centerAzimuthDeg = 37.0f;
    params.centerElevationDeg = 21.0f;
    params.centerDistance = 1.0f;
    params.spatialWidth = 0.0f;
    params.spatialScatter = 0.0f;
    params.orbitDepth = 0.0f;
    params.orbitRateHz = 0.0f;
    params.air = 0.0f;
    params.doppler = 0.0f;
    params.probability = 1.0f;
    encoder.setParams(params);
    encoder.reset();

    Buffer buffer {};
    renderPeak(encoder, buffer, 24u);
    const auto basis = s3g::acnSn3dBasis7(s3g::directionFromAed(37.0f, 21.0f));
    bool foundSignal = false;
    for (uint32_t frame = 0u; frame < kFrames; ++frame) {
        const float omni = buffer[0][frame];
        if (std::fabs(omni) < 1.0e-4f) continue;
        foundSignal = true;
        for (uint32_t channel = 1u; channel < s3g::kAmbiPulsarMaxChannels; ++channel) {
            const float expected = omni * basis[channel];
            if (std::fabs(buffer[channel][frame] - expected) > 3.0e-4f) {
                std::cerr << "Pulsar HOA vector lost directional coherence at channel " << channel << "\n";
                return false;
            }
        }
    }
    if (!foundSignal) {
        std::cerr << "Pulsar coherence test found no signal\n";
        return false;
    }
    return true;
}

bool testSanitizationAndQuality()
{
    s3g::AmbiPulsarParams unsafe;
    unsafe.order = 99u;
    unsafe.emissionHz = -1.0f;
    unsafe.sieveModulo = 0u;
    unsafe.sieveResidue = 99u;
    unsafe.lanes[0].formantHz = 1.0e9f;
    unsafe.lanes[0].overlap = 99.0f;
    unsafe.centerElevationDeg = 180.0f;
    const auto safe = s3g::sanitizeAmbiPulsarParams(unsafe);
    if (safe.order != 7u || safe.emissionHz != 0.05f || safe.sieveModulo != 1u
        || safe.sieveResidue != 0u || safe.lanes[0].overlap != 8.0f
        || safe.centerElevationDeg != 89.0f) {
        std::cerr << "Pulsar parameter sanitization failed\n";
        return false;
    }

    s3g::AmbiPulsarEncoder encoder;
    encoder.prepare(96000.0);
    auto params = s3g::ambiPulsarFactoryPreset(4u);
    params.order = 7u;
    params.quality = s3g::AmbiPulsarQuality::Ultra;
    encoder.setParams(params);
    encoder.reset();
    Buffer buffer {};
    const float peak = renderPeak(encoder, buffer, 40u);
    if (!(peak > 1.0e-5f) || peak > 4.0f) {
        std::cerr << "Pulsar ultra quality render failed: " << peak << "\n";
        return false;
    }
    bool highOrderEnergy = false;
    for (uint32_t channel = 49u; channel < 64u; ++channel) {
        for (float sample : buffer[channel]) highOrderEnergy = highOrderEnergy || std::fabs(sample) > 1.0e-6f;
    }
    if (!highOrderEnergy) {
        std::cerr << "Pulsar 7OA render produced no seventh-order energy\n";
        return false;
    }
    return true;
}

bool testNeuralCircuit()
{
    s3g::NeuralSynthesisParams params;
    params.drive = 2.8f;
    params.feedback = 0.96f;
    params.coupling = 0.68f;
    params.hierarchy = 0.72f;
    params.phaseShift = 0.58f;
    params.brownian = 0.16f;
    params.drift = 0.24f;
    params.selfModulation = 0.66f;
    params.audioFeedback = 0.28f;
    params.seed = 0x1234abcdu;

    s3g::NeuralSynthesisNetwork network;
    s3g::NeuralSynthesisNetwork duplicate;
    network.prepare(48000.0);
    duplicate.prepare(48000.0);
    network.setParams(params);
    duplicate.setParams(params);
    network.reset();
    duplicate.reset();

    std::array<double, s3g::kNeuralSynthesisClusters> energy {};
    std::array<uint32_t, s3g::kNeuralSynthesisClusters> crossings {};
    std::array<float, s3g::kNeuralSynthesisClusters> previous {};
    constexpr uint32_t totalFrames = 48000u * 6u;
    constexpr uint32_t warmupFrames = 48000u;
    for (uint32_t frame = 0u; frame < totalFrames; ++frame) {
        const float external = std::sin(static_cast<float>(frame) * 0.00131f) * 0.08f;
        const auto a = network.process(external);
        const auto b = duplicate.process(external);
        for (uint32_t node = 0u; node < s3g::kNeuralSynthesisNodes; ++node) {
            if (!std::isfinite(a.nodes[node]) || std::fabs(a.nodes[node]) > 1.01f
                || std::fabs(a.nodes[node] - b.nodes[node]) > 1.0e-7f) {
                std::cerr << "Neural circuit lost bounded deterministic state at node " << node << "\n";
                return false;
            }
        }
        if (frame < warmupFrames) continue;
        for (uint32_t cluster = 0u; cluster < s3g::kNeuralSynthesisClusters; ++cluster) {
            const float value = a.nodes[cluster * s3g::kNeuralNodesPerCluster];
            energy[cluster] += static_cast<double>(value) * value;
            if ((value >= 0.0f) != (previous[cluster] >= 0.0f)) ++crossings[cluster];
            previous[cluster] = value;
        }
    }

    for (uint32_t cluster = 0u; cluster < s3g::kNeuralSynthesisClusters; ++cluster) {
        const double rms = std::sqrt(energy[cluster] / static_cast<double>(totalFrames - warmupFrames));
        if (!(rms > 1.0e-4) || crossings[cluster] == 0u) {
            std::cerr << "Neural cluster " << cluster << " did not sustain activity: rms=" << rms
                      << " crossings=" << crossings[cluster] << "\n";
            return false;
        }
    }
    if (!(crossings[3] > crossings[0] && crossings[2] >= crossings[0])) {
        std::cerr << "Neural RC bands did not separate from slow to fast: "
                  << crossings[0] << ", " << crossings[1] << ", " << crossings[2] << ", " << crossings[3] << "\n";
        return false;
    }

    s3g::NeuralSynthesisNetwork uncoupled;
    uncoupled.prepare(48000.0);
    auto uncoupledParams = params;
    uncoupledParams.coupling = 0.0f;
    uncoupled.setParams(uncoupledParams);
    uncoupled.reset();
    network.reset();
    double difference = 0.0;
    for (uint32_t frame = 0u; frame < 96000u; ++frame) {
        const auto coupledFrame = network.process();
        const auto uncoupledFrame = uncoupled.process();
        difference += std::fabs(coupledFrame.nodes[14] - uncoupledFrame.nodes[14]);
    }
    if (difference < 10.0) {
        std::cerr << "Neural signed matrix had no material cross-cluster influence\n";
        return false;
    }
    return true;
}

bool testNeuralEncoderIntegration()
{
    s3g::AmbiPulsarEncoder encoder;
    encoder.prepare(48000.0);
    auto params = s3g::ambiPulsarFactoryPreset(8u);
    params.order = 3u;
    params.probability = 0.0f;
    for (auto& lane : params.lanes) lane.level = 0.0f;
    params.neuralLevel = 0.9f;
    params.neural.audioFeedback = 0.42f;
    encoder.setParams(params);
    encoder.reset();

    Buffer buffer {};
    const float peak = renderPeak(encoder, buffer, 80u); // enough to arm and capture the 0.5 s table window
    if (!(peak > 1.0e-5f) || peak > 4.0f || encoder.neuralCaptureGeneration() == 0u) {
        std::cerr << "Neural direct/capture integration failed: peak=" << peak
                  << " captures=" << encoder.neuralCaptureGeneration() << "\n";
        return false;
    }

    params.probability = 1.0f;
    params.neuralLevel = 0.0f;
    params.neuralPulsaretMix = 1.0f;
    params.neuralEnvelopeMix = 1.0f;
    params.neuralFmDepthSemitones = 8.0f;
    for (auto& lane : params.lanes) lane.level = 0.55f;
    ++params.neuralCapture;
    encoder.setParams(params);
    const uint32_t before = encoder.neuralCaptureGeneration();
    const float capturedPeak = renderPeak(encoder, buffer, 24u);
    if (!(capturedPeak > 1.0e-5f) || capturedPeak > 4.0f || encoder.neuralCaptureGeneration() <= before) {
        std::cerr << "Neural pulsaret/envelope/FM capture paths failed: peak=" << capturedPeak << "\n";
        return false;
    }
    return true;
}

} // namespace

int main()
{
    if (!testFactoryPresets()) return 1;
    if (!testOrderAndMasking()) return 1;
    if (!testEventRate()) return 1;
    if (!testValidAmbisonicEncoding()) return 1;
    if (!testSanitizationAndQuality()) return 1;
    if (!testNeuralCircuit()) return 1;
    if (!testNeuralEncoderIntegration()) return 1;
    std::cout << "s3g Pulsar Encoder smoke test passed\n";
    return 0;
}
