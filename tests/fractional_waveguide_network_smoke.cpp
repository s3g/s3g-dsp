#include "s3g_fractional_waveguide_network.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr uint32_t kBlockFrames = 256u;

struct RenderBlock {
    std::array<std::array<float, kBlockFrames>,
        s3g::kFractionalWaveguideMaxChannels> storage {};
    std::array<float*, s3g::kFractionalWaveguideMaxChannels> pointers {};

    RenderBlock()
    {
        for (uint32_t channel = 0u; channel < pointers.size(); ++channel) {
            pointers[channel] = storage[channel].data();
        }
    }
};

bool finiteBlock(const RenderBlock& block, uint32_t frames)
{
    for (const auto& channel : block.storage) {
        for (uint32_t frame = 0u; frame < frames; ++frame) {
            if (!std::isfinite(channel[frame])) return false;
        }
    }
    return true;
}

bool fractionalDelayProbe()
{
    constexpr float requestedDelay = 37.35f;
    s3g::WaveguideFractionalDelay delay;
    delay.prepare(kSampleRate, 0.1f);
    delay.setDelaySamples(requestedDelay);
    delay.reset();

    double energy = 0.0;
    double dc = 0.0;
    double moment = 0.0;
    for (uint32_t frame = 0u; frame < 4096u; ++frame) {
        const float output = delay.read();
        delay.writeAndAdvance(frame == 0u ? 1.0f : 0.0f);
        energy += static_cast<double>(output) * output;
        dc += output;
        moment += static_cast<double>(frame) * output;
    }
    const double measuredDelay = moment / std::max(1.0e-12, dc);
    if (std::abs(delay.fractionalDelay() - 0.35f) > 0.0001f
        || std::abs(energy - 1.0) > 0.0005
        || std::abs(dc - 1.0) > 0.0005
        || std::abs(measuredDelay - requestedDelay) > 0.001) {
        std::cerr << "fractional delay was not lossless or accurately timed: "
                  << delay.fractionalDelay() << " / "
                  << energy << " / " << dc << " / "
                  << measuredDelay << "\n";
        return false;
    }
    return true;
}

bool directionalRadiationProbe()
{
    s3g::FractionalWaveguideNetwork network;
    s3g::FractionalWaveguideParams params;
    params.order = 1u;
    params.radiation = 0.0f;
    params.outputGainDb = -18.0f;
    network.setParams(params);
    network.prepare(kSampleRate);
    network.strike(0u, 0.5f);

    RenderBlock output;
    network.process(nullptr, output.pointers.data(),
        static_cast<uint32_t>(output.pointers.size()), 1u);
    if (!finiteBlock(output, 1u)
        || !(output.storage[0][0] > 0.0f)
        || !(output.storage[1][0] < 0.0f)
        || !(output.storage[2][0] < 0.0f)
        || !(output.storage[3][0] < 0.0f)) {
        std::cerr << "cube-node strike did not radiate toward its HOA direction\n";
        return false;
    }
    for (uint32_t channel = 4u; channel < output.storage.size(); ++channel) {
        if (output.storage[channel][0] != 0.0f) {
            std::cerr << "channels above the selected order were not cleared\n";
            return false;
        }
    }
    return true;
}

struct DecayProbe {
    double firstEnergy = 0.0;
    double lastEnergy = 0.0;
    float peak = 0.0f;
    bool finite = true;
};

DecayProbe renderDecay()
{
    constexpr uint32_t totalFrames = 4u * 48000u;
    s3g::FractionalWaveguideNetwork network;
    s3g::FractionalWaveguideParams params;
    params.order = 3u;
    params.decaySeconds = 1.2f;
    params.absorption = 0.14f;
    params.radiation = 0.08f;
    params.outputGainDb = -18.0f;
    network.setParams(params);
    network.prepare(kSampleRate);
    network.strike(0u, 0.8f);

    RenderBlock output;
    DecayProbe result;
    for (uint32_t offset = 0u; offset < totalFrames;
         offset += kBlockFrames) {
        const uint32_t frames = std::min(
            kBlockFrames, totalFrames - offset);
        network.process(nullptr, output.pointers.data(),
            static_cast<uint32_t>(output.pointers.size()), frames);
        result.finite = result.finite && finiteBlock(output, frames)
            && std::isfinite(network.travelingEnergy());
        for (uint32_t frame = 0u; frame < frames; ++frame) {
            const float sample = output.storage[0][frame];
            const double energy = static_cast<double>(sample) * sample;
            if (offset + frame < 48000u) result.firstEnergy += energy;
            if (offset + frame >= totalFrames - 48000u)
                result.lastEnergy += energy;
            result.peak = std::max(result.peak, std::abs(sample));
        }
    }
    return result;
}

