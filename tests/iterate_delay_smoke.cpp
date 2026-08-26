#include "s3g_iterate_delay.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

int main()
{
    constexpr double sampleRate = 1000.0;
    constexpr uint32_t frames = 4000u;
    s3g::IterateDelay iterate;
    bool ok = iterate.prepare(sampleRate, frames);
    s3g::IterateDelayParams params;
    params.delayMs = 80.0f;
    params.delayRandom = 0.35f;
    params.windowMs = 120.0f;
    params.pitchRandomSemitones = 4.0f;
    params.fade = 0.12f;
    params.dry = 0.0f;
    params.seed = 12345u;
    iterate.setParams(params);
    iterate.setOutputChannels(8u);

    std::vector<float> left(frames);
    std::vector<float> right(frames);
    for (uint32_t frame = 0u; frame < frames; ++frame) {
        left[frame] = 0.2f * std::sin(0.071f * frame);
        right[frame] = 0.2f * std::cos(0.053f * frame);
    }
    std::array<std::vector<float>, 16u> output;
    std::array<float*, 16u> pointers {};
    for (uint32_t channel = 0u; channel < output.size(); ++channel) {
        output[channel].assign(frames, 0.0f);
        pointers[channel] = output[channel].data();
    }
    iterate.process(left.data(), right.data(), pointers.data(), frames);

    uint32_t activeMask = 0u;
    for (uint32_t channel = 0u; channel < 8u; ++channel) {
        double energy = 0.0;
        for (float value : output[channel]) {
            ok = ok && std::isfinite(value);
            energy += value * value;
        }
        if (energy > 1.0e-7) activeMask |= 1u << channel;
    }
    ok = ok && activeMask == 0xffu;
    for (uint32_t channel = 8u; channel < output.size(); ++channel)
        for (float value : output[channel]) ok = ok && value == 0.0f;

    if (!ok) {
        std::cerr << "Iterate Delay smoke failed\n";
        return 1;
    }
    std::cout << "Iterate Delay smoke passed\n";
    return 0;
}
