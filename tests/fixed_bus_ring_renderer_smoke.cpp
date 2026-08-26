#include "s3g_fixed_bus_ring_renderer.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>

namespace {

bool expect(bool condition, const char* message)
{
    if (!condition) std::cerr << message << '\n';
    return condition;
}

float energy(const std::array<float, 16u>& frame)
{
    float result = 0.0f;
    for (float value : frame) result += value * value;
    return result;
}

} // namespace

int main()
{
    bool ok = true;
    s3g::FixedBusRingRenderer<> renderer;
    std::array<float, 16u> input {};
    std::array<float, 16u> output {};

    constexpr std::array<s3g::FixedBusRingFormat, 4u> formats {{
        s3g::FixedBusRingFormat::Direct16,
        s3g::FixedBusRingFormat::Ring8,
        s3g::FixedBusRingFormat::QuadRing,
        s3g::FixedBusRingFormat::StereoRing,
    }};
    constexpr std::array<uint32_t, 4u> widths {{ 16u, 8u, 4u, 2u }};

    for (uint32_t index = 0u; index < formats.size(); ++index) {
        renderer.configure(formats[index], 0.0f);
        input.fill(0.0f);
        input[0u] = 1.0f;
        output.fill(9.0f);
        renderer.processFrame(input.data(), output.data());
        ok &= expect(renderer.activeChannels() == widths[index],
            "format reported the wrong active width");
        ok &= expect(std::abs(output[0u] - 1.0f) < 1.0e-6f,
            "zero-degree ring routing changed delayed-lane energy");
        for (uint32_t channel = widths[index]; channel < output.size();
             ++channel)
            ok &= expect(output[channel] == 0.0f,
                "inactive host lane was not silent");
    }

    renderer.configure(s3g::FixedBusRingFormat::Ring8, 22.5f);
    input.fill(0.0f);
    input[0] = 1.0f;
    renderer.processFrame(input.data(), output.data());
    ok &= expect(output[0] > 0.70f && output[1] > 0.70f,
        "fractional ring rotation did not use equal-power neighbors");
    ok &= expect(std::abs(energy(output) - 1.0f) < 1.0e-5f,
        "fractional ring rotation did not preserve delayed-lane energy");

    renderer.configure(s3g::FixedBusRingFormat::QuadRing, 90.0f);
    input.fill(0.0f);
    input[0] = 1.0f;
    renderer.processFrame(input.data(), output.data());
    ok &= expect(std::abs(output[1] - 1.0f) < 1.0e-6f,
        "quarter-turn ring rotation did not advance one quad lane");

    // A late source lane must survive every fold. This is the contract that
    // distinguishes a format fold from asking the processor to emit fewer
    // delayed channels.
    for (uint32_t index = 1u; index < formats.size(); ++index) {
        renderer.configure(formats[index], 0.0f);
        input.fill(0.0f);
        input[15u] = 1.0f;
        renderer.processFrame(input.data(), output.data());
        ok &= expect(energy(output) > 0.01f,
            "a high-numbered source lane disappeared in the fold-down");
    }

    if (!ok) return 1;
    std::cout << "s3g fixed-bus ring renderer smoke: ok\n";
    return 0;
}
