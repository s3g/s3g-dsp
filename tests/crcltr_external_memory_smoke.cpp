#define S3G_CRCLTR_EXTERNAL_MEMORY_ONLY 1
#include "s3g_crcltr.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>

namespace {

constexpr double kSampleRate = 1000.0;
constexpr uint32_t kBlockFrames = 25u;
constexpr uint32_t kLoopCapacity = 32000u;
constexpr uint32_t kPreRollCapacity = 101u;

std::array<float, kLoopCapacity> loop1Left {};
std::array<float, kLoopCapacity> loop1Right {};
std::array<float, kLoopCapacity> loop2Left {};
std::array<float, kLoopCapacity> loop2Right {};
std::array<float, kPreRollCapacity> preRoll1Left {};
std::array<float, kPreRollCapacity> preRoll1Right {};
std::array<float, kPreRollCapacity> preRoll2Left {};
std::array<float, kPreRollCapacity> preRoll2Right {};

} // namespace

int main()
{
    bool ok = true;
    s3g::Crcltr dsp;

    // Embedded-only builds must reject startup without caller-owned storage.
    const bool rejectedOwnedMemory = !dsp.prepare(kSampleRate, kBlockFrames);
    ok = ok && rejectedOwnedMemory;

    s3g::CrcltrMemory memory {
        loop1Left.data(), loop1Right.data(), loop2Left.data(), loop2Right.data(),
        kLoopCapacity,
        preRoll1Left.data(), preRoll1Right.data(),
        preRoll2Left.data(), preRoll2Right.data(), kPreRollCapacity,
    };
    const bool prepared = dsp.prepare(kSampleRate, kBlockFrames, &memory);
    const bool external = dsp.usingExternalMemory();
    ok = ok && prepared && external;

    s3g::CrcltrParams params;
    params.blend = 0.0f;
    dsp.setParams(params);

    std::array<float, kBlockFrames> inputLeft {};
    std::array<float, kBlockFrames> inputRight {};
    std::array<float, kBlockFrames> outputLeft {};
    std::array<float, kBlockFrames> outputRight {};
    std::fill(inputLeft.begin(), inputLeft.end(), 0.1f);
    std::fill(inputRight.begin(), inputRight.end(), -0.1f);
    dsp.process(inputLeft.data(), inputRight.data(), outputLeft.data(),
        outputRight.data(), kBlockFrames);

    const bool finiteLeft = std::all_of(outputLeft.begin(), outputLeft.end(),
        [](float sample) { return std::isfinite(sample); });
    const bool finiteRight = std::all_of(outputRight.begin(), outputRight.end(),
        [](float sample) { return std::isfinite(sample); });
    ok = ok && finiteLeft && finiteRight;

    if (!ok) {
        std::cerr << "CRCLTR external-memory smoke test failed: owned="
                  << rejectedOwnedMemory << ", prepared=" << prepared
                  << ", external=" << external << ", finite=" << finiteLeft
                  << "/" << finiteRight << '\n';
        return 1;
    }
    std::cout << "CRCLTR external-memory smoke test passed\n";
    return 0;
}
