#include "s3g_ambi_effect_gain.h"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>

namespace {

constexpr uint32_t kChannels = s3g::kAmbiEffectGainMaxChannels;
constexpr uint32_t kFrames = 512u;
constexpr double kSampleRate = 44100.0;

using Audio = std::array<std::array<float, kFrames>, kChannels>;
using Pointers = std::array<float*, kChannels>;

void render(s3g::AmbiEffectGain& gain, Pointers& input,
    Pointers& output, uint32_t blocks)
{
    for (uint32_t block = 0u; block < blocks; ++block) {
        gain.process(input.data(), output.data(), kChannels, kChannels,
            kFrames);
    }
}

void benchmark(const char* label, s3g::AmbiEffectGain& gain,
    Pointers& input, Pointers& output, uint32_t blocks)
{
    const auto start = std::chrono::steady_clock::now();
    render(gain, input, output, blocks);
    const auto stop = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double>(stop - start).count();
    double checksum = 0.0;
    for (const float* channel : output)
        for (uint32_t frame = 0u; frame < kFrames; ++frame)
            checksum += channel[frame];
    const double duration = static_cast<double>(blocks) * kFrames / kSampleRate;
    std::cout << std::fixed << std::setprecision(3)
              << label
              << " blocks=" << blocks
              << " ns_per_block=" << elapsed * 1.0e9 / blocks
              << " realtime_percent=" << elapsed / duration * 100.0
              << " transition=" << (gain.matrixTransitionActive() ? 1 : 0)
              << " checksum=" << checksum << '\n';
}

void prepare(s3g::AmbiEffectGain& gain, uint32_t order)
{
    s3g::AmbiEffectGainParams params {};
    params.order = order;
    params.gainDb = -6.0f;
    gain.setParams(params);
    gain.prepare(kSampleRate);
    gain.reset();
}

} // namespace

int main(int argc, char** argv)
{
    uint32_t blocks = 2000u;
    if (argc > 1) {
        const unsigned long parsed = std::strtoul(argv[1], nullptr, 10);
        if (parsed > 0u) blocks = static_cast<uint32_t>(parsed);
    }

    Audio input {}, output {};
    Pointers inputPointers {}, outputPointers {};
    for (uint32_t channel = 0u; channel < kChannels; ++channel) {
        inputPointers[channel] = input[channel].data();
        outputPointers[channel] = output[channel].data();
        for (uint32_t frame = 0u; frame < kFrames; ++frame) {
            input[channel][frame] = channel < 36u
                ? 0.01f * std::sin(0.013f * static_cast<float>(
                    (channel + 1u) * (frame + 1u)))
                : 0.0f;
        }
    }

    s3g::AmbiEffectGain fresh5;
    prepare(fresh5, 5u);
    render(fresh5, inputPointers, outputPointers, 200u);
    benchmark("fresh_5oa", fresh5, inputPointers, outputPointers, blocks);

    s3g::AmbiEffectGain fresh7;
    prepare(fresh7, 7u);
    render(fresh7, inputPointers, outputPointers, 200u);
    benchmark("fresh_7oa", fresh7, inputPointers, outputPointers, blocks);

    s3g::AmbiEffectGain down;
    prepare(down, 7u);
    s3g::AmbiEffectGainParams downParams {};
    downParams.order = 5u;
    downParams.gainDb = -6.0f;
    down.setParams(downParams);
    render(down, inputPointers, outputPointers, 200u);
    benchmark("transition_7_to_5", down, inputPointers, outputPointers,
        blocks);

    s3g::AmbiEffectGain up;
    prepare(up, 5u);
    s3g::AmbiEffectGainParams upParams {};
    upParams.order = 7u;
    upParams.gainDb = -6.0f;
    up.setParams(upParams);
    render(up, inputPointers, outputPointers, 200u);
    benchmark("transition_5_to_7", up, inputPointers, outputPointers,
        blocks);
    if (down.matrixTransitionActive() || up.matrixTransitionActive()) {
        std::cerr << "matrix transition failed to converge\n";
        return 1;
    }
    return 0;
}
