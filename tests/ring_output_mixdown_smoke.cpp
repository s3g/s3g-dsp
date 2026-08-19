#include "s3g_ring_output_mixdown.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>

int main()
{
    std::array<float, s3g::kRingOutputChannels> input {
        0.25f, -0.50f, 0.75f, -1.0f,
        0.30f, -0.20f, 0.10f, -0.05f,
    };
    std::array<float, s3g::kRingOutputChannels> output {};
    s3g::RingOutputMixdown mixdown;

    mixdown.configure(s3g::RingOutputFormat::Direct8, 0.0f);
    mixdown.processFrame(input.data(), output.data());
    bool ok = output == input;

    mixdown.configure(s3g::RingOutputFormat::QuadRing, 37.0f);
    mixdown.processFrame(input.data(), output.data());
    const float quadEnergy = std::abs(output[0]) + std::abs(output[1])
        + std::abs(output[2]) + std::abs(output[3]);
    ok = ok && quadEnergy > 0.0001f
        && std::all_of(output.begin() + 4, output.end(),
            [](float value) { return value == 0.0f; });

    mixdown.configure(s3g::RingOutputFormat::StereoRing, -83.0f);
    mixdown.processFrame(input.data(), output.data());
    const float stereoEnergy = std::abs(output[0]) + std::abs(output[1]);
    ok = ok && stereoEnergy > 0.0001f
        && std::all_of(output.begin() + 2, output.end(),
            [](float value) { return value == 0.0f; });

    input.fill(0.0f);
    input[0] = 1.0f;
    std::array<float, s3g::kRingOutputChannels> unrotated {};
    std::array<float, s3g::kRingOutputChannels> rotated {};
    mixdown.configure(s3g::RingOutputFormat::StereoRing, 0.0f);
    mixdown.processFrame(input.data(), unrotated.data());
    mixdown.configure(s3g::RingOutputFormat::StereoRing, 90.0f);
    mixdown.processFrame(input.data(), rotated.data());
    ok = ok && (std::abs(unrotated[0] - rotated[0]) > 0.01f
        || std::abs(unrotated[1] - rotated[1]) > 0.01f);

    if (!ok) {
        std::cerr << "Ring output mixdown smoke failed\n";
        return 1;
    }
    std::cout << "Ring output mixdown smoke passed\n";
    return 0;
}