std::vector<float> renderGeometry(float halfExtent)
{
    constexpr uint32_t frames = 48000u;
    s3g::FractionalWaveguideNetwork network;
    s3g::FractionalWaveguideParams params;
    params.order = 1u;
    params.decaySeconds = 1.8f;
    params.absorption = 0.05f;
    params.radiation = 0.0f;
    params.outputGainDb = -20.0f;
    network.configureCube(halfExtent);
    network.setParams(params);
    network.prepare(kSampleRate);
    network.strike(0u, 0.7f);

    std::vector<float> result;
    result.reserve(frames);
    RenderBlock output;
    for (uint32_t offset = 0u; offset < frames; offset += kBlockFrames) {
        const uint32_t count = std::min(kBlockFrames, frames - offset);
        network.process(nullptr, output.pointers.data(),
            static_cast<uint32_t>(output.pointers.size()), count);
        result.insert(result.end(), output.storage[0].begin(),
            output.storage[0].begin() + count);
    }
    return result;
}

bool geometryTimingProbe()
{
    s3g::FractionalWaveguideNetwork network;
    network.prepare(kSampleRate);
    const float defaultDelay = network.edgeDelaySamples(0u);
    network.configureCube(0.503f);
    const float movedDelay = network.edgeDelaySamples(0u);
    if (network.nodeCount() != 8u || network.edgeCount() != 12u
        || std::abs(defaultDelay - 48000.0f / 343.0f) > 0.01f
        || std::abs(defaultDelay - movedDelay) < 0.2f) {
        std::cerr << "cube geometry did not produce metric edge delays: "
                  << defaultDelay << " / " << movedDelay << "\n";
        return false;
    }

    const auto first = renderGeometry(0.5f);
    const auto second = renderGeometry(0.503f);
    double difference = 0.0;
    double energy = 0.0;
    for (uint32_t frame = 0u; frame < first.size(); ++frame) {
        const double delta =
            static_cast<double>(first[frame]) - second[frame];
        difference += delta * delta;
        energy += static_cast<double>(first[frame]) * first[frame];
    }
    if (!(difference > energy * 0.01)) {
        std::cerr << "fractional geometry did not materially retune the network: "
                  << difference << " / " << energy << "\n";
        return false;
    }
    return true;
}

bool repeatedExcitationProbe()
{
    s3g::FractionalWaveguideNetwork network;
    s3g::FractionalWaveguideParams params;
    params.order = 3u;
    params.decaySeconds = 12.0f;
    params.absorption = 0.0f;
    params.junctionNonlinearity = 0.28f;
    params.radiation = 0.0f;
    params.outputGainDb = 12.0f;
    network.setParams(params);
    network.prepare(kSampleRate);

    RenderBlock output;
    float peak = 0.0f;
    for (uint32_t block = 0u; block < 1200u; ++block) {
        if (block % 3u == 0u) {
            network.strike((block / 3u) % network.nodeCount(), 1.0f);
        }
        network.process(nullptr, output.pointers.data(),
            static_cast<uint32_t>(output.pointers.size()), kBlockFrames);
        if (!finiteBlock(output, kBlockFrames)
            || !std::isfinite(network.travelingEnergy())) {
            std::cerr << "repeated excitation produced non-finite state\n";
            return false;
        }
        for (const auto& channel : output.storage) {
            for (float sample : channel) {
                peak = std::max(peak, std::abs(sample));
            }
        }
    }
    if (peak > 0.89126f || !(network.guardGain() < 1.0f)
        || !(network.outputPeak() > 0.0f)) {
        std::cerr << "linked HOA guard did not contain repeated excitation: "
                  << peak << " / " << network.guardGain() << "\n";
        return false;
    }
    return true;
}

} // namespace

int main()
{
    if (!fractionalDelayProbe()
        || !directionalRadiationProbe()
        || !geometryTimingProbe()
        || !repeatedExcitationProbe()) {
        return 1;
    }

    const DecayProbe decay = renderDecay();
    if (!decay.finite || !(decay.firstEnergy > 0.000001)
        || !(decay.lastEnergy < decay.firstEnergy * 0.02)
        || decay.peak > 0.89126f) {
        std::cerr << "waveguide field did not decay safely: "
                  << decay.firstEnergy << " / "
                  << decay.lastEnergy << " / "
                  << decay.peak << "\n";
        return 1;
    }

    std::cout << "fractional waveguide network smoke passed\n";
    return 0;
}
